// Host-only reference for the mplex unit test: the shared op bodies (test_mplex_ops.h) instantiated with STOCK
// mkFit Matriplex (RecoTracker/MkFitCore/src/Matriplex, CMSSW_20_1_0_pre2), stock vdt and stock mkfit helpers,
// at the stock lane width NN = 8. Compiled by the host compiler only (also in the CUDA test binary).

#include <cmath>
#include <cstring>
#include <utility>

#include "mplex_stock/Matrix.h"  // verbatim copy of RecoTracker/MkFitCore/src/Matrix.h (see mplex_stock/README)
#include "RecoTracker/MkFitCore/interface/Config.h"
#include "RecoTracker/MkFitCore/interface/Hit.h"
#include <vdt/atan.h>

#include "RecoTracker/MkFitCore/interface/MatrixSTypes.h"
#include "RecoTracker/MkFitAlpaka/interface/matriplex/MatrixSTypes.h"

#include "test_mplex_ops.h"

namespace mplextest {

  struct StockTraits {
    template <int N>
    using LL = Matriplex::Matriplex<float, 6, 6, N>;
    template <int N>
    using LV = Matriplex::Matriplex<float, 6, 1, N>;
    template <int N>
    using LS = Matriplex::MatriplexSym<float, 6, N>;
    template <int N>
    using HH = Matriplex::Matriplex<float, 3, 3, N>;
    template <int N>
    using HS = Matriplex::MatriplexSym<float, 3, N>;
    template <int N>
    using LH = Matriplex::Matriplex<float, 6, 3, N>;
    template <int N>
    using HL = Matriplex::Matriplex<float, 3, 6, N>;
    template <int N>
    using M22 = Matriplex::Matriplex<float, 2, 2, N>;
    template <int N>
    using S22 = Matriplex::MatriplexSym<float, 2, N>;
    template <int N>
    using QF = Matriplex::Matriplex<float, 1, 1, N>;
    using SSym66 = mkfit::SMatrixSym66;

    // Stock has no generic dense similarity: A * S * A^T with multiplyGeneral on expanded matrices (same products,
    // same summation order as the GenMul-generated SimilarityLL*.ah of the port).
    template <int N>
    static void similarity(const LL<N>& a, const LS<N>& b, LS<N>& c) {
      LL<N> bf, at, tmp, full;
      for (int n = 0; n < N; ++n)
        for (int i = 0; i < 6; ++i)
          for (int j = 0; j < 6; ++j) {
            bf(n, i, j) = b.constAt(n, i, j);
            at(n, i, j) = a.constAt(n, j, i);
          }
      Matriplex::multiplyGeneral(a, bf, tmp);
      Matriplex::multiplyGeneral(tmp, at, full);
      for (int n = 0; n < N; ++n)
        for (int i = 0; i < 6; ++i)
          for (int j = 0; j <= i; ++j)
            c.At(n, i, j) = full.constAt(n, i, j);
    }

    static void sincos4f(float x, float& s, float& c) { mkfit::sincos4(x, s, c); }
    static float fast_logf(float x) { return vdt::fast_logf(x); }
    static float fast_atanf(float x) { return vdt::fast_atanf(x); }
    static float fast_isqrtf(float x) { return vdt::fast_isqrtf(x); }
    static float getEta(float r, float z) { return mkfit::getEta(r, z); }
    static float getEta3(float x, float y, float z) { return mkfit::getEta(x, y, z); }
    static float getPhi(float x, float y) { return mkfit::getPhi(x, y); }
    static float squashPhiGeneral(float p) { return mkfit::squashPhiGeneral(p); }
    static float squashPhiMinimal(float p) { return mkfit::squashPhiMinimal(p); }
    static float bFieldFromZR(float z, float r) { return mkfit::Config::bFieldFromZR(z, r); }
    static float hipo(float x, float y) { return mkfit::hipo(x, y); }
  };

  template <int OP>
  void stockRefOp(const float* in, float* out, int nTracks) {
    constexpr int N = mkfit::NN;
    for (int t = 0; t < nTracks; t += N)
      runOp<StockTraits, OP, N>(in + t * inRec(OP), out + t * outRec(OP));
  }

  template <int... OPS>
  void stockRefDispatch(int op, const float* in, float* out, int nTracks, std::integer_sequence<int, OPS...>) {
    ((op == OPS ? stockRefOp<OPS>(in, out, nTracks) : void()), ...);
  }

  // nTracks must be a multiple of mkfit::NN (8).
  void stockRef(int op, const float* in, float* out, int nTracks) {
    stockRefDispatch(op, in, out, nTracks, std::make_integer_sequence<int, kNumOps>{});
  }

  int stockNN() { return mkfit::NN; }

  // mkfitdev S-types vs ROOT SMatrix (stock MatrixSTypes.h): same size and same element storage order.
  template <typename R, typename M>
  int compareSType(bool sym) {
    R r;
    for (int i = 0; i < R::kRows; ++i)
      for (int j = 0; j < R::kCols; ++j)
        if (!sym || i >= j)
          r(i, j) = 100.f * i + j;
    M m;
    static_assert(sizeof(M) == sizeof(R));
    std::memcpy(m.fArray, r.Array(), sizeof(m.fArray));
    int bad = 0;
    for (int i = 0; i < R::kRows; ++i)
      for (int j = 0; j < R::kCols; ++j)
        bad += (m(i, j) != r(i, j)) + (&m(i, j) - m.Array() != &r(i, j) - r.Array());
    return bad;
  }

  int checkSTypesLayout() {
    int bad = 0;
    bad += compareSType<mkfit::SMatrixSym66, mkfitdev::SMatrixSym66>(true);
    bad += compareSType<mkfit::SMatrixSym33, mkfitdev::SMatrixSym33>(true);
    bad += compareSType<mkfit::SMatrixSym22, mkfitdev::SMatrixSym22>(true);
    bad += compareSType<mkfit::SMatrix66, mkfitdev::SMatrix66>(false);
    bad += compareSType<mkfit::SMatrix33, mkfitdev::SMatrix33>(false);
    bad += compareSType<mkfit::SMatrix22, mkfitdev::SMatrix22>(false);
    bad += compareSType<mkfit::SMatrix36, mkfitdev::SMatrix36>(false);
    bad += compareSType<mkfit::SMatrix63, mkfitdev::SMatrix63>(false);
    bad += compareSType<mkfit::SMatrix26, mkfitdev::SMatrix26>(false);
    bad += compareSType<mkfit::SMatrix62, mkfitdev::SMatrix62>(false);
    static_assert(sizeof(mkfitdev::SVector6) == sizeof(mkfit::SVector6));
    static_assert(sizeof(mkfitdev::SVector3) == sizeof(mkfit::SVector3));
    static_assert(sizeof(mkfitdev::SVector2) == sizeof(mkfit::SVector2));
    // MatriplexSym packed order == MatRepSym order: copyIn of a ROOT error array gives the right elements
    mkfit::SMatrixSym66 e;
    for (int i = 0; i < 6; ++i)
      for (int j = 0; j <= i; ++j)
        e(i, j) = 10.f * i + j;
    Matriplex::MatriplexSym<float, 6, 8> mp;
    mp.copyIn(3, e.Array());
    for (int i = 0; i < 6; ++i)
      for (int j = 0; j < 6; ++j)
        bad += mp.constAt(3, i, j) != e(i, j);
    return bad;
  }

}  // namespace mplextest
