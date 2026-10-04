// Lane clean: compare the device duplicate cleaner and LST-step filter with STOCK mkFit decisions on real tracks.
// Input: the binary dump of plugins/MkFitCleanDump.cc (stock mkFit on HLT events, stock decisions included).
// Usage: cleanCompare<Backend> <dump.bin> [nRepeat]
// Checks, per event:
//   D1 device duplicate flags == stock flags (StdSeq::clean_duplicates_sharedhits_pixelseed), exact
//   D2 device compacted survivors (labels, order) == stock remove_duplicates order, exact
//   D3 host all-pairs loop with the transliterated pair decision == stock flags (decision transliteration check)
//   D4 stress: copies with injected non-finite / out-of-range phi and cot theta: device binned == host all-pairs
//   D5 determinism: repeated device runs give identical flags
//   F1/F2 device filter decisions (pre: forward params, post: backward params) == stock, F3 compacted order
//   S1 seed compaction map (filter_comb_cands walk + stable compaction + region separators) == host transliteration

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <alpaka/alpaka.hpp>

#include "DataFormats/Portable/interface/PortableHostCollection.h"
#include "FWCore/Utilities/interface/stringize.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/devices.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/tracks/TrackSoADeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAHostCollection.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/clean/CleanAlgo.h"
// constants and device helpers only; the cleaner kernels are launched through CleanAlgo (library)
#include "RecoTracker/MkFitAlpaka/src/alpaka/clean/CleanKernels.h"
#include "RecoTracker/MkFitAlpaka/test/cleanDumpReader.h"

using namespace ALPAKA_ACCELERATOR_NAMESPACE;
using ::mkfitdev::TrackSoAHostCollection;
namespace dclean = ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::clean;
namespace hclean = ::mkfitdev::clean;

namespace {

  using cleandump::DEvent;
  using cleandump::DTrack;

  // Track::Status bit positions (gcc bitfield order, Track.h): not_findable 0, stopped 1, prod_type 2-3,
  // align_was_seed_type 4-5, duplicate 6, algorithm 7-12, n_overlaps 13-20 (signed), n_seed_hits 21-24, eta_region 25-27
  void fillRow(::mkfitdev::TrackSoAView v, int r, DTrack const& t) {
    for (int k = 0; k < 6; ++k)
      v[r].params().v[k] = t.par[k];
    for (int k = 0; k < 21; ++k)
      v[r].errors().v[k] = t.err[k];
    v[r].charge() = t.charge;
    v[r].chi2() = t.chi2;
    v[r].score() = t.score;
    v[r].label() = t.label;
    const int nh = t.nTot < ::mkfitdev::kMaxTrkHits ? t.nTot : ::mkfitdev::kMaxTrkHits;
    v[r].nTotalHits() = nh;
    v[r].nFoundHits() = t.nFound;
    const uint32_t s = uint32_t(t.status);
    v[r].duplicate() = (s >> 6) & 1;
    v[r].algorithm() = (s >> 7) & 0x3f;
    v[r].nOverlaps() = int8_t((s >> 13) & 0xff);
    v[r].nSeedHits() = (s >> 21) & 0xf;
    v[r].etaRegion() = (s >> 25) & 0x7;
    for (int h = 0; h < nh; ++h)
      std::memcpy(&v[r].hits().hot[h], &t.hots[h], 4);
  }

  void fill(TrackSoAHostCollection& h, std::vector<DTrack> const& trks, int& overflowHits) {
    auto v = h.view();
    v.nTracks() = trks.size();
    v.nOverflowTracks() = 0;
    int ov = 0;
    for (size_t i = 0; i < trks.size(); ++i) {
      fillRow(v, i, trks[i]);
      if (trks[i].nTot > ::mkfitdev::kMaxTrkHits)
        ++ov;
    }
    v.nOverflowHits() = ov;
    overflowHits += ov;
  }

  // Host all-pairs reference with the transliterated decision (same loop structure as stock).
  std::vector<int8_t> hostAllPairs(::mkfitdev::TrackSoAConstView v, hclean::DupCleanParams const& p) {
    const int n = v.nTracks();
    std::vector<float> ct(n);
    for (int i = 0; i < n; ++i)
      ct[i] = 1.f / std::tan(v[i].params().v[5]);
    std::vector<int8_t> dup(n, 0);
    for (int i = 0; i < n; ++i)
      for (int j = i + 1; j < n; ++j) {
        const int l = hclean::dupPairDecision(v, ct.data(), i, j, p);
        if (l >= 0)
          dup[l] = 1;
      }
    return dup;
  }

  struct KernelFirstPassing {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ::mkfitdev::TrackSoAConstView cands,
                                  const int* seedFirst,
                                  const int* seedNCands,
                                  int nSeeds,
                                  int minHitsQF,
                                  int* selected,
                                  int* pass) const {
      for (int32_t s : cms::alpakatools::uniform_elements(acc, nSeeds)) {
        const int f = seedFirst[s];
        const int j = dclean::firstPassingCand(
            seedNCands[s],
            [&](int k) { return int(cands[f + k].nFoundHits()); },
            [&](int k) { return cands[f + k].errors().v; },
            minHitsQF);
        selected[s] = j;
        pass[s] = j >= 0 ? 1 : 0;
      }
    }
  };

  struct Tally {
    long events = 0, tracks = 0, mismatch = 0, flagged = 0;
    void add(long n, long mm, long fl) {
      ++events;
      tracks += n;
      mismatch += mm;
      flagged += fl;
    }
  };

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: %s dump.bin [nRepeat]\n", argv[0]);
    return 2;
  }
  const int nRepeat = argc > 2 ? std::atoi(argv[2]) : 3;
  cleandump::Dump dump;
  if (!cleandump::readDump(argv[1], dump)) {
    std::printf("cannot read %s\n", argv[1]);
    return 2;
  }
  const float* fp = dump.fp;
  const int32_t* ip = dump.ip;
  auto& events = dump.events;
  const hclean::DupCleanParams params{fp[0], fp[1], fp[2], fp[3]};
  std::printf("dc params: frac %.9g drth central %.9g obarrel %.9g forward %.9g; minHitsQF fwd %d bkw %d\n",
              fp[0], fp[1], fp[2], fp[3], ip[0], ip[1]);

  std::printf("read %zu events\n", events.size());

  auto const& devs = cms::alpakatools::devices<Platform>();
  if (devs.empty()) {
    std::printf("no device for backend %s\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE));
    return 1;
  }
  Queue queue(devs[0]);
  std::printf("backend %s device %s\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE), alpaka::getName(devs[0]).c_str());

  int cap = 16;
  for (auto const& e : events)
    cap = std::max<int>(cap, std::max(e.noDC.size(), e.noFilt.size()) * 4 + 64);  // x4: stress copies below

  TrackSoAHostCollection hIn(queue, cap), hOut(queue, cap);
  ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::TrackSoADeviceCollection dIn(queue, cap), dOut(queue, cap);
  dclean::CleanAlgo algo(queue, cap);
  auto hFlagsBuf = cms::alpakatools::make_host_buffer<int[]>(queue, cap);
  auto hCnt = cms::alpakatools::make_host_buffer<int[]>(queue, dclean::kNCounters);
  auto hPairs = cms::alpakatools::make_host_buffer<int[]>(queue, cap + 1);

  Tally d1, d2, d3, d4, f1, f2, f3, s1;
  long nonDet = 0, cellOv = 0, ovHits = 0, wildTot = 0, pairThreads = 0, nIdentical = 0;
  double tDev = 0, tDevFilt = 0;
  int ovh = 0;

  for (auto const& e : events) {
    nIdentical += e.identical;
    // ---------------- duplicate cleaner ----------------
    fill(hIn, e.noDC, ovh);
    const int n = e.noDC.size();
    std::vector<int8_t> firstFlags;
    for (int rep = 0; rep < nRepeat; ++rep) {
      alpaka::memcpy(queue, dIn.buffer(), hIn.const_buffer());
      alpaka::wait(queue);
      auto t0 = std::chrono::steady_clock::now();
      algo.flagDuplicates(queue, dIn.view(), params);
      algo.removeDuplicates(queue, dIn.const_view(), dOut.view());
      alpaka::wait(queue);
      auto t1 = std::chrono::steady_clock::now();
      if (rep > 0)
        tDev += std::chrono::duration<double, std::milli>(t1 - t0).count();
      alpaka::memcpy(queue, hOut.buffer(), dOut.const_buffer());
      alpaka::memcpy(queue, hIn.buffer(), dIn.const_buffer());  // duplicate column
      alpaka::memcpy(queue, hCnt, cms::alpakatools::make_device_view(alpaka::getDev(queue), algo.counters(), dclean::kNCounters));
      alpaka::memcpy(queue, hPairs, cms::alpakatools::make_device_view(alpaka::getDev(queue), algo.pairOffsets(), cap + 1));
      alpaka::wait(queue);
      cellOv += hCnt[dclean::kCntCellOverflow];
      std::vector<int8_t> flags(n);
      for (int i = 0; i < n; ++i)
        flags[i] = hIn.view()[i].duplicate();
      if (rep == 0) {
        firstFlags = flags;
        wildTot += hCnt[dclean::kCntWildcard];
        pairThreads += hPairs[n];
      } else if (flags != firstFlags) {
        ++nonDet;
      }
    }
    // D1
    long mm = 0, fl = 0;
    for (int i = 0; i < n; ++i) {
      mm += (firstFlags[i] != e.noDC[i].a);
      fl += e.noDC[i].a;
    }
    d1.add(n, mm, fl);
    if (mm)
      std::printf("  D1 mismatch event %llu: %ld of %d\n", (unsigned long long)e.event, mm, n);
    // D2 survivors order
    {
      std::vector<int> stockSurv;
      for (int i = 0; i < n; ++i)
        if (!e.noDC[i].a)
          stockSurv.push_back(e.noDC[i].label);
      long m2 = (int(stockSurv.size()) != hOut.view().nTracks());
      for (int k = 0; k < std::min<int>(stockSurv.size(), hOut.view().nTracks()); ++k)
        m2 += (stockSurv[k] != hOut.view()[k].label());
      d2.add(stockSurv.size(), m2, 0);
    }
    // D3 host all-pairs with the transliterated decision vs stock
    {
      // restore the duplicate column to the input values (host reference reads nothing of it)
      auto ref = hostAllPairs(hIn.const_view(), params);
      long m3 = 0;
      for (int i = 0; i < n; ++i)
        m3 += (ref[i] != e.noDC[i].a);
      d3.add(n, m3, 0);
    }
    // D4 stress: 4 copies of the event (labels shifted so copies pair only through the decision), with injected
    // non-finite and out-of-range values; device binned vs host all-pairs on the same rows
    {
      std::vector<DTrack> st;
      std::mt19937 rng(e.event);
      for (int c = 0; c < 4; ++c)
        for (auto t : e.noDC) {
          t.label += c * 1000000;
          t.par[4] += c * 0.13f;  // copy c shifted by 0.13 rad: cross-copy pairs at dphi 0.13 / 0.26 / 0.39 (cell edges)
          const unsigned r = rng() % 1000;
          if (r < 3)
            t.par[5] = std::nanf("");
          else if (r < 6)
            t.par[4] = std::nanf("");
          else if (r < 9)
            t.par[4] += (rng() % 2 ? 1 : -1) * 2.f * 3.14159265f;  // outside [-pi, pi]
          else if (r < 12)
            t.par[5] = 1e-6f;  // cot theta ~ 1e6 (edge cell)
          else if (r < 15)
            t.par[4] = 1e3f;  // beyond binning range -> wildcard
          st.push_back(t);
        }
      fill(hIn, st, ovh);
      alpaka::memcpy(queue, dIn.buffer(), hIn.const_buffer());
      algo.flagDuplicates(queue, dIn.view(), params);
      alpaka::memcpy(queue, hOut.buffer(), dIn.const_buffer());
      alpaka::wait(queue);
      auto ref = hostAllPairs(hIn.const_view(), params);
      long m4 = 0, f4 = 0;
      for (size_t i = 0; i < st.size(); ++i) {
        m4 += (ref[i] != hOut.view()[i].duplicate());
        f4 += ref[i];
      }
      d4.add(st.size(), m4, f4);
    }
    // ---------------- filters ----------------
    {
      fill(hIn, e.noFilt, ovh);
      const int nf = e.noFilt.size();
      for (int pass = 0; pass < 2; ++pass) {
        alpaka::memcpy(queue, dIn.buffer(), hIn.const_buffer());
        alpaka::wait(queue);
        auto t0 = std::chrono::steady_clock::now();
        algo.filterTracks(queue, dIn.const_view(), dOut.view(), ip[pass]);
        alpaka::wait(queue);
        tDevFilt += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        alpaka::memcpy(queue, hFlagsBuf, cms::alpakatools::make_device_view(alpaka::getDev(queue), algo.passFlags(), cap));
        alpaka::memcpy(queue, hOut.buffer(), dOut.const_buffer());
        alpaka::wait(queue);
        long m = 0, p = 0;
        std::vector<int> stockKept;
        for (int i = 0; i < nf; ++i) {
          const int st = pass == 0 ? e.noFilt[i].a : e.noFilt[i].b;
          m += (hFlagsBuf[i] != st);
          p += st;
          if (st)
            stockKept.push_back(e.noFilt[i].label);
        }
        (pass == 0 ? f1 : f2).add(nf, m, nf - p);
        long m3 = (int(stockKept.size()) != hOut.view().nTracks());
        for (int k = 0; k < std::min<int>(stockKept.size(), hOut.view().nTracks()); ++k)
          m3 += (stockKept[k] != hOut.view()[k].label());
        f3.add(stockKept.size(), m3, 0);
      }
    }
    // ---------------- seed compaction map (filter_comb_cands) ----------------
    {
      // synthetic comb candidates: consecutive noFilt tracks grouped into seeds of 1..5 candidates, 5 regions
      const int nf = e.noFilt.size();
      std::vector<int> first, ncand;
      std::mt19937 rng(e.event + 7);
      for (int i = 0; i < nf;) {
        const int k = std::min<int>(1 + rng() % 5, nf - i);
        first.push_back(i);
        ncand.push_back(k);
        i += k;
      }
      const int ns = first.size();
      std::vector<int> oldSep(5);
      for (int r = 0; r < 5; ++r)
        oldSep[r] = (r + 1) * ns / 5;
      // host transliteration of MkBuilder::filter_comb_cands with stock per-candidate decisions (passPre)
      std::vector<int> refSel(ns, -1), refKeptSeeds;
      std::vector<int> removedPerReg(5, 0);
      for (int s = 0, reg = 0; s < ns; ++s) {
        while (s >= oldSep[reg])
          ++reg;
        for (int j = 0; j < ncand[s]; ++j)
          if (e.noFilt[first[s] + j].a) {
            refSel[s] = j;
            break;
          }
        if (refSel[s] >= 0)
          refKeptSeeds.push_back(s);
        else
          ++removedPerReg[reg];
      }
      std::vector<int> refSep(5);
      for (int r = 0, nrem = 0; r < 5; ++r) {
        nrem += removedPerReg[r];
        refSep[r] = oldSep[r] - nrem;
      }
      // device
      auto hI = cms::alpakatools::make_host_buffer<int[]>(queue, 2 * ns + 5);
      for (int s = 0; s < ns; ++s) {
        hI[s] = first[s];
        hI[ns + s] = ncand[s];
      }
      for (int r = 0; r < 5; ++r)
        hI[2 * ns + r] = oldSep[r];
      auto dI = cms::alpakatools::make_device_buffer<int[]>(queue, 2 * ns + 5);
      auto dSel = cms::alpakatools::make_device_buffer<int[]>(queue, ns);
      auto dPass = cms::alpakatools::make_device_buffer<int[]>(queue, ns);
      auto dDest = cms::alpakatools::make_device_buffer<int[]>(queue, ns + 1);
      auto dNs = cms::alpakatools::make_device_buffer<int32_t>(queue);
      auto dSep = cms::alpakatools::make_device_buffer<int[]>(queue, 5);
      auto hNs = cms::alpakatools::make_host_buffer<int32_t>(queue);
      *hNs = ns;
      alpaka::memcpy(queue, dI, hI);
      alpaka::memcpy(queue, dNs, hNs);
      alpaka::memcpy(queue, dIn.buffer(), hIn.const_buffer());
      alpaka::exec<Acc1D>(queue,
                          cms::alpakatools::make_workdiv<Acc1D>((ns + 255) / 256, 256),
                          KernelFirstPassing{},
                          dIn.const_view(),
                          dI.data(),
                          dI.data() + ns,
                          ns,
                          ip[0],
                          dSel.data(),
                          dPass.data());
      algo.seedCompactionMap(queue, dPass.data(), dNs.data(), dDest.data(), dI.data() + 2 * ns, dSep.data(), 5);
      auto hSel = cms::alpakatools::make_host_buffer<int[]>(queue, ns);
      auto hDest = cms::alpakatools::make_host_buffer<int[]>(queue, ns + 1);
      auto hSep = cms::alpakatools::make_host_buffer<int[]>(queue, 5);
      alpaka::memcpy(queue, hSel, dSel);
      alpaka::memcpy(queue, hDest, dDest);
      alpaka::memcpy(queue, hSep, dSep);
      alpaka::wait(queue);
      long m = 0;
      for (int s = 0; s < ns; ++s)
        m += (hSel[s] != refSel[s]);
      m += (hDest[ns] != int(refKeptSeeds.size()));
      for (size_t k = 0; k < refKeptSeeds.size(); ++k)
        m += (hDest[refKeptSeeds[k]] != int(k));
      for (int r = 0; r < 5; ++r)
        m += (hSep[r] != refSep[r]);
      s1.add(ns, m, ns - refKeptSeeds.size());
    }
  }
  ovHits = ovh;

  auto pr = [](const char* name, Tally const& t, const char* what) {
    std::printf("%-46s events %4ld  items %8ld  mismatches %6ld  %s %ld\n", name, t.events, t.tracks, t.mismatch, what, t.flagged);
  };
  std::printf("\n==== RESULTS (%s) ====\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE));
  std::printf("stock: cleaner applied to no-cleaning tracks == production collection in %ld / %zu events\n",
              nIdentical, events.size());
  pr("D1 dup flags device vs stock", d1, "stock-flagged");
  pr("D2 survivors (label, order) device vs stock", d2, "-");
  pr("D3 host all-pairs transliteration vs stock", d3, "-");
  pr("D4 stress (x4, injected NaN/range) dev vs host", d4, "flagged");
  pr("F1 pre-bkfit filter device vs stock", f1, "stock-rejected");
  pr("F2 post-bkfit filter device vs stock", f2, "stock-rejected");
  pr("F3 filtered survivors (label, order)", f3, "-");
  pr("S1 seed compaction map vs filter_comb_cands", s1, "seeds-removed");
  std::printf("cell-fill consistency overflows: %ld (must be 0)\n", cellOv);
  std::printf("D5 determinism: %ld non-identical repeats out of %zu x %d\n", nonDet, events.size(), nRepeat - 1);
  std::printf("wildcard tracks (real events): %ld; pair threads: %ld (%.1f per track); hit-list overflows: %ld\n",
              wildTot, pairThreads, d1.tracks ? double(pairThreads) / d1.tracks : 0., ovHits);
  std::printf("device time flag+remove: %.3f ms/event (excl. first rep); filter+compact: %.3f ms per call\n",
              (nRepeat > 1 && !events.empty()) ? tDev / (events.size() * (nRepeat - 1)) : 0.,
              events.empty() ? 0. : tDevFilt / (2 * events.size()));
  const long bad = d1.mismatch + d2.mismatch + d3.mismatch + d4.mismatch + f1.mismatch + f2.mismatch + f3.mismatch +
                   s1.mismatch + nonDet + cellOv;
  std::printf("OVERALL: %s\n", bad == 0 ? "PASS" : "FAIL");
  return bad == 0 ? 0 : 1;
}
