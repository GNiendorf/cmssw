#ifndef RecoTracker_MkFitAlpaka_interface_math_DetPlaneTest_h
#define RecoTracker_MkFitAlpaka_interface_math_DetPlaneTest_h

// Stage C (c), lane stagec round 10, FIRST PIECE of the device per-det test of the missing-hit navigation (doc/stagec.txt
// section 8): what GeomDetCompatibilityChecker + AnalyticalPropagator + Chi2MeasurementEstimator(-3 sigma) do for one
// det plane, portable (ALPAKA_FN_HOST_ACC), in double:
//   - the straight-line sagitta pre-check (maxSagitta 2 cm, minTolerance 0.5 cm, bounds widened by the tolerance);
//   - the helix (field along z at the start point, the TSOS transverse curvature) to the plane: Newton on the 3D path
//     length from the straight-line solution, the first crossing in the propagation direction;
//   - AnalyticalPropagator's maxDPhi rule (|rho * s_T| <= 1.6);
//   - the local position errors from the END curvilinear covariance (x_T, y_T block: u = z x t / |z x t|, v = t x u),
//     projected along the track direction onto the plane's local axes;
//   - RectangularPlaneBounds::inside(p, err, -3).
// The curvilinear error transport from the start is pca::curvilinearJacobian + pca::similarity5 (PcaToBeamLine.h, the
// same code as the device PCA); the host check ([outconv DETTEST]) runs both the end-covariance-from-host and the full
// device chain.

#include <cmath>
#include <limits>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

namespace mkfitdev::navdev {

  struct HelixStart {
    double x[3];  // global position (cm)
    double p[3];  // global momentum (GeV)
    double rho;   // transverse curvature (1/cm), as FreeTrajectoryState::transverseCurvature()
  };
  struct DetPlane {
    double pos[3];
    double ax[3], ay[3], az[3];  // local x, y, z (= normal) axes in global coordinates
    double halfWidth, halfLength, halfThickness;
  };
  struct PlaneHit {
    bool valid = false;  // a crossing in the propagation direction within maxDPhi
    double s = 0;        // 3D path length (signed)
    double x[3]{}, t[3]{};
    double lx = 0, ly = 0, lz = 0;  // local position
  };

  ALPAKA_FN_HOST_ACC inline double dot3(const double* a, const double* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  }

  // sagitta pre-check of GeomDetCompatibilityChecker::isCompatible: false = rejected without propagation
  ALPAKA_FN_HOST_ACC inline bool sagittaOk(HelixStart const& st, DetPlane const& pl, bool along, double maxSag, double minTol2) {
    if (maxSag <= 0)
      return true;
    double d[3] = {st.x[0] - pl.pos[0], st.x[1] - pl.pos[1], st.x[2] - pl.pos[2]};
    const double pz = dot3(pl.az, st.p);
    const double dS = -dot3(pl.az, d) / pz;
    if ((along && dS < 0) || (!along && dS > 0) || !(std::abs(dS) < std::numeric_limits<double>::infinity()))
      return true;  // no straight-line crossing: the stock check does not reject
    const double g[3] = {st.x[0] + dS * st.p[0], st.x[1] + dS * st.p[1], st.x[2] + dS * st.p[2]};
    const double tpath2 = (g[0] - st.x[0]) * (g[0] - st.x[0]) + (g[1] - st.x[1]) * (g[1] - st.x[1]);
    const double sagitta = 0.5 * std::abs(tpath2 * st.rho);
    if (!(sagitta < maxSag))
      return true;
    const double tol = std::sqrt(sagitta * sagitta > minTol2 ? sagitta * sagitta : minTol2);
    const double r[3] = {g[0] - pl.pos[0], g[1] - pl.pos[1], g[2] - pl.pos[2]};
    const double lx = dot3(pl.ax, r), ly = dot3(pl.ay, r), lz = dot3(pl.az, r);
    // RectangularPlaneBounds::inside(p, err, 1): inside(p) or |z| < ht && |x| < hw + tol && |y| < hl + tol
    const bool in0 = std::abs(lz) < pl.halfThickness && std::abs(lx) < pl.halfWidth && std::abs(ly) < pl.halfLength;
    return in0 || (std::abs(lz) < pl.halfThickness && std::abs(lx) < pl.halfWidth + tol &&
                   std::abs(ly) < pl.halfLength + tol);
  }

  // the helix to the plane (AnalyticalPropagator::propagateWithPath on a plane, error-free part)
  ALPAKA_FN_HOST_ACC inline PlaneHit helixToPlane(HelixStart const& st, DetPlane const& pl, bool along, double maxDPhi) {
    PlaneHit h;
    const double pmag = std::sqrt(dot3(st.p, st.p));
    const double pt = std::sqrt(st.p[0] * st.p[0] + st.p[1] * st.p[1]);
    const double sinT = pt / pmag, cosT = st.p[2] / pmag;
    const double phi0 = std::atan2(st.p[1], st.p[0]);
    const double rho = st.rho;
    auto pos = [&](double s, double* x, double* t) {
      const double sT = s * sinT;
      const double ph = phi0 + rho * sT;
      const double sp = std::sin(ph), cp = std::cos(ph);
      if (std::abs(rho * sT) < 1e-9) {
        x[0] = st.x[0] + sT * std::cos(phi0);
        x[1] = st.x[1] + sT * std::sin(phi0);
      } else {
        x[0] = st.x[0] + (sp - std::sin(phi0)) / rho;
        x[1] = st.x[1] - (cp - std::cos(phi0)) / rho;
      }
      x[2] = st.x[2] + s * cosT;
      t[0] = cp * sinT;
      t[1] = sp * sinT;
      t[2] = cosT;
    };
    // straight-line start
    const double t0[3] = {st.p[0] / pmag, st.p[1] / pmag, st.p[2] / pmag};
    const double d0[3] = {pl.pos[0] - st.x[0], pl.pos[1] - st.x[1], pl.pos[2] - st.x[2]};
    const double nt0 = dot3(pl.az, t0);
    if (nt0 == 0)
      return h;
    double s = dot3(pl.az, d0) / nt0;
    double x[3], t[3];
    bool conv = false;
    for (int it = 0; it < 30; ++it) {
      pos(s, x, t);
      const double r[3] = {x[0] - pl.pos[0], x[1] - pl.pos[1], x[2] - pl.pos[2]};
      const double f = dot3(pl.az, r), fp = dot3(pl.az, t);
      if (fp == 0)
        break;
      const double ds = f / fp;
      s -= ds;
      if (std::abs(ds) < 1e-10) {
        conv = true;
        break;
      }
    }
    if (!conv || (along && s < 0) || (!along && s > 0))
      return h;
    pos(s, x, t);
    if (std::abs(rho * s * sinT) > maxDPhi)
      return h;
    h.valid = true;
    h.s = s;
    for (int i = 0; i < 3; ++i) {
      h.x[i] = x[i];
      h.t[i] = t[i];
    }
    const double r[3] = {x[0] - pl.pos[0], x[1] - pl.pos[1], x[2] - pl.pos[2]};
    h.lx = dot3(pl.ax, r);
    h.ly = dot3(pl.ay, r);
    h.lz = dot3(pl.az, r);
    return h;
  }

  // local (x, y) position variances on the plane from the curvilinear covariance block of (x_T, y_T) at the crossing
  // (cTT = [C33, C34, C44]), the track direction t at the crossing and the plane axes
  ALPAKA_FN_HOST_ACC inline void localPositionErrors(
      DetPlane const& pl, const double* t, const double* cTT, double& sxx, double& syy) {
    const double tT = std::sqrt(t[0] * t[0] + t[1] * t[1]);
    const double u[3] = {-t[1] / tT, t[0] / tT, 0.};
    const double v[3] = {t[1] * u[2] - t[2] * u[1], t[2] * u[0] - t[0] * u[2], t[0] * u[1] - t[1] * u[0]};
    const double nt = dot3(pl.az, t);
    const double nu = dot3(pl.az, u), nv = dot3(pl.az, v);
    // shift by a u + b v, back to the plane along t
    const double jxu = dot3(pl.ax, u) - nu * dot3(pl.ax, t) / nt, jxv = dot3(pl.ax, v) - nv * dot3(pl.ax, t) / nt;
    const double jyu = dot3(pl.ay, u) - nu * dot3(pl.ay, t) / nt, jyv = dot3(pl.ay, v) - nv * dot3(pl.ay, t) / nt;
    sxx = jxu * jxu * cTT[0] + 2 * jxu * jxv * cTT[1] + jxv * jxv * cTT[2];
    syy = jyu * jyu * cTT[0] + 2 * jyu * jyv * cTT[1] + jyv * jyv * cTT[2];
  }

  // RectangularPlaneBounds::inside(p, err, nSigma < 0)
  ALPAKA_FN_HOST_ACC inline bool insideShrunk(DetPlane const& pl, PlaneHit const& h, double sxx, double syy, double nSigma) {
    return std::abs(h.lz) < pl.halfThickness && std::abs(h.lx) < pl.halfWidth + std::sqrt(sxx) * nSigma &&
           std::abs(h.ly) < pl.halfLength + std::sqrt(syy) * nSigma;
  }

}  // namespace mkfitdev::navdev

#endif
