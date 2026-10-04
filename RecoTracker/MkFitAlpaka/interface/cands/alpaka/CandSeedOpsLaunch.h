#ifndef RecoTracker_MkFitAlpaka_interface_cands_alpaka_CandSeedOpsLaunch_h
#define RecoTracker_MkFitAlpaka_interface_cands_alpaka_CandSeedOpsLaunch_h

// Host entry points of the per-seed bookkeeping ops (one thread per seed). The kernels live in
// src/alpaka/cands/CandSeedOpsKernels.h and are instantiated once, in src/alpaka/Cands.dev.cc (library symbols).
// Callers (producers, tests) use these functions and never launch the kernels themselves (nvlink duplicate-RDC rule).

#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandSeedOps.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandsSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev {

  // filter_comb_cands per seed: passed[s] = 0/1.
  void filterSeeds(Queue& queue,
                   ::mkfitdev::SeedCandsSoA::View seeds,
                   ::mkfitdev::CandSlotsSoA::View slots,
                   ::mkfitdev::CandHotsSoA::View hots,
                   int8_t* passed,
                   bool bkwRep,
                   bool attemptAllCands,
                   int minHitsQF,
                   int nSeeds);

  // K1: activate the candidates of every seed for the next layer step.
  void activateSeeds(Queue& queue,
                     ::mkfitdev::SeedCandsSoA::View seeds,
                     ::mkfitdev::CandSlotsSoA::View slots,
                     ::mkfitdev::CandHotsSoA::View hots,
                     ::mkfitdev::CandOptionsSoA::View opts,
                     const ::mkfitdev::CandKin* kin,
                     const int16_t* stepLayer,
                     const int16_t* stepPrevLayer,
                     bool pickupOnly,
                     bool fwdSearch,
                     float minPtCut,
                     int nSeeds);

  // K6: mergeCandsAndBestShortOne per seed.
  void mergeSeeds(Queue& queue,
                  ::mkfitdev::SeedCandsSoA::View seeds,
                  ::mkfitdev::CandSlotsSoA::View slots,
                  ::mkfitdev::CandHotsSoA::View hots,
                  int maxCandsPerSeed,
                  bool updateScore,
                  bool sortCands,
                  int nSeeds);

  // compactifyHitStorageForBestCand and/or beginBkwSearch per seed.
  void compactifyBeginBkw(Queue& queue,
                          ::mkfitdev::SeedCandsSoA::View seeds,
                          ::mkfitdev::CandSlotsSoA::View slots,
                          ::mkfitdev::CandHotsSoA::View hots,
                          bool removeSeedHits,
                          int backwardFitMinHits,
                          bool doCompactify,
                          bool doBeginBkw,
                          int nSeeds);

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev

#endif
