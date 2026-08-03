#ifndef RecoTracker_LSTCore_src_alpaka_ChainGate_h
#define RecoTracker_LSTCore_src_alpaka_ChainGate_h

#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainConfig.h"
#include "RecoTracker/LSTCore/interface/ChainEdgesSoA.h"
#include "RecoTracker/LSTCore/interface/ChainIncidenceSoA.h"
#include "RecoTracker/LSTCore/interface/ChainNodesSoA.h"
#include "RecoTracker/LSTCore/interface/ChainsSoA.h"
#include "RecoTracker/LSTCore/interface/MiniDoubletsSoA.h"
#include "RecoTracker/LSTCore/interface/ModulesSoA.h"
#include "RecoTracker/LSTCore/interface/SegmentsSoA.h"
#include "RecoTracker/LSTCore/interface/TripletsSoA.h"

#include "Chain2NetworkWeights.h"
#include "Chain3NetworkWeights.h"
#include "ChainEdges.h"
#include "ChainWeld.h"
#include "NeuralNetwork.h"

// Chain-tracking gate, phase P2.2 of standalone/prototype/P2_PORT_MAP.md.
//
// Stages implemented here:
//   K7a ChainFeatures - the frozen 25-float chain row plus the chain transverse DCA
//   K7b ChainGate     - the 3-class head (fake / prompt-true / displaced-true), raw logits
//   K7c ChainGateKill - the -G 6 branch kill with the frozen margins and band deltas
//
// Reference implementation: prototype/ChainFeatures.cc, prototype/PixelAttach.cc k8ChainDcaXY,
// prototype/ChainInference.cc runChainInference3, and the -G 6 block of prototype/main.cc.
// K7a keeps every fit accumulation in double exactly as the reference does; the MD list is read
// from global memory instead of being staged in a vector, and each pass recomputes what it needs
// in the reference's operation order so every partial sum is bit-identical.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  namespace chaingate {
    constexpr float kEps = 1e-9f;
    // prototype/main.cc: the K10 TC pt convention, t3_pt = radius * k2Rinv1GeVf * 2, and the
    // ntuple writer's eta of the OUTERMOST anchor hit. Both are trained-in definitions.
    ALPAKA_FN_ACC ALPAKA_FN_INLINE float t3Pt(TripletsConst triplets, uint32_t t3) {
      return triplets.radius()[t3] * k2Rinv1GeVf * 2;
    }
  }  // namespace chaingate

  // t3_eta as the ntuple writer computes it (standalone/code/core/lst_math.h Hit::eta over the
  // anchor hit of the triplet's LAST MD): sign(z) * acosh(r3 / rt).
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainT3Eta(TAcc const& acc, MiniDoubletsConst mds, unsigned int md2) {
    float const x = mds.anchorX()[md2], y = mds.anchorY()[md2], z = mds.anchorZ()[md2];
    float const r3 = alpaka::math::sqrt(acc, x * x + y * y + z * z);
    float const rt = alpaka::math::sqrt(acc, x * x + y * y);
    float const sign = static_cast<float>((z > 0.f) - (z < 0.f));
    return sign * alpaka::math::acosh(acc, r3 / rt);
  }

  // rotSign of a triplet exactly as the node feature f[0] and prototype/ChainFeatures.cc
  // t3RotSign: sign of the z component of cross(c01, c12); collinear counts as +1.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainT3RotSign(
      TripletsConst triplets, SegmentsConst segments, MiniDoubletsConst mds, uint32_t t3) {
    unsigned int m0, m1, m2;
    chainNodeMDs(triplets, segments, t3, m0, m1, m2);
    float const c01x = mds.anchorX()[m1] - mds.anchorX()[m0];
    float const c01y = mds.anchorY()[m1] - mds.anchorY()[m0];
    float const c12x = mds.anchorX()[m2] - mds.anchorX()[m1];
    float const c12y = mds.anchorY()[m2] - mds.anchorY()[m1];
    float const cross = c01x * c12y - c01y * c12x;
    return (cross >= 0.f) ? 1.f : -1.f;
  }

  // Kasa algebraic circle fit over n <= 6 anchor hits, chi2/hit in cm^2 (prototype/ChainFeatures.cc
  // kasaChi2PerHit; feature 19's per-bridge fit). Numerically identical to the full-chain block.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE double chainKasaChi2PerHit(TAcc const& acc,
                                                            double const* x,
                                                            double const* y,
                                                            int n) {
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
      double const u = x[k] - xbar, v = y[k] - ybar;
      double const w = u * u + v * v;
      Suu += u * u;
      Svv += v * v;
      Suv += u * v;
      Suw += u * w;
      Svw += v * w;
      Sw += w;
    }
    double const det = Suu * Svv - Suv * Suv;
    double const scale = Suu + Svv;
    if (!(det > 1e-12 * scale * scale))
      return 0.0;
    double const uc = (Svv * (0.5 * Suw) - Suv * (0.5 * Svw)) / det;
    double const vc = (Suu * (0.5 * Svw) - Suv * (0.5 * Suw)) / det;
    double const R = alpaka::math::sqrt(acc, chainMaxd(uc * uc + vc * vc + Sw / n, 0.0));
    double chi2 = 0.0;
    for (int k = 0; k < n; ++k) {
      double const du = (x[k] - xbar) - uc, dv = (y[k] - ybar) - vc;
      double const resid = alpaka::math::sqrt(acc, du * du + dv * dv) - R;
      chi2 += resid * resid;
    }
    return chi2 / n;
  }

  // ------------------------------------------------------------------------------------------
  // K7a. The 25 frozen chain features plus the chain dcaXY.
  struct ChainFeaturesKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  ChainEdgesConst edges,
                                  ChainIncidenceConst mdIncidence,
                                  ChainIncidenceConst lsIncidence,
                                  ChainItemsConst items,
                                  Chains chains) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());

      for (uint32_t c : cms::alpakatools::uniform_elements(acc, nChains)) {
        uint32_t const off = chains.nodeOffset()[c];
        int const nNodes = chains.nNodes()[c];
        int const nMD = chains.nMDs()[c];
        int const nE = nNodes - 1;
        uint32_t const mdBase = 3u * off;
        uint32_t const* mdList = &items.mdItems()[mdBase];

        if (nNodes < 1 || nMD < 1)
          continue;  // unreachable by the K6 contract; the row stays zeroed

        // --- 2-4, 18: member weld-edge logit aggregates -------------------------------------
        float sumL = 0.f, minL = 0.f;
        double sumL2 = 0.0;
        if (nE > 0) {
          minL = edges.logOdds()[items.edgeItems()[off]];
          for (int k = 0; k < nE; ++k) {
            float const lo = edges.logOdds()[items.edgeItems()[off + k]];
            sumL += lo;
            sumL2 += static_cast<double>(lo) * static_cast<double>(lo);
            if (lo < minL)
              minL = lo;
          }
        }
        float const meanL = nE > 0 ? sumL / static_cast<float>(nE) : 0.f;
        float stdL = 0.f;
        if (nE > 1) {
          double const var = sumL2 / nE - static_cast<double>(meanL) * static_cast<double>(meanL);
          stdL = static_cast<float>(alpaka::math::sqrt(acc, chainMaxd(var, 0.0)));
        }

        // --- 5, 7, 16 and the dcaXY: Kasa circle fit over all anchor hits --------------------
        double fitChi2PerHit = 0.0, fitKappa = 0.0, maxXyResid = 0.0;
        float dcaXY = 1e9f;
        bool circleOk = false;
        if (nMD >= 3) {
          double xbar = 0.0, ybar = 0.0;
          for (int k = 0; k < nMD; ++k) {
            xbar += mds.anchorX()[mdList[k]];
            ybar += mds.anchorY()[mdList[k]];
          }
          xbar /= nMD;
          ybar /= nMD;
          double Suu = 0.0, Svv = 0.0, Suv = 0.0, Suw = 0.0, Svw = 0.0, Sw = 0.0;
          for (int k = 0; k < nMD; ++k) {
            double const u = mds.anchorX()[mdList[k]] - xbar, v = mds.anchorY()[mdList[k]] - ybar;
            double const w = u * u + v * v;
            Suu += u * u;
            Svv += v * v;
            Suv += u * v;
            Suw += u * w;
            Svw += v * w;
            Sw += w;
          }
          double const det = Suu * Svv - Suv * Suv;
          double const scale = Suu + Svv;
          if (det > 1e-12 * scale * scale) {  // degenerate (collinear) -> flags stay 0 / 0
            circleOk = true;
            double const uc = (Svv * (0.5 * Suw) - Suv * (0.5 * Svw)) / det;
            double const vc = (Suu * (0.5 * Svw) - Suv * (0.5 * Suw)) / det;
            double const R = alpaka::math::sqrt(acc, chainMaxd(uc * uc + vc * vc + Sw / nMD, 0.0));
            double chi2 = 0.0;
            for (int k = 0; k < nMD; ++k) {
              double const du = (mds.anchorX()[mdList[k]] - xbar) - uc;
              double const dv = (mds.anchorY()[mdList[k]] - ybar) - vc;
              double const resid = alpaka::math::sqrt(acc, du * du + dv * dv) - R;
              chi2 += resid * resid;
              maxXyResid = chainMaxd(maxXyResid, alpaka::math::abs(acc, resid));
            }
            fitChi2PerHit = chi2 / nMD;
            double crossSum = 0.0;
            for (int k = 0; k + 2 < nMD; ++k) {
              double const ax = static_cast<double>(mds.anchorX()[mdList[k + 1]]) -
                                static_cast<double>(mds.anchorX()[mdList[k]]);
              double const ay = static_cast<double>(mds.anchorY()[mdList[k + 1]]) -
                                static_cast<double>(mds.anchorY()[mdList[k]]);
              double const bx = static_cast<double>(mds.anchorX()[mdList[k + 2]]) -
                                static_cast<double>(mds.anchorX()[mdList[k + 1]]);
              double const by = static_cast<double>(mds.anchorY()[mdList[k + 2]]) -
                                static_cast<double>(mds.anchorY()[mdList[k + 1]]);
              crossSum += ax * by - ay * bx;
            }
            double const rotSign = (crossSum >= 0.0) ? 1.0 : -1.0;
            fitKappa = rotSign / chainMaxd(R, 1e-6);

            // prototype/PixelAttach.cc k8ChainDcaXY reuses this exact fit (same accumulation
            // order, same guard) and only adds the absolute centre and the origin distance.
            double const cx = xbar + uc, cy = ybar + vc;
            dcaXY = static_cast<float>(
                alpaka::math::abs(acc, alpaka::math::sqrt(acc, cx * cx + cy * cy) - R));
          }
        }
        if (!circleOk) {
          // Degenerate (or nMD < 3) fit: straight-line limit, the perpendicular distance from the
          // origin to the line through the innermost and outermost anchor hits. nMD < 2 is
          // unreachable by the K6 contract and is never IP-compatible.
          if (nMD < 2) {
            dcaXY = 1e9f;
          } else {
            double const x1 = mds.anchorX()[mdList[0]], y1 = mds.anchorY()[mdList[0]];
            double const x2 = mds.anchorX()[mdList[nMD - 1]], y2 = mds.anchorY()[mdList[nMD - 1]];
            double const len = alpaka::math::sqrt(acc, (x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
            dcaXY = (len < 1e-9) ? 1e9f
                                 : static_cast<float>(alpaka::math::abs(acc, x1 * y2 - x2 * y1) / len);
          }
        }

        // --- 6, 17: rz straight-line fit z vs s (s = cumulative xy chord length) -------------
        double rzChi2PerHit = 0.0, maxRzResid = 0.0;
        {
          double s = 0.0, sbar = 0.0, zbar = 0.0;
          for (int k = 0; k < nMD; ++k) {
            if (k > 0) {
              double const dx = static_cast<double>(mds.anchorX()[mdList[k]]) -
                              static_cast<double>(mds.anchorX()[mdList[k - 1]]);
              double const dy = static_cast<double>(mds.anchorY()[mdList[k]]) -
                              static_cast<double>(mds.anchorY()[mdList[k - 1]]);
              s += alpaka::math::sqrt(acc, dx * dx + dy * dy);
            }
            sbar += s;
            zbar += mds.anchorZ()[mdList[k]];
          }
          sbar /= nMD;
          zbar /= nMD;
          double Sss = 0.0, Ssz = 0.0;
          s = 0.0;
          for (int k = 0; k < nMD; ++k) {
            if (k > 0) {
              double const dx = static_cast<double>(mds.anchorX()[mdList[k]]) -
                              static_cast<double>(mds.anchorX()[mdList[k - 1]]);
              double const dy = static_cast<double>(mds.anchorY()[mdList[k]]) -
                              static_cast<double>(mds.anchorY()[mdList[k - 1]]);
              s += alpaka::math::sqrt(acc, dx * dx + dy * dy);
            }
            double const ds = s - sbar;
            Sss += ds * ds;
            Ssz += ds * (mds.anchorZ()[mdList[k]] - zbar);
          }
          if (Sss > 1e-12) {  // degenerate abscissa -> flag stays 0
            double const b = Ssz / Sss;
            double const a = zbar - b * sbar;
            double chi2 = 0.0;
            s = 0.0;
            for (int k = 0; k < nMD; ++k) {
              if (k > 0) {
                double const dx = static_cast<double>(mds.anchorX()[mdList[k]]) -
                              static_cast<double>(mds.anchorX()[mdList[k - 1]]);
                double const dy = static_cast<double>(mds.anchorY()[mdList[k]]) -
                              static_cast<double>(mds.anchorY()[mdList[k - 1]]);
                s += alpaka::math::sqrt(acc, dx * dx + dy * dy);
              }
              double const r = mds.anchorZ()[mdList[k]] - a - b * s;
              chi2 += r * r;
              maxRzResid = chainMaxd(maxRzResid, alpaka::math::abs(acc, r));
            }
            rzChi2PerHit = chi2 / nMD;
          }
        }

        // --- 8, 9, 15, 20-24: member-T3 aggregates -------------------------------------------
        int nPos = 0, nNeg = 0;
        float minFakeT3 = 0.f, maxFakeT3 = 0.f, minDispT3 = 0.f;
        double sumPromptT3 = 0.0, sumDispT3 = 0.0;
        for (int k = 0; k < nNodes; ++k) {
          uint32_t const t3 = nodes.tripletIndex()[items.nodeItems()[off + k]];
          float const rs = chainT3RotSign(triplets, segments, mds, t3);
          (rs >= 0.f ? nPos : nNeg) += 1;
          float const fs = triplets.fakeScore()[t3];
          float const ps = triplets.promptScore()[t3];
          float const ds = triplets.displacedScore()[t3];
          if (k == 0) {
            minFakeT3 = maxFakeT3 = fs;
            minDispT3 = ds;
          } else {
            minFakeT3 = chainMinf(minFakeT3, fs);
            maxFakeT3 = chainMaxf(maxFakeT3, fs);
            minDispT3 = chainMinf(minDispT3, ds);
          }
          sumPromptT3 += ps;
          sumDispT3 += ds;
        }
        float const meanPromptT3 = static_cast<float>(sumPromptT3 / nNodes);
        float const meanDispT3 = static_cast<float>(sumDispT3 / nNodes);

        // LOWER median = the ((n - 1) / 2)-th order statistic, which is exactly the value
        // std::nth_element leaves at that position. Selected by rank counting so no scratch
        // array (and no sort) is needed for a list this short.
        int const mid = (nNodes - 1) / 2;
        float medianKappaT3 = 0.f, ptEst = 0.f;
        for (int pass = 0; pass < 2; ++pass) {
          float chosen = 0.f;
          bool found = false;
          for (int i = 0; i < nNodes && !found; ++i) {
            uint32_t const t3i = nodes.tripletIndex()[items.nodeItems()[off + i]];
            float vi;
            if (pass == 0) {
              float const rs = chainT3RotSign(triplets, segments, mds, t3i);
              vi = rs / chainMaxf(chainCleanRadius(triplets.radius()[t3i]), chaingate::kEps);
            } else {
              vi = chaingate::t3Pt(triplets, t3i);
            }
            int nLess = 0, nEq = 0;
            for (int j = 0; j < nNodes; ++j) {
              uint32_t const t3j = nodes.tripletIndex()[items.nodeItems()[off + j]];
              float vj;
              if (pass == 0) {
                float const rs = chainT3RotSign(triplets, segments, mds, t3j);
                vj = rs / chainMaxf(chainCleanRadius(triplets.radius()[t3j]), chaingate::kEps);
              } else {
                vj = chaingate::t3Pt(triplets, t3j);
              }
              if (vj < vi)
                ++nLess;
              else if (vj == vi)
                ++nEq;
            }
            if (nLess <= mid && mid < nLess + nEq) {
              chosen = vi;
              found = true;
            }
          }
          if (pass == 0)
            medianKappaT3 = chosen;
          else
            ptEst = chosen;
        }
        float const dKappaFitVsMedianT3 = static_cast<float>(fitKappa) - medianKappaT3;

        long long const totPairs = static_cast<long long>(nNodes) * (nNodes - 1) / 2;
        long long const eqPairs =
            static_cast<long long>(nPos) * (nPos - 1) / 2 + static_cast<long long>(nNeg) * (nNeg - 1) / 2;
        float const chargeConsistency =
            totPairs > 0 ? static_cast<float>(eqPairs) / static_cast<float>(totPairs) : 1.f;

        // --- 10-13: MD-set detector-category aggregates --------------------------------------
        int const innermostLayer = chainMdLayer(modules, mds, mdList[0]);
        int minLay = innermostLayer, maxLay = innermostLayer, nPS = 0, nBarrel = 0;
        for (int k = 0; k < nMD; ++k) {
          int const lay = chainMdLayer(modules, mds, mdList[k]);
          minLay = (lay < minLay) ? lay : minLay;
          maxLay = (lay > maxLay) ? lay : maxLay;
          nPS += chainMdIsPS(modules, mds, mdList[k]);
          nBarrel += (lay <= 6) ? 1 : 0;
        }

        // --- 14: max junction degree product over the member weld edges ----------------------
        long long maxDegProd = 0;
        for (int k = 0; k < nE; ++k) {
          uint32_t const e = items.edgeItems()[off + k];
          // The junction is the inner node's "in" side, whose dense incidence keys K1c stored on
          // the node; same values the K5 edge features used, one load instead of three.
          uint32_t const innerNode = edges.inner()[e];
          long long degIn, degOut;
          if (edges.type()[e] == 1u) {
            uint32_t const m = nodes.mdKeyIn()[innerNode];
            degIn = mdIncidence.t3InOffsets()[m + 1u] - mdIncidence.t3InOffsets()[m];
            degOut = mdIncidence.t3OutOffsets()[m + 1u] - mdIncidence.t3OutOffsets()[m];
          } else {
            uint32_t const l = nodes.lsKeyIn()[innerNode];
            degIn = lsIncidence.t3InOffsets()[l + 1u] - lsIncidence.t3InOffsets()[l];
            degOut = lsIncidence.t3OutOffsets()[l + 1u] - lsIncidence.t3OutOffsets()[l];
          }
          long long const prod = degIn * degOut;
          maxDegProd = (prod > maxDegProd) ? prod : maxDegProd;
        }

        // --- 19: bridge-circle chi2 over consecutive member-T3 pairs -------------------------
        double maxBridgeChi2 = 0.0;
        {
          double bx[6], by[6];
          unsigned int bmd[6];
          for (int k = 0; k + 1 < nNodes; ++k) {
            uint32_t const ti = nodes.tripletIndex()[items.nodeItems()[off + k]];
            uint32_t const to = nodes.tripletIndex()[items.nodeItems()[off + k + 1]];
            unsigned int i0, i1, i2, o0, o1, o2;
            chainNodeMDs(triplets, segments, ti, i0, i1, i2);
            chainNodeMDs(triplets, segments, to, o0, o1, o2);
            unsigned int const src[6] = {i0, i1, i2, o0, o1, o2};
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
              bx[n] = mds.anchorX()[src[q]];
              by[n] = mds.anchorY()[src[q]];
              ++n;
            }
            maxBridgeChi2 = chainMaxd(maxBridgeChi2, chainKasaChi2PerHit(acc, bx, by, n));
          }
        }

        // --- store (order = the frozen contract in ChainsSoA.h) ------------------------------
        float f[Params_ChainFeat::kFeatures];
        f[0] = static_cast<float>(nNodes);
        f[1] = static_cast<float>(chains.nLayers()[c]);
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

        CMS_UNROLL_LOOP
        for (int i = 0; i < Params_ChainFeat::kFeatures; ++i)
          chains.features()[c][i] = chainSanitize(f[i]);
        chains.dcaXY()[c] = dcaXY;
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // K7b + K7c, fused. The head is 25 -> 32 -> 32 -> 3 with the kSrcCol gather (column -1 = the
  // chain dcaXY) and NO softmax: every downstream decision is on the logit MARGINS.
  struct ChainGateKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  ChainItemsConst items,
                                  Chains chains,
                                  ChainConfig cfg) const {
      static_assert(dnn::chain3mlp::kInput == Params_ChainFeat::kFeatures,
                    "Chain3NetworkWeights.h input size does not match the frozen feature contract");
      static_assert(dnn::chainmlp::kInput == Params_ChainFeat::kFeatures,
                    "Chain2NetworkWeights.h input size does not match the frozen feature contract");
      static_assert(dnn::chain3mlp::kOutput == 3, "the chain3 gate must have 3 outputs");

      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());
      bool const zOn = cfg.etaBandActive();

      for (uint32_t c : cms::alpakatools::uniform_elements(acc, nChains)) {
        float const dca = chains.dcaXY()[c];

        // --- K7b: the 3-class head ----------------------------------------------------------
        float x[dnn::chain3mlp::kInput];
        for (int i = 0; i < dnn::chain3mlp::kInput; ++i) {
          int const col = dnn::chain3mlp::kSrcCol[i];
          float v = (col < 0) ? dca : chains.features()[c][col];
          if (dnn::chain3mlp::kLog10p1[i])
            v = alpaka::math::log10(acc, 1.f + v);
          v = chainMinf(chainMaxf(v, dnn::chain3mlp::kClipLo[i]), dnn::chain3mlp::kClipHi[i]);
          x[i] = (v - dnn::chain3mlp::kFeatMean[i]) / dnn::chain3mlp::kFeatStd[i];
        }

        float x1[dnn::chain3mlp::kHidden];
        float x2[dnn::chain3mlp::kHidden];
        float z[dnn::chain3mlp::kOutput];

        linear_layer<dnn::chain3mlp::kInput, dnn::chain3mlp::kHidden>(
            x, x1, dnn::chain3mlp::wgt_l1, dnn::chain3mlp::bias_l1);
        relu_activation<dnn::chain3mlp::kHidden>(x1);
        linear_layer<dnn::chain3mlp::kHidden, dnn::chain3mlp::kHidden>(
            x1, x2, dnn::chain3mlp::wgt_l2, dnn::chain3mlp::bias_l2);
        relu_activation<dnn::chain3mlp::kHidden>(x2);
        linear_layer<dnn::chain3mlp::kHidden, dnn::chain3mlp::kOutput>(
            x2, z, dnn::chain3mlp::wgt_out, dnn::chain3mlp::bias_out);

        float const mP = z[1] - z[0];
        float const mD = z[2] - z[0];
        float const mX = chainMaxf(z[1], z[2]) - z[0];

        chains.zFake()[c] = z[0];
        chains.zPrompt()[c] = z[1];
        chains.zDisp()[c] = z[2];
        chains.marginP()[c] = mP;
        chains.marginD()[c] = mD;
        chains.marginX()[c] = mX;

        // --- K7b': the a2 2-CLASS head (prototype/ChainInference.cc runChainInference) ---------
        // Not a gate, not a kill, not in the -BK 1 order key. Its only live consumer is attach
        // pair feature 11, and the reference computes it for EVERY chain from the same feature row
        // regardless of -G mode, so it is evaluated here beside the 3-class head. It reads the
        // full 25-column contract in order (no kSrcCol gather, kInput == kFeatures).
        {
          float y[dnn::chainmlp::kInput];
          for (int i = 0; i < dnn::chainmlp::kInput; ++i) {
            float v = chains.features()[c][i];
            if (dnn::chainmlp::kLog10p1[i])
              v = alpaka::math::log10(acc, 1.f + v);
            v = chainMinf(chainMaxf(v, dnn::chainmlp::kClipLo[i]), dnn::chainmlp::kClipHi[i]);
            y[i] = (v - dnn::chainmlp::kFeatMean[i]) / dnn::chainmlp::kFeatStd[i];
          }
          float y1[dnn::chainmlp::kHidden];
          float y2[dnn::chainmlp::kHidden];
          linear_layer<dnn::chainmlp::kInput, dnn::chainmlp::kHidden>(
              y, y1, dnn::chainmlp::wgt_l1, dnn::chainmlp::bias_l1);
          relu_activation<dnn::chainmlp::kHidden>(y1);
          linear_layer<dnn::chainmlp::kHidden, dnn::chainmlp::kHidden>(
              y1, y2, dnn::chainmlp::wgt_l2, dnn::chainmlp::bias_l2);
          relu_activation<dnn::chainmlp::kHidden>(y2);
          float logit2 = dnn::chainmlp::bias_out;
          for (int j = 0; j < dnn::chainmlp::kHidden; ++j)
            logit2 += y2[j] * dnn::chainmlp::wgt_out[j];
          chains.gateLogit2()[c] = logit2;
        }

        // --- K7c: the -G 6 branch kill ------------------------------------------------------
        int const nL = chains.nLayers()[c];
        uint32_t const off = chains.nodeOffset()[c];

        int8_t const branch = nL <= 4 ? (dca >= chainMaxf(cfg.dcaSplit, cfg.t4ExemptDcaMin) ? 1 : 0)
                                      : (dca < cfg.dcaSplit ? 2 : 3);
        chains.branch()[c] = branch;

        // Band membership on the chain's K10 eta (the innermost member T3), so a chain is
        // tightened in exactly the |eta| band its TC is counted in.
        bool inZ = false;
        if (cfg.zEta2 > cfg.zEta1 && chains.nNodes()[c] > 0) {
          uint32_t const t3In = nodes.tripletIndex()[items.nodeItems()[off]];
          unsigned int m0, m1, m2;
          chainNodeMDs(triplets, segments, t3In, m0, m1, m2);
          float const ae = alpaka::math::abs(acc, chainT3Eta(acc, mds, m2));
          inZ = ae >= cfg.zEta1 && ae < cfg.zEta2;
        }

        // -ZIL 1: the band levers only reach chains whose innermost MD sits in layer 1.
        bool ilOk = true;
        if (cfg.zInLayer1) {
          ilOk = false;
          if (chains.nMDs()[c] > 0)
            ilOk = (chains.features()[c][10] == 1.f);
        }

        bool const zz = zOn && inZ && ilOk;
        float const dRI = zz ? cfg.zdRI : 0.f;
        float const dR = zz ? (cfg.zdR + (nL == 5 ? cfg.zdR5 : cfg.zdR6)) : 0.f;
        float const d4 = zz ? cfg.zdM4 : 0.f;
        float const d4D = zz ? cfg.zdM4D : 0.f;
        float const dCP = zz ? cfg.zdCP : 0.f;
        float const dCD = zz ? cfg.zdCD : 0.f;

        uint8_t flags = 0u;
        if (inZ)
          flags |= kChainFlagEtaBand;

        float score = chains.score()[c];
        if (nL <= 4) {
          if (dca >= chainMaxf(cfg.dcaSplit, cfg.t4ExemptDcaMin)) {
            // Exempt (large-DCA) T4-class: displaced-oriented acceptance on mD.
            if (mD < cfg.m3Theta4D + d4D) {
              score -= cfg.gateKill;
              flags |= kChainFlagKilled;
            }
            flags |= kChainFlagExempt;
          } else if (mX < cfg.m3Theta4 + d4) {
            score -= cfg.gateKill;
            flags |= kChainFlagKilled;
          }
        } else if (dca < cfg.dcaSplit) {
          // IP-compatible 5+: per-length threshold with the -MRI OR-rescue on mX.
          float const thr = nL >= 6 ? cfg.m3Theta6 : cfg.m3Theta5;
          if (mP < thr && mX < cfg.m3ThetaRI + dRI) {
            score -= cfg.gateKill;
            flags |= kChainFlagKilled;
          }
        } else {
          // Exempt (large-DCA) 5+, with the -MR OR-rescue on mX.
          if (mD < cfg.m3ThetaD && mX < cfg.m3ThetaR + dR) {
            score -= cfg.gateKill;
            flags |= kChainFlagKilled;
          }
          flags |= kChainFlagExempt;
        }

        // ANGLE C1 -C25 / -C25D: extra margin for the (nNodes == 2, nLayers == 5) CELL only, on
        // top of whichever branch rule already ran. The displaced head is respected (kill only
        // when BOTH margins fail) and an already-killed chain is never re-killed.
        if (cfg.c25Theta > -1e9f && nL == 5 && chains.nNodes()[c] == 2) {
          if (score > -0.5f * cfg.gateKill) {
            if (mP < cfg.c25Theta + dCP && mD < cfg.c25ThetaD + dCD) {
              score -= cfg.gateKill;
              flags |= kChainFlagKilled;
              flags |= kChainFlagCellKill;
            }
          }
        }

        chains.score()[c] = score;
        chains.flags()[c] = flags;
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
