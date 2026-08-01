#include "ChainInference.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "chain_mlp_weights.h"

// Same implementation pattern as EdgeInference.cc / production src/alpaka/NeuralNetwork.h:
// fixed-size unrolled linear_layer / relu_activation templates over constexpr weight
// arrays from a generated header.
#if defined(__GNUC__) && !defined(__clang__)
#define CHAINMLP_UNROLL_LOOP _Pragma("GCC ivdep")
#else
#define CHAINMLP_UNROLL_LOOP
#endif

namespace {

template <int IN_FEATURES, int OUT_FEATURES>
inline void linear_layer(const float (&input)[IN_FEATURES],
                         float (&output)[OUT_FEATURES],
                         const float (&weights)[IN_FEATURES][OUT_FEATURES],
                         const float (&biases)[OUT_FEATURES]) {
  CHAINMLP_UNROLL_LOOP
  for (int i = 0; i < OUT_FEATURES; ++i) {
    output[i] = biases[i];
    CHAINMLP_UNROLL_LOOP
    for (int j = 0; j < IN_FEATURES; ++j) {
      output[i] += input[j] * weights[j][i];
    }
  }
}

template <int FEATURES>
inline void relu_activation(float (&input)[FEATURES]) {
  CHAINMLP_UNROLL_LOOP
  for (int col = 0; col < FEATURES; ++col) {
    input[col] = (input[col] > 0.f) ? input[col] : 0.f;
  }
}

// Per-input preprocessing baked into the generated header (order matters, see there):
// optional log10(1+x) -> clip -> standardize.
inline float preprocess(float x, int i) {
  if (chainmlp::kLog10p1[i])
    x = std::log10(1.f + x);
  x = std::min(std::max(x, chainmlp::kClipLo[i]), chainmlp::kClipHi[i]);
  return (x - chainmlp::kFeatMean[i]) / chainmlp::kFeatStd[i];
}

}  // namespace

float chainGateLogit(const float* f) {
  static_assert(chainmlp::kInput == kChainFeat,
                "chain_mlp_weights.h input size does not match ChainFeatures.h layout");

  float x[chainmlp::kInput];
  CHAINMLP_UNROLL_LOOP
  for (int i = 0; i < chainmlp::kInput; ++i)
    x[i] = preprocess(f[i], i);

  float x1[chainmlp::kHidden];
  float x2[chainmlp::kHidden];

  // Layer 1: Linear + Relu
  linear_layer<chainmlp::kInput, chainmlp::kHidden>(x, x1, chainmlp::wgt_l1, chainmlp::bias_l1);
  relu_activation<chainmlp::kHidden>(x1);

  // Layer 2: Linear + Relu
  linear_layer<chainmlp::kHidden, chainmlp::kHidden>(x1, x2, chainmlp::wgt_l2, chainmlp::bias_l2);
  relu_activation<chainmlp::kHidden>(x2);

  // Output layer: single unit, NO sigmoid -- K9 thresholds/orders on the logit.
  float logit = chainmlp::bias_out;
  CHAINMLP_UNROLL_LOOP
  for (int j = 0; j < chainmlp::kHidden; ++j)
    logit += x2[j] * chainmlp::wgt_out[j];

  return logit;
}

void runChainInference(const ChainFeatures& cf, std::vector<float>& out) {
  const std::size_t nChains = cf.f.size() / kChainFeat;
  out.assign(nChains, 0.f);
  for (std::size_t c = 0; c < nChains; ++c)
    out[c] = chainGateLogit(&cf.f[c * kChainFeat]);
}
