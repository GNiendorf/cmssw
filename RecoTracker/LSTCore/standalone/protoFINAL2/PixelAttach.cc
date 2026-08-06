// K8 pixel attach (M7, plan section 3 K8 / 5a): implementation of the frozen
// PixelAttach.h contract plus the shared prefilter pair enumeration (PixelAttachPairs.h).
//
// M16 (general attach): the target side is now EITHER an accepted chain (ttype 0) or a
// BARE T3 (ttype 1, k8BuildBareT3Mask). Everything below is written once against a
// TargetPre struct that both kinds fill; the prefilter, the propagation and the feature
// builder are literally the same code for both -- the "one helix-propagation candidate
// finder, target-agnostic" of the plan-11 design decision. T3-target field mapping (the
// only deltas; PixelAttach.h documents the rationale):
//   rtInner/zInner  : md_anchor rt/z of t3_md0
//   chordPhi        : atan2 of (md1 anchor - md0 anchor)  [the innermost chord, exactly
//                     as a chain uses its two innermost deduped MDs]
//   tanLambda       : dz02 / ds02 over the T3 anchors (ds02 = xy chord md0 -> md2), the
//                     Features.cc node-feature-2 convention -- NOT an rz line fit, since
//                     a 3-point rz fit through the anchors is the same two-point slope
//                     up to the middle-point residual and the node convention is already
//                     validated upstream
//   fitKappa (f7)   : rotSign / max(t3_radius, 1e-9), rotSign = sign of the z-component
//                     of cross(md0->md1, md1->md2) (collinear -> +1). Same sign
//                     convention as the chain Kasa fit (both are "the rotation sense the
//                     hits sweep"), so f12 chargeAgree and f13 dKappa stay comparable.
//                     Non-finite t3_radius -> 1e12 stand-in (Features.cc cleanRadius).
//   innermostLayer  : md_layer[t3_md0]        nLayers (f10): 3
//   gateLogit (f11) : 0 (no chain gate exists for a bare T3 -- feature 18 flags it)
//   centerX/Y       : t3_centerX / t3_centerY (the T3's OWN circle-fit center, the exact
//                     analog of the chain's Kasa center); non-finite -> centerValid
//                     false -> f16 = 0, the same degenerate flag value chains use
//
// EXACT FEATURE DEFINITIONS (kAttachFeat = 19, frozen order; the header delegates the
// precise definitions to this site):
//   pLS side (constant per pLS, hoisted):
//     0 log10PtIn        : log10(max(pLS_pt, 1e-6))
//     1 ptErrRel         : pLS_ptErr / max(pLS_pt, 1e-6)
//     2 etaErr           : pLS_etaErr
//     3 charge           : pLS_charge as float (+/-1)
//     4 isQuad           : pLS_isQuad ? 1 : 0
//     5 log10CircleRadius: log10(max(pLS_circleRadius, 1e-6))  [cm]
//     6 plsDeltaPhi      : pLS_deltaPhi (the stored LST seed deltaPhi)
//   chain side (constant per chain, hoisted; cf = the frozen ChainFeatures row):
//     7 fitKappaSigned   : cf[7] (Kasa fit curvature, sign = chain rotation sign)
//     8 chainTanLambda   : slope b of the z-vs-s straight-line fit over the chain's
//                          anchor hits, s = cumulative xy chord length from the
//                          innermost hit -- the SAME fit whose chi2 is cf[6]
//                          (rzLineChi2PerHit); dz/ds IS tanLambda, origin-free.
//                          Degenerate abscissa (Sss <= 1e-12) -> 0 (flag).
//     9 innermostLayer   : cf[10]
//    10 nLayers          : cf[1]
//    11 chainGateLogit   : the chain-gate MLP logit for this chain (passed in; computed
//                          by the caller regardless of -G mode so the feature is always
//                          the gate scale, even when K9 scored the chain differently)
//   pair:
//    12 chargeAgree      : 1 if the pLS rotation sign equals the chain rotation sign.
//                          pLS rotation sign = -pLS_charge: VERIFIED empirically on
//                          PU200RelVal (sign of cross(hit0 - circleCenter, (px,py))_z
//                          == -charge on 5000/5000 pLS; CMS B_z > 0, positive charge
//                          curves clockwise). Chain rotation sign = sign(cf[7]),
//                          with fitKappa == 0 (degenerate/straight) counting +1.
//    13 dKappa           : (-charge / max(circleRadius, 1e-6)) - fitKappaSigned
//                          (pLS curvature signed by charge minus chain fit curvature,
//                          both on the rotation-sign convention above)
//    14 dTanLambda       : pLS tanLambda - chainTanLambda, with pLS tanLambda =
//                          pLS_pz / max(pLS_pt, 1e-6) (pz/pt, MORE DIRECT than the
//                          header's from-eta suggestion -- documented deviation)
//    15 dPhiAtInnermost  : propagate the pLS circle (circleCenterX/Y, circleRadius,
//                          rotation sense -charge) to the radius rt_inner of the
//                          chain's innermost anchor hit: of the two circle-circle
//                          intersection points take the one where the direction of
//                          motion points radially OUTWARD (t . p > 0), then
//                          dPhi = wrap(phi(direction of motion) - chainChordPhi),
//                          chainChordPhi = phi of (anchor[1] - anchor[0]) over the
//                          chain's innermost-first deduped MD list. No intersection
//                          (pLS circle too small / does not reach rt_inner) ->
//                          fallback dPhi = wrap(pLS_phi - chainChordPhi).
//    16 circleCenterDist : |pLS circle center - chain Kasa fit center| (xy, cm). The
//                          chain center is re-fit here with EXACTLY the ChainFeatures.cc
//                          Kasa algorithm (double precision, same degeneracy guard);
//                          degenerate chain fit (collinear) -> 0 (flag, matching the
//                          ChainFeatures degenerate convention).
//    17 zResidAtInnermost: pLS_hit0_z + pLSTanLambda * (rt_inner - rt_hit0) - z_inner
//                          (header formula; pLSTanLambda = pz/pt as in feature 14;
//                          rt_hit0 = hypot(pLS_hit0_x, pLS_hit0_y)).
//   target kind:
//    18 targetType      : 0.0 for an accepted-chain target, 1.0 for a bare-T3 target
//                         (M16; the categorical that tells the head slot 11 is a
//                         structural zero and slot 10 is pinned to 3).
//
// PREFILTER (efficiency-first, plan v1): charge compatibility NOT required (sign flips
// exist for near-straight/displaced tracks, the M2 lesson); |dTanLambda| < prefDTanL
// AND |dPhiAtInnermost| < prefDPhi. Chain targets: only nLayers >= 5 participate (v1
// scope: chain+pLS -> pT5-class). M16 bare-T3 targets: EVERY masked T3 participates and
// runs the IDENTICAL windows with no T3-specific loosening or tightening -- the general
// attach has ONE candidate finder, and any per-class tuning belongs to the head margins,
// not the prefilter.
//
// NaN/Inf guard: every emitted feature vector passes the Features.cc sanitize
// convention (NaN -> 0, +/-Inf -> +/-1e12); the epsilon guards above keep all
// arithmetic finite for finite inputs anyway.
//
// SELECTION (k8AttachPixels): each prefiltered pair is scored by attachLogit
// (AttachInference.h; 0-sentinel before training). Per chain the highest logit >=
// thetaAttach wins (tie -> lower pLS row). pLS contention (one chain per pLS) is
// resolved by higher logit, tie -> the earlier accepted-chain position keeps it; the
// losing chain gets NO attachment (no second-best fallback in v1 -- the production
// kernel would re-arbitrate).

#include "PixelAttach.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "AttachInference.h"
#include "PixelAttachCand.h"
#include "PixelAttachPairs.h"

const char* const kAttachFeatNames[kAttachFeat] = {"log10PtIn",
                                                   "ptErrRel",
                                                   "etaErr",
                                                   "charge",
                                                   "isQuad",
                                                   "log10CircleRadius",
                                                   "plsDeltaPhi",
                                                   "fitKappaSigned",
                                                   "chainTanLambda",
                                                   "innermostLayer",
                                                   "nLayers",
                                                   "chainGateLogit",
                                                   "chargeAgree",
                                                   "dKappa",
                                                   "dTanLambda",
                                                   "dPhiAtInnermost",
                                                   "circleCenterDist",
                                                   "zResidAtInnermost",
                                                   "targetType"};

namespace {

  constexpr float kEps = 1e-6f;
  constexpr float kPi = 3.14159265358979323846f;

  inline float wrapPhi(float d) {
    while (d > kPi)
      d -= 2.f * kPi;
    while (d < -kPi)
      d += 2.f * kPi;
    return d;
  }

  inline float sanitizeOne(float x) {
    if (std::isnan(x))
      return 0.f;
    if (std::isinf(x))
      return (x > 0.f) ? 1e12f : -1e12f;
    return x;
  }

  // ---- per-pLS hoisted quantities -------------------------------------------------------
  struct PlsPre {
    // feature block 0-6
    float log10Pt, ptErrRel, etaErr, charge, isQuad, log10R, plsDeltaPhi;
    // propagation / pair inputs
    float tanLambda;    // pz / max(pt, eps)
    float kappaSigned;  // -charge / max(circleRadius, eps)
    float rotSign;      // -charge (empirically verified rotation sense)
    float phi;          // pLS_phi (dPhi fallback)
    float cx, cy, r;    // circle center / radius
    float d;            // |center|
    float hit0z, rt0;   // innermost seed hit z and rt
  };

  PlsPre makePlsPre(const LSTEventData& ev, int p) {
    PlsPre o;
    const float pt = std::max(ev.pLS_pt[p], kEps);
    o.log10Pt = std::log10(pt);
    o.ptErrRel = ev.pLS_ptErr[p] / pt;
    o.etaErr = ev.pLS_etaErr[p];
    o.charge = static_cast<float>(ev.pLS_charge[p]);
    o.isQuad = ev.pLS_isQuad[p] ? 1.f : 0.f;
    const float r = std::max(ev.pLS_circleRadius[p], kEps);
    o.log10R = std::log10(r);
    o.plsDeltaPhi = ev.pLS_deltaPhi[p];
    o.tanLambda = ev.pLS_pz[p] / pt;
    o.rotSign = (o.charge > 0.f) ? -1.f : 1.f;
    o.kappaSigned = o.rotSign / r;
    o.phi = ev.pLS_phi[p];
    o.cx = ev.pLS_circleCenterX[p];
    o.cy = ev.pLS_circleCenterY[p];
    o.r = r;
    o.d = std::sqrt(o.cx * o.cx + o.cy * o.cy);
    o.hit0z = ev.pLS_hit0_z[p];
    const float hx = ev.pLS_hit0_x[p], hy = ev.pLS_hit0_y[p];
    o.rt0 = std::sqrt(hx * hx + hy * hy);
    return o;
  }

  // ---- per-target hoisted quantities (M16: chain OR bare T3) ----------------------------
  struct TargetPre {
    // geometry (probe path needs only these)
    float rtInner = 0.f, zInner = 0.f, chordPhi = 0.f;
    float tanLambda = 0.f;  // rz line fit slope / T3 dz02-ds02 (0 if degenerate)
    // feature block 7-11 (cf row + gate logit; unused by the probe)
    float fitKappa = 0.f, innermostLayer = 0.f, nLayersF = 0.f, gateLogit = 0.f;
    float rotSign = 1.f;  // sign(fitKappa), 0 -> +1
    // target circle-fit center (feature 16); centerValid false -> flag value 0
    float centerX = 0.f, centerY = 0.f;
    bool centerValid = false;
    // feature 18 (M16): 0 = accepted chain, 1 = bare T3
    float targetType = static_cast<float>(kAttachTargetChain);
  };

  // Geometry part: innermost anchor rt/z, innermost chord phi, rz-fit tanLambda, Kasa
  // center -- all over chains.mdItems (innermost-first), double accumulation exactly as
  // ChainFeatures.cc.
  TargetPre makeChainPreGeom(const LSTEventData& ev, const Chains& chains, int c) {
    TargetPre o;
    const int mb = chains.mdOffsets[c], me = chains.mdOffsets[c + 1];
    const int nMD = me - mb;
    if (nMD < 1)
      return o;  // unreachable by the K6 contract

    const int mdIn = chains.mdItems[mb];
    const double x0 = ev.md_anchor_x[mdIn], y0 = ev.md_anchor_y[mdIn];
    o.rtInner = static_cast<float>(std::sqrt(x0 * x0 + y0 * y0));
    o.zInner = ev.md_anchor_z[mdIn];
    if (nMD >= 2) {
      const int md1 = chains.mdItems[mb + 1];
      o.chordPhi = std::atan2(ev.md_anchor_y[md1] - y0, ev.md_anchor_x[md1] - x0);
    }

    // rz line fit (z vs cumulative xy chord length) -- slope only; mirrors the cf[6] fit.
    {
      double s = 0.0, sPrevX = x0, sPrevY = y0;
      double sbar = 0.0, zbar = 0.0;
      std::vector<double> sArc(nMD);
      sArc[0] = 0.0;
      for (int k = 1; k < nMD; ++k) {
        const int md = chains.mdItems[mb + k];
        const double dx = ev.md_anchor_x[md] - sPrevX, dy = ev.md_anchor_y[md] - sPrevY;
        s += std::sqrt(dx * dx + dy * dy);
        sArc[k] = s;
        sPrevX = ev.md_anchor_x[md];
        sPrevY = ev.md_anchor_y[md];
      }
      for (int k = 0; k < nMD; ++k) {
        sbar += sArc[k];
        zbar += ev.md_anchor_z[chains.mdItems[mb + k]];
      }
      sbar /= nMD;
      zbar /= nMD;
      double Sss = 0.0, Ssz = 0.0;
      for (int k = 0; k < nMD; ++k) {
        const double ds = sArc[k] - sbar;
        Sss += ds * ds;
        Ssz += ds * (ev.md_anchor_z[chains.mdItems[mb + k]] - zbar);
      }
      if (Sss > 1e-12)
        o.tanLambda = static_cast<float>(Ssz / Sss);
    }

    // Kasa circle fit center (feature 16) -- EXACTLY the ChainFeatures.cc algorithm.
    if (nMD >= 3) {
      double xbar = 0.0, ybar = 0.0;
      for (int k = mb; k < me; ++k) {
        xbar += ev.md_anchor_x[chains.mdItems[k]];
        ybar += ev.md_anchor_y[chains.mdItems[k]];
      }
      xbar /= nMD;
      ybar /= nMD;
      double Suu = 0.0, Svv = 0.0, Suv = 0.0, Suw = 0.0, Svw = 0.0;
      for (int k = mb; k < me; ++k) {
        const double u = ev.md_anchor_x[chains.mdItems[k]] - xbar;
        const double v = ev.md_anchor_y[chains.mdItems[k]] - ybar;
        const double w = u * u + v * v;
        Suu += u * u;
        Svv += v * v;
        Suv += u * v;
        Suw += u * w;
        Svw += v * w;
      }
      const double det = Suu * Svv - Suv * Suv;
      const double scale = Suu + Svv;
      if (det > 1e-12 * scale * scale) {
        const double uc = (Svv * (0.5 * Suw) - Suv * (0.5 * Svw)) / det;
        const double vc = (Suu * (0.5 * Svw) - Suv * (0.5 * Suw)) / det;
        o.centerX = static_cast<float>(xbar + uc);
        o.centerY = static_cast<float>(ybar + vc);
        o.centerValid = true;
      }
    }
    return o;
  }

  TargetPre makeChainPre(const LSTEventData& ev, const Chains& chains, int c, const ChainFeatures& cf, float gateLogit) {
    TargetPre o = makeChainPreGeom(ev, chains, c);
    const float* f = &cf.f[static_cast<std::size_t>(c) * kChainFeat];
    o.fitKappa = f[7];
    o.innermostLayer = f[10];
    o.nLayersF = f[1];
    o.gateLogit = gateLogit;
    o.rotSign = (o.fitKappa >= 0.f) ? 1.f : -1.f;
    return o;
  }

  // ---- M16: bare-T3 target ---------------------------------------------------------------
  // Geometry part (the probe path needs only these): innermost anchor rt/z from md0, the
  // md0 -> md1 chord phi, and tanLambda = dz02/ds02 over the anchors (Features.cc node
  // convention). Field-for-field the chain analog; see the mapping block at the top.
  TargetPre makeT3PreGeom(const LSTEventData& ev, int t) {
    TargetPre o;
    o.targetType = static_cast<float>(kAttachTargetT3);
    const int m0 = ev.t3_md0[t], m1 = ev.t3_md1[t], m2 = ev.t3_md2[t];
    const float x0 = ev.md_anchor_x[m0], y0 = ev.md_anchor_y[m0];
    o.rtInner = std::sqrt(x0 * x0 + y0 * y0);
    o.zInner = ev.md_anchor_z[m0];
    o.chordPhi = std::atan2(ev.md_anchor_y[m1] - y0, ev.md_anchor_x[m1] - x0);
    const float c02x = ev.md_anchor_x[m2] - x0;
    const float c02y = ev.md_anchor_y[m2] - y0;
    const float c02z = ev.md_anchor_z[m2] - o.zInner;
    const float c02xy = std::sqrt(c02x * c02x + c02y * c02y);
    o.tanLambda = c02z / std::max(c02xy, 1e-9f);
    return o;
  }

  TargetPre makeT3Pre(const LSTEventData& ev, int t) {
    TargetPre o = makeT3PreGeom(ev, t);
    const int m0 = ev.t3_md0[t], m1 = ev.t3_md1[t], m2 = ev.t3_md2[t];
    // rotSign = sign(z of cross(md0->md1, md1->md2)); collinear counts +1 (Features.cc).
    const float c01x = ev.md_anchor_x[m1] - ev.md_anchor_x[m0];
    const float c01y = ev.md_anchor_y[m1] - ev.md_anchor_y[m0];
    const float c12x = ev.md_anchor_x[m2] - ev.md_anchor_x[m1];
    const float c12y = ev.md_anchor_y[m2] - ev.md_anchor_y[m1];
    const float cross = c01x * c12y - c01y * c12x;
    o.rotSign = (cross >= 0.f) ? 1.f : -1.f;
    const float radius = std::isfinite(ev.t3_radius[t]) ? ev.t3_radius[t] : 1e12f;
    o.fitKappa = o.rotSign / std::max(radius, 1e-9f);
    o.innermostLayer = static_cast<float>(ev.md_layer[m0]);
    o.nLayersF = 3.f;   // a T3 is a 3-layer object by construction
    o.gateLogit = 0.f;  // no chain gate exists for a bare T3 (feature 18 flags the absence)
    const float cx = ev.t3_centerX[t], cy = ev.t3_centerY[t];
    if (std::isfinite(cx) && std::isfinite(cy)) {
      o.centerX = cx;
      o.centerY = cy;
      o.centerValid = true;
    }
    return o;
  }

  // Azimuth of the pLS direction of motion where its helix circle crosses radius R1
  // OUTWARD; falls back to the seed's production direction pls.phi when the circles do not
  // intersect (R1 outside [|d - r|, d + r]) or the circle is centred on the origin.
  // M20: extracted verbatim from dPhiAtInnermost so the binned candidate index can evaluate
  // the SAME quantity at arbitrary radii. dPhiAtInnermost is unchanged in value.
  float phiDirAtRadius(const PlsPre& pls, float R1) {
    float phiDir = pls.phi;  // fallback: seed direction at production
    if (pls.d > kEps) {
      const float a = (pls.d * pls.d + R1 * R1 - pls.r * pls.r) / (2.f * pls.d);
      const float h2 = R1 * R1 - a * a;
      if (h2 >= 0.f) {
        const float h = std::sqrt(h2);
        const float ux = pls.cx / pls.d, uy = pls.cy / pls.d;  // unit vector to the center
        float bestTx = 0.f, bestTy = 0.f, bestRadial = -std::numeric_limits<float>::infinity();
        for (int sgn = -1; sgn <= 1; sgn += 2) {
          const float px = a * ux - sgn * h * uy;
          const float py = a * uy + sgn * h * ux;
          // direction of motion: rotSign * perp(p - c), perp(v) = (-vy, vx)
          const float tx = pls.rotSign * (-(py - pls.cy));
          const float ty = pls.rotSign * (px - pls.cx);
          const float radial = tx * px + ty * py;  // outgoing crossing has radial > 0
          if (radial > bestRadial) {
            bestRadial = radial;
            bestTx = tx;
            bestTy = ty;
          }
        }
        phiDir = std::atan2(bestTy, bestTx);
      }
    }
    return phiDir;
  }

  // dPhiAtInnermost (feature 15 / prefilter window 2). See the definition block on top.
  inline float dPhiAtInnermost(const PlsPre& pls, const TargetPre& cp) {
    return wrapPhi(phiDirAtRadius(pls, cp.rtInner) - cp.chordPhi);
  }

  // Shared pair evaluation. Always computes both prefilter quantities when evalAll is
  // true (probe path); the fast path rejects on dTanLambda before touching the circle
  // propagation. fOut may be null (windows-only probe).
  // M20: enforceWindows == false (mode-2 map candidates only) keeps the two window
  // quantities as FEATURES but stops them from rejecting -- the external map IS the
  // prefilter in that mode. Every other caller leaves it true and is bit-exact.
  bool evalPair(const PlsPre& pls,
                const TargetPre& cp,
                const AttachParams& params,
                bool evalAll,
                float* fOut,
                float& absDTanL,
                float& absDPhi,
                bool enforceWindows = true) {
    const float dTanL = pls.tanLambda - cp.tanLambda;
    absDTanL = std::fabs(dTanL);
    if (enforceWindows && !evalAll && absDTanL >= params.prefDTanL)
      return false;

    const float dPhi = dPhiAtInnermost(pls, cp);
    absDPhi = std::fabs(dPhi);
    const bool pass = !enforceWindows || (absDTanL < params.prefDTanL && absDPhi < params.prefDPhi);
    if (!pass || fOut == nullptr)
      return pass;

    const float chargeAgree = (pls.rotSign == cp.rotSign) ? 1.f : 0.f;
    const float dKappa = pls.kappaSigned - cp.fitKappa;
    float centerDist = 0.f;  // flag value for a degenerate (collinear) chain fit
    if (cp.centerValid) {
      const float dcx = pls.cx - cp.centerX, dcy = pls.cy - cp.centerY;
      centerDist = std::sqrt(dcx * dcx + dcy * dcy);
    }
    const float zResid = pls.hit0z + pls.tanLambda * (cp.rtInner - pls.rt0) - cp.zInner;

    fOut[0] = pls.log10Pt;
    fOut[1] = pls.ptErrRel;
    fOut[2] = pls.etaErr;
    fOut[3] = pls.charge;
    fOut[4] = pls.isQuad;
    fOut[5] = pls.log10R;
    fOut[6] = pls.plsDeltaPhi;
    fOut[7] = cp.fitKappa;
    fOut[8] = cp.tanLambda;
    fOut[9] = cp.innermostLayer;
    fOut[10] = cp.nLayersF;
    fOut[11] = cp.gateLogit;
    fOut[12] = chargeAgree;
    fOut[13] = dKappa;
    fOut[14] = dTanL;
    fOut[15] = dPhi;
    fOut[16] = centerDist;
    fOut[17] = zResid;
    fOut[18] = cp.targetType;  // M16
    for (int i = 0; i < kAttachFeat; ++i)
      fOut[i] = sanitizeOne(fOut[i]);
    return true;
  }

  // ======================================================================================
  // M20 (T3ATTACH-BUILD): THE SCALAR BINNED CANDIDATE INDEX
  // Design, axes and the superset proof are in PixelAttachCand.h. Everything below is
  // mechanics; no physics quantity is defined here that is not already defined above.
  // ======================================================================================

  constexpr float kTwoPi = 2.f * kPi;

  // phi in [0, 2pi) -> bin. nPhi bins tile the circle exactly (phiW = 2pi/nPhi).
  inline int phiBinOf(float phi, float phiW, int nPhi) {
    float a = std::fmod(phi, kTwoPi);
    if (a < 0.f)
      a += kTwoPi;
    int b = static_cast<int>(a / phiW);
    if (b < 0)
      b = 0;
    if (b >= nPhi)
      b = nPhi - 1;
    return b;
  }

  inline int tanBinOf(float t, float tanClamp, float tanW, int nTan) {
    const float c = std::min(std::max(t, -tanClamp), tanClamp);
    int b = static_cast<int>((c + tanClamp) / tanW);
    if (b < 0)
      b = 0;
    if (b >= nTan)
      b = nTan - 1;
    return b;
  }

  inline int rtBinOf(float rt, float rtW, int nRt) {
    int b = static_cast<int>(rt / rtW);
    if (b < 0)
      b = 0;
    if (b >= nRt)
      b = nRt - 1;  // the last bin is open-ended by construction
    return b;
  }

  inline bool plsGeomFinite(const PlsPre& p) {
    return std::isfinite(p.tanLambda) && std::isfinite(p.phi) && std::isfinite(p.cx) && std::isfinite(p.cy) &&
           std::isfinite(p.r) && std::isfinite(p.d);
  }

  // The inserted phi arc of seed `p` over rt bin [lo, hi). Returns false when the arc is
  // too wide to describe (>= pi), which the caller answers by inserting into EVERY phi bin
  // -- the conservative direction, never a miss.
  bool plsPhiArc(const PlsPre& p, float lo, float hi, float& start, float& span) {
    float ang[5];
    int n = 0;
    bool fallback = (p.d <= kEps);
    if (p.d > kEps) {
      const float rMin = std::fabs(p.d - p.r), rMax = p.d + p.r;
      if (lo < rMin || hi > rMax)
        fallback = true;  // part of the bin is unreachable -> the code's pls.phi fallback
      const float a = std::max(lo, rMin), b = std::min(hi, rMax);
      if (a <= b) {
        // phiDirAtRadius is monotone in R (PixelAttachCand.h), so the endpoints bracket the
        // arc exactly; the two interior samples are belt and braces against a float edge.
        ang[n++] = phiDirAtRadius(p, a);
        ang[n++] = phiDirAtRadius(p, b);
        if (b > a) {
          ang[n++] = phiDirAtRadius(p, a + (b - a) * (1.f / 3.f));
          ang[n++] = phiDirAtRadius(p, a + (b - a) * (2.f / 3.f));
        }
      }
    }
    if (fallback)
      ang[n++] = p.phi;
    if (n == 0)
      return false;  // unreachable in practice; treat as "all bins"
    const float ref = ang[0];
    float dLo = 0.f, dHi = 0.f;
    for (int i = 1; i < n; ++i) {
      const float d = wrapPhi(ang[i] - ref);
      dLo = std::min(dLo, d);
      dHi = std::max(dHi, d);
    }
    span = dHi - dLo;
    start = ref + dLo;
    return span < kPi;  // a hull wider than pi is not reliably oriented -> all bins
  }

  // Walks phi bins forward from bin(a) to bin(b) inclusive (mod nPhi) and calls fn(bin).
  template <typename F>
  inline void forEachPhiBin(float a, float b, float phiW, int nPhi, F&& fn) {
    const int b0 = phiBinOf(a, phiW, nPhi);
    const int b1 = phiBinOf(b, phiW, nPhi);
    int steps = b1 - b0;
    if (steps < 0)
      steps += nPhi;
    for (int s = 0; s <= steps; ++s)
      fn((b0 + s) % nPhi);
  }

}  // namespace

void k8BuildPlsCandIndex(const LSTEventData& ev,
                         const AttachParams& params,
                         const CandIndexParams& ip,
                         PlsCandIndex& out) {
  const int nPls = static_cast<int>(ev.pLS_pt.size());
  out.nPls = nPls;
  out.wildPls.clear();
  out.cellItems.clear();
  out.qStamp.assign(nPls, -1);
  out.qEpoch = 0;

  out.rtW = std::max(ip.rtBinW, 0.5f);
  out.nRt = std::max(1, static_cast<int>(std::ceil(std::max(ip.rtMax, out.rtW) / out.rtW)));
  out.tanClamp = std::max(ip.tanClamp, 1.f);
  out.tanW = std::max(params.prefDTanL * std::max(ip.binMult, 0.05f), 1e-3f);
  out.nTan = std::min(8192, std::max(1, static_cast<int>(std::ceil(2.f * out.tanClamp / out.tanW))));
  out.nPhi = std::min(
      4096, std::max(1, static_cast<int>(kTwoPi / std::max(params.prefDPhi * std::max(ip.binMult, 0.05f), 1e-3f))));
  out.phiW = kTwoPi / static_cast<float>(out.nPhi);

  const long long nCell = static_cast<long long>(out.nRt) * out.nTan * out.nPhi;
  out.cellStart.assign(static_cast<std::size_t>(nCell) + 1, 0);
  if (nPls == 0)
    return;

  std::vector<PlsPre> pre;
  pre.reserve(nPls);
  for (int p = 0; p < nPls; ++p)
    pre.push_back(makePlsPre(ev, p));

  // Two passes over the same deterministic enumeration: count, then fill. Seeds are
  // visited in ascending row order, so every cell ends up ASCENDING in plsRow -- which is
  // what makes the emitted pair list identical to the full scan's.
  auto enumerateCells = [&](int p, auto&& sink) {
    const PlsPre& q = pre[p];
    const int tb = tanBinOf(q.tanLambda, out.tanClamp, out.tanW, out.nTan);
    for (int r = 0; r < out.nRt; ++r) {
      const float lo = static_cast<float>(r) * out.rtW;
      const float hi = (r == out.nRt - 1) ? 1e9f : lo + out.rtW;
      float start = 0.f, span = 0.f;
      const long long base = (static_cast<long long>(r) * out.nTan + tb) * out.nPhi;
      if (plsPhiArc(q, lo, hi, start, span) && span + 2.f * ip.phiPad < kTwoPi) {
        forEachPhiBin(
            start - ip.phiPad, start + span + ip.phiPad, out.phiW, out.nPhi, [&](int pb) { sink(base + pb); });
      } else {
        for (int pb = 0; pb < out.nPhi; ++pb)
          sink(base + pb);
      }
    }
  };

  for (int p = 0; p < nPls; ++p) {
    if (!plsGeomFinite(pre[p])) {
      out.wildPls.push_back(p);
      continue;
    }
    enumerateCells(p, [&](long long cell) { ++out.cellStart[static_cast<std::size_t>(cell) + 1]; });
  }
  for (long long c = 0; c < nCell; ++c)
    out.cellStart[static_cast<std::size_t>(c) + 1] += out.cellStart[static_cast<std::size_t>(c)];
  out.cellItems.assign(static_cast<std::size_t>(out.cellStart[static_cast<std::size_t>(nCell)]), 0);
  std::vector<int> fill(out.cellStart.begin(), out.cellStart.end() - 1);
  for (int p = 0; p < nPls; ++p) {
    if (!plsGeomFinite(pre[p]))
      continue;
    enumerateCells(p, [&](long long cell) { out.cellItems[fill[static_cast<std::size_t>(cell)]++] = p; });
  }
}

namespace {

  // Fills idx.qBuf with the candidate pLS rows for one target, ASCENDING and deduplicated.
  // Returns false when the target must fall back to the full scan (wild geometry).
  bool candidatesForTarget(const PlsCandIndex& idx, const TargetPre& cp, const AttachParams& params, CandStats* st) {
    idx.qBuf.clear();
    if (!std::isfinite(cp.rtInner) || !std::isfinite(cp.tanLambda) || !std::isfinite(cp.chordPhi)) {
      if (st != nullptr)
        ++st->nWildTargets;
      return false;
    }
    const int rb = rtBinOf(cp.rtInner, idx.rtW, idx.nRt);
    const float ct = std::min(std::max(cp.tanLambda, -idx.tanClamp), idx.tanClamp);
    const int tLo = tanBinOf(ct - params.prefDTanL, idx.tanClamp, idx.tanW, idx.nTan);
    const int tHi = tanBinOf(ct + params.prefDTanL, idx.tanClamp, idx.tanW, idx.nTan);

    const int epoch = ++idx.qEpoch;
    auto take = [&](int p) {
      if (idx.qStamp[p] == epoch)
        return;
      idx.qStamp[p] = epoch;
      idx.qBuf.push_back(p);
    };
    for (int tb = tLo; tb <= tHi; ++tb) {
      const long long base = (static_cast<long long>(rb) * idx.nTan + tb) * idx.nPhi;
      forEachPhiBin(cp.chordPhi - params.prefDPhi, cp.chordPhi + params.prefDPhi, idx.phiW, idx.nPhi, [&](int pb) {
        const std::size_t c = static_cast<std::size_t>(base + pb);
        for (int i = idx.cellStart[c]; i < idx.cellStart[c + 1]; ++i)
          take(idx.cellItems[i]);
      });
    }
    for (int p : idx.wildPls)
      take(p);
    std::sort(idx.qBuf.begin(), idx.qBuf.end());
    return true;
  }

  // One target's emission, shared by every candidate mode. `cands`/`nCands` == the pLS rows
  // to test (null => every row, the frozen full scan).
  void emitTarget(const std::vector<PlsPre>& pls,
                  int nPls,
                  const TargetPre& cp,
                  const AttachParams& params,
                  int8_t ttype,
                  int chainPos,
                  int t3Row,
                  int targetOrd,
                  const int* cands,
                  int nCands,
                  bool enforceWindows,
                  std::vector<AttachPair>& out,
                  CandStats* st) {
    const int n = (cands == nullptr) ? nPls : nCands;
    const std::size_t before = out.size();
    for (int i = 0; i < n; ++i) {
      const int p = (cands == nullptr) ? i : cands[i];
      AttachPair ap;
      float aDT, aDP;
      if (!evalPair(pls[p], cp, params, false, ap.f, aDT, aDP, enforceWindows))
        continue;
      ap.ttype = ttype;
      ap.chainPos = chainPos;
      ap.t3Row = t3Row;
      ap.targetOrd = targetOrd;
      ap.plsRow = p;
      out.push_back(ap);
    }
    if (st == nullptr)
      return;
    ++st->nTargets;
    st->nFullScan += nPls;
    st->nExamined += n;
    st->nEmitted += static_cast<long long>(out.size() - before);
    if (params.candAudit && cands != nullptr) {
      // SUPERSET AUDIT. Run the analytic full scan and check, pair by pair, that every pair
      // it accepts is PRESENT in the candidate set the finder produced. `cands` is sorted
      // ascending in both modes, so membership is a binary search. For the binned prefilter
      // st->nMissing MUST be 0 on every event -- that is the correctness gate. For map mode
      // the same counter is read as COVERAGE of the analytic window set, not as a bug.
      long long nAna = 0, nMiss = 0;
      for (int p = 0; p < nPls; ++p) {
        float aDT, aDP;
        if (!evalPair(pls[p], cp, params, false, nullptr, aDT, aDP))
          continue;
        ++nAna;
        if (!std::binary_search(cands, cands + nCands, p))
          ++nMiss;
      }
      st->nAnalytic += nAna;
      st->nMissing += nMiss;
    }
  }

}  // namespace

void k8EnumeratePrefilteredPairsGeneral(const LSTEventData& ev,
                                        const Chains& chains,
                                        const std::vector<int>& acceptedChains,
                                        const ChainFeatures& cf,
                                        const std::vector<float>& chainGateLogits,
                                        const std::vector<char>& bareT3Mask,
                                        const AttachParams& params,
                                        std::vector<AttachPair>& out) {
  out.clear();
  const int nPls = static_cast<int>(ev.pLS_pt.size());
  if (nPls == 0)
    return;

  std::vector<PlsPre> pls;
  pls.reserve(nPls);
  for (int p = 0; p < nPls; ++p)
    pls.push_back(makePlsPre(ev, p));

  int targetOrd = 0;

  // M20: which target kinds use the pluggable candidate finder. Chain targets keep the
  // frozen full scan unless -CFC asks otherwise, so nothing the M19 freeze measured can
  // move when only the bare-T3 side is being developed.
  const PlsCandIndex* idx = params.cand;
  const int mode = (idx == nullptr) ? kCandAnalytic : params.candMode;
  CandStats* st = params.candStats;

  // (a) accepted chains, nLayers >= 5 (ttype 0) -- unchanged M7 behavior.
  const bool chainUsesIdx = (mode == kCandBinned) && params.candChainToo;
  for (int pos = 0; pos < static_cast<int>(acceptedChains.size()); ++pos) {
    const int c = acceptedChains[pos];
    if (chains.nLayers[c] < params.minChainLayers)
      continue;  // v1 scope (default 5): attach only to pT5-class chains
    const float gate = c < static_cast<int>(chainGateLogits.size()) ? chainGateLogits[c] : 0.f;
    const TargetPre cp = makeChainPre(ev, chains, c, cf, gate);
    const int* cands = nullptr;
    int nCands = 0;
    if (chainUsesIdx && candidatesForTarget(*idx, cp, params, st)) {
      cands = idx->qBuf.data();
      nCands = static_cast<int>(idx->qBuf.size());
    }
    emitTarget(
        pls, nPls, cp, params, static_cast<int8_t>(kAttachTargetChain), pos, -1, targetOrd, cands, nCands, true, out, st);
    ++targetOrd;
  }

  // (b) M16: bare T3s (ttype 1) -- the SAME prefilter, the SAME feature builder.
  const int nT3 = static_cast<int>(bareT3Mask.size());
  for (int t = 0; t < nT3; ++t) {
    if (!bareT3Mask[t])
      continue;
    const TargetPre cp = makeT3Pre(ev, t);
    const int* cands = nullptr;
    int nCands = 0;
    bool enforce = true;
    if (mode == kCandBinned) {
      if (candidatesForTarget(*idx, cp, params, st)) {
        cands = idx->qBuf.data();
        nCands = static_cast<int>(idx->qBuf.size());
      }
    } else if (mode == kCandMap) {
      // The external map IS the prefilter here: the analytic windows are features, not
      // gates, unless -CFW asks for both.
      enforce = params.candMapWindows;
      if (t + 1 < static_cast<int>(idx->mapStart.size())) {
        const int b = idx->mapStart[t], e = idx->mapStart[t + 1];
        cands = idx->mapItems.data() + b;
        nCands = e - b;
        if (nCands == 0 && st != nullptr)
          ++st->nMapTargetsEmpty;
      } else {
        static const int kNone = 0;
        cands = &kNone;
        nCands = 0;
        if (st != nullptr)
          ++st->nMapTargetsEmpty;
      }
    }
    emitTarget(
        pls, nPls, cp, params, static_cast<int8_t>(kAttachTargetT3), -1, t, targetOrd, cands, nCands, enforce, out, st);
    ++targetOrd;
  }
}

void k8EnumeratePrefilteredPairs(const LSTEventData& ev,
                                 const Chains& chains,
                                 const std::vector<int>& acceptedChains,
                                 const ChainFeatures& cf,
                                 const std::vector<float>& chainGateLogits,
                                 const AttachParams& params,
                                 std::vector<AttachPair>& out) {
  static const std::vector<char> kNoBareT3;  // chain targets only (M7 callers)
  k8EnumeratePrefilteredPairsGeneral(ev, chains, acceptedChains, cf, chainGateLogits, kNoBareT3, params, out);
}

void k8BuildBareT3Mask(const LSTEventData& ev,
                       const Chains& chains,
                       const std::vector<int>& acceptedChains,
                       std::vector<char>& mask) {
  const int nT3 = static_cast<int>(ev.t3_lsIdx0.size());
  mask.assign(nT3, 1);
  for (int c : acceptedChains) {
    for (int k = chains.offsets[c]; k < chains.offsets[c + 1]; ++k) {
      const int t = chains.items[k];
      if (t >= 0 && t < nT3)
        mask[t] = 0;
    }
  }
}

bool k8ProbePairWindows(const LSTEventData& ev,
                        const Chains& chains,
                        int chainIdx,
                        const AttachParams& params,
                        int plsRow,
                        float& absDTanL,
                        float& absDPhi) {
  const PlsPre pls = makePlsPre(ev, plsRow);
  const TargetPre cp = makeChainPreGeom(ev, chains, chainIdx);
  return evalPair(pls, cp, params, true, nullptr, absDTanL, absDPhi);
}

bool k8ProbePairWindowsT3(
    const LSTEventData& ev, int t3Row, const AttachParams& params, int plsRow, float& absDTanL, float& absDPhi) {
  const PlsPre pls = makePlsPre(ev, plsRow);
  const TargetPre cp = makeT3PreGeom(ev, t3Row);
  return evalPair(pls, cp, params, true, nullptr, absDTanL, absDPhi);
}

float k8ChainDcaXY(const LSTEventData& ev, const Chains& chains, int c) {
  const int mb = chains.mdOffsets[c], me = chains.mdOffsets[c + 1];
  const int nMD = me - mb;
  if (nMD < 2)
    return 1e9f;  // unreachable by the K6 contract; never IP-compatible

  // Kasa circle fit, EXACTLY the ChainFeatures.cc algorithm (incl. the radius,
  // R^2 = uc^2 + vc^2 + Sw/n, which makeChainPreGeom does not need and omits).
  if (nMD >= 3) {
    double xbar = 0.0, ybar = 0.0;
    for (int k = mb; k < me; ++k) {
      xbar += ev.md_anchor_x[chains.mdItems[k]];
      ybar += ev.md_anchor_y[chains.mdItems[k]];
    }
    xbar /= nMD;
    ybar /= nMD;
    double Suu = 0.0, Svv = 0.0, Suv = 0.0, Suw = 0.0, Svw = 0.0, Sw = 0.0;
    for (int k = mb; k < me; ++k) {
      const double u = ev.md_anchor_x[chains.mdItems[k]] - xbar;
      const double v = ev.md_anchor_y[chains.mdItems[k]] - ybar;
      const double w = u * u + v * v;
      Suu += u * u;
      Svv += v * v;
      Suv += u * v;
      Suw += u * w;
      Svw += v * w;
      Sw += w;
    }
    const double det = Suu * Svv - Suv * Suv;
    const double scale = Suu + Svv;
    if (det > 1e-12 * scale * scale) {
      const double uc = (Svv * (0.5 * Suw) - Suv * (0.5 * Svw)) / det;
      const double vc = (Suu * (0.5 * Svw) - Suv * (0.5 * Suw)) / det;
      const double cx = xbar + uc, cy = ybar + vc;
      const double R = std::sqrt(std::max(uc * uc + vc * vc + Sw / nMD, 0.0));
      return static_cast<float>(std::fabs(std::sqrt(cx * cx + cy * cy) - R));
    }
  }

  // Degenerate (collinear) fit: straight-line limit -- perpendicular distance from the
  // origin to the line through the innermost and outermost anchor hits.
  const int m0 = chains.mdItems[mb], m1 = chains.mdItems[me - 1];
  const double x1 = ev.md_anchor_x[m0], y1 = ev.md_anchor_y[m0];
  const double x2 = ev.md_anchor_x[m1], y2 = ev.md_anchor_y[m1];
  const double len = std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
  if (len < 1e-9)
    return 1e9f;
  return static_cast<float>(std::fabs(x1 * y2 - x2 * y1) / len);
}

void k8AttachPixels(const LSTEventData& ev,
                    const Chains& chains,
                    const std::vector<int>& acceptedChains,
                    const ChainFeatures& cf,
                    const std::vector<float>& chainGateLogits,
                    const AttachParams& params,
                    Attachments& out) {
  const int nAcc = static_cast<int>(acceptedChains.size());
  const float negInf = -std::numeric_limits<float>::infinity();
  out.plsRow.assign(nAcc, -1);
  out.logit.assign(nAcc, negInf);
  out.bestLogit.assign(nAcc, negInf);
  out.nPairsPrefiltered = 0;
  out.nPairsScored = 0;

  std::vector<AttachPair> pairs;
  k8EnumeratePrefilteredPairs(ev, chains, acceptedChains, cf, chainGateLogits, params, pairs);
  out.nPairsPrefiltered = static_cast<long long>(pairs.size());

  // Score every prefiltered pair (0-sentinel before training, AttachInference.h) and
  // keep the per-chain best above thetaAttach; ties -> lower pLS row (pairs arrive
  // plsRow-ascending per chain, so strict > keeps the first).
  for (const AttachPair& pr : pairs) {
    const float lo = attachLogit(pr.f);
    ++out.nPairsScored;
    if (lo > out.bestLogit[pr.chainPos])
      out.bestLogit[pr.chainPos] = lo;  // pre-theta, pre-contention (-A 3 evidence)
    if (lo < params.thetaAttach)
      continue;
    if (out.plsRow[pr.chainPos] < 0 || lo > out.logit[pr.chainPos]) {
      out.plsRow[pr.chainPos] = pr.plsRow;
      out.logit[pr.chainPos] = lo;
    }
  }

  // pLS contention (one chain per pLS): higher logit wins; tie -> the earlier accepted-
  // chain position keeps it. The losing chain gets NO attachment (v1: no fallback).
  std::unordered_map<int, int> owner;  // plsRow -> chainPos currently holding it
  owner.reserve(nAcc * 2);
  for (int pos = 0; pos < nAcc; ++pos) {
    const int p = out.plsRow[pos];
    if (p < 0)
      continue;
    auto it = owner.find(p);
    if (it == owner.end()) {
      owner.emplace(p, pos);
      continue;
    }
    const int prev = it->second;
    if (out.logit[pos] > out.logit[prev]) {  // tie keeps prev (deterministic)
      out.plsRow[prev] = -1;
      out.logit[prev] = negInf;
      it->second = pos;
    } else {
      out.plsRow[pos] = -1;
      out.logit[pos] = negInf;
    }
  }
}
