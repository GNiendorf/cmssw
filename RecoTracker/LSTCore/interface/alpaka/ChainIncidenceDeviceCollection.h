#ifndef RecoTracker_LSTCore_interface_ChainIncidenceDeviceCollection_h
#define RecoTracker_LSTCore_interface_ChainIncidenceDeviceCollection_h

#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainIncidenceSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {
  using ChainIncidenceDeviceCollection = PortableCollection<ChainIncidenceSoA>;
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst
#endif
