#include "ChainFeatures.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

// Chain-level features (contract + guard documentation in ChainFeatures.h).

const char* const kChainFeatNames[kChainFeat] = {"nNodes",
                                                 "nLayers",
                                                 "sumEdgeLogit",
                                                 "minEdgeLogit",
                                                 "meanEdgeLogit",
                                                 "fullFitChi2PerHit",
                                                 "rzLineChi2PerHit",
                                                 "fitKappa",
                                                 "dKappaFitVsMedianT3",
                                                 "ptEst",
                                                 "innermostLayer",
                                                 "layerSpan",
                                                 "nPS",
                                                 "nBarrel",
                                                 "maxJunctionDegProduct",
                                                 "chargeConsistency",
                                                 "maxXyResid",
                                                 "maxRzResid",
                                                 "stdEdgeLogit",
                                                 "maxBridgeChi2",
                                                 "minT3FakeScore",
                                                 "maxT3FakeScore",
                                                 "meanT3PromptScore",
                                                 "minT3DisplacedScore",
                                                 "meanT3DisplacedScore"};

namespace {

constexpr float kEps = 1e-9f;

// Non-finite t3_radius (degenerate ntuple circle fit) -> large finite stand-in;
// identical to the Features.cc convention so member kappaSigned matches node f[0].
inline float cleanRadius(float r) { return std::isfinite(r) ? r : 1e12f; }

// Contract: no NaN/Inf may reach the output (same pass as Features.cc).
void sanitize(std::vector<float>& v) {
  for (float& x : v) {
    if (std::isnan(x))
      x = 0.f;
    else if (std::isinf(x))
      x = (x > 0.f) ? 1e12f : -1e12f;
  }
}

// rotSign of a T3 exactly as Features.cc node f[0]: sign of z of cross(c01, c12) over
// the three MD anchor hits; collinear (cross == 0) counts as +1.
inline float t3RotSign(const LSTEventData& ev, int t) {
  const int m0 = ev.t3_md0[t], m1 = ev.t3_md1[t], m2 = ev.t3_md2[t];
  const float c01x = ev.md_anchor_x[m1] - ev.md_anchor_x[m0];
  const float c01y = ev.md_anchor_y[m1] - ev.md_anchor_y[m0];
  const float c12x = ev.md_anchor_x[m2] - ev.md_anchor_x[m1];
  const float c12y = ev.md_anchor_y[m2] - ev.md_anchor_y[m1];
  const float cross = c01x * c12y - c01y * c12x;
  return (cross >= 0.f) ? 1.f : -1.f;
}

// a2 feature 19 helper: Kasa algebraic circle fit over n <= 6 anchor hits, returning
// chi2/hit = sum (dist - R)^2 / n in cm^2. Numerically IDENTICAL to the full-chain fit
// block below (same centering, same normal equations, same degeneracy guard), just over
// a sub-list. Degenerate (n < 3 or collinear) -> 0, matching the feature-5 convention.
inline double kasaChi2PerHit(const double* x, const double* y, int n) {
  if (n < 3)
    return 0.0;
  double xbar = 0.0, ybar = 0.0;
  for (int k = 0; k < n; ++k) {
    xbar += x[k];
    ybar += y[k];
  }
  xbar /= n;
  ybar /= n;
  double Suu = 0.0, Svv = 0.0, Suv = 0.0, Suw = 0.0, Svw = 0.0, Sw = 0.0;
  for (int k = 0; k < n; ++k) {
    const double u = x[k] - xbar, v = y[k] - ybar;
    const double w = u * u + v * v;
    Suu += u * u;
    Svv += v * v;
    Suv += u * v;
    Suw += u * w;
    Svw += v * w;
    Sw += w;
  }
  const double det = Suu * Svv - Suv * Suv;
  const double scale = Suu + Svv;
  if (!(det > 1e-12 * scale * scale))
    return 0.0;
  const double uc = (Svv * (0.5 * Suw) - Suv * (0.5 * Svw)) / det;
  const double vc = (Suu * (0.5 * Svw) - Suv * (0.5 * Suw)) / det;
  const double R = std::sqrt(std::max(uc * uc + vc * vc + Sw / n, 0.0));
  double chi2 = 0.0;
  for (int k = 0; k < n; ++k) {
    const double du = (x[k] - xbar) - uc, dv = (y[k] - ybar) - vc;
    const double resid = std::sqrt(du * du + dv * dv) - R;
    chi2 += resid * resid;
  }
  return chi2 / n;
}

// LOWER median (sorted element (n-1)/2) -- the K10 convention. Mutates v.
inline float lowerMedian(std::vector<float>& v) {
  const std::size_t mid = (v.size() - 1) / 2;
  std::nth_element(v.begin(), v.begin() + mid, v.end());
  return v[mid];
}

}  // namespace

void computeChainFeatures(const LSTEventData& ev,
                          const ChainGraph& g,
                          const Chains& chains,
                          const EdgeScores& scores,
                          ChainFeatures& out) {
  const int nChains = chains.offsets.empty() ? 0 : static_cast<int>(chains.offsets.size()) - 1;
  out.f.assign(static_cast<std::size_t>(nChains) * kChainFeat, 0.f);

  std::vector<float> scratch;  // reused for the two medians
  std::vector<double> hx, hy, hz, sArc;

  for (int c = 0; c < nChains; ++c) {
    const int ib = chains.offsets[c], ie = chains.offsets[c + 1];
    const int nNodes = ie - ib;
    const int mb = chains.mdOffsets[c], me = chains.mdOffsets[c + 1];
    const int nMD = me - mb;
    const int eb = chains.edgeOffsets[c], ee = chains.edgeOffsets[c + 1];
    const int nE = ee - eb;

    if (nNodes < 1 || nMD < 1)
      continue;  // unreachable by the K6 contract (>= 2 nodes, >= 4 MDs); row stays 0

    // --- 2-4, 18: member weld-edge logit aggregates ---------------------------------
    float sumL = 0.f, minL = 0.f;
    double sumL2 = 0.0;
    if (nE > 0) {
      minL = scores.logOdds[chains.edgeItems[eb]];
      for (int k = eb; k < ee; ++k) {
        const float lo = scores.logOdds[chains.edgeItems[k]];
        sumL += lo;
        sumL2 += static_cast<double>(lo) * static_cast<double>(lo);
        if (lo < minL)
          minL = lo;
      }
    }
    const float meanL = nE > 0 ? sumL / static_cast<float>(nE) : 0.f;
    // Population std of the weld-edge logits (0 for a single weld).
    float stdL = 0.f;
    if (nE > 1) {
      const double var = sumL2 / nE - static_cast<double>(meanL) * static_cast<double>(meanL);
      stdL = static_cast<float>(std::sqrt(std::max(var, 0.0)));
    }

    // --- gather anchor hits (innermost-first mdItems order) ------------------------
    hx.resize(nMD);
    hy.resize(nMD);
    hz.resize(nMD);
    for (int k = 0; k < nMD; ++k) {
      const int md = chains.mdItems[mb + k];
      hx[k] = ev.md_anchor_x[md];
      hy[k] = ev.md_anchor_y[md];
      hz[k] = ev.md_anchor_z[md];
    }

    // --- 5, 7, 16: Kasa algebraic circle fit (xy) over all anchor hits -------------
    // Centered coordinates u = x - xbar, v = y - ybar, w = u^2 + v^2; solve
    //   [Suu Suv; Suv Svv] [uc; vc] = 0.5 [Suw; Svw],  R^2 = uc^2 + vc^2 + Sw/n.
    double fitChi2PerHit = 0.0, fitKappa = 0.0, maxXyResid = 0.0;
    if (nMD >= 3) {
      double xbar = 0.0, ybar = 0.0;
      for (int k = 0; k < nMD; ++k) {
        xbar += hx[k];
        ybar += hy[k];
      }
      xbar /= nMD;
      ybar /= nMD;
      double Suu = 0.0, Svv = 0.0, Suv = 0.0, Suw = 0.0, Svw = 0.0, Sw = 0.0;
      for (int k = 0; k < nMD; ++k) {
        const double u = hx[k] - xbar, v = hy[k] - ybar;
        const double w = u * u + v * v;
        Suu += u * u;
        Svv += v * v;
        Suv += u * v;
        Suw += u * w;
        Svw += v * w;
        Sw += w;
      }
      const double det = Suu * Svv - Suv * Suv;
      const double scale = Suu + Svv;
      if (det > 1e-12 * scale * scale) {  // degenerate (collinear) -> flags stay 0/0
        const double uc = (Svv * (0.5 * Suw) - Suv * (0.5 * Svw)) / det;
        const double vc = (Suu * (0.5 * Svw) - Suv * (0.5 * Suw)) / det;
        const double R = std::sqrt(std::max(uc * uc + vc * vc + Sw / nMD, 0.0));
        double chi2 = 0.0;
        for (int k = 0; k < nMD; ++k) {
          const double du = (hx[k] - xbar) - uc, dv = (hy[k] - ybar) - vc;
          const double resid = std::sqrt(du * du + dv * dv) - R;
          chi2 += resid * resid;
          maxXyResid = std::max(maxXyResid, std::fabs(resid));
        }
        fitChi2PerHit = chi2 / nMD;
        // Chain rotation sign: majority rotation = sign of the summed triplet crosses
        // (innermost-first); sum == 0 counts +1 (header convention).
        double crossSum = 0.0;
        for (int k = 0; k + 2 < nMD; ++k) {
          const double ax = hx[k + 1] - hx[k], ay = hy[k + 1] - hy[k];
          const double bx = hx[k + 2] - hx[k + 1], by = hy[k + 2] - hy[k + 1];
          crossSum += ax * by - ay * bx;
        }
        const double rotSign = (crossSum >= 0.0) ? 1.0 : -1.0;
        fitKappa = rotSign / std::max(R, 1e-6);
      }
    }

    // --- 6, 17: rz straight-line fit z vs s (s = cumulative xy chord length) -------
    double rzChi2PerHit = 0.0, maxRzResid = 0.0;
    {
      sArc.resize(nMD);
      double s = 0.0;
      sArc[0] = 0.0;
      for (int k = 1; k < nMD; ++k) {
        const double dx = hx[k] - hx[k - 1], dy = hy[k] - hy[k - 1];
        s += std::sqrt(dx * dx + dy * dy);
        sArc[k] = s;
      }
      double sbar = 0.0, zbar = 0.0;
      for (int k = 0; k < nMD; ++k) {
        sbar += sArc[k];
        zbar += hz[k];
      }
      sbar /= nMD;
      zbar /= nMD;
      double Sss = 0.0, Ssz = 0.0;
      for (int k = 0; k < nMD; ++k) {
        const double ds = sArc[k] - sbar;
        Sss += ds * ds;
        Ssz += ds * (hz[k] - zbar);
      }
      if (Sss > 1e-12) {  // degenerate abscissa -> flag stays 0
        const double b = Ssz / Sss;
        const double a = zbar - b * sbar;
        double chi2 = 0.0;
        for (int k = 0; k < nMD; ++k) {
          const double r = hz[k] - a - b * sArc[k];
          chi2 += r * r;
          maxRzResid = std::max(maxRzResid, std::fabs(r));
        }
        rzChi2PerHit = chi2 / nMD;
      }
    }

    // --- 8, 9, 15, 20-24: member-T3 aggregates --------------------------------------
    scratch.clear();
    int nPos = 0, nNeg = 0;
    // a2 20-24: upstream t3dnn 3-class outputs, never consumed by the M6-M9 gate.
    float minFakeT3 = 0.f, maxFakeT3 = 0.f, minDispT3 = 0.f;
    double sumPromptT3 = 0.0, sumDispT3 = 0.0;
    for (int k = ib; k < ie; ++k) {
      const int t = chains.items[k];
      const float rs = t3RotSign(ev, t);
      (rs >= 0.f ? nPos : nNeg) += 1;
      scratch.push_back(rs / std::max(cleanRadius(ev.t3_radius[t]), kEps));
      const float fs = ev.t3_fakeScore[t], ps = ev.t3_promptScore[t], ds = ev.t3_displacedScore[t];
      if (k == ib) {
        minFakeT3 = maxFakeT3 = fs;
        minDispT3 = ds;
      } else {
        minFakeT3 = std::min(minFakeT3, fs);
        maxFakeT3 = std::max(maxFakeT3, fs);
        minDispT3 = std::min(minDispT3, ds);
      }
      sumPromptT3 += ps;
      sumDispT3 += ds;
    }
    const float meanPromptT3 = static_cast<float>(sumPromptT3 / nNodes);
    const float meanDispT3 = static_cast<float>(sumDispT3 / nNodes);
    const float medianKappaT3 = lowerMedian(scratch);
    const float dKappaFitVsMedianT3 = static_cast<float>(fitKappa) - medianKappaT3;

    scratch.clear();
    for (int k = ib; k < ie; ++k)
      scratch.push_back(ev.t3_pt[chains.items[k]]);
    const float ptEst = lowerMedian(scratch);

    const long long totPairs = static_cast<long long>(nNodes) * (nNodes - 1) / 2;
    const long long eqPairs = static_cast<long long>(nPos) * (nPos - 1) / 2 + static_cast<long long>(nNeg) * (nNeg - 1) / 2;
    const float chargeConsistency = totPairs > 0 ? static_cast<float>(eqPairs) / static_cast<float>(totPairs) : 1.f;

    // --- 10-13: MD-set detector-category aggregates --------------------------------
    const int innermostLayer = ev.md_layer[chains.mdItems[mb]];
    int minLay = innermostLayer, maxLay = innermostLayer, nPS = 0, nBarrel = 0;
    for (int k = mb; k < me; ++k) {
      const int md = chains.mdItems[k];
      const int lay = ev.md_layer[md];
      minLay = std::min(minLay, lay);
      maxLay = std::max(maxLay, lay);
      nPS += (ev.md_type[md] == 1) ? 1 : 0;
      nBarrel += (lay <= 6) ? 1 : 0;
    }

    // --- 14: max junction degree product over member weld edges --------------------
    // Same shared-key degrees as EdgeFeatures f[12]/f[13] (E1: incidence at the shared
    // MD; E2: incidence at the shared LS).
    long long maxDegProd = 0;
    for (int k = eb; k < ee; ++k) {
      const ChainGraph::Edge& edge = g.edges[chains.edgeItems[k]];
      long long degIn, degOut;
      if (edge.type == 1) {
        const int m = ev.t3_md2[edge.inner];  // == t3_md0[edge.outer]
        degIn = g.mdT3InOffsets[m + 1] - g.mdT3InOffsets[m];
        degOut = g.mdT3OutOffsets[m + 1] - g.mdT3OutOffsets[m];
      } else {
        const int l = ev.t3_lsIdx1[edge.inner];  // == t3_lsIdx0[edge.outer]
        degIn = g.lsT3InOffsets[l + 1] - g.lsT3InOffsets[l];
        degOut = g.lsT3OutOffsets[l + 1] - g.lsT3OutOffsets[l];
      }
      maxDegProd = std::max(maxDegProd, degIn * degOut);
    }

    // --- 19: bridge-circle chi2 over CONSECUTIVE member-T3 pairs --------------------
    // Union of the two T3s' MD anchor hits in chain order (E1 weld -> 5 distinct MDs,
    // E2 weld -> 4); dedup preserves order. Isolates each weld's own circle
    // consistency, which the global fit (feature 5) can absorb.
    double maxBridgeChi2 = 0.0;
    {
      double bx[6], by[6];
      int bmd[6];
      for (int k = ib; k + 1 < ie; ++k) {
        const int ti = chains.items[k], to = chains.items[k + 1];
        const int src[6] = {ev.t3_md0[ti], ev.t3_md1[ti], ev.t3_md2[ti],
                            ev.t3_md0[to], ev.t3_md1[to], ev.t3_md2[to]};
        int n = 0;
        for (int q = 0; q < 6; ++q) {
          bool dup = false;
          for (int p = 0; p < n; ++p)
            if (bmd[p] == src[q]) {
              dup = true;
              break;
            }
          if (dup)
            continue;
          bmd[n] = src[q];
          bx[n] = ev.md_anchor_x[src[q]];
          by[n] = ev.md_anchor_y[src[q]];
          ++n;
        }
        maxBridgeChi2 = std::max(maxBridgeChi2, kasaChi2PerHit(bx, by, n));
      }
    }

    // --- store (order = the frozen contract in ChainFeatures.h) --------------------
    float* f = &out.f[static_cast<std::size_t>(c) * kChainFeat];
    f[0] = static_cast<float>(nNodes);
    f[1] = static_cast<float>(chains.nLayers[c]);
    f[2] = sumL;
    f[3] = minL;
    f[4] = meanL;
    f[5] = static_cast<float>(fitChi2PerHit);
    f[6] = static_cast<float>(rzChi2PerHit);
    f[7] = static_cast<float>(fitKappa);
    f[8] = dKappaFitVsMedianT3;
    f[9] = ptEst;
    f[10] = static_cast<float>(innermostLayer);
    f[11] = static_cast<float>(maxLay - minLay);
    f[12] = static_cast<float>(nPS);
    f[13] = static_cast<float>(nBarrel);
    f[14] = static_cast<float>(maxDegProd);
    f[15] = chargeConsistency;
    f[16] = static_cast<float>(maxXyResid);
    f[17] = static_cast<float>(maxRzResid);
    f[18] = stdL;
    f[19] = static_cast<float>(maxBridgeChi2);
    f[20] = minFakeT3;
    f[21] = maxFakeT3;
    f[22] = meanPromptT3;
    f[23] = minDispT3;
    f[24] = meanDispT3;
  }

  sanitize(out.f);
}
