// Compares the device per-seed candidate selection (mkfitdev::selectCandidates) with STOCK mkFit's
// CandCloner::processSeedRange on records dumped from a stock HLT job (instrumented private MkFitCore,
// see doc/cands.txt). Usage: <binary> <dump file or directory>... [--batch N] [--verbose N]
// Without arguments it prints the usage and exits 0 (so that scram's unit-test run passes).

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/devices.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandOptions.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandsHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/alpaka/CandSelect.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/alpaka/CandsDeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/test/cands_dump_format.h"

using namespace ALPAKA_ACCELERATOR_NAMESPACE;
namespace md = ::mkfitdev;
namespace dv = ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev;

namespace {

  struct Record {
    mkfitdump::Header h;
    std::vector<mkfitdump::Cand> cin, ext, cout;
    mkfitdump::Cand bsin, bsout;
    std::vector<mkfitdump::Option> opts, sorted;
    std::vector<mkfitdump::Update> upd, ovl;
    std::vector<mkfitdump::HoT> hots;
  };

  template <class T>
  bool rd(std::ifstream& f, T& v) {
    return bool(f.read(reinterpret_cast<char*>(&v), sizeof(T)));
  }
  template <class T>
  bool rdv(std::ifstream& f, std::vector<T>& v) {
    int32_t n;
    if (!rd(f, n) || n < 0 || n > 100000)
      return false;
    v.resize(n);
    return n == 0 || bool(f.read(reinterpret_cast<char*>(v.data()), sizeof(T) * n));
  }

  bool readRecord(std::ifstream& f, Record& r) {
    if (!rd(f, r.h))
      return false;
    if (r.h.magic != mkfitdump::kRecordMagic) {
      fprintf(stderr, "bad magic\n");
      return false;
    }
    return rdv(f, r.cin) && rdv(f, r.ext) && rd(f, r.bsin) && rdv(f, r.opts) && rdv(f, r.sorted) && rdv(f, r.cout) &&
           rd(f, r.bsout) && rdv(f, r.upd) && rdv(f, r.ovl) && rdv(f, r.hots);
  }

  md::CandBook toBook(const mkfitdump::Cand& c) {
    md::CandBook b;
    b.score = c.score;
    b.chi2 = c.chi2;
    b.lastHitIdx = c.lastHitIdx;
    b.nFound = c.nFound;
    b.nMissing = c.nMissing;
    b.nOverlap = c.nOverlap;
    b.nInsideMinusOne = c.nInsideMinusOne;
    b.nTailMinusOne = c.nTailMinusOne;
    b.originIndex = c.originIndex;
    for (int i = 0; i < 2; ++i)
      b.overlaps.M[i] = md::HitMatch{c.ovHit[i], c.ovModule[i], c.ovChi2[i]};
    return b;
  }

  bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

  // Bookkeeping equality except lastHitIdx (node numbering differs by design; chains are compared instead).
  bool sameBook(const md::CandBook& d, const mkfitdump::Cand& s) {
    bool ok = sameBits(d.score, s.score) && sameBits(d.chi2, s.chi2) && d.nFound == s.nFound &&
              d.nMissing == s.nMissing && d.nOverlap == s.nOverlap && d.nInsideMinusOne == s.nInsideMinusOne &&
              d.nTailMinusOne == s.nTailMinusOne && d.originIndex == s.originIndex;
    for (int i = 0; i < 2; ++i)
      ok = ok && d.overlaps.M[i].hit == s.ovHit[i] && d.overlaps.M[i].module == s.ovModule[i] &&
           sameBits(d.overlaps.M[i].chi2, s.ovChi2[i]);
    return ok;
  }

  struct Link {
    int idx, layer;
    float chi2;
    bool operator==(const Link& o) const { return idx == o.idx && layer == o.layer && sameBits(chi2, o.chi2); }
  };

  // Walk the nodes created during this step (index >= nHotsIn); returns the anchor (first old node index).
  int chainStock(const Record& r, int last, std::vector<Link>& out) {
    out.clear();
    int n = last;
    while (n >= r.h.nHotsIn) {
      const auto& h = r.hots.at(n - r.h.nHotsIn);
      out.push_back({h.index, h.layer, h.chi2});
      n = h.prev;
    }
    return n;
  }
  int chainDev(const md::CandHotsSoA::ConstView& hv, int rowBase, int nHotsIn, int last, std::vector<Link>& out) {
    out.clear();
    int n = last;
    while (n >= nHotsIn) {
      const auto& h = hv.node(rowBase + n);
      out.push_back({h.index, h.layer, h.chi2});
      n = h.prev;
    }
    return n;
  }

  struct Stats {
    long records = 0, skippedOverflow = 0, ok = 0;
    long badN = 0, badBook = 0, badChain = 0, badBest = 0, badUpd = 0, badOvl = 0, badOverflow = 0;
    long cands = 0, updates = 0, bestShortSet = 0, withExtras = 0, withOpts = 0, bkw = 0, recordsWithTies = 0;
    long badWithTies = 0;
    double kernelMs = 0;
    long optsChecked = 0, optsBad = 0, optsBadScoreOnly = 0;
  };

  // Rebuilds every dumped option from its parent's bookkeeping with the device option builders (CandOptions.h).
  // LST-step JSON values: maxHolesPerCand 4, maxConsecHoles 2 (forward and backward).
  class KernelRebuildOptions {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  md::CandSlotsSoA::ConstView slots,
                                  md::CandOptionsSoA::ConstView opts,
                                  md::CandOption* out,
                                  int n) const {
      for (int32_t k : cms::alpakatools::uniform_elements(acc, n * md::kMaxOptsPerSeed)) {
        const int r = k / md::kMaxOptsPerSeed;
        const md::CandOption& o = opts.opt(k);
        md::CandOption x = o;
        if (o.hitIdx != md::kOptEmpty) {
          const md::CandBook& c = slots.book(md::candSlotRow(r, 0, o.trkIdx));
          if (o.hitIdx >= 0) {
            x = md::makeHitOption(c, o.trkIdx, o.pt, o.hitIdx, o.module, o.chi2_hit);
          } else {
            int code = o.hitIdx;
            if (code == md::kHitMissIdx || code == md::kHitStopIdx)
              code = md::invalidHitCode(c, 4, 2, md::kWsrInside, false, 0, false);
            x = md::makeInvalidOption(c, o.trkIdx, o.pt, code);
          }
        }
        out[k] = x;
      }
    }
  };

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> files;
  int batch = 32768, verbose = 5;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--batch" && i + 1 < argc)
      batch = std::stoi(argv[++i]);
    else if (a == "--verbose" && i + 1 < argc)
      verbose = std::stoi(argv[++i]);
    else if (std::filesystem::is_directory(a)) {
      std::vector<std::string> v;
      for (auto& e : std::filesystem::directory_iterator(a))
        if (e.path().filename().string().rfind("ccdump_", 0) == 0)
          v.push_back(e.path().string());
      std::sort(v.begin(), v.end());
      files.insert(files.end(), v.begin(), v.end());
    } else
      files.push_back(a);
  }
  if (files.empty()) {
    printf("usage: %s <ccdump dir or files>... [--batch N] [--verbose N]\n", argv[0]);
    return 0;
  }

  std::vector<Record> recs;
  for (auto& fn : files) {
    std::ifstream f(fn, std::ios::binary);
    Record r;
    while (readRecord(f, r))
      recs.push_back(r);
  }
  printf("read %zu records from %zu files\n", recs.size(), files.size());

  auto const& devices = cms::alpakatools::devices<Platform>();
  if (devices.empty()) {
    printf("no device for backend %s, skipping\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE));
    return 0;
  }
  auto const& device = devices[0];
  Queue queue(device);
  printf("backend %s, device %s\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE), alpaka::getName(device).c_str());

  Stats st;
  std::vector<Link> ls, ld;

  for (size_t b0 = 0; b0 < recs.size(); b0 += batch) {
    const int n = int(std::min<size_t>(batch, recs.size() - b0));
    int hps = 0;
    for (int r = 0; r < n; ++r)
      hps = std::max(hps, recs[b0 + r].h.nHotsIn);
    hps += 4 * md::kMaxCandsPerSeed;  // room for this step's nodes

    md::SeedCandsHostCollection seedsH(queue, n);
    md::CandSlotsHostCollection slotsH(queue, n * md::kSlotsPerSeed);
    md::CandHotsHostCollection hotsH(queue, n * hps);
    md::CandOptionsHostCollection optsH(queue, n * md::kMaxOptsPerSeed);
    md::CandExtrasHostCollection extH(queue, n * md::kMaxExtrasPerSeed);
    md::CandUpdatesHostCollection updH(queue, n * md::kMaxCandsPerSeed);
    std::memset(hotsH.buffer().data(), 0xff, alpaka::getExtentProduct(hotsH.buffer()));

    auto sv = seedsH.view();
    auto slv = slotsH.view();
    auto ov = optsH.view();
    auto ev = extH.view();
    sv.hotsPerSeed() = hps;
    sv.nOverflowHots() = 0;
    sv.nOverflowOpts() = 0;
    sv.nOverflowExtras() = 0;
    std::vector<char> skip(n, 0);
    float maxPtCut = 0;
    int maxCands = 0, recheck = 0;
    for (int r = 0; r < n; ++r) {
      const Record& R = recs[b0 + r];
      maxPtCut = R.h.pTCutOverlap;
      maxCands = R.h.maxCandsPerSeed;
      recheck = R.h.recheckOverlap;
      if ((int)R.opts.size() > md::kMaxOptsPerSeed || (int)R.cin.size() > md::kMaxCandsPerSeed ||
          (int)R.ext.size() > md::kMaxExtrasPerSeed)
        skip[r] = 1;
      sv.nCands(r) = skip[r] ? 0 : R.cin.size();
      sv.curBuf(r) = 0;
      sv.state(r) = R.h.state;
      sv.layer(r) = R.h.layer;
      sv.nHots(r) = R.h.nHotsIn;
      sv.nExtras(r) = skip[r] ? 0 : R.ext.size();
      sv.bestShort(r) = toBook(R.bsin);
      sv.bestShortValid(r) = R.bsin.hasCombCand;
      sv.overflowBits(r) = 0;
      if (skip[r])
        continue;
      for (size_t ic = 0; ic < R.cin.size(); ++ic) {
        slv.book(md::candSlotRow(r, 0, ic)) = toBook(R.cin[ic]);
        auto& s = slv.state(md::candSlotRow(r, 0, ic));
        std::memset(&s, 0, sizeof(s));
        s.par[3] = 1.f / R.cin[ic].pt;  // only pT() = |1/par[3]| is read by the selection
        s.label = R.cin[ic].label;
      }
      for (size_t e = 0; e < R.ext.size(); ++e) {
        md::CandExtra x;
        x.book = toBook(R.ext[e]);
        x.stateSrc = 0;
        for (size_t ic = 0; ic < R.cin.size(); ++ic)
          if (R.cin[ic].lastHitIdx == R.ext[e].lastHitIdx && sameBits(R.cin[ic].score, R.ext[e].score))
            x.stateSrc = ic;
        ev.extra(md::extraRow(r, e)) = x;
      }
      for (int j = 0; j < md::kMaxOptsPerSeed; ++j) {
        md::CandOption o;
        if (j < (int)R.opts.size()) {
          const auto& d = R.opts[j];
          o = md::CandOption{d.module, d.hitIdx, d.trkIdx, d.nhits, d.ntailholes, d.noverlaps, d.nholes,
                             d.pt,     d.chi2,   d.chi2_hit, d.score};
        } else {
          std::memset(&o, 0, sizeof(o));
          o.hitIdx = md::kOptEmpty;
        }
        ov.opt(md::optRow(r, j)) = o;
      }
    }

    dv::SeedCandsDeviceCollection seedsD(queue, n);
    dv::CandSlotsDeviceCollection slotsD(queue, n * md::kSlotsPerSeed);
    dv::CandHotsDeviceCollection hotsD(queue, n * hps);
    dv::CandOptionsDeviceCollection optsD(queue, n * md::kMaxOptsPerSeed);
    dv::CandExtrasDeviceCollection extD(queue, n * md::kMaxExtrasPerSeed);
    dv::CandUpdatesDeviceCollection updD(queue, n * md::kMaxCandsPerSeed);
    alpaka::memcpy(queue, seedsD.buffer(), seedsH.buffer());
    alpaka::memcpy(queue, slotsD.buffer(), slotsH.buffer());
    alpaka::memcpy(queue, hotsD.buffer(), hotsH.buffer());
    alpaka::memcpy(queue, optsD.buffer(), optsH.buffer());
    alpaka::memcpy(queue, extD.buffer(), extH.buffer());
    alpaka::wait(queue);

    md::SeedSelParams params{-1, maxCands, maxPtCut, recheck != 0};
    auto t0 = std::chrono::steady_clock::now();
    dv::selectCandidates(
        queue, seedsD.view(), slotsD.view(), hotsD.view(), optsD.const_view(), extD.const_view(), updD.view(), params, n);
    alpaka::wait(queue);
    st.kernelMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    auto optRebD = cms::alpakatools::make_device_buffer<md::CandOption[]>(queue, n * md::kMaxOptsPerSeed);
    auto optRebH = cms::alpakatools::make_host_buffer<md::CandOption[]>(queue, n * md::kMaxOptsPerSeed);
    alpaka::exec<Acc1D>(queue,
                        cms::alpakatools::make_workdiv<Acc1D>(
                            cms::alpakatools::divide_up_by(n * md::kMaxOptsPerSeed, 128), 128),
                        KernelRebuildOptions{},
                        slotsD.const_view(),
                        optsD.const_view(),
                        optRebD.data(),
                        n);
    alpaka::memcpy(queue, optRebH, optRebD);
    alpaka::memcpy(queue, seedsH.buffer(), seedsD.buffer());
    alpaka::memcpy(queue, slotsH.buffer(), slotsD.buffer());
    alpaka::memcpy(queue, hotsH.buffer(), hotsD.buffer());
    alpaka::memcpy(queue, updH.buffer(), updD.buffer());
    alpaka::wait(queue);

    auto csv = seedsH.const_view();
    auto cslv = slotsH.const_view();
    auto chv = hotsH.const_view();
    auto cuv = updH.const_view();
    for (int r = 0; r < n; ++r) {
      const Record& R = recs[b0 + r];
      ++st.records;
      if (skip[r]) {
        ++st.skippedOverflow;
        continue;
      }
      for (int j = 0; j < (int)R.opts.size() && j < md::kMaxOptsPerSeed; ++j) {
        const auto& d = R.opts[j];
        const md::CandOption& x = optRebH[r * md::kMaxOptsPerSeed + j];
        const md::CandOption ref{d.module, d.hitIdx, d.trkIdx, d.nhits, d.ntailholes, d.noverlaps, d.nholes,
                                 d.pt,     d.chi2,   d.chi2_hit, d.score};
        ++st.optsChecked;
        if (std::memcmp(&x, &ref, sizeof(ref)) != 0) {
          ++st.optsBad;
          md::CandOption y = x;
          y.score = ref.score;
          if (std::memcmp(&y, &ref, sizeof(ref)) == 0)
            ++st.optsBadScoreOnly;
        }
      }
      bool ties = false;
      for (size_t i = 0; i < R.opts.size() && !ties; ++i)
        for (size_t j = i + 1; j < R.opts.size(); ++j)
          if (R.opts[i].score == R.opts[j].score) {
            ties = true;
            break;
          }
      st.recordsWithTies += ties;
      st.withExtras += !R.ext.empty();
      st.withOpts += !R.opts.empty();
      st.bkw += R.h.bkwRep;
      const int buf = csv.curBuf(r);
      const int nOut = csv.nCands(r);
      const int rowBase = r * hps;
      bool ok = true;
      std::string why;
      if (csv.overflowBits(r) != 0) {
        ++st.badOverflow;
        ok = false;
        why += " overflow";
      }
      if (nOut != (int)R.cout.size()) {
        ++st.badN;
        ok = false;
        why += " nCands";
      } else {
        bool bb = true, bc = true;
        for (int k = 0; k < nOut; ++k) {
          const auto& d = cslv.book(md::candSlotRow(r, buf, k));
          bb = bb && sameBook(d, R.cout[k]);
          const int as = chainStock(R, R.cout[k].lastHitIdx, ls);
          const int ad = chainDev(chv, rowBase, R.h.nHotsIn, d.lastHitIdx, ld);
          bc = bc && as == ad && ls == ld;
        }
        st.cands += nOut;
        if (!bb) {
          ++st.badBook;
          ok = false;
          why += " book";
        }
        if (!bc) {
          ++st.badChain;
          ok = false;
          why += " chain";
        }
      }
      {  // best short
        bool good = (bool)csv.bestShortValid(r) == (bool)R.bsout.hasCombCand;
        if (good && R.bsout.hasCombCand) {
          good = sameBook(csv.bestShort(r), R.bsout);
          const int as = chainStock(R, R.bsout.lastHitIdx, ls);
          const int ad = chainDev(chv, rowBase, R.h.nHotsIn, csv.bestShort(r).lastHitIdx, ld);
          good = good && as == ad && ls == ld;
        }
        if (!sameBits(R.bsout.score, R.bsin.score) || R.bsout.lastHitIdx != R.bsin.lastHitIdx)
          ++st.bestShortSet;
        if (!good) {
          ++st.badBest;
          ok = false;
          why += " bestShort";
        }
      }
      {  // update lists
        bool good = csv.nUpdates(r) == (int)R.upd.size();
        for (int u = 0; good && u < (int)R.upd.size(); ++u) {
          const auto& d = cuv.upd(md::updRow(r, u));
          good = d.cand_idx == R.upd[u].cand_idx && d.hit_idx == R.upd[u].hit_idx && d.ovlp_idx == R.upd[u].ovlp_idx;
        }
        st.updates += R.upd.size();
        if (!good) {
          ++st.badUpd;
          ok = false;
          why += " upd";
        }
        bool g2 = csv.nOverlapUpdates(r) == (int)R.ovl.size();
        for (int u = 0; g2 && u < (int)R.ovl.size(); ++u) {
          const auto& d = cuv.ovl(md::updRow(r, u));
          g2 = d.cand_idx == R.ovl[u].cand_idx && d.hit_idx == R.ovl[u].hit_idx && d.ovlp_idx == R.ovl[u].ovlp_idx;
        }
        if (!g2) {
          ++st.badOvl;
          ok = false;
          why += " ovl";
        }
      }
      if (ok)
        ++st.ok;
      else {
        st.badWithTies += ties;
        if (verbose-- > 0)
          printf("MISMATCH record %ld seed %d layer %d state %d bkw %d nOpts %zu nExt %zu nIn %zu nOut stock %zu dev %d:%s\n",
                 long(b0 + r),
                 R.h.seedIdx,
                 R.h.layer,
                 R.h.state,
                 R.h.bkwRep,
                 R.opts.size(),
                 R.ext.size(),
                 R.cin.size(),
                 R.cout.size(),
                 nOut,
                 why.c_str());
      }
    }
  }

  printf("RESULT records %ld  identical %ld  mismatching %ld  skipped(capacity) %ld\n",
         st.records,
         st.ok,
         st.records - st.ok - st.skippedOverflow,
         st.skippedOverflow);
  printf("  mismatch kinds: nCands %ld book %ld chain %ld bestShort %ld updList %ld ovlList %ld overflow %ld"
         " (mismatching records with exact score ties: %ld)\n",
         st.badN,
         st.badBook,
         st.badChain,
         st.badBest,
         st.badUpd,
         st.badOvl,
         st.badOverflow,
         st.badWithTies);
  printf("  coverage: kept candidates %ld, update entries %ld, best-short changes %ld, records with options %ld,"
         " with extras %ld, backward-search records %ld, records with option score ties %ld\n",
         st.cands,
         st.updates,
         st.bestShortSet,
         st.withOpts,
         st.withExtras,
         st.bkw,
         st.recordsWithTies);
  printf("  option rebuild (CandOptions.h) from parent bookkeeping: options %ld, bitwise different %ld (score bits only %ld)\n",
         st.optsChecked,
         st.optsBad,
         st.optsBadScoreOnly);
  printf("  kernel wall time (all batches, incl. launch) %.3f ms\n", st.kernelMs);
  return (st.records - st.ok - st.skippedOverflow) == 0 && st.skippedOverflow == 0 && st.optsBad == 0 ? 0 : 1;
}
