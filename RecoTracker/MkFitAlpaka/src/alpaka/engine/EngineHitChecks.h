#ifndef RecoTracker_MkFitAlpaka_src_alpaka_engine_EngineHitChecks_h
#define RecoTracker_MkFitAlpaka_src_alpaka_engine_EngineHitChecks_h

// The per-hit acceptance checks of the clone engine's K3b (KernelEngineOptions), shared with the seed overlap-hit
// search (src/alpaka/select/SeedOverlapKernel.h): the dynamic chi2 cut and the strip q compatibility.

#include <cmath>

#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandEngineTypes.h"
#include "RecoTracker/MkFitAlpaka/interface/math/Config.h"
#include "RecoTracker/MkFitAlpaka/interface/matriplex/Matrix.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev {

  // MkFinder::getHitSelDynamicChi2Cut (MkFinder.cc:301-319) on the layer-propagated state (iP).
  // MkFitCore (GCC, x86-64-v3) contracts v[c2_0]*max_invpt + v[c2_1]*theta into an fma; written explicitly.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float engineDynamicChi2Cut(TAcc const& acc,
                                                            const ::mkfitdev::EngineLayerParams& lp,
                                                            const ::mkfitdev::CandPropState& ps,
                                                            float minChi2Cut) {
    const float invpt = ps.par[3];
    const float theta = std::abs(ps.par[5] - ::mkfitdev::Const::PIOver2);
    const float max_invpt = alpaka::math::min(acc, invpt, 10.0f);
    if (lp.hasC2) {
      const float this_c2 = lp.c2[0] * (alpaka::math::fma(acc, lp.c2[1], max_invpt, lp.c2[2] * theta) + lp.c2[3]);
      if (this_c2 > minChi2Cut)
        return this_c2;
    }
    return minChi2Cut;
  }

  // isStripQCompatible (MkFinder.cc:1305-1330): pErr = layer-propagated errors (iP), (px, py, pz) =
  // plane-propagated parameters, msErr/msPar of the hit. Sums of products written as the fma chains GCC forms.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool engineIsStripQCompatible(TAcc const& acc,
                                                               bool isBarrel,
                                                               const float* pErr,  // 21, lower triangle
                                                               float px,
                                                               float py,
                                                               float pz,
                                                               const ::mkfitdev::MPlexHS<1>& msErr,
                                                               const ::mkfitdev::MPlexHV<1>& msPar) {
    if (isBarrel) {
      const float res = std::abs(msPar.constAt(0, 2, 0) - pz);
      const float hitHL = alpaka::math::sqrt(acc, msErr.constAt(0, 2, 2) * 3.f);
      const float qErr = alpaka::math::sqrt(acc, pErr[5]);  // (2,2)
      return hitHL + alpaka::math::max(acc, 3.f * qErr, 0.5f) > res;
    } else {
      const float res0 = msPar.constAt(0, 0, 0) - px;
      const float res1 = msPar.constAt(0, 1, 0) - py;
      const float hitT2 = msErr.constAt(0, 0, 0) + msErr.constAt(0, 1, 1);
      const float hitT2inv = 1.f / hitT2;
      const float proj0 = msErr.constAt(0, 0, 0) * hitT2inv;
      const float proj1 = msErr.constAt(0, 0, 1) * hitT2inv;
      const float proj2 = msErr.constAt(0, 1, 1) * hitT2inv;
      // pErr (0,0) = [0], (1,0) = [1], (1,1) = [2]
      const float q =
          alpaka::math::fma(acc, pErr[2], proj2, alpaka::math::fma(acc, pErr[0], proj0, 2.f * pErr[1] * proj1));
      const float qErr = alpaka::math::sqrt(acc, std::abs(q));
      const float rp = alpaka::math::fma(
          acc, res1 * proj2, res1, alpaka::math::fma(acc, res0 * proj0, res0, 2.f * res1 * proj1 * res0));
      const float resProj = alpaka::math::sqrt(acc, rp);
      return alpaka::math::sqrt(acc, hitT2 * 3.f) + alpaka::math::max(acc, 3.f * qErr, 0.5f) > resProj;
    }
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev

#endif
