#ifndef RecoTracker_MkFitAlpaka_interface_alpaka_fit_FitOuterStateDeviceCollection_h
#define RecoTracker_MkFitAlpaka_interface_alpaka_fit_FitOuterStateDeviceCollection_h

#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/MkFitAlpaka/interface/fit/FitOuterStateHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/fit/FitOuterStateSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev {
  using ::mkfitdev::FitOuterStateSoA;
  using ::mkfitdev::FitOuterStateSoAConstView;
  using ::mkfitdev::FitOuterStateSoAView;
  using FitOuterStateDeviceCollection = PortableCollection<FitOuterStateSoA>;
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev

#endif
