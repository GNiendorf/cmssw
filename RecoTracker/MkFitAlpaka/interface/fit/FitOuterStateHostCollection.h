#ifndef RecoTracker_MkFitAlpaka_interface_fit_FitOuterStateHostCollection_h
#define RecoTracker_MkFitAlpaka_interface_fit_FitOuterStateHostCollection_h

#include "DataFormats/Portable/interface/PortableHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/fit/FitOuterStateSoA.h"

namespace mkfitdev {
  using FitOuterStateHostCollection = PortableHostCollection<FitOuterStateSoA>;
}  // namespace mkfitdev

#endif
