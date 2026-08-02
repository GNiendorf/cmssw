#ifndef PROTOTYPE_PIXELATTACHPAIRS_H
#define PROTOTYPE_PIXELATTACHPAIRS_H

// K8 pair-enumeration support (M7): the PREFILTER-ONLY (chain, pLS) pair enumeration
// with the full kAttachFeat feature vector, SHARED between k8AttachPixels (which scores
// and selects) and the pairdump training factory (which labels and writes every pair) --
// one code path for features, the plan 10.3 discipline. Additive companion to the
// frozen PixelAttach.h contract; exact feature definitions are documented at the
// definition site in PixelAttach.cc.

#include <vector>

#include "PixelAttach.h"

// Ordered names for the pairdump feature_spec TNamed (defined in PixelAttach.cc; MUST
// stay in sync with the kAttachFeat contract in PixelAttach.h).
extern const char* const kAttachFeatNames[kAttachFeat];

struct AttachPair {
  // M16: pairs now carry their TARGET KIND. ttype == kAttachTargetChain -> chainPos is a
  // position in the acceptedChains vector passed in and t3Row is -1; ttype ==
  // kAttachTargetT3 -> t3Row is a row into ev.t3_* and chainPos is -1.
  int8_t ttype = static_cast<int8_t>(kAttachTargetChain);
  int chainPos = -1;  // position in the acceptedChains vector passed in (chain targets)
  int t3Row = -1;     // row into ev.t3_* (bare-T3 targets)
  int plsRow = -1;    // row into ev.pLS_*
  // M16: unified target ordinal into the enumeration's target list (chain targets first
  // in acceptedChains order, then bare T3s in t3-row order). Lets a caller build per-
  // target CSR spans over a mixed pair list without re-deriving the ordering.
  int targetOrd = -1;
  float f[kAttachFeat] = {};
};

// Enumerates every (accepted chain with nLayers >= 5, pLS) pair passing the analytic
// prefilter (charge compatibility NOT required -- sign flips exist; |dTanLambda| <
// params.prefDTanL and |dPhiAtInnermost| < params.prefDPhi), with all kAttachFeat
// features computed and NaN/Inf-guarded. Pairs are emitted grouped by chainPos
// ascending, plsRow ascending within a chain (deterministic).
// Chain-targets-only wrapper over k8EnumeratePrefilteredPairsGeneral (empty bare mask);
// every emitted pair has ttype == kAttachTargetChain, so the M7 callers are unchanged.
void k8EnumeratePrefilteredPairs(const LSTEventData& ev,
                                 const Chains& chains,
                                 const std::vector<int>& acceptedChains,
                                 const ChainFeatures& cf,
                                 const std::vector<float>& chainGateLogits,
                                 const AttachParams& params,
                                 std::vector<AttachPair>& out);

// M16 GENERAL enumeration: the SAME prefilter and the SAME feature builder run over the
// union of both target universes --
//   (a) accepted chains with nLayers >= 5   (ttype 0),
//   (b) bare T3s, bareT3Mask[t] != 0        (ttype 1; pass an EMPTY mask for chains only).
// Emission order: all chain targets first (acceptedChains order), then all bare T3s in
// ascending t3 row; within a target, plsRow ascending. targetOrd counts targets in that
// same order starting at 0, so a caller can bucket pairs into per-target spans with one
// counting pass.
void k8EnumeratePrefilteredPairsGeneral(const LSTEventData& ev,
                                        const Chains& chains,
                                        const std::vector<int>& acceptedChains,
                                        const ChainFeatures& cf,
                                        const std::vector<float>& chainGateLogits,
                                        const std::vector<char>& bareT3Mask,
                                        const AttachParams& params,
                                        std::vector<AttachPair>& out);

// Diagnostic single-pair probe (pairdump prefilter-efficiency accounting, NOT the hot
// path): evaluates BOTH prefilter quantities for (chain chainIdx, pLS plsRow) even when
// the first window already fails, so the caller can attribute a missed true pair to the
// window(s) that bind. Returns true iff the pair passes the prefilter.
bool k8ProbePairWindows(const LSTEventData& ev,
                        const Chains& chains,
                        int chainIdx,
                        const AttachParams& params,
                        int plsRow,
                        float& absDTanL,
                        float& absDPhi);

// M16: the same probe for a BARE T3 target (identical windows, T3 target geometry).
bool k8ProbePairWindowsT3(const LSTEventData& ev,
                          int t3Row,
                          const AttachParams& params,
                          int plsRow,
                          float& absDTanL,
                          float& absDPhi);

#endif
