#ifndef RecoTracker_LSTCore_interface_ChainEdgesDeviceCollection_h
#define RecoTracker_LSTCore_interface_ChainEdgesDeviceCollection_h

#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainEdgesSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {
  using ChainEdgesDeviceCollection = PortableCollection<ChainEdgesSoA>;
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst
#endif
