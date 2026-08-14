#ifndef RecoTracker_LSTCore_interface_ChainEdgesDeviceCollection_h
#define RecoTracker_LSTCore_interface_ChainEdgesDeviceCollection_h

#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainEdgesSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {
  // Device-side edge list of the chain-tracking triplet graph (ChainEdgesSoA.h), allocated per
  // event at the exact edge count the incidence degree arithmetic gives.
  using ChainEdgesDeviceCollection = PortableCollection<ChainEdgesSoA>;
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst
#endif
