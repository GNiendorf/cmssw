#ifndef RecoTracker_MkFitAlpaka_interface_cands_alpaka_CandSelect_h
#define RecoTracker_MkFitAlpaka_interface_cands_alpaka_CandSelect_h

// Host entry point of the per-seed candidate selection (device CandCloner::processSeedRange).
// One thread per seed; instantiated in src/alpaka/Cands.dev.cc. Round-1 entry (extras given by the caller); the
// engine step uses KernelEngineSelect (interface/cands/alpaka/CandsEngine.h).

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandSelection.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandsSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev {

  // For every seed s < nSeeds: reads the current candidates (buffer seeds.curBuf(s)), the option slots
  // [optRow(s,0), optRow(s,kMaxOptsPerSeed)), the extras and the best-short candidate; writes the new candidates
  // into the other buffer and flips curBuf (or leaves the seed untouched exactly where stock does), appends HoT
  // nodes, fills the per-seed update / overlap lists (CandUpdatesSoA rows updRow(s, u), counts in
  // seeds.nUpdates / nOverlapUpdates), copies parent states into the new slots, and clears the extras.
  // Overflows are flagged per seed (overflowBits) and counted in the SeedCandsSoA scalars.
  void selectCandidates(Queue& queue,
                        ::mkfitdev::SeedCandsSoA::View seeds,
                        ::mkfitdev::CandSlotsSoA::View slots,
                        ::mkfitdev::CandHotsSoA::View hots,
                        ::mkfitdev::CandOptionsSoA::ConstView opts,
                        ::mkfitdev::CandExtrasSoA::ConstView extras,
                        ::mkfitdev::CandUpdatesSoA::View upds,
                        ::mkfitdev::SeedSelParams const& params,
                        int nSeeds);

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev

#endif
