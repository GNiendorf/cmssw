#include "Extend.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>

#include "Trim.h"  // chainFitChi2Combined (the shared chain-fit arithmetic)

// Contract + rationale in Extend.h.

namespace {

constexpr int kMaxLayer = 12;   // md_layer is 1-6 barrel, 7-11 endcap
constexpr int kPhiBins = 64;    // free-MD index granularity
constexpr double kPi = 3.14159265358979323846;

// Chain fit parameters over the chain's MD anchor hits. The arithmetic is the VERBATIM
// ChainFeatures.cc / Trim.cc pair (Kasa algebraic circle in mean-centred coordinates,
// straight z vs cumulative xy chord length), only kept instead of thrown away so a new
// point can be tested against it without refitting.
struct ChainFit {
  bool ok = false;
  double cx = 0, cy = 0, R = 0;  // circle centre in ABSOLUTE coordinates, radius
  double a = 0, b = 0;           // z = a + b * s
  double sLast = 0;              // cumulative chord of the outermost MD (s of MD0 == 0)
  double xIn = 0, yIn = 0, zIn = 0;
  double xOut = 0, yOut = 0, zOut = 0;
  double chi2 = 0;               // combined chi2/hit, identical to chainFitChi2Combined
};

bool buildChainFit(const LSTEventData& ev, const int* mdItems, int nMD, ChainFit& f) {
  if (nMD < 3)
    return false;
  static thread_local std::vector<double> hx, hy, hz, sArc;
  hx.resize(nMD);
  hy.resize(nMD);
  hz.resize(nMD);
  for (int k = 0; k < nMD; ++k) {
    const int md = mdItems[k];
    hx[k] = ev.md_anchor_x[md];
    hy[k] = ev.md_anchor_y[md];
    hz[k] = ev.md_anchor_z[md];
  }

  // --- xy: Kasa algebraic circle fit (same guards as chainFitChi2Combined) -----------
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
  if (!(det > 1e-12 * scale * scale))
    return false;
  const double uc = (Svv * (0.5 * Suw) - Suv * (0.5 * Svw)) / det;
  const double vc = (Suu * (0.5 * Svw) - Suv * (0.5 * Suw)) / det;
  const double R = std::sqrt(std::max(uc * uc + vc * vc + Sw / nMD, 0.0));
  double xyChi2 = 0.0;
  for (int k = 0; k < nMD; ++k) {
    const double du = (hx[k] - xbar) - uc, dv = (hy[k] - ybar) - vc;
    const double resid = std::sqrt(du * du + dv * dv) - R;
    xyChi2 += resid * resid;
  }
  xyChi2 /= nMD;

  // --- rz: straight-line z vs cumulative xy chord length -----------------------------
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
  if (!(Sss > 1e-12))
    return false;
  const double bb = Ssz / Sss;
  const double aa = zbar - bb * sbar;
  double rzChi2 = 0.0;
  for (int k = 0; k < nMD; ++k) {
    const double r = hz[k] - aa - bb * sArc[k];
    rzChi2 += r * r;
  }
  rzChi2 /= nMD;

  f.ok = true;
  f.cx = xbar + uc;
  f.cy = ybar + vc;
  f.R = R;
  f.a = aa;
  f.b = bb;
  f.sLast = sArc[nMD - 1];
  f.xIn = hx[0];
  f.yIn = hy[0];
  f.zIn = hz[0];
  f.xOut = hx[nMD - 1];
  f.yOut = hy[nMD - 1];
  f.zOut = hz[nMD - 1];
  f.chi2 = xyChi2 + rzChi2;
  return true;
}

inline double dist3(double x0, double y0, double z0, double x1, double y1, double z1) {
  const double dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline int phiBin(double phi) {
  int b = static_cast<int>((phi + kPi) * (kPhiBins / (2.0 * kPi)));
  if (b < 0)
    b = 0;
  if (b >= kPhiBins)
    b = kPhiBins - 1;
  return b;
}

}  // namespace

void extendChains(const LSTEventData& ev,
                  const std::vector<int>& acceptedChains,
                  const ExtendParams& p,
                  std::vector<char>& claimedHit,
                  Chains& chains,
                  ExtendStats& st) {
  const int nChains = chains.offsets.empty() ? 0 : static_cast<int>(chains.offsets.size()) - 1;
  if (p.mode <= 0 || nChains == 0)
    return;
  const int nMDall = static_cast<int>(ev.md_anchorHitIdx.size());
  const int nHit = static_cast<int>(claimedHit.size());

  // ---- free-MD index: (layer, phi bin) -> MD rows whose BOTH hits are unclaimed -------
  // Rebuilt once per event. ~56k MDs on PU200, so the build is negligible next to the
  // 265 ms/evt pipeline.
  std::vector<std::vector<int>> bucket(static_cast<std::size_t>(kMaxLayer) * kPhiBins);
  for (int m = 0; p.segLinked == 0 && m < nMDall; ++m) {
    if (ev.md_isPLS[m])
      continue;
    const int ha = ev.md_anchorHitIdx[m], hb = ev.md_otherHitIdx[m];
    if (ha < 0 || hb < 0 || ha >= nHit || hb >= nHit)
      continue;
    if (claimedHit[ha] || claimedHit[hb])
      continue;
    const int L = ev.md_layer[m];
    if (L < 1 || L >= kMaxLayer)
      continue;
    const double phi = std::atan2(ev.md_anchor_y[m], ev.md_anchor_x[m]);
    bucket[static_cast<std::size_t>(L) * kPhiBins + phiBin(phi)].push_back(m);
  }

  // ---- MD -> MD adjacency through EXISTING LineSegments (-EXS 1) ----------------------
  // ls_mdIdx0 is the inner MD, ls_mdIdx1 the outer one. An LS exists only where LST's own
  // module map and segment cuts already accepted the pair, so walking these edges asks the
  // detector "is this a legal next hit?" instead of inventing a window.
  std::vector<int> nbrOutOff, nbrOutItems, nbrInOff, nbrInItems;
  if (p.segLinked >= 1) {
    const int nLS = static_cast<int>(ev.ls_mdIdx0.size());
    nbrOutOff.assign(nMDall + 1, 0);
    nbrInOff.assign(nMDall + 1, 0);
    for (int l = 0; l < nLS; ++l) {
      if (l < static_cast<int>(ev.ls_isPLS.size()) && ev.ls_isPLS[l])
        continue;
      const int a = ev.ls_mdIdx0[l], b = ev.ls_mdIdx1[l];
      if (a < 0 || b < 0 || a >= nMDall || b >= nMDall)
        continue;
      ++nbrOutOff[a + 1];
      ++nbrInOff[b + 1];
    }
    for (int m = 0; m < nMDall; ++m) {
      nbrOutOff[m + 1] += nbrOutOff[m];
      nbrInOff[m + 1] += nbrInOff[m];
    }
    nbrOutItems.resize(nbrOutOff[nMDall]);
    nbrInItems.resize(nbrInOff[nMDall]);
    std::vector<int> wo(nbrOutOff.begin(), nbrOutOff.end() - 1), wi(nbrInOff.begin(), nbrInOff.end() - 1);
    for (int l = 0; l < nLS; ++l) {
      if (l < static_cast<int>(ev.ls_isPLS.size()) && ev.ls_isPLS[l])
        continue;
      const int a = ev.ls_mdIdx0[l], b = ev.ls_mdIdx1[l];
      if (a < 0 || b < 0 || a >= nMDall || b >= nMDall)
        continue;
      nbrOutItems[wo[a]++] = b;
      nbrInItems[wi[b]++] = a;
    }
  }

  // ---- per-chain extension, K9 best-first order --------------------------------------
  // Accepted rows are visited in the order K9 produced, so a higher-ranked chain always
  // gets first refusal on a free MD.
  std::vector<std::vector<int>> addOut(nChains), addIn(nChains);
  const double win = static_cast<double>(p.window);
  const double win2 = win * win;
  std::vector<int> mdTest;

  for (int c : acceptedChains) {
    if (c < 0 || c >= nChains)
      continue;
    if (chains.nLayers[c] < p.minLayers)
      continue;  // K10 emits no TC for these; do not resurrect them
    const int mb = chains.mdOffsets[c], me = chains.mdOffsets[c + 1];
    const int nMD = me - mb;
    if (nMD < 3)
      continue;
    ChainFit fit;
    if (!buildChainFit(ev, &chains.mdItems[mb], nMD, fit)) {
      ++st.nNoFit;
      continue;
    }
    if (p.maxChi2 > 0.f && fit.chi2 > static_cast<double>(p.maxChi2)) {
      ++st.nRejFit;
      continue;  // no trustworthy trajectory to extrapolate along
    }
    ++st.nChains;

    uint32_t layerMask = 0;
    for (int k = mb; k < me; ++k)
      layerMask |= (1u << ev.md_layer[chains.mdItems[k]]);

    // end 0 = outer (append), end 1 = inner (prepend)
    for (int end = 0; end < 2; ++end) {
      const bool outer = (end == 0);
      if (outer && !(p.mode == 1 || p.mode == 3))
        continue;
      if (!outer && !(p.mode == 2 || p.mode == 3))
        continue;
      // Terminal reference, updated after every accepted MD so that maxPerEnd > 1 walks
      // outward one detector layer at a time. The FIT itself is never re-derived (the
      // acceptance window can therefore only ever get tighter, never wider).
      int tMd = outer ? chains.mdItems[me - 1] : chains.mdItems[mb];
      double tx = outer ? fit.xOut : fit.xIn;
      double ty = outer ? fit.yOut : fit.yIn;
      double tz = outer ? fit.zOut : fit.zIn;
      double sT = outer ? fit.sLast : 0.0;
      for (int rep = 0; rep < p.maxPerEnd; ++rep) {
        const int tLay = ev.md_layer[tMd];
        // Opposite terminal: the monotone "further along the trajectory" reference. A
        // pt > 0.8 GeV track in the tracker never loops, so 3D distance from the far end
        // increases monotonically outward -- this works identically in barrel and endcap
        // without any r-vs-z special casing.
        const double ox = outer ? fit.xIn : fit.xOut;
        const double oy = outer ? fit.yIn : fit.yOut;
        const double oz = outer ? fit.zIn : fit.zOut;
        const double dRef = dist3(ox, oy, oz, tx, ty, tz);
        const double rT = std::sqrt(tx * tx + ty * ty);
        const double phiT = std::atan2(ty, tx);
        // Angular half-window implied by the 3D distance cap: chord >= (2/pi)*r*|dphi|
        // for |dphi| <= pi, so |dphi| <= (pi/2) * maxDist / r is a strict bound whenever
        // the candidate radius is >= r (always true going outward, and the inner-end
        // scan simply falls back to the whole layer when the bound saturates).
        double dphiWin = kPi;
        if (rT > 1.0)
          dphiWin = std::min(kPi, 0.5 * kPi * static_cast<double>(p.maxDist) / rT);

        int bestMd = -1;
        // P2.5 determinism: the stable tie operand of the extension argmin. The pre-P2.5
        // rule kept the FIRST of an exact residual tie, walking neighbours in ascending
        // LineSegment / MD index -- and LST hands those out by atomicAdd, so the rule
        // permuted run to run. Production (ChainArbitrate.h) decides the tie on the
        // candidate MD's own hit rows instead: hitKey = (anchorHit << 32) | outerHit,
        // SMALLER wins. ha/hb are already loaded in the candidate test, so it is free.
        uint64_t bestHitKey = 0;
        double bestRes = 1e30, secondRes = 1e30;
        // One candidate test, shared by both search modes.
        auto testCand = [&](int m) {
          if (m < 0 || m >= nMDall || ev.md_isPLS[m])
            return;
          const int L = ev.md_layer[m];
          if (L < 1 || L >= kMaxLayer)
            return;
          const int jump = outer ? (L - tLay) : (tLay - L);
          if (jump < 1 || jump > p.maxJump)
            return;
          if (layerMask & (1u << L))
            return;  // the chain already occupies this layer: no length to gain
          const int ha = ev.md_anchorHitIdx[m], hb = ev.md_otherHitIdx[m];
          if (ha < 0 || hb < 0 || ha >= nHit || hb >= nHit)
            return;
          if (claimedHit[ha] || claimedHit[hb])
            return;  // owned by a delivered object, or taken by an earlier extension
          const double mx = ev.md_anchor_x[m], my = ev.md_anchor_y[m], mz = ev.md_anchor_z[m];
          if (dist3(tx, ty, tz, mx, my, mz) > static_cast<double>(p.maxDist))
            return;
          if (dist3(ox, oy, oz, mx, my, mz) <= dRef)
            return;  // not beyond the terminal -> not an extension
          ++st.nCand;
          // Residual of the candidate anchor hit to the chain's own fit.
          const double dcx = mx - fit.cx, dcy = my - fit.cy;
          const double rxy = std::sqrt(dcx * dcx + dcy * dcy) - fit.R;
          const double chord = std::sqrt((mx - tx) * (mx - tx) + (my - ty) * (my - ty));
          const double sC = outer ? (sT + chord) : (sT - chord);
          const double rrz = mz - (fit.a + fit.b * sC);
          double res;
          if (p.rzWindow > 0.f) {
            // Split test: the two residuals carry very different intrinsic resolutions, so
            // they get their own windows. The RANKING key stays the xy residual, which is
            // the precisely measured one.
            if (std::fabs(rxy) > win || std::fabs(rrz) > static_cast<double>(p.rzWindow))
              return;
            res = std::fabs(rxy);
          } else {
            res = std::sqrt(rxy * rxy + rrz * rrz);
            if (res > win)
              return;
          }
          const uint64_t hitKey = (static_cast<uint64_t>(static_cast<uint32_t>(ha)) << 32) |
                                  static_cast<uint64_t>(static_cast<uint32_t>(hb));
          const bool better = (res < bestRes) || (res == bestRes && bestMd >= 0 && hitKey < bestHitKey);
          if (better) {
            secondRes = bestRes;
            bestRes = res;
            bestMd = m;
            bestHitKey = hitKey;
          } else if (res < secondRes) {
            secondRes = res;
          }
        };

        if (p.segLinked >= 1) {
          // Walk only MDs the detector already declared segment-compatible with the
          // terminal MD. Typically a handful of neighbours, so no spatial index is needed.
          const std::vector<int>& off = outer ? nbrOutOff : nbrInOff;
          const std::vector<int>& items = outer ? nbrOutItems : nbrInItems;
          for (int k = off[tMd]; k < off[tMd + 1]; ++k)
            testCand(items[k]);
        } else {
          for (int L = 1; L < kMaxLayer; ++L) {
            const int jump = outer ? (L - tLay) : (tLay - L);
            if (jump < 1 || jump > p.maxJump)
              continue;
            if (layerMask & (1u << L))
              continue;
            const int b0 = phiBin(phiT - dphiWin), b1 = phiBin(phiT + dphiWin);
            const bool allBins = (dphiWin >= kPi);
            for (int q = 0; q < kPhiBins; ++q) {
              if (!allBins) {
                const bool inside = (b0 <= b1) ? (q >= b0 && q <= b1) : (q >= b0 || q <= b1);
                if (!inside)
                  continue;
              }
              for (int m : bucket[static_cast<std::size_t>(L) * kPhiBins + q])
                testCand(m);
            }
          }
        }
        if (bestMd < 0)
          break;

        // Ambiguity guard (-EXU): if a runner-up free MD fits nearly as well, the winner
        // is a coin flip. Refuse rather than guess.
        if (p.uniqMargin > 0.f && secondRes < 1e29 &&
            (secondRes - bestRes) < static_cast<double>(p.uniqMargin)) {
          ++st.nRejUniq;
          break;
        }

        // Conservative second guard: the REFIT combined chi2/hit over the enlarged MD
        // list must stay within chi2Factor of the original (with an absolute floor of
        // window^2 so a numerically perfect chain is not barred from ever extending).
        mdTest.clear();
        if (!outer)
          mdTest.push_back(bestMd);
        for (const int& x : addIn[c])
          mdTest.push_back(x);
        for (int k = mb; k < me; ++k)
          mdTest.push_back(chains.mdItems[k]);
        for (const int& x : addOut[c])
          mdTest.push_back(x);
        if (outer)
          mdTest.push_back(bestMd);
        const double chi2New = chainFitChi2Combined(ev, mdTest.data(), static_cast<int>(mdTest.size()));
        if (chi2New > static_cast<double>(p.chi2Factor) * std::max(fit.chi2, win2)) {
          ++st.nRejChi2;
          break;
        }

        claimedHit[ev.md_anchorHitIdx[bestMd]] = 1;
        claimedHit[ev.md_otherHitIdx[bestMd]] = 1;
        layerMask |= (1u << ev.md_layer[bestMd]);
        if (outer) {
          addOut[c].push_back(bestMd);
          ++st.nExtOuter;
        } else {
          // Prepended MDs are stored inner-most last here and reversed on emission.
          addIn[c].push_back(bestMd);
          ++st.nExtInner;
        }
        // Walk the terminal to the MD just accepted so a further repetition steps one
        // more detector layer out (or in) rather than re-testing from the same anchor.
        {
          const double nx = ev.md_anchor_x[bestMd], ny = ev.md_anchor_y[bestMd];
          const double chordAcc = std::sqrt((nx - tx) * (nx - tx) + (ny - ty) * (ny - ty));
          sT = outer ? (sT + chordAcc) : (sT - chordAcc);
          tMd = bestMd;
          tx = nx;
          ty = ny;
          tz = ev.md_anchor_z[bestMd];
        }
      }
    }
    if (!addOut[c].empty() || !addIn[c].empty())
      ++st.nExtChains;
  }

  if (st.nExtOuter == 0 && st.nExtInner == 0)
    return;

  // ---- rebuild the MD CSR (node list / edge list / score copied verbatim) -------------
  std::vector<int> newMdOff(nChains + 1, 0), newMdItems;
  newMdItems.reserve(chains.mdItems.size() + static_cast<std::size_t>(st.nExtOuter + st.nExtInner));
  for (int c = 0; c < nChains; ++c) {
    for (std::size_t q = addIn[c].size(); q-- > 0;)
      newMdItems.push_back(addIn[c][q]);
    for (int k = chains.mdOffsets[c]; k < chains.mdOffsets[c + 1]; ++k)
      newMdItems.push_back(chains.mdItems[k]);
    for (int x : addOut[c])
      newMdItems.push_back(x);
    newMdOff[c + 1] = static_cast<int>(newMdItems.size());
    chains.nLayers[c] += static_cast<int>(addIn[c].size()) + static_cast<int>(addOut[c].size());
  }
  chains.mdOffsets.swap(newMdOff);
  chains.mdItems.swap(newMdItems);
}
