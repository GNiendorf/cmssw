#ifndef RecoTracker_MkFitAlpaka_plugins_alpaka_MkFitAlpakaHpFeaturesKernels_h
#define RecoTracker_MkFitAlpaka_plugins_alpaka_MkFitAlpakaHpFeaturesKernels_h

// Stage C, device HP selection, step 2 (round 7, lane hpsel; doc/hpsel.txt): the 15 inputs of the initial step's HP
// Torch classifier (TrackFeatureExtractor) computed on the device from the fitted TrackSoA, one thread per track.
//   exact from the fit output (same arithmetic as the host converter + reco::TrackBase accessors):
//     validPixelHits, validStripHits (valid hits per layer type), ndof (= 2 per pixel hit + 1 per Phase-2 OT hit - 5,
//     MkFitOutputTrackConverter), normalizedChi2 (float chi2 / float ndof, TrackBase::normalizedChi2),
//     layersWithoutMeas (0: the converter appends no invalid TRACK_HITS);
//   PROTOTYPE (mode kHelixUniformBz): eta, phi, dxy, dz from a helix in a uniform Bz (mkFit's field at the innermost
//     hit) from the fitted state at the innermost hit to the beam spot; the host uses TSCBLBuilderNoMaterial with
//     CMSSW's field and the tilted beam line. Lane outconv's device PCA state replaces this;
//   NOT on the device: dxyError, dzError, etaError, phiError (need the PCA covariance, lane outconv) and
//     lostInnerHits, lostOuterHits (the converter's NavigationSchool + compatibleDets + MeasurementTracker activity):
//     written as NaN.
#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/FinalTrackSelectors/interface/TrackTorchClassifierFeaturesSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/es/ESLayouts.h"
#include "RecoTracker/MkFitAlpaka/interface/math/Config.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoA.h"

namespace mkfitdev::hpsel {
  struct Params {
    float bsx, bsy, bsz;  // reco::BeamSpot::position()
    ::mkfitdev::Config::BFieldParams bField;
  };
}  // namespace mkfitdev::hpsel

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::hpsel {
  // rows [0, n) of trk -> rows [0, n) of out
  void launchFeatures(Queue& queue,
                      ::mkfitdev::TrackSoAConstView trk,
                      int n,
                      ::mkfitdev::LayerInfoSoA::ConstView layers,
                      ::mkfitdev::hpsel::Params const& p,
                      TrackTorchClassifierFeaturesSoA::View out);
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::hpsel

#endif
