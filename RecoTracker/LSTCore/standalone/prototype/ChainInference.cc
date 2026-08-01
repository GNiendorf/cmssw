#include "ChainInference.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "chain_mlp_weights.h"
#include "chain3_mlp_weights.h"

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

// ---------------------------------------------------------------------------------
// ANGLE-1 3-class gate. Same pattern, separate weight namespace (chain3mlp) so the
// 2-class path above is untouched. Input vector = the kChainFeat ChainFeatures columns
// followed by the chain's transverse DCA (k8ChainDcaXY).

namespace {

inline float preprocess3(float x, int i) {
  if (chain3mlp::kLog10p1[i])
    x = std::log10(1.f + x);
  x = std::min(std::max(x, chain3mlp::kClipLo[i]), chain3mlp::kClipHi[i]);
  return (x - chain3mlp::kFeatMean[i]) / chain3mlp::kFeatStd[i];
}

}  // namespace

bool chainGate3Available() {
  // A sentinel (untrained) header exports an all-zero output layer; a trained one
  // never does.
  for (int o = 0; o < chain3mlp::kOutput; ++o) {
    if (chain3mlp::bias_out[o] != 0.f)
      return true;
    for (int j = 0; j < chain3mlp::kHidden; ++j)
      if (chain3mlp::wgt_out[j][o] != 0.f)
        return true;
  }
  return false;
}

int chainGate3NumInputs() { return chain3mlp::kInput; }

void chainGate3Logits(const float* f, float dcaXY, float* out3) {
  static_assert(chain3mlp::kOutput == 3, "chain3 gate must have 3 outputs");

  // Gather: input i reads ChainFeatures column kSrcCol[i], or the dca argument (-1).
  float x[chain3mlp::kInput];
  for (int i = 0; i < chain3mlp::kInput; ++i) {
    const int col = chain3mlp::kSrcCol[i];
    x[i] = preprocess3(col < 0 ? dcaXY : f[col], i);
  }

  float x1[chain3mlp::kHidden];
  float x2[chain3mlp::kHidden];

  linear_layer<chain3mlp::kInput, chain3mlp::kHidden>(x, x1, chain3mlp::wgt_l1, chain3mlp::bias_l1);
  relu_activation<chain3mlp::kHidden>(x1);
  linear_layer<chain3mlp::kHidden, chain3mlp::kHidden>(x1, x2, chain3mlp::wgt_l2, chain3mlp::bias_l2);
  relu_activation<chain3mlp::kHidden>(x2);
  // Output layer: 3 units, NO softmax -- K9 thresholds on logit MARGINS.
  float out[chain3mlp::kOutput];
  linear_layer<chain3mlp::kHidden, chain3mlp::kOutput>(x2, out, chain3mlp::wgt_out, chain3mlp::bias_out);
  for (int o = 0; o < chain3mlp::kOutput; ++o)
    out3[o] = out[o];
}

void runChainInference3(const ChainFeatures& cf, const std::vector<float>& dca, std::vector<float>& out3) {
  const std::size_t nChains = cf.f.size() / kChainFeat;
  out3.assign(nChains * 3, 0.f);
  for (std::size_t c = 0; c < nChains; ++c)
    chainGate3Logits(&cf.f[c * kChainFeat], c < dca.size() ? dca[c] : 0.f, &out3[c * 3]);
}
