#ifndef RecoTracker_LSTCore_interface_ChainIncidenceHostCollection_h
#define RecoTracker_LSTCore_interface_ChainIncidenceHostCollection_h

#include "RecoTracker/LSTCore/interface/ChainIncidenceSoA.h"
#include "DataFormats/Portable/interface/PortableHostCollection.h"

namespace lst {
  using ChainIncidenceHostCollection = PortableHostCollection<ChainIncidenceSoA>;
}  // namespace lst
#endif
