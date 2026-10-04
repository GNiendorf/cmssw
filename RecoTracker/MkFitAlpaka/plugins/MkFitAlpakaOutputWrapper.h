#ifndef RecoTracker_MkFitAlpaka_plugins_MkFitAlpakaOutputWrapper_h
#define RecoTracker_MkFitAlpaka_plugins_MkFitAlpakaOutputWrapper_h

// HOST: TrackSoA (host copy of the device output) -> stock MkFitOutputWrapper, so that the STOCK MkFitOutputConverter
// (TrackCandidates) and MkFitOutputTrackConverter (reco::Tracks of the mkFit final fit) run unchanged downstream.
// Row order is kept (= stock export order). propagatedToFirstLayer as stock MkFitProducer: !backwardFitInCMSSW.

#include "RecoTracker/MkFit/interface/MkFitOutputWrapper.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAMkFitConversion.h"

namespace mkfitdev {

  inline MkFitOutputWrapper makeMkFitOutputWrapper(TrackSoAConstView v, bool propagatedToFirstLayer) {
    return MkFitOutputWrapper(tracksFromSoA(v), propagatedToFirstLayer);
  }

}  // namespace mkfitdev

#endif
