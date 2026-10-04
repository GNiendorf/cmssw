#ifndef RecoTracker_MkFitAlpaka_src_alpaka_cands_CandSeedOpsKernels_h
#define RecoTracker_MkFitAlpaka_src_alpaka_cands_CandSeedOpsKernels_h

// SoA kernels (one thread per seed) for the per-seed bookkeeping ops of interface/cands/CandSeedOps.h:
//   K1 activateSeeds   (MkBuilder::find_tracks_unroll_candidates)  + per-step resets (layer, extras, option slots)
//   K6 mergeSeeds      (CombCandidate::mergeCandsAndBestShortOne after the last plan step)
//   compactifySeeds + beginBkwSearchSeeds (before the backward search)
//   filterSeeds        (MkBuilder::filter_comb_cands per seed; writes a pass flag, the compaction of surviving
//                       seeds keeping stock order is a prefix scan over the flags, done by the caller)
// The candidate kinematics for K1 (pT, posRsq, posPhi = vdt fast_atan2f(y, x), momPhi) come in a CandKin array with
// rows candSlotRow-compatible (s * kMaxCandsPerSeed + ic); round 3 computes them in K1 with mplex's portable math.

#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandSeedOps.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandsSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev {

  using namespace ::mkfitdev;

  ALPAKA_FN_ACC inline SeedCandsRef makeSeedRef(SeedCandsSoA::View seeds,
                                                CandSlotsSoA::View slots,
                                                CandHotsSoA::View hots,
                                                int s,
                                                int hps) {
    SeedCandsRef r;
    const int cur = seeds.curBuf(s);
    r.state = &seeds.state(s);
    r.pickupLayer = &seeds.pickupLayer(s);
    r.cands = &slots.book(candSlotRow(s, cur, 0));
    r.states = &slots.state(candSlotRow(s, cur, 0));
    r.nCands = &seeds.nCands(s);
    r.bestShort = &seeds.bestShort(s);
    r.bestShortState = &slots.state(bestShortRow(s));
    r.bestShortValid = &seeds.bestShortValid(s);
    r.hots = &hots.node(hotRow(s, 0, hps));
    r.hotOffset = 0;
    r.hotCap = hps;
    r.nHots = &seeds.nHots(s);
    r.lastHitIdxBeforeBkw = &seeds.lastHitIdxBeforeBkw(s);
    r.nInsideMinusOneBeforeBkw = &seeds.nInsideMinusOneBeforeBkw(s);
    r.nTailMinusOneBeforeBkw = &seeds.nTailMinusOneBeforeBkw(s);
    r.overflowBits = &seeds.overflowBits(s);
    return r;
  }

  ALPAKA_FN_ACC inline void countHotOverflow(Acc1D const& acc, SeedCandsSoA::View seeds, int s, uint32_t ovf0) {
    if ((seeds.overflowBits(s) & kOverflowHotsBit) && !(ovf0 & kOverflowHotsBit))
      alpaka::atomicAdd(acc, &seeds.nOverflowHots(), 1u, alpaka::hierarchy::Blocks{});
  }

  class KernelActivateSeeds {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  SeedCandsSoA::View seeds,
                                  CandSlotsSoA::View slots,
                                  CandHotsSoA::View hots,
                                  CandOptionsSoA::View opts,
                                  const CandKin* kin,
                                  const int16_t* stepLayer,      // per region: layer of this plan step (-1: none)
                                  const int16_t* stepPrevLayer,  // per region: layer of the previous plan step
                                  bool pickupOnly,
                                  bool fwdSearch,
                                  float minPtCut,
                                  int nSeeds) const {
      const int hps = seeds.hotsPerSeed();
      for (int32_t s : cms::alpakatools::uniform_elements(acc, nSeeds)) {
        const int reg = seeds.region(s);
        const int layer = stepLayer[reg];
        seeds.nExtras(s) = 0;
        seeds.nActive(s) = 0;
        seeds.activeMask(s) = 0;
        if (layer < 0)
          continue;  // this region's plan has no step here
        seeds.layer(s) = layer;
        const uint32_t ovf0 = seeds.overflowBits(s);
        SeedCandsRef r = makeSeedRef(seeds, slots, hots, s, hps);
        int32_t active[kMaxCandsPerSeed];
        int32_t nActive = 0;
        activateSeedCands(r,
                          kin + s * kMaxCandsPerSeed,
                          layer,
                          stepPrevLayer[reg],
                          pickupOnly,
                          fwdSearch,
                          minPtCut,
                          active,
                          &nActive);
        uint8_t mask = 0;
        for (int k = 0; k < nActive; ++k)
          mask |= uint8_t(1u << active[k]);
        seeds.nActive(s) = nActive;
        seeds.activeMask(s) = mask;
        if (nActive > 0)
          for (int j = 0; j < kMaxOptsPerSeed; ++j)
            opts.opt(optRow(s, j)).hitIdx = kOptEmpty;
        countHotOverflow(acc, seeds, s, ovf0);
      }
    }
  };

  class KernelMergeSeeds {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  SeedCandsSoA::View seeds,
                                  CandSlotsSoA::View slots,
                                  CandHotsSoA::View hots,
                                  int maxCandsPerSeed,
                                  bool updateScore,
                                  bool sortCands,
                                  int nSeeds) const {
      const int hps = seeds.hotsPerSeed();
      for (int32_t s : cms::alpakatools::uniform_elements(acc, nSeeds)) {
        SeedCandsRef r = makeSeedRef(seeds, slots, hots, s, hps);
        float pt[kMaxCandsPerSeed];
        for (int ic = 0; ic < *r.nCands; ++ic) {
          const float v = 1.f / r.states[ic].par[3];  // TrackState::pT()
          pt[ic] = v < 0.f ? -v : v;
        }
        const float vb = 1.f / r.bestShortState->par[3];
        mergeCandsAndBestShortOne(r, maxCandsPerSeed, updateScore, sortCands, pt, vb < 0.f ? -vb : vb);
      }
    }
  };

  class KernelCompactifyBeginBkw {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  SeedCandsSoA::View seeds,
                                  CandSlotsSoA::View slots,
                                  CandHotsSoA::View hots,
                                  bool removeSeedHits,
                                  int backwardFitMinHits,
                                  bool doCompactify,
                                  bool doBeginBkw,
                                  int nSeeds) const {
      const int hps = seeds.hotsPerSeed();
      for (int32_t s : cms::alpakatools::uniform_elements(acc, nSeeds)) {
        if (seeds.nCands(s) <= 0)
          continue;
        const uint32_t ovf0 = seeds.overflowBits(s);
        SeedCandsRef r = makeSeedRef(seeds, slots, hots, s, hps);
        if (doCompactify)
          compactifyHitStorageForBestCand(r, removeSeedHits, backwardFitMinHits);
        if (doBeginBkw)
          beginBkwSearch(r);
        countHotOverflow(acc, seeds, s, ovf0);
      }
    }
  };

  class KernelFilterSeeds {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  SeedCandsSoA::View seeds,
                                  CandSlotsSoA::View slots,
                                  CandHotsSoA::View hots,
                                  int8_t* passed,
                                  bool bkwRep,
                                  bool attemptAllCands,
                                  int minHitsQF,
                                  int nSeeds) const {
      const int hps = seeds.hotsPerSeed();
      for (int32_t s : cms::alpakatools::uniform_elements(acc, nSeeds)) {
        if (seeds.nCands(s) <= 0) {
          passed[s] = 0;
          continue;
        }
        SeedCandsRef r = makeSeedRef(seeds, slots, hots, s, hps);
        passed[s] = filterSeedCands(r, bkwRep, attemptAllCands, minHitsQF) ? 1 : 0;
      }
    }
  };

  inline void filterSeedsImpl(Queue& queue,
                              SeedCandsSoA::View seeds,
                              CandSlotsSoA::View slots,
                              CandHotsSoA::View hots,
                              int8_t* passed,
                              bool bkwRep,
                              bool attemptAllCands,
                              int minHitsQF,
                              int nSeeds) {
    if (nSeeds <= 0)
      return;
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(nSeeds, 128), 128);
    alpaka::exec<Acc1D>(
        queue, wd, KernelFilterSeeds{}, seeds, slots, hots, passed, bkwRep, attemptAllCands, minHitsQF, nSeeds);
  }

  inline void activateSeedsImpl(Queue& queue,
                                SeedCandsSoA::View seeds,
                                CandSlotsSoA::View slots,
                                CandHotsSoA::View hots,
                                CandOptionsSoA::View opts,
                                const CandKin* kin,
                                const int16_t* stepLayer,
                                const int16_t* stepPrevLayer,
                                bool pickupOnly,
                                bool fwdSearch,
                                float minPtCut,
                                int nSeeds) {
    if (nSeeds <= 0)
      return;
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(nSeeds, 128), 128);
    alpaka::exec<Acc1D>(queue,
                        wd,
                        KernelActivateSeeds{},
                        seeds,
                        slots,
                        hots,
                        opts,
                        kin,
                        stepLayer,
                        stepPrevLayer,
                        pickupOnly,
                        fwdSearch,
                        minPtCut,
                        nSeeds);
  }

  inline void mergeSeedsImpl(Queue& queue,
                             SeedCandsSoA::View seeds,
                             CandSlotsSoA::View slots,
                             CandHotsSoA::View hots,
                             int maxCandsPerSeed,
                             bool updateScore,
                             bool sortCands,
                             int nSeeds) {
    if (nSeeds <= 0)
      return;
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(nSeeds, 128), 128);
    alpaka::exec<Acc1D>(
        queue, wd, KernelMergeSeeds{}, seeds, slots, hots, maxCandsPerSeed, updateScore, sortCands, nSeeds);
  }

  inline void compactifyBeginBkwImpl(Queue& queue,
                                     SeedCandsSoA::View seeds,
                                     CandSlotsSoA::View slots,
                                     CandHotsSoA::View hots,
                                     bool removeSeedHits,
                                     int backwardFitMinHits,
                                     bool doCompactify,
                                     bool doBeginBkw,
                                     int nSeeds) {
    if (nSeeds <= 0)
      return;
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(nSeeds, 128), 128);
    alpaka::exec<Acc1D>(queue,
                        wd,
                        KernelCompactifyBeginBkw{},
                        seeds,
                        slots,
                        hots,
                        removeSeedHits,
                        backwardFitMinHits,
                        doCompactify,
                        doBeginBkw,
                        nSeeds);
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev

#endif
