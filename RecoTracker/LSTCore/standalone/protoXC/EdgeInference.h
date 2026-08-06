#ifndef PROTOTYPE_EDGEINFERENCE_H
#define PROTOTYPE_EDGEINFERENCE_H

// C++ inference for the edge classifier, using the SAME implementation pattern as
// production src/alpaka/NeuralNetwork.h (maintainer requirement, plan 5c Export):
// weights as constexpr const float arrays in a GENERATED header
// (edge_mlp_weights.h, emitted by export_weights.py from the .pt + norm json — never
// hand-copied), fixed-size unrolled linear_layer/relu template functions.
// Feature standardization AND the v2 clipping spec are baked into the generated header
// (clip -> (x - mean) / std) so python and C++ scores agree to float precision.
//
// Returns the LOGIT (pre-sigmoid) = log-odds, the quantity K6 sums.

#include "Features.h"
#include "Stages.h"

// Fills out.logOdds for every edge from the already-computed features.
void runEdgeInference(const ChainGraph& g, const NodeFeatures& nf, const EdgeFeatures& ef, EdgeScores& out);

#endif
