#ifndef RecoTracker_MkFitAlpaka_plugins_alpaka_MkFitAlpakaNavKernels_h
#define RecoTracker_MkFitAlpaka_plugins_alpaka_MkFitAlpakaNavKernels_h
// Stage C (round 8, lane stagec): device navigation prototype kernel, one thread per (track, direction)
#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

#include "../MkFitAlpakaNavDevice.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::navdev {
  void launchNav(Queue& queue,
                 ::mkfitdev::nav::Table const* table,
                 ::mkfitdev::nav::TrackIn const* in,
                 uint64_t* out,  // [2 * i + d], d = 0 inner/opposite, 1 outer/along
                 int n);
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::navdev

#endif
