#ifndef RecoTracker_LSTCore_interface_ChainsHostCollection_h
#define RecoTracker_LSTCore_interface_ChainsHostCollection_h

#include "RecoTracker/LSTCore/interface/ChainsSoA.h"
#include "DataFormats/Portable/interface/PortableHostCollection.h"

namespace lst {
  using ChainsHostCollection = PortableHostCollection<ChainsSoA>;
  using ChainItemsHostCollection = PortableHostCollection<ChainItemsSoA>;
}  // namespace lst
#endif
