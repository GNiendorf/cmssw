// Compares the device per-seed bookkeeping operations (interface/cands/CandSeedOps.h) with STOCK mkFit on
// CombCandidate snapshots dumped before/after each operation by the instrumented private MkFitCore
// (files ccsnap_*.bin, kinds: 1 unroll/activate, 2 mergeCandsAndBestShortOne, 3 compactifyHitStorageForBestCand,
// 4 beginBkwSearch, 5 repackCandPostBkwSearch, 6 filter_comb_cands per seed). One device thread per record.
// Usage: <binary> <dump dir or files>... ; no arguments: usage, exit 0.

#include <algorithm>
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
#include <map>
#include <tuple>

#include "RecoTracker/MkFitAlpaka/interface/cands/CandSeedOps.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandsHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/alpaka/CandsDeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/alpaka/CandSeedOpsLaunch.h"
#include "RecoTracker/MkFitAlpaka/test/cands_dump_format.h"

using namespace ALPAKA_ACCELERATOR_NAMESPACE;
namespace md = ::mkfitdev;
namespace dv = ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev;

namespace {

  struct CandFull {
    mkfitdump::Cand c;
    float par[6];
    float posPhi, posRsq;
    int32_t nSeedHits, lastHitIndex, lastHitLayer;
  };
  struct Snap {
    int32_t state, pickup, hotsSize, lastBefore, insideBefore, tailBefore;
    CandFull best;
    std::vector<CandFull> cands;
    std::vector<mkfitdump::HoT> hots;
  };
  struct SRec {
    int32_t kind, seed, i0, i1, i2, i3;
    float f0;
    Snap before, after;
    std::vector<mkfitdump::HoT> newHots;
    std::vector<int32_t> active;
    std::vector<float> errs;  // kind 6: 21 error elements per input cand
    int32_t passed = 0;       // kind 6
  };

  template <class T>
  bool rd(std::ifstream& f, T& v) {
    return bool(f.read(reinterpret_cast<char*>(&v), sizeof(T)));
  }
  template <class T>
  bool rdv(std::ifstream& f, std::vector<T>& v) {
    int32_t n;
    if (!rd(f, n) || n < 0 || n > 1000000)
      return false;
    v.resize(n);
    return n == 0 || bool(f.read(reinterpret_cast<char*>(v.data()), sizeof(T) * n));
  }
  bool rdSnap(std::ifstream& f, Snap& s) {
    return rd(f, s.state) && rd(f, s.pickup) && rd(f, s.hotsSize) && rd(f, s.lastBefore) && rd(f, s.insideBefore) &&
           rd(f, s.tailBefore) && rd(f, s.best) && rdv(f, s.cands) && rdv(f, s.hots);
  }
  bool readRec(std::ifstream& f, SRec& r) {
    uint32_t magic;
    if (!rd(f, magic))
      return false;
    if (magic != 0x31534343u) {
      fprintf(stderr, "bad snapshot magic\n");
      return false;
    }
    if (!(rd(f, r.kind) && rd(f, r.seed) && rd(f, r.i0) && rd(f, r.i1) && rd(f, r.i2) && rd(f, r.i3) && rd(f, r.f0)))
      return false;
    if (!rdSnap(f, r.before))
      return false;
    r.errs.clear();
    if (r.kind == 6) {
      int32_t nc;
      if (!rd(f, nc) || nc < 0 || nc > 64)
        return false;
      r.errs.resize(21 * nc);
      if (nc > 0 && !f.read(reinterpret_cast<char*>(r.errs.data()), sizeof(float) * 21 * nc))
        return false;
      if (!rd(f, r.passed))
        return false;
    }
    if (!rdSnap(f, r.after))
      return false;
    r.newHots.clear();
    r.active.clear();
    if (r.kind == 1)
      return rdv(f, r.newHots) && rdv(f, r.active);
    return true;
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
  md::CandState toState(const CandFull& f) {
    md::CandState s;
    std::memset(&s, 0, sizeof(s));
    for (int i = 0; i < 6; ++i)
      s.par[i] = f.par[i];
    s.label = f.c.label;
    s.nSeedHits = f.nSeedHits;
    return s;
  }
  bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }
  bool sameBook(const md::CandBook& d, const mkfitdump::Cand& s, bool checkScore = true) {
    bool ok = (!checkScore || sameBits(d.score, s.score)) && sameBits(d.chi2, s.chi2) && d.lastHitIdx == s.lastHitIdx &&
              d.nFound == s.nFound && d.nMissing == s.nMissing && d.nOverlap == s.nOverlap &&
              d.nInsideMinusOne == s.nInsideMinusOne && d.nTailMinusOne == s.nTailMinusOne &&
              d.originIndex == s.originIndex;
    for (int i = 0; i < 2; ++i)
      ok = ok && d.overlaps.M[i].hit == s.ovHit[i] && d.overlaps.M[i].module == s.ovModule[i] &&
           sameBits(d.overlaps.M[i].chi2, s.ovChi2[i]);
    return ok;
  }

  // Flat per-record device record
  struct TSeed {
    int32_t kind, i0, i1, i2, i3;
    float f0;
    int8_t state, bsValid;
    int16_t pickup, lb, ib, tb;
    int32_t nCands, nHots, hotBase, hotOffset, hotCap, nActive, passed;
    uint32_t ovf;
    int32_t active[md::kMaxCandsPerSeed];
    md::CandBook bs;
    md::CandState bsState;
    float ptBest;
    md::CandBook cands[md::kMaxCandsPerSeed];
    md::CandState states[md::kMaxCandsPerSeed];
    md::CandKin kin[md::kMaxCandsPerSeed];
    float pt[md::kMaxCandsPerSeed];
  };

  class KernelSeedOps {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, TSeed* recs, md::HoTNode* pool, int n) const {
      for (int32_t i : cms::alpakatools::uniform_elements(acc, n)) {
        TSeed& t = recs[i];
        md::SeedCandsRef r;
        r.state = &t.state;
        r.pickupLayer = &t.pickup;
        r.cands = t.cands;
        r.states = t.states;
        r.nCands = &t.nCands;
        r.bestShort = &t.bs;
        r.bestShortState = &t.bsState;
        r.bestShortValid = &t.bsValid;
        r.hots = pool + t.hotBase;
        r.hotOffset = t.hotOffset;
        r.hotCap = t.hotCap;
        r.nHots = &t.nHots;
        r.lastHitIdxBeforeBkw = &t.lb;
        r.nInsideMinusOneBeforeBkw = &t.ib;
        r.nTailMinusOneBeforeBkw = &t.tb;
        r.overflowBits = &t.ovf;
        switch (t.kind) {
          case 1:
            md::activateSeedCands(r, t.kin, t.i0, t.i1, t.i2 != 0, t.i3 == 0, t.f0, t.active, &t.nActive);
            break;
          case 2:
            md::mergeCandsAndBestShortOne(r, t.i2, t.i0 != 0, t.i1 != 0, t.pt, t.ptBest);
            break;
          case 3:
            md::compactifyHitStorageForBestCand(r, t.i0 != 0, t.i1);
            break;
          case 4:
            md::beginBkwSearch(r);
            break;
          case 5:
            md::repackCandPostBkwSearch(r, t.i0);
            break;
          case 6:
            t.passed = md::filterSeedCands(r, t.i0 != 0, t.i1 != 0, t.i2) ? 1 : 0;
            break;
          default:
            break;
        }
      }
    }
  };

  // Compares the device result t (pool p, node n at p[n - off], n < cap) with the stock after-snapshot.
  std::string compareRec(const SRec& R, const TSeed& t, const md::HoTNode* p, int off, int cap, bool& scoreBitsOnly) {
    const Snap& A = R.after;
    std::string why;
    if (t.state != A.state)
      why += " state";
    if (t.pickup != A.pickup)
      why += " pickup";
    if (t.nHots != A.hotsSize)
      why += " nHots";
    if (t.lb != A.lastBefore || t.ib != A.insideBefore || t.tb != A.tailBefore)
      why += " bkwSaved";
    if ((bool)t.bsValid != (bool)A.best.c.hasCombCand || !sameBits(t.bs.score, A.best.c.score))
      why += " bestShort";
    if (t.nCands != (int)A.cands.size())
      why += " nCands", scoreBitsOnly = false;
    else
      for (int ic = 0; ic < t.nCands; ++ic) {
        if (!sameBook(t.cands[ic], A.cands[ic].c)) {
          if (sameBook(t.cands[ic], A.cands[ic].c, false))
            why += " score";
          else {
            why += " cand";
            scoreBitsOnly = false;
          }
        }
        for (int k = 0; k < 6; ++k)
          if (!sameBits(t.states[ic].par[k], A.cands[ic].par[k])) {
            why += " state-par";
            scoreBitsOnly = false;
            break;
          }
      }
    if (R.kind == 1) {
      if (t.nActive != (int)R.active.size())
        why += " nActive";
      else
        for (int k = 0; k < t.nActive; ++k)
          if (t.active[k] != R.active[k])
            why += " active";
      for (size_t k = 0; k < R.newHots.size(); ++k) {
        const auto& h = R.newHots[k];
        const int idx = R.before.hotsSize + (int)k;
        const md::HoTNode& d = p[idx - off];
        if (idx >= cap || d.index != h.index || d.layer != h.layer || !sameBits(d.chi2, h.chi2) || d.prev != h.prev) {
          why += " newHot";
          break;
        }
      }
    } else if (R.kind != 2) {
      for (int k = 0; k < A.hotsSize && k < (int)A.hots.size(); ++k) {
        const auto& h = A.hots[k];
        const md::HoTNode& d = p[k];
        if (d.index != h.index || d.layer != h.layer || !sameBits(d.chi2, h.chi2) || d.prev != h.prev) {
          why += " hots";
          break;
        }
      }
    }
    if (R.kind == 6 && (t.passed != 0) != (R.passed != 0))
      why += " passed";
    if (t.ovf)
      why += " overflow";
    return why;
  }

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (std::filesystem::is_directory(a)) {
      std::vector<std::string> v;
      for (auto& e : std::filesystem::directory_iterator(a))
        if (e.path().filename().string().rfind("ccsnap_", 0) == 0)
          v.push_back(e.path().string());
      std::sort(v.begin(), v.end());
      files.insert(files.end(), v.begin(), v.end());
    } else
      files.push_back(a);
  }
  if (files.empty()) {
    printf("usage: %s <ccsnap dir or files>...\n", argv[0]);
    return 0;
  }
  std::vector<SRec> recs;
  for (auto& fn : files) {
    std::ifstream f(fn, std::ios::binary);
    SRec r;
    while (readRec(f, r))
      recs.push_back(r);
  }
  printf("read %zu snapshot records from %zu files\n", recs.size(), files.size());

  auto const& devices = cms::alpakatools::devices<Platform>();
  if (devices.empty()) {
    printf("no device for backend %s, skipping\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE));
    return 0;
  }
  Queue queue(devices[0]);
  printf("backend %s, device %s\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE), alpaka::getName(devices[0]).c_str());

  const char* kname[7] = {"", "activate(unroll)", "mergeCandsAndBestShortOne", "compactifyHitStorage", "beginBkwSearch",
                          "repackCandPostBkwSearch", "filter_comb_cands(seed)"};
  long nk[7] = {0}, okk[7] = {0}, scoreOnly[7] = {0}, nFail = 0;
  int printed = 0;
  const size_t batch = 65536;
  for (size_t b0 = 0; b0 < recs.size(); b0 += batch) {
    const int n = int(std::min(batch, recs.size() - b0));
    auto hrec = cms::alpakatools::make_host_buffer<TSeed[]>(queue, n);
    std::vector<int> base(n + 1, 0);
    std::vector<int> off(n, 0), cap(n, 0);
    for (int i = 0; i < n; ++i) {
      const SRec& R = recs[b0 + i];
      int lo = 0, hi = R.before.hotsSize + 2 * md::kMaxCandsPerSeed;
      if (R.kind == 1) {
        lo = R.before.hotsSize;
        for (auto& c : R.before.cands)
          lo = std::min(lo, c.c.lastHitIdx);
      } else if (R.kind == 2) {
        lo = hi = 0;
      }
      off[i] = lo;
      cap[i] = hi;
      base[i + 1] = base[i] + (hi - lo);
    }
    auto hpool = cms::alpakatools::make_host_buffer<md::HoTNode[]>(queue, std::max(1, base[n]));
    std::memset(hpool.data(), 0xff, sizeof(md::HoTNode) * std::max(1, base[n]));
    for (int i = 0; i < n; ++i) {
      const SRec& R = recs[b0 + i];
      TSeed& t = hrec[i];
      std::memset(&t, 0, sizeof(TSeed));
      t.kind = R.kind;
      t.i0 = R.i0;
      t.i1 = R.i1;
      t.i2 = R.i2;
      t.i3 = R.i3;
      t.f0 = R.f0;
      t.state = R.before.state;
      t.pickup = R.before.pickup;
      t.lb = R.before.lastBefore;
      t.ib = R.before.insideBefore;
      t.tb = R.before.tailBefore;
      t.nCands = std::min<int>(R.before.cands.size(), md::kMaxCandsPerSeed);
      t.nHots = R.before.hotsSize;
      t.hotBase = base[i];
      t.hotOffset = off[i];
      t.hotCap = cap[i];
      t.bs = toBook(R.before.best.c);
      t.bsState = toState(R.before.best);
      t.bsValid = R.before.best.c.hasCombCand;
      t.ptBest = R.before.best.c.pt;
      for (int ic = 0; ic < t.nCands; ++ic) {
        const CandFull& c = R.before.cands[ic];
        t.cands[ic] = toBook(c.c);
        t.states[ic] = toState(c);
        if (R.kind == 6 && (int)R.errs.size() >= 21 * (ic + 1))
          for (int e = 0; e < 21; ++e)
            t.states[ic].err[e] = R.errs[21 * ic + e];
        t.pt[ic] = c.c.pt;
        t.kin[ic] = md::CandKin{c.c.pt, c.posRsq, c.posPhi, c.par[4]};
      }
      md::HoTNode* p = hpool.data() + base[i];
      if (R.kind == 1) {
        for (auto& c : R.before.cands)
          if (c.c.lastHitIdx >= off[i] && c.c.lastHitIdx < cap[i])
            p[c.c.lastHitIdx - off[i]] = md::HoTNode{c.lastHitIndex, c.lastHitLayer, 0.f, -1};
      } else if (R.kind != 2) {
        for (int k = 0; k < (int)R.before.hots.size() && k < cap[i]; ++k) {
          const auto& h = R.before.hots[k];
          p[k] = md::HoTNode{h.index, h.layer, h.chi2, h.prev};
        }
      }
    }
    auto drec = cms::alpakatools::make_device_buffer<TSeed[]>(queue, n);
    auto dpool = cms::alpakatools::make_device_buffer<md::HoTNode[]>(queue, std::max(1, base[n]));
    alpaka::memcpy(queue, drec, hrec);
    alpaka::memcpy(queue, dpool, hpool);
    const uint32_t threads = 128;
    alpaka::exec<Acc1D>(queue,
                        cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(n, threads), threads),
                        KernelSeedOps{},
                        drec.data(),
                        dpool.data(),
                        n);
    alpaka::memcpy(queue, hrec, drec);
    alpaka::memcpy(queue, hpool, dpool);
    alpaka::wait(queue);

    for (int i = 0; i < n; ++i) {
      const SRec& R = recs[b0 + i];
      const TSeed& t = hrec[i];
      const md::HoTNode* p = hpool.data() + base[i];
      ++nk[R.kind];
      bool scoreBitsOnly = true;
      const std::string why = compareRec(R, t, p, off[i], cap[i], scoreBitsOnly);
      if (why.empty())
        ++okk[R.kind];
      else {
        if (why == " score" || (scoreBitsOnly && why.find("score") != std::string::npos))
          ++scoreOnly[R.kind];
        if (printed++ < 10)
          printf("MISMATCH kind %d (%s) seed %d nCandsIn %zu:%s\n", R.kind, kname[R.kind], R.seed, R.before.cands.size(),
                 why.c_str());
      }
    }
  }
  // ---- the same records through the SoA kernels of src/alpaka/cands/CandSeedOpsKernels.h ----
  long sk[7] = {0}, sok[7] = {0};
  for (int kind = 1; kind <= 6; ++kind) {
    if (kind == 5)
      continue;  // repack has no kernel of its own (it runs inside the filter kernel)
    std::map<std::tuple<int, int, int>, std::vector<size_t>> groups;
    for (size_t i = 0; i < recs.size(); ++i) {
      const SRec& R = recs[i];
      if (R.kind != kind)
        continue;
      int f0bits;
      std::memcpy(&f0bits, &R.f0, 4);
      const auto key = kind == 1   ? std::make_tuple(R.i2, int(R.i3 == 0), f0bits)
                       : kind == 2 ? std::make_tuple(R.i0, R.i1, R.i2)
                       : kind == 3 ? std::make_tuple(R.i0, R.i1, 0)
                       : kind == 6 ? std::make_tuple(R.i0, R.i1, R.i2)
                                   : std::make_tuple(0, 0, 0);
      groups[key].push_back(i);
    }
    for (auto& [key, idxs] : groups) {
      for (size_t c0 = 0; c0 < idxs.size(); c0 += 16384) {
        const int n = int(std::min<size_t>(16384, idxs.size() - c0));
        int hps = 0;
        for (int r = 0; r < n; ++r)
          hps = std::max(hps, recs[idxs[c0 + r]].before.hotsSize);
        hps += 2 * md::kMaxCandsPerSeed;
        md::SeedCandsHostCollection seedsH(queue, n);
        md::CandSlotsHostCollection slotsH(queue, n * md::kSlotsPerSeed);
        md::CandHotsHostCollection hotsH(queue, n * hps);
        md::CandOptionsHostCollection optsH(queue, n * md::kMaxOptsPerSeed);
        auto kinH = cms::alpakatools::make_host_buffer<md::CandKin[]>(queue, n * md::kMaxCandsPerSeed);
        auto layH = cms::alpakatools::make_host_buffer<int16_t[]>(queue, n);
        auto prevH = cms::alpakatools::make_host_buffer<int16_t[]>(queue, n);
        std::memset(hotsH.buffer().data(), 0xff, alpaka::getExtentProduct(hotsH.buffer()));
        auto sv = seedsH.view();
        auto slv = slotsH.view();
        auto hv = hotsH.view();
        sv.hotsPerSeed() = hps;
        sv.nOverflowHots() = 0;
        sv.nOverflowOpts() = 0;
        sv.nOverflowExtras() = 0;
        for (int r = 0; r < n; ++r) {
          const SRec& R = recs[idxs[c0 + r]];
          const int nc = std::min<int>(R.before.cands.size(), md::kMaxCandsPerSeed);
          sv.nCands(r) = nc;
          sv.curBuf(r) = 0;
          sv.state(r) = R.before.state;
          sv.pickupLayer(r) = R.before.pickup;
          sv.layer(r) = -1;
          sv.region(r) = r;
          sv.nHots(r) = R.before.hotsSize;
          sv.nExtras(r) = 0;
          sv.bestShort(r) = toBook(R.before.best.c);
          sv.bestShortValid(r) = R.before.best.c.hasCombCand;
          sv.lastHitIdxBeforeBkw(r) = R.before.lastBefore;
          sv.nInsideMinusOneBeforeBkw(r) = R.before.insideBefore;
          sv.nTailMinusOneBeforeBkw(r) = R.before.tailBefore;
          sv.overflowBits(r) = 0;
          slv.state(md::bestShortRow(r)) = toState(R.before.best);
          for (int ic = 0; ic < nc; ++ic) {
            const CandFull& c = R.before.cands[ic];
            slv.book(md::candSlotRow(r, 0, ic)) = toBook(c.c);
            slv.state(md::candSlotRow(r, 0, ic)) = toState(c);
            if (kind == 6 && (int)R.errs.size() >= 21 * (ic + 1))
              for (int e = 0; e < 21; ++e)
                slv.state(md::candSlotRow(r, 0, ic)).err[e] = R.errs[21 * ic + e];
            kinH[r * md::kMaxCandsPerSeed + ic] = md::CandKin{c.c.pt, c.posRsq, c.posPhi, c.par[4]};
          }
          if (kind == 1) {
            for (auto& c : R.before.cands)
              if (c.c.lastHitIdx >= 0 && c.c.lastHitIdx < hps)
                hv.node(md::hotRow(r, c.c.lastHitIdx, hps)) = md::HoTNode{c.lastHitIndex, c.lastHitLayer, 0.f, -1};
          } else {
            for (int k = 0; k < (int)R.before.hots.size() && k < hps; ++k) {
              const auto& h = R.before.hots[k];
              hv.node(md::hotRow(r, k, hps)) = md::HoTNode{h.index, h.layer, h.chi2, h.prev};
            }
          }
          layH[r] = R.i0;
          prevH[r] = R.i1;
        }
        dv::SeedCandsDeviceCollection seedsD(queue, n);
        dv::CandSlotsDeviceCollection slotsD(queue, n * md::kSlotsPerSeed);
        dv::CandHotsDeviceCollection hotsD(queue, n * hps);
        dv::CandOptionsDeviceCollection optsD(queue, n * md::kMaxOptsPerSeed);
        auto kinD = cms::alpakatools::make_device_buffer<md::CandKin[]>(queue, n * md::kMaxCandsPerSeed);
        auto layD = cms::alpakatools::make_device_buffer<int16_t[]>(queue, n);
        auto prevD = cms::alpakatools::make_device_buffer<int16_t[]>(queue, n);
        auto passD = cms::alpakatools::make_device_buffer<int8_t[]>(queue, n);
        auto passH = cms::alpakatools::make_host_buffer<int8_t[]>(queue, n);
        alpaka::memcpy(queue, seedsD.buffer(), seedsH.buffer());
        alpaka::memcpy(queue, slotsD.buffer(), slotsH.buffer());
        alpaka::memcpy(queue, hotsD.buffer(), hotsH.buffer());
        alpaka::memcpy(queue, kinD, kinH);
        alpaka::memcpy(queue, layD, layH);
        alpaka::memcpy(queue, prevD, prevH);
        const auto& [k0, k1, k2] = key;
        float minPtCut;
        std::memcpy(&minPtCut, &k2, 4);
        if (kind == 1)
          dv::activateSeeds(queue, seedsD.view(), slotsD.view(), hotsD.view(), optsD.view(), kinD.data(),
                                layD.data(), prevD.data(), k0 != 0, k1 != 0, minPtCut, n);
        else if (kind == 2)
          dv::mergeSeeds(queue, seedsD.view(), slotsD.view(), hotsD.view(), k2, k0 != 0, k1 != 0, n);
        else if (kind == 3)
          dv::compactifyBeginBkw(queue, seedsD.view(), slotsD.view(), hotsD.view(), k0 != 0, k1, true, false, n);
        else if (kind == 6)
          dv::filterSeeds(queue, seedsD.view(), slotsD.view(), hotsD.view(), passD.data(), k0 != 0, k1 != 0, k2, n);
        else
          dv::compactifyBeginBkw(queue, seedsD.view(), slotsD.view(), hotsD.view(), false, 0, false, true, n);
        alpaka::memcpy(queue, seedsH.buffer(), seedsD.buffer());
        alpaka::memcpy(queue, slotsH.buffer(), slotsD.buffer());
        alpaka::memcpy(queue, hotsH.buffer(), hotsD.buffer());
        if (kind == 6)
          alpaka::memcpy(queue, passH, passD);
        alpaka::wait(queue);
        auto csv = seedsH.const_view();
        auto cslv = slotsH.const_view();
        for (int r = 0; r < n; ++r) {
          const SRec& R = recs[idxs[c0 + r]];
          TSeed t;
          std::memset(&t, 0, sizeof(t));
          t.kind = kind;
          t.state = csv.state(r);
          t.pickup = csv.pickupLayer(r);
          t.nCands = csv.nCands(r);
          t.nHots = csv.nHots(r);
          t.lb = csv.lastHitIdxBeforeBkw(r);
          t.ib = csv.nInsideMinusOneBeforeBkw(r);
          t.tb = csv.nTailMinusOneBeforeBkw(r);
          t.bs = csv.bestShort(r);
          t.bsValid = csv.bestShortValid(r);
          t.ovf = csv.overflowBits(r);
          t.passed = kind == 6 ? passH[r] : 0;
          for (int ic = 0; ic < t.nCands && ic < md::kMaxCandsPerSeed; ++ic) {
            t.cands[ic] = cslv.book(md::candSlotRow(r, csv.curBuf(r), ic));
            t.states[ic] = cslv.state(md::candSlotRow(r, csv.curBuf(r), ic));
          }
          t.nActive = 0;
          for (int ic = 0; ic < md::kMaxCandsPerSeed; ++ic)
            if (csv.activeMask(r) & (1u << ic))
              t.active[t.nActive++] = ic;
          bool scoreBitsOnly = true;
          const std::string why =
              compareRec(R, t, &hotsH.const_view().node(md::hotRow(r, 0, hps)), 0, hps, scoreBitsOnly);
          ++sk[kind];
          if (why.empty())
            ++sok[kind];
          else if (printed++ < 20)
            printf("SOA MISMATCH kind %d (%s) seed %d:%s [i0..i3 %d %d %d %d, state %d->%d dev %d, nCands %zu->%zu dev %d, r %d n %d]\n",
                   kind, kname[kind], R.seed, why.c_str(), R.i0, R.i1, R.i2, R.i3, R.before.state, R.after.state,
                   t.state, R.before.cands.size(), R.after.cands.size(), t.nCands, r, n);
        }
      }
    }
  }
  long stot = 0, stotok = 0;
  for (int k = 1; k <= 6; ++k) {
    if (k == 5)
      continue;
    printf("RESULT-SOA %-24s records %8ld identical %8ld mismatching %6ld\n", kname[k], sk[k], sok[k], sk[k] - sok[k]);
    stot += sk[k];
    stotok += sok[k];
  }
  long tot = 0, totok = 0;
  for (size_t i = 0; i < recs.size(); ++i)
    nFail += (recs[i].kind == 6 && !recs[i].passed);
  printf("filter records with a failing seed (stock): %ld\n", nFail);
  for (int k = 1; k <= 6; ++k) {
    printf("RESULT %-28s records %8ld identical %8ld mismatching %6ld (score-bits-only %ld)\n",
           kname[k], nk[k], okk[k], nk[k] - okk[k], scoreOnly[k]);
    tot += nk[k];
    totok += okk[k];
  }
  printf("RESULT total records %ld identical %ld\n", tot, totok);
  return (tot == totok && stot == stotok) ? 0 : 1;
}
