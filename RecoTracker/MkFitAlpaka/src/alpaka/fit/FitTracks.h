#ifndef RecoTracker_MkFitAlpaka_src_alpaka_fit_FitTracks_h
#define RecoTracker_MkFitAlpaka_src_alpaka_fit_FitTracks_h

// Exported entry point of the device mkFit final fit (src/alpaka/FitTracks.dev.cc). Callers never instantiate the
// kernel themselves (DESIGN D-layout).

#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/MkFitAlpaka/interface/es/ESView.h"
#include "RecoTracker/MkFitAlpaka/interface/fit/CpeGeneric.h"
#include "RecoTracker/MkFitAlpaka/interface/fit/FitOuterStateSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/fit/HitStateDev.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/HitSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::fit {

  using ::mkfitdev::ESView;

  // Device-side counters of one event (all int32; the caller zeroes them).
  struct FitCounters {
    int32_t nRefit;             // tracks refitted in pass 2 (stock "remap")
    int32_t nRemovedHits;       // outlier hits removed in pass 1
    int32_t nNaN;               // fit directions whose chi2 sum was NaN (state kept, stock reFitOutputTracks)
    int32_t nHitCountMismatch;  // nFoundHits != number of hits with index >= 0 (stock indexes out of range then)
    int32_t nOverflow;          // more than kMaxTrkHits hits to order (never expected)
    int32_t nShadowed;          // hits passing the outlier cut but hidden by the std::map scorer collision (D-H2b)
  };

  // Outlier switches of the final fit (DEVIATION D3, round 8; defaults = stock MkBuilder::fit_tracks):
  // edgeOutliers: also test the first and last fitted hit, each with the pass that predicts it from all other hits
  //   (chi2 > edgeChi2Cut, the KF final fit's EstimateCut); stock tests interior-style cuts only.
  // outlierRounds: refits after outlier removal; every refit but the last is checked for outliers again (stock: 1).
  struct FitOptions {
    bool edgeOutliers = false;
    int outlierRounds = 1;
    float edgeChi2Cut = 20.f;
    // firstHitProp (DEVIATION D7, round 9): trackreco#186 updates the first hit of a pass without propagation; after the
    //   innermost hit was removed as an outlier the refit's state is not on the new first hit's plane. 1 = propagate to
    //   the first hit in the refits (pass 2+), 2 = in every pass, 0 = stock.
    int firstHitProp = 0;
  };

  // Fits rows [0, nTracks) of `tracks` in place: parameters, errors, chi2 at the innermost hit; removed outliers get
  // hit index -1 and nFoundHits is decremented (stock Track::removeHit). Charge, score and status are not touched.
  // `hits` is the event HitSoA (pixel wrapper rows first, then strip rows starting at nPixel); `es` the device ES.
  // `counters` must point to zeroed device memory. Asynchronous in `queue`.
  // useCpe: apply the device PixelCPEGeneric with the track angles on every pixel hit (stock menu behaviour);
  // `cpe` (device tables) and `clusters` ([nPixel], device) are then required. false = the no-CPE variant.
  // hitStates ([nTracks * kMaxTrkHits], device; nullptr = off): stock storeHitStates (trackreco#186), the smoothed
  // local state at every HitOnTrack position, element t * kMaxTrkHits + position.
  // nTracksDev (round 6, lane gpu: device build -> fit handoff): if set, the row count is read on the device from
  // this address (e.g. the TrackSoA nTracks scalar of the device candCutSel) and nTracks is the CAPACITY (grid size,
  // scratch stride); rows [*nTracksDev, nTracks) are not touched. No host synchronization.
  // outerStates (DEVIATION D6; nullptr = off; >= nTracks rows, pos pre-set to -1 by the caller): per fitted row the
  // forward pass's updated state at the outermost fitted hit of the final fit (see interface/fit/FitOuterStateSoA.h).
  void runFinalFit(Queue& queue,
                   ESView const& es,
                   ::mkfitdev::HitSoAConstView hits,
                   uint32_t nPixel,
                   ::mkfitdev::TrackSoAView tracks,
                   int nTracks,
                   FitCounters* counters,
                   bool useCpe = false,
                   ::mkfitdev::cpe::CpeTables cpe = {},
                   const ::mkfitdev::cpe::ClusterCpe* clusters = nullptr,
                   ::mkfitdev::fit::HitStateDev* hitStates = nullptr,
                   const int32_t* nTracksDev = nullptr,
                   FitOptions const& options = FitOptions{},
                   ::mkfitdev::FitOuterStateSoAView* outerStates = nullptr);

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::fit

#endif
