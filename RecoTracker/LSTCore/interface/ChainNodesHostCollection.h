#ifndef RecoTracker_LSTCore_interface_ChainNodesHostCollection_h
#define RecoTracker_LSTCore_interface_ChainNodesHostCollection_h

#include "RecoTracker/LSTCore/interface/ChainNodesSoA.h"
#include "DataFormats/Portable/interface/PortableHostCollection.h"

namespace lst {
  // Host-side dense triplet nodes of the chain-tracking graph (ChainNodesSoA.h).
  using ChainNodesHostCollection = PortableHostCollection<ChainNodesSoA>;
}  // namespace lst
#endif
