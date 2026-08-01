#ifndef PROTOTYPE_ATTACHINFERENCE_H
#define PROTOTYPE_ATTACHINFERENCE_H

// C++ inference for the K8 attach pair head (M7), same implementation pattern as
// ChainInference.h / EdgeInference.h: weights as constexpr float arrays in a GENERATED
// header (attach_mlp_weights.h, to be emitted by an export script from the trained .pt +
// norm json -- never hand-copied), unrolled linear_layer/relu templates, preprocessing
// (log10_1p / clip / standardize) baked into the generated header.
//
// SENTINEL MODE (PixelAttach.h contract): the head does not exist until the pairdump ->
// train -> export loop has run once. AttachInference.cc keys on
// __has_include("attach_mlp_weights.h"): with the header present it compiles the real
// MLP; without it attachLogit returns 0 for every pair (score = 0 sentinel) and
// attachHeadAvailable() reports false, so k8AttachPixels and the dump run BEFORE
// training with documented, deterministic behavior.

bool attachHeadAvailable();

// Logit (log-odds, pre-sigmoid) for ONE pair from its kAttachFeat raw features
// (PixelAttach.h frozen order). Returns 0 in sentinel mode.
float attachLogit(const float* f);

#endif
