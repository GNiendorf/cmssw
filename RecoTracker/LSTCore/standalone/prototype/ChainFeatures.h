#ifndef PROTOTYPE_CHAINFEATURES_H
#define PROTOTYPE_CHAINFEATURES_H

// Chain-level features for the HARD chain gate (plan 5a: the one irreversible decision,
// taken at maximum evidence per candidate; M6). Computed per WELDED chain (K6 output,
// PRE-arbitration), aggregating the member edge log-odds with full-length fit residuals
// and detector-category counts.
//
// FROZEN CONTRACT (kChainFeat = 16 floats per chain, row-major [c * kChainFeat + i]):
//   0 nNodes             : number of member T3 nodes (K6 contract: >= 2)
//   1 nLayers            : chains.nLayers[c] = distinct md_layer count over the MD set
//   2 sumEdgeLogit       : sum of member weld-edge MLP logits (log-odds; K6's edgeSum)
//   3 minEdgeLogit       : min member weld-edge logit (the weakest link)
//   4 meanEdgeLogit      : sumEdgeLogit / nEdges (nEdges = nNodes - 1 >= 1)
//   5 fullFitChi2PerHit  : single xy circle fit over ALL chain MD ANCHOR hits
//                          (chains.mdItems, innermost-first), Kasa algebraic fit --
//                          the same algebraic family as the production T3 three-point
//                          fit (computeRadiusFromThreeAnchorHits solves the circle
//                          algebraically; Kasa is its N-point least-squares extension
//                          and reproduces the exact circle at N=3). Residual
//                          chi2 = sum_i (dist(hit_i, center) - R)^2 / nHits, UNWEIGHTED
//                          (no hit uncertainties), units cm^2. Known Kasa caveat:
//                          small-arc radius underestimate; acceptable for a gate input.
//   6 rzLineChi2PerHit   : straight-line fit z vs s over the same anchor hits, where
//                          s = cumulative xy chord length from the innermost hit
//                          (arc-length proxy; robust in the endcap where rt saturates).
//                          chi2 = sum_i (z_i - a - b*s_i)^2 / nHits, units cm^2.
//   7 fitKappa           : signed curvature 1/R_fit from feature 5's circle fit;
//                          sign = chain rotation sign (see rotation convention below).
//   8 dKappaFitVsMedianT3: fitKappa - median member T3 kappaSigned, where kappaSigned =
//                          rotSign(T3) / t3_radius exactly as NodeFeatures f[0]
//                          (Features.h). Median = LOWER median (sorted element
//                          (n-1)/2), same convention as K10's pt median.
//   9 ptEst              : LOWER median of member t3_pt (K10's TC pt convention).
//  10 innermostLayer     : md_layer of the innermost chain MD (chains.mdItems front;
//                          1-6 barrel, 7-11 endcap).
//  11 layerSpan          : max - min md_layer over the chain MD set (NOTE: raw
//                          md_layer arithmetic, barrel 1-6 and endcap 7-11 share the
//                          integer axis by contract).
//  12 nPS                : count of chain MDs with md_type == 1 (PS modules).
//  13 nBarrel            : count of chain MDs with md_layer <= 6 (barrel).
//  14 maxJunctionDegProduct: max over member weld edges of degIn*degOut at the edge's
//                          shared key, from the K1 incidence CSRs (E1: mdT3In/Out at
//                          the shared MD; E2: lsT3In/Out at the shared LS) -- the same
//                          degrees as EdgeFeatures f[12]/f[13]. Local combinatorial
//                          density at the chain's internal junctions.
//  15 chargeConsistency  : fraction of the C(nNodes,2) member T3 PAIRS with equal
//                          rotSign (rotSign as in Features.h node f[0]: sign of z of
//                          cross(c01, c12) over anchor hits, collinear counts +1).
//
// Rotation convention for feature 7: chain rotSign = sign of the SUM over consecutive
// anchor-hit triplets (innermost-first mdItems order) of the z component of
// cross(h_{k+1}-h_k, h_{k+2}-h_{k+1}) -- the majority rotation over the whole chain;
// sum == 0 counts as +1 (matches the T3 collinear convention).
//
// Degenerate-fit / division guards (documented flag values; NO NaN/Inf ever, enforced
// by a final sanitize pass identical to Features.cc):
//   - circle fit with nHits < 3 or collinear hits (normal-equation determinant
//     det <= 1e-12 * (Suu+Svv)^2, double precision): fullFitChi2PerHit = 0 and
//     fitKappa = 0 (a straight line IS zero curvature with zero circle residual);
//   - rz line fit with degenerate abscissa (sum (s-sbar)^2 <= 1e-12): rzLineChi2PerHit = 0;
//   - meanEdgeLogit with nEdges == 0 (unreachable, K6 emits >= 2 nodes): 0, and
//     minEdgeLogit = 0;
//   - chargeConsistency with < 2 members (unreachable): 1 (vacuously consistent);
//   - fit radius clamped below at 1e-6 cm before 1/R; non-finite t3_radius mapped to
//     1e12 (Features.cc cleanRadius convention) before the member-kappa median.
//
// All fit accumulation is done in double; features are stored as float.

#include <vector>

#include "EventData.h"
#include "Stages.h"

constexpr int kChainFeat = 16;

// Ordered names for the feature_spec TNamed (defined in ChainFeatures.cc; MUST stay in
// sync with the contract above).
extern const char* const kChainFeatNames[kChainFeat];

struct ChainFeatures {
  std::vector<float> f;  // nChains * kChainFeat
};

// Requires chains.edgeOffsets/edgeItems (the K6 weld-edge CSR): the edge aggregates and
// junction degrees must reflect the edges K6 actually welded, not a recomputed guess
// (parallel E1/E2 edges between the same node pair would be ambiguous).
void computeChainFeatures(const LSTEventData& ev,
                          const ChainGraph& g,
                          const Chains& chains,
                          const EdgeScores& scores,
                          ChainFeatures& out);

#endif
