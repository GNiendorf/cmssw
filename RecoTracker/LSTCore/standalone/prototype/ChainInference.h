#ifndef PROTOTYPE_CHAININFERENCE_H
#define PROTOTYPE_CHAININFERENCE_H

// C++ inference for the chain-gate classifier (M6, plan 5a hard gate), using the SAME
// implementation pattern as EdgeInference.h / production src/alpaka/NeuralNetwork.h:
// weights as constexpr const float arrays in a GENERATED header (chain_mlp_weights.h,
// emitted by export_chain_weights.py from the .pt + norm json -- never hand-copied),
// fixed-size unrolled linear_layer/relu template functions. Feature conditioning
// (log10_1p / clip) and standardization are baked into the generated header so python
// and C++ scores agree to float precision.
//
// Returns the LOGIT (pre-sigmoid) = log-odds. In hybrid mode with the gate on (-G 1)
// this logit REPLACES the chain's K6 sum-logit score: K9 both thresholds
// (thetaChain4/5/6) and orders arbitration on this scale.

#include <vector>

#include "ChainFeatures.h"

// Logit for ONE chain from its kChainFeat raw features (ChainFeatures.h order).
float chainGateLogit(const float* f);

// Fills out[c] = chainGateLogit(chain c) for every chain in cf.
void runChainInference(const ChainFeatures& cf, std::vector<float>& out);

// ---------------------------------------------------------------------------------
// ANGLE-1 3-CLASS chain gate (chain3_mlp_weights.h, namespace chain3mlp; same
// implementation pattern, separate generated header so the 2-class path -- and every
// -G 0..5 config -- stays byte-identical).
//
// Inputs: a SUBSET of the ChainFeatures columns plus the chain's transverse DCA to the
// origin (k8ChainDcaXY -- the SAME function the -X score split uses, and the same
// quantity dumped as the training `dcaXY` branch, so train/infer are identical by
// construction). Which columns, and in which order, is baked into the generated header
// as chain3mlp::kSrcCol (-1 = the dca argument), so the model may drop columns (M12
// drops maxBridgeChi2) without touching this code.
//
// Outputs: THREE raw softmax logits (no softmax applied):
//   out3[0] fake, out3[1] prompt-true (simVxy < 1 cm), out3[2] displaced-true.
// Decisions use margins (softmax is monotone in them, so these are log-odds ratios):
//   mP = out3[1] - out3[0]   IP-compatible branch discriminator
//   mD = out3[2] - out3[0]   exempt / large-DCA branch discriminator
//   mX = max(out3[1], out3[2]) - out3[0]   T4-class discriminator

// True iff a trained 3-class header is compiled in (a sentinel header exports zeros).
bool chainGate3Available();

// Raw 3 logits for ONE chain from its full kChainFeat feature row + its dcaXY.
void chainGate3Logits(const float* f, float dcaXY, float* out3);

// Number of network inputs (== chain3mlp::kInput); exposed for the parity tool.
int chainGate3NumInputs();

// Fills out3[3*c + k] for every chain in cf; dca[c] = k8ChainDcaXY(chain c).
void runChainInference3(const ChainFeatures& cf, const std::vector<float>& dca, std::vector<float>& out3);

#endif
