// K8 pixel attach (M7, plan section 3 K8 / 5a): implementation of the frozen
// PixelAttach.h contract plus the shared prefilter pair enumeration (PixelAttachPairs.h).
//
// EXACT FEATURE DEFINITIONS (kAttachFeat = 18, frozen order; the header delegates the
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
//
// PREFILTER (efficiency-first, plan v1): charge compatibility NOT required (sign flips
// exist for near-straight/displaced tracks, the M2 lesson); |dTanLambda| < prefDTanL
// AND |dPhiAtInnermost| < prefDPhi. Only chains with nLayers >= 5 participate (v1
// scope: chain+pLS -> pT5-class).
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
                                                   "zResidAtInnermost"};

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
  float tanLambda;   // pz / max(pt, eps)
  float kappaSigned; // -charge / max(circleRadius, eps)
  float rotSign;     // -charge (empirically verified rotation sense)
  float phi;         // pLS_phi (dPhi fallback)
  float cx, cy, r;   // circle center / radius
  float d;           // |center|
  float hit0z, rt0;  // innermost seed hit z and rt
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

// ---- per-chain hoisted quantities -----------------------------------------------------
struct ChainPre {
  // geometry (probe path needs only these)
  float rtInner = 0.f, zInner = 0.f, chordPhi = 0.f;
  float tanLambda = 0.f;  // rz line fit slope (0 if degenerate)
  // feature block 7-11 (cf row + gate logit; unused by the probe)
  float fitKappa = 0.f, innermostLayer = 0.f, nLayersF = 0.f, gateLogit = 0.f;
  float rotSign = 1.f;  // sign(fitKappa), 0 -> +1
  // chain Kasa fit center (feature 16); centerValid false -> flag value 0
  float centerX = 0.f, centerY = 0.f;
  bool centerValid = false;
};

// Geometry part: innermost anchor rt/z, innermost chord phi, rz-fit tanLambda, Kasa
// center -- all over chains.mdItems (innermost-first), double accumulation exactly as
// ChainFeatures.cc.
ChainPre makeChainPreGeom(const LSTEventData& ev, const Chains& chains, int c) {
  ChainPre o;
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

ChainPre makeChainPre(
    const LSTEventData& ev, const Chains& chains, int c, const ChainFeatures& cf, float gateLogit) {
  ChainPre o = makeChainPreGeom(ev, chains, c);
  const float* f = &cf.f[static_cast<std::size_t>(c) * kChainFeat];
  o.fitKappa = f[7];
  o.innermostLayer = f[10];
  o.nLayersF = f[1];
  o.gateLogit = gateLogit;
  o.rotSign = (o.fitKappa >= 0.f) ? 1.f : -1.f;
  return o;
}

// dPhiAtInnermost (feature 15 / prefilter window 2). See the definition block on top.
float dPhiAtInnermost(const PlsPre& pls, const ChainPre& cp) {
  float phiDir = pls.phi;  // fallback: seed direction at production
  const float R1 = cp.rtInner;
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
  return wrapPhi(phiDir - cp.chordPhi);
}

// Shared pair evaluation. Always computes both prefilter quantities when evalAll is
// true (probe path); the fast path rejects on dTanLambda before touching the circle
// propagation. fOut may be null (windows-only probe).
bool evalPair(const PlsPre& pls,
              const ChainPre& cp,
              const AttachParams& params,
              bool evalAll,
              float* fOut,
              float& absDTanL,
              float& absDPhi) {
  const float dTanL = pls.tanLambda - cp.tanLambda;
  absDTanL = std::fabs(dTanL);
  if (!evalAll && absDTanL >= params.prefDTanL)
    return false;

  const float dPhi = dPhiAtInnermost(pls, cp);
  absDPhi = std::fabs(dPhi);
  const bool pass = absDTanL < params.prefDTanL && absDPhi < params.prefDPhi;
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
  for (int i = 0; i < kAttachFeat; ++i)
    fOut[i] = sanitizeOne(fOut[i]);
  return true;
}

}  // namespace

void k8EnumeratePrefilteredPairs(const LSTEventData& ev,
                                 const Chains& chains,
                                 const std::vector<int>& acceptedChains,
                                 const ChainFeatures& cf,
                                 const std::vector<float>& chainGateLogits,
                                 const AttachParams& params,
                                 std::vector<AttachPair>& out) {
  out.clear();
  const int nPls = static_cast<int>(ev.pLS_pt.size());
  if (nPls == 0 || acceptedChains.empty())
    return;

  std::vector<PlsPre> pls;
  pls.reserve(nPls);
  for (int p = 0; p < nPls; ++p)
    pls.push_back(makePlsPre(ev, p));

  for (int pos = 0; pos < static_cast<int>(acceptedChains.size()); ++pos) {
    const int c = acceptedChains[pos];
    if (chains.nLayers[c] < 5)
      continue;  // v1 scope: attach only to pT5-class chains
    const float gate = c < static_cast<int>(chainGateLogits.size()) ? chainGateLogits[c] : 0.f;
    const ChainPre cp = makeChainPre(ev, chains, c, cf, gate);
    for (int p = 0; p < nPls; ++p) {
      AttachPair ap;
      float aDT, aDP;
      if (!evalPair(pls[p], cp, params, false, ap.f, aDT, aDP))
        continue;
      ap.chainPos = pos;
      ap.plsRow = p;
      out.push_back(ap);
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
  const ChainPre cp = makeChainPreGeom(ev, chains, chainIdx);
  return evalPair(pls, cp, params, true, nullptr, absDTanL, absDPhi);
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
