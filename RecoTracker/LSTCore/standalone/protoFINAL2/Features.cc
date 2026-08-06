#include "Features.h"

#include <algorithm>
#include <cmath>

namespace {

  constexpr float kEps = 1e-9f;
  constexpr float kPi = 3.14159265358979323846f;

  // Wrap an angle difference into [-pi, pi].
  inline float wrapPhi(float d) {
    while (d > kPi)
      d -= 2.f * kPi;
    while (d < -kPi)
      d += 2.f * kPi;
    return d;
  }

  // deltaPhi(a, b) = a - b wrapped into [-pi, pi].
  inline float deltaPhi(float a, float b) { return wrapPhi(a - b); }

  // t3_radius straight from the ntuple can be Inf/NaN for a degenerate circle fit;
  // map to a large finite stand-in so no downstream feature goes non-finite.
  inline float cleanRadius(float r) { return std::isfinite(r) ? r : 1e12f; }

  // The epsilon guards keep all arithmetic finite for finite inputs; this pass only
  // fires if the ntuple itself carries non-finite values (contract: no NaN/Inf may
  // reach the output). NaN -> 0, +/-Inf -> +/-1e12.
  void sanitize(std::vector<float>& v) {
    for (float& x : v) {
      if (std::isnan(x))
        x = 0.f;
      else if (std::isinf(x))
        x = (x > 0.f) ? 1e12f : -1e12f;
    }
  }

}  // namespace

void computeNodeFeatures(const LSTEventData& ev, NodeFeatures& out) {
  const int nT3 = static_cast<int>(ev.t3_lsIdx0.size());
  out.f.assign(static_cast<size_t>(nT3) * kNodeFeat, 0.f);

  for (int t = 0; t < nT3; ++t) {
    const int m0 = ev.t3_md0[t], m1 = ev.t3_md1[t], m2 = ev.t3_md2[t];
    const float x0 = ev.md_anchor_x[m0], y0 = ev.md_anchor_y[m0], z0 = ev.md_anchor_z[m0];
    const float x1 = ev.md_anchor_x[m1], y1 = ev.md_anchor_y[m1], z1 = ev.md_anchor_z[m1];
    const float x2 = ev.md_anchor_x[m2], y2 = ev.md_anchor_y[m2], z2 = ev.md_anchor_z[m2];

    const float c01x = x1 - x0, c01y = y1 - y0, c01z = z1 - z0;
    const float c12x = x2 - x1, c12y = y2 - y1, c12z = z2 - z1;
    const float c02x = x2 - x0, c02y = y2 - y0, c02z = z2 - z0;

    // rotSign = sign of z of cross(c01, c12); collinear (cross == 0) counts as +1.
    const float cross = c01x * c12y - c01y * c12x;
    const float rotSign = (cross >= 0.f) ? 1.f : -1.f;

    const float radius = cleanRadius(ev.t3_radius[t]);
    const float kappaSigned = rotSign / std::max(radius, kEps);
    const float log10R = std::log10(std::max(radius, 1e-3f));

    const float c02xy = std::sqrt(c02x * c02x + c02y * c02y);
    const float tanLambda = c02z / std::max(c02xy, kEps);
    // eta of the displacement vector c02: asinh(dz / drt).
    const float chordEta = std::asinh(c02z / std::max(c02xy, kEps));

    const float dphi01 = deltaPhi(std::atan2(c01y, c01x), std::atan2(c12y, c12x));

    const float rt0 = std::sqrt(x0 * x0 + y0 * y0);
    const float rt1 = std::sqrt(x1 * x1 + y1 * y1);
    const float rt2 = std::sqrt(x2 * x2 + y2 * y2);

    const int l0 = ev.md_layer[m0], l1 = ev.md_layer[m1], l2 = ev.md_layer[m2];
    const int nBarrel = (l0 <= 6 ? 1 : 0) + (l1 <= 6 ? 1 : 0) + (l2 <= 6 ? 1 : 0);
    const int nPS = (ev.md_type[m0] == 1 ? 1 : 0) + (ev.md_type[m1] == 1 ? 1 : 0) + (ev.md_type[m2] == 1 ? 1 : 0);

    float* f = &out.f[static_cast<size_t>(t) * kNodeFeat];
    f[0] = kappaSigned;
    f[1] = log10R;
    f[2] = tanLambda;
    f[3] = chordEta;
    f[4] = dphi01;
    f[5] = c01z;
    f[6] = c12z;
    f[7] = rt1 - rt0;
    f[8] = rt2 - rt1;
    f[9] = static_cast<float>(l0);
    f[10] = static_cast<float>(nBarrel);
    f[11] = static_cast<float>(nPS);
    f[12] = ev.t3_fakeScore[t];
  }

  sanitize(out.f);
}

void computeEdgeFeatures(const LSTEventData& ev, const ChainGraph& g, const NodeFeatures& nf, EdgeFeatures& out) {
  const int nT3 = static_cast<int>(ev.t3_lsIdx0.size());

  // Per-T3 chord angles, computed once (each T3 appears in many edges).
  // theta(c) = atan2(|c_xy|, c_z) in [0, pi] -- the ONE fixed rz-angle definition:
  // kinkTheta = theta(inner c12) - theta(outer c01), in [-pi, pi], no wrap needed.
  std::vector<float> phi01(nT3), phi12(nT3), th01(nT3), th12(nT3);
  for (int t = 0; t < nT3; ++t) {
    const int m0 = ev.t3_md0[t], m1 = ev.t3_md1[t], m2 = ev.t3_md2[t];
    const float c01x = ev.md_anchor_x[m1] - ev.md_anchor_x[m0];
    const float c01y = ev.md_anchor_y[m1] - ev.md_anchor_y[m0];
    const float c01z = ev.md_anchor_z[m1] - ev.md_anchor_z[m0];
    const float c12x = ev.md_anchor_x[m2] - ev.md_anchor_x[m1];
    const float c12y = ev.md_anchor_y[m2] - ev.md_anchor_y[m1];
    const float c12z = ev.md_anchor_z[m2] - ev.md_anchor_z[m1];
    phi01[t] = std::atan2(c01y, c01x);
    phi12[t] = std::atan2(c12y, c12x);
    th01[t] = std::atan2(std::sqrt(c01x * c01x + c01y * c01y), c01z);
    th12[t] = std::atan2(std::sqrt(c12x * c12x + c12y * c12y), c12z);
  }

  const size_t nE = g.edges.size();
  out.f.assign(nE * kEdgeFeat, 0.f);

  for (size_t e = 0; e < nE; ++e) {
    const int inner = g.edges[e].inner;
    const int outer = g.edges[e].outer;
    const int etype = g.edges[e].type;
    const float* fi = &nf.f[static_cast<size_t>(inner) * kNodeFeat];
    const float* fo = &nf.f[static_cast<size_t>(outer) * kNodeFeat];

    const float kIn = fi[0], kOut = fo[0];
    const float dKappa = kIn - kOut;
    const float dKappaRel = std::fabs(dKappa) / (std::fabs(kIn) + std::fabs(kOut) + kEps);
    // sign(kappaSigned) == rotSign (positive divisor), so this is rotSign agreement.
    const float chargeAgree = ((kIn >= 0.f) == (kOut >= 0.f)) ? 1.f : 0.f;

    const float cdx = ev.t3_centerX[inner] - ev.t3_centerX[outer];
    const float cdy = ev.t3_centerY[inner] - ev.t3_centerY[outer];
    const float centerDist = std::sqrt(cdx * cdx + cdy * cdy);
    const float rIn = cleanRadius(ev.t3_radius[inner]);
    const float rOut = cleanRadius(ev.t3_radius[outer]);
    const float centerDistRel = centerDist / std::max(0.5f * (rIn + rOut), kEps);

    // Shared MD: E1 -> the shared middle MD; E2 -> first MD of the shared LS.
    int sharedMd;
    int degIn, degOut;
    if (etype == 1) {
      const int m = ev.t3_md2[inner];  // == t3_md0[outer]
      sharedMd = m;
      degIn = g.mdT3InOffsets[m + 1] - g.mdT3InOffsets[m];
      degOut = g.mdT3OutOffsets[m + 1] - g.mdT3OutOffsets[m];
    } else {
      const int l = ev.t3_lsIdx1[inner];  // == t3_lsIdx0[outer]
      sharedMd = ev.ls_mdIdx0[l];
      degIn = g.lsT3InOffsets[l + 1] - g.lsT3InOffsets[l];
      degOut = g.lsT3OutOffsets[l + 1] - g.lsT3OutOffsets[l];
    }
    const int sharedLayer = ev.md_layer[sharedMd];

    float* f = &out.f[e * kEdgeFeat];
    f[0] = static_cast<float>(etype);
    f[1] = dKappa;
    f[2] = dKappaRel;
    f[3] = chargeAgree;
    f[4] = fi[2] - fo[2];
    f[5] = deltaPhi(phi12[inner], phi01[outer]);
    f[6] = th12[inner] - th01[outer];
    f[7] = centerDist;
    f[8] = centerDistRel;
    f[9] = static_cast<float>(sharedLayer);
    f[10] = static_cast<float>(ev.md_type[sharedMd]);
    f[11] = (sharedLayer <= 6) ? 1.f : 0.f;
    f[12] = static_cast<float>(degIn);
    f[13] = static_cast<float>(degOut);
  }

  sanitize(out.f);
}
