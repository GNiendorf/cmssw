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
  int chainPos = -1;  // position in the acceptedChains vector passed in
  int plsRow = -1;    // row into ev.pLS_*
  float f[kAttachFeat] = {};
};

// Enumerates every (accepted chain with nLayers >= 5, pLS) pair passing the analytic
// prefilter (charge compatibility NOT required -- sign flips exist; |dTanLambda| <
// params.prefDTanL and |dPhiAtInnermost| < params.prefDPhi), with all kAttachFeat
// features computed and NaN/Inf-guarded. Pairs are emitted grouped by chainPos
// ascending, plsRow ascending within a chain (deterministic).
void k8EnumeratePrefilteredPairs(const LSTEventData& ev,
                                 const Chains& chains,
                                 const std::vector<int>& acceptedChains,
                                 const ChainFeatures& cf,
                                 const std::vector<float>& chainGateLogits,
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

#endif
