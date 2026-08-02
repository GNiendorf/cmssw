#ifndef RecoTracker_LSTCore_src_alpaka_ChainEdges_h
#define RecoTracker_LSTCore_src_alpaka_ChainEdges_h

#include <bit>
#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainEdgesSoA.h"
#include "RecoTracker/LSTCore/interface/ChainIncidenceSoA.h"
#include "RecoTracker/LSTCore/interface/ChainNodesSoA.h"
#include "RecoTracker/LSTCore/interface/MiniDoubletsSoA.h"
#include "RecoTracker/LSTCore/interface/ModulesSoA.h"
#include "RecoTracker/LSTCore/interface/SegmentsSoA.h"
#include "RecoTracker/LSTCore/interface/TripletsSoA.h"

#include "EdgeNetworkWeights.h"
#include "NeuralNetwork.h"

// Chain-tracking edge layer, phase P2.1 of standalone/prototype/P2_PORT_MAP.md.
//
// Stages implemented here:
//   K2  BuildEdges       - exact-count enumeration of the E1 and E2 pair families
//   K3  NodeFeatures     - the frozen 13-float origin-free node row per chain node
//   K5  EdgeInference    - the 14 edge floats built in registers, then edgemlp 40->32->32->1
//
// The reference implementation is standalone/prototype/ (Stages.cc k2BuildEdges, Features.cc,
// EdgeInference.cc). Every arithmetic expression below is a transcription of the corresponding
// prototype line, in the same operation order, because the phase gate is float parity of the
// resulting logits against that implementation. Where a quantity comes from the LST ntuple in
// the prototype, the production expression that reproduces the ntuple writer is used and the
// mapping is documented at the helper.
//
// Nothing here is read by any existing LST stage; the whole file only runs when
// useChainTracking is true and only writes into ChainNodes / ChainEdges.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  // ------------------------------------------------------------------------------------------
  // Scalar helpers. std::max / std::min are NOT fmax / fmin: they are plain comparisons and
  // propagate the first argument on NaN. The prototype uses std::max / std::min, so these
  // reproduce that exact behaviour rather than calling alpaka::math::max.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainMaxf(float a, float b) { return (a < b) ? b : a; }
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainMinf(float a, float b) { return (b < a) ? b : a; }

  namespace chainfeat {
    constexpr float kEps = 1e-9f;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kBig = 1e12f;
  }  // namespace chainfeat

  // Wrap an angle difference into [-pi, pi] (prototype Features.cc wrapPhi).
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainWrapPhi(float d) {
    while (d > chainfeat::kPi)
      d -= 2.f * chainfeat::kPi;
    while (d < -chainfeat::kPi)
      d += 2.f * chainfeat::kPi;
    return d;
  }

  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainDeltaPhi(float a, float b) { return chainWrapPhi(a - b); }

  // Classification by bit pattern rather than by std::isnan / std::isinf. The standalone device
  // library is built with -Ofast, which implies -ffinite-math-only and lets the compiler fold the
  // library predicates to constants; the bit tests survive that and keep the prototype's
  // NaN / Inf contract meaningful on every backend.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool chainIsFinite(float x) {
    return (std::bit_cast<uint32_t>(x) & 0x7f800000u) != 0x7f800000u;
  }
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool chainIsNan(float x) {
    uint32_t const b = std::bit_cast<uint32_t>(x);
    return (b & 0x7f800000u) == 0x7f800000u && (b & 0x007fffffu) != 0u;
  }
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool chainIsInf(float x) {
    return (std::bit_cast<uint32_t>(x) & 0x7fffffffu) == 0x7f800000u;
  }

  // A degenerate triplet circle fit can leave radius non-finite; map it to a large finite
  // stand-in so no feature goes non-finite (prototype Features.cc cleanRadius).
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainCleanRadius(float r) { return chainIsFinite(r) ? r : chainfeat::kBig; }

  // Final contract guard: no NaN / Inf may reach the head (prototype Features.cc sanitize).
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainSanitize(float x) {
    if (chainIsNan(x))
      return 0.f;
    if (chainIsInf(x))
      return (x > 0.f) ? chainfeat::kBig : -chainfeat::kBig;
    return x;
  }

  // ------------------------------------------------------------------------------------------
  // MiniDoublet detector categories.
  //
  // The frozen head was trained on the ntuple's md_layer / md_type, so these reproduce
  // standalone/code/core/write_lst_ntuple.cc exactly:
  //   md_layer = ph2_layer + 6 * (subdet == Endcap)         -> 1-6 barrel, 7-11 endcap
  //   md_type  = endcap ? (md_layer <= 2 ? ring <= 10 : ring <= 7) : (md_layer <= 3)
  // Note the endcap branch tests the ALREADY-SHIFTED layer, so it always reduces to ring <= 7.
  // That is the trained definition and is reproduced verbatim; it is not the same predicate as
  // modules.moduleType(), which comes from the geometry file (see the P2.1 notes).
  ALPAKA_FN_ACC ALPAKA_FN_INLINE int chainMdLayer(ModulesConst modules, MiniDoubletsConst mds, unsigned int md) {
    uint16_t const mod = mds.moduleIndices()[md];
    int const isEndcap = (modules.subdets()[mod] == Endcap) ? 1 : 0;
    return static_cast<int>(modules.layers()[mod]) + 6 * isEndcap;
  }

  ALPAKA_FN_ACC ALPAKA_FN_INLINE int chainMdIsPS(ModulesConst modules, MiniDoubletsConst mds, unsigned int md) {
    uint16_t const mod = mds.moduleIndices()[md];
    bool const isEndcap = (modules.subdets()[mod] == Endcap);
    int const layer = static_cast<int>(modules.layers()[mod]) + (isEndcap ? 6 : 0);
    int const ring = static_cast<int>(modules.rings()[mod]);
    if (isEndcap)
      return (layer <= 2) ? (ring <= 10 ? 1 : 0) : (ring <= 7 ? 1 : 0);
    return (layer <= 3) ? 1 : 0;
  }

  // The three anchor MDs of a triplet, innermost first. Same convention as the ntuple's
  // t3_md0 / t3_md1 / t3_md2 (AccessHelper getMDsFromT3: {LS0.md0, LS0.md1, LS1.md1}).
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void chainNodeMDs(TripletsConst triplets,
                                                   SegmentsConst segments,
                                                   uint32_t t3,
                                                   unsigned int& m0,
                                                   unsigned int& m1,
                                                   unsigned int& m2) {
    unsigned int const innerSeg = triplets.segmentIndices()[t3][0];
    unsigned int const outerSeg = triplets.segmentIndices()[t3][1];
    m0 = segments.mdIndices()[innerSeg][0];
    m1 = segments.mdIndices()[innerSeg][1];
    m2 = segments.mdIndices()[outerSeg][1];
  }

  // Largest key k in [0, nKeys) with prefix[k] <= value, for a non-decreasing prefix with
  // prefix[0] == 0 and prefix[nKeys] > value. Keys with a zero edge product have equal
  // consecutive prefix entries and are skipped by construction, so the returned key always has
  // degIn * degOut > 0. ~17 cached reads for a few hundred thousand keys.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainDecodeKey(uint32_t const* prefix, uint32_t nKeys, uint32_t value) {
    uint32_t lo = 0u;
    uint32_t hi = nKeys;
    while (lo + 1u < hi) {
      uint32_t const mid = lo + (hi - lo) / 2u;
      if (prefix[mid] <= value)
        lo = mid;
      else
        hi = mid;
    }
    return lo;
  }

  // ------------------------------------------------------------------------------------------
  // K2. Exact-count edge enumeration.
  //
  // Rows [0, nE1) are the MD-keyed E1 family and rows [nE1, nE1 + nE2) the Segment-keyed E2
  // family, matching the reference implementation's emission order. Each thread owns one row:
  // it binary-searches the edge-product prefix for its key, splits the remainder by the key's
  // out-degree, and reads the two CSR item slots. No atomic, no compaction, no reservation.
  //
  // Orientation (prototype Stages.cc k2BuildEdges): the INNER endpoint comes from the "in"
  // slice (triplets ending at the key) and the OUTER endpoint from the "out" slice (triplets
  // starting at the key), with the "in" slice as the outer loop, so the row split is
  //   (i, j) = (rem / degOut, rem % degOut).
  struct ChainBuildEdges {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TripletsConst triplets,
                                  SegmentsConst segments,
                                  ChainNodesConst nodes,
                                  ChainIncidenceConst mdIncidence,
                                  ChainIncidenceConst lsIncidence,
                                  ChainEdges edges,
                                  uint32_t nE1,
                                  uint32_t nE2) const {
      uint32_t const nMDKeys = static_cast<uint32_t>(mdIncidence.metadata().size()) - 1u;
      uint32_t const nLSKeys = static_cast<uint32_t>(lsIncidence.metadata().size()) - 1u;

      // Record the family split in the collection so a consumer can slice the row range without
      // carrying the two counts separately. Written on device to keep the host sync count at the
      // two K1b already needs.
      if (cms::alpakatools::once_per_grid(acc)) {
        edges.nE1Exact() = nE1;
        edges.nE2Exact() = nE2;
      }

      for (uint32_t e : cms::alpakatools::uniform_elements(acc, nE1 + nE2)) {
        bool const isE1 = (e < nE1);

        uint32_t const* prodPrefix = (isE1 ? mdIncidence.edgeProdPrefix() : lsIncidence.edgeProdPrefix()).data();
        uint32_t const* inOffsets = (isE1 ? mdIncidence.t3InOffsets() : lsIncidence.t3InOffsets()).data();
        uint32_t const* outOffsets = (isE1 ? mdIncidence.t3OutOffsets() : lsIncidence.t3OutOffsets()).data();
        uint32_t const* inItems = (isE1 ? nodes.mdT3InItems() : nodes.lsT3InItems()).data();
        uint32_t const* outItems = (isE1 ? nodes.mdT3OutItems() : nodes.lsT3OutItems()).data();
        uint32_t const nKeys = isE1 ? nMDKeys : nLSKeys;
        uint32_t const local = isE1 ? e : (e - nE1);

        uint32_t const key = chainDecodeKey(prodPrefix, nKeys, local);
        uint32_t const rem = local - prodPrefix[key];
        uint32_t const degOut = outOffsets[key + 1u] - outOffsets[key];
        ALPAKA_ASSERT_ACC(degOut > 0u);

        uint32_t const inner = inItems[inOffsets[key] + rem / degOut];
        uint32_t const outer = outItems[outOffsets[key] + rem % degOut];

        uint8_t type = isE1 ? 1u : 2u;
        if (inner == outer) {
          // Would need md2 == md0 (E1) or ls1 == ls0 (E2) inside one triplet.
          type = 0u;
        } else if (!isE1) {
          // An E2 pair that is also an E1 pair would need md2(inner) == md0(outer) == md1(inner),
          // impossible for a triplet with three distinct MDs. Kept as an O(1) test, exactly as
          // the reference does, so the two families stay provably disjoint.
          unsigned int const t3Inner = nodes.tripletIndex()[inner];
          unsigned int const t3Outer = nodes.tripletIndex()[outer];
          unsigned int const md2Inner = segments.mdIndices()[triplets.segmentIndices()[t3Inner][1]][1];
          unsigned int const md0Outer = segments.mdIndices()[triplets.segmentIndices()[t3Outer][0]][0];
          if (md2Inner == md0Outer)
            type = 0u;
        }

        edges.inner()[e] = inner;
        edges.outer()[e] = outer;
        edges.type()[e] = type;
        edges.logOdds()[e] = 0.f;
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // K3. The frozen 13-float node row, plus the four chord angles the edge build needs.
  // Transcription of prototype/Features.cc computeNodeFeatures (plus the per-T3 angle hoist at
  // the top of computeEdgeFeatures).
  struct ChainNodeFeatures {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodes nodes) const {
      uint32_t const nNodes = static_cast<uint32_t>(nodes.metadata().size());

      for (uint32_t n : cms::alpakatools::uniform_elements(acc, nNodes)) {
        uint32_t const t3 = nodes.tripletIndex()[n];
        unsigned int m0, m1, m2;
        chainNodeMDs(triplets, segments, t3, m0, m1, m2);

        float const x0 = mds.anchorX()[m0], y0 = mds.anchorY()[m0], z0 = mds.anchorZ()[m0];
        float const x1 = mds.anchorX()[m1], y1 = mds.anchorY()[m1], z1 = mds.anchorZ()[m1];
        float const x2 = mds.anchorX()[m2], y2 = mds.anchorY()[m2], z2 = mds.anchorZ()[m2];

        float const c01x = x1 - x0, c01y = y1 - y0, c01z = z1 - z0;
        float const c12x = x2 - x1, c12y = y2 - y1, c12z = z2 - z1;
        float const c02x = x2 - x0, c02y = y2 - y0, c02z = z2 - z0;

        // rotSign = sign of the z component of cross(c01, c12); collinear counts as +1.
        float const cross = c01x * c12y - c01y * c12x;
        float const rotSign = (cross >= 0.f) ? 1.f : -1.f;

        float const radius = chainCleanRadius(triplets.radius()[t3]);
        float const kappaSigned = rotSign / chainMaxf(radius, chainfeat::kEps);
        float const log10R = alpaka::math::log10(acc, chainMaxf(radius, 1e-3f));

        float const c02xy = alpaka::math::sqrt(acc, c02x * c02x + c02y * c02y);
        float const tanLambda = c02z / chainMaxf(c02xy, chainfeat::kEps);
        float const chordEta = alpaka::math::asinh(acc, c02z / chainMaxf(c02xy, chainfeat::kEps));

        float const phi01 = alpaka::math::atan2(acc, c01y, c01x);
        float const phi12 = alpaka::math::atan2(acc, c12y, c12x);
        float const dphi01 = chainDeltaPhi(phi01, phi12);

        float const rt0 = alpaka::math::sqrt(acc, x0 * x0 + y0 * y0);
        float const rt1 = alpaka::math::sqrt(acc, x1 * x1 + y1 * y1);
        float const rt2 = alpaka::math::sqrt(acc, x2 * x2 + y2 * y2);

        int const l0 = chainMdLayer(modules, mds, m0);
        int const l1 = chainMdLayer(modules, mds, m1);
        int const l2 = chainMdLayer(modules, mds, m2);
        int const nBarrel = (l0 <= 6 ? 1 : 0) + (l1 <= 6 ? 1 : 0) + (l2 <= 6 ? 1 : 0);
        int const nPS = chainMdIsPS(modules, mds, m0) + chainMdIsPS(modules, mds, m1) + chainMdIsPS(modules, mds, m2);

        float f[Params_ChainNode::kFeatures];
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
        f[12] = triplets.fakeScore()[t3];

        CMS_UNROLL_LOOP
        for (int i = 0; i < Params_ChainNode::kFeatures; ++i)
          nodes.features()[n][i] = chainSanitize(f[i]);

        // theta(c) = atan2(|c_xy|, c_z) in [0, pi]; the one fixed rz-angle definition, so the
        // edge kink theta12(inner) - theta01(outer) lands in [-pi, pi] with no wrap needed.
        nodes.phiC01()[n] = phi01;
        nodes.phiC12()[n] = phi12;
        nodes.thetaC01()[n] = alpaka::math::atan2(acc, alpaka::math::sqrt(acc, c01x * c01x + c01y * c01y), c01z);
        nodes.thetaC12()[n] = alpaka::math::atan2(acc, alpaka::math::sqrt(acc, c12x * c12x + c12y * c12y), c12z);
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // K5. Edge features (in registers) fused with the edge-MLP inference.
  // Transcription of prototype/Features.cc computeEdgeFeatures plus EdgeInference.cc.
  //
  // edgeFeatOut is a debug tap: when non-null the 14 edge floats are also written to
  // edgeFeatOut[e * kChainEdgeFeatures + k]. It is nullptr in normal running.
  struct ChainEdgeInference {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  ChainIncidenceConst mdIncidence,
                                  ChainIncidenceConst lsIncidence,
                                  ChainEdges edges,
                                  float* edgeFeatOut) const {
      static_assert(dnn::edgemlp::kInput == 2 * Params_ChainNode::kFeatures + kChainEdgeFeatures,
                    "EdgeNetworkWeights.h input size does not match the frozen feature layout");

      uint32_t const nEdges = static_cast<uint32_t>(edges.metadata().size());

      for (uint32_t e : cms::alpakatools::uniform_elements(acc, nEdges)) {
        uint8_t const etype = edges.type()[e];
        if (etype == 0u)
          continue;

        uint32_t const inner = edges.inner()[e];
        uint32_t const outer = edges.outer()[e];
        uint32_t const t3In = nodes.tripletIndex()[inner];
        uint32_t const t3Out = nodes.tripletIndex()[outer];

        float const kIn = nodes.features()[inner][0];
        float const kOut = nodes.features()[outer][0];
        float const dKappa = kIn - kOut;
        float const dKappaRel = alpaka::math::abs(acc, dKappa) /
                                (alpaka::math::abs(acc, kIn) + alpaka::math::abs(acc, kOut) + chainfeat::kEps);
        // sign(kappaSigned) == rotSign (the divisor is positive), so this is rotSign agreement.
        float const chargeAgree = ((kIn >= 0.f) == (kOut >= 0.f)) ? 1.f : 0.f;

        float const cdx = triplets.centerX()[t3In] - triplets.centerX()[t3Out];
        float const cdy = triplets.centerY()[t3In] - triplets.centerY()[t3Out];
        float const centerDist = alpaka::math::sqrt(acc, cdx * cdx + cdy * cdy);
        float const rIn = chainCleanRadius(triplets.radius()[t3In]);
        float const rOut = chainCleanRadius(triplets.radius()[t3Out]);
        float const centerDistRel = centerDist / chainMaxf(0.5f * (rIn + rOut), chainfeat::kEps);

        // Shared key: E1 -> the shared middle MD; E2 -> the shared LS and its first MD.
        unsigned int sharedMd;
        uint32_t degIn, degOut;
        if (etype == 1u) {
          unsigned int const m = segments.mdIndices()[triplets.segmentIndices()[t3In][1]][1];  // == md0(outer)
          sharedMd = m;
          degIn = mdIncidence.t3InOffsets()[m + 1u] - mdIncidence.t3InOffsets()[m];
          degOut = mdIncidence.t3OutOffsets()[m + 1u] - mdIncidence.t3OutOffsets()[m];
        } else {
          unsigned int const l = triplets.segmentIndices()[t3In][1];  // == ls0(outer)
          sharedMd = segments.mdIndices()[l][0];
          degIn = lsIncidence.t3InOffsets()[l + 1u] - lsIncidence.t3InOffsets()[l];
          degOut = lsIncidence.t3OutOffsets()[l + 1u] - lsIncidence.t3OutOffsets()[l];
        }
        int const sharedLayer = chainMdLayer(modules, mds, sharedMd);

        float ef[kChainEdgeFeatures];
        ef[0] = static_cast<float>(etype);
        ef[1] = dKappa;
        ef[2] = dKappaRel;
        ef[3] = chargeAgree;
        ef[4] = nodes.features()[inner][2] - nodes.features()[outer][2];
        ef[5] = chainDeltaPhi(nodes.phiC12()[inner], nodes.phiC01()[outer]);
        ef[6] = nodes.thetaC12()[inner] - nodes.thetaC01()[outer];
        ef[7] = centerDist;
        ef[8] = centerDistRel;
        ef[9] = static_cast<float>(sharedLayer);
        ef[10] = static_cast<float>(chainMdIsPS(modules, mds, sharedMd));
        ef[11] = (sharedLayer <= 6) ? 1.f : 0.f;
        ef[12] = static_cast<float>(degIn);
        ef[13] = static_cast<float>(degOut);

        CMS_UNROLL_LOOP
        for (int i = 0; i < kChainEdgeFeatures; ++i)
          ef[i] = chainSanitize(ef[i]);

        if (edgeFeatOut != nullptr) {
          for (int i = 0; i < kChainEdgeFeatures; ++i)
            edgeFeatOut[static_cast<size_t>(e) * kChainEdgeFeatures + i] = ef[i];
        }

        // Assemble in the frozen training order: ni_00..ni_12, no_00..no_12, ef_00..ef_13.
        float x[dnn::edgemlp::kInput];
        CMS_UNROLL_LOOP
        for (int i = 0; i < Params_ChainNode::kFeatures; ++i) {
          x[i] = nodes.features()[inner][i];
          x[Params_ChainNode::kFeatures + i] = nodes.features()[outer][i];
        }
        CMS_UNROLL_LOOP
        for (int i = 0; i < kChainEdgeFeatures; ++i)
          x[2 * Params_ChainNode::kFeatures + i] = ef[i];

        // Per-input preprocessing baked into the generated header, in this order:
        // optional log10(1 + x) -> clip -> standardize.
        CMS_UNROLL_LOOP
        for (int i = 0; i < dnn::edgemlp::kInput; ++i) {
          float v = x[i];
          if (dnn::edgemlp::kLog10p1[i])
            v = alpaka::math::log10(acc, 1.f + v);
          v = chainMinf(chainMaxf(v, dnn::edgemlp::kClipLo[i]), dnn::edgemlp::kClipHi[i]);
          x[i] = (v - dnn::edgemlp::kFeatMean[i]) / dnn::edgemlp::kFeatStd[i];
        }

        float x1[dnn::edgemlp::kHidden];
        float x2[dnn::edgemlp::kHidden];

        linear_layer<dnn::edgemlp::kInput, dnn::edgemlp::kHidden>(
            x, x1, dnn::edgemlp::wgt_l1, dnn::edgemlp::bias_l1);
        relu_activation<dnn::edgemlp::kHidden>(x1);

        linear_layer<dnn::edgemlp::kHidden, dnn::edgemlp::kHidden>(
            x1, x2, dnn::edgemlp::wgt_l2, dnn::edgemlp::bias_l2);
        relu_activation<dnn::edgemlp::kHidden>(x2);

        // Single output unit, NO sigmoid: K6 needs the logit (log-odds) so chain scores are sums.
        float logit = dnn::edgemlp::bias_out;
        CMS_UNROLL_LOOP
        for (int j = 0; j < dnn::edgemlp::kHidden; ++j)
          logit += x2[j] * dnn::edgemlp::wgt_out[j];

        edges.logOdds()[e] = logit;
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
