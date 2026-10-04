#ifndef RecoTracker_MkFitAlpaka_plugins_MkFitAlpakaNavDevice_h
#define RecoTracker_MkFitAlpaka_plugins_MkFitAlpakaNavDevice_h
// Stage C (round 8, lane stagec): PROTOTYPE of steps (a) + (b) of the device missing-hit navigation (doc/stagec.txt 4):
// SimpleNavigationSchool's compatibleLayers(layer, fts, dir) as a per-track device function.
//   (a) a table per DetLayer: the candidate lists of SimpleBarrel/ForwardNavigableLayer::nextLayers, rebuilt on the
//       host from the school's public static lists + TkLayerLess (MkFitAlpakaNavDeviceCheck.cc), as layer indices;
//   (b) the branch bits of nextLayers, the stop rule of SimpleNavigableLayer::wellInside (thickness-extended bounds,
//       crossing-side test) with a double helix, field z at the start point and AnalyticalPropagator's maxDPhi 1.6;
//   the closure as compatibleLayers (<= 150 rounds) on a 64-bit layer set.
#include <cmath>
#include <cstdint>

#include <alpaka/core/Common.hpp>

namespace mkfitdev::nav {
  constexpr int kMaxLayers = 64;
  constexpr int kMaxIdx = 4096;
  // barrel lists
  enum { kNegOuter, kPosOuter, kNegInner, kPosInner, kIB, kIL, kIR, kOB, kOL, kOR, kNLists };
  // forward lists (same slots): sOut, sIn, inner forward, outer barrel, inner barrel, outer forward
  enum { kFOut = 0, kFIn = 1, kFIF = 2, kFOB = 3, kFIB = 4, kFOF = 5 };
  struct Layer {
    int barrel;
    float rz;  // barrel: radius; disk: z
    float halfLength, thickness, rin, rout;
    int off[kNLists], len[kNLists];
  };
  struct Table {
    int nLayers;
    int nIdx;
    Layer layers[kMaxLayers];
    int idx[kMaxIdx];
  };
  struct TrackIn {  // the error-free state at the first hit + the start layers
    float x, y, z, px, py, pz;
    int charge;
    float bz;  // Tesla, at (x, y, z)
    int start[2];  // [0] innermost-hit layer (oppositeToMomentum), [1] outermost-hit layer (alongMomentum)
  };

  // crossing of the cylinder r = RZ (barrel) or the plane z = RZ (disk); false if not reachable within maxDPhi
  ALPAKA_FN_HOST_ACC inline bool helixTo(TrackIn const& t, bool barrel, double RZ, bool along, double out[6]) {
    const double pt = sqrt(double(t.px) * t.px + double(t.py) * t.py);
    const double kappa = -2.99792458e-3 * t.charge * t.bz / pt;
    const double phi0 = atan2(double(t.py), double(t.px));
    const double sgn = along ? 1. : -1.;
    double s = 0;
    if (!barrel) {
      if (t.pz == 0)
        return false;
      s = (RZ - t.z) * pt / t.pz;
      if (s * sgn <= 0)
        return false;
    } else {
      const double xc = t.x - sin(phi0) / kappa, yc = t.y + cos(phi0) / kappa;
      const double rho = 1. / fabs(kappa), d = sqrt(xc * xc + yc * yc);
      if (d > RZ + rho || d < fabs(RZ - rho))
        return false;
      const double a = (RZ * RZ - rho * rho + d * d) / (2 * d);
      const double h = sqrt(fmax(0., RZ * RZ - a * a));
      const double ux = xc / d, uy = yc / d;
      const double period = 2 * M_PI / fabs(kappa);
      double best = 1e30;
      for (int k = 0; k < 2; ++k) {
        const double hh = k ? -h : h;
        const double px = a * ux - hh * uy, py = a * uy + hh * ux;
        const double a0 = atan2(t.y - yc, t.x - xc), a1 = atan2(py - yc, px - xc);
        double sk = (a1 - a0) / kappa;
        while (sk * sgn <= 0)
          sk += sgn * period;
        while (sk * sgn > period)
          sk -= sgn * period;
        if (fabs(sk) < fabs(best))
          best = sk;
      }
      if (best == 1e30)
        return false;
      s = best;
    }
    if (fabs(kappa * s) > 1.6)
      return false;
    const double phi = phi0 + kappa * s;
    out[0] = t.x + (sin(phi) - sin(phi0)) / kappa;
    out[1] = t.y - (cos(phi) - cos(phi0)) / kappa;
    out[2] = t.z + s * t.pz / pt;
    out[3] = cos(phi);
    out[4] = sin(phi);
    out[5] = t.pz / pt;
    return true;
  }

  // SimpleNavigableLayer::wellInside for one candidate: pushed / well inside
  ALPAKA_FN_HOST_ACC inline void decide(Layer const& L, TrackIn const& t, bool along, bool& pushed, bool& well) {
    pushed = well = false;
    double b[6];
    if (!helixTo(t, L.barrel != 0, L.rz, along, b))
      return;
    if ((t.x * b[0] + t.y * b[1]) < 0 || (t.x * b[0] + t.y * b[1] + t.z * b[2]) < 0)
      return;
    const double tperp = sqrt(b[3] * b[3] + b[4] * b[4]);
    if (L.barrel) {
      const float deltaZ = 0.5f * L.thickness * fabs(b[5]) / tperp;
      const double az = fabs(b[2]);
      pushed = az < L.halfLength + deltaZ;
      well = az < L.halfLength - deltaZ;
    } else {
      const float rpos = sqrt(b[0] * b[0] + b[1] * b[1]);
      const float deltaR = 0.5f * L.thickness * tperp / fabs(b[5]);
      pushed = L.rin - deltaR < rpos && rpos < L.rout + deltaR;
      well = L.rin + deltaR < rpos && rpos < L.rout - deltaR;
    }
  }

  ALPAKA_FN_HOST_ACC inline bool wellInsideList(
      Table const& T, Layer const& from, int list, TrackIn const& t, bool along, uint64_t& result) {
    for (int i = 0; i < from.len[list]; ++i) {
      const int c = T.idx[from.off[list] + i];
      bool pushed, well;
      decide(T.layers[c], t, along, pushed, well);
      if (pushed)
        result |= (uint64_t(1) << c);
      if (well)
        return true;
    }
    return false;
  }

  // SimpleBarrel/ForwardNavigableLayer::nextLayers(fts, dir) as a layer set
  ALPAKA_FN_HOST_ACC inline uint64_t nextLayers(Table const& T, int l, TrackIn const& t, bool along) {
    Layer const& L = T.layers[l];
    const bool inOutB = t.x * t.px + t.y * t.py > 0;
    const bool inOutF = t.pz * t.z > 0;
    const bool xb = (along && inOutB) || (!along && !inOutB);
    const bool xf = (along && inOutF) || (!along && !inOutF);
    uint64_t r = 0;
    if (L.barrel) {
      const bool signZ = ((t.pz > 0) && !along) || (!(t.pz > 0) && along);
      if (xb && xf)
        wellInsideList(T, L, signZ ? kNegOuter : kPosOuter, t, along, r);
      else if (!xb && !xf)
        wellInsideList(T, L, signZ ? kPosInner : kNegInner, t, along, r);
      else if (!xb && xf) {
        wellInsideList(T, L, kIB, t, along, r);
        wellInsideList(T, L, signZ ? kIL : kIR, t, along, r);
        wellInsideList(T, L, signZ ? kOL : kOR, t, along, r);
      } else {
        wellInsideList(T, L, signZ ? kIL : kIR, t, along, r);
        wellInsideList(T, L, kOB, t, along, r);
      }
    } else {
      if (xf && xb)
        wellInsideList(T, L, kFOut, t, along, r);
      else if (!xf && !xb)
        wellInsideList(T, L, kFIn, t, along, r);
      else if (!xf && xb) {
        wellInsideList(T, L, kFIF, t, along, r);
        wellInsideList(T, L, kFOB, t, along, r);
      } else {
        wellInsideList(T, L, kFIB, t, along, r);
        wellInsideList(T, L, kFOF, t, along, r);
      }
    }
    return r;
  }

  // compatibleLayers: the closure of nextLayers (SimpleNavigableLayer::compatibleLayers, <= 150 rounds)
  ALPAKA_FN_HOST_ACC inline uint64_t compatibleLayers(Table const& T, int start, TrackIn const& t, bool along) {
    uint64_t collect = 0, toTry = nextLayers(T, start, t, along);
    int counter = 0;
    while (toTry != 0 && (counter++) <= 150) {
      uint64_t next = 0;
      for (int l = 0; l < T.nLayers; ++l) {
        const uint64_t bit = uint64_t(1) << l;
        if (!(toTry & bit) || (collect & bit))
          continue;
        collect |= bit;
        next |= nextLayers(T, l, t, along);
      }
      toTry = next;
    }
    return collect;
  }
}  // namespace mkfitdev::nav

#endif
