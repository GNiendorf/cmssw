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
// Pair features (kAttachFeat = 18, frozen order; implementer documents exact defs at
// the definition site):
//   pLS side (7):   0 log10(ptIn), 1 ptErr/ptIn, 2 etaErr, 3 charge, 4 isQuad,
//                   5 log10(circleRadius), 6 deltaPhi
//   chain side (5): 7 fitKappaSigned, 8 chainTanLambda, 9 innermostLayer, 10 nLayers,
//                   11 chainGateLogit
//   pair (6):       12 chargeAgree (pLS charge vs chain rotation sign),
//                   13 dKappa (pLS 1/R signed by charge - chain fitKappa),
//                   14 dTanLambda (pLS tanLambda from eta vs chain tanLambda),
//                   15 dPhiAtInnermost (pLS circle propagated to the chain's innermost
//                      hit radius vs the chain's innermost chord direction),
//                   16 circleCenterDist (pLS circle center vs chain fit center),
//                   17 zResidAtInnermost (pLS hit0 z + tanLambda * (rt_inner - rt_hit0)
//                      minus the chain's innermost anchor z)
constexpr int kAttachFeat = 18;

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

#endif
