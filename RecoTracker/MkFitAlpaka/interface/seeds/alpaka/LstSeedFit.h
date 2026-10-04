#ifndef RecoTracker_MkFitAlpaka_interface_seeds_alpaka_LstSeedFit_h
#define RecoTracker_MkFitAlpaka_interface_seeds_alpaka_LstSeedFit_h

// O6-1 option (b), lane seeds round 6: the seed state of the LST T5/T4/pT3/pT5 seeds from a device mkFit Kalman
// fit of the seed's own hits, instead of the host CMSSW seed creator (LSTOutputConverter makeSeed: FastHelix from
// two hits + the origin, KF with PropagatorWithMaterial) + MkFitSeedConverter. pLS seeds keep the copied pixel state.
// SWITCH-GATED (build module parameter lstSeedFit, default false); a DEVIATION candidate (doc/seeds.txt R6).
// Exported entry point (src/alpaka/LstSeeds.dev.cc); callers never instantiate the kernel (DESIGN D-layout).

#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/MkFitAlpaka/interface/es/ESView.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/HitSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/SeedSoA.h"

namespace mkfitdev::lstseeds {

  // Per-event device counters (int32; the caller zeroes them).
  struct LstSeedFitCounters {
    int32_t nPixelOnly;     // seeds with no outer-tracker hit (pLS, or pT3/pT5 whose host makeSeed fell back): kept
    int32_t nFitted;        // seeds whose state was replaced by the device fit
    int32_t nTooFewHits;    // < 3 hits with an OT hit (never expected for LST seeds): kept
    int32_t nFailed;        // fit failed (propagation fail flag or non-finite state/chi2): host state kept
    int32_t nChargeFlip;    // fitted charge != the host seed's charge (diagnostic)
    int32_t nOverflowHits;  // more than kMaxSeedHits hits (never: the packer truncates and counts)
    int32_t nRejDPhi;       // host-structure mode: a step turned by more than MaxDPhi (part of nFailed)
    int32_t nRejBackward;   // host-structure mode: a step against the momentum (part of nFailed)
    int32_t nSafeRetry;     // host-structure mode: float failure with the exact prior, refitted with the capped prior
    int32_t nFallback;      // host-structure mode, dropFailed: a failed pixel+OT (pT3/pT5) seed refitted on its leading
                            // pixel hits only and truncated to them (emulates LSTOutputConverter's pixel-seed fallback)
    int32_t nFallbackFailed;  // ... and that pixel-only refit failed too (seed dropped)
  };

  // Per-seed outcome of the host-structure fit (written to `st` when given, for the validation dump).
  enum LstSeedFitStatus : int8_t {
    kStNotFitted = -1,  // pixel-only / too few hits / not host-structure mode
    kStOk = 0,
    kStRejDPhi = 1,
    kStRejBackward = 2,
    kStRejInvalid = 3,
    kStFallbackOk = 4,      // failed (any reason), refitted on the pixel hits and truncated
    kStFallbackFailed = 5,  // failed, pixel refit failed too
    kStDroppedOT = 6        // failed T5/T4 (OT-only) seed dropped (dropFailed; host: TC skipped)
  };

  // originPrior value selecting the host-creator structure (D7-b option b', round 7): FastHelix through the region
  // origin, the creator's initial errors, ONE forward KF pass, the creator's rejections (doc/seeds.txt R7).
  constexpr int kHostCreator = 3;

  // Seed classes (from the hit content; written to `cls` when given, for the validation dump).
  enum LstSeedClass : int8_t { kClsPixelOnly = 0, kClsOTOnly = 1, kClsMixed = 2, kClsTooFew = 3 };

  // Tunables of the device seed fit (defaults = the configuration validated in round 6).
  struct LstSeedFitConfig {
    float posVar = 1.0f;          // initial variance of x, y, z (cm^2) at the first hit
    float relInvPtErr = 1.0f;     // initial sigma(1/pT) relative to the 3-hit estimate
    float invPtErrFloor = 0.01f;  // plus this absolute floor (1/GeV)
    float angVar = 0.01f;         // initial variance of phi and theta (rad^2)
    float errScale = 1.0f;        // final errors scaled by this factor (1 = the KF errors as they are)
    int passes = 3;               // 3 = forward, backward, forward (errors x100 in between; validated); 1 = one pass
    // Origin prior as the host seed creator (SeedFromConsecutiveHitsCreator with a default GlobalTrackingRegion:
    // KF started at the beam line, transverse sigma 0.2 cm, z sigma 22.7 cm, unconstrained 1/pT and angles):
    // 0 = none, 1 = OT-only seeds (T5/T4), 2 = every fitted seed; 3 = kHostCreator (option b': the host creator's
    // structure, the 3-hit settings above are not used; passes selects the field model: 3 = the final fit's
    // refitKernelFlags (#186 mid-point B + radial kick), 1 = B at the start of each step without the kick, as the
    // host's AnalyticalPropagator).
    int originPrior = 0;
    float originR2 = 0.04f;    // (0.2 cm)^2
    float originZ2 = 515.29f;  // (22.7 cm)^2
    // kHostCreator: GlobalTrackingRegion() ptMin, SeedCreatorPSet MinOneOverPtError, PropagatorWithMaterial MaxDPhi
    float hlPtMin = 1.0f;
    float hlMinOneOverPtErr = 1.0f;
    float hlMaxDPhi = 1.6f;
    // kHostCreator retry after a float failure: capped angle (rad^2) and yT (cm^2) prior variances
    float hlSafeAngVar = 0.01f;
    float hlSafeYTVar = 25.f;
    // kHostCreator: a step counts as 'against the momentum' only below -hlBackwardTol (cm) along the direction (round
    // 8: the strict test rejected host-accepted pT3/pT5 seeds whose consecutive hits sit on the same or nearly the
    // same plane, where the host propagator does not move: AnalyticalPropagator 'already on surface'); 0 = round 7
    float hlBackwardTol = 0.01f;
    // kHostCreator + dropFailed: a failed seed with leading pixel hits and OT hits (pT3/pT5) is refitted on the pixel
    // hits only and truncated to them instead of dropped (LSTOutputConverter.cc 'seeds.empty() ? seed : seeds[0]':
    // the host keeps the pixel seed when the creator fails on the pT3/pT5 hits); false = drop (round 7)
    bool hlPixelFallback = true;
    bool dropFailed = false;  // failed fits: false = keep the input state; true = NaN error (removed by the
                              // stock seed_post_cleaning), for host seeds that carry only a placeholder state
  };

}  // namespace mkfitdev::lstseeds

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstseeds {

  using ::mkfitdev::lstseeds::LstSeedFitConfig;
  using ::mkfitdev::lstseeds::LstSeedFitCounters;

  // Replaces params / errors / charge of every seed row s < n with at least one outer-tracker hit by the device fit
  // of its hits (positions + errors from the event HitSoA: pixel rows first, strip rows from nPixel; module planes
  // from the ES). Rows without OT hits, and failed fits, keep their input state. `cls` (optional, device, [n]):
  // the seed class per row; `st` (optional, device, [n]): LstSeedFitStatus per row. Asynchronous in `queue`.
  void fitLstSeeds(Queue& queue,
                   ::mkfitdev::ESView const& es,
                   ::mkfitdev::HitSoAConstView hits,
                   uint32_t nPixel,
                   ::mkfitdev::SeedSoAView seeds,
                   int32_t n,
                   LstSeedFitConfig const& cfg,
                   LstSeedFitCounters* counters,
                   int8_t* cls = nullptr,
                   int8_t* st = nullptr);

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstseeds

#endif
