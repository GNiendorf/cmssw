#ifndef RecoTracker_LSTCore_interface_ChainEdgesHostCollection_h
#define RecoTracker_LSTCore_interface_ChainEdgesHostCollection_h

#include "RecoTracker/LSTCore/interface/ChainEdgesSoA.h"
#include "DataFormats/Portable/interface/PortableHostCollection.h"

namespace lst {
  // Host-side edge list of the chain-tracking triplet graph (ChainEdgesSoA.h).
  using ChainEdgesHostCollection = PortableHostCollection<ChainEdgesSoA>;
}  // namespace lst
#endif
