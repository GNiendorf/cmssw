#include "EdgeInference.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "edge_mlp_weights.h"

// Same implementation pattern as production src/alpaka/NeuralNetwork.h: fixed-size
// unrolled linear_layer / relu_activation templates over constexpr weight arrays from a
// generated header. CMS_UNROLL_LOOP there expands to an ivdep-style hint; standalone we
// use the plain GCC pragma (no-op elsewhere).
#if defined(__GNUC__) && !defined(__clang__)
#define EDGEMLP_UNROLL_LOOP _Pragma("GCC ivdep")
#else
#define EDGEMLP_UNROLL_LOOP
#endif

namespace {

template <int IN_FEATURES, int OUT_FEATURES>
inline void linear_layer(const float (&input)[IN_FEATURES],
                         float (&output)[OUT_FEATURES],
                         const float (&weights)[IN_FEATURES][OUT_FEATURES],
                         const float (&biases)[OUT_FEATURES]) {
  EDGEMLP_UNROLL_LOOP
  for (int i = 0; i < OUT_FEATURES; ++i) {
    output[i] = biases[i];
    EDGEMLP_UNROLL_LOOP
    for (int j = 0; j < IN_FEATURES; ++j) {
      output[i] += input[j] * weights[j][i];
    }
  }
}

template <int FEATURES>
inline void relu_activation(float (&input)[FEATURES]) {
  EDGEMLP_UNROLL_LOOP
  for (int col = 0; col < FEATURES; ++col) {
    input[col] = (input[col] > 0.f) ? input[col] : 0.f;
  }
}

// Per-input preprocessing baked into the generated header (order matters, see there):
// optional log10(1+x) -> clip -> standardize. All conditioning is a no-op for v1.
inline float preprocess(float x, int i) {
  if (edgemlp::kLog10p1[i])
    x = std::log10(1.f + x);
  x = std::min(std::max(x, edgemlp::kClipLo[i]), edgemlp::kClipHi[i]);
  return (x - edgemlp::kFeatMean[i]) / edgemlp::kFeatStd[i];
}

}  // namespace

void runEdgeInference(const ChainGraph& g,
                      const NodeFeatures& nf,
                      const EdgeFeatures& ef,
                      EdgeScores& out) {
  static_assert(edgemlp::kInput == 2 * kNodeFeat + kEdgeFeat,
                "edge_mlp_weights.h input size does not match Features.h layout");

  const std::size_t nEdges = g.edges.size();
  out.logOdds.assign(nEdges, 0.f);

  for (std::size_t e = 0; e < nEdges; ++e) {
    const ChainGraph::Edge& edge = g.edges[e];
    const float* ni = &nf.f[static_cast<std::size_t>(edge.inner) * kNodeFeat];
    const float* no = &nf.f[static_cast<std::size_t>(edge.outer) * kNodeFeat];
    const float* ee = &ef.f[e * kEdgeFeat];

    // Assemble in the frozen training order: ni_00..ni_12, no_00..no_12, ef_00..ef_13.
    float x[edgemlp::kInput];
    EDGEMLP_UNROLL_LOOP
    for (int i = 0; i < kNodeFeat; ++i) {
      x[i] = ni[i];
      x[kNodeFeat + i] = no[i];
    }
    EDGEMLP_UNROLL_LOOP
    for (int i = 0; i < kEdgeFeat; ++i)
      x[2 * kNodeFeat + i] = ee[i];

    EDGEMLP_UNROLL_LOOP
    for (int i = 0; i < edgemlp::kInput; ++i)
      x[i] = preprocess(x[i], i);

    float x1[edgemlp::kHidden];
    float x2[edgemlp::kHidden];

    // Layer 1: Linear + Relu
    linear_layer<edgemlp::kInput, edgemlp::kHidden>(x, x1, edgemlp::wgt_l1, edgemlp::bias_l1);
    relu_activation<edgemlp::kHidden>(x1);

    // Layer 2: Linear + Relu
    linear_layer<edgemlp::kHidden, edgemlp::kHidden>(x1, x2, edgemlp::wgt_l2, edgemlp::bias_l2);
    relu_activation<edgemlp::kHidden>(x2);

    // Output layer: single unit, NO sigmoid — K6 needs the logit (log-odds).
    float logit = edgemlp::bias_out;
    EDGEMLP_UNROLL_LOOP
    for (int j = 0; j < edgemlp::kHidden; ++j)
      logit += x2[j] * edgemlp::wgt_out[j];

    out.logOdds[e] = logit;
  }
}
