#ifndef RecoTracker_MkFitAlpaka_test_alpaka_test_mplex_ops_h
#define RecoTracker_MkFitAlpaka_test_alpaka_test_mplex_ops_h

// Shared op bodies of the mplex unit test. Written once against the Matriplex API and instantiated twice:
//   - with StockTraits (stock RecoTracker/MkFitCore Matriplex + vdt + mkfit helpers), host only, in test_mplex_stockref.cc
//   - with PortTraits (mkfitdev portable Matriplex + math), in the Alpaka kernels of test_mplex.dev.cc
// Matriplex free functions are found by ADL; scalar math goes through the traits.
// Data layout: one record of inRec(OP) floats per track (all inputs, each in stock element order), one record of
// outRec(OP) floats per track for the outputs. A call processes N consecutive tracks.

#include <cmath>

#include <alpaka/core/Common.hpp>

namespace mplextest {

  enum Op {
    kMul66 = 0,       // Matriplex::multiply(MPlexLL, MPlexLL, MPlexLL)
    kMul33,           // Matriplex::multiply(MPlexHH, MPlexHH, MPlexHH)
    kMulGen63x36,     // Matriplex::multiplyGeneral(MPlexLH, MPlexHL, MPlexLL)
    kMulGen66x61,     // Matriplex::multiplyGeneral(MPlexLL, MPlexLV, MPlexLV)
    kSymMul66,        // Matriplex::multiply(MPlexLS, MPlexLS, MPlexLL)
    kSymMul33,        // Matriplex::multiply(MPlexHS, MPlexHS, MPlexHH)
    kInvCramerSym33,  // Matriplex::invertCramerSym(MPlexHS) (+ determinant)
    kInvCramerSym22,  // Matriplex::invertCramerSym(MPlex2S) (+ determinant)
    kInvCholSym33,    // Matriplex::invertCholeskySym(MPlexHS)
    kInvCramer33,     // Matriplex::invertCramer(MPlexHH)
    kInvCramer22,     // Matriplex::invertCramer(MPlex22)
    kInvChol33,       // Matriplex::invertCholesky(MPlexHH)
    kInvUL3x3,        // MPlexLS::invertUpperLeft3x3 (+ addNoiseIntoUpperLeft3x3 first)
    kSimilarity66,    // A * S * A^T (port: generated SimilarityLL*.ah; stock: multiplyGeneral on expanded matrices)
    kFastSinCos,      // Matriplex::fast_sincos(MPlexQF)
    kFastSinCosTan,   // Matriplex::fast_sin, fast_cos, fast_tan
    kFastAtan2,       // Matriplex::fast_atan2
    kFastIsqrt,       // Matriplex::fast_isqrt
    kSinCos4,         // Matriplex::sincos4 + mkfit::sincos4
    kElementwise,     // hypot, sqrt, abs, min_max, min, max, + - * / with scalars and plexes, negate_if_ltz, sqr
    kScalarMath,  // vdt fast_logf, fast_atanf, getEta, getPhi, squashPhiGeneral, squashPhiMinimal, bFieldFromZR, hipo
    kDataMove,    // slurpIn, copyIn/Out, ReduceFixedIJ, aij, setDiagonal3x3, scale/add/subtract/negate, SMatrixSym66
    kStdMath,     // std-version Matriplex transcendentals: sin, cos, tan, atan2, sincos (std:: libm vs CUDA libdevice)
    kNumOps
  };

  constexpr const char* opName(int op) {
    constexpr const char* names[] = {"mul66",         "mul33",          "mulGen63x36",    "mulGen66x61",  "symMul66",
                                     "symMul33",      "invCramerSym33", "invCramerSym22", "invCholSym33", "invCramer33",
                                     "invCramer22",   "invChol33",      "invUL3x3",       "similarity66", "fastSinCos",
                                     "fastSinCosTan", "fastAtan2",      "fastIsqrt",      "sinCos4",      "elementwise",
                                     "scalarMath",    "dataMove",       "stdMath"};
    return names[op];
  }

  // floats per track: inputs, outputs
  constexpr int inRec(int op) {
    switch (op) {
      case kMul66:
        return 72;
      case kMul33:
        return 18;
      case kMulGen63x36:
        return 36;
      case kMulGen66x61:
        return 42;
      case kSymMul66:
        return 42;
      case kSymMul33:
        return 12;
      case kInvCramerSym33:
        return 6;
      case kInvCramerSym22:
        return 3;
      case kInvCholSym33:
        return 6;
      case kInvCramer33:
        return 9;
      case kInvCramer22:
        return 4;
      case kInvChol33:
        return 9;
      case kInvUL3x3:
        return 21;
      case kSimilarity66:
        return 57;
      case kFastSinCos:
        return 1;
      case kFastSinCosTan:
        return 1;
      case kFastAtan2:
        return 2;
      case kFastIsqrt:
        return 1;
      case kSinCos4:
        return 1;
      case kElementwise:
        return 2;
      case kScalarMath:
        return 4;
      case kDataMove:
        return 57;
      case kStdMath:
        return 2;
      default:
        return 0;
    }
  }

  constexpr int outRec(int op) {
    switch (op) {
      case kMul66:
        return 36;
      case kMul33:
        return 9;
      case kMulGen63x36:
        return 36;
      case kMulGen66x61:
        return 6;
      case kSymMul66:
        return 36;
      case kSymMul33:
        return 9;
      case kInvCramerSym33:
        return 7;
      case kInvCramerSym22:
        return 4;
      case kInvCholSym33:
        return 6;
      case kInvCramer33:
        return 10;
      case kInvCramer22:
        return 5;
      case kInvChol33:
        return 9;
      case kInvUL3x3:
        return 21;
      case kSimilarity66:
        return 21;
      case kFastSinCos:
        return 2;
      case kFastSinCosTan:
        return 3;
      case kFastAtan2:
        return 1;
      case kFastIsqrt:
        return 1;
      case kSinCos4:
        return 4;
      case kElementwise:
        return 8;
      case kScalarMath:
        return 10;
      case kDataMove:
        return 101;
      case kStdMath:
        return 6;
      default:
        return 0;
    }
  }

  template <typename MP>
  ALPAKA_FN_HOST_ACC inline void load(MP& m, const float* in, int rec, int off) {
    for (int n = 0; n < m.plexSize(); ++n)
      m.copyIn(n, in + n * rec + off);
  }

  template <typename MP>
  ALPAKA_FN_HOST_ACC inline void store(const MP& m, float* out, int rec, int off) {
    for (int n = 0; n < m.plexSize(); ++n)
      m.copyOut(n, out + n * rec + off);
  }

  // Run op OP on N tracks: in/out point at the first track's record.
  template <typename M, int OP, int N>
  ALPAKA_FN_HOST_ACC void runOp(const float* in, float* out) {
    constexpr int ir = inRec(OP);
    constexpr int orc = outRec(OP);
    using LL = typename M::template LL<N>;
    using LV = typename M::template LV<N>;
    using LS = typename M::template LS<N>;
    using HH = typename M::template HH<N>;
    using HS = typename M::template HS<N>;
    using LH = typename M::template LH<N>;
    using HL = typename M::template HL<N>;
    using M22 = typename M::template M22<N>;
    using S22 = typename M::template S22<N>;
    using QF = typename M::template QF<N>;

    if constexpr (OP == kMul66) {
      LL a, b, c;
      load(a, in, ir, 0);
      load(b, in, ir, 36);
      multiply(a, b, c);
      store(c, out, orc, 0);
    } else if constexpr (OP == kMul33) {
      HH a, b, c;
      load(a, in, ir, 0);
      load(b, in, ir, 9);
      multiply(a, b, c);
      store(c, out, orc, 0);
    } else if constexpr (OP == kMulGen63x36) {
      LH a;
      HL b;
      LL c;
      load(a, in, ir, 0);
      load(b, in, ir, 18);
      multiplyGeneral(a, b, c);
      store(c, out, orc, 0);
    } else if constexpr (OP == kMulGen66x61) {
      LL a;
      LV b, c;
      load(a, in, ir, 0);
      load(b, in, ir, 36);
      multiplyGeneral(a, b, c);
      store(c, out, orc, 0);
    } else if constexpr (OP == kSymMul66) {
      LS a, b;
      LL c;
      load(a, in, ir, 0);
      load(b, in, ir, 21);
      multiply(a, b, c);
      store(c, out, orc, 0);
    } else if constexpr (OP == kSymMul33) {
      HS a, b;
      HH c;
      load(a, in, ir, 0);
      load(b, in, ir, 6);
      multiply(a, b, c);
      store(c, out, orc, 0);
    } else if constexpr (OP == kInvCramerSym33) {
      HS a;
      double det[N];
      load(a, in, ir, 0);
      invertCramerSym(a, det);
      store(a, out, orc, 0);
      for (int n = 0; n < N; ++n)
        out[n * orc + 6] = float(det[n]);
    } else if constexpr (OP == kInvCramerSym22) {
      S22 a;
      double det[N];
      load(a, in, ir, 0);
      invertCramerSym(a, det);
      store(a, out, orc, 0);
      for (int n = 0; n < N; ++n)
        out[n * orc + 3] = float(det[n]);
    } else if constexpr (OP == kInvCholSym33) {
      HS a;
      load(a, in, ir, 0);
      invertCholeskySym(a);
      store(a, out, orc, 0);
    } else if constexpr (OP == kInvCramer33) {
      HH a;
      double det[N];
      load(a, in, ir, 0);
      invertCramer(a, det);
      store(a, out, orc, 0);
      for (int n = 0; n < N; ++n)
        out[n * orc + 9] = float(det[n]);
    } else if constexpr (OP == kInvCramer22) {
      M22 a;
      double det[N];
      load(a, in, ir, 0);
      invertCramer(a, det);
      store(a, out, orc, 0);
      for (int n = 0; n < N; ++n)
        out[n * orc + 4] = float(det[n]);
    } else if constexpr (OP == kInvChol33) {
      HH a;
      load(a, in, ir, 0);
      invertCholesky(a);
      store(a, out, orc, 0);
    } else if constexpr (OP == kInvUL3x3) {
      LS a;
      load(a, in, ir, 0);
      a.addNoiseIntoUpperLeft3x3(1.e-4f);  // one value per plex (stock signature): a constant, so any N agrees
      a.invertUpperLeft3x3();
      store(a, out, orc, 0);
    } else if constexpr (OP == kSimilarity66) {
      LL a;
      LS b, c;
      load(a, in, ir, 0);
      load(b, in, ir, 36);
      M::similarity(a, b, c);
      store(c, out, orc, 0);
    } else if constexpr (OP == kFastSinCos) {
      QF x, s, c;
      load(x, in, ir, 0);
      fast_sincos(x, s, c);
      store(s, out, orc, 0);
      store(c, out, orc, 1);
    } else if constexpr (OP == kFastSinCosTan) {
      QF x;
      load(x, in, ir, 0);
      QF s = fast_sin(x);
      QF c = fast_cos(x);
      QF t = fast_tan(x);
      store(s, out, orc, 0);
      store(c, out, orc, 1);
      store(t, out, orc, 2);
    } else if constexpr (OP == kFastAtan2) {
      QF y, x;
      load(y, in, ir, 0);
      load(x, in, ir, 1);
      QF r = fast_atan2(y, x);
      store(r, out, orc, 0);
    } else if constexpr (OP == kFastIsqrt) {
      QF x;
      load(x, in, ir, 0);
      QF r = fast_isqrt(x);
      store(r, out, orc, 0);
    } else if constexpr (OP == kSinCos4) {
      QF x, s, c;
      load(x, in, ir, 0);
      sincos4(x, s, c);  // Matriplex::sincos4 (ADL)
      store(s, out, orc, 0);
      store(c, out, orc, 1);
      for (int n = 0; n < N; ++n) {
        float ss, cc;
        M::sincos4f(in[n * ir], ss, cc);  // Matrix.h float version
        out[n * orc + 2] = ss;
        out[n * orc + 3] = cc;
      }
    } else if constexpr (OP == kElementwise) {
      QF a, b;
      load(a, in, ir, 0);
      load(b, in, ir, 1);
      QF h = hypot(a, b);
      QF s = sqrt(abs(a));
      QF mn, mx;
      min_max(a, b, mn, mx);
      QF r1 = (h * a - b / (mx + 1.f)) + 2.f * s - mn;
      QF r2 = negate_if_ltz(sqr(a) - 3.f, b);
      QF r3 = min(a, b) * max(a, b) / (1.f + sqr(b));
      QF r4 = -a;
      r4 += b;
      r4 *= 0.5f;
      r4 -= a * b;
      store(h, out, orc, 0);
      store(s, out, orc, 1);
      store(mn, out, orc, 2);
      store(mx, out, orc, 3);
      store(r1, out, orc, 4);
      store(r2, out, orc, 5);
      store(r3, out, orc, 6);
      store(r4, out, orc, 7);
    } else if constexpr (OP == kStdMath) {
      QF x, y;
      load(x, in, ir, 0);
      load(y, in, ir, 1);
      QF s = sin(x), c = cos(x), t = tan(x), a = atan2(y, x), s2, c2;
      sincos(y, s2, c2);
      store(s, out, orc, 0);
      store(c, out, orc, 1);
      store(t, out, orc, 2);
      store(a, out, orc, 3);
      store(s2, out, orc, 4);
      store(c2, out, orc, 5);
    } else if constexpr (OP == kDataMove) {
      LS s, d, x;
      LL a;
      load(s, in, ir, 0);
      load(a, in, ir, 21);
      int vi[N];
      for (int j = 0; j < N; ++j)
        vi[j] = j * ir;
      x.slurpIn(in, vi);  // generic slurpIn: lane j = in[j * ir + i], i.e. the LS of track j
      d = s;
      d.scale(2.f);
      d.add(x);
      d.subtract(d, s);  // 2 s
      for (int n = 0; n < N; ++n)
        d.setDiagonal3x3(n, 0.5f);
      QF q = a.ReduceFixedIJ(2, 3);
      a.aij(1, 1) = q;
      a.aij(0, 0) = 3.f;
      a.negate();
      QF nq = -q;
      store(d, out, orc, 0);
      store(a, out, orc, 21);
      store(x, out, orc, 57);
      store(q, out, orc, 78);
      store(nq, out, orc, 79);
      // per-track state storage (stock: ROOT SMatrixSym66, port: mkfitdev::SMatrixSym66) <-> plex
      typename M::SSym66 e;
      LS y;
      for (int n = 0; n < N; ++n) {
        for (int i = 0; i < 6; ++i)
          for (int j = 0; j <= i; ++j)
            e(i, j) = 3.f * s.constAt(n, i, j);
        y.copyIn(n, e.Array());
      }
      store(y, out, orc, 80);
    } else if constexpr (OP == kScalarMath) {
      for (int n = 0; n < N; ++n) {
        const float* p = in + n * ir;
        float* o = out + n * orc;
        const float x = p[0], y = p[1], z = p[2], w = p[3];
        o[0] = M::fast_logf(w);
        o[1] = M::fast_atanf(x / 50.f);
        o[2] = M::getEta(std::abs(y) + 1.f, z);
        o[3] = M::getEta3(x, y, z);
        o[4] = M::getPhi(x, y);
        o[5] = M::squashPhiGeneral(w * 7.f - 30.f);
        o[6] = M::squashPhiMinimal(x / 15.f);
        o[7] = M::bFieldFromZR(z, std::abs(y));
        o[8] = M::hipo(x, y);
        o[9] = M::fast_isqrtf(w);
      }
    }
  }

}  // namespace mplextest

#endif
