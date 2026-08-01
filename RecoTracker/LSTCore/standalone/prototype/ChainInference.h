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

#endif
