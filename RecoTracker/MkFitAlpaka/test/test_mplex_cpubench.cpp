// CPU micro-benchmark: portable Matriplex (mkfitdev, N = 8) vs STOCK Matriplex (vendored copy, N = 8), same TU,
// same compiler flags. Built twice (test/BuildFile.xml): with the CMSSW default flags of an Alpaka serial build, and
// with the flags of stock RecoTracker/MkFitCore (ofast-flag + -fopenmp-simd). Data stays in plexes (2048 tracks =
// 256 plexes), each op is applied in place many times; the time per track and op is printed.

#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include "alpaka/mplex_stock/Matrix.h"  // verbatim copy of RecoTracker/MkFitCore/src/Matrix.h

#include "RecoTracker/MkFitAlpaka/interface/matriplex/Matrix.h"
#include "RecoTracker/MkFitAlpaka/interface/matriplex/MatriplexOps.h"

namespace {
  constexpr int kN = 8;
  constexpr int kTracks = 2048;
  constexpr int kPlex = kTracks / kN;

  template <typename MP>
  void fill(std::vector<MP>& v, std::mt19937& rng, float lo, float hi, float diag = 0.f) {
    std::uniform_real_distribution<float> d(lo, hi);
    for (auto& m : v)
      for (int i = 0; i < MP::kTotSize; ++i)
        m.fArray[i] = d(rng);
    if (diag != 0.f)  // make symmetric matrices diagonally dominant (positive definite)
      for (auto& m : v)
        for (int n = 0; n < kN; ++n)
          for (int i = 0; i < MP::kRows; ++i)
            m(n, i, i) = diag + std::abs(m(n, i, i));
  }

  template <typename F>
  double timeIt(F&& f, int nRep) {
    f();
    auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < nRep; ++r)
      f();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / nRep / kTracks;
  }

  // Same generated similarity for both flavours (stock has no generic one): only the plex classes differ.
  template <typename LL, typename LS>
  inline void simil(const LL& A, const LS& B, LS& C) {
    constexpr int N = kN;
    LL T;
    {
      const float* a = A.fArray;
      const float* b = B.fArray;
      float* c = T.fArray;
#include "RecoTracker/MkFitAlpaka/interface/matriplex/SimilarityLL.ah"
    }
    {
      const float* a = A.fArray;
      const float* b = T.fArray;
      float* c = C.fArray;
#include "RecoTracker/MkFitAlpaka/interface/matriplex/SimilarityLLTransp.ah"
    }
  }

  float sink = 0;

  template <typename LL, typename LS, typename HS, typename QF>
  void bench(const char* tag) {
    std::mt19937 rng(42);
    std::vector<LL> jl(kPlex), ml(kPlex), ol(kPlex);
    std::vector<LS> sa(kPlex), sb(kPlex);
    std::vector<HS> h(kPlex), h0(kPlex);
    std::vector<QF> x(kPlex), y(kPlex), s(kPlex), c(kPlex);
    fill(jl, rng, -0.3f, 0.3f);
    fill(ml, rng, -0.3f, 0.3f);
    fill(sa, rng, -0.1f, 0.1f, 1.f);
    fill(h0, rng, -0.1f, 0.1f, 1.f);
    fill(x, rng, -3.f, 3.f);
    fill(y, rng, -3.f, 3.f);
    const int nRep = 2000;

    const double tSim = timeIt(
        [&] {
          for (int p = 0; p < kPlex; ++p)
            simil(jl[p], sa[p], sb[p]);
        },
        nRep);
    // as the Alpaka serial benchmark kernel: 16 dependent similarities per plex (S <- A S A^T)
    const double tSimChain = timeIt(
                                 [&] {
                                   for (int p = 0; p < kPlex; ++p) {
                                     LS b = sa[p];
                                     for (int it = 0; it < 16; ++it) {
                                       simil(jl[p], b, sb[p]);
                                       b = sb[p];
                                     }
                                   }
                                 },
                                 nRep / 16) /
                             16;
    const double tMul = timeIt(
        [&] {
          for (int p = 0; p < kPlex; ++p)
            multiply(jl[p], ml[p], ol[p]);
        },
        nRep);
    const double tSym = timeIt(
        [&] {
          for (int p = 0; p < kPlex; ++p)
            multiply(sa[p], sa[p], ol[p]);
        },
        nRep);
    const double tInv = timeIt(
        [&] {
          for (int p = 0; p < kPlex; ++p) {
            h[p] = h0[p];
            invertCramerSym(h[p]);
          }
        },
        nRep);
    const double tUL = timeIt(
        [&] {
          for (int p = 0; p < kPlex; ++p) {
            sb[p] = sa[p];
            sb[p].invertUpperLeft3x3();
          }
        },
        nRep);
    const double tSC = timeIt(
        [&] {
          for (int p = 0; p < kPlex; ++p)
            fast_sincos(x[p], s[p], c[p]);
        },
        nRep);
    const double tAt = timeIt(
        [&] {
          for (int p = 0; p < kPlex; ++p)
            s[p] = fast_atan2(y[p], x[p]);
        },
        nRep);
    for (int p = 0; p < kPlex; ++p)
      sink += sb[p].fArray[3] + ol[p].fArray[5] + h[p].fArray[2] + s[p].fArray[1] + c[p].fArray[0];
    std::printf(
        "  %-6s ns/track: similarity66 %6.3f (chained x16 %6.3f)  mul66 %6.3f  symMul66 %6.3f  invCramerSym33 %6.3f  "
        "invUL3x3 %6.3f  "
        "fast_sincos %6.3f  fast_atan2 %6.3f\n",
        tag,
        tSim,
        tSimChain,
        tMul,
        tSym,
        tInv,
        tUL,
        tSC,
        tAt);
  }
}  // namespace

int main() {
#ifdef MPLEX_CPUBENCH_OFAST
  std::printf("CPU micro-benchmark, flags of stock MkFitCore (ofast-flag, -fopenmp-simd), N=8, %d tracks\n", kTracks);
#else
  std::printf("CPU micro-benchmark, CMSSW default flags (as an Alpaka serial build), N=8, %d tracks\n", kTracks);
#endif
  for (int round = 0; round < 2; ++round) {
    bench<Matriplex::Matriplex<float, 6, 6, kN>,
          Matriplex::MatriplexSym<float, 6, kN>,
          Matriplex::MatriplexSym<float, 3, kN>,
          Matriplex::Matriplex<float, 1, 1, kN>>("stock");
    bench<mkfitdev::MPlexLL<kN>, mkfitdev::MPlexLS<kN>, mkfitdev::MPlexHS<kN>, mkfitdev::MPlexQF<kN>>("port");
  }
  return sink == 12345.f ? 1 : 0;
}
