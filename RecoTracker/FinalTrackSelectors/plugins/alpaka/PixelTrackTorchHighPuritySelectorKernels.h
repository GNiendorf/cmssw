#ifndef RecoTracker_FinalTrackSelectors_plugins_alpaka_PixelTrackTorchHighPuritySelectorKernels_h
#define RecoTracker_FinalTrackSelectors_plugins_alpaka_PixelTrackTorchHighPuritySelectorKernels_h

#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "DataFormats/TrackSoA/interface/TracksDevice.h"
#include "DataFormats/TrackSoA/interface/TracksHost.h"
#include "DataFormats/TrackSoA/interface/alpaka/TracksSoACollection.h"
#include "DataFormats/TrackSoA/interface/TracksSoA.h"
#include "DataFormats/TrackSoA/interface/TrackDefinitions.h"
#include "DataFormats/TrackingRecHitSoA/interface/OTRecHitsSoA.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsSoA.h"

#include "RecoTracker/FinalTrackSelectors/interface/PixelTrackFeaturesSoA.h"
#include "RecoTracker/PixelSeeding/interface/CAHitsView.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  void launchCAPreselection(Queue& queue,
                            const int maxNumberOfTracks,
                            const int minNumberOfHits,
                            const ::pixelTrack::Quality minimumTrackQuality,
                            const ::reco::TrackSoAConstView tracks,
                            int* preselectedTrackIndices,
                            int* preselectionOffsets,
                            int* nPreselectedTracks);

  void launchFeaturesExtractor(Queue& queue,
                               const int maxPreselectedTracks,
                               const ::reco::TrackSoAConstView tracks,
                               const ::reco::TrackHitSoAConstView track_hits,
                               const caStructures::CAHitsView hits,
                               const int nHitsTot,
                               // Raw OT-rechit view + count so the feature walk resolves tagged
                               // OT extras (nOTHits == 0 / empty view => merged-hits-only).
                               const ::reco::OTRecHitsConstView otHits,
                               const uint32_t nOTHits,
                               const int* preselectedTrackIndices,
                               const int* nPreselectedTracks,
                               PixelTrackFitFeaturesView fitFeatures,
                               PixelTrackHitFeaturesView hitFeatures,  // sized 0: no hit-feature walk
                               int* trackHitCounts);

  // Score working points held on the device. When thresholds is set, the score cut of a track is the entry of
  // its (pT bin, |eta| bin) cell, the bin of a value being the number of edges at or below it; tracks with
  // exactly three hits use thresholdsThreeHit when it is set. Each block is row-major in pT, with
  // (nPtEdges + 1) x (nAbsEtaEdges + 1) entries. A non-negative highPurityThreshold marks the selected tracks with
  // at least highPurityMinNumberOfHits hits and a score at or above it highPurity and the other selected tracks
  // at most tight, so a quality cut downstream gives a high-purity subset.
  struct ScoreWorkingPoints {
    const float* ptEdges = nullptr;
    const float* absEtaEdges = nullptr;
    const float* thresholds = nullptr;
    const float* thresholdsThreeHit = nullptr;
    int nPtEdges = 0;
    int nAbsEtaEdges = 0;
    float highPurityThreshold = -1.f;
    int highPurityMinNumberOfHits = 0;
  };

  // selectedHighPurity (may be null): per selected track, whether it is in the high-purity subset; filled only
  // when wp.highPurityThreshold is non-negative.
  void launchScoreFilter(Queue& queue,
                         const int maxPreselectedTracks,
                         const double scoreThreshold,
                         const double scoreThresholdLowDxy,
                         const double dxyRampKnee,
                         const bool passThreeHitTracks,
                         const PixelTrackFitFeaturesConstView fitFeatures,
                         const PixelTrackScoresSoA::View trackScores,
                         const int* preselectedTrackIndices,
                         const int* nPreselectedTracks,
                         const int* trackHitCounts,
                         int* selectedTrackIndices,
                         int* nSelectedTracks,
                         int* selectedTrackHitOffsets,
                         const ScoreWorkingPoints& wp = ScoreWorkingPoints{},
                         int* selectedHighPurity = nullptr);

  // selectedHighPurity (optional, may be null): the high-purity flags from launchScoreFilter; when given, the output
  // tracks carry quality highPurity for the flagged tracks and at most tight for the others.
  // selectedCounts (optional, may be null): 2-word device buffer the compaction kernel fills with
  // [0] tracks written into the output SoA, [1] hits written into the output hit block. Both are exact,
  // so a consumer can size against them.
  reco::TracksSoACollection launchProduceOutputTracks(Queue& queue,
                                                      const int maxPreselectedTracks,
                                                      const int avgHitsPerTrack,
                                                      const ::reco::TrackSoAConstView tracks,
                                                      const ::reco::TrackHitSoAConstView track_hits,
                                                      const int* selectedTrackIndices,
                                                      const int* nSelectedTracks,
                                                      const int* selectedTrackHitOffsets,
                                                      uint32_t* selectedCounts = nullptr,
                                                      const int* selectedHighPurity = nullptr);
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#endif  // RecoTracker_FinalTrackSelectors_plugins_alpaka_PixelTrackTorchHighPuritySelectorKernels_h
