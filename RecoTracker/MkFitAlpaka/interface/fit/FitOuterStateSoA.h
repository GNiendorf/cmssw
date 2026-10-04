#ifndef RecoTracker_MkFitAlpaka_interface_fit_FitOuterStateSoA_h
#define RecoTracker_MkFitAlpaka_interface_fit_FitOuterStateSoA_h

// DEVIATION D6 (R7-H3; switch storeOuterState of MkFitAlpakaFitDeviceProducer, default off): per fitted track, the final
// fit's state at its outermost fitted hit = the forward pass's updated state there (stock storeHitStates writes the
// same state as that hit's HitStateOnTrack, kind ForwardOnly). Row t belongs to row t of the fitted TrackSoA.
// params / errors: CCS state as TrackSoA (TrackParams / TrackErrors); pos: HitOnTrack position of the outermost fitted
// hit, -1 = no state (row not fitted, or the forward chi2 sum was NaN and the track kept its previous state).

#include <cstdint>

#include "DataFormats/SoATemplate/interface/SoALayout.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoA.h"

namespace mkfitdev {

  GENERATE_SOA_LAYOUT(FitOuterStateLayout,
                      SOA_COLUMN(TrackParams, params),
                      SOA_COLUMN(TrackErrors, errors),
                      SOA_COLUMN(int16_t, pos))

  using FitOuterStateSoA = FitOuterStateLayout<>;
  using FitOuterStateSoAView = FitOuterStateSoA::View;
  using FitOuterStateSoAConstView = FitOuterStateSoA::ConstView;

}  // namespace mkfitdev

#endif
