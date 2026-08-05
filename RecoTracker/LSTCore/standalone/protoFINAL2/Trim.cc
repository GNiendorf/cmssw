#include "Trim.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

// Angle B2 terminal trim (contract + rationale in Trim.h).

namespace {

// chi2 denominator floor: a remaining fit that is numerically perfect (or degenerate, which
// the ChainFeatures guards report as 0) must not produce a NaN ratio. 1e-9 cm^2 is far below
// any physical anchor-hit residual, so the ratio simply becomes "enormous" -- which is the
// correct reading of "the remainder lies exactly on a circle".
constexpr double kChi2Floor = 1e-9;

// MD union over a node sublist, first-appearance order -- the VERBATIM K6Weld.cc rule
// (linear dedup scan over the chain's own slice), plus the layer bitmask popcount that
// produces nLayers.
void mdUnionOf(const LSTEventData& ev, const int* nodes, int n, std::vector<int>& md, int& nLayers) {
  md.clear();
  uint32_t layerMask = 0;
  for (int k = 0; k < n; ++k) {
    const int t3 = nodes[k];
    const int mds[3] = {ev.t3_md0[t3], ev.t3_md1[t3], ev.t3_md2[t3]};
    for (int m : mds) {
      bool seen = false;
      for (std::size_t q = 0; q < md.size() && !seen; ++q)
        seen = (md[q] == m);
      if (!seen) {
        md.push_back(m);
        layerMask |= (1u << ev.md_layer[m]);
      }
    }
  }
  nLayers = 0;
  for (uint32_t b = layerMask; b != 0u; b &= b - 1)
    ++nLayers;
}

}  // namespace

double chainFitChi2Combined(const LSTEventData& ev, const int* mdItems, int nMD, double* xyOut, double* rzOut) {
  static thread_local std::vector<double> hx, hy, hz, sArc;
  if (xyOut)
    *xyOut = 0.0;
  if (rzOut)
    *rzOut = 0.0;
  if (nMD < 1)
    return 0.0;
  hx.resize(nMD);
  hy.resize(nMD);
  hz.resize(nMD);
  for (int k = 0; k < nMD; ++k) {
    const int md = mdItems[k];
    hx[k] = ev.md_anchor_x[md];
    hy[k] = ev.md_anchor_y[md];
    hz[k] = ev.md_anchor_z[md];
  }

  // --- xy: Kasa algebraic circle fit (ChainFeatures.cc feature 5, same guards) ---------
  double xyChi2 = 0.0;
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
    if (det > 1e-12 * scale * scale) {
      const double uc = (Svv * (0.5 * Suw) - Suv * (0.5 * Svw)) / det;
      const double vc = (Suu * (0.5 * Svw) - Suv * (0.5 * Suw)) / det;
      const double R = std::sqrt(std::max(uc * uc + vc * vc + Sw / nMD, 0.0));
      double chi2 = 0.0;
      for (int k = 0; k < nMD; ++k) {
        const double du = (hx[k] - xbar) - uc, dv = (hy[k] - ybar) - vc;
        const double resid = std::sqrt(du * du + dv * dv) - R;
        chi2 += resid * resid;
      }
      xyChi2 = chi2 / nMD;
    }
  }

  // --- rz: straight-line z vs cumulative xy chord length (feature 6, same guard) -------
  double rzChi2 = 0.0;
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
    if (Sss > 1e-12) {
      const double b = Ssz / Sss;
      const double a = zbar - b * sbar;
      double chi2 = 0.0;
      for (int k = 0; k < nMD; ++k) {
        const double r = hz[k] - a - b * sArc[k];
        chi2 += r * r;
      }
      rzChi2 = chi2 / nMD;
    }
  }

  if (xyOut)
    *xyOut = xyChi2;
  if (rzOut)
    *rzOut = rzChi2;
  return xyChi2 + rzChi2;
}

void k6TrimTerminals(const LSTEventData& ev,
                     const EdgeScores& s,
                     float lambdaLen,
                     float ttFactor,
                     int minLayersAfter,
                     float absChi2Min,
                     Chains& chains,
                     std::vector<int8_t>* action,
                     TrimStats& st) {
  const int nChains = chains.offsets.empty() ? 0 : static_cast<int>(chains.offsets.size()) - 1;
  if (action)
    action->assign(nChains, 0);
  if (nChains == 0 || !(ttFactor > 0.f))
    return;

  Chains out;
  out.offsets.assign(1, 0);
  out.mdOffsets.assign(1, 0);
  out.edgeOffsets.assign(1, 0);
  out.items.reserve(chains.items.size());
  out.mdItems.reserve(chains.mdItems.size());
  out.edgeItems.reserve(chains.edgeItems.size());
  out.score.reserve(chains.score.size());
  out.stableKey.reserve(chains.stableKey.size());
  out.nLayers.reserve(chains.nLayers.size());

  std::vector<int> mdIn, mdOut;
  for (int c = 0; c < nChains; ++c) {
    const int ib = chains.offsets[c], ie = chains.offsets[c + 1];
    const int mb = chains.mdOffsets[c], me = chains.mdOffsets[c + 1];
    const int eb = chains.edgeOffsets[c], ee = chains.edgeOffsets[c + 1];
    const int nNodes = ie - ib;

    int drop = 0;  // 0 = keep, 1 = drop innermost, 2 = drop outermost
    int nLayAfter = 0;
    if (nNodes >= 3) {
      ++st.nExamined;
      const double chi2Full = chainFitChi2Combined(ev, &chains.mdItems[mb], me - mb);
      if (chi2Full <= static_cast<double>(absChi2Min)) {
        // Concentrating guard: a chain that already fits well has no parasitic arm to
        // remove; trimming it would be pure track-length cost.
        if (action)
          (*action)[c] = 0;
        out.items.insert(out.items.end(), chains.items.begin() + ib, chains.items.begin() + ie);
        out.edgeItems.insert(out.edgeItems.end(), chains.edgeItems.begin() + eb, chains.edgeItems.begin() + ee);
        out.mdItems.insert(out.mdItems.end(), chains.mdItems.begin() + mb, chains.mdItems.begin() + me);
        out.nLayers.push_back(chains.nLayers[c]);
        out.score.push_back(chains.score[c]);
        out.stableKey.push_back(chains.stableKey[c]);  // P2.5: PRE-trim head, copied not recomputed
        out.offsets.push_back(static_cast<int>(out.items.size()));
        out.mdOffsets.push_back(static_cast<int>(out.mdItems.size()));
        out.edgeOffsets.push_back(static_cast<int>(out.edgeItems.size()));
        continue;
      }
      int nLayI = 0, nLayO = 0;
      mdUnionOf(ev, &chains.items[ib + 1], nNodes - 1, mdIn, nLayI);
      mdUnionOf(ev, &chains.items[ib], nNodes - 1, mdOut, nLayO);
      double rI = -1.0, rO = -1.0;
      if (nLayI >= minLayersAfter)
        rI = chi2Full / std::max(chainFitChi2Combined(ev, mdIn.data(), static_cast<int>(mdIn.size())), kChi2Floor);
      if (nLayO >= minLayersAfter)
        rO = chi2Full / std::max(chainFitChi2Combined(ev, mdOut.data(), static_cast<int>(mdOut.size())), kChi2Floor);
      // Larger improvement wins; inner wins exact ties (deterministic).
      if (rI >= rO && rI > static_cast<double>(ttFactor)) {
        drop = 1;
        nLayAfter = nLayI;
      } else if (rO > rI && rO > static_cast<double>(ttFactor)) {
        drop = 2;
        nLayAfter = nLayO;
      }
    }

    if (action)
      (*action)[c] = static_cast<int8_t>(drop);

    if (drop == 0) {
      // Verbatim copy: byte-identical to K6's emission for this chain.
      out.items.insert(out.items.end(), chains.items.begin() + ib, chains.items.begin() + ie);
      out.edgeItems.insert(out.edgeItems.end(), chains.edgeItems.begin() + eb, chains.edgeItems.begin() + ee);
      out.mdItems.insert(out.mdItems.end(), chains.mdItems.begin() + mb, chains.mdItems.begin() + me);
      out.nLayers.push_back(chains.nLayers[c]);
      out.score.push_back(chains.score[c]);
      out.stableKey.push_back(chains.stableKey[c]);  // P2.5: PRE-trim head, copied not recomputed
    } else {
      const int nib = (drop == 1) ? ib + 1 : ib;
      const int nie = (drop == 1) ? ie : ie - 1;
      const int neb = (drop == 1) ? eb + 1 : eb;
      const int nee = (drop == 1) ? ee : ee - 1;
      out.items.insert(out.items.end(), chains.items.begin() + nib, chains.items.begin() + nie);
      out.edgeItems.insert(out.edgeItems.end(), chains.edgeItems.begin() + neb, chains.edgeItems.begin() + nee);
      const std::vector<int>& md = (drop == 1) ? mdIn : mdOut;
      out.mdItems.insert(out.mdItems.end(), md.begin(), md.end());
      float edgeSum = 0.f;
      for (int k = neb; k < nee; ++k)
        edgeSum += s.logOdds[chains.edgeItems[k]];
      out.nLayers.push_back(nLayAfter);
      out.score.push_back(edgeSum + lambdaLen * static_cast<float>(nLayAfter));
      out.stableKey.push_back(chains.stableKey[c]);  // P2.5: the trimmed chain keeps its PRE-trim identity
      if (drop == 1)
        ++st.nTrimInner;
      else
        ++st.nTrimOuter;
    }
    out.offsets.push_back(static_cast<int>(out.items.size()));
    out.mdOffsets.push_back(static_cast<int>(out.mdItems.size()));
    out.edgeOffsets.push_back(static_cast<int>(out.edgeItems.size()));
  }

  chains = std::move(out);
}

void buildTrimStudyChains(const LSTEventData& ev,
                          const Chains& chains,
                          const EdgeScores& s,
                          float lambdaLen,
                          Chains& out,
                          std::vector<int>& srcChain,
                          std::vector<int8_t>& variant) {
  const int nChains = chains.offsets.empty() ? 0 : static_cast<int>(chains.offsets.size()) - 1;
  out.offsets.assign(1, 0);
  out.mdOffsets.assign(1, 0);
  out.edgeOffsets.assign(1, 0);
  out.items.clear();
  out.mdItems.clear();
  out.edgeItems.clear();
  out.score.clear();
  out.nLayers.clear();
  out.stableKey.clear();
  srcChain.clear();
  variant.clear();

  std::vector<int> md;
  for (int c = 0; c < nChains; ++c) {
    const int ib = chains.offsets[c], ie = chains.offsets[c + 1];
    const int nNodes = ie - ib;
    if (nNodes < 3)
      continue;
    for (int v = 0; v < 3; ++v) {
      const int nib = (v == 1) ? ib + 1 : ib;
      const int nie = (v == 2) ? ie - 1 : ie;
      const int eb = chains.edgeOffsets[c], ee = chains.edgeOffsets[c + 1];
      const int neb = (v == 1) ? eb + 1 : eb;
      const int nee = (v == 2) ? ee - 1 : ee;
      int nLay = 0;
      mdUnionOf(ev, &chains.items[nib], nie - nib, md, nLay);
      out.items.insert(out.items.end(), chains.items.begin() + nib, chains.items.begin() + nie);
      out.edgeItems.insert(out.edgeItems.end(), chains.edgeItems.begin() + neb, chains.edgeItems.begin() + nee);
      out.mdItems.insert(out.mdItems.end(), md.begin(), md.end());
      float edgeSum = 0.f;
      for (int k = neb; k < nee; ++k)
        edgeSum += s.logOdds[chains.edgeItems[k]];
      out.nLayers.push_back(nLay);
      out.score.push_back(edgeSum + lambdaLen * static_cast<float>(nLay));
      out.stableKey.push_back(chains.stableKey[c]);  // P2.5: every variant of one chain
      out.offsets.push_back(static_cast<int>(out.items.size()));
      out.mdOffsets.push_back(static_cast<int>(out.mdItems.size()));
      out.edgeOffsets.push_back(static_cast<int>(out.edgeItems.size()));
      srcChain.push_back(c);
      variant.push_back(static_cast<int8_t>(v));
    }
  }
}
