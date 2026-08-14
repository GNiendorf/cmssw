#ifndef RecoTracker_LSTCore_interface_alpaka_ChainsDeviceCollection_h
#define RecoTracker_LSTCore_interface_alpaka_ChainsDeviceCollection_h

#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainsSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {
  // Device-side welded chains and their CSR payloads (ChainsSoA.h). Both are per-event: they are
  // sized from device counts the weld produces, so they are allocated inside the event loop.
  using ChainsDeviceCollection = PortableCollection<ChainsSoA>;
  using ChainItemsDeviceCollection = PortableCollection<ChainItemsSoA>;
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst
#endif
