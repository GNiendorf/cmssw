#ifndef RecoTracker_MkFitAlpaka_src_alpaka_seeds_LstSeedKernels_h
#define RecoTracker_MkFitAlpaka_src_alpaka_seeds_LstSeedKernels_h

// O6-1 option (b): device fit of the LST seed state (see interface/seeds/alpaka/LstSeedFit.h, doc/seeds.txt R6).
// One thread per seed row (N = 1 Matriplex slot on every backend):
//   1. class from the hit content: no OT hit -> pixel-only (pLS) -> kept;
//   2. initial helix from three of the seed's hits (first, middle, last): circle in xy -> 1/pT, charge, phi at the
//      first hit; theta from the arc length and dz between the first and the last hit (no origin, no beam spot:
//      LST seeds may be displaced);
//   3. mkFit Kalman fit over the seed's hits in their stored order (the host seed order: inner -> outer), the same
//      primitive as the device final fit (kalmanPropagateAndUpdateAndChi2Plane: helix propagation to the module
//      plane with the parametrised field and material, plane-local update), material skipped on the first hit;
//      passes = 3: forward, backward, forward again with the errors scaled by 100 between passes (mkFit final-fit
//      style), so the result forgets the 3-hit estimate;
//   4. the state at the last hit (the seed's last hit, as the host seed) replaces params / errors / charge.
// Round 7 (lane seedfit, D7-b option b'): originPrior = kHostCreator (3) emulates the STRUCTURE of the host creator
// SeedFromConsecutiveHitsCreator::makeSeed with the LSTOutputConverter's default GlobalTrackingRegion (fitHostLike):
//   initialKinematic: FastHelix(hit 1, hit 0, region origin (0, 0, 0)), nominal field 3.8 T, 0.3 GeV/(T m), state at
//     the origin; initialError: diagonal curvilinear (q/p, lambda, phi, xT, yT) prior of the region (ptMin 1,
//     MinOneOverPtError 1, r 0.2 cm, half-length 22.7 cm) rotated to mkFit's CCS; ONE forward KF pass over every
//     seed hit: propagation to the module plane (mkFit helix, #186 refit field flags and material, applied at every
//     destination incl. the first hit, as PropagatorWithMaterial), checkHit (true: no seed comparitor), update; the
//     state at the last hit. Rejections as the host's: a step with |dphi| > MaxDPhi (1.6 rad, PropagatorWithMaterial),
//     a step against the momentum (alongMomentum propagator), a non-finite / non-positive state.

#include <cstdint>
#include <limits>

#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/MkFitAlpaka/interface/es/ESView.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/HitSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/math/Config.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/SeedSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/LstSeedFit.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/fit/FitKernels.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/prop/KalmanUtilsMPlex.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstseeds {

  using ::mkfitdev::ESView;
  using ::mkfitdev::HitSoAConstView;
  using ::mkfitdev::SeedSoAView;
  using namespace ::mkfitdev::lstseeds;

  struct LstSeedHitRow {
    ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE static uint32_t row(ESView const& es, uint32_t nPixel, int layer, int index) {
      return (es.layers[layer].is_pixel() ? 0u : nPixel) + static_cast<uint32_t>(index);
    }
  };

  // Outcome of the host-structure fit (fitHostLike).
  enum LstHostLikeStatus : int { kHLOk = 0, kHLRejDPhi = 1, kHLRejBackward = 2, kHLRejInvalid = 3 };

  // D7-b option (b'): SeedFromConsecutiveHitsCreator::makeSeed (initialKinematic + initialError + buildSeed) with
  // mkFit's propagation / material model. On kHLOk par/err/chg hold the updated state at the last seed hit.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE int fitHostLike(TAcc const& acc,
                                                 ESView const& es,
                                                 HitSoAConstView hits,
                                                 uint32_t nPixel,
                                                 const ::mkfitdev::HitOnTrack* hot,
                                                 const int nh,
                                                 LstSeedFitConfig const& cfg,
                                                 const bool safePrior,
                                                 MPlexLV<1>& parA,
                                                 MPlexLS<1>& errA,
                                                 MPlexQI<1>& chg) {
    if (hot[0].index < 0 || hot[1].index < 0)
      return kHLRejInvalid;
    // ---- initialKinematic: FastHelix(outer = hit 1, middle = hit 0, vertex = region origin), double as the host
    const uint32_t r0 = LstSeedHitRow::row(es, nPixel, hot[0].layer, hot[0].index);
    const uint32_t r1 = LstSeedHitRow::row(es, nPixel, hot[1].layer, hot[1].index);
    const double mx = hits[r0].x(), my = hits[r0].y(), mz = hits[r0].z();  // middle
    const double ox = hits[r1].x(), oy = hits[r1].y(), oz = hits[r1].z();  // outer
    const double vx = 0., vy = 0.;                                         // GlobalTrackingRegion() origin (0, 0, 0)
    constexpr double kTesla0 = 3.8;                                        // 0.1 * MagneticField::nominalValue()
    constexpr double kCm2GeV = 0.01 * 0.3 * kTesla0;
    constexpr double kMaxPt = 10000.;
    constexpr double kMaxRho = kMaxPt / kCm2GeV;
    // FastCircle through (outer, middle, vertex): exact circle through three points (FastCircle's Riemann-sphere
    // construction is exact for three points; the direct form differs at rounding level)
    const double ax = mx - vx, ay = my - vy, bx = ox - vx, by = oy - vy;
    const double cr = ax * by - ay * bx;
    double x0 = 0., y0 = 0., rho = 0.;
    bool circle =
        alpaka::math::abs(acc, cr) > 1e-12 * (ax * ax + ay * ay) * (bx * bx + by * by) / (1. + ax * ax + ay * ay);
    if (circle) {
      const double a2 = ax * ax + ay * ay, b2 = bx * bx + by * by;
      x0 = vx + (by * a2 - ay * b2) / (2. * cr);
      y0 = vy + (ax * b2 - bx * a2) / (2. * cr);
      rho = alpaka::math::sqrt(acc, (x0 - vx) * (x0 - vx) + (y0 - vy) * (y0 - vy));
    }
    double px, py, pz, zv;
    int q = 1;
    bool helix = circle && rho < kMaxRho;
    double dcphi = 0.;
    if (helix) {
      dcphi = ((ox - x0) * (mx - x0) + (oy - y0) * (my - y0)) / (rho * rho);
      helix = alpaka::math::abs(acc, dcphi) < 1.;
    }
    if (helix) {  // FastHelix::helixStateAtVertex
      const double pt = kCm2GeV * rho;
      px = -kCm2GeV * (vy - y0);
      py = kCm2GeV * (vx - x0);
      if (px * (mx - vx) + py * (my - vy) < 0.) {
        px = -px;
        py = -py;
      }
      const double dzdrphi = (oz - mz) / (rho * alpaka::math::acos(acc, dcphi));
      pz = pt * dzdrphi;
      if (x0 * py - y0 * px < 0)
        q = -q;
      zv = mz;
      double ds = ((vx - x0) * (mx - x0) + (vy - y0) * (my - y0)) / (rho * rho);
      if (alpaka::math::abs(acc, ds) < 1.) {
        ds = rho * alpaka::math::acos(acc, ds);
        zv -= ds * dzdrphi;
      } else {
        const double dmv2 = (mx - vx) * (mx - vx) + (my - vy) * (my - vy);
        const double dom2 = (ox - mx) * (ox - mx) + (oy - my) * (oy - my);
        zv -= alpaka::math::sqrt(acc, dmv2 / dom2) * (oz - mz);
      }
    } else {  // FastHelix::straightLineStateAtVertex (pT = 10 TeV along the vertex -> middle chord, charge +1)
      const double cl = alpaka::math::sqrt(acc, ax * ax + ay * ay);
      px = kMaxPt * ax / cl;
      py = kMaxPt * ay / cl;
      const double rm = alpaka::math::sqrt(acc, mx * mx + my * my), ro = alpaka::math::sqrt(acc, ox * ox + oy * oy);
      const double dzdr = (oz - mz) / (ro - rm);
      pz = kMaxPt * dzdr;
      zv = mz - rm * dzdr;
    }
    const double pt2 = px * px + py * py;
    const double ptv = alpaka::math::sqrt(acc, pt2);
    const double p = alpaka::math::sqrt(acc, pt2 + pz * pz);
    const float sinL = pz / p, cosL = ptv / p, sinF = py / ptv, cosF = px / ptv;
    const float ipt = 1. / ptv;
    parA.At(0, 0, 0) = vx;
    parA.At(0, 1, 0) = vy;
    parA.At(0, 2, 0) = zv;
    parA.At(0, 3, 0) = ipt;
    parA.At(0, 4, 0) = alpaka::math::atan2(acc, py, px);
    parA.At(0, 5, 0) = alpaka::math::atan2(acc, ptv, pz);
    chg.At(0, 0, 0) = q;

    // ---- initialError: curvilinear diag(q/p, lambda, phi, xT, yT) -> CCS (x, y, z, 1/pT, phi, theta)
    //      J: x = -sinF xT - sinL cosF yT, y = cosF xT - sinL sinF yT, z = cosL yT,
    //         1/pT = q (q/p) / cosL (d/dlambda = 1/pT tanL), phi = phi, theta = pi/2 - lambda
    const float sin2th = cosL * cosL;
    float c00 =
        alpaka::math::max(acc, sin2th / (cfg.hlPtMin * cfg.hlPtMin), cfg.hlMinOneOverPtErr * cfg.hlMinOneOverPtErr);
    float cL = 1.f, cF = 1.f;       // "no good reason. no bad reason...."
    const float cT = cfg.originR2;  // (OriginTransverseErrorMultiplier * originRBound)^2
    float cY = cfg.originZ2 * sin2th + cT * (1.f - sin2th);
    if (safePrior) {
      // retry after a float failure (the host does this in double): the uninformative parts of the prior capped so
      // that one update never needs > ~1e5 of dynamic range: q/p sigma (1/pT + 0.01) sin(theta), angles 0.1 rad,
      // yT 5 cm; the informative transverse 0.2 cm is kept
      const float sq = (ipt + 0.01f) * cosL;
      c00 = alpaka::math::min(acc, c00, sq * sq);
      cL = cfg.hlSafeAngVar;
      cF = cfg.hlSafeAngVar;
      cY = alpaka::math::min(acc, cY, cfg.hlSafeYTVar);
    }
    for (int i = 0; i < 6; ++i)
      for (int j = 0; j <= i; ++j)
        errA.At(0, i, j) = 0.f;
    errA.At(0, 0, 0) = sinF * sinF * cT + sinL * sinL * cosF * cosF * cY;
    errA.At(0, 1, 0) = -sinF * cosF * cT + sinL * sinL * sinF * cosF * cY;
    errA.At(0, 1, 1) = cosF * cosF * cT + sinL * sinL * sinF * sinF * cY;
    errA.At(0, 2, 0) = -sinL * cosL * cosF * cY;
    errA.At(0, 2, 1) = -sinL * cosL * sinF * cY;
    errA.At(0, 2, 2) = cosL * cosL * cY;
    const float dIptdL = ipt * sinL / cosL;
    errA.At(0, 3, 3) = c00 / (cosL * cosL) + dIptdL * dIptdL * cL;
    errA.At(0, 4, 4) = cF;
    errA.At(0, 5, 3) = -dIptdL * cL;
    errA.At(0, 5, 5) = cL;

    // ---- buildSeed: one forward pass, propagate (with material at the destination) + checkHit + update per hit
    const ::mkfitdev::RefitConfig rc = es.config->refit;
    PropagationFlags pf = ::ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::fit::refitKernelFlags(es);
    if (cfg.passes == 1) {
      // field model as the host's AnalyticalPropagator: B at the START of each step, no radial-field kick
      // (the #186 field constants stay); passes = 3 (default) = the final fit's refitKernelFlags (mid-point B + kick)
      pf.b_field_at_mid = false;
      pf.radial_field_corr = false;
    }
    if (rc.elossSignFromPass) {
      pf.eloss_by_pass = true;
      pf.eloss_outward = true;  // along the momentum: energy lost
    }
    PropagationFlags pfNoMat = pf;
    pfNoMat.apply_material = false;
    MPlexLS<1> errP, errQ;
    MPlexLV<1> parP, parQ;
    MPlexQI<1> failFlag, noMat;
    MPlexQF<1> outChi2, matRadl, matBbxi;
    MPlexHS<1> msErr;
    MPlexHV<1> msPar, norm, dir, pnt;
    noMat.At(0, 0, 0) = 0;
    const MPlexQF<1>* matRadlPtr = rc.materialPerModule ? &matRadl : nullptr;
    const MPlexQF<1>* matBbxiPtr = rc.materialPerModule ? &matBbxi : nullptr;
    for (int h = 0; h < nh; ++h) {
      if (hot[h].index < 0)
        continue;
      const int layer = hot[h].layer;
      const uint32_t r = LstSeedHitRow::row(es, nPixel, layer, hot[h].index);
      msPar.At(0, 0, 0) = hits[r].x();
      msPar.At(0, 1, 0) = hits[r].y();
      msPar.At(0, 2, 0) = hits[r].z();
      msErr.At(0, 0, 0) = hits[r].e00();
      msErr.At(0, 1, 0) = hits[r].e10();
      msErr.At(0, 1, 1) = hits[r].e11();
      msErr.At(0, 2, 0) = hits[r].e20();
      msErr.At(0, 2, 1) = hits[r].e21();
      msErr.At(0, 2, 2) = hits[r].e22();
      const int mod = es.moduleRow(layer, ::mkfitdev::hitpack::detIDinLayer(hits[r].packed()));
      const auto mi = es.modules[mod];
      norm.At(0, 0, 0) = mi.zdir_x();
      norm.At(0, 1, 0) = mi.zdir_y();
      norm.At(0, 2, 0) = mi.zdir_z();
      dir.At(0, 0, 0) = mi.xdir_x();
      dir.At(0, 1, 0) = mi.xdir_y();
      dir.At(0, 2, 0) = mi.xdir_z();
      pnt.At(0, 0, 0) = mi.pos_x();
      pnt.At(0, 1, 0) = mi.pos_y();
      pnt.At(0, 2, 0) = mi.pos_z();
      matRadl.At(0, 0, 0) = mi.radl();
      matBbxi.At(0, 0, 0) = mi.bbxi();
      failFlag.At(0, 0, 0) = 0;
      const MPlexLV<1>* src = &parA;
      const MPlexLS<1>* srcE = &errA;
      if (h == 0) {
        // the long origin -> first-hit step: a geometry-only step to the plane first (mkFit's plane propagation does
        // nSStepsInProp2Plane path refinements), then the material step from there (Jacobians chain)
        propagateHelixToPlaneMPlex<1>(errA, parA, chg, pnt, norm, errQ, parQ, failFlag, 1, pfNoMat, &noMat);
        if (failFlag.At(0, 0, 0))
          return kHLRejInvalid;
        src = &parQ;
        srcE = &errQ;
      }
      propagateHelixToPlaneMPlex<1>(
          *srcE, *src, chg, pnt, norm, errP, parP, failFlag, 1, pf, &noMat, matRadlPtr, matBbxiPtr, nullptr);
      if (failFlag.At(0, 0, 0))
        return kHLRejInvalid;
      // AnalyticalPropagator: |dphi| of the step > MaxDPhi fails; PropagationDirection alongMomentum
      const float dphi = ::mkfitdev::squashPhiGeneral(parP.At(0, 4, 0) - parA.At(0, 4, 0));
      if (alpaka::math::abs(acc, dphi) > cfg.hlMaxDPhi)
        return kHLRejDPhi;
      const float st = alpaka::math::sin(acc, parA.At(0, 5, 0));
      const float dx = parP.At(0, 0, 0) - parA.At(0, 0, 0), dy = parP.At(0, 1, 0) - parA.At(0, 1, 0),
                  dz = parP.At(0, 2, 0) - parA.At(0, 2, 0);
      const float fwd = dx * alpaka::math::cos(acc, parA.At(0, 4, 0)) * st +
                        dy * alpaka::math::sin(acc, parA.At(0, 4, 0)) * st +
                        dz * alpaka::math::cos(acc, parA.At(0, 5, 0));
      if (fwd < -cfg.hlBackwardTol)
        return kHLRejBackward;
      // checkHit: SeedFromConsecutiveHitsCreator::checkHit = filter->compatible or true; the LST converter passes no
      // seed comparitor -> true. KFUpdator: the plane-local update of the propagated state.
      kalmanPropagateAndUpdateAndChi2Plane<1>(
          errP, parP, chg, msErr, msPar, norm, dir, pnt, errA, parA, failFlag, outChi2, 1, pf, false, &noMat);
      // KFUpdator returns an invalid state only when R = V + H C H^T cannot be inverted: per step only finite
      // parameters are required (as the device final fit); the covariance is checked once, at the end
      bool ok = true;
      for (int k = 0; k < 6; ++k)
        ok = ok && ::mkfitdev::isFinite(parA.At(0, k, 0));
      if (!ok)
        return kHLRejInvalid;
    }
    bool ok = true;
    for (int k = 0; k < 6; ++k)
      ok = ok && ::mkfitdev::isFinite(errA.At(0, k, k)) && errA.At(0, k, k) > 0.f;
    return ok ? kHLOk : kHLRejInvalid;
  }

  class KernelLstSeedFit {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ESView es,
                                  HitSoAConstView hits,
                                  uint32_t nPixel,
                                  SeedSoAView seeds,
                                  int32_t n,
                                  LstSeedFitConfig cfg,
                                  LstSeedFitCounters* cnt,
                                  int8_t* cls,
                                  int8_t* stOut) const {
      const PropagationFlags pf(PF_use_param_b_field | PF_apply_material, es.material);
      for (int32_t s : cms::alpakatools::uniform_elements(acc, n)) {
        const int nh = seeds[s].nHits();
        const auto& hot = seeds[s].hits().hot;
        int nOT = 0;
        for (int h = 0; h < nh; ++h)
          if (hot[h].index >= 0 && !es.layers[hot[h].layer].is_pixel())
            ++nOT;
        if (nOT == 0) {
          if (cls)
            cls[s] = kClsPixelOnly;
          alpaka::atomicAdd(acc, &cnt->nPixelOnly, 1, alpaka::hierarchy::Blocks{});
          continue;
        }
        if (cfg.originPrior == kHostCreator) {
          if (nh < 2) {
            if (cls)
              cls[s] = kClsTooFew;
            alpaka::atomicAdd(acc, &cnt->nTooFewHits, 1, alpaka::hierarchy::Blocks{});
            continue;
          }
          if (cls)
            cls[s] = (nOT == nh) ? kClsOTOnly : kClsMixed;
          MPlexLV<1> parH;
          MPlexLS<1> errH;
          MPlexQI<1> chgH;
          int st = fitHostLike(acc, es, hits, nPixel, hot, nh, cfg, false, parH, errH, chgH);
          if (st == kHLRejInvalid) {  // float failure of the exact prior: retry with the capped prior
            alpaka::atomicAdd(acc, &cnt->nSafeRetry, 1, alpaka::hierarchy::Blocks{});
            st = fitHostLike(acc, es, hits, nPixel, hot, nh, cfg, true, parH, errH, chgH);
          }
          int nFit = nh;  // the seed keeps its first nFit hits
          int8_t status = kStOk;
          if (st != kHLOk) {
            alpaka::atomicAdd(acc, &cnt->nFailed, 1, alpaka::hierarchy::Blocks{});
            if (st == kHLRejDPhi)
              alpaka::atomicAdd(acc, &cnt->nRejDPhi, 1, alpaka::hierarchy::Blocks{});
            else if (st == kHLRejBackward)
              alpaka::atomicAdd(acc, &cnt->nRejBackward, 1, alpaka::hierarchy::Blocks{});
            status = st == kHLRejDPhi ? kStRejDPhi : (st == kHLRejBackward ? kStRejBackward : kStRejInvalid);
            if (!cfg.dropFailed) {  // the input (host creator) state is kept
              if (stOut)
                stOut[s] = status;
              continue;
            }
            // light seeds (placeholder input state): emulate the host. T5/T4 (OT only): the host skips the TC -> drop.
            // pT3/pT5 (leading pixel hits + OT hits): the host keeps the pixel seed -> refit the leading pixel hits
            // with the same creator structure and truncate the seed to them.
            int nPixLead = 0;
            while (nPixLead < nh && hot[nPixLead].index >= 0 && es.layers[hot[nPixLead].layer].is_pixel())
              ++nPixLead;
            bool fallback = false;
            if (cfg.hlPixelFallback && nOT < nh && nPixLead >= 2) {
              int st2 = fitHostLike(acc, es, hits, nPixel, hot, nPixLead, cfg, false, parH, errH, chgH);
              if (st2 == kHLRejInvalid)
                st2 = fitHostLike(acc, es, hits, nPixel, hot, nPixLead, cfg, true, parH, errH, chgH);
              fallback = (st2 == kHLOk);
              alpaka::atomicAdd(
                  acc, fallback ? &cnt->nFallback : &cnt->nFallbackFailed, 1, alpaka::hierarchy::Blocks{});
              status = fallback ? kStFallbackOk : kStFallbackFailed;
              nFit = nPixLead;
            } else if (nOT == nh) {
              status = kStDroppedOT;
            }
            if (!fallback) {
              seeds[s].errors().v[0] = std::numeric_limits<float>::quiet_NaN();  // stock seed_post_cleaning removes it
              if (stOut)
                stOut[s] = status;
              continue;
            }
          }
          if (stOut)
            stOut[s] = status;
          if (nFit < nh) {  // truncated (pixel fallback): hit count and last-hit position of the shorter seed
            seeds[s].nHits() = static_cast<int16_t>(nFit);
            const uint32_t rl = LstSeedHitRow::row(es, nPixel, hot[nFit - 1].layer, hot[nFit - 1].index);
            seeds[s].lastX() = hits[rl].x();
            seeds[s].lastY() = hits[rl].y();
            seeds[s].lastZ() = hits[rl].z();
          }
          // DEVIATION D2: seed state from the device fit of the seed hits (stock: CMSSW seed creator, host)
          if (cfg.errScale != 1.f)
            errH.scale(cfg.errScale);
          for (int k = 0; k < 6; ++k)
            seeds[s].params().v[k] = parH.At(0, k, 0);
          errH.copyOut(0, seeds[s].errors().v);
          if (chgH.At(0, 0, 0) != seeds[s].charge())
            alpaka::atomicAdd(acc, &cnt->nChargeFlip, 1, alpaka::hierarchy::Blocks{});
          seeds[s].charge() = static_cast<int16_t>(chgH.At(0, 0, 0));
          alpaka::atomicAdd(acc, &cnt->nFitted, 1, alpaka::hierarchy::Blocks{});
          continue;
        }
        if (nh < 3) {
          if (cls)
            cls[s] = kClsTooFew;
          alpaka::atomicAdd(acc, &cnt->nTooFewHits, 1, alpaka::hierarchy::Blocks{});
          continue;
        }
        if (cls)
          cls[s] = (nOT == nh) ? kClsOTOnly : kClsMixed;

        // 2. initial helix from the first, middle and last hit
        const uint32_t r0 = LstSeedHitRow::row(es, nPixel, hot[0].layer, hot[0].index);
        const uint32_t r1 = LstSeedHitRow::row(es, nPixel, hot[nh / 2].layer, hot[nh / 2].index);
        const uint32_t r2 = LstSeedHitRow::row(es, nPixel, hot[nh - 1].layer, hot[nh - 1].index);
        const float x0 = hits[r0].x(), y0 = hits[r0].y(), z0 = hits[r0].z();
        const float ax = hits[r1].x() - x0, ay = hits[r1].y() - y0;
        const float bx = hits[r2].x() - x0, by = hits[r2].y() - y0;
        const float cross = ax * by - ay * bx;
        const float a2 = ax * ax + ay * ay, b2 = bx * bx + by * by;
        float invPt, phi;
        int charge;
        const float kPtPerCm = ::mkfitdev::Const::sol_over_100 * ::mkfitdev::Config::Bfield;  // pT = k R
        if (alpaka::math::abs(acc, cross) < 1e-6f * a2 * b2 / (1.f + a2 + b2) || cross == 0.f) {
          // straight in xy: 1/pT ~ 0, charge +1 (the fit may flip it)
          invPt = 1e-3f;
          charge = 1;
          phi = alpaka::math::atan2(acc, by, bx);
        } else {
          const float d = 2.f * cross;
          const float ux = (by * a2 - ay * b2) / d, uy = (ax * b2 - bx * a2) / d;  // centre relative to hit 0
          const float R = alpaka::math::sqrt(acc, ux * ux + uy * uy);
          invPt = 1.f / (kPtPerCm * R);
          charge = cross > 0.f ? -1 : 1;  // counter-clockwise in xy = negative for Bz > 0
          // tangent at hit 0, perpendicular to the radius (hit0 - centre) = (-ux, -uy), oriented toward hit 1
          float tx = uy, ty = -ux;
          if (tx * ax + ty * ay < 0.f) {
            tx = -tx;
            ty = -ty;
          }
          phi = alpaka::math::atan2(acc, ty, tx);
        }
        // theta from the transverse arc length between hit 0 and the last hit
        const float chord = alpaka::math::sqrt(acc, b2);
        float sArc = chord;
        if (invPt > 1e-3f) {
          const float R = 1.f / (kPtPerCm * invPt);
          const float q = chord / (2.f * R);
          sArc = 2.f * R * alpaka::math::asin(acc, q < 1.f ? q : 1.f);
        }
        const float theta = alpaka::math::atan2(acc, sArc, hits[r2].z() - z0);

        // origin prior (host seed creator style): circle through the beam line point (0, 0), hit 0 and the last hit;
        // the KF starts AT the beam line with the creator's prior and material is applied on every hit
        const bool usePrior = (cfg.originPrior == 1 && nOT == nh) || cfg.originPrior == 2;
        float priorPar[6] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        int priorChg = 1;
        if (usePrior) {
          const float px = x0, py = y0, qx = hits[r2].x(), qy = hits[r2].y();
          const float p2 = px * px + py * py, q2 = qx * qx + qy * qy;
          const float cr = px * qy - py * qx;
          float ipt = 1e-3f, ph = alpaka::math::atan2(acc, py, px);
          float s0 = alpaka::math::sqrt(acc, p2), sl = alpaka::math::sqrt(acc, q2);
          if (alpaka::math::abs(acc, cr) > 1e-6f * p2 * q2 / (1.f + p2 + q2) && cr != 0.f) {
            const float d = 2.f * cr;
            const float ux = (qy * p2 - py * q2) / d, uy = (px * q2 - qx * p2) / d;  // centre relative to the origin
            const float R = alpaka::math::sqrt(acc, ux * ux + uy * uy);
            ipt = 1.f / (kPtPerCm * R);
            priorChg = cr > 0.f ? -1 : 1;
            float tx = uy, ty = -ux;
            if (tx * px + ty * py < 0.f) {
              tx = -tx;
              ty = -ty;
            }
            ph = alpaka::math::atan2(acc, ty, tx);
            const float a0 = s0 / (2.f * R), al = sl / (2.f * R);
            s0 = 2.f * R * alpaka::math::asin(acc, a0 < 1.f ? a0 : 1.f);
            sl = 2.f * R * alpaka::math::asin(acc, al < 1.f ? al : 1.f);
          }
          const float ds = (sl - s0) > 1e-3f ? (sl - s0) : 1e-3f;
          const float cotT = (hits[r2].z() - z0) / ds;
          priorPar[2] = z0 - s0 * cotT;
          priorPar[3] = ipt;
          priorPar[4] = ph;
          priorPar[5] = alpaka::math::atan2(acc, 1.f, cotT);
        }

        MPlexLS<1> errA, errB;
        MPlexLV<1> parA, parB;
        MPlexQI<1> chg, failFlag, noMat;
        MPlexQF<1> outChi2;
        MPlexHS<1> msErr;
        MPlexHV<1> msPar, norm, dir, pnt;
        parA.At(0, 0, 0) = x0;
        parA.At(0, 1, 0) = y0;
        parA.At(0, 2, 0) = z0;
        parA.At(0, 3, 0) = invPt;
        parA.At(0, 4, 0) = phi;
        parA.At(0, 5, 0) = theta;
        for (int i = 0; i < 6; ++i)
          for (int j = 0; j <= i; ++j)
            errA.At(0, i, j) = 0.f;
        errA.At(0, 0, 0) = cfg.posVar;
        errA.At(0, 1, 1) = cfg.posVar;
        errA.At(0, 2, 2) = cfg.posVar;
        const float sIpt = cfg.relInvPtErr * invPt + cfg.invPtErrFloor;
        errA.At(0, 3, 3) = sIpt * sIpt;
        errA.At(0, 4, 4) = cfg.angVar;
        errA.At(0, 5, 5) = cfg.angVar;
        chg.At(0, 0, 0) = charge;
        failFlag.At(0, 0, 0) = 0;
        if (usePrior) {
          for (int k = 0; k < 6; ++k)
            parA.At(0, k, 0) = priorPar[k];
          errA.At(0, 0, 0) = cfg.originR2;
          errA.At(0, 1, 1) = cfg.originR2;
          errA.At(0, 2, 2) = cfg.originZ2;
          errA.At(0, 3, 3) = 1.f;  // host: C(q/p) >= MinOneOverPtError^2 = 1, angles 1 rad^2
          errA.At(0, 4, 4) = 1.f;
          errA.At(0, 5, 5) = 1.f;
          chg.At(0, 0, 0) = priorChg;
        }

        // 3. Kalman passes over the seed hits (pass 0 forward, pass 1 backward, pass 2 forward, ...)
        const int nPass = cfg.passes >= 3 ? 3 : 1;
        bool failed = false;
        float chi2 = 0.f;
        for (int p = 0; p < nPass && !failed; ++p) {
          const bool bkw = (p & 1);
          if (p > 0)
            errA.scale(100.0f);
          chi2 = 0.f;
          for (int h = 0; h < nh; ++h) {
            const int m = bkw ? nh - 1 - h : h;
            if (hot[m].index < 0)
              continue;
            const int layer = hot[m].layer;
            const uint32_t r = LstSeedHitRow::row(es, nPixel, layer, hot[m].index);
            msPar.At(0, 0, 0) = hits[r].x();
            msPar.At(0, 1, 0) = hits[r].y();
            msPar.At(0, 2, 0) = hits[r].z();
            msErr.At(0, 0, 0) = hits[r].e00();
            msErr.At(0, 1, 0) = hits[r].e10();
            msErr.At(0, 1, 1) = hits[r].e11();
            msErr.At(0, 2, 0) = hits[r].e20();
            msErr.At(0, 2, 1) = hits[r].e21();
            msErr.At(0, 2, 2) = hits[r].e22();
            const int mod = es.moduleRow(layer, ::mkfitdev::hitpack::detIDinLayer(hits[r].packed()));
            const auto mi = es.modules[mod];
            norm.At(0, 0, 0) = mi.zdir_x();
            norm.At(0, 1, 0) = mi.zdir_y();
            norm.At(0, 2, 0) = mi.zdir_z();
            dir.At(0, 0, 0) = mi.xdir_x();
            dir.At(0, 1, 0) = mi.xdir_y();
            dir.At(0, 2, 0) = mi.xdir_z();
            pnt.At(0, 0, 0) = mi.pos_x();
            pnt.At(0, 1, 0) = mi.pos_y();
            pnt.At(0, 2, 0) = mi.pos_z();
            noMat.At(0, 0, 0) = (h == 0 && !(usePrior && p == 0)) ? 1 : 0;
            kalmanPropagateAndUpdateAndChi2Plane<1>(
                errA, parA, chg, msErr, msPar, norm, dir, pnt, errB, parB, failFlag, outChi2, 1, pf, true, &noMat);
            if (failFlag.At(0, 0, 0)) {
              failed = true;
              break;
            }
            errA = errB;
            parA = parB;
            chi2 += outChi2.At(0, 0, 0);
          }
        }
        bool finite = (chi2 == chi2);
        for (int k = 0; k < 6; ++k)
          finite = finite && ::mkfitdev::isFinite(parA.At(0, k, 0)) && errA.At(0, k, k) > 0.f;
        if (failed || !finite) {
          alpaka::atomicAdd(acc, &cnt->nFailed, 1, alpaka::hierarchy::Blocks{});
          if (cfg.dropFailed)
            seeds[s].errors().v[0] = std::numeric_limits<float>::quiet_NaN();  // stock seed_post_cleaning removes it
          continue;
        }
        // DEVIATION D2: seed state from the device mkFit fit of the seed hits (stock: CMSSW seed creator, host)
        // 4. state at the last hit -> the seed row
        if (cfg.errScale != 1.f)
          errA.scale(cfg.errScale);
        for (int k = 0; k < 6; ++k)
          seeds[s].params().v[k] = parA.At(0, k, 0);
        errA.copyOut(0, seeds[s].errors().v);
        if (chg.At(0, 0, 0) != seeds[s].charge())
          alpaka::atomicAdd(acc, &cnt->nChargeFlip, 1, alpaka::hierarchy::Blocks{});
        seeds[s].charge() = static_cast<int16_t>(chg.At(0, 0, 0));
        alpaka::atomicAdd(acc, &cnt->nFitted, 1, alpaka::hierarchy::Blocks{});
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstseeds

#endif
