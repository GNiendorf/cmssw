#ifndef RecoTracker_LSTCore_interface_ChainNodesDeviceCollection_h
#define RecoTracker_LSTCore_interface_ChainNodesDeviceCollection_h

#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainNodesSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {
  using ChainNodesDeviceCollection = PortableCollection<ChainNodesSoA>;
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst
#endif
