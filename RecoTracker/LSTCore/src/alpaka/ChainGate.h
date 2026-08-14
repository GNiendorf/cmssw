#ifndef RecoTracker_LSTCore_src_alpaka_ChainGate_h
#define RecoTracker_LSTCore_src_alpaka_ChainGate_h

#include <numbers>
#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "HeterogeneousCore/AlpakaMath/interface/deltaPhi.h"

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

#include "ChainNetworkWeights.h"
#include "ChainEdges.h"
#include "ChainWeld.h"
#include "NeuralNetwork.h"

// Chain gate: the learned judgement of a welded chain.
//
// It consumes the welded chains (ChainWeld.h) and produces, per chain, the frozen 25-float feature
// row, the chain transverse DCA, three class logits and a kill mark on the chain score. Two kernels:
//   ChainFeaturesKernel - the feature row and the dcaXY
//   ChainGateKernel     - the chain head, followed by the branch kill
//
// The head is 25 -> 32 -> 32 -> 3 and its outputs are [0] fake, [1] prompt-true, [2] displaced-true.
// No softmax is ever applied: every downstream decision is taken on a logit MARGIN,
//   mP = z[1] - z[0]              prompt against fake
//   mD = z[2] - z[0]              displaced against fake
//   mX = max(z[1], z[2]) - z[0]   real (either class) against fake
// which is exact because the softmax is monotone in each of them.
//
// The kill branches on (nLayers, dcaXY) into four cells, each with its own bar, because the four
// carry different physics: 4-layer IP, 4-layer large-DCA ("exempt"), 5+-layer IP, 5+-layer exempt.
// The IP / exempt split is the chain's own reconstructed dcaXY against config.dcaSplit. The 4-layer
// cells are judged on mX and mD, the 5+ cells on mP and mD with an OR-rescue on mX, and an extra
// cell rule covers (nNodes == 2, nLayers == 5). A killed chain has config.gateKill subtracted from its
// score, which is the single number the claim stage thresholds on.
//
// Every fit accumulation below is done in double, and each pass recomputes what it needs rather
// than caching partial results, so the sums happen in a fixed order. Reordering them moves the
// feature row and therefore the head's verdict.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  namespace chaingate {
    constexpr float kEps = 1e-9f;
    // The track-candidate pt convention, pt = radius * k2Rinv1GeVf * 2. It is a trained-in
    // definition: feature 9 is built from it, so it must match what the head was fitted on.
    ALPAKA_FN_ACC ALPAKA_FN_INLINE float t3Pt(TripletsConst triplets, uint32_t tripletIdx) {
      return triplets.radius()[tripletIdx] * k2Rinv1GeVf * 2;
    }
  }  // namespace chaingate

  // Pseudorapidity of one mini-doublet's anchor hit, sign(z) * acosh(r3 / rt). Callers pass the
  // triplet's LAST MD, which is the triplet eta convention the emitted track candidate carries.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainT3Eta(TAcc const& acc,
                                                  MiniDoubletsConst miniDoublets,
                                                  unsigned int mdIndex) {
    float const x = miniDoublets.anchorX()[mdIndex], y = miniDoublets.anchorY()[mdIndex],
                z = miniDoublets.anchorZ()[mdIndex];
    float const radius3D = alpaka::math::sqrt(acc, x * x + y * y + z * z);
    float const rt = alpaka::math::sqrt(acc, x * x + y * y);
    float const sign = static_cast<float>((z > 0.f) - (z < 0.f));
    return sign * alpaka::math::acosh(acc, radius3D / rt);
  }

  // Bend direction of a triplet: the sign of the z component of cross(firstMD->midMD,
  // midMD->lastMD) in the transverse plane. A collinear triplet counts as +1. It stands in for the
  // charge sign, and features 8 and 15 are built from it.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainT3RotSign(TripletsConst triplets,
                                                      SegmentsConst segments,
                                                      MiniDoubletsConst miniDoublets,
                                                      uint32_t tripletIdx) {
    unsigned int firstMD, midMD, lastMD;
    chainNodeMDs(triplets, segments, tripletIdx, firstMD, midMD, lastMD);
    float const chord01X = miniDoublets.anchorX()[midMD] - miniDoublets.anchorX()[firstMD];
    float const chord01Y = miniDoublets.anchorY()[midMD] - miniDoublets.anchorY()[firstMD];
    float const chord12X = miniDoublets.anchorX()[lastMD] - miniDoublets.anchorX()[midMD];
    float const chord12Y = miniDoublets.anchorY()[lastMD] - miniDoublets.anchorY()[midMD];
    float const cross = chord01X * chord12Y - chord01Y * chord12X;
    return (cross >= 0.f) ? 1.f : -1.f;
  }

  // Kasa algebraic circle fit over at most 6 anchor hits, returning chi2 per hit in cm^2.
  //
  // The fit works in coordinates centred on the hit centroid, du = x - xMean and dv = y - yMean,
  // and in the auxiliary distSq = du^2 + dv^2. Minimising the ALGEBRAIC residual (du^2 + dv^2 -
  // 2*a*du - 2*b*dv - c) is linear in the unknowns, so the centre falls out of a 2x2 normal system
  // whose coefficients are the sums accumulated below: sumUU = sum du*du, sumUV = sum du*dv,
  // sumUW = sum du*distSq, and so on. In the published Kasa derivation those carry the symbols
  // u, v, w and Suu, Suv, Suw; the correspondence is name for name.
  //
  // Algebraic rather than geometric fitting is deliberate: it is closed-form and branch-free,
  // which is what a per-chain device kernel wants, at the cost of a mild bias towards larger radii
  // that is shared by every hit on the chain and cancels in the comparisons this feature feeds.
  //
  // The full-chain fit inside chainBuildFeatures is the same arithmetic in the same order, kept
  // separate only because it also publishes the fitted centre.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE double chainKasaChi2PerHit(TAcc const& acc,
                                                            double const* hitX,
                                                            double const* hitY,
                                                            int nHits) {
    if (nHits < 3)
      return 0.0;
    double xMean = 0.0, yMean = 0.0;
    for (int i = 0; i < nHits; ++i) {
      xMean += hitX[i];
      yMean += hitY[i];
    }
    xMean /= nHits;
    yMean /= nHits;
    double sumUU = 0.0, sumVV = 0.0, sumUV = 0.0, sumUW = 0.0, sumVW = 0.0, sumW = 0.0;
    for (int i = 0; i < nHits; ++i) {
      double const du = hitX[i] - xMean, dv = hitY[i] - yMean;
      double const distSq = du * du + dv * dv;
      sumUU += du * du;
      sumVV += dv * dv;
      sumUV += du * dv;
      sumUW += du * distSq;
      sumVW += dv * distSq;
      sumW += distSq;
    }
    double const determinant = sumUU * sumVV - sumUV * sumUV;
    double const scale = sumUU + sumVV;
    if (!(determinant > 1e-12 * scale * scale))
      return 0.0;  // collinear hits: no circle is defined, and the caller treats 0 as "no evidence"
    // (centreU, centreV) is the fitted centre relative to the hit centroid, radius the fitted one.
    double const centreU = (sumVV * (0.5 * sumUW) - sumUV * (0.5 * sumVW)) / determinant;
    double const centreV = (sumUU * (0.5 * sumVW) - sumUV * (0.5 * sumUW)) / determinant;
    double const radius =
        alpaka::math::sqrt(acc, alpaka::math::max(acc, centreU * centreU + centreV * centreV + sumW / nHits, 0.0));
    double chi2 = 0.0;
    for (int i = 0; i < nHits; ++i) {
      double const du = (hitX[i] - xMean) - centreU, dv = (hitY[i] - yMean) - centreV;
      double const resid = alpaka::math::sqrt(acc, du * du + dv * dv) - radius;
      chi2 += resid * resid;
    }
    return chi2 / nHits;
  }

  // ChainFeaturesKernel core. The 25 frozen chain features plus the chain dcaXY. It takes an EXPLICIT
  // (node run, MD union) rather than a chain row, so a candidate TERMINAL VARIANT of a chain -- a
  // node run that is not yet, and may never become, a chain -- is scored by the identical function
  // (ChainTrimLearn.h). `featuresOut` receives the sanitized row.
  //
  // The 25 columns, in the frozen order of Params_ChainFeat (ChainsSoA.h). The head gathers 24 of
  // them, skipping 19, and appends the dcaXY as its own 25th input (ChainNetworkWeights.h kSrcCol):
  //    0 nNodes            1 nLayers           2-4 sum / min / mean of the member weld-edge logits
  //    5 fullFitChi2PerHit  6 rzLineChi2PerHit   7 fitKappa (signed 1/R of the full circle fit)
  //    8 fitKappa minus the member-triplet median kappa      9 ptEst (median member-triplet pt)
  //   10 innermostLayer   11 layerSpan         12 nPS       13 nBarrel
  //   14 maxJunctionDegProduct (the crowding of the region the chain was welded in)
  //   15 chargeConsistency  16 maxXyResid      17 maxRzResid   18 std of the member weld-edge logits
  //   19 maxBridgeChi2 (worst circle fit over one consecutive member-triplet pair)
  //   20-24 aggregates of the member triplets' own head scores: min / max fake, mean prompt,
  //         min / mean displaced
  // Broadly: 2-4 and 18 say how well the weld believed its own edges, 5-8 and 16-19 how well the
  // hits fit one helix, 10-14 where in the detector the chain lives, and 20-24 what the upstream
  // triplet head thought of its parts.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void chainBuildFeatures(Acc1D const& acc,
                                                         ModulesConst modules,
                                                         MiniDoubletsConst miniDoublets,
                                                         SegmentsConst segments,
                                                         TripletsConst triplets,
                                                         ChainNodesConst nodes,
                                                         ChainEdgesConst edges,
                                                         ChainIncidenceConst mdIncidence,
                                                         ChainIncidenceConst lsIncidence,
                                                         ChainItemsConst items,
                                                         uint32_t nodeOffset,
                                                         int nNodes,
                                                         uint32_t const* mdList,
                                                         int nMDs,
                                                         int nLayersIn,
                                                         float* featuresOut,
                                                         float& dcaOut) {
    // A chain of n nodes carries exactly n - 1 welded edges, sharing the node run's offset.
    int const nEdges = nNodes - 1;

    // --- 2-4, 18: member weld-edge logit aggregates -------------------------------------
    float sumEdgeLogit = 0.f, minEdgeLogit = 0.f;
    double sumEdgeLogit2 = 0.0;
    if (nEdges > 0) {
      minEdgeLogit = edges.logOdds()[items.edgeItems()[nodeOffset]];
      for (int k = 0; k < nEdges; ++k) {
        float const logOdds = edges.logOdds()[items.edgeItems()[nodeOffset + k]];
        sumEdgeLogit += logOdds;
        sumEdgeLogit2 += static_cast<double>(logOdds) * static_cast<double>(logOdds);
        if (logOdds < minEdgeLogit)
          minEdgeLogit = logOdds;
      }
    }
    float const meanEdgeLogit = nEdges > 0 ? sumEdgeLogit / static_cast<float>(nEdges) : 0.f;
    float stdEdgeLogit = 0.f;
    if (nEdges > 1) {
      double const variance =
          sumEdgeLogit2 / nEdges - static_cast<double>(meanEdgeLogit) * static_cast<double>(meanEdgeLogit);
      stdEdgeLogit = static_cast<float>(alpaka::math::sqrt(acc, alpaka::math::max(acc, variance, 0.0)));
    }

    // --- 5, 7, 16 and the dcaXY: Kasa circle fit over all anchor hits --------------------
    double fitChi2PerHit = 0.0, fitKappa = 0.0, maxXyResid = 0.0;
    float dcaXY = 1e9f;
    bool circleOk = false;
    if (nMDs >= 3) {
      double xMean = 0.0, yMean = 0.0;
      for (int k = 0; k < nMDs; ++k) {
        xMean += miniDoublets.anchorX()[mdList[k]];
        yMean += miniDoublets.anchorY()[mdList[k]];
      }
      xMean /= nMDs;
      yMean /= nMDs;
      double sumUU = 0.0, sumVV = 0.0, sumUV = 0.0, sumUW = 0.0, sumVW = 0.0, sumW = 0.0;
      for (int k = 0; k < nMDs; ++k) {
        double const du = miniDoublets.anchorX()[mdList[k]] - xMean, dv = miniDoublets.anchorY()[mdList[k]] - yMean;
        double const distSq = du * du + dv * dv;
        sumUU += du * du;
        sumVV += dv * dv;
        sumUV += du * dv;
        sumUW += du * distSq;
        sumVW += dv * distSq;
        sumW += distSq;
      }
      double const determinant = sumUU * sumVV - sumUV * sumUV;
      double const scale = sumUU + sumVV;
      if (determinant > 1e-12 * scale * scale) {  // degenerate (collinear) -> the fit columns stay 0
        circleOk = true;
        double const centreU = (sumVV * (0.5 * sumUW) - sumUV * (0.5 * sumVW)) / determinant;
        double const centreV = (sumUU * (0.5 * sumVW) - sumUV * (0.5 * sumUW)) / determinant;
        double const radius =
            alpaka::math::sqrt(acc, alpaka::math::max(acc, centreU * centreU + centreV * centreV + sumW / nMDs, 0.0));
        double chi2 = 0.0;
        for (int k = 0; k < nMDs; ++k) {
          double const du = (miniDoublets.anchorX()[mdList[k]] - xMean) - centreU;
          double const dv = (miniDoublets.anchorY()[mdList[k]] - yMean) - centreV;
          double const resid = alpaka::math::sqrt(acc, du * du + dv * dv) - radius;
          chi2 += resid * resid;
          maxXyResid = alpaka::math::max(acc, maxXyResid, alpaka::math::abs(acc, resid));
        }
        fitChi2PerHit = chi2 / nMDs;
        double crossSum = 0.0;
        for (int k = 0; k + 2 < nMDs; ++k) {
          double const chordAX = static_cast<double>(miniDoublets.anchorX()[mdList[k + 1]]) -
                                 static_cast<double>(miniDoublets.anchorX()[mdList[k]]);
          double const chordAY = static_cast<double>(miniDoublets.anchorY()[mdList[k + 1]]) -
                                 static_cast<double>(miniDoublets.anchorY()[mdList[k]]);
          double const chordBX = static_cast<double>(miniDoublets.anchorX()[mdList[k + 2]]) -
                                 static_cast<double>(miniDoublets.anchorX()[mdList[k + 1]]);
          double const chordBY = static_cast<double>(miniDoublets.anchorY()[mdList[k + 2]]) -
                                 static_cast<double>(miniDoublets.anchorY()[mdList[k + 1]]);
          crossSum += chordAX * chordBY - chordAY * chordBX;
        }
        double const rotSign = (crossSum >= 0.0) ? 1.0 : -1.0;
        fitKappa = rotSign / alpaka::math::max(acc, radius, 1e-6);

        // The chain dcaXY rides on this same fit: the transverse distance of closest approach to
        // the beam line is |distance(origin, centre) - radius|. It costs the absolute centre and one
        // square root, which is why it is produced here rather than by a second fit.
        double const centreX = xMean + centreU, centreY = yMean + centreV;
        dcaXY = static_cast<float>(
            alpaka::math::abs(acc, alpaka::math::sqrt(acc, centreX * centreX + centreY * centreY) - radius));
      }
    }
    if (!circleOk) {
      // Degenerate (or nMDs < 3) fit: straight-line limit, the perpendicular distance from the
      // origin to the line through the innermost and outermost anchor hits. nMDs < 2 is unreachable
      // by the weld contract, and the 1e9 it would return is never IP-compatible.
      if (nMDs < 2) {
        dcaXY = 1e9f;
      } else {
        double const innerX = miniDoublets.anchorX()[mdList[0]], innerY = miniDoublets.anchorY()[mdList[0]];
        double const outerX = miniDoublets.anchorX()[mdList[nMDs - 1]],
                     outerY = miniDoublets.anchorY()[mdList[nMDs - 1]];
        double const chordLength =
            alpaka::math::sqrt(acc, (outerX - innerX) * (outerX - innerX) + (outerY - innerY) * (outerY - innerY));
        dcaXY = (chordLength < 1e-9)
                    ? 1e9f
                    : static_cast<float>(alpaka::math::abs(acc, innerX * outerY - outerX * innerY) / chordLength);
      }
    }

    // --- 6, 17: rz straight-line fit of z against the cumulative transverse chord length -
    // A helix is a straight line in (arc length, z), so a least-squares line here measures the
    // longitudinal consistency the circle fit above cannot see. The arc length is re-accumulated
    // in each of the three passes rather than stored: it keeps the scratch at zero words, and the
    // three passes must sum in the same order anyway.
    double rzChi2PerHit = 0.0, maxRzResid = 0.0;
    {
      double arcLength = 0.0, arcMean = 0.0, zMean = 0.0;
      for (int k = 0; k < nMDs; ++k) {
        if (k > 0) {
          double const stepX = static_cast<double>(miniDoublets.anchorX()[mdList[k]]) -
                               static_cast<double>(miniDoublets.anchorX()[mdList[k - 1]]);
          double const stepY = static_cast<double>(miniDoublets.anchorY()[mdList[k]]) -
                               static_cast<double>(miniDoublets.anchorY()[mdList[k - 1]]);
          arcLength += alpaka::math::sqrt(acc, stepX * stepX + stepY * stepY);
        }
        arcMean += arcLength;
        zMean += miniDoublets.anchorZ()[mdList[k]];
      }
      arcMean /= nMDs;
      zMean /= nMDs;
      double sumArcSq = 0.0, sumArcZ = 0.0;
      arcLength = 0.0;
      for (int k = 0; k < nMDs; ++k) {
        if (k > 0) {
          double const stepX = static_cast<double>(miniDoublets.anchorX()[mdList[k]]) -
                               static_cast<double>(miniDoublets.anchorX()[mdList[k - 1]]);
          double const stepY = static_cast<double>(miniDoublets.anchorY()[mdList[k]]) -
                               static_cast<double>(miniDoublets.anchorY()[mdList[k - 1]]);
          arcLength += alpaka::math::sqrt(acc, stepX * stepX + stepY * stepY);
        }
        double const dArc = arcLength - arcMean;
        sumArcSq += dArc * dArc;
        sumArcZ += dArc * (miniDoublets.anchorZ()[mdList[k]] - zMean);
      }
      if (sumArcSq > 1e-12) {  // all hits at one arc length -> no line is defined, the columns stay 0
        double const slope = sumArcZ / sumArcSq;
        double const zIntercept = zMean - slope * arcMean;
        double chi2 = 0.0;
        arcLength = 0.0;
        for (int k = 0; k < nMDs; ++k) {
          if (k > 0) {
            double const stepX = static_cast<double>(miniDoublets.anchorX()[mdList[k]]) -
                                 static_cast<double>(miniDoublets.anchorX()[mdList[k - 1]]);
            double const stepY = static_cast<double>(miniDoublets.anchorY()[mdList[k]]) -
                                 static_cast<double>(miniDoublets.anchorY()[mdList[k - 1]]);
            arcLength += alpaka::math::sqrt(acc, stepX * stepX + stepY * stepY);
          }
          double const resid = miniDoublets.anchorZ()[mdList[k]] - zIntercept - slope * arcLength;
          chi2 += resid * resid;
          maxRzResid = alpaka::math::max(acc, maxRzResid, alpaka::math::abs(acc, resid));
        }
        rzChi2PerHit = chi2 / nMDs;
      }
    }

    // --- 8, 9, 15, 20-24: member-T3 aggregates -------------------------------------------
    int nPos = 0, nNeg = 0;
    float minFakeT3 = 0.f, maxFakeT3 = 0.f, minDispT3 = 0.f;
    double sumPromptT3 = 0.0, sumDispT3 = 0.0;
    for (int k = 0; k < nNodes; ++k) {
      uint32_t const tripletIdx = nodes.tripletIndex()[items.nodeItems()[nodeOffset + k]];
      float const rotSign = chainT3RotSign(triplets, segments, miniDoublets, tripletIdx);
      (rotSign >= 0.f ? nPos : nNeg) += 1;
      float const fakeScore = triplets.fakeScore()[tripletIdx];
      float const promptScore = triplets.promptScore()[tripletIdx];
      float const dispScore = triplets.displacedScore()[tripletIdx];
      if (k == 0) {
        minFakeT3 = maxFakeT3 = fakeScore;
        minDispT3 = dispScore;
      } else {
        minFakeT3 = alpaka::math::min(acc, minFakeT3, fakeScore);
        maxFakeT3 = alpaka::math::max(acc, maxFakeT3, fakeScore);
        minDispT3 = alpaka::math::min(acc, minDispT3, dispScore);
      }
      sumPromptT3 += promptScore;
      sumDispT3 += dispScore;
    }
    float const meanPromptT3 = static_cast<float>(sumPromptT3 / nNodes);
    float const meanDispT3 = static_cast<float>(sumDispT3 / nNodes);

    // Features 8 and 9 both need the median over the member triplets, of the signed curvature
    // (pass 0) and of the pt (pass 1). LOWER median = the ((nNodes - 1) / 2)-th order statistic,
    // and it is selected by rank counting: a chain holds a handful of nodes, so an O(n^2) scan is
    // cheaper than a sort and needs no scratch array. A value is the median when the number of
    // members strictly below it is at most medianRank and the ties straddle medianRank.
    int const medianRank = (nNodes - 1) / 2;
    float medianKappaT3 = 0.f, ptEst = 0.f;
    for (int pass = 0; pass < 2; ++pass) {
      float chosen = 0.f;
      bool found = false;
      for (int i = 0; i < nNodes && !found; ++i) {
        uint32_t const tripletI = nodes.tripletIndex()[items.nodeItems()[nodeOffset + i]];
        float valueI;
        if (pass == 0) {
          float const rotSign = chainT3RotSign(triplets, segments, miniDoublets, tripletI);
          valueI =
              rotSign / alpaka::math::max(acc, chainCleanRadius(triplets.radius()[tripletI]), float{chaingate::kEps});
        } else {
          valueI = chaingate::t3Pt(triplets, tripletI);
        }
        int nLess = 0, nEqual = 0;
        for (int j = 0; j < nNodes; ++j) {
          uint32_t const tripletJ = nodes.tripletIndex()[items.nodeItems()[nodeOffset + j]];
          float valueJ;
          if (pass == 0) {
            float const rotSign = chainT3RotSign(triplets, segments, miniDoublets, tripletJ);
            valueJ =
                rotSign / alpaka::math::max(acc, chainCleanRadius(triplets.radius()[tripletJ]), float{chaingate::kEps});
          } else {
            valueJ = chaingate::t3Pt(triplets, tripletJ);
          }
          if (valueJ < valueI)
            ++nLess;
          else if (valueJ == valueI)
            ++nEqual;
        }
        if (nLess <= medianRank && medianRank < nLess + nEqual) {
          chosen = valueI;
          found = true;
        }
      }
      if (pass == 0)
        medianKappaT3 = chosen;
      else
        ptEst = chosen;
    }
    float const dKappaFitVsMedianT3 = static_cast<float>(fitKappa) - medianKappaT3;

    // Feature 15: the fraction of member pairs that agree on the bend direction. A real track bends
    // one way, so a chain welded across two different tracks tends to disagree with itself.
    long long const totPairs = static_cast<long long>(nNodes) * (nNodes - 1) / 2;
    long long const eqPairs =
        static_cast<long long>(nPos) * (nPos - 1) / 2 + static_cast<long long>(nNeg) * (nNeg - 1) / 2;
    float const chargeConsistency = totPairs > 0 ? static_cast<float>(eqPairs) / static_cast<float>(totPairs) : 1.f;

    // --- 10-13: MD-set detector-category aggregates --------------------------------------
    int const innermostLayer = chainMdLayer(modules, miniDoublets, mdList[0]);
    int minLayer = innermostLayer, maxLayer = innermostLayer, nPSModules = 0, nBarrel = 0;
    for (int k = 0; k < nMDs; ++k) {
      int const layer = chainMdLayer(modules, miniDoublets, mdList[k]);
      minLayer = (layer < minLayer) ? layer : minLayer;
      maxLayer = (layer > maxLayer) ? layer : maxLayer;
      nPSModules += chainMdIsPS(modules, miniDoublets, mdList[k]);
      nBarrel += (layer <= 6) ? 1 : 0;
    }

    // --- 14: max junction degree product over the member weld edges ----------------------
    // How crowded the detector element each weld was made at is: degIn * degOut is the number of
    // triplet pairs that could have been joined there, so a large value means the weld had many
    // equally plausible alternatives. It conditions the 4-layer gate bar (chainT4DensRelax).
    long long maxDegProd = 0;
    for (int k = 0; k < nEdges; ++k) {
      uint32_t const edgeIdx = items.edgeItems()[nodeOffset + k];
      // The junction is the inner node's "in" side, whose dense incidence key is stored on the
      // node by the CSR scatter -- one load instead of re-deriving it from the triplet.
      uint32_t const innerNode = edges.inner()[edgeIdx];
      long long degIn, degOut;
      if (edges.type()[edgeIdx] == 1u) {  // shared mini-doublet
        uint32_t const mdKey = nodes.mdKeyIn()[innerNode];
        degIn = mdIncidence.t3InOffsets()[mdKey + 1u] - mdIncidence.t3InOffsets()[mdKey];
        degOut = mdIncidence.t3OutOffsets()[mdKey + 1u] - mdIncidence.t3OutOffsets()[mdKey];
      } else {  // shared line segment
        uint32_t const lsKey = nodes.lsKeyIn()[innerNode];
        degIn = lsIncidence.t3InOffsets()[lsKey + 1u] - lsIncidence.t3InOffsets()[lsKey];
        degOut = lsIncidence.t3OutOffsets()[lsKey + 1u] - lsIncidence.t3OutOffsets()[lsKey];
      }
      long long const degProduct = degIn * degOut;
      maxDegProd = (degProduct > maxDegProd) ? degProduct : maxDegProd;
    }

    // --- 19: bridge-circle chi2 over consecutive member-T3 pairs -------------------------
    // The full-chain fit averages a bad joint away over every hit; this is the worst SINGLE joint,
    // a circle fit over just the two triplets a weld edge connects (at most 6 distinct MDs, since
    // an E1 edge shares one and an E2 edge shares two).
    double maxBridgeChi2 = 0.0;
    {
      double bridgeX[6], bridgeY[6];
      unsigned int bridgeMD[6];
      for (int k = 0; k + 1 < nNodes; ++k) {
        uint32_t const innerTriplet = nodes.tripletIndex()[items.nodeItems()[nodeOffset + k]];
        uint32_t const outerTriplet = nodes.tripletIndex()[items.nodeItems()[nodeOffset + k + 1]];
        unsigned int innerMD0, innerMD1, innerMD2, outerMD0, outerMD1, outerMD2;
        chainNodeMDs(triplets, segments, innerTriplet, innerMD0, innerMD1, innerMD2);
        chainNodeMDs(triplets, segments, outerTriplet, outerMD0, outerMD1, outerMD2);
        unsigned int const candidates[6] = {innerMD0, innerMD1, innerMD2, outerMD0, outerMD1, outerMD2};
        int nBridge = 0;
        for (int candidateIdx = 0; candidateIdx < 6; ++candidateIdx) {
          bool duplicate = false;
          for (int existingIdx = 0; existingIdx < nBridge; ++existingIdx)
            if (bridgeMD[existingIdx] == candidates[candidateIdx]) {
              duplicate = true;
              break;
            }
          if (duplicate)
            continue;
          bridgeMD[nBridge] = candidates[candidateIdx];
          bridgeX[nBridge] = miniDoublets.anchorX()[candidates[candidateIdx]];
          bridgeY[nBridge] = miniDoublets.anchorY()[candidates[candidateIdx]];
          ++nBridge;
        }
        maxBridgeChi2 = alpaka::math::max(acc, maxBridgeChi2, chainKasaChi2PerHit(acc, bridgeX, bridgeY, nBridge));
      }
    }

    // --- store (order = the frozen contract in ChainsSoA.h) ------------------------------
    float features[Params_ChainFeat::kFeatures];
    features[0] = static_cast<float>(nNodes);
    features[1] = static_cast<float>(nLayersIn);
    features[2] = sumEdgeLogit;
    features[3] = minEdgeLogit;
    features[4] = meanEdgeLogit;
    features[5] = static_cast<float>(fitChi2PerHit);
    features[6] = static_cast<float>(rzChi2PerHit);
    features[7] = static_cast<float>(fitKappa);
    features[8] = dKappaFitVsMedianT3;
    features[9] = ptEst;
    features[10] = static_cast<float>(innermostLayer);
    features[11] = static_cast<float>(maxLayer - minLayer);
    features[12] = static_cast<float>(nPSModules);
    features[13] = static_cast<float>(nBarrel);
    features[14] = static_cast<float>(maxDegProd);
    features[15] = chargeConsistency;
    features[16] = static_cast<float>(maxXyResid);
    features[17] = static_cast<float>(maxRzResid);
    features[18] = stdEdgeLogit;
    features[19] = static_cast<float>(maxBridgeChi2);
    features[20] = minFakeT3;
    features[21] = maxFakeT3;
    features[22] = meanPromptT3;
    features[23] = minDispT3;
    features[24] = meanDispT3;

    // Every column is sanitized on the way out: a degenerate fit can produce a non-finite value,
    // and one NaN reaching the head poisons all three logits and hence the branch decision.
    CMS_UNROLL_LOOP
    for (int i = 0; i < Params_ChainFeat::kFeatures; ++i)
      featuresOut[i] = chainSanitize(features[i]);
    dcaOut = dcaXY;
  }

  // ChainFeaturesKernel. One frozen feature row and one dcaXY per chain, in place in the chain row.
  struct ChainFeaturesKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst miniDoublets,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  ChainEdgesConst edges,
                                  ChainIncidenceConst mdIncidence,
                                  ChainIncidenceConst lsIncidence,
                                  ChainItemsConst items,
                                  Chains chains) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());

      for (uint32_t chainIdx : cms::alpakatools::uniform_elements(acc, nChains)) {
        // The terminal trim scores its winning variant with this same builder, so when it has
        // published a row there is nothing to recompute -- the build would be an identity.
        if (chains.featValid()[chainIdx] != 0u)
          continue;
        int const nNodes = chains.nNodes()[chainIdx];
        int const nMDs = chains.nMDs()[chainIdx];
        if (nNodes < 1 || nMDs < 1)
          continue;  // unreachable by the weld contract; the row stays zeroed
        uint32_t const nodeOffset = chains.nodeOffset()[chainIdx];
        float features[Params_ChainFeat::kFeatures];
        float dca = 1e9f;
        chainBuildFeatures(acc,
                           modules,
                           miniDoublets,
                           segments,
                           triplets,
                           nodes,
                           edges,
                           mdIncidence,
                           lsIncidence,
                           items,
                           nodeOffset,
                           nNodes,
                           &items.mdItems()[3u * nodeOffset],
                           nMDs,
                           chains.nLayers()[chainIdx],
                           features,
                           dca);
        CMS_UNROLL_LOOP
        for (int i = 0; i < Params_ChainFeat::kFeatures; ++i)
          chains.features()[chainIdx][i] = features[i];
        chains.dcaXY()[chainIdx] = dca;
      }
    }
  };

  // The density-conditioned relaxation of the 4-layer IP gate bar, in bar units.
  //
  // `degProd` is feature column 14 straight off the chain row: the crowding of the region this
  // chain was welded in. It is used strictly as a CONDITIONING variable -- it decides how much of a
  // fixed relaxation applies, never how a chain ranks against another.
  //
  // The ramp is identically zero at or below config.t4DensRho0 and saturates config.t4DensDecades decades
  // above it, so a region whose occupancy sits below rho0 takes the unrelaxed bar BIT FOR BIT. That
  // makes the sparse-region invariance a property of the code rather than of a measurement.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainT4DensRelax(TAcc const& acc, ChainConfig const& config, float degProd) {
    if (!(config.t4DensDelta > 0.f))
      return 0.f;
    float const decades = alpaka::math::log10(acc, 1.f + degProd) - alpaka::math::log10(acc, 1.f + config.t4DensRho0);
    if (!(decades > 0.f))
      return 0.f;
    float const ramp = (config.t4DensDecades > 0.f) ? alpaka::math::min(acc, decades / config.t4DensDecades, 1.f) : 1.f;
    return config.t4DensDelta * ramp;
  }

  // ChainGateKernel core. The chain head, 25 -> 32 -> 32 -> 3, returning raw logits. Each input is gathered
  // from the feature row through the head's own kSrcCol table (column -1 means the chain dcaXY,
  // which is not a feature column), optionally log10(1 + x)-compressed, clipped and standardised --
  // all four tables ship with the weights and must be applied in that order.
  //
  // Taking the feature row as a POINTER rather than a chain index is what lets a candidate terminal
  // variant be scored by the same head as a real chain.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void chainGateLogits(Acc1D const& acc,
                                                      float const* features,
                                                      float dca,
                                                      float (&logits)[dnn::chain3mlp::kOutput]) {
    float inputs[dnn::chain3mlp::kInput];
    for (int i = 0; i < dnn::chain3mlp::kInput; ++i) {
      int const sourceColumn = dnn::chain3mlp::kSrcCol[i];
      float value = (sourceColumn < 0) ? dca : features[sourceColumn];
      if (dnn::chain3mlp::kLog10p1[i])
        value = alpaka::math::log10(acc, 1.f + value);
      value =
          alpaka::math::min(acc, alpaka::math::max(acc, value, dnn::chain3mlp::kClipLo[i]), dnn::chain3mlp::kClipHi[i]);
      inputs[i] = (value - dnn::chain3mlp::kFeatMean[i]) / dnn::chain3mlp::kFeatStd[i];
    }

    float hidden1[dnn::chain3mlp::kHidden];
    float hidden2[dnn::chain3mlp::kHidden];

    linear_layer<dnn::chain3mlp::kInput, dnn::chain3mlp::kHidden>(
        inputs, hidden1, dnn::chain3mlp::wgt_l1, dnn::chain3mlp::bias_l1);
    relu_activation<dnn::chain3mlp::kHidden>(hidden1);
    linear_layer<dnn::chain3mlp::kHidden, dnn::chain3mlp::kHidden>(
        hidden1, hidden2, dnn::chain3mlp::wgt_l2, dnn::chain3mlp::bias_l2);
    relu_activation<dnn::chain3mlp::kHidden>(hidden2);
    linear_layer<dnn::chain3mlp::kHidden, dnn::chain3mlp::kOutput>(
        hidden2, logits, dnn::chain3mlp::wgt_out, dnn::chain3mlp::bias_out);
  }

  // ChainGateKernel, fused: run the head on every chain, publish the three logits and the three margins,
  // then apply the branch kill described in the file header. The only state this kernel changes is
  // the chain's score (reduced by config.gateKill when the chain is killed) and its flags; the chain
  // itself is never edited, so a killed chain is still visible to anything that wants to inspect it.
  struct ChainGateKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  MiniDoubletsConst miniDoublets,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  ChainItemsConst items,
                                  Chains chains,
                                  ChainConfig config) const {
      static_assert(dnn::chain3mlp::kInput == Params_ChainFeat::kFeatures,
                    "ChainNetworkWeights.h input size does not match the frozen feature contract");
      static_assert(dnn::chain3mlp::kOutput == 3, "the chain3 gate must have 3 outputs");

      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());
      bool const etaBandLive = config.etaBandActive();

      for (uint32_t chainIdx : cms::alpakatools::uniform_elements(acc, nChains)) {
        float const dca = chains.dcaXY()[chainIdx];

        // --- the 3-class head ----------------------------------------------------------
        float logits[dnn::chain3mlp::kOutput];
        chainGateLogits(acc, chains.features()[chainIdx].data(), dca, logits);

        float const mP = logits[1] - logits[0];
        float const mD = logits[2] - logits[0];
        float const mX = alpaka::math::max(acc, logits[1], logits[2]) - logits[0];

        chains.zFake()[chainIdx] = logits[0];
        chains.zPrompt()[chainIdx] = logits[1];
        chains.zDisp()[chainIdx] = logits[2];
        chains.marginP()[chainIdx] = mP;
        chains.marginD()[chainIdx] = mD;
        chains.marginX()[chainIdx] = mX;

        // --- the branch kill -----------------------------------------------------------
        int const nLayers = chains.nLayers()[chainIdx];
        uint32_t const nodeOffset = chains.nodeOffset()[chainIdx];

        int8_t const branch = nLayers <= 4
                                  ? (dca >= alpaka::math::max(acc, config.dcaSplit, config.t4ExemptDcaMin) ? 1 : 0)
                                  : (dca < config.dcaSplit ? 2 : 3);
        chains.branch()[chainIdx] = branch;

        // Band membership is taken on |eta| of the INNERMOST member triplet, which is the eta the
        // emitted track candidate is counted at, so a chain is tightened in exactly the band its
        // candidate lands in. A negative value means "no member triplet" and falls back to the
        // unbanded bars.
        float absEtaInner = -1.f;
        if (chains.nNodes()[chainIdx] > 0) {
          uint32_t const innerTriplet = nodes.tripletIndex()[items.nodeItems()[nodeOffset]];
          unsigned int firstMD, midMD, lastMD;
          chainNodeMDs(triplets, segments, innerTriplet, firstMD, midMD, lastMD);
          absEtaInner = alpaka::math::abs(acc, chainT3Eta(acc, miniDoublets, lastMD));
        }
        bool const inEtaBand = config.zEta2 > config.zEta1 && absEtaInner >= config.zEta1 && absEtaInner < config.zEta2;
        // The exempt-5+ rescue floor is split three ways on the same axis. Note the asymmetry with
        // inEtaBand: the barrel band does NOT require zEta2 > zEta1, so it stays live even when the
        // transition band is switched off by collapsing the two boundaries.
        bool const inBarrelBand = absEtaInner >= 0.f && absEtaInner < config.zEta1;
        float const rescueBar = inBarrelBand ? config.m3ThetaRB : (inEtaBand ? config.m3ThetaRT : config.m3ThetaR);

        // Optionally the band deltas only reach chains whose innermost MD sits in layer 1
        // (feature column 10).
        bool layer1Ok = true;
        if (config.zInLayer1) {
          layer1Ok = false;
          if (chains.nMDs()[chainIdx] > 0)
            layer1Ok = (chains.features()[chainIdx][10] == 1.f);
        }

        // The band deltas are ADDITIVE on the bars below, and all six are zero unless this chain is
        // in the band, so an out-of-band chain runs the unbanded rule with no branch of its own.
        bool const bandApplies = etaBandLive && inEtaBand && layer1Ok;
        float const dThetaRI = bandApplies ? config.zdRI : 0.f;
        float const dThetaR = bandApplies ? (config.zdR + (nLayers == 5 ? config.zdR5 : config.zdR6)) : 0.f;
        float const dTheta4 = bandApplies ? config.zdM4 : 0.f;
        float const dTheta4D = bandApplies ? config.zdM4D : 0.f;
        float const dC25P = bandApplies ? config.zdCP : 0.f;
        float const dC25D = bandApplies ? config.zdCD : 0.f;

        uint8_t flags = 0u;
        if (inEtaBand)
          flags |= kChainFlagEtaBand;

        float score = chains.score()[chainIdx];
        if (nLayers <= 4) {
          if (dca >= alpaka::math::max(acc, config.dcaSplit, config.t4ExemptDcaMin)) {
            // Exempt (large-DCA) 4-layer: judged on mD, because a displaced track is what this cell
            // is made of and the prompt class has nothing to say about it.
            //
            // Above the second dcaXY breakpoint the cell gets a bar of its own and the eta-band
            // delta is NOT applied (see ChainConfig.h dcaSplit2): the shared bar was fitted on a
            // population sitting just above dcaSplit, and a genuinely far-displaced chain is a
            // different object. That far cell also requires a good fit (feature 16 = maxXyResid),
            // so it selects "displaced AND well measured" rather than merely "badly fitted".
            bool const farCell = dca >= config.dcaSplit2 && chains.features()[chainIdx][16] <= config.t4FarMaxResid;
            // The suppression offset reaches the NON-far exempt bar only: the far-dca cell keeps
            // its free pass, which is where the displaced advantage lives.
            float const bar4D = farCell ? config.m3Theta4D2 : (config.m3Theta4D + dTheta4D + config.t4GateTighten);
            if (mD < bar4D) {
              score -= config.gateKill;
              flags |= kChainFlagKilled;
            }
            flags |= kChainFlagExempt;
          } else if (mX < config.m3Theta4 + dTheta4 + config.t4GateTighten -
                              chainT4DensRelax(acc, config, chains.features()[chainIdx][14])) {
            // IP 4-layer: judged on mX (either real class passes), with the bar relaxed in crowded
            // regions only.
            score -= config.gateKill;
            flags |= kChainFlagKilled;
          }
        } else if (dca < config.dcaSplit) {
          // IP 5+: per-length bar on mP, with an OR-rescue on mX so a chain the head calls
          // displaced is not killed by a prompt bar.
          float const promptBar = nLayers >= 6 ? config.m3Theta6 : config.m3Theta5;
          if (mP < promptBar && mX < config.m3ThetaRI + dThetaRI) {
            score -= config.gateKill;
            flags |= kChainFlagKilled;
          }
        } else {
          // Exempt (large-DCA) 5+, with the eta-band-split OR-rescue on mX.
          if (mD < config.m3ThetaD && mX < rescueBar + dThetaR) {
            score -= config.gateKill;
            flags |= kChainFlagKilled;
          }
          flags |= kChainFlagExempt;
        }

        // Extra margin for the (nNodes == 2, nLayers == 5) cell only, on top of whichever branch
        // rule already ran. Two conditions make it safe to stack: it kills only when BOTH margins
        // fail, so the displaced class is still respected, and it never re-kills an already-killed
        // chain, so the score can never be reduced twice.
        if (config.c25Theta > -1e9f && nLayers == 5 && chains.nNodes()[chainIdx] == 2) {
          if (score > -0.5f * config.gateKill) {
            if (mP < config.c25Theta + dC25P && mD < config.c25ThetaD + dC25D) {
              score -= config.gateKill;
              flags |= kChainFlagKilled;
              flags |= kChainFlagCellKill;
            }
          }
        }

        chains.score()[chainIdx] = score;
        chains.flags()[chainIdx] = flags;
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
