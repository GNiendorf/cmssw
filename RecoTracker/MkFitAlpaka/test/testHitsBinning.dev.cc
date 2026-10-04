// Replays dumps written by MkFitHitsCompareDumper (stock MkFitEventOfHitsProducer inputs + stock result) through the
// device EventOfHits (src/alpaka/hits/EventOfHitsKernels.h) and compares per event and per layer:
//   hit count, internal order (getOriginalHitIndex), hit infos (phi, q, half-length, qbar), bin table, dead bins.
// Usage: testMkFitAlpakaHits<backend> <dump.bin> [nRepeatTiming]
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include <alpaka/alpaka.hpp>

#include "FWCore/Utilities/interface/stringize.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/devices.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/LayerAxes.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/LayerOfHitsAccess.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/alpaka/EventOfHitsBuild.h"

namespace acc = ALPAKA_ACCELERATOR_NAMESPACE;

namespace {
  struct Reader {
    FILE* f;
    bool ok = true;
    template <typename T>
    T get() {
      T v{};
      ok = ok && fread(&v, sizeof(T), 1, f) == 1;
      return v;
    }
    template <typename T>
    void get(T* p, size_t n) {
      if (n)
        ok = ok && fread(p, sizeof(T), n, f) == n;
    }
  };

  struct StockLayer {
    std::vector<uint32_t> ranks;
    std::vector<float> infos;  // 4 per hit
    std::vector<uint32_t> content;
    std::vector<uint8_t> dead;
  };

  struct DumpEvent {
    uint32_t run = 0, lumi = 0;
    uint64_t event = 0;
    bool useDeads = false;
    std::vector<::mkfitdev::LayerAxisInput> axes;
    std::vector<float> hitf[2];  // 9 per hit
    std::vector<uint32_t> hitp[2];
    std::vector<int> layerOf[2];
    std::vector<::mkfitdev::DeadRegionDev> deads;
    std::vector<StockLayer> stock;
  };

  bool readEvent(Reader& r, DumpEvent& e) {
    const uint32_t magic = r.get<uint32_t>();
    if (!r.ok)
      return false;
    if (magic != 0x4d4b4831) {
      fprintf(stderr, "bad magic\n");
      return false;
    }
    e.run = r.get<uint32_t>();
    e.lumi = r.get<uint32_t>();
    e.event = r.get<uint64_t>();
    e.useDeads = r.get<uint32_t>() != 0;
    const uint32_t nl = r.get<uint32_t>();
    e.axes.resize(nl);
    for (auto& a : e.axes) {
      a.qmin = r.get<float>();
      a.qmax = r.get<float>();
      a.nq = r.get<uint32_t>();
      a.isBarrel = r.get<uint32_t>() != 0;
      a.isPixel = r.get<uint32_t>() != 0;
    }
    for (int w = 0; w < 2; ++w) {
      const uint32_t n = r.get<uint32_t>();
      e.hitf[w].resize(9 * n);
      e.hitp[w].resize(n);
      for (uint32_t i = 0; i < n; ++i) {
        r.get(&e.hitf[w][9 * i], 9);
        e.hitp[w][i] = r.get<uint32_t>();
      }
      const uint32_t nm = r.get<uint32_t>();
      e.layerOf[w].resize(nm);
      r.get(e.layerOf[w].data(), nm);
    }
    const uint32_t nd = r.get<uint32_t>();
    e.deads.resize(nd);
    r.get(e.deads.data(), nd);
    e.stock.resize(nl);
    for (uint32_t il = 0; il < nl; ++il) {
      auto& s = e.stock[il];
      const uint32_t n = r.get<uint32_t>();
      s.ranks.resize(n);
      r.get(s.ranks.data(), n);
      s.infos.resize(4 * n);
      r.get(s.infos.data(), 4 * n);
      const uint32_t nb = e.axes[il].nq * ::mkfitdev::kNPhiBins;
      s.content.resize(nb);
      r.get(s.content.data(), nb);
      s.dead.resize(nb);
      r.get(s.dead.data(), nb);
    }
    return r.ok;
  }

  struct Totals {
    uint64_t events = 0, hits = 0, orderMismatch = 0, orderMismatchSortPath = 0, orderMismatchTie = 0,
             countMismatchLayers = 0, infoNotIdentical = 0, bins = 0, binMismatch = 0, deadBins = 0, deadMismatch = 0,
             hitsInSortPathLayers = 0, overflows = 0;
    float maxInfoDiff = 0;
    uint32_t maxBinOcc = 0;
    double msTotal = 0, msCopy = 0;
    int timedEvents = 0;
  };
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s dump.bin [nRepeat]\n", argv[0]);
    return 2;
  }
  const int nRepeat = argc > 2 ? atoi(argv[2]) : 5;
  auto const& devices = cms::alpakatools::devices<acc::Platform>();
  if (devices.empty()) {
    printf("no device for backend %s, skipping\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE));
    return 0;
  }
  auto device = devices[0];
  acc::Queue queue(device);
  printf("backend %s device %s\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE), alpaka::getName(device).c_str());

  FILE* f = fopen(argv[1], "rb");
  if (!f) {
    fprintf(stderr, "cannot open %s\n", argv[1]);
    return 2;
  }
  Reader rd{f};
  Totals T;
  DumpEvent e;
  while (readEvent(rd, e)) {
    const uint32_t nPix = e.hitp[0].size(), nStr = e.hitp[1].size(), nHits = nPix + nStr;
    const uint32_t nL = e.axes.size();
    ::mkfitdev::HitsHostCollection hitsH(queue, nHits);
    auto hv = hitsH.view();
    hv.nPixel() = nPix;
    hv.nStrip() = nStr;
    for (int w = 0; w < 2; ++w) {
      const uint32_t base = w == 0 ? 0 : nPix;
      for (uint32_t i = 0; i < e.hitp[w].size(); ++i) {
        const float* p = &e.hitf[w][9 * i];
        auto r = hv[base + i];
        r.x() = p[0];
        r.y() = p[1];
        r.z() = p[2];
        r.e00() = p[3];
        r.e10() = p[4];
        r.e11() = p[5];
        r.e20() = p[6];
        r.e21() = p[7];
        r.e22() = p[8];
        r.packed() = e.hitp[w][i];
        r.layer() = i < e.layerOf[w].size() ? e.layerOf[w][i] : -1;
        r.origIdx() = i;
      }
    }
    ::mkfitdev::LayersHostCollection layersH(queue, nL);
    ::mkfitdev::fillLayers(e.axes, nPix, layersH.view());
    const std::vector<::mkfitdev::DeadRegionDev> noDeads;
    const auto& deads = e.useDeads ? e.deads : noDeads;

    // timing (best of nRepeat; the first call warms the caching allocator):
    //   build = binning kernels with hits already on the device; full = H2D of hits + layer table + build
    double best = 1e30, bestFull = 1e30;
    for (int it = 0; it < nRepeat; ++it) {
      alpaka::wait(queue);
      auto t0 = std::chrono::steady_clock::now();
      auto dtmp = acc::mkfitdev::hits::runMakeEventOfHitsDevice(queue, nHits, nL, layersH.view().nBinsTotal());
      alpaka::memcpy(queue, dtmp.hits.buffer(), hitsH.buffer());
      alpaka::memcpy(queue, dtmp.layers.buffer(), layersH.buffer());
      alpaka::wait(queue);
      auto t1 = std::chrono::steady_clock::now();
      acc::mkfitdev::hits::runBuildEventOfHits(queue, dtmp, deads);
      alpaka::wait(queue);
      auto t2 = std::chrono::steady_clock::now();
      best = std::min(best, std::chrono::duration<double, std::milli>(t2 - t1).count());
      bestFull = std::min(bestFull, std::chrono::duration<double, std::milli>(t2 - t0).count());
    }
    const double bestCopy = bestFull - best;
    if (nRepeat > 0) {
      T.msTotal += best;
      T.msCopy += bestFull;
      T.timedEvents++;
    }

    auto d = acc::mkfitdev::hits::runBuildEventOfHits(queue, hitsH, layersH, deads);
    ::mkfitdev::LayersHostCollection layersOut(queue, nL);
    ::mkfitdev::BinnedHitsHostCollection binnedOut(queue, nHits);
    ::mkfitdev::BinsHostCollection binsOut(queue, layersH.view().nBinsTotal());
    alpaka::memcpy(queue, layersOut.buffer(), d.layers.buffer());
    alpaka::memcpy(queue, binnedOut.buffer(), d.binnedHits.buffer());
    alpaka::memcpy(queue, binsOut.buffer(), d.bins.buffer());
    alpaka::wait(queue);

    auto lo = layersOut.const_view();
    auto bo = binnedOut.const_view();
    auto bn = binsOut.const_view();
    T.overflows += lo.nOverflowFirst() + lo.nOverflowCount();
    uint64_t evMis = 0, evHits = 0, evBinMis = 0, evInfo = 0, evDeadMis = 0;
    for (uint32_t il = 0; il < nL; ++il) {
      const auto& s = e.stock[il];
      const uint32_t n = s.ranks.size();
      evHits += n;
      if (lo[il].nHits() != n) {
        T.countMismatchLayers++;
        printf("  ev %lu layer %u nHits dev %u stock %u\n", (unsigned long)e.event, il, lo[il].nHits(), n);
        continue;
      }
      const uint32_t hb = lo[il].hitBegin();
      const uint32_t bb = lo[il].binBegin();
      const uint32_t nb = e.axes[il].nq * ::mkfitdev::kNPhiBins;
      // bin of each internal index (from the stock table) to classify order mismatches
      std::vector<uint32_t> binOfPos(n, 0xffffffff);
      for (uint32_t b = 0; b < nb; ++b) {
        const uint32_t c = s.content[b];
        for (uint32_t k = 0; k < ::mkfitdev::binCount(c); ++k)
          if (::mkfitdev::binFirst(c) + k < n)
            binOfPos[::mkfitdev::binFirst(c) + k] = b;
        T.maxBinOcc = std::max(T.maxBinOcc, ::mkfitdev::binCount(c));
      }
      std::vector<uint32_t> posOfOrig(nHits, n);
      for (uint32_t k = 0; k < n; ++k)
        if (s.ranks[k] < nHits)
          posOfOrig[s.ranks[k]] = k;
      const bool sortPath = n < 256;  // stock binnor uses std::sort (unstable) below 256 entries
      if (sortPath)
        T.hitsInSortPathLayers += n;
      // read back through the device-side LayerOfHits accessor (same API as stock), on the host copies
      const ::mkfitdev::LayerOfHitsAccess lacc{lo, bo, bn, int32_t(il)};
      for (uint32_t i = 0; i < n; ++i) {
        if (lacc.getOriginalHitIndex(i) != s.ranks[i]) {
          ++evMis;
          if (sortPath)
            T.orderMismatchSortPath++;
          // tie = same stock phi M-bin and same bin: compare the stock hit infos of the two hits at this position
          // (only meaningful when the device put a hit of the same bin there)
          const uint32_t o = bo[hb + i].rank();
          const uint32_t j = o < posOfOrig.size() ? posOfOrig[o] : n;
          if (j < n && binOfPos[j] == binOfPos[i] &&
              std::floor((s.infos[4 * j] + 3.14159265f) * 65536.f / 6.2831853f) ==
                  std::floor((s.infos[4 * i] + 3.14159265f) * 65536.f / 6.2831853f))
            T.orderMismatchTie++;
          continue;
        }
        const float* si = &s.infos[4 * i];
        const float di[4] = {lacc.hit_phi(i), lacc.hit_q(i), lacc.hit_q_half_length(i), lacc.hit_qbar(i)};
        for (int k = 0; k < 4; ++k) {
          if (std::memcmp(&si[k], &di[k], sizeof(float)) != 0) {
            ++evInfo;
            T.maxInfoDiff = std::max(T.maxInfoDiff, std::fabs(si[k] - di[k]));
          }
        }
      }
      for (uint32_t b = 0; b < nb; ++b) {
        const uint16_t pb = b % ::mkfitdev::kNPhiBins, qb = b / ::mkfitdev::kNPhiBins;
        if (lacc.binContent(pb, qb) != s.content[b] || bn[bb + b].content() != s.content[b])
          ++evBinMis;
        if (lacc.isBinDead(pb, qb) != (s.dead[b] != 0))
          ++evDeadMis;
        T.deadBins += s.dead[b];
      }
      T.bins += nb;
    }
    T.events++;
    T.hits += evHits;
    T.orderMismatch += evMis;
    T.binMismatch += evBinMis;
    T.infoNotIdentical += evInfo;
    T.deadMismatch += evDeadMis;
    printf(
        "event %u:%u:%lu  hits %u (registered %lu)  deads %zu  order mismatches %lu  bin-table mismatches %lu  "
        "hit-info non-identical %lu  dead-bin mismatches %lu  overflow %u/%u  build %.3f ms (alloc+H2D %.3f ms)\n",
        e.run,
        e.lumi,
        (unsigned long)e.event,
        nHits,
        (unsigned long)evHits,
        deads.size(),
        (unsigned long)evMis,
        (unsigned long)evBinMis,
        (unsigned long)evInfo,
        (unsigned long)evDeadMis,
        lo.nOverflowFirst(),
        lo.nOverflowCount(),
        best,
        bestCopy);
  }
  fclose(f);
  printf(
      "SUMMARY %s: events %lu  registered hits %lu  order mismatches %lu (in std::sort-path layers %lu, same-bin same-fine-phi "
      "ties %lu)  layers with nHits mismatch %lu  bins %lu  bin-table mismatches %lu  hit-info values not bit-identical "
      "%lu (max |diff| %g)  dead bins (stock) %lu  dead mismatches %lu  overflows %lu  max bin occupancy %u  "
      "hits in layers < 256 hits (stock std::sort path) %lu  mean build %.3f ms/event (incl. alloc + H2D %.3f ms)\n",
      EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE),
      (unsigned long)T.events,
      (unsigned long)T.hits,
      (unsigned long)T.orderMismatch,
      (unsigned long)T.orderMismatchSortPath,
      (unsigned long)T.orderMismatchTie,
      (unsigned long)T.countMismatchLayers,
      (unsigned long)T.bins,
      (unsigned long)T.binMismatch,
      (unsigned long)T.infoNotIdentical,
      T.maxInfoDiff,
      (unsigned long)T.deadBins,
      (unsigned long)T.deadMismatch,
      (unsigned long)T.overflows,
      T.maxBinOcc,
      (unsigned long)T.hitsInSortPathLayers,
      T.timedEvents ? T.msTotal / T.timedEvents : 0.,
      T.timedEvents ? T.msCopy / T.timedEvents : 0.);
  return (T.orderMismatch == 0 && T.binMismatch == 0 && T.countMismatchLayers == 0 && T.deadMismatch == 0) ? 0 : 1;
}
