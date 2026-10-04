#ifndef RecoTracker_MkFitAlpaka_plugins_alpaka_MkFitAlpakaOTRecHitsKernels_h
#define RecoTracker_MkFitAlpaka_plugins_alpaka_MkFitAlpakaOTRecHitsKernels_h

// Stage D (round 8, lane otdev): device Phase2StripCPE kernel, one thread per OT cluster (doc/otdev.txt).
#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/MkFitAlpaka/interface/othits/OTRecHitSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::otdev {

  // fills lx, ly, exx, eyy, gx, gy, gz of rows [0, n) from module, clustSize, strip and the per-module table
  // (index = module - firstIndex); contractLocal / contractGlobal = mkfitdev::Contract (0 fuse second, 1 fuse first,
  // 2 no fuse)
  void runOTCpe(Queue& queue,
                ::mkfitdev::OTRecHitSoA::View view,
                ::mkfitdev::OTCpeModule const* table,
                int32_t firstIndex,
                uint32_t n,
                int contractLocal,
                int contractGlobal);

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::otdev

#endif
