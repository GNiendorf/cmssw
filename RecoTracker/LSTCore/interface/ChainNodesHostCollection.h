#ifndef RecoTracker_LSTCore_interface_ChainNodesHostCollection_h
#define RecoTracker_LSTCore_interface_ChainNodesHostCollection_h

#include "RecoTracker/LSTCore/interface/ChainNodesSoA.h"
#include "DataFormats/Portable/interface/PortableHostCollection.h"

namespace lst {
  using ChainNodesHostCollection = PortableHostCollection<ChainNodesSoA>;
}  // namespace lst
#endif
