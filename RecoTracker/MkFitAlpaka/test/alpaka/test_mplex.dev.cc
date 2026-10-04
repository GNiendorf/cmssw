// mplex unit test: every ported Matriplex / math op on random inputs of realistic magnitude (2048 tracks per op),
// run in Alpaka kernels on the backend of this binary (lane width kNN: 8 on CPU backends, 1 on GPU backends) and
// on the host with the port at N = 1, compared element by element with STOCK mkFit Matriplex/vdt (N = 8, host).
// Then a small benchmark: 6x6 similarity A * S * A^T on 2048 tracks (and on 256k tracks).
// Two builds (test/BuildFile.xml):
//   testMkFitAlpakaMplex<Backend>        CMSSW default flags (gcc contracts a*b+c into FMA, nvcc -fmad=true): the
//                                        realistic difference distribution; fails only on gross differences
//                                        (|diff| > 1e-2 x largest |element| of the matrix), i.e. a transliteration bug.
//   testMkFitAlpakaMplexStrict<Backend>  -ffp-contract=off and nvcc -fmad=false (MPLEX_TEST_STRICT): no FMA contraction
//                                        anywhere, so port (any N, any backend) and stock must agree bit by bit
//                                        (std:: sin/cos/tan/atan2 on a GPU excepted: device libm != host libm).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <type_traits>
#include <utility>
#include <vector>

#include <alpaka/alpaka.hpp>

#include "FWCore/Utilities/interface/stringize.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "RecoTracker/MkFitAlpaka/interface/matriplex/MatriplexBackend.h"
#include "RecoTracker/MkFitAlpaka/interface/matriplex/MatriplexOps.h"

#include "test_mplex_ops.h"

namespace mplextest {
  // test_mplex_stockref.cc (host compiler, stock mkFit)
  void stockRef(int op, const float* in, float* out, int nTracks);
  int stockNN();
  int checkSTypesLayout();

  struct PortTraits {
    template <int N>
    using LL = mkfitdev::MPlexLL<N>;
    template <int N>
    using LV = mkfitdev::MPlexLV<N>;
    template <int N>
    using LS = mkfitdev::MPlexLS<N>;
    template <int N>
    using HH = mkfitdev::MPlexHH<N>;
    template <int N>
    using HS = mkfitdev::MPlexHS<N>;
    template <int N>
    using LH = mkfitdev::MPlexLH<N>;
    template <int N>
    using HL = mkfitdev::MPlexHL<N>;
    template <int N>
    using M22 = mkfitdev::MPlex22<N>;
    template <int N>
    using S22 = mkfitdev::MPlex2S<N>;
    template <int N>
    using QF = mkfitdev::MPlexQF<N>;
    using SSym66 = mkfitdev::SMatrixSym66;

    template <int N>
    ALPAKA_FN_HOST_ACC static void similarity(const LL<N>& a, const LS<N>& b, LS<N>& c) {
      mkfitdev::similarityLL(a, b, c);
    }

    ALPAKA_FN_HOST_ACC static void sincos4f(float x, float& s, float& c) { mkfitdev::sincos4(x, s, c); }
    ALPAKA_FN_HOST_ACC static float fast_logf(float x) { return mkfitdev::vdt::fast_logf(x); }
    ALPAKA_FN_HOST_ACC static float fast_atanf(float x) { return mkfitdev::vdt::fast_atanf(x); }
    ALPAKA_FN_HOST_ACC static float fast_isqrtf(float x) { return mkfitdev::vdt::fast_isqrtf(x); }
    ALPAKA_FN_HOST_ACC static float getEta(float r, float z) { return mkfitdev::getEta(r, z); }
    ALPAKA_FN_HOST_ACC static float getEta3(float x, float y, float z) { return mkfitdev::getEta(x, y, z); }
    ALPAKA_FN_HOST_ACC static float getPhi(float x, float y) { return mkfitdev::getPhi(x, y); }
    ALPAKA_FN_HOST_ACC static float squashPhiGeneral(float p) { return mkfitdev::squashPhiGeneral(p); }
    ALPAKA_FN_HOST_ACC static float squashPhiMinimal(float p) { return mkfitdev::squashPhiMinimal(p); }
    ALPAKA_FN_HOST_ACC static float bFieldFromZR(float z, float r) { return mkfitdev::Config::bFieldFromZR(z, r); }
    ALPAKA_FN_HOST_ACC static float hipo(float x, float y) { return mkfitdev::hipo(x, y); }
  };

  //--------------------------------------------------------------------------------------------------------------
  // Inputs

  struct Gen {
    std::mt19937 rng;
    explicit Gen(unsigned seed) : rng(seed) {}
    float u(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }
    float logu(float a, float b) { return std::exp(u(std::log(a), std::log(b))); }

    // symmetric positive definite D x D, packed lower triangle (stock MatriplexSym order), std devs s[i]
    void cov(int d, const float* s, float* out) {
      float l[6][6] = {};
      for (int i = 0; i < d; ++i)
        for (int j = 0; j <= i; ++j)
          l[i][j] = (i == j) ? u(0.5f, 1.5f) : u(-0.6f, 0.6f);
      for (int i = 0; i < d; ++i)
        for (int j = 0; j <= i; ++j) {
          double c = 0;
          for (int k = 0; k <= j; ++k)
            c += double(l[i][k]) * l[j][k];
          out[i * (i + 1) / 2 + j] = float(c * s[i] * s[j]);
        }
    }
    void covFull(int d, const float* s, float* out) {
      float p[21];
      cov(d, s, p);
      for (int i = 0; i < d; ++i)
        for (int j = 0; j < d; ++j)
          out[i * d + j] = p[i >= j ? i * (i + 1) / 2 + j : j * (j + 1) / 2 + i];
    }
    // propagation-Jacobian-like: 1 + small on the diagonal, mixed magnitudes off it
    void jac(int d1, int d2, float* out) {
      for (int i = 0; i < d1; ++i)
        for (int j = 0; j < d2; ++j)
          out[i * d2 + j] = (i == j) ? 1.f + u(-0.1f, 0.1f) : u(-1.f, 1.f) * std::pow(10.f, u(-2.f, 1.f));
    }
    void general(int d1, int d2, float* out, float diag) {
      for (int i = 0; i < d1; ++i)
        for (int j = 0; j < d2; ++j)
          out[i * d2 + j] = u(-1.f, 1.f) + (i == j ? diag : 0.f);
    }
  };

  // std devs: track (x, y, z [cm], 1/pt, phi, theta), hit (x, y, z), local 2D plane hit
  constexpr float kTrkS[6] = {0.03f, 0.03f, 0.1f, 0.01f, 0.002f, 0.002f};
  constexpr float kHitS[3] = {0.002f, 0.002f, 0.1f};
  constexpr float kLocS[2] = {0.003f, 0.5f};

  std::vector<float> makeInputs(int op, int nt) {
    Gen g(12345u + 7919u * op);
    const int ir = inRec(op);
    std::vector<float> v(size_t(nt) * ir, 0.f);
    for (int t = 0; t < nt; ++t) {
      float* p = v.data() + size_t(t) * ir;
      switch (op) {
        case kMul66:
          g.jac(6, 6, p);
          g.covFull(6, kTrkS, p + 36);
          break;
        case kMul33:
          g.general(3, 3, p, 0.f);
          g.covFull(3, kHitS, p + 9);
          break;
        case kMulGen63x36:
          g.general(6, 3, p, 0.f);
          g.general(3, 6, p + 18, 0.f);
          break;
        case kMulGen66x61:
          g.jac(6, 6, p);
          p[36] = g.u(-100.f, 100.f);
          p[37] = g.u(-100.f, 100.f);
          p[38] = g.u(-200.f, 200.f);
          p[39] = g.u(0.05f, 2.f);
          p[40] = g.u(-3.14f, 3.14f);
          p[41] = g.u(0.1f, 3.f);
          break;
        case kSymMul66:
          g.cov(6, kTrkS, p);
          g.cov(6, kTrkS, p + 21);
          break;
        case kSymMul33:
          g.cov(3, kHitS, p);
          g.cov(3, kTrkS, p + 6);
          break;
        case kInvCramerSym33:
        case kInvCholSym33:
          g.cov(3, kHitS, p);
          break;
        case kInvCramerSym22:
          g.cov(2, kLocS, p);
          break;
        case kInvCramer33:
          g.general(3, 3, p, 2.f);
          break;
        case kInvCramer22:
          g.general(2, 2, p, 2.f);
          break;
        case kInvChol33:
          g.covFull(3, kHitS, p);
          break;
        case kInvUL3x3:
          g.cov(6, kTrkS, p);
          break;
        case kSimilarity66:
          g.jac(6, 6, p);
          g.cov(6, kTrkS, p + 36);
          break;
        case kFastSinCos:
          p[0] = g.u(-4.f, 4.f);
          break;
        case kFastSinCosTan:
          p[0] = g.u(0.02f, 3.12f);
          break;
        case kFastAtan2:
          p[0] = g.u(-120.f, 120.f);
          p[1] = g.u(-120.f, 120.f);
          break;
        case kFastIsqrt:
          p[0] = g.logu(1e-6f, 1e6f);
          break;
        case kSinCos4:
          p[0] = g.u(-0.5f, 0.5f);
          break;
        case kElementwise:
          p[0] = g.u(-100.f, 100.f);
          p[1] = g.u(-100.f, 100.f);
          break;
        case kStdMath:
          p[0] = g.u(-4.f, 4.f);
          p[1] = g.u(-4.f, 4.f);
          break;
        case kDataMove:
          g.cov(6, kTrkS, p);
          g.jac(6, 6, p + 21);
          break;
        case kScalarMath:
          p[0] = g.u(-120.f, 120.f);
          p[1] = g.u(-120.f, 120.f);
          p[2] = g.u(-280.f, 280.f);
          p[3] = g.logu(1e-4f, 10.f);
          break;
        default:
          break;
      }
    }
    return v;
  }

  //--------------------------------------------------------------------------------------------------------------
  // Comparison

  inline int64_t orderedBits(float f) {
    int32_t i;
    std::memcpy(&i, &f, 4);
    return i < 0 ? int64_t(int32_t(0x80000000)) - int64_t(i) : int64_t(i);
  }

  struct Cmp {
    long n = 0, nEq = 0, nNaNMismatch = 0;
    long hist[6] = {};  // ulp: 0, 1, 2-3, 4-15, 16-255, >=256
    int64_t maxUlp = 0;
    double maxRel = 0;      // element-wise relative difference (large where an element cancels)
    double maxNormRel = 0;  // difference relative to the largest |element| of the same output record (matrix)
    void add(float a, float b, double scale) {
      ++n;
      if (std::isnan(a) || std::isnan(b)) {
        if (std::isnan(a) && std::isnan(b)) {
          ++nEq;
          ++hist[0];
        } else
          ++nNaNMismatch;
        return;
      }
      const int64_t d = std::llabs(orderedBits(a) - orderedBits(b));
      if (d == 0)
        ++nEq;
      hist[d == 0 ? 0 : d == 1 ? 1 : d < 4 ? 2 : d < 16 ? 3 : d < 256 ? 4 : 5]++;
      maxUlp = std::max(maxUlp, d);
      const double den = std::max(std::abs(double(a)), std::abs(double(b)));
      if (den > 0)
        maxRel = std::max(maxRel, std::abs(double(a) - double(b)) / den);
      if (scale > 0)
        maxNormRel = std::max(maxNormRel, std::abs(double(a) - double(b)) / scale);
    }
    void print(const char* tag, const char* name) const {
      std::printf(
          "  %-6s %-15s n=%7ld  bitEq=%7.3f%%  ulp:0=%ld 1=%ld 2-3=%ld 4-15=%ld 16-255=%ld >=256=%ld  maxUlp=%lld  "
          "maxRel=%.2e  maxNormRel=%.2e%s\n",
          tag,
          name,
          n,
          n ? 100.0 * nEq / n : 0.,
          hist[0],
          hist[1],
          hist[2],
          hist[3],
          hist[4],
          hist[5],
          (long long)maxUlp,
          maxRel,
          maxNormRel,
          nNaNMismatch ? "  NaN-MISMATCH" : "");
    }
  };

}  // namespace mplextest

using namespace ALPAKA_ACCELERATOR_NAMESPACE;
using namespace mplextest;

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev {

  template <int OP>
  struct KernelMplexOp {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  const float* __restrict__ in,
                                  float* __restrict__ out,
                                  int nBlocks) const {
      for (int32_t ib : cms::alpakatools::uniform_elements(acc, nBlocks)) {
        runOp<PortTraits, OP, kNN>(in + ib * kNN * inRec(OP), out + ib * kNN * outRec(OP));
      }
    }
  };

  struct KernelSimilarity {
    ALPAKA_FN_ACC void operator()(
        Acc1D const& acc, const float* __restrict__ in, float* __restrict__ out, int nBlocks, int nIter) const {
      constexpr int ir = 57, orc = 21;
      for (int32_t ib : cms::alpakatools::uniform_elements(acc, nBlocks)) {
        const float* pin = in + ib * kNN * ir;
        float* pout = out + ib * kNN * orc;
        MPlexLL<kNN> a;
        MPlexLS<kNN> b, c;
        for (int n = 0; n < kNN; ++n) {
          a.copyIn(n, pin + n * ir);
          b.copyIn(n, pin + n * ir + 36);
        }
        ::mkfitdev::similarityLL(a, b, c);
        for (int it = 1; it < nIter; ++it) {  // chained: S <- A S A^T, data stays in registers / L1
          b = c;
          ::mkfitdev::similarityLL(a, b, c);
        }
        for (int n = 0; n < kNN; ++n)
          c.copyOut(n, pout + n * orc);
      }
    }
  };

  template <int OP>
  void runPortOnDevice(Queue& queue, const std::vector<float>& in, std::vector<float>& out, int nt) {
    const int nBlocks = nt / kNN;
    auto in_h = cms::alpakatools::make_host_buffer<float[]>(queue, in.size());
    auto out_h = cms::alpakatools::make_host_buffer<float[]>(queue, out.size());
    std::memcpy(in_h.data(), in.data(), in.size() * sizeof(float));
    auto in_d = cms::alpakatools::make_device_buffer<float[]>(queue, in.size());
    auto out_d = cms::alpakatools::make_device_buffer<float[]>(queue, out.size());
    alpaka::memcpy(queue, in_d, in_h);
    alpaka::memset(queue, out_d, 0);
    const int threads = 64;
    auto div = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(nBlocks, threads), threads);
    alpaka::exec<Acc1D>(queue, div, KernelMplexOp<OP>{}, in_d.data(), out_d.data(), nBlocks);
    alpaka::memcpy(queue, out_h, out_d);
    alpaka::wait(queue);
    std::memcpy(out.data(), out_h.data(), out.size() * sizeof(float));
  }

  template <int OP>
  void runPortHostN1(const std::vector<float>& in, std::vector<float>& out, int nt) {
    for (int t = 0; t < nt; ++t)
      runOp<PortTraits, OP, 1>(in.data() + size_t(t) * inRec(OP), out.data() + size_t(t) * outRec(OP));
  }

  template <int OP>
  bool testOp(Queue& queue, int nt, bool cpuBackend) {
    std::vector<float> in = makeInputs(OP, nt);
    const size_t no = size_t(nt) * outRec(OP);
    std::vector<float> ref(no, 0.f), dev(no, 0.f), host1(no, 0.f);
    stockRef(OP, in.data(), ref.data(), nt);
    runPortOnDevice<OP>(queue, in, dev, nt);
    runPortHostN1<OP>(in, host1, nt);
    Cmp cDev, cHost;
    const int orc = outRec(OP);
    for (int t = 0; t < nt; ++t) {
      double scale = 0;
      for (int k = 0; k < orc; ++k)
        if (std::isfinite(ref[size_t(t) * orc + k]))
          scale = std::max(scale, std::abs(double(ref[size_t(t) * orc + k])));
      for (int k = 0; k < orc; ++k) {
        const size_t i = size_t(t) * orc + k;
        cDev.add(dev[i], ref[i], scale);
        cHost.add(host1[i], ref[i], scale);
      }
    }
    cDev.print(cpuBackend ? "dev" : "gpu", opName(OP));
    cHost.print("hostN1", opName(OP));
#ifdef MPLEX_TEST_STRICT
    // no FMA contraction anywhere: port and stock must agree bit by bit on every backend, except the std:: (not vdt)
    // transcendentals on a GPU, whose math library differs from the host libm by design (reported, not required)
    if (OP == kStdMath && !cpuBackend)
      return cHost.nEq == cHost.n && cDev.maxNormRel < 1e-5 && !cDev.nNaNMismatch;
    return cHost.nEq == cHost.n && cDev.nEq == cDev.n && !cHost.nNaNMismatch && !cDev.nNaNMismatch;
#else
    // FMA contraction differs between builds: only gross differences (a transliteration bug) fail
    return cHost.maxNormRel < 1e-2 && cDev.maxNormRel < 1e-2 && !cHost.nNaNMismatch && !cDev.nNaNMismatch;
#endif
  }

  template <int... OPS>
  bool testAll(Queue& queue, int nt, bool cpuBackend, std::integer_sequence<int, OPS...>) {
    bool ok = true;
    ((ok = testOp<OPS>(queue, nt, cpuBackend) && ok), ...);
    return ok;
  }

  double benchSimilarity(Queue& queue, int nt, int nRep, int nIter) {
    std::vector<float> in = makeInputs(kSimilarity66, nt);
    const int nBlocks = nt / kNN;
    auto in_h = cms::alpakatools::make_host_buffer<float[]>(queue, in.size());
    std::memcpy(in_h.data(), in.data(), in.size() * sizeof(float));
    auto in_d = cms::alpakatools::make_device_buffer<float[]>(queue, in.size());
    auto out_d = cms::alpakatools::make_device_buffer<float[]>(queue, size_t(nt) * 21);
    alpaka::memcpy(queue, in_d, in_h);
    const int threads = 128;
    auto div = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(nBlocks, threads), threads);
    for (int i = 0; i < 5; ++i)
      alpaka::exec<Acc1D>(queue, div, KernelSimilarity{}, in_d.data(), out_d.data(), nBlocks, nIter);
    alpaka::wait(queue);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < nRep; ++i)
      alpaka::exec<Acc1D>(queue, div, KernelSimilarity{}, in_d.data(), out_d.data(), nBlocks, nIter);
    alpaka::wait(queue);
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(t1 - t0).count() / nRep;
  }

  // Benchmark only (not stock): the stock Cramer inverters compute the determinant in double; this variant keeps it in
  // float, to measure what the double costs on each backend.
  template <int D>
  ALPAKA_FN_ACC void invertCramerSymFloatDet(MPlexSym<float, D, kNN>& A) {
    float* a = A.fArray;
    constexpr int N = kNN;
    for (int n = 0; n < N; ++n) {
      if constexpr (D == 2) {
        const float det = a[0 * N + n] * a[2 * N + n] - a[1 * N + n] * a[1 * N + n];
        const float s = 1.f / det;
        const float tmp = s * a[2 * N + n];
        a[1 * N + n] *= -s;
        a[2 * N + n] = s * a[0 * N + n];
        a[0 * N + n] = tmp;
      } else {
        const float c00 = a[2 * N + n] * a[5 * N + n] - a[4 * N + n] * a[4 * N + n];
        const float c01 = a[4 * N + n] * a[3 * N + n] - a[1 * N + n] * a[5 * N + n];
        const float c02 = a[1 * N + n] * a[4 * N + n] - a[2 * N + n] * a[3 * N + n];
        const float c11 = a[5 * N + n] * a[0 * N + n] - a[3 * N + n] * a[3 * N + n];
        const float c12 = a[3 * N + n] * a[1 * N + n] - a[4 * N + n] * a[0 * N + n];
        const float c22 = a[0 * N + n] * a[2 * N + n] - a[1 * N + n] * a[1 * N + n];
        const float det = a[0 * N + n] * c00 + a[1 * N + n] * c01 + a[3 * N + n] * c02;
        const float s = 1.f / det;
        a[0 * N + n] = s * c00;
        a[1 * N + n] = s * c01;
        a[2 * N + n] = s * c11;
        a[3 * N + n] = s * c02;
        a[4 * N + n] = s * c12;
        a[5 * N + n] = s * c22;
      }
    }
  }

  template <int D, bool kFloatDet>
  struct KernelInvBench {
    ALPAKA_FN_ACC void operator()(
        Acc1D const& acc, const float* __restrict__ in, float* __restrict__ out, int nBlocks, int nIter) const {
      constexpr int sz = D * (D + 1) / 2;
      for (int32_t ib : cms::alpakatools::uniform_elements(acc, nBlocks)) {
        MPlexSym<float, D, kNN> a;
        for (int n = 0; n < kNN; ++n)
          a.copyIn(n, in + (ib * kNN + n) * sz);
        for (int it = 0; it < nIter; ++it) {
          if constexpr (kFloatDet)
            invertCramerSymFloatDet<D>(a);
          else
            Matriplex::invertCramerSym(a);
        }
        for (int n = 0; n < kNN; ++n)
          a.copyOut(n, out + (ib * kNN + n) * sz);
      }
    }
  };

  template <int D, bool kFloatDet>
  double benchInv(Queue& queue, int nt, int nRep, int nIter) {
    constexpr int sz = D * (D + 1) / 2;
    std::vector<float> in = makeInputs(D == 2 ? kInvCramerSym22 : kInvCramerSym33, nt);
    const int nBlocks = nt / kNN;
    auto in_h = cms::alpakatools::make_host_buffer<float[]>(queue, in.size());
    std::memcpy(in_h.data(), in.data(), in.size() * sizeof(float));
    auto in_d = cms::alpakatools::make_device_buffer<float[]>(queue, in.size());
    auto out_d = cms::alpakatools::make_device_buffer<float[]>(queue, size_t(nt) * sz);
    alpaka::memcpy(queue, in_d, in_h);
    const int threads = 128;
    auto div = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(nBlocks, threads), threads);
    for (int i = 0; i < 3; ++i)
      alpaka::exec<Acc1D>(queue, div, KernelInvBench<D, kFloatDet>{}, in_d.data(), out_d.data(), nBlocks, nIter);
    alpaka::wait(queue);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < nRep; ++i)
      alpaka::exec<Acc1D>(queue, div, KernelInvBench<D, kFloatDet>{}, in_d.data(), out_d.data(), nBlocks, nIter);
    alpaka::wait(queue);
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(t1 - t0).count() / nRep;
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev

int main() {
  using namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev;
  auto const& devices = cms::alpakatools::devices<Platform>();
  if (devices.empty()) {
    std::printf("No device available for backend %s, test skipped\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE));
    return 0;
  }
  const bool cpuBackend = std::is_same_v<Platform, alpaka::PlatformCpu>;
  auto const& device = devices[0];
  Queue queue(device);
  constexpr int kTracks = 2048;
#ifdef MPLEX_TEST_STRICT
  const char* mode = "STRICT (no FMA contraction)";
#else
  const char* mode = "default CMSSW flags";
#endif
  std::printf("mplex test [%s]: backend %s, device %s, kNN=%d, stock NN=%d, %d tracks per op\n",
              mode,
              EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE),
              alpaka::getName(device).c_str(),
              kNN,
              stockNN(),
              kTracks);
  std::printf(
      "  rows: port in the Alpaka kernel ('dev'/'gpu') and port on the host at N=1 ('hostN1') vs STOCK (host, N=8)\n");
  const int badSTypes = checkSTypesLayout();
  std::printf("  mkfitdev S-types vs ROOT SMatrix (stock MatrixSTypes.h) storage order: %d mismatches\n", badSTypes);
  const bool ok = testAll(queue, kTracks, cpuBackend, std::make_integer_sequence<int, kNumOps>{}) && badSTypes == 0;

  std::printf("benchmark 6x6 similarity A*S*A^T (kNN=%d):\n", kNN);
  for (int nIter : {1, 16}) {
    for (int nt : {2048, 262144}) {
      const int nRep = nt <= 2048 ? 1000 : 20;
      const double us = benchSimilarity(queue, nt, nRep, nIter);
      std::printf("  %7d tracks x %2d chained similarities per load: %10.2f us per launch, %8.3f ns per similarity\n",
                  nt,
                  nIter,
                  us,
                  1000. * us / nt / nIter);
    }
  }
  std::printf("benchmark Cramer inversion, 262144 tracks x 64 chained inversions (kNN=%d), ns per inversion:\n", kNN);
  {
    constexpr int nt = 262144, nIter = 64;
    const int nRep = cpuBackend ? 2 : 20;
    const double s2d = benchInv<2, false>(queue, nt, nRep, nIter), s2f = benchInv<2, true>(queue, nt, nRep, nIter);
    const double s3d = benchInv<3, false>(queue, nt, nRep, nIter), s3f = benchInv<3, true>(queue, nt, nRep, nIter);
    std::printf("  invertCramerSym 2x2: stock (double det) %8.4f   float det (not stock) %8.4f\n",
                1000. * s2d / nt / nIter,
                1000. * s2f / nt / nIter);
    std::printf("  invertCramerSym 3x3: stock (double det) %8.4f   float det (not stock) %8.4f\n",
                1000. * s3d / nt / nIter,
                1000. * s3f / nt / nIter);
  }
  std::printf("%s\n", ok ? "RESULT: OK" : "RESULT: FAILED (see criteria at the top of test_mplex.dev.cc)");
  return ok ? 0 : 1;
}
