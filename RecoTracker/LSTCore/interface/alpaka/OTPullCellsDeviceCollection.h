#ifndef RecoTracker_LSTCore_interface_alpaka_OTPullCellsDeviceCollection_h
#define RecoTracker_LSTCore_interface_alpaka_OTPullCellsDeviceCollection_h

#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/LSTCore/interface/OTPullCellsSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {
  using OTPullCellsDeviceCollection = PortableCollection<::lst::OTPullCellsSoA>;
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
