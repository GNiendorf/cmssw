// Clone-engine kernels (K1, K3a, K3b, K4 + extras, K5, K6) vs STOCK mkFit on real events, through the exported entry
// points of interface/cands/alpaka/CandsEngine.h only (no kernel instantiation here, D-layout).
// Input: step dumps of the engine lane's private instrumented stock MkFitCore (test/engine_step_format.h) and the
// stock material map from lane prop's HLT capture (real_ttbar.bin header).
// K2 (propagation to the layer + selectHitIndicesV2, lane select) is REPLAYED from the dump: the layer-propagated
// state, the selected hits (with their positions, errors and module planes) and the raw WSR of every candidate
// stock listed.
// Two modes, both per find_tracks_in_layers call (one region chunk of one event):
//   isolated  every plan step starts from STOCK's state before the step (K1 from the previous step's snapshot)
//   chained   the device state evolves over the whole plan from the call's START snapshot (forward search; the
//             backward search starts from stock's state after beginBkwSearch), K6 at the end vs stock FINAL.
// A seed is "identical" when candidate count, order and bookkeeping (score, chi2, hit/hole/overlap counters,
// overlap pair) and every hit chain (hit, layer, chi2) are bitwise equal; states are reported separately
// (bitwise equal, and the max relative difference).
// Usage: <binary> <material.bin> <dump dir>... ; no arguments: usage, exit 0.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/devices.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandSeedOps.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandsHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/EngineFromES.h"  // compiled here (host helpers for the producer)
#include "RecoTracker/MkFitAlpaka/interface/cands/alpaka/CandsEngine.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/HitSoA.h"
#include "RecoTracker/MkFitAlpaka/test/engine_step_format.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/engine/EngineSelectBridge.h"
#include "DataFormats/Portable/interface/PortableHostCollection.h"
#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"

using namespace ALPAKA_ACCELERATOR_NAMESPACE;
namespace md = ::mkfitdev;
namespace dv = ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev;
namespace ed = ::enginedump;

namespace {

  int kHps = md::kDefaultHotsPerSeed;  // --hots=N: HoT pool capacity per seed (H4 overflow test)
  bool gBridge = false;                // --bridge: chained mode feeds K2 through the select bridge (list + scatter)
  constexpr int kNLayers = 80;
  constexpr int kModPerLayer = 1 << 14;
  constexpr int32_t kLocalBase = 0x400000;  // device-side hit ids of this test: kLocalBase + local row
  constexpr int kNRegions = 5;

  bool bitEq(float a, float b) { return std::memcmp(&a, &b, 4) == 0; }

  // first-mismatch reasons
  enum Why { kWhyState = 0, kWhyNCands, kWhyCounters, kWhyScoreChi2, kWhyOverlap, kWhyChainHits, kWhyChainChi2, kWhyBest, kNWhy };
  const char* kWhyName[kNWhy] = {"seed state", "n cands", "counters", "score/chi2 bits", "overlap pair", "chain hits", "chain chi2 bits", "best-short"};
  thread_local int g_why = -1;
  long g_whyCount[4][kNWhy] = {};

  md::CandBook toBook(const ed::Cand& c) {
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
  md::CandState toState(const ed::Cand& c) {
    md::CandState s;
    std::memcpy(s.par, c.par, sizeof(s.par));
    std::memcpy(s.err, c.err, sizeof(s.err));
    s.charge = c.charge;
    s.label = c.label;
    s.status = c.status;
    s.nSeedHits = c.nSeedHits;
    return s;
  }

  struct HostBufs {
    md::SeedCandsHostCollection seeds;
    md::CandSlotsHostCollection slots;
    md::CandHotsHostCollection hots;
    md::CandSelHitsHostCollection sel;
    HostBufs(Queue& q, int n)
        : seeds(q, n), slots(q, n * md::kSlotsPerSeed), hots(q, n * kHps), sel(q, n * md::kMaxCandsPerSeed) {}
  };

  bool loadSeed(const ed::Seed& sd, int row, int region, HostBufs& h) {
    auto s = h.seeds.view();
    if ((int)sd.hots.size() > kHps || (int)sd.cands.size() > md::kMaxCandsPerSeed) {
      s.nCands(row) = 0;  // does not fit: an empty, finished seed (excluded from comparisons by the caller)
      s.state(row) = md::kFinished;
      s.region(row) = region;
      s.nHots(row) = 0;
      s.bestShortValid(row) = 0;
      s.overflowBits(row) = 0;
      s.curBuf(row) = 0;
      return false;
    }
    s.nCands(row) = sd.cands.size();
    s.curBuf(row) = 0;
    s.state(row) = sd.state;
    s.pickupLayer(row) = sd.pickupLayer;
    s.layer(row) = -1;
    s.region(row) = region;
    s.activeMask(row) = 0;
    s.nActive(row) = 0;
    s.seedOriginIdx(row) = sd.seedIdx;
    s.nHots(row) = sd.hots.size();
    s.nExtras(row) = 0;
    s.nUpdates(row) = 0;
    s.nOverlapUpdates(row) = 0;
    s.bestShort(row) = toBook(sd.best);
    s.bestShortValid(row) = sd.best.hasCC ? 1 : 0;
    s.lastHitIdxBeforeBkw(row) = sd.lastHitIdxBeforeBkw;
    s.nInsideMinusOneBeforeBkw(row) = sd.nInsideBkw;
    s.nTailMinusOneBeforeBkw(row) = sd.nTailBkw;
    s.overflowBits(row) = 0;
    auto sl = h.slots.view();
    for (int ic = 0; ic < (int)sd.cands.size(); ++ic) {
      sl.book(md::candSlotRow(row, 0, ic)) = toBook(sd.cands[ic]);
      sl.state(md::candSlotRow(row, 0, ic)) = toState(sd.cands[ic]);
    }
    sl.state(md::bestShortRow(row)) = toState(sd.best);
    auto ho = h.hots.view();
    for (int k = 0; k < (int)sd.hots.size(); ++k)
      ho.node(md::hotRow(row, k, kHps)) =
          md::HoTNode{sd.hots[k].index, sd.hots[k].layer, sd.hots[k].chi2, sd.hots[k].prev};
    return true;
  }

  struct Stats {
    long seeds = 0, ident = 0, identBits = 0, stateBit = 0, k1Mismatch = 0, skipped = 0;
    double maxRel = 0;
    void add(const Stats& o) {
      seeds += o.seeds;
      ident += o.ident;
      identBits += o.identBits;
      stateBit += o.stateBit;
      k1Mismatch += o.k1Mismatch;
      skipped += o.skipped;
      maxRel = std::max(maxRel, o.maxRel);
    }
  };

  struct Ctx {
    const std::vector<int32_t>* l2g;  // local row -> global hit index
    int32_t mapIdx(int32_t v) const {
      if (v >= kLocalBase) {
        const int k = v - kLocalBase;
        return k < (int)l2g->size() ? (*l2g)[k] : -999999;
      }
      return v;
    }
  };

  void cmpState(const md::CandState& d, const ed::Cand& s, bool& bit, double& maxRel) {
    for (int i = 0; i < 6; ++i) {
      if (!bitEq(d.par[i], s.par[i]))
        bit = false;
      const double den = std::max(1e-6, std::abs((double)s.par[i]));
      maxRel = std::max(maxRel, std::abs((double)d.par[i] - s.par[i]) / den);
    }
    for (int i = 0; i < 21; ++i) {
      if (!bitEq(d.err[i], s.err[i]))
        bit = false;
      const double den = std::max(1e-12, std::abs((double)s.err[i]));
      maxRel = std::max(maxRel, std::abs((double)d.err[i] - s.err[i]) / den);
    }
    if (d.charge != s.charge)
      bit = false;
  }

  bool cmpBookChain(const md::CandBook& d,
                    const ed::Cand& s,
                    const md::HoTNode* dh,
                    int dn,
                    const std::vector<ed::HoT>& sh,
                    const Ctx& cx) {
    if (d.nFound != s.nFound || d.nMissing != s.nMissing || d.nOverlap != s.nOverlap ||
        d.nInsideMinusOne != s.nInsideMinusOne || d.nTailMinusOne != s.nTailMinusOne || d.originIndex != s.originIndex)
      return g_why = kWhyCounters, false;
    for (int i = 0; i < 2; ++i)
      if (cx.mapIdx(d.overlaps.M[i].hit) != s.ovHit[i] || d.overlaps.M[i].module != s.ovModule[i])
        return g_why = kWhyOverlap, false;
    int a = d.lastHitIdx, b = s.lastHitIdx, guard = 0;
    while (a >= 0 && b >= 0 && guard++ < 1000) {
      if (a >= dn || b >= (int)sh.size())
        return false;
      const md::HoTNode& x = dh[a];
      const ed::HoT& y = sh[b];
      if (cx.mapIdx(x.index) != y.index || x.layer != y.layer)
        return g_why = kWhyChainHits, false;
      a = x.prev;
      b = y.prev;
    }
    if (!(a < 0 && b < 0))
      return g_why = kWhyChainHits, false;
    // float bits last: structural agreement first
    a = d.lastHitIdx;
    b = s.lastHitIdx;
    while (a >= 0 && b >= 0) {
      if (!bitEq(dh[a].chi2, sh[b].chi2))
        return g_why = kWhyChainChi2, false;
      a = dh[a].prev;
      b = sh[b].prev;
    }
    if (!bitEq(d.score, s.score) || !bitEq(d.chi2, s.chi2))
      return g_why = kWhyScoreChi2, false;
    for (int i = 0; i < 2; ++i)
      if (!bitEq(d.overlaps.M[i].chi2, s.ovChi2[i]))
        return g_why = kWhyScoreChi2, false;
    return true;
  }

  // Compare device row with stock seed snapshot. Returns bookkeeping+chains identical; stBit = states bitwise.
  bool cmpSeed(HostBufs& h, int row, const ed::Seed& sd, const Ctx& cx, bool& stBit, double& maxRel) {
    auto s = h.seeds.view();
    auto sl = h.slots.view();
    const md::HoTNode* dh = &h.hots.view().node(md::hotRow(row, 0, kHps));
    const int dn = s.nHots(row);
    stBit = true;
    if (s.state(row) != sd.state)
      return g_why = kWhyState, false;
    if (s.nCands(row) != (int)sd.cands.size())
      return g_why = kWhyNCands, false;
    const int cur = s.curBuf(row);
    for (int ic = 0; ic < (int)sd.cands.size(); ++ic) {
      if (!cmpBookChain(sl.book(md::candSlotRow(row, cur, ic)), sd.cands[ic], dh, dn, sd.hots, cx))
        return false;
      cmpState(sl.state(md::candSlotRow(row, cur, ic)), sd.cands[ic], stBit, maxRel);
    }
    const bool dv = s.bestShortValid(row) != 0;
    if (dv != (sd.best.hasCC != 0))
      return g_why = kWhyBest, false;
    if (dv) {
      if (!cmpBookChain(s.bestShort(row), sd.best, dh, dn, sd.hots, cx))
        return false;
      cmpState(sl.state(md::bestShortRow(row)), sd.best, stBit, maxRel);
    }
    return true;
  }

  // structural = everything but float bits of chi2/score (rounding level); bits = everything bitwise.
  inline bool structOk(int why) { return why < 0 || why == kWhyChainChi2 || why == kWhyScoreChi2; }
  void account(Stats& st, bool ok, bool bit, double mr) {
    const int why = ok ? -1 : g_why;
    if (structOk(why)) {
      ++st.ident;
      if (why < 0)
        ++st.identBits;
      if (bit)
        ++st.stateBit;
      st.maxRel = std::max(st.maxRel, mr);
    }
  }

  // Host emulation of filter_comb_cands for one seed row of a host buffer (the per-seed body is the round-1 verified
  // filterSeedCands); used to check the device filter + stable compaction (scan + gather).
  bool hostFilter(HostBufs& h, int row, bool bkwRep, int minHitsQF) {
    auto s = h.seeds.view();
    if (s.nCands(row) <= 0)
      return false;
    auto sl = h.slots.view();
    md::SeedCandsRef r;
    const int cur = s.curBuf(row);
    r.state = &s.state(row);
    r.pickupLayer = &s.pickupLayer(row);
    r.cands = &sl.book(md::candSlotRow(row, cur, 0));
    r.states = &sl.state(md::candSlotRow(row, cur, 0));
    r.nCands = &s.nCands(row);
    r.bestShort = &s.bestShort(row);
    r.bestShortState = &sl.state(md::bestShortRow(row));
    r.bestShortValid = &s.bestShortValid(row);
    r.hots = &h.hots.view().node(md::hotRow(row, 0, kHps));
    r.hotOffset = 0;
    r.hotCap = kHps;
    r.nHots = &s.nHots(row);
    r.lastHitIdxBeforeBkw = &s.lastHitIdxBeforeBkw(row);
    r.nInsideMinusOneBeforeBkw = &s.nInsideMinusOneBeforeBkw(row);
    r.nTailMinusOneBeforeBkw = &s.nTailMinusOneBeforeBkw(row);
    r.overflowBits = &s.overflowBits(row);
    return md::filterSeedCands(r, bkwRep, true, minHitsQF);
  }

  struct DevHits {
    int cap;
    cms::alpakatools::device_buffer<Device, float[]> f;  // 9 columns x cap
    cms::alpakatools::device_buffer<Device, uint32_t[]> packed;
    DevHits(Queue& q, int c)
        : cap(c),
          f(cms::alpakatools::make_device_buffer<float[]>(q, 9 * c)),
          packed(cms::alpakatools::make_device_buffer<uint32_t[]>(q, c)) {}
  };

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: %s <material.bin (lane_prop run/hlt/real_ttbar.bin)> <step dump dir>...\n", argv[0]);
    return 0;
  }
  auto const& devices = cms::alpakatools::devices<Platform>();
  if (devices.empty()) {
    std::printf("No devices for %s, skipping.\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE));
    return 0;
  }
  Queue queue(devices[0]);
  std::printf("backend %s\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE));

  // ---- synthetic check of the stable compaction across scan chunks (n > 1024, random pass pattern) ----
  {
    const int nSyn = 5000;
    dv::EngineBuffers a(queue, nSyn, 4), o(queue, nSyn, 4);
    md::SeedCandsHostCollection hs(queue, nSyn);
    md::CandSlotsHostCollection hsl(queue, nSyn * md::kSlotsPerSeed);
    md::CandHotsHostCollection hh(queue, nSyn * 4);
    std::vector<int> expect;
    uint32_t rng = 12345;
    for (int i = 0; i < nSyn; ++i) {
      rng = rng * 1664525u + 1013904223u;
      const bool pass = (rng >> 13) % 3 != 0;
      auto v = hs.view();
      v.nCands(i) = 1;
      v.curBuf(i) = 0;
      v.seedOriginIdx(i) = 100000 + i;
      v.nHots(i) = 1;
      v.overflowBits(i) = 0;
      md::CandBook b{};
      b.nFound = pass ? 5 : 2;
      b.lastHitIdx = 0;
      hsl.view().book(md::candSlotRow(i, 0, 0)) = b;
      md::CandState st{};
      for (int k = 0; k < 21; ++k)
        st.err[k] = 1.f;
      hsl.view().state(md::candSlotRow(i, 0, 0)) = st;
      hh.view().node(md::hotRow(i, 0, 4)) = md::HoTNode{i, 3, 0.f, -1};
      if (pass)
        expect.push_back(i);
    }
    hs.view().hotsPerSeed() = 4;
    alpaka::memcpy(queue, a.seeds.buffer(), hs.buffer());
    alpaka::memcpy(queue, a.slots.buffer(), hsl.buffer());
    alpaka::memcpy(queue, a.hots.buffer(), hh.buffer());
    const int nOut = dv::engineFilterCompact(queue, a, o, false, 4, nSyn);
    alpaka::memcpy(queue, hs.buffer(), o.seeds.buffer());
    alpaka::memcpy(queue, hh.buffer(), o.hots.buffer());
    alpaka::wait(queue);
    int bad = nOut != (int)expect.size();
    for (int d = 0; d < nOut && d < (int)expect.size(); ++d)
      bad += hs.view().seedOriginIdx(d) != 100000 + expect[d] || hh.view().node(md::hotRow(d, 0, 4)).index != expect[d];
    std::printf("synthetic compaction: n %d, survivors %d (expected %zu), mismatches %d\n", nSyn, nOut, expect.size(), bad);
  }

  // ---- material map (header of the prop capture file) ----
  int nBinsZ = 0, nBinsR = 0;
  float rngZ = 0, rngR = 0;
  std::vector<float> bbxi, radl;
  {
    FILE* f = std::fopen(argv[1], "rb");
    uint32_t magic = 0;
    bool ok = f && std::fread(&magic, 4, 1, f) == 1 && magic == 0x50524f50 && std::fread(&nBinsZ, 4, 1, f) == 1 &&
              std::fread(&nBinsR, 4, 1, f) == 1 && std::fread(&rngZ, 4, 1, f) == 1 && std::fread(&rngR, 4, 1, f) == 1;
    if (ok) {
      bbxi.resize(nBinsZ * nBinsR);
      radl.resize(nBinsZ * nBinsR);
      ok = std::fread(bbxi.data(), 4, bbxi.size(), f) == bbxi.size() &&
           std::fread(radl.data(), 4, radl.size(), f) == radl.size();
    }
    if (f)
      std::fclose(f);
    if (!ok) {
      std::printf("cannot read material from %s\n", argv[1]);
      return 2;
    }
  }
  auto dBbxi = cms::alpakatools::make_device_buffer<float[]>(queue, bbxi.size());
  auto dRadl = cms::alpakatools::make_device_buffer<float[]>(queue, radl.size());
  alpaka::memcpy(queue, dBbxi, cms::alpakatools::make_host_view(bbxi.data(), bbxi.size()));
  alpaka::memcpy(queue, dRadl, cms::alpakatools::make_host_view(radl.data(), radl.size()));
  const md::MaterialView mv{dBbxi.data(), dRadl.data(), nBinsZ, nBinsR, nBinsZ / rngZ, nBinsR / rngR};
  dv::EnginePropConfig pc;
  // phase-2 finding flags (use_param_b_field | apply_material); Cands.dev.cc converts them with the shared adapter
  pc.interLayer = md::PropFlags{true, true, false};
  pc.intraLayer = md::PropFlags{true, true, false};
  pc.material = mv;
  pc.propToHit = true;

  // ---- read all dumps ----
  std::vector<std::string> files;
  for (int a = 2; a < argc; ++a) {
    if (std::strcmp(argv[a], "--bridge") == 0) {
      gBridge = true;
      std::printf("chained mode through the select bridge (engineBuildSelectList + engineScatterSelect)\n");
      continue;
    }
    if (std::strncmp(argv[a], "--hots=", 7) == 0) {
      kHps = std::atoi(argv[a] + 7);
      std::printf("HoT pool capacity per seed set to %d (H4 test)\n", kHps);
      continue;
    }
    if (std::filesystem::is_directory(argv[a])) {
      for (auto& e : std::filesystem::directory_iterator(argv[a]))
        if (e.path().filename().string().rfind("step_", 0) == 0)
          files.push_back(e.path().string());
    } else
      files.push_back(argv[a]);
  }
  std::sort(files.begin(), files.end());
  // calls in file order: (file, call id) -> records
  std::vector<std::vector<ed::Record>> calls;
  std::vector<md::EngineModule> modules(kNLayers * kModPerLayer);
  std::vector<md::EngineLayerParams> layFwd(kNLayers), layBkw(kNLayers);
  for (auto& l : layFwd)
    l = md::EngineLayerParams{0, 0, 0, 0, 0, {0, 0, 0, 0}};
  layBkw = layFwd;
  size_t maxSeeds = 1, maxHits = 1;
  long nRec = 0, nBadRead = 0;
  for (auto& fn : files) {
    ed::Reader rd(fn);
    std::map<int, int> callPos;
    ed::Record r;
    while (rd.next(r)) {
      ++nRec;
      auto it = callPos.find(r.call);
      if (it == callPos.end()) {
        callPos[r.call] = calls.size();
        calls.emplace_back();
        it = callPos.find(r.call);
      }
      if (r.kind == 1) {
        auto& lp = (r.step.inFwd ? layFwd : layBkw)[r.step.layer];
        lp.moduleBegin = r.step.layer * kModPerLayer;
        lp.isPixel = r.step.isPixel;
        lp.isBarrel = r.step.isBarrel;
        lp.hasC2 = r.step.nWin > 0;
        for (int i = 0; i < 4; ++i)
          lp.c2[i] = r.step.c2[i];
        size_t nh = 0;
        for (auto& l : r.listed)
          for (auto& h : l.hits) {
            md::EngineModule& m = modules[r.step.layer * kModPerLayer + h.module];
            for (int i = 0; i < 3; ++i) {
              m.nrm[i] = h.nrm[i];
              m.dir[i] = h.dir[i];
              m.pnt[i] = h.pnt[i];
            }
            ++nh;
          }
        maxHits = std::max(maxHits, nh);
      }
      maxSeeds = std::max(maxSeeds, (size_t)r.nSeeds);
      calls[it->second].push_back(std::move(r));
    }
  }
  // layers seen only in one direction: copy geometry flags (windows stay per direction)
  for (int l = 0; l < kNLayers; ++l) {
    for (auto* p : {&layFwd[l], &layBkw[l]}) {
      p->moduleBegin = l * kModPerLayer;
    }
    if (!layFwd[l].isPixel && !layFwd[l].isBarrel && (layBkw[l].isPixel || layBkw[l].isBarrel)) {
      layFwd[l].isPixel = layBkw[l].isPixel;
      layFwd[l].isBarrel = layBkw[l].isBarrel;
    }
    if (!layBkw[l].isPixel && !layBkw[l].isBarrel && (layFwd[l].isPixel || layFwd[l].isBarrel)) {
      layBkw[l].isPixel = layFwd[l].isPixel;
      layBkw[l].isBarrel = layFwd[l].isBarrel;
    }
  }
  std::printf("files %zu, records %ld, calls %zu, max seeds/call %zu\n", files.size(), nRec, calls.size(), maxSeeds);

  auto dModules = cms::alpakatools::make_device_buffer<md::EngineModule[]>(queue, modules.size());
  alpaka::memcpy(queue, dModules, cms::alpakatools::make_host_view(modules.data(), modules.size()));
  auto dLayFwd = cms::alpakatools::make_device_buffer<md::EngineLayerParams[]>(queue, kNLayers);
  auto dLayBkw = cms::alpakatools::make_device_buffer<md::EngineLayerParams[]>(queue, kNLayers);
  alpaka::memcpy(queue, dLayFwd, cms::alpakatools::make_host_view(layFwd.data(), kNLayers));
  alpaka::memcpy(queue, dLayBkw, cms::alpakatools::make_host_view(layBkw.data(), kNLayers));

  // per-call local hit table (max over calls: hits of all steps of one call)
  size_t maxCallHits = 1;
  for (auto& c : calls) {
    size_t n = 0;
    for (auto& r : c)
      for (auto& l : r.listed)
        n += l.hits.size();
    maxCallHits = std::max(maxCallHits, n);
  }
  DevHits dh(queue, maxCallHits);
  std::vector<float> hf(9 * maxCallHits);
  std::vector<uint32_t> hp(maxCallHits);

  const int nS = maxSeeds;
  dv::EngineBuffers isoB(queue, nS, kHps), chB(queue, nS, kHps);
  HostBufs hIso(queue, nS), hCh(queue, nS);
  const int nL = nS * md::kMaxCandsPerSeed;
  PortableHostCollection<md::SelListSoA> hList(queue, nL);
  PortableHostCollection<md::PropStateSoA> hProps(queue, nL);
  PortableHostCollection<md::SelHitsSoA> hSels(queue, nL);
  ALPAKA_ACCELERATOR_NAMESPACE::PortableCollection<md::SelListSoA> dList(queue, nL);
  ALPAKA_ACCELERATOR_NAMESPACE::PortableCollection<md::PropStateSoA> dProps(queue, nL);
  ALPAKA_ACCELERATOR_NAMESPACE::PortableCollection<md::SelHitsSoA> dSels(queue, nL);
  long brLists = 0, brRows = 0, brOrderBad = 0, brMissing = 0;
  auto hStep = cms::alpakatools::make_host_buffer<int16_t[]>(queue, 2 * kNRegions);
  auto dStep = cms::alpakatools::make_device_buffer<int16_t[]>(queue, 2 * kNRegions);

  Stats isoF, isoB_, chF, chB_, finF, finB;
  long k6mismatchCalls = 0, nPickupOnly = 0;
  long fcCalls = 0, fcSeeds = 0, fcPass = 0, fcBad = 0;  // filter + compaction check
  long h4Violations = 0, h4CounterMismatch = 0, h4MaxOverflowed = 0;  // H4 overflow semantics
  std::vector<long> h4LastCount(calls.size(), 0);
  long h4FilterLeaks = 0, nNotLoaded = 0, devMaxHots = 0, stockMaxHots = 0;
  for (auto& c : calls) {
    if (c.empty() || c.front().kind != 0)
      continue;
    const ed::Record& start = c.front();
    const int n = start.nSeeds, s0 = start.startSeed, region = start.region;
    const bool fwd = start.iterDir == 0;
    // local hit table of this call
    std::map<std::pair<int, int>, int32_t> g2l;  // (isPixel, global idx) -> local row
    std::vector<int32_t> l2g;
    for (auto& r : c)
      if (r.kind == 1)
        for (auto& l : r.listed)
          for (auto& h : l.hits) {
            auto key = std::make_pair(r.step.isPixel, h.idx);
            if (g2l.count(key))
              continue;
            const int k = l2g.size();
            g2l[key] = k;
            l2g.push_back(h.idx);
            for (int i = 0; i < 3; ++i)
              hf[i * maxCallHits + k] = h.pos[i];
            for (int i = 0; i < 6; ++i)
              hf[(3 + i) * maxCallHits + k] = h.err[i];
            hp[k] = md::hitpack::pack(h.module, 0, h.spanRows - 1, 0);
          }
    alpaka::memcpy(queue, dh.f, cms::alpakatools::make_host_view(hf.data(), hf.size()));
    alpaka::memcpy(queue, dh.packed, cms::alpakatools::make_host_view(hp.data(), hp.size()));
    md::EngineHitInputs in;
    float* fb = dh.f.data();
    in.x = fb + 0 * maxCallHits - kLocalBase;
    in.y = fb + 1 * maxCallHits - kLocalBase;
    in.z = fb + 2 * maxCallHits - kLocalBase;
    in.e00 = fb + 3 * maxCallHits - kLocalBase;
    in.e10 = fb + 4 * maxCallHits - kLocalBase;
    in.e11 = fb + 5 * maxCallHits - kLocalBase;
    in.e20 = fb + 6 * maxCallHits - kLocalBase;
    in.e21 = fb + 7 * maxCallHits - kLocalBase;
    in.e22 = fb + 8 * maxCallHits - kLocalBase;
    in.packed = dh.packed.data() - kLocalBase;
    in.nPixel = 0;
    in.modules = dModules.data();
    in.layers = fwd ? dLayFwd.data() : dLayBkw.data();
    Ctx cx{&l2g};

    // stock state per seed (latest snapshot)
    std::vector<ed::Seed> stock(n);
    bool loadOk = true;
    for (auto& sd : start.seeds)
      stock[sd.seedIdx - s0] = sd;
    // chained: load START
    std::vector<char> loaded(n, 1);
    for (int i = 0; i < n; ++i)
      loaded[i] = loadSeed(stock[i], i, region, hCh);
    hCh.seeds.view().hotsPerSeed() = kHps;
    hCh.seeds.view().nOverflowHots() = 0;
    alpaka::memcpy(queue, chB.seeds.buffer(), hCh.seeds.buffer());
    alpaka::memcpy(queue, chB.slots.buffer(), hCh.slots.buffer());
    alpaka::memcpy(queue, chB.hots.buffer(), hCh.hots.buffer());
    std::vector<char> diverged(n, 0);
    for (int i = 0; i < n; ++i)
      if (!loaded[i]) {
        diverged[i] = 4;
        ++nNotLoaded;
      }

    md::EngineIterParams ip{};
    for (auto& r : c) {
      if (r.kind == 2) {  // FINAL: K6 on the chained state
        dv::engineMerge(queue, chB, ip.maxCandsPerSeed > 0 ? ip.maxCandsPerSeed : 5, n);
        alpaka::memcpy(queue, hCh.seeds.buffer(), chB.seeds.buffer());
        alpaka::memcpy(queue, hCh.slots.buffer(), chB.slots.buffer());
        alpaka::memcpy(queue, hCh.hots.buffer(), chB.hots.buffer());
        alpaka::wait(queue);
        Stats& st = fwd ? finF : finB;
        bool anyBad = false;
        for (auto& sd : r.seeds) {
          const int row = sd.seedIdx - s0;
          ++st.seeds;
          if (diverged[row]) {
            ++st.skipped;
            anyBad = true;
            continue;
          }
          bool bit = true;
          double mr = 0;
          g_why = -1;
          const bool ok = cmpSeed(hCh, row, sd, cx, bit, mr);
          account(st, ok, bit, mr);
          if (!structOk(ok ? -1 : g_why))
            anyBad = true;
        }
        k6mismatchCalls += anyBad;
        // filter + stable compaction (device) vs host emulation on the same merged state
        {
          const bool bkwRep = !fwd;
          const int nOut = dv::engineFilterCompact(queue, chB, isoB, bkwRep, 4, n);
          alpaka::memcpy(queue, hIso.seeds.buffer(), isoB.seeds.buffer());
          alpaka::memcpy(queue, hIso.slots.buffer(), isoB.slots.buffer());
          alpaka::memcpy(queue, hIso.hots.buffer(), isoB.hots.buffer());
          alpaka::wait(queue);
          int d = 0;
          bool bad = false;
          for (int i = 0; i < n; ++i) {
            ++fcSeeds;
            const bool ovf = hCh.seeds.view().overflowBits(i) & md::kOverflowHotsBit;
            if (!hostFilter(hCh, i, bkwRep, 4))
              continue;
            if (ovf)
              ++h4FilterLeaks;
            ++fcPass;
            auto so = hIso.seeds.view();
            auto si = hCh.seeds.view();
            if (d >= nOut || so.seedOriginIdx(d) != si.seedOriginIdx(i) || so.nCands(d) != si.nCands(i) ||
                so.nHots(d) != si.nHots(i)) {
              bad = true;
            } else {
              const md::CandBook& bo = hIso.slots.view().book(md::candSlotRow(d, so.curBuf(d), 0));
              const md::CandBook& bi = hCh.slots.view().book(md::candSlotRow(i, si.curBuf(i), 0));
              if (std::memcmp(&bo, &bi, sizeof(md::CandBook)) != 0)
                bad = true;
              for (int k = 0; k < si.nHots(i) && !bad; ++k)
                if (std::memcmp(&hIso.hots.view().node(md::hotRow(d, k, kHps)),
                                &hCh.hots.view().node(md::hotRow(i, k, kHps)),
                                sizeof(md::HoTNode)) != 0)
                  bad = true;
            }
            ++d;
          }
          if (d != nOut)
            bad = true;
          ++fcCalls;
          fcBad += bad;
        }
        continue;
      }
      if (r.kind != 1)
        continue;
      const ed::StepHeader& h = r.step;
      ip.maxCandsPerSeed = h.maxCandsPerSeed;
      ip.maxHolesPerCand = h.maxHolesPerCand;
      ip.maxConsecHoles = h.maxConsecHoles;
      ip.maxClusterSize = h.maxClusterSize;
      ip.chi2CutMin = h.chi2CutMin;
      ip.pTCutOverlap = h.pTCutOverlap;
      ip.minPtCut = h.minPtCut;
      ip.recheckOverlap = h.recheckOverlap != 0;
      for (int i = 0; i < kNRegions; ++i) {
        hStep.data()[i] = -1;
        hStep.data()[kNRegions + i] = -1;
      }
      hStep.data()[region] = h.layer;
      hStep.data()[kNRegions + region] = h.prevLayer;
      nPickupOnly += h.pickupOnly != 0;
      alpaka::memcpy(queue, dStep, hStep);
      // stock listing per seed row
      std::vector<uint8_t> stockMask(n, 0);
      for (auto& l : r.listed)
        stockMask[l.seed - s0] |= uint8_t(1u << l.ic);
      // sel host buffer from the dump (K2 replay), all rows reset
      auto fillSel = [&](HostBufs& hb) {
        auto sv = hb.sel.view();
        for (int i = 0; i < n * md::kMaxCandsPerSeed; ++i) {
          sv.sel(i).n = 0;
          sv.sel(i).wsr = -1;
          sv.sel(i).inGap = 0;
        }
        for (auto& l : r.listed) {
          const int row = md::selRow(l.seed - s0, l.ic);
          md::CandSelHits& sh = sv.sel(row);
          md::CandPropState& ps = sv.prop(row);
          std::memcpy(ps.par, l.par, sizeof(ps.par));
          std::memcpy(ps.err, l.err, sizeof(ps.err));
          ps.charge = l.charge;
          sh.n = std::min<int>(l.nHits, md::kMaxHitsPerCand);
          sh.wsr = l.wsr;
          sh.inGap = l.inGap;
          for (int k = 0; k < sh.n; ++k)
            sh.hit[k] = kLocalBase + g2l[std::make_pair(h.isPixel, l.hits[k].idx)];
        }
      };

      // ---------------- isolated ----------------
      {
        bool ok = true;
        for (int i = 0; i < n; ++i)
          ok = loadSeed(stock[i], i, region, hIso) && ok;
        hIso.seeds.view().hotsPerSeed() = kHps;
        hIso.seeds.view().nOverflowHots() = 0;
        alpaka::memcpy(queue, isoB.seeds.buffer(), hIso.seeds.buffer());
        alpaka::memcpy(queue, isoB.slots.buffer(), hIso.slots.buffer());
        alpaka::memcpy(queue, isoB.hots.buffer(), hIso.hots.buffer());
        dv::engineActivate(queue, isoB, dStep.data(), dStep.data() + kNRegions, fwd, ip.minPtCut, n);
        alpaka::memcpy(queue, hIso.seeds.buffer(), isoB.seeds.buffer());
        alpaka::wait(queue);
        std::vector<char> k1bad(n, 0);
        for (int i = 0; i < n; ++i)
          k1bad[i] = hIso.seeds.view().activeMask(i) != stockMask[i];
        fillSel(hIso);
        alpaka::memcpy(queue, isoB.sel.buffer(), hIso.sel.buffer());
        dv::engineStep(queue, isoB, in, pc, ip, n);
        alpaka::memcpy(queue, hIso.seeds.buffer(), isoB.seeds.buffer());
        alpaka::memcpy(queue, hIso.slots.buffer(), isoB.slots.buffer());
        alpaka::memcpy(queue, hIso.hots.buffer(), isoB.hots.buffer());
        alpaka::wait(queue);
        Stats& st = fwd ? isoF : isoB_;
        for (auto& sd : r.seeds) {
          const int row = sd.seedIdx - s0;
          ++st.seeds;
          if (k1bad[row]) {
            ++st.k1Mismatch;
            continue;
          }
          bool bit = true;
          double mr = 0;
          g_why = -1;
          const bool ok = cmpSeed(hIso, row, sd, cx, bit, mr);
          account(st, ok, bit, mr);
          if (!ok && g_why >= 0)
            ++g_whyCount[fwd ? 0 : 1][g_why];
        }
        (void)ok;
      }

      // ---------------- chained ----------------
      {
        dv::engineActivate(queue, chB, dStep.data(), dStep.data() + kNRegions, fwd, ip.minPtCut, n);
        alpaka::memcpy(queue, hCh.seeds.buffer(), chB.seeds.buffer());
        alpaka::wait(queue);
        for (int i = 0; i < n; ++i)
          if (hCh.seeds.view().activeMask(i) != stockMask[i] && !diverged[i])
            diverged[i] = 2;  // K1 listing differs
        if (!gBridge) {
          fillSel(hCh);
          alpaka::memcpy(queue, chB.sel.buffer(), hCh.sel.buffer());
        } else {
          // K1 -> dense list (device) -> select outputs in LIST order (from the dump) -> scatter (device)
          dv::engineBuildSelectList(queue, chB, dList.view(), n);
          alpaka::memcpy(queue, hList.buffer(), dList.buffer());
          alpaka::wait(queue);
          const int nl = hList.view().n();
          ++brLists;
          std::map<std::pair<int, int>, const ed::Listed*> byKey;
          for (auto& l : r.listed)
            byKey[{l.seed - s0, l.ic}] = &l;
          // stock seed_cand_idx order must equal the list order (where the K1 listings agree)
          int li = 0;
          for (auto& l : r.listed) {
            if (diverged[l.seed - s0])
              continue;
            while (li < nl) {
              const int row = hList.view()[li].row();
              if (!diverged[row / md::kSlotsPerSeed])
                break;
              ++li;
            }
            const int row = li < nl ? hList.view()[li].row() : -1;
            if (row < 0 || row / md::kSlotsPerSeed != l.seed - s0 ||
                (row % md::kSlotsPerSeed) % md::kMaxCandsPerSeed != l.ic)
              ++brOrderBad;
            ++li;
          }
          for (int i = 0; i < nl; ++i) {
            ++brRows;
            const int row = hList.view()[i].row();
            const int sr = row / md::kSlotsPerSeed, ic = (row % md::kSlotsPerSeed) % md::kMaxCandsPerSeed;
            md::SelHits& sh = hSels.view()[i].sel();
            md::PropState& ps = hProps.view()[i].ps();
            auto it = byKey.find({sr, ic});
            if (it == byKey.end()) {
              ++brMissing;
              sh.nHits = 0;
              sh.wsrRaw = -1;
              sh.inGap = 0;
              continue;
            }
            const ed::Listed& l = *it->second;
            std::memcpy(ps.par, l.par, sizeof(ps.par));
            std::memcpy(ps.err, l.err, sizeof(ps.err));
            ps.fail = 0;
            sh.nHits = std::min<int>(l.nHits, md::kMaxHitsPerCand);
            sh.wsrRaw = l.wsr;
            sh.inGap = l.inGap;
            for (int k = 0; k < md::kMaxHitsPerCand; ++k)
              sh.hits[k] = k < sh.nHits ? kLocalBase + g2l[std::make_pair(h.isPixel, l.hits[k].idx)] : -1;
          }
          alpaka::memcpy(queue, dProps.buffer(), hProps.buffer());
          alpaka::memcpy(queue, dSels.buffer(), hSels.buffer());
          dv::engineScatterSelect(queue, chB, dList.const_view(), dProps.const_view(), dSels.const_view(), nL);
        }
        dv::engineStep(queue, chB, in, pc, ip, n);
        alpaka::memcpy(queue, hCh.seeds.buffer(), chB.seeds.buffer());
        alpaka::memcpy(queue, hCh.slots.buffer(), chB.slots.buffer());
        alpaka::memcpy(queue, hCh.hots.buffer(), chB.hots.buffer());
        alpaka::wait(queue);
        // H4: overflowed seeds must be failed (Finished, no cands, no updates) and counted exactly once
        {
          auto sv = hCh.seeds.view();
          long nBit = 0;
          for (int i = 0; i < n; ++i)
            if (sv.overflowBits(i) & md::kOverflowHotsBit) {
              ++nBit;
              if (!diverged[i])
                diverged[i] = 3;
              if (sv.state(i) != md::kFinished || sv.nCands(i) != 0 || sv.nUpdates(i) != 0 || sv.nHots(i) > kHps)
                ++h4Violations;
            }
          if ((long)sv.nOverflowHots() != nBit)
            ++h4CounterMismatch;
          h4MaxOverflowed = std::max(h4MaxOverflowed, nBit);
          for (int i = 0; i < n; ++i)
            devMaxHots = std::max<long>(devMaxHots, sv.nHots(i));
          for (auto& sd : r.seeds)
            stockMaxHots = std::max<long>(stockMaxHots, sd.hots.size());
          h4LastCount[&c - &calls[0]] = nBit;
        }
        Stats& st = fwd ? chF : chB_;
        for (auto& sd : r.seeds) {
          const int row = sd.seedIdx - s0;
          ++st.seeds;
          if (diverged[row]) {
            ++st.skipped;
            continue;
          }
          bool bit = true;
          double mr = 0;
          g_why = -1;
          const bool ok = cmpSeed(hCh, row, sd, cx, bit, mr);
          account(st, ok, bit, mr);
          if (!structOk(ok ? -1 : g_why)) {
            diverged[row] = 1;
            ++g_whyCount[fwd ? 2 : 3][g_why];
          }
        }
      }
      for (auto& sd : r.seeds)
        stock[sd.seedIdx - s0] = sd;
    }
  }

  auto pr = [](const char* name, const Stats& s) {
    std::printf(
        "%-27s seeds %7ld | identical: structural %7ld (%.4f%% of all, %.4f%% of compared), bitwise %7ld (%.4f%%) | "
        "states bitwise %.4f%% of structural, max rel %.3g | K1-list mismatch %ld, skipped (diverged earlier) %ld\n",
        name,
        s.seeds,
        s.ident,
        s.seeds ? 100.0 * s.ident / s.seeds : 0.0,
        100.0 * s.ident / std::max(1L, s.seeds - s.skipped - s.k1Mismatch),
        s.identBits,
        s.seeds ? 100.0 * s.identBits / s.seeds : 0.0,
        s.ident ? 100.0 * s.stateBit / s.ident : 0.0,
        s.maxRel,
        s.k1Mismatch,
        s.skipped);
  };
  for (int d = 0; d < 4; ++d) {
    std::printf("%s first-mismatch reasons:", d == 0 ? "isolated forward " : d == 1 ? "isolated backward" : d == 2 ? "chained forward  " : "chained backward ");
    for (int w = 0; w < kNWhy; ++w)
      std::printf("  %s %ld", kWhyName[w], g_whyCount[d][w]);
    std::printf("\n");
  }
  pr("isolated step, forward", isoF);
  pr("isolated step, backward", isoB_);
  pr("chained, forward", chF);
  pr("chained, backward", chB_);
  pr("after K6 (FINAL), forward", finF);
  pr("after K6 (FINAL), backward", finB);
  std::printf("filter + stable compaction (device scan + gather vs host emulation): calls %ld, seeds %ld, passing %ld, "
              "calls with a mismatch %ld\n",
              fcCalls,
              fcSeeds,
              fcPass,
              fcBad);
  long h4Total = 0;
  for (long v : h4LastCount)
    h4Total += v;
  std::printf("H4 (HoT pool %d/seed): overflowed seeds %ld (sum over calls), state violations %ld, counter mismatches %ld, "
              "overflowed seeds passing the filter %ld\n",
              kHps,
              h4Total,
              h4Violations,
              h4CounterMismatch,
              h4FilterLeaks);
  if (gBridge)
    std::printf("select bridge: steps %ld, list rows %ld, list-order mismatches vs stock seed_cand_idx %ld, rows without "
                "stock K2 data %ld\n",
                brLists,
                brRows,
                brOrderBad,
                brMissing);
  std::printf("seeds not fitting the HoT pool at call start (skipped): %ld; max HoT nodes per seed: device %ld, stock %ld\n",
              nNotLoaded,
              devMaxHots,
              stockMaxHots);
  std::printf("calls with a FINAL mismatch: %ld; pickup-only step records %ld; read errors %ld\n",
              k6mismatchCalls,
              nPickupOnly,
              nBadRead);
  return 0;
}
