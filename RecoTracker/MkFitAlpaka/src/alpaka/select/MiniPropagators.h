#ifndef RecoTracker_MkFitAlpaka_src_alpaka_select_MiniPropagators_h
#define RecoTracker_MkFitAlpaka_src_alpaka_select_MiniPropagators_h

// Portable transliteration of stock mkFit mini propagators (CMSSW_20_1_0_pre2 RecoTracker/MkFitCore/src/
// MiniPropagators.{h,cc}), as used by MkFinder::selectHitIndicesV2 of the LST step.
//   - State / InitialState: the scalar flavour (per-hit propagation). State(par) uses std::cos/sin/tan as stock.
//   - StateV / InitialStateV: one slot of the stock "Plex" flavour (StatePlex / InitialStatePlex, used for the bin
//     limits). Stock evaluates it element-wise on MPlexQF with vdt fast_sincos / fast_tan; every operation is
//     per element, so one slot per call is the same arithmetic.
// Only the algorithms the LST step reaches are ported: propagate_to_r(PA_Exact), propagate_to_z(PA_Exact),
// propagate_to_plane(PA_Line). Stock PA_Line/PA_Quadratic of to_r/to_z fall through to PA_Exact; the plane
// PA_Quadratic/PA_Exact throw in stock and are never called.

#include <cmath>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/MkFitAlpaka/interface/math/Config.h"
#include "RecoTracker/MkFitAlpaka/interface/math/MathUtils.h"
#include "RecoTracker/MkFitAlpaka/interface/math/vdtMath.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::mini_propagators {

  namespace mconst = ::mkfitdev::Const;
  namespace mconfig = ::mkfitdev::Config;

  enum PropAlgo_e { PA_Line, PA_Quadratic, PA_Exact };

  // Module plane of ModuleInfo as used by propagate_to_plane: center position and normal (zdir).
  struct ModulePlane {
    float pos[3];
    float zdir[3];
  };

  struct State {
    float x, y, z;
    float px, py, pz;
    float dalpha;
    int fail_flag;
  };

  // stock State::State(const MPlexLV& par, int ti): par = {x, y, z, 1/pT, phi, theta}
  ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE State makeState(const float* par) {
    State s;
    s.x = par[0];
    s.y = par[1];
    s.z = par[2];
    const float pt = 1.0f / par[3];
    s.px = pt * std::cos(par[4]);
    s.py = pt * std::sin(par[4]);
    s.pz = pt / std::tan(par[5]);
    s.dalpha = 0.f;  // stock leaves dalpha and fail_flag indeterminate; never read before written
    s.fail_flag = 0;
    return s;
  }

  struct InitialState : public State {
    float inv_pt, inv_k;
    float theta;

    ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE InitialState(const float* par, int charge) : State(makeState(par)) {
      inv_pt = par[3];
      theta = par[5];
      inv_k = ((charge < 0) ? 0.01f : -0.01f) * mconst::sol * mconfig::Bfield;
    }

    // stock InitialState::propagate_to_r (PA_Exact); returns the fail flag
    ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE bool propagate_to_r(float R, State& c) const {
      // Momentum is always updated -- used as temporary for stepping.
      const float k = 1.0f / inv_k;

      const float curv = 0.5f * inv_k * inv_pt;
      const float oo_curv = 1.0f / curv;  // 2 * radius of curvature
      const float lambda = pz * inv_pt;

      float D = 0;

      c = *this;
      c.dalpha = 0;
      for (int i = 0; i < mconfig::Niter; ++i) {
        // 3-rd order asin for symmetric incidence (shortest arc length).
        float r0 = ::mkfitdev::hipo(c.x, c.y);
        float td = (R - r0) * curv;
        float id = oo_curv * td * (1.0f + 0.16666666f * td * td);
        D += id;

        float alpha = id * inv_pt * inv_k;
        float sina, cosa;
        ::mkfitdev::vdt::fast_sincosf(alpha, sina, cosa);

        c.dalpha += alpha;
        c.x += k * (c.px * sina - c.py * (1.0f - cosa));
        c.y += k * (c.py * sina + c.px * (1.0f - cosa));

        const float o_px = c.px;  // copy before overwriting
        c.px = c.px * cosa - c.py * sina;
        c.py = c.py * cosa + o_px * sina;
      }

      c.z += lambda * D;

      c.fail_flag = std::abs(::mkfitdev::hipo(c.x, c.y) - R) < 0.1f ? 0 : 1;
      return c.fail_flag;
    }

    // stock InitialState::propagate_to_z (PA_Exact, update_momentum = true); never fails
    ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE bool propagate_to_z(float Z, State& c) const {
      const float k = 1.0f / inv_k;

      const float dz = Z - z;
      const float alpha = dz * inv_k / pz;

      float sina, cosa;
      ::mkfitdev::vdt::fast_sincosf(alpha, sina, cosa);

      c.dalpha = alpha;
      c.x = x + k * (px * sina - py * (1.0f - cosa));
      c.y = y + k * (py * sina + px * (1.0f - cosa));
      c.z = Z;

      c.px = px * cosa - py * sina;
      c.py = py * cosa + px * sina;
      c.pz = pz;

      c.fail_flag = 0;
      return c.fail_flag;
    }

    // stock InitialState::propagate_to_plane (PA_Line): straight step along the momentum to the module plane;
    // stock returns false (no failure) and does not touch c.fail_flag
    ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE bool propagate_to_plane(const ModulePlane& mi, State& c) const {
      // t * p_vec intersects the plane:
      float t = (mi.pos[0] * mi.zdir[0] + mi.pos[1] * mi.zdir[1] + mi.pos[2] * mi.zdir[2] - x * mi.zdir[0] -
                 y * mi.zdir[1] - z * mi.zdir[2]) /
                (px * mi.zdir[0] + py * mi.zdir[1] + pz * mi.zdir[2]);

      c = *this;
      c.x += t * c.px;
      c.y += t * c.py;
      c.z += t * c.pz;
      return false;
    }
  };

  //-----------------------------------------------------------
  // One slot of the stock vectorized flavour (StatePlex / InitialStatePlex)
  //-----------------------------------------------------------

  // stock StatePlex::StatePlex(const MPlexLV& par): fast_sincos(phi, py, px); px *= pt; py *= pt;
  // pz = pt / fast_tan(theta)
  ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE State makeStateV(const float* par) {
    State s;
    s.x = par[0];
    s.y = par[1];
    s.z = par[2];
    const float pt = 1.0f / par[3];
    ::mkfitdev::vdt::fast_sincosf(par[4], s.py, s.px);
    s.px *= pt;
    s.py *= pt;
    s.pz = pt / ::mkfitdev::vdt::fast_tanf(par[5]);
    s.dalpha = 0.f;
    s.fail_flag = 0;
    return s;
  }

  // Matriplex::hypot (a*a + b*b, then sqrt) as stock's -Ofast x86-64-v3 build contracts it (see InitialStateV).
  ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE float hypotV(float a, float b) { return std::sqrt(std::fma(b, b, a * a)); }

  // Vectorized flavour: stock evaluates Matriplex expressions that GCC (-Ofast, x86-64-v3) contracts as
  // a*b + c*d -> fma(c, d, a*b), a*b - c*d -> fma(-c, d, a*b), x + k*y -> fma(k, y, x); the explicit fma forms below
  // reproduce that (checked against stock Bins::sp1/sp2 dumps).
  struct InitialStateV : public State {
    float inv_pt, inv_k;
    float theta;

    ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE InitialStateV(const float* par, int charge) : State(makeStateV(par)) {
      inv_pt = par[3];
      theta = par[5];
      inv_k = ((charge < 0) ? 0.01f : -0.01f) * mconst::sol * mconfig::Bfield;
    }

    // stock InitialStatePlex::propagate_to_r (PA_Exact) for one slot; fail if |R - r| > 0.1
    ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE int propagate_to_r(float R, State& c) const {
      const float k = 1.0f / inv_k;

      const float curv = 0.5f * inv_k * inv_pt;
      const float oo_curv = 1.0f / curv;  // 2 * radius of curvature
      const float lambda = pz * inv_pt;

      float D = 0;

      c = *this;
      c.dalpha = 0;
      for (int i = 0; i < mconfig::Niter; ++i) {
        float r0 = hypotV(c.x, c.y);
        float td = (R - r0) * curv;
        float id = oo_curv * td * std::fma(0.16666666f * td, td, 1.0f);
        D += id;

        float alpha = id * inv_pt * inv_k;

        float sina, cosa;
        ::mkfitdev::vdt::fast_sincosf(alpha, sina, cosa);

        c.dalpha += alpha;
        c.x = std::fma(k, std::fma(-c.py, 1.0f - cosa, c.px * sina), c.x);
        c.y = std::fma(k, std::fma(c.px, 1.0f - cosa, c.py * sina), c.y);

        float o_px = c.px;  // copy before overwriting
        c.px = std::fma(-c.py, sina, c.px * cosa);
        c.py = std::fma(o_px, sina, c.py * cosa);
      }

      c.z = std::fma(lambda, D, c.z);

      const float r = hypotV(c.x, c.y);
      c.fail_flag = (std::abs(R - r) > 0.1f) ? 1 : 0;
      return c.fail_flag;
    }

    // stock InitialStatePlex::propagate_to_z (PA_Exact, update_momentum = true) for one slot
    ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE int propagate_to_z(float Z, State& c) const {
      float k = 1.0f / inv_k;

      float dz = Z - z;
      float alpha = dz * inv_k / pz;

      float sina, cosa;
      ::mkfitdev::vdt::fast_sincosf(alpha, sina, cosa);

      c.dalpha = alpha;
      c.x = std::fma(k, std::fma(-py, 1.0f - cosa, px * sina), x);
      c.y = std::fma(k, std::fma(px, 1.0f - cosa, py * sina), y);
      c.z = Z;

      c.px = std::fma(-py, sina, px * cosa);
      c.py = std::fma(px, sina, py * cosa);
      c.pz = pz;

      c.fail_flag = 0;
      return 0;
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::mini_propagators

#endif
