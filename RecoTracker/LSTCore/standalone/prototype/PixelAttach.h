#ifndef PROTOTYPE_PIXELATTACH_H
#define PROTOTYPE_PIXELATTACH_H

// K8 pixel attach (plan section 3 K8, 5a "additive evidence"): match pLS seeds to
// ACCEPTED chains so chain+pLS becomes a pT5-class TC, the attached pLS's baseline
// pixel TCs are suppressed (structural crossclean, replacing the partOfPT5 pixdrop for
// attached chains), and failure-to-attach becomes usable evidence later.
//
// v1 scope decisions (documented for the record):
//   - Attach only to accepted chains with nLayers >= 5 (chain+pLS -> type 7 pT5-class).
//     4-layer chains stay bare T4-class; pT3s (pLS + single T3) remain baseline-copied.
//   - IP-referenced pLS features are ALLOWED here (plan: a pixel seed is legitimately an
//     IP-region object); chain-side features stay origin-free.
//   - Prototype uses a cheap analytic prefilter + a small trained pair head (the
//     production version replaces the prefilter with the invariant-keyed grid).
//
// ============================ M16 CONTRACT BUMP (18 -> 19) ============================
// GENERAL pLS->OT ATTACH (maintainer design decision, plan 11 / 2026-08-01): ONE attach
// machine over BOTH outer-tracker target kinds, not per-type machinery --
//   ttype 0 = an ACCEPTED CHAIN (the existing target; nLayers >= 5),
//   ttype 1 = a BARE T3, i.e. a T3 that is NOT a member of any K9-ACCEPTED chain (the
//             membership mask is computed AFTER arbitration, k8BuildBareT3Mask below).
//             pLS + bare T3 is the pT3-class delivery path that replaces LST's superbin
//             + pt3dnn machinery.
// The prefilter is IDENTICAL for both kinds (pLS circle propagated to the TARGET's
// innermost anchor rt, then the same |dTanLambda| / |dPhiAtInnermost| windows) -- one
// helix-propagation candidate finder, target-agnostic, per the design decision.
// Target-side features 7-11 are MAPPED onto the same slots for a T3 target (exact
// definitions at the PixelAttach.cc definition site):
//   7 fitKappaSigned -> the T3's signed curvature rotSign / t3_radius (rotSign = sign of
//     the z-component of cross(md0->md1, md1->md2), collinear = +1; the Features.cc node
//     convention verbatim)
//   8 chainTanLambda -> dz02 / ds02 of the T3 anchors (ds02 = xy chord length md0->md2)
//   9 innermostLayer -> md_layer of md0
//  10 nLayers        -> 3 (a T3 is a 3-layer object by construction)
//  11 chainGateLogit -> 0 (ABSENT for a T3: no chain gate exists; feature 18 tells the
//     head that slot 11 is a structural zero, which is exactly why targetType is a
//     feature and not just a bookkeeping branch)
// NEW feature 18 (the only layout addition):
//  18 targetType     -> 0 = accepted chain, 1 = bare T3 (the categorical the design
//     decision calls for: "target-side features unified + layer/node count as
//     categorical inputs")
// Feature slots 0-17 are UNCHANGED in definition and order, so a legacy 18-input head
// (the M7 attach_mlp_weights.h) still scores chain targets bit-identically -- see the
// AttachInference.cc kInput <= kAttachFeat rule.
// ======================================================================================
//
// Pair features (kAttachFeat = 19, frozen order; implementer documents exact defs at
// the definition site):
//   pLS side (7):   0 log10(ptIn), 1 ptErr/ptIn, 2 etaErr, 3 charge, 4 isQuad,
//                   5 log10(circleRadius), 6 deltaPhi
//   target side (5):7 fitKappaSigned, 8 chainTanLambda, 9 innermostLayer, 10 nLayers,
//                   11 chainGateLogit
//   pair (6):       12 chargeAgree (pLS charge vs target rotation sign),
//                   13 dKappa (pLS 1/R signed by charge - target fitKappa),
//                   14 dTanLambda (pLS tanLambda from eta vs target tanLambda),
//                   15 dPhiAtInnermost (pLS circle propagated to the target's innermost
//                      hit radius vs the target's innermost chord direction),
//                   16 circleCenterDist (pLS circle center vs target fit center),
//                   17 zResidAtInnermost (pLS hit0 z + tanLambda * (rt_inner - rt_hit0)
//                      minus the target's innermost anchor z)
//   target kind (1):18 targetType (0 = accepted chain, 1 = bare T3)
constexpr int kAttachFeat = 19;

// M16: the two target kinds of the general attach (feature 18 / the pairdump ttype
// branch carry exactly these values).
enum AttachTargetType : int { kAttachTargetChain = 0, kAttachTargetT3 = 1 };

#include <vector>

#include "ChainFeatures.h"
#include "EventData.h"
#include "Stages.h"

struct AttachParams {
  // Prefilter windows (loose, efficiency-first; measured before tightening):
  float prefDPhi = 0.4f;        // |dPhiAtInnermost| window
  float prefDTanL = 0.6f;       // |dTanLambda| window
  float thetaAttach = 0.f;      // pair-head logit threshold to attach
  bool suppressPixel = true;    // suppress baseline pT5/pLS TCs of the attached pLS
};

struct Attachments {
  // Parallel to the acceptedChains vector passed in: best pLS row (into ev.pLS_*) or -1,
  // and the pair-head logit for it (-inf if none).
  std::vector<int> plsRow;
  std::vector<float> logit;
  // Best scored-pair logit per chain position BEFORE the thetaAttach cut and BEFORE pLS
  // contention (-inf if the chain had no prefiltered pair at all). This is the M9 -A 3
  // attach-as-EVIDENCE quantity: "failed attach" there means NO pair above thetaAttach
  // (bestLogit < thetaAttach), NOT losing the one-chain-per-pLS contention -- a chain
  // that found a compatible pLS but lost exclusivity still has pixel evidence.
  std::vector<float> bestLogit;
  long long nPairsPrefiltered = 0, nPairsScored = 0;
};

// Computes candidate pairs (prefilter), scores them with the attach head (generated
// header attach_mlp_weights.h once trained; a -1 sentinel path must exist for running
// BEFORE the head is trained: score = 0 for all prefiltered pairs, used by the dump).
void k8AttachPixels(const LSTEventData& ev,
                    const Chains& chains,
                    const std::vector<int>& acceptedChains,
                    const ChainFeatures& cf,
                    const std::vector<float>& chainGateLogits,
                    const AttachParams& params,
                    Attachments& out);

// M16: BARE-T3 membership mask. mask[t] = 1 iff T3 row t is NOT a member of ANY chain in
// acceptedChains -- the second target universe of the general attach. MUST be called
// AFTER K9 arbitration (the mask is defined against the ACCEPTED set, not the welded
// set): a T3 welded into a chain that K9 rejected is still bare and remains attachable,
// which is precisely the pT3-class population LST recovers with its superbin machinery.
// No other filter is applied (in particular t3_partOfPT3 / t3_partOfPT5 T3s stay in the
// universe: those ARE the tracks the general attach is meant to deliver itself).
void k8BuildBareT3Mask(const LSTEventData& ev,
                       const Chains& chains,
                       const std::vector<int>& acceptedChains,
                       std::vector<char>& mask);

// M7c (a): transverse DCA of the chain's full-fit circle to the origin,
// |dist(center, origin) - R|, from EXACTLY the ChainFeatures.cc Kasa fit over the
// chain's MD anchor hits (double accumulation, same degeneracy guard, R^2 = uc^2 +
// vc^2 + Sw/n). Degenerate (collinear) fit falls back to the straight-line limit:
// perpendicular distance from the origin to the line through the innermost and
// outermost anchor hits. Chains with < 2 MDs (unreachable by the K6 contract) return
// 1e9 (never IP-compatible). Used as the IP-compatibility gate on attach ELIGIBILITY
// and by the K7-lite kinematic dedup (both -A 2 only).
float k8ChainDcaXY(const LSTEventData& ev, const Chains& chains, int c);

#endif
