// P2.6b microbenchmark: which formulation of the 40->32->32->1 edge head (and the 19->24->24->1
// attach head) is fastest under the standalone flags, at bit-identical per-row accumulation order.
// Build with the same flags the device library uses:
//   g++ -march=native -mtune=native -Ofast -fno-reciprocal-math -fopenmp-simd -std=c++20
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <random>
#include <vector>

#define UNROLL _Pragma("GCC ivdep")

template <int IN, int OUT>
static inline void linear_scalar(float const (&in)[IN], float (&out)[OUT],
                                 float const (&w)[IN][OUT], float const (&b)[OUT]) {
  UNROLL
  for (int i = 0; i < OUT; ++i) {
    out[i] = b[i];
    UNROLL
    for (int j = 0; j < IN; ++j)
      out[i] += in[j] * w[j][i];
  }
}

template <int N>
static inline void relu_scalar(float (&x)[N]) {
  UNROLL
  for (int i = 0; i < N; ++i)
    x[i] = (x[i] > 0.f) ? x[i] : 0.f;
}

// ---- variant B: blocked, plain loops (what P2.6b built first) --------------------------------
template <int IN, int OUT, int B>
static inline void linear_blk(float const* __restrict__ inT, float* __restrict__ outT,
                              float const (&w)[IN][OUT], float const (&bi)[OUT]) {
  constexpr int kBlk = 8;
  for (int i0 = 0; i0 < OUT; i0 += kBlk) {
    float acc[kBlk][B];
    UNROLL
    for (int c = 0; c < kBlk; ++c)
      for (int b = 0; b < B; ++b)
        acc[c][b] = bi[i0 + c];
    for (int j = 0; j < IN; ++j) {
      float const* __restrict__ in = inT + j * B;
      UNROLL
      for (int c = 0; c < kBlk; ++c) {
        float const ww = w[j][i0 + c];
        for (int b = 0; b < B; ++b)
          acc[c][b] += in[b] * ww;
      }
    }
    UNROLL
    for (int c = 0; c < kBlk; ++c)
      for (int b = 0; b < B; ++b)
        outT[(i0 + c) * B + b] = acc[c][b];
  }
}

// ---- variant C: blocked, omp simd on the lane loop --------------------------------------------
template <int IN, int OUT, int B>
static inline void linear_omp(float const* __restrict__ inT, float* __restrict__ outT,
                              float const (&w)[IN][OUT], float const (&bi)[OUT]) {
  constexpr int kBlk = 8;
  for (int i0 = 0; i0 < OUT; i0 += kBlk) {
    float acc[kBlk][B];
    for (int c = 0; c < kBlk; ++c)
#pragma omp simd
      for (int b = 0; b < B; ++b)
        acc[c][b] = bi[i0 + c];
    for (int j = 0; j < IN; ++j) {
      float const* __restrict__ in = inT + j * B;
      for (int c = 0; c < kBlk; ++c) {
        float const ww = w[j][i0 + c];
#pragma omp simd
        for (int b = 0; b < B; ++b)
          acc[c][b] += in[b] * ww;
      }
    }
    for (int c = 0; c < kBlk; ++c)
#pragma omp simd
      for (int b = 0; b < B; ++b)
        outT[(i0 + c) * B + b] = acc[c][b];
  }
}

// ---- variant D: blocked, explicit GCC vector type ---------------------------------------------
template <int B>
struct Vec {
  typedef float T __attribute__((vector_size(B * sizeof(float))));
};

template <int IN, int OUT, int B>
static inline void linear_vec(float const* __restrict__ inT, float* __restrict__ outT,
                              float const (&w)[IN][OUT], float const (&bi)[OUT]) {
  using V = typename Vec<B>::T;
  constexpr int kBlk = 8;
  V const* vin = reinterpret_cast<V const*>(inT);
  V* vout = reinterpret_cast<V*>(outT);
  for (int i0 = 0; i0 < OUT; i0 += kBlk) {
    V acc[kBlk];
    UNROLL
    for (int c = 0; c < kBlk; ++c)
      acc[c] = bi[i0 + c] - V{};
    for (int j = 0; j < IN; ++j) {
      V const in = vin[j];
      UNROLL
      for (int c = 0; c < kBlk; ++c)
        acc[c] += in * (w[j][i0 + c] - V{});
    }
    UNROLL
    for (int c = 0; c < kBlk; ++c)
      vout[i0 + c] = acc[c];
  }
}

template <int N, int B>
static inline void relu_vec(float* __restrict__ t) {
  using V = typename Vec<B>::T;
  V* v = reinterpret_cast<V*>(t);
  V const zero = V{};
  for (int i = 0; i < N; ++i)
    v[i] = (v[i] > zero) ? v[i] : zero;
}

template <int N, int B>
static inline void dot_vec(float const* __restrict__ inT, float* __restrict__ out,
                           float const (&w)[N], float bias) {
  using V = typename Vec<B>::T;
  V const* vin = reinterpret_cast<V const*>(inT);
  V acc = bias - V{};
  UNROLL
  for (int j = 0; j < N; ++j)
    acc += vin[j] * (w[j] - V{});
  *reinterpret_cast<V*>(out) = acc;
}

template <int N, int B>
static inline void relu_blk(float* __restrict__ t) {
  UNROLL
  for (int i = 0; i < N * B; ++i)
    t[i] = (t[i] > 0.f) ? t[i] : 0.f;
}

template <int N, int B>
static inline void dot_blk(float const* __restrict__ inT, float* __restrict__ out,
                           float const (&w)[N], float bias) {
  float acc[B];
  for (int b = 0; b < B; ++b)
    acc[b] = bias;
  UNROLL
  for (int j = 0; j < N; ++j) {
    float const ww = w[j];
    for (int b = 0; b < B; ++b)
      acc[b] += inT[j * B + b] * ww;
  }
  for (int b = 0; b < B; ++b)
    out[b] = acc[b];
}

// ---------------------------------------------------------------------------------------------
#ifndef IN_F
#define IN_F 40
#endif
#ifndef H_F
#define H_F 32
#endif
constexpr int IN = IN_F, H = H_F;
alignas(64) float W1[IN][H], W2[H][H], WO[H], B1[H], B2[H];
float BO;

int NROWS = 75000;

static double bench_scalar(std::vector<float> const& rows, std::vector<float>& out) {
  auto t0 = std::chrono::steady_clock::now();
  for (int r = 0; r < NROWS; ++r) {
    float x[IN];
    for (int i = 0; i < IN; ++i)
      x[i] = rows[size_t(r) * IN + i];
    float h1[H], h2[H];
    linear_scalar<IN, H>(x, h1, W1, B1);
    relu_scalar<H>(h1);
    linear_scalar<H, H>(h1, h2, W2, B2);
    relu_scalar<H>(h2);
    float lo = BO;
    UNROLL
    for (int j = 0; j < H; ++j)
      lo += h2[j] * WO[j];
    out[r] = lo;
  }
  auto t1 = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

template <int B, int VARIANT>
static double bench_batch(std::vector<float> const& rows, std::vector<float>& out) {
  auto t0 = std::chrono::steady_clock::now();
  alignas(64) float xT[IN * B];
  alignas(64) float h1[H * B];
  alignas(64) float h2[H * B];
  alignas(64) float lo[B];
  for (int r0 = 0; r0 < NROWS; r0 += B) {
    for (int b = 0; b < B; ++b)
      for (int i = 0; i < IN; ++i)
        xT[i * B + b] = rows[size_t(r0 + b) * IN + i];
    if constexpr (VARIANT == 0) {
      linear_blk<IN, H, B>(xT, h1, W1, B1);
      relu_blk<H, B>(h1);
      linear_blk<H, H, B>(h1, h2, W2, B2);
      relu_blk<H, B>(h2);
      dot_blk<H, B>(h2, lo, WO, BO);
    } else if constexpr (VARIANT == 1) {
      linear_omp<IN, H, B>(xT, h1, W1, B1);
      relu_blk<H, B>(h1);
      linear_omp<H, H, B>(h1, h2, W2, B2);
      relu_blk<H, B>(h2);
      dot_blk<H, B>(h2, lo, WO, BO);
    } else {
      linear_vec<IN, H, B>(xT, h1, W1, B1);
      relu_vec<H, B>(h1);
      linear_vec<H, H, B>(h1, h2, W2, B2);
      relu_vec<H, B>(h2);
      dot_vec<H, B>(h2, lo, WO, BO);
    }
    for (int b = 0; b < B; ++b)
      out[r0 + b] = lo[b];
  }
  auto t1 = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main(int argc, char** argv) {
  if (argc > 1)
    NROWS = atoi(argv[1]);
  NROWS = (NROWS / 64) * 64;
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> d(-1.f, 1.f);
  for (int j = 0; j < IN; ++j)
    for (int i = 0; i < H; ++i)
      W1[j][i] = d(rng);
  for (int j = 0; j < H; ++j)
    for (int i = 0; i < H; ++i)
      W2[j][i] = d(rng);
  for (int i = 0; i < H; ++i) {
    WO[i] = d(rng);
    B1[i] = d(rng);
    B2[i] = d(rng);
  }
  BO = d(rng);
  std::vector<float> rows(size_t(NROWS) * IN);
  for (auto& v : rows)
    v = d(rng);

  std::vector<float> sref(NROWS), ref(NROWS), o(NROWS);
  double best = 1e30;
  for (int k = 0; k < 5; ++k)
    best = std::min(best, bench_scalar(rows, sref));
  printf("scalar (per-row linear_layer)  : %8.2f ms / %d rows\n", best, NROWS);

  // The BATCHED reference: plain blocked B=16, the formulation whose bit-identity against the
  // production baseline was already proven by the P2.6b gate (a) run. Every other batched variant
  // must reproduce it exactly; the scalar column here is a separate question (this benchmark's own
  // linear_scalar is free to be reassociated by the compiler, the production one is not).
  for (int k = 0; k < 3; ++k)
    bench_batch<16, 0>(rows, ref);

  auto check = [&](char const* name, double ms) {
    int bad = 0, bads = 0;
    for (int r = 0; r < NROWS; ++r) {
      if (memcmp(&ref[r], &o[r], 4) != 0)
        ++bad;
      if (memcmp(&sref[r], &o[r], 4) != 0)
        ++bads;
    }
    printf("%-30s : %8.2f ms   vs blockedB16=%d   vs scalar=%d\n", name, ms, bad, bads);
  };

  best = 1e30;
  for (int k = 0; k < 5; ++k)
    best = std::min(best, bench_batch<8, 0>(rows, o));
  check("blocked B=8", best);
  best = 1e30;
  for (int k = 0; k < 5; ++k)
    best = std::min(best, bench_batch<16, 0>(rows, o));
  check("blocked B=16", best);
  best = 1e30;
  for (int k = 0; k < 5; ++k)
    best = std::min(best, bench_batch<16, 1>(rows, o));
  check("blocked+ompsimd B=16", best);
  best = 1e30;
  for (int k = 0; k < 5; ++k)
    best = std::min(best, bench_batch<8, 2>(rows, o));
  check("vector-type B=8", best);
  best = 1e30;
  for (int k = 0; k < 5; ++k)
    best = std::min(best, bench_batch<16, 2>(rows, o));
  check("vector-type B=16", best);
  best = 1e30;
  for (int k = 0; k < 5; ++k)
    best = std::min(best, bench_batch<32, 2>(rows, o));
  check("vector-type B=32", best);
  return 0;
}
