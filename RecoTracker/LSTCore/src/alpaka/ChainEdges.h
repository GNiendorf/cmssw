#ifndef RecoTracker_LSTCore_src_alpaka_ChainEdges_h
#define RecoTracker_LSTCore_src_alpaka_ChainEdges_h

#include <bit>
#include <numbers>
#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "HeterogeneousCore/AlpakaMath/interface/deltaPhi.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainEdgesSoA.h"
#include "RecoTracker/LSTCore/interface/ChainIncidenceSoA.h"
#include "RecoTracker/LSTCore/interface/ChainNodesSoA.h"
#include "RecoTracker/LSTCore/interface/MiniDoubletsSoA.h"
#include "RecoTracker/LSTCore/interface/ModulesSoA.h"
#include "RecoTracker/LSTCore/interface/SegmentsSoA.h"
#include "RecoTracker/LSTCore/interface/TripletsSoA.h"

#include "ChainGraph.h"  // chainCappedDegree: the edge decode must apply the SAME cap the count did
#include "EdgeNetworkWeights.h"
#include "NeuralNetwork.h"

// Chain-tracking edge layer: it builds and scores the graph the weld runs on.
//
// A NODE is one LST triplet under a dense index (ChainNodesSoA.h). An EDGE is an ordered pair of
// triplets that overlap on a detector element, oriented inner -> outer, in two families:
//   E1  the inner triplet's last mini-doublet is the outer triplet's first, so the pair spans one
//       layer more than either triplet alone;
//   E2  the two triplets share a whole line segment (two mini-doublets), so the pair spans the
//       same layers as its members.
// At each shared element the family is the full cross product -- every triplet ending there
// against every triplet starting there -- so the edge count is exactly the sum over shared
// elements of degIn * degOut. That product is what makes a collimated event expensive and is why
// the per-element degree cap (ChainConfig::degreeCap) exists.
//
// Stages:
//   ChainBuildEdges     exact-count enumeration of the E1 and E2 families
//   ChainNodeFeatures   the 13-float node row, the four chord angles the edge features need,
//                          and the node's (pt, |eta|) working-point cell
//   ChainEdgeInference  the 14 edge floats built in registers, then the 40 -> 32 -> 32 -> 3
//                          edge head; writes each edge's logOdds and its weld eligibility bar
//
// Nothing here is read by any existing LST stage: this file runs only when chain tracking is on,
// and only writes ChainNodes and ChainEdges.
//
// The feature layout and the operation order are part of the head's contract: the logits are
// validated by float parity, so reordering an expression or a feature moves decisions.

// Vectorise the LANE loop of the batched MLP primitives below. Vectorising across batch lanes
// cannot change any lane's arithmetic, so this is bit-neutral by construction; it exists purely to
// stop GCC completely unrolling the lane body and spilling the accumulators (see chainLinearBatch).
// Only the host compilers see it: the device backends run the unbatched B == 1 path.
#if defined(__GNUC__) && !defined(__CUDACC__) && !defined(__HIP__)
#define CHAIN_LANE_SIMD _Pragma("omp simd")
#else
#define CHAIN_LANE_SIMD
#endif

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  // Scalar helpers. These are plain comparisons, NOT fmax / fmin: they return the FIRST argument
  // whenever the comparison is false, NaN included, which is the behaviour the feature build is
  // written against.

  namespace chainfeat {
    constexpr float kEps = 1e-9f;
    constexpr float kBig = 1e12f;
  }  // namespace chainfeat

  // Wrap an angle difference into [-pi, pi].

  // Classification by bit pattern rather than by std::isnan / std::isinf. The standalone device
  // library is built with -Ofast, which implies -ffinite-math-only and lets the compiler fold the
  // library predicates to constants; the bit tests survive that and keep the NaN / Inf contract
  // meaningful on every backend.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool chainIsFinite(float value) {
    return (std::bit_cast<uint32_t>(value) & 0x7f800000u) != 0x7f800000u;
  }
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool chainIsNan(float value) {
    uint32_t const bits = std::bit_cast<uint32_t>(value);
    return (bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0u;
  }
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool chainIsInf(float value) {
    return (std::bit_cast<uint32_t>(value) & 0x7fffffffu) == 0x7f800000u;
  }

  // A degenerate triplet circle fit can leave the radius non-finite; map it to a large finite
  // stand-in so that no feature goes non-finite.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainCleanRadius(float radius) {
    return chainIsFinite(radius) ? radius : chainfeat::kBig;
  }

  // Final contract guard: no NaN / Inf may reach the head.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainSanitize(float value) {
    if (chainIsNan(value))
      return 0.f;
    if (chainIsInf(value))
      return (value > 0.f) ? chainfeat::kBig : -chainfeat::kBig;
    return value;
  }

  // Batched MLP primitives, shared by the edge head here and by the pixel-attach head in
  // ChainAttach.h.
  //
  // One evaluation of these small heads is LATENCY bound, not throughput bound: each hidden unit
  // accumulates its inputs in a single serial chain, so the vector unit spends most of its cycles
  // waiting on the previous multiply-add. These take a TRANSPOSED batch -- inputsT[j * BATCH + lane]
  // is input j of row `lane` -- and evaluate BATCH rows at once, which supplies BATCH independent
  // accumulator chains.
  //
  // The per-row operation order is untouched: for any (row, unit) the accumulation still starts at
  // the bias and runs over j ascending, exactly as NeuralNetwork.h linear_layer does, so every lane
  // is BIT-IDENTICAL to the unbatched result and no head's decisions can move.
  //
  // Three details are load-bearing for the SPEED, all three measured:
  //   - __restrict__. Without it the compiler must assume the staging block and the output block
  //     overlap, which forbids keeping an accumulator in a register at all.
  //   - unit blocking. kUnitBlock units are accumulated at a time so the kUnitBlock x BATCH
  //     accumulators live in vector registers for the whole input loop. One unit at a time is
  //     latency-bound again; all of them at once spills.
  //   - CHAIN_LANE_SIMD on the lane loop. Left to itself GCC completely unrolls the lane body
  //     before the vectoriser runs, scalarises the accumulators and spills them, which made the
  //     batched form 2.4x-3.6x SLOWER than the unbatched head instead of 1.7x-6.0x faster.
  // Callers instantiate this only with BATCH > 1 (the batched host path); the unbatched BATCH == 1
  // case stays on the original NeuralNetwork.h primitives so device code generation does not move.
  template <int IN_FEATURES, int OUT_FEATURES, int BATCH>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void chainLinearBatch(float const* __restrict__ inputsT,
                                                       float* __restrict__ outputsT,
                                                       float const (&weights)[IN_FEATURES][OUT_FEATURES],
                                                       float const (&biases)[OUT_FEATURES]) {
    constexpr int kUnitBlock = 8;
    static_assert(OUT_FEATURES % kUnitBlock == 0, "unit blocking needs a multiple of the block width");
    for (int unit0 = 0; unit0 < OUT_FEATURES; unit0 += kUnitBlock) {
      float accum[kUnitBlock][BATCH];
      for (int unit = 0; unit < kUnitBlock; ++unit) {
        CHAIN_LANE_SIMD
        for (int lane = 0; lane < BATCH; ++lane)
          accum[unit][lane] = biases[unit0 + unit];
      }
      for (int j = 0; j < IN_FEATURES; ++j) {
        float const* __restrict__ laneInputs = inputsT + j * BATCH;
        for (int unit = 0; unit < kUnitBlock; ++unit) {
          float const weight = weights[j][unit0 + unit];
          CHAIN_LANE_SIMD
          for (int lane = 0; lane < BATCH; ++lane)
            accum[unit][lane] += laneInputs[lane] * weight;
        }
      }
      for (int unit = 0; unit < kUnitBlock; ++unit) {
        CHAIN_LANE_SIMD
        for (int lane = 0; lane < BATCH; ++lane)
          outputsT[(unit0 + unit) * BATCH + lane] = accum[unit][lane];
      }
    }
  }

  template <int FEATURES, int BATCH>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void chainReluBatch(float* __restrict__ data) {
    CMS_UNROLL_LOOP
    for (int i = 0; i < FEATURES * BATCH; ++i)
      data[i] = (data[i] > 0.f) ? data[i] : 0.f;
  }

  // The single-output tail shared by the chain heads:
  // outputs[lane] = bias + sum_j inputsT[j][lane] * weights[j]. One BATCH-wide accumulator, so the
  // input loop is a single register-resident chain per lane.
  template <int FEATURES, int BATCH>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void chainDotBatch(float const* __restrict__ inputsT,
                                                    float* __restrict__ outputs,
                                                    float const (&weights)[FEATURES],
                                                    float bias) {
    float accum[BATCH];
    for (int lane = 0; lane < BATCH; ++lane)
      accum[lane] = bias;
    CMS_UNROLL_LOOP
    for (int j = 0; j < FEATURES; ++j) {
      float const weight = weights[j];
      for (int lane = 0; lane < BATCH; ++lane)
        accum[lane] += inputsT[j * BATCH + lane] * weight;
    }
    for (int lane = 0; lane < BATCH; ++lane)
      outputs[lane] = accum[lane];
  }

  // How many edges the host backends push through the edge head at a time (see ChainEdgeInference).
  static constexpr int kChainEdgeBatch = 16;

  // MiniDoublet detector categories.
  //
  // The head was trained on the ntuple's md_layer / md_type, so these reproduce the ntuple writer
  // exactly:
  //   md_layer = ph2_layer + 6 * (subdet == Endcap)         -> 1-6 barrel, 7-11 endcap
  //   md_type  = endcap ? (md_layer <= 2 ? ring <= 10 : ring <= 7) : (md_layer <= 3)
  // Note that the endcap branch tests the ALREADY-SHIFTED layer, so it always reduces to ring <= 7.
  // That is the trained definition and is reproduced verbatim; it is NOT the same predicate as
  // modules.moduleType(), which comes from the geometry file.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE int chainMdLayer(ModulesConst modules,
                                                  MiniDoubletsConst miniDoublets,
                                                  unsigned int mdIndex) {
    uint16_t const moduleIdx = miniDoublets.moduleIndices()[mdIndex];
    int const isEndcap = (modules.subdets()[moduleIdx] == Endcap) ? 1 : 0;
    return static_cast<int>(modules.layers()[moduleIdx]) + 6 * isEndcap;
  }

  ALPAKA_FN_ACC ALPAKA_FN_INLINE int chainMdIsPS(ModulesConst modules,
                                                 MiniDoubletsConst miniDoublets,
                                                 unsigned int mdIndex) {
    uint16_t const moduleIdx = miniDoublets.moduleIndices()[mdIndex];
    bool const isEndcap = (modules.subdets()[moduleIdx] == Endcap);
    int const layer = static_cast<int>(modules.layers()[moduleIdx]) + (isEndcap ? 6 : 0);
    int const ring = static_cast<int>(modules.rings()[moduleIdx]);
    if (isEndcap)
      return (layer <= 2) ? (ring <= 10 ? 1 : 0) : (ring <= 7 ? 1 : 0);
    return (layer <= 3) ? 1 : 0;
  }

  // The three anchor MDs of a triplet, innermost first: {LS0.md0, LS0.md1, LS1.md1}, the same
  // convention as the ntuple's t3_md0 / t3_md1 / t3_md2.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void chainNodeMDs(TripletsConst triplets,
                                                   SegmentsConst segments,
                                                   uint32_t tripletIdx,
                                                   unsigned int& firstMD,
                                                   unsigned int& midMD,
                                                   unsigned int& lastMD) {
    unsigned int const innerSeg = triplets.segmentIndices()[tripletIdx][0];
    unsigned int const outerSeg = triplets.segmentIndices()[tripletIdx][1];
    firstMD = segments.mdIndices()[innerSeg][0];
    midMD = segments.mdIndices()[innerSeg][1];
    lastMD = segments.mdIndices()[outerSeg][1];
  }

  // Largest key k in [0, nKeys) with prefix[k] <= value, for a non-decreasing prefix with
  // prefix[0] == 0 and prefix[nKeys] > value. Keys with a zero edge product have equal consecutive
  // prefix entries and are skipped by construction, so the returned key always has
  // degIn * degOut > 0. ~17 cached reads for a few hundred thousand keys.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainDecodeKey(uint32_t const* prefix, uint32_t nKeys, uint32_t value) {
    uint32_t lowKey = 0u;
    uint32_t highKey = nKeys;
    while (lowKey + 1u < highKey) {
      uint32_t const midKey = lowKey + (highKey - lowKey) / 2u;
      if (prefix[midKey] <= value)
        lowKey = midKey;
      else
        highKey = midKey;
    }
    return lowKey;
  }

  // ChainBuildEdges. Exact-count edge enumeration.
  //
  // Rows [0, nEdgesE1) are the MD-keyed E1 family and rows [nEdgesE1, nEdgesE1 + nEdgesE2) the
  // Segment-keyed E2 family.
  // Each thread owns one row: it binary-searches the edge-product prefix for its shared key, splits
  // the remainder by that key's out-degree, and reads the two CSR item slots. No atomic, no
  // compaction, no reservation.
  //
  // Orientation: the INNER endpoint comes from the "in" slice (triplets ending at the key) and the
  // OUTER endpoint from the "out" slice (triplets starting at the key), with the "in" slice as the
  // outer loop, so the row split is (i, j) = (remainder / degOut, remainder % degOut).
  //
  // `degOut` and the implied `degIn` bound are the CAPPED degrees (ChainConfig::degreeCap, applied
  // through the same chainCappedDegree the counting prefix used), so this stays the exact inverse
  // of that prefix -- which it must be, or a thread decodes a row that was never counted. The CSR
  // OFFSETS are uncapped, so `i < min(degIn, C)` and `j < min(degOut, C)` simply address the FIRST
  // C entries of each of the key's two full slices and every read is in bounds by construction. At
  // kChainDegreeCapOff both calls are the identity.
  struct ChainBuildEdges {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TripletsConst triplets,
                                  SegmentsConst segments,
                                  ChainNodesConst nodes,
                                  ChainIncidenceConst mdIncidence,
                                  ChainIncidenceConst lsIncidence,
                                  ChainEdges edges,
                                  uint32_t nEdgesE1,
                                  uint32_t nEdgesE2,
                                  uint32_t degCap) const {
      uint32_t const nMDKeys = static_cast<uint32_t>(mdIncidence.metadata().size()) - 1u;
      uint32_t const nLSKeys = static_cast<uint32_t>(lsIncidence.metadata().size()) - 1u;

      // edges.nE1Exact() / nE2Exact() are never written and never read: the host carries nEdgesE1
      // and nEdgesE2 itself. The two SOA_SCALARs stay in ChainEdgesSoA on purpose -- removing a
      // layout member shifts every later column's base address, and this codebase has a recorded
      // timing regression from exactly that.

      for (uint32_t edgeIdx : cms::alpakatools::uniform_elements(acc, nEdgesE1 + nEdgesE2)) {
        bool const isE1 = (edgeIdx < nEdgesE1);

        uint32_t const* prodPrefix = (isE1 ? mdIncidence.edgeProdPrefix() : lsIncidence.edgeProdPrefix()).data();
        uint32_t const* inOffsets = (isE1 ? mdIncidence.t3InOffsets() : lsIncidence.t3InOffsets()).data();
        uint32_t const* outOffsets = (isE1 ? mdIncidence.t3OutOffsets() : lsIncidence.t3OutOffsets()).data();
        uint32_t const* inItems = (isE1 ? nodes.mdT3InItems() : nodes.lsT3InItems()).data();
        uint32_t const* outItems = (isE1 ? nodes.mdT3OutItems() : nodes.lsT3OutItems()).data();
        uint32_t const nKeys = isE1 ? nMDKeys : nLSKeys;
        uint32_t const localEdge = isE1 ? edgeIdx : (edgeIdx - nEdgesE1);

        uint32_t const sharedKey = chainDecodeKey(prodPrefix, nKeys, localEdge);
        uint32_t const remainder = localEdge - prodPrefix[sharedKey];
        uint32_t const degOut = chainCappedDegree(outOffsets[sharedKey + 1u] - outOffsets[sharedKey], degCap);
        ALPAKA_ASSERT_ACC(degOut > 0u);
        ALPAKA_ASSERT_ACC(remainder / degOut <
                          chainCappedDegree(inOffsets[sharedKey + 1u] - inOffsets[sharedKey], degCap));

        uint32_t const inner = inItems[inOffsets[sharedKey] + remainder / degOut];
        uint32_t const outer = outItems[outOffsets[sharedKey] + remainder % degOut];

        // Type 0 is an enumeration hole: a row the cross product produced but that is not a valid
        // edge. The weld and the head both skip those rows.
        uint8_t edgeType = isE1 ? 1u : 2u;
        if (inner == outer) {
          // Would need md2 == md0 (E1) or ls1 == ls0 (E2) inside one triplet.
          edgeType = 0u;
        } else if (!isE1) {
          // An E2 pair that is also an E1 pair would need md2(inner) == md0(outer) == md1(inner),
          // impossible for a triplet with three distinct MDs. Kept as an O(1) test so that the two
          // families stay provably disjoint.
          unsigned int const t3Inner = nodes.tripletIndex()[inner];
          unsigned int const t3Outer = nodes.tripletIndex()[outer];
          unsigned int const md2Inner = segments.mdIndices()[triplets.segmentIndices()[t3Inner][1]][1];
          unsigned int const md0Outer = segments.mdIndices()[triplets.segmentIndices()[t3Outer][0]][0];
          if (md2Inner == md0Outer)
            edgeType = 0u;
        }

        edges.inner()[edgeIdx] = inner;
        edges.outer()[edgeIdx] = outer;
        edges.type()[edgeIdx] = edgeType;
        edges.logOdds()[edgeIdx] = 0.f;
        // The stable weld tie-break (ChainWeld.h). XOR is a bijection in each argument, so with a
        // collision-free node stableId the tie word is distinct for any two edges that share an
        // endpoint, which is exactly the uniqueness the weld argmax needs in either direction.
        edges.tie()[edgeIdx] = nodes.stableId()[inner] ^ nodes.stableId()[outer];
      }
    }
  };

  // ChainNodeFeatures. The 13-float node row (the layout is fixed by the trained head, see Params_ChainNode),
  // plus the four chord angles the edge build needs and the node's working-point cell. Everything
  // here is built from the triplet's three anchor hits and its circle fit.
  struct ChainNodeFeatures {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst miniDoublets,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodes nodes) const {
      uint32_t const nNodes = static_cast<uint32_t>(nodes.metadata().size());

      for (uint32_t nodeIdx : cms::alpakatools::uniform_elements(acc, nNodes)) {
        uint32_t const tripletIdx = nodes.tripletIndex()[nodeIdx];
        unsigned int firstMD, midMD, lastMD;
        chainNodeMDs(triplets, segments, tripletIdx, firstMD, midMD, lastMD);

        float const firstX = miniDoublets.anchorX()[firstMD], firstY = miniDoublets.anchorY()[firstMD],
                    firstZ = miniDoublets.anchorZ()[firstMD];
        float const midX = miniDoublets.anchorX()[midMD], midY = miniDoublets.anchorY()[midMD],
                    midZ = miniDoublets.anchorZ()[midMD];
        float const lastX = miniDoublets.anchorX()[lastMD], lastY = miniDoublets.anchorY()[lastMD],
                    lastZ = miniDoublets.anchorZ()[lastMD];

        // The three chords between the anchor hits. Every position-dependent feature is built from
        // these differences, so the row does not depend on where the triplet sits in the detector.
        float const chord01X = midX - firstX, chord01Y = midY - firstY, chord01Z = midZ - firstZ;
        float const chord12X = lastX - midX, chord12Y = lastY - midY, chord12Z = lastZ - midZ;
        float const chord02X = lastX - firstX, chord02Y = lastY - firstY, chord02Z = lastZ - firstZ;

        // rotSign = sign of the z component of cross(chord01, chord12); collinear counts as +1.
        float const cross = chord01X * chord12Y - chord01Y * chord12X;
        float const rotSign = (cross >= 0.f) ? 1.f : -1.f;

        float const radius = chainCleanRadius(triplets.radius()[tripletIdx]);
        float const kappaSigned = rotSign / alpaka::math::max(acc, radius, float{chainfeat::kEps});
        float const log10Radius = alpaka::math::log10(acc, alpaka::math::max(acc, radius, 1e-3f));

        float const chord02XY = alpaka::math::sqrt(acc, chord02X * chord02X + chord02Y * chord02Y);
        float const tanLambda = chord02Z / alpaka::math::max(acc, chord02XY, float{chainfeat::kEps});
        float const chordEta =
            alpaka::math::asinh(acc, chord02Z / alpaka::math::max(acc, chord02XY, float{chainfeat::kEps}));

        float const phi01 = alpaka::math::atan2(acc, chord01Y, chord01X);
        float const phi12 = alpaka::math::atan2(acc, chord12Y, chord12X);
        float const dphi01 = cms::alpakatools::deltaPhi(acc, phi01, phi12);

        float const firstRt = alpaka::math::sqrt(acc, firstX * firstX + firstY * firstY);
        float const midRt = alpaka::math::sqrt(acc, midX * midX + midY * midY);
        float const lastRt = alpaka::math::sqrt(acc, lastX * lastX + lastY * lastY);

        int const layer0 = chainMdLayer(modules, miniDoublets, firstMD);
        int const layer1 = chainMdLayer(modules, miniDoublets, midMD);
        int const layer2 = chainMdLayer(modules, miniDoublets, lastMD);
        int const nBarrel = (layer0 <= 6 ? 1 : 0) + (layer1 <= 6 ? 1 : 0) + (layer2 <= 6 ? 1 : 0);
        int const nPSModules = chainMdIsPS(modules, miniDoublets, firstMD) + chainMdIsPS(modules, miniDoublets, midMD) +
                               chainMdIsPS(modules, miniDoublets, lastMD);

        float features[Params_ChainNode::kFeatures];
        features[0] = kappaSigned;
        features[1] = log10Radius;
        features[2] = tanLambda;
        features[3] = chordEta;
        features[4] = dphi01;
        features[5] = chord01Z;
        features[6] = chord12Z;
        features[7] = midRt - firstRt;
        features[8] = lastRt - midRt;
        features[9] = static_cast<float>(layer0);
        features[10] = static_cast<float>(nBarrel);
        features[11] = static_cast<float>(nPSModules);
        features[12] = triplets.fakeScore()[tripletIdx];

        CMS_UNROLL_LOOP
        for (int i = 0; i < Params_ChainNode::kFeatures; ++i)
          nodes.features()[nodeIdx][i] = chainSanitize(features[i]);

        // This node's working-point cell, on LST's T3-DNN binning and built from the SAME two
        // quantities t3dnn::runInference bins its own working points on: the T3's radius-derived
        // pt and the |eta| of the anchor hit of its FIRST MD. Both are free here -- the radius is
        // already in a register and firstMD is already resolved -- while in the per-edge kernel they
        // would cost a triplet -> segment -> md index chase. ChainEdgeInference turns this cell into the edge's
        // weld eligibility bar.
        float const wpPt = radius * k2Rinv1GeVf * 2.f;
        float const wpEta = alpaka::math::abs(acc, miniDoublets.anchorEta()[firstMD]);
        unsigned int const wpPtBin = (wpPt > 5.f) ? 1u : 0u;
        unsigned int const wpEtaBin =
            (wpEta > 2.5f) ? (dnn::kEtaBins - 1) : static_cast<unsigned int>(wpEta / dnn::kEtaSize);
        nodes.wpBin()[nodeIdx] = static_cast<uint8_t>(wpPtBin * dnn::kEtaBins + wpEtaBin);

        // theta(chord) = atan2(|chord_xy|, chord_z) in [0, pi]; one fixed rz-angle definition, so
        // that the edge kink theta12(inner) - theta01(outer) lands in [-pi, pi] with no wrap needed.
        nodes.phiC01()[nodeIdx] = phi01;
        nodes.phiC12()[nodeIdx] = phi12;
        nodes.thetaC01()[nodeIdx] =
            alpaka::math::atan2(acc, alpaka::math::sqrt(acc, chord01X * chord01X + chord01Y * chord01Y), chord01Z);
        nodes.thetaC12()[nodeIdx] =
            alpaka::math::atan2(acc, alpaka::math::sqrt(acc, chord12X * chord12X + chord12Y * chord12Y), chord12Z);
      }
    }
  };

  // ChainEdgeInference. The 14 edge features, built in registers and fused with the edge head's inference. The
  // head's input row is the frozen concatenation (inner node's 13, outer node's 13, edge's 14).
  //
  // edgeFeatOut is a debug tap: when non-null the 14 edge floats are also written to
  // edgeFeatOut[edgeIdx * kChainEdgeFeatures + i]. It is nullptr in normal running.
  struct ChainEdgeInference {
    template <typename TAcc>
    ALPAKA_FN_ACC void operator()(TAcc const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst miniDoublets,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  ChainIncidenceConst mdIncidence,
                                  ChainIncidenceConst lsIncidence,
                                  ChainEdges edges,
                                  float thetaEdgeE1,
                                  float thetaEdgeE2,
                                  bool edgeWpTable,
                                  float* edgeFeatOut) const {
      static_assert(dnn::edgemlp::kInput == 2 * Params_ChainNode::kFeatures + kChainEdgeFeatures,
                    "EdgeNetworkWeights.h input size does not match the frozen feature layout");
      static_assert(dnn::edgemlp::kOutputs == 3, "arm F's edge head is 3-class: fake / prompt / displaced");
      static_assert(dnn::edgemlp::kWpBins == dnn::kPtBins * dnn::kEtaBins,
                    "the edge working-point table must be on LST's kPtBins x kEtaBins binning");

      // On the host backends the head runs kBatch edges at a time: the 40 preprocessed inputs of
      // each surviving edge are staged TRANSPOSED into stagedInputsT and retired in blocks, which
      // interleaves kBatch independent accumulator chains through the 40 -> 32 -> 32 -> 3
      // evaluation. Nothing about an individual edge changes -- its features, its preprocessing and
      // its per-unit accumulation order are the same statements as in the unbatched form -- so every
      // logit is bit-identical. The device path takes kBatch == 1 and is exactly the unbatched code.
      constexpr int kBatch = cms::alpakatools::requires_single_thread_per_block_v<TAcc> ? kChainEdgeBatch : 1;
      constexpr int kNetInputs = dnn::edgemlp::kInput;
      constexpr int kHiddenUnits = dnn::edgemlp::kHidden;

      alignas(64) float stagedInputsT[kNetInputs * kBatch];
      constexpr int kClasses = dnn::edgemlp::kOutputs;
      uint32_t batchEdge[kBatch];
      // TRANSPOSED like every other staged block: logits[classIdx * kBatch + lane]
      float logits[kClasses * kBatch];
      uint8_t batchFamily[kBatch], batchCell[kBatch];
      int nBatch = 0;
      for (int i = 0; i < kNetInputs * kBatch; ++i)
        stagedInputsT[i] = 0.f;  // the tail lanes of a partial block are evaluated and discarded

      auto flush = [&]() {
        alignas(64) float hidden1[kHiddenUnits * kBatch];
        alignas(64) float hidden2[kHiddenUnits * kBatch];
        if constexpr (kBatch == 1) {
          // Unbatched (device) path, on the shared primitives, so this backend's code generation is
          // exactly what it was before the batching existed.
          linear_layer<kNetInputs, kHiddenUnits>(stagedInputsT, hidden1, dnn::edgemlp::wgt_l1, dnn::edgemlp::bias_l1);
          relu_activation<kHiddenUnits>(hidden1);
          linear_layer<kHiddenUnits, kHiddenUnits>(hidden1, hidden2, dnn::edgemlp::wgt_l2, dnn::edgemlp::bias_l2);
          relu_activation<kHiddenUnits>(hidden2);
          // THREE output units, NO softmax: the pipeline works on logit MARGINS, exactly as the
          // 3-class chain gate does, and the stored scalar has to stay a log-odds so that chain
          // scores remain sums.
          CMS_UNROLL_LOOP
          for (int classIdx = 0; classIdx < kClasses; ++classIdx) {
            float logit = dnn::edgemlp::bias_out[classIdx];
            CMS_UNROLL_LOOP
            for (int j = 0; j < kHiddenUnits; ++j)
              logit += hidden2[j] * dnn::edgemlp::wgt_out[classIdx][j];
            logits[classIdx] = logit;
          }
        } else {
          chainLinearBatch<kNetInputs, kHiddenUnits, kBatch>(
              stagedInputsT, hidden1, dnn::edgemlp::wgt_l1, dnn::edgemlp::bias_l1);
          chainReluBatch<kHiddenUnits, kBatch>(hidden1);
          chainLinearBatch<kHiddenUnits, kHiddenUnits, kBatch>(
              hidden1, hidden2, dnn::edgemlp::wgt_l2, dnn::edgemlp::bias_l2);
          chainReluBatch<kHiddenUnits, kBatch>(hidden2);
          // The batched linear primitive blocks its output units by 8, so it cannot take 3; the
          // output layer is instead three dot products over the same staged hidden block. wgt_out
          // is stored [class][hidden] -- the ONE place the wgt[in][out] convention is transposed --
          // so each class's weight vector is contiguous for chainDotBatch.
          CMS_UNROLL_LOOP
          for (int classIdx = 0; classIdx < kClasses; ++classIdx)
            chainDotBatch<kHiddenUnits, kBatch>(
                hidden2, logits + classIdx * kBatch, dnn::edgemlp::wgt_out[classIdx], dnn::edgemlp::bias_out[classIdx]);
        }
        for (int lane = 0; lane < nBatch; ++lane) {
          float const zFake = logits[0 * kBatch + lane];
          float const mP = logits[1 * kBatch + lane] - zFake;  // prompt-true margin over fake
          float const mD = logits[2 * kBatch + lane] - zFake;  // displaced-true margin over fake
          // The ONE scalar the rest of the pipeline consumes (the weld argmax key, the chain score
          // summed against lambdaLen, chain features 2/3/4/18): mX = max(mP, mD), i.e. "how much
          // more this edge looks like SOME real track than like a fake". Its scale is pinned to the
          // previous head's by the affine map baked into the output layer, so the claim order key
          // and the gate's inputs keep their distribution across a retrain.
          edges.logOdds()[batchEdge[lane]] = alpaka::math::max(acc, mP, mD);
          // Eligibility is an OR over the two per-cell tables, which is the thing a single scalar
          // bar cannot express: an edge is eligible if it is prompt-like enough OR displaced-like
          // enough for its cell. The result is materialised into weldBar as a degenerate bar
          // (-/+ 1e30) so that the weld keeps its single `logOdds < weldBar` test.
          if (edgeWpTable) {
            uint32_t const wpIndex = static_cast<uint32_t>(batchFamily[lane]) * dnn::edgemlp::kWpBins + batchCell[lane];
            bool const eligible = (mP >= dnn::edgemlp::kWpPrompt[wpIndex]) || (mD >= dnn::edgemlp::kWpDisp[wpIndex]);
            edges.weldBar()[batchEdge[lane]] = eligible ? -1e30f : 1e30f;
          }
        }
        nBatch = 0;
      };

      uint32_t const nEdges = static_cast<uint32_t>(edges.metadata().size());

      for (uint32_t edgeIdx : cms::alpakatools::uniform_elements(acc, nEdges)) {
        uint8_t const edgeType = edges.type()[edgeIdx];
        if (edgeType == 0u)
          continue;

        uint32_t const inner = edges.inner()[edgeIdx];
        uint32_t const outer = edges.outer()[edgeIdx];
        uint32_t const tripletInner = nodes.tripletIndex()[inner];
        uint32_t const tripletOuter = nodes.tripletIndex()[outer];

        // The per-edge eligibility decision needs BOTH class margins, so it is taken in flush()
        // where the three logits live; here we only stage this edge's cell. With edgeWpTable false
        // the bar is instead the per-family scalar, taken on the stored mX.
        uint8_t const wpCell = nodes.wpBin()[inner];
        if (!edgeWpTable)
          edges.weldBar()[edgeIdx] = (edgeType == 1u) ? thetaEdgeE1 : thetaEdgeE2;

        float const kappaIn = nodes.features()[inner][0];
        float const kappaOut = nodes.features()[outer][0];
        float const dKappa = kappaIn - kappaOut;
        float const dKappaRel = alpaka::math::abs(acc, dKappa) /
                                (alpaka::math::abs(acc, kappaIn) + alpaka::math::abs(acc, kappaOut) + chainfeat::kEps);
        // sign(kappaSigned) == rotSign (the divisor is positive), so this is rotSign agreement.
        float const chargeAgree = ((kappaIn >= 0.f) == (kappaOut >= 0.f)) ? 1.f : 0.f;

        float const centerDX = triplets.centerX()[tripletInner] - triplets.centerX()[tripletOuter];
        float const centerDY = triplets.centerY()[tripletInner] - triplets.centerY()[tripletOuter];
        float const centerDist = alpaka::math::sqrt(acc, centerDX * centerDX + centerDY * centerDY);
        float const radiusInner = chainCleanRadius(triplets.radius()[tripletInner]);
        float const radiusOuter = chainCleanRadius(triplets.radius()[tripletOuter]);
        float const centerDistRel =
            centerDist / alpaka::math::max(acc, 0.5f * (radiusInner + radiusOuter), float{chainfeat::kEps});

        // Shared element: E1 -> the shared middle MD; E2 -> the shared line segment and its first
        // MD. The shared object is always the inner node's "in" side, so its DENSE incidence key is
        // the one already parked on that node; the raw index is still needed for the layer lookup.
        unsigned int sharedMd;
        uint32_t degIn, degOut;
        if (edgeType == 1u) {
          // the inner triplet's last MD, which is the outer triplet's first
          unsigned int const midMd = segments.mdIndices()[triplets.segmentIndices()[tripletInner][1]][1];
          sharedMd = midMd;
          uint32_t const incidenceKey = nodes.mdKeyIn()[inner];
          degIn = mdIncidence.t3InOffsets()[incidenceKey + 1u] - mdIncidence.t3InOffsets()[incidenceKey];
          degOut = mdIncidence.t3OutOffsets()[incidenceKey + 1u] - mdIncidence.t3OutOffsets()[incidenceKey];
        } else {
          unsigned int const sharedLs = triplets.segmentIndices()[tripletInner][1];  // == ls0(outer)
          sharedMd = segments.mdIndices()[sharedLs][0];
          uint32_t const incidenceKey = nodes.lsKeyIn()[inner];
          degIn = lsIncidence.t3InOffsets()[incidenceKey + 1u] - lsIncidence.t3InOffsets()[incidenceKey];
          degOut = lsIncidence.t3OutOffsets()[incidenceKey + 1u] - lsIncidence.t3OutOffsets()[incidenceKey];
        }
        int const sharedLayer = chainMdLayer(modules, miniDoublets, sharedMd);

        float edgeFeatures[kChainEdgeFeatures];
        edgeFeatures[0] = static_cast<float>(edgeType);
        edgeFeatures[1] = dKappa;
        edgeFeatures[2] = dKappaRel;
        edgeFeatures[3] = chargeAgree;
        edgeFeatures[4] = nodes.features()[inner][2] - nodes.features()[outer][2];
        edgeFeatures[5] = cms::alpakatools::deltaPhi(acc, nodes.phiC12()[inner], nodes.phiC01()[outer]);
        edgeFeatures[6] = nodes.thetaC12()[inner] - nodes.thetaC01()[outer];
        edgeFeatures[7] = centerDist;
        edgeFeatures[8] = centerDistRel;
        edgeFeatures[9] = static_cast<float>(sharedLayer);
        edgeFeatures[10] = static_cast<float>(chainMdIsPS(modules, miniDoublets, sharedMd));
        edgeFeatures[11] = (sharedLayer <= 6) ? 1.f : 0.f;
        edgeFeatures[12] = static_cast<float>(degIn);
        edgeFeatures[13] = static_cast<float>(degOut);

        CMS_UNROLL_LOOP
        for (int i = 0; i < kChainEdgeFeatures; ++i)
          edgeFeatures[i] = chainSanitize(edgeFeatures[i]);

        if (edgeFeatOut != nullptr) {
          for (int i = 0; i < kChainEdgeFeatures; ++i)
            edgeFeatOut[static_cast<size_t>(edgeIdx) * kChainEdgeFeatures + i] = edgeFeatures[i];
        }

        // Assemble in the trained input order: inner node's 13, outer node's 13, then the 14 edge
        // floats, into this row's lane of the transposed staging block.
        float* laneRow = stagedInputsT + nBatch;
        CMS_UNROLL_LOOP
        for (int i = 0; i < Params_ChainNode::kFeatures; ++i) {
          laneRow[i * kBatch] = nodes.features()[inner][i];
          laneRow[(Params_ChainNode::kFeatures + i) * kBatch] = nodes.features()[outer][i];
        }
        CMS_UNROLL_LOOP
        for (int i = 0; i < kChainEdgeFeatures; ++i)
          laneRow[(2 * Params_ChainNode::kFeatures + i) * kBatch] = edgeFeatures[i];

        // Per-input preprocessing, baked into the generated header, in this order:
        // optional log10(1 + x) -> clip -> standardize.
        CMS_UNROLL_LOOP
        for (int i = 0; i < dnn::edgemlp::kInput; ++i) {
          float value = laneRow[i * kBatch];
          if (dnn::edgemlp::kLog10p1[i])
            value = alpaka::math::log10(acc, 1.f + value);
          value =
              alpaka::math::min(acc, alpaka::math::max(acc, value, dnn::edgemlp::kClipLo[i]), dnn::edgemlp::kClipHi[i]);
          laneRow[i * kBatch] = (value - dnn::edgemlp::kFeatMean[i]) / dnn::edgemlp::kFeatStd[i];
        }

        batchEdge[nBatch] = edgeIdx;
        batchFamily[nBatch] = static_cast<uint8_t>(edgeType - 1u);
        batchCell[nBatch] = wpCell;
        ++nBatch;
        if (nBatch == kBatch)
          flush();
      }
      if (nBatch > 0)
        flush();
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
