#ifndef RecoTracker_LSTCore_src_alpaka_ChainWeld_h
#define RecoTracker_LSTCore_src_alpaka_ChainWeld_h

#include <bit>
#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainConfig.h"
#include "RecoTracker/LSTCore/interface/ChainEdgesSoA.h"
#include "RecoTracker/LSTCore/interface/ChainNodesSoA.h"
#include "RecoTracker/LSTCore/interface/ChainsSoA.h"
#include "RecoTracker/LSTCore/interface/MiniDoubletsSoA.h"
#include "RecoTracker/LSTCore/interface/ModulesSoA.h"
#include "RecoTracker/LSTCore/interface/SegmentsSoA.h"
#include "RecoTracker/LSTCore/interface/TripletsSoA.h"

#include "ChainEdges.h"
#include "ChainGraph.h"

// Chain-tracking weld: the stage that turns the scored triplet graph into chains.
//
// The graph model. A NODE is one LST triplet under a dense index (ChainNodesSoA.h). An EDGE is an
// ordered pair of triplets that overlap on a detector element, oriented inner -> outer, in two
// families: an E1 edge shares one mini-doublet (the inner triplet's last is the outer triplet's
// first) and so advances the track by a layer, an E2 edge shares a whole line segment and does
// not. ChainEdges.h enumerates and scores them; `logOdds` -- the edge head's margin of "some real
// track" over "fake" -- is the only edge quantity this stage reads.
//
// The weld keeps at most one out-edge and one in-edge per node, so the welded subgraph is a set of
// disjoint simple paths. A CHAIN is one such path, and it is the object the gate judges and the
// claim arbitrates. The matching is an iterated MUTUAL BEST: per sweep every node takes the argmax
// over its still-eligible incident edges (ChainWeldArgmax) and an edge that is the argmax at BOTH of its
// endpoints is welded (ChainWeldMutual). An edge competes only while both of the slots it needs are still
// free, so a node cannot advance past its own best edge until another weld frees the blocker;
// kChainWeldSweeps bounds how far that iteration runs.
//
// Stages:
//   ChainWeldArgmax     per-node best eligible edge, packed-key atomicMax  (x kChainWeldSweeps)
//   ChainWeldMutual     apply the mutual-best pairs                        (x kChainWeldSweeps)
//   ChainCountChains    head detection plus the path-length walk
//   ChainPrefixChains   exclusive prefixes -> chain index and node offset
//   ChainEmitChains     node / edge / deduped-MD CSR fill, nLayers, chain score
//   ChainTrimTerminals  the terminal trim
//
// The arithmetic here is order-sensitive: the combined fit below is validated by float parity, and
// reordering an expression moves its last bits and with them which chains get trimmed.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  // Plain comparisons, NOT fmax / fmin: they return the FIRST argument whenever the comparison is
  // false, NaN included, which is the behaviour the fit code below is written against.

  // The packed weld key.
  //
  // The weld ranks an edge by "higher logOdds first, and a stable tie-break second". The tie-break
  // cannot be the edge index: that index is a position in an enumeration whose CSR slices are
  // filled by an atomicAdd cursor over a node numbering that itself descends from LST's atomicAdd
  // triplet slots, so it permutes between two identical runs and between the CPU and CUDA
  // backends. Exact-logit ties are common (~1e4 same-key adjacent pairs per event, because
  // duplicate feature rows give bit-identical MLP outputs), so an index tie-break would make the
  // welded chain set run-dependent. The tie operand is instead ChainEdges::tie, the XOR of the two
  // endpoints' stableId (ChainNodesSoA.h), which is a function of hit rows alone.
  //
  // The packed 64-bit key is a single unsigned total order:
  //   high word = the standard monotone float -> uint32 map, so a larger float is a larger word;
  //   low  word = the stable tie word, larger wins.
  // Key 0 is a safe "no edge" sentinel: only eligible edges take part and, with a non-negative
  // eligibility bar, every eligible logOdds maps to a high word >= 0x80000000.
  //
  // The key is deliberately not decodable back to an edge index. ChainWeldMutual does not need it to be: it is
  // edge-parallel, and each edge tests whether it is itself the argmax at both of its endpoints.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainOrderFloat(float value) {
    uint32_t const bits = std::bit_cast<uint32_t>(value);
    return (bits >> 31) ? (bits ^ 0xffffffffu) : (bits ^ 0x80000000u);
  }

  // Its exact inverse: a value that went through an atomicMax on the order key comes back
  // unchanged. Used by the attach contention (ChainAttach.h), which packs logits the same way.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainUnorderFloat(uint32_t orderKey) {
    uint32_t const bits = (orderKey & 0x80000000u) ? (orderKey & 0x7fffffffu) : ~orderKey;
    return std::bit_cast<float>(bits);
  }

  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint64_t chainWeldKey(float logOdds, uint32_t tieWord) {
    return (static_cast<uint64_t>(chainOrderFloat(logOdds)) << 32) | static_cast<uint64_t>(tieWord);
  }

  // Incidence degree product at an edge's junction, i.e. how many triplet pairs compete for the
  // same shared element: for an E1 edge the junction is the shared middle MD, for an E2 edge the
  // shared line segment. This is the per-edge summand of chain feature 14 and is read from the
  // same two incidence CSRs the chain features read, so the two agree by construction. The offsets
  // carry the per-side degree cap, which cannot move a row across a knee at or below that cap.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE long long chainWeldDegProd(ChainEdgesConst const& edges,
                                                            ChainNodesConst const& nodes,
                                                            ChainIncidenceConst const& mdInc,
                                                            ChainIncidenceConst const& lsInc,
                                                            uint32_t edgeIdx) {
    uint32_t const innerNode = edges.inner()[edgeIdx];
    if (edges.type()[edgeIdx] == 1u) {
      uint32_t const mdKey = nodes.mdKeyIn()[innerNode];
      long long const degIn = mdInc.t3InOffsets()[mdKey + 1u] - mdInc.t3InOffsets()[mdKey];
      long long const degOut = mdInc.t3OutOffsets()[mdKey + 1u] - mdInc.t3OutOffsets()[mdKey];
      return degIn * degOut;
    }
    uint32_t const lsKey = nodes.lsKeyIn()[innerNode];
    long long const degIn = lsInc.t3InOffsets()[lsKey + 1u] - lsInc.t3InOffsets()[lsKey];
    long long const degOut = lsInc.t3OutOffsets()[lsKey + 1u] - lsInc.t3OutOffsets()[lsKey];
    return degIn * degOut;
  }

  // The occupancy-conditioned weld key: one extra bit of order in front of logOdds, so that the E2
  // family wins a slot outright, but only at junctions with at least kChainWeldFamilyDegKnee
  // competing triplet pairs. ChainConfig.h carries the justification for both the ordering and the
  // conditioning. Every row goes through the SAME monotone transform, so the comparison stays one
  // unsigned total order and the 32-bit stable tie word survives intact: the family bit is taken
  // out of the float's last mantissa bit, never out of the tie word.
  //
  // This changes only WHICH edge wins a slot. The chain score still sums the untouched logOdds, so
  // the gate, the claim order key and every chain feature are bit for bit what they were.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint64_t chainWeldKeyDense(float logOdds,
                                                            uint32_t tieWord,
                                                            uint8_t edgeType,
                                                            long long degProd) {
    if constexpr (kChainWeldFamilyDegKnee <= 0)
      return chainWeldKey(logOdds, tieWord);
    bool const familyFirst = (edgeType == 2u) && (degProd >= kChainWeldFamilyDegKnee);
    uint32_t const orderWord = (chainOrderFloat(logOdds) >> 1) | (familyFirst ? 0x80000000u : 0u);
    return (static_cast<uint64_t>(orderWord) << 32) | static_cast<uint64_t>(tieWord);
  }

  // ChainWeldArgmax. One sweep's argmax, over a frozen snapshot of the weld slots: every eligible edge whose
  // two slots are both still free offers its key to its tail's out-slot and its head's in-slot.
  struct ChainWeldArgmax {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainEdgesConst edges,
                                  ChainNodesConst nodes,
                                  ChainIncidenceConst mdInc,
                                  ChainIncidenceConst lsInc,
                                  int32_t const* outWeld,
                                  int32_t const* inWeld,
                                  uint64_t* bestOut,
                                  uint64_t* bestIn) const {
      uint32_t const nEdges = static_cast<uint32_t>(edges.metadata().size());

      for (uint32_t edgeIdx : cms::alpakatools::uniform_elements(acc, nEdges)) {
        // Type 0 rows are the enumeration holes ChainBuildEdges never filled, and their logOdds column was
        // never given a meaning, so they must not be able to win an argmax.
        uint8_t const edgeType = edges.type()[edgeIdx];
        if (edgeType == 0u)
          continue;
        float const logOdds = edges.logOdds()[edgeIdx];
        // The per-edge eligibility bar, resolved once by ChainEdgeInference and carried on the edge row: the bar
        // is per family and per (pt, |eta|) cell, so it cannot be a kernel-argument scalar. A
        // family whose whole table row holds one value behaves exactly like a single scalar bar.
        if (logOdds < edges.weldBar()[edgeIdx])
          continue;
        uint32_t const tailNode = edges.inner()[edgeIdx];
        uint32_t const headNode = edges.outer()[edgeIdx];
        if (outWeld[tailNode] != -1 || inWeld[headNode] != -1)
          continue;  // tail's out-slot or head's in-slot already taken
        uint64_t const weldKey = chainWeldKeyDense(
            logOdds, edges.tie()[edgeIdx], edgeType, chainWeldDegProd(edges, nodes, mdInc, lsInc, edgeIdx));
        alpaka::atomicMax(acc, &bestOut[tailNode], weldKey, alpaka::hierarchy::Threads{});
        alpaka::atomicMax(acc, &bestIn[headNode], weldKey, alpaka::hierarchy::Threads{});
      }
    }
  };

  // ChainWeldMutual. Apply the mutual-best pairs, edge-parallel: an edge is welded exactly when it is the
  // argmax of its tail's out-slot AND of its head's in-slot. Walking edges rather than nodes is
  // what lets the packed key carry a stable tie word instead of an edge index -- a node-parallel
  // form would have to decode the winner back out of the key.
  //
  // No atomic is needed and the two stores cannot race: the key is unique inside each node's
  // incident-edge list, so at most one edge per node satisfies bestOut[tail] == key and at most
  // one satisfies bestIn[head] == key.
  //
  // The type and weldBar gates mirror ChainWeldArgmax exactly, so an ineligible row can never collide with the
  // 0 sentinel. The weld-slot gates are deliberately NOT repeated: an edge whose tail or head was
  // welded in an earlier sweep was skipped by ChainWeldArgmax, so that node's best key is still 0 and the
  // equality test rejects it anyway. Not reading the weld slots also removes the only
  // read-after-write pair this kernel would otherwise have.
  struct ChainWeldMutual {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainEdgesConst edges,
                                  ChainNodesConst nodes,
                                  ChainIncidenceConst mdInc,
                                  ChainIncidenceConst lsInc,
                                  int32_t* outWeld,
                                  int32_t* inWeld,
                                  uint64_t const* bestOut,
                                  uint64_t const* bestIn) const {
      uint32_t const nEdges = static_cast<uint32_t>(edges.metadata().size());

      for (uint32_t edgeIdx : cms::alpakatools::uniform_elements(acc, nEdges)) {
        uint8_t const edgeType = edges.type()[edgeIdx];
        if (edgeType == 0u)
          continue;  // enumeration hole
        uint32_t const tailNode = edges.inner()[edgeIdx];
        uint64_t const bestKey = bestOut[tailNode];
        // The tail either has no eligible edge this sweep or was welded in an earlier one; ChainWeldArgmax
        // left the 0 sentinel either way and no key can match it. Testing that before the logOdds
        // and tie loads keeps the later sweeps -- where nearly every tail is already welded -- at
        // two loads per edge, which is what makes walking edges instead of nodes free here.
        if (bestKey == 0u)
          continue;
        float const logOdds = edges.logOdds()[edgeIdx];
        if (logOdds < edges.weldBar()[edgeIdx])
          continue;
        if (bestKey !=
            chainWeldKeyDense(
                logOdds, edges.tie()[edgeIdx], edgeType, chainWeldDegProd(edges, nodes, mdInc, lsInc, edgeIdx)))
          continue;
        uint32_t const headNode = edges.outer()[edgeIdx];
        if (bestIn[headNode] != bestKey)
          continue;
        outWeld[tailNode] = static_cast<int32_t>(edgeIdx);
        inWeld[headNode] = static_cast<int32_t>(edgeIdx);
      }
    }
  };

  // ChainCountChains. A chain head is a node with no in-weld and at least one out-weld; walking forward from it
  // gives the chain's node count. In- and out-degree <= 1 make the welded graph a set of disjoint
  // simple paths, so one walk per head visits every welded node exactly once.
  struct ChainCountChains {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainEdgesConst edges,
                                  uint32_t nNodes,
                                  int32_t const* outWeld,
                                  int32_t const* inWeld,
                                  uint32_t* headNodeCount) const {
      for (uint32_t nodeIdx : cms::alpakatools::uniform_elements(acc, nNodes)) {
        uint32_t count = 0u;
        if (inWeld[nodeIdx] == -1 && outWeld[nodeIdx] != -1) {
          uint32_t walkNode = nodeIdx;
          count = 1u;
          while (outWeld[walkNode] != -1 && count < kChainMaxNodes) {
            walkNode = edges.outer()[static_cast<uint32_t>(outWeld[walkNode])];
            ++count;
          }
          ALPAKA_ASSERT_ACC(count < kChainMaxNodes);
        }
        headNodeCount[nodeIdx] = count;
      }
    }
  };

  // ChainPrefixChains. Exclusive prefixes over the node index space: the number of heads strictly before a head
  // is its chain index, and the node-count prefix is its CSR base. Chains therefore come out in
  // ascending head-node order.
  struct ChainPrefixChains {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint32_t nNodes,
                                  uint32_t const* headNodeCount,
                                  uint32_t* chainIndexOf,
                                  uint32_t* nodeOffsetOf,
                                  uint32_t* nChains,
                                  uint32_t* nChainNodesTotal) const {
      // 1-block kernel
      ALPAKA_ASSERT_ACC((alpaka::getWorkDiv<alpaka::Grid, alpaka::Blocks>(acc)[0u] == 1));

      auto& partial = alpaka::declareSharedVar<uint32_t[4 * kChainScanBlockThreads], __COUNTER__>(acc);

      uint32_t const nWorkers = chainScanWorkerCount(acc);
      uint32_t const worker = chainScanWorkerIndex(acc);
      ALPAKA_ASSERT_ACC(nWorkers <= kChainScanBlockThreads);

      uint32_t const chunkSize = (nNodes + nWorkers - 1u) / nWorkers;
      uint32_t const beginNode = (worker * chunkSize < nNodes) ? worker * chunkSize : nNodes;
      uint32_t const endNode = (beginNode + chunkSize < nNodes) ? beginNode + chunkSize : nNodes;

      uint32_t local[2] = {0u, 0u};
      for (uint32_t nodeIdx = beginNode; nodeIdx < endNode; ++nodeIdx) {
        uint32_t const nodeCount = headNodeCount[nodeIdx];
        local[0] += (nodeCount != 0u) ? 1u : 0u;
        local[1] += nodeCount;
      }

      uint32_t base[2], total[2];
      chainScanBlockExclusive<2>(acc, &partial[0], nWorkers, worker, local, base, total);

      uint32_t runHeads = base[0], runNodes = base[1];
      for (uint32_t nodeIdx = beginNode; nodeIdx < endNode; ++nodeIdx) {
        uint32_t const nodeCount = headNodeCount[nodeIdx];
        chainIndexOf[nodeIdx] = runHeads;
        nodeOffsetOf[nodeIdx] = runNodes;
        runHeads += (nodeCount != 0u) ? 1u : 0u;
        runNodes += nodeCount;
      }

      alpaka::syncBlockThreads(acc);
      if (cms::alpakatools::once_per_block(acc)) {
        *nChains = total[0];
        *nChainNodesTotal = total[1];
      }
    }
  };

  // ChainEmitChains. The second walk, which materialises the chain: node list, weld-edge list and edge-logit
  // sum, then the deduped MD union in first-appearance order with its layer bitmask.
  struct ChainEmitChains {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ModulesConst modules,
                                  MiniDoubletsConst miniDoublets,
                                  ChainNodesConst nodes,
                                  ChainEdgesConst edges,
                                  uint32_t nNodes,
                                  int32_t const* outWeld,
                                  uint32_t const* headNodeCount,
                                  uint32_t const* chainIndexOf,
                                  uint32_t const* nodeOffsetOf,
                                  Chains chains,
                                  ChainItems items,
                                  float lambdaLen) const {
      for (uint32_t headNode : cms::alpakatools::uniform_elements(acc, nNodes)) {
        uint32_t const nChainNodes = headNodeCount[headNode];
        if (nChainNodes == 0u)
          continue;

        uint32_t const chainIdx = chainIndexOf[headNode];
        uint32_t const nodeOffset = nodeOffsetOf[headNode];

        float edgeSum = 0.f;
        uint32_t walkNode = headNode;
        for (uint32_t k = 0; k < nChainNodes; ++k) {
          items.nodeItems()[nodeOffset + k] = walkNode;
          int32_t const edgeIdx = outWeld[walkNode];
          if (edgeIdx == -1)
            break;
          items.edgeItems()[nodeOffset + k] = static_cast<uint32_t>(edgeIdx);
          edgeSum += edges.logOdds()[static_cast<uint32_t>(edgeIdx)];
          walkNode = edges.outer()[static_cast<uint32_t>(edgeIdx)];
        }

        // Deduped MD union of the members' {md0, md1, md2}, in first-appearance order walking
        // innermost-first. The dedup is a linear scan because a chain holds at most a few dozen
        // MDs and the list must stay in that order.
        uint32_t const mdBase = 3u * nodeOffset;
        uint32_t nMDs = 0u;
        uint32_t layerMask = 0u;
        for (uint32_t k = 0; k < nChainNodes; ++k) {
          uint32_t const tripletIdx = nodes.tripletIndex()[items.nodeItems()[nodeOffset + k]];
          unsigned int firstMD, midMD, lastMD;
          chainNodeMDs(triplets, segments, tripletIdx, firstMD, midMD, lastMD);
          unsigned int const mdTriple[3] = {firstMD, midMD, lastMD};
          for (int i = 0; i < 3; ++i) {
            uint32_t const mdIndex = static_cast<uint32_t>(mdTriple[i]);
            bool seen = false;
            for (uint32_t j = 0; j < nMDs && !seen; ++j)
              seen = (items.mdItems()[mdBase + j] == mdIndex);
            if (!seen) {
              items.mdItems()[mdBase + nMDs] = mdIndex;
              ++nMDs;
              layerMask |= (1u << chainMdLayer(modules, miniDoublets, mdIndex));
            }
          }
        }
        int nLayers = 0;
        for (uint32_t bits = layerMask; bits != 0u; bits &= bits - 1u)
          ++nLayers;

        chains.nodeOffset()[chainIdx] = nodeOffset;
        chains.stableKey()[chainIdx] = nodes.stableId()[headNode];  // the head node names the chain
        chains.nNodes()[chainIdx] = static_cast<uint16_t>(nChainNodes);
        chains.nMDs()[chainIdx] = static_cast<uint16_t>(nMDs);
        chains.nLayers()[chainIdx] = static_cast<uint8_t>(nLayers);
        chains.score()[chainIdx] = edgeSum + lambdaLen * static_cast<float>(nLayers);
        chains.trimAction()[chainIdx] = 0;
        chains.branch()[chainIdx] = -1;
        chains.flags()[chainIdx] = 0u;
      }
    }
  };

  // Combined chain fit chi2/hit over an MD list: xy Kasa circle chi2/hit plus rz line chi2/hit,
  // both in cm^2. The MD list is read straight from global memory rather than staged, and the rz
  // arc length is recomputed on each of the three passes in the same order it is accumulated, so
  // that every partial sum is reproducible.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE double chainFitChi2Combined(TAcc const& acc,
                                                             MiniDoubletsConst miniDoublets,
                                                             uint32_t const* mdList,
                                                             int nMDs) {
    if (nMDs < 1)
      return 0.0;

    double xyChi2 = 0.0;
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
      // A near-singular normal matrix means the hits are collinear in xy: leave the circle term at
      // 0 rather than inverting it.
      if (determinant > 1e-12 * scale * scale) {
        double const centreU = (sumVV * (0.5 * sumUW) - sumUV * (0.5 * sumVW)) / determinant;
        double const centreV = (sumUU * (0.5 * sumVW) - sumUV * (0.5 * sumUW)) / determinant;
        double const radius =
            alpaka::math::sqrt(acc, alpaka::math::max(acc, centreU * centreU + centreV * centreV + sumW / nMDs, 0.0));
        double chi2 = 0.0;
        for (int k = 0; k < nMDs; ++k) {
          double const du = (miniDoublets.anchorX()[mdList[k]] - xMean) - centreU,
                       dv = (miniDoublets.anchorY()[mdList[k]] - yMean) - centreV;
          double const resid = alpaka::math::sqrt(acc, du * du + dv * dv) - radius;
          chi2 += resid * resid;
        }
        xyChi2 = chi2 / nMDs;
      }
    }

    double rzChi2 = 0.0;
    {
      double arcLength = 0.0, arcMean = 0.0, zMean = 0.0;
      for (int k = 0; k < nMDs; ++k) {
        if (k > 0) {
          // The anchor hits are widened to double BEFORE differencing; differencing in float first
          // would round the step.
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
      if (sumArcSq > 1e-12) {
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
        }
        rzChi2 = chi2 / nMDs;
      }
    }

    return xyChi2 + rzChi2;
  }

  // ChainTrimTerminals. Terminal trim, after the weld and before the gate and the claim: drop the inner or the
  // outer end node of a chain when the shortened chain fits enough better to be worth the layer.
  // It is expressed as an endpoint move rather than a rebuild:
  //   - the MD union of the OUTER-dropped variant is by construction the PREFIX of the full
  //     first-appearance union that the first nNodes - 1 members produce, so it needs no rebuild;
  //   - the INNER-dropped variant is built into the per-chain mdScratch region and copied into
  //     place only if it wins, which keeps the read of the full union hazard-free.
  // Both moves leave the chain inside its original 3 * nNodes MD allocation (ChainsSoA.h).
  struct ChainTrimTerminals {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ModulesConst modules,
                                  MiniDoubletsConst miniDoublets,
                                  ChainNodesConst nodes,
                                  ChainEdgesConst edges,
                                  Chains chains,
                                  ChainItems items,
                                  ChainConfig config) const {
      // Denominator floor for the improvement ratio: a numerically perfect or degenerate remainder
      // must not produce a NaN.
      constexpr double kChi2Floor = 1e-9;

      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());

      for (uint32_t chainIdx : cms::alpakatools::uniform_elements(acc, nChains)) {
        uint32_t const nodeOffset = chains.nodeOffset()[chainIdx];
        int const nNodes = chains.nNodes()[chainIdx];
        int const nMDs = chains.nMDs()[chainIdx];
        uint32_t const mdBase = 3u * nodeOffset;
        if (nNodes < 3)
          continue;

        double const chi2Full = chainFitChi2Combined(acc, miniDoublets, &items.mdItems()[mdBase], nMDs);
        if (chi2Full <= static_cast<double>(config.trimAbsChi2))
          continue;  // concentrating guard: a chain that already fits well has no parasitic arm

        // Outer-dropped union == prefix of the full union over the first nNodes - 1 members, which
        // the assert below states as an invariant rather than relying on it silently.
        int nMDOut = 0;
        uint32_t maskOut = 0u;
        for (int k = 0; k + 1 < nNodes; ++k) {
          uint32_t const tripletIdx = nodes.tripletIndex()[items.nodeItems()[nodeOffset + k]];
          unsigned int firstMD, midMD, lastMD;
          chainNodeMDs(triplets, segments, tripletIdx, firstMD, midMD, lastMD);
          unsigned int const mdTriple[3] = {firstMD, midMD, lastMD};
          for (int i = 0; i < 3; ++i) {
            uint32_t const mdIndex = static_cast<uint32_t>(mdTriple[i]);
            bool seen = false;
            for (int j = 0; j < nMDOut && !seen; ++j)
              seen = (items.mdItems()[mdBase + j] == mdIndex);
            if (!seen) {
              ALPAKA_ASSERT_ACC(items.mdItems()[mdBase + nMDOut] == mdIndex);
              ++nMDOut;
              maskOut |= (1u << chainMdLayer(modules, miniDoublets, mdIndex));
            }
          }
        }
        int nLayersOut = 0;
        for (uint32_t bits = maskOut; bits != 0u; bits &= bits - 1u)
          ++nLayersOut;

        // Inner-dropped union, built into the scratch region.
        int nMDIn = 0;
        uint32_t maskIn = 0u;
        for (int k = 1; k < nNodes; ++k) {
          uint32_t const tripletIdx = nodes.tripletIndex()[items.nodeItems()[nodeOffset + k]];
          unsigned int firstMD, midMD, lastMD;
          chainNodeMDs(triplets, segments, tripletIdx, firstMD, midMD, lastMD);
          unsigned int const mdTriple[3] = {firstMD, midMD, lastMD};
          for (int i = 0; i < 3; ++i) {
            uint32_t const mdIndex = static_cast<uint32_t>(mdTriple[i]);
            bool seen = false;
            for (int j = 0; j < nMDIn && !seen; ++j)
              seen = (items.mdScratch()[mdBase + j] == mdIndex);
            if (!seen) {
              items.mdScratch()[mdBase + nMDIn] = mdIndex;
              ++nMDIn;
              maskIn |= (1u << chainMdLayer(modules, miniDoublets, mdIndex));
            }
          }
        }
        int nLayersIn = 0;
        for (uint32_t bits = maskIn; bits != 0u; bits &= bits - 1u)
          ++nLayersIn;

        // A variant that would leave too few layers keeps its ratio at -1 and can never win.
        double ratioIn = -1.0, ratioOut = -1.0;
        if (nLayersIn >= config.trimMinLayersAfter)
          ratioIn = chi2Full /
                    alpaka::math::max(
                        acc, chainFitChi2Combined(acc, miniDoublets, &items.mdScratch()[mdBase], nMDIn), kChi2Floor);
        if (nLayersOut >= config.trimMinLayersAfter)
          ratioOut = chi2Full /
                     alpaka::math::max(
                         acc, chainFitChi2Combined(acc, miniDoublets, &items.mdItems()[mdBase], nMDOut), kChi2Floor);

        // Larger improvement wins; inner wins an exact tie, which keeps the choice deterministic.
        int drop = 0;
        int nLayersAfter = 0;
        if (ratioIn >= ratioOut && ratioIn > static_cast<double>(config.trimFactor)) {
          drop = 1;
          nLayersAfter = nLayersIn;
        } else if (ratioOut > ratioIn && ratioOut > static_cast<double>(config.trimFactor)) {
          drop = 2;
          nLayersAfter = nLayersOut;
        }
        if (drop == 0)
          continue;

        uint32_t const newNodeOffset = (drop == 1) ? nodeOffset + 1u : nodeOffset;
        int const newNodes = nNodes - 1;
        float edgeSum = 0.f;
        for (int k = 0; k < newNodes - 1; ++k)
          edgeSum += edges.logOdds()[items.edgeItems()[newNodeOffset + k]];

        if (drop == 1) {
          uint32_t const newMdBase = 3u * newNodeOffset;
          for (int j = 0; j < nMDIn; ++j)
            items.mdItems()[newMdBase + j] = items.mdScratch()[mdBase + j];
          chains.nMDs()[chainIdx] = static_cast<uint16_t>(nMDIn);
        } else {
          chains.nMDs()[chainIdx] = static_cast<uint16_t>(nMDOut);
        }
        chains.nodeOffset()[chainIdx] = newNodeOffset;
        chains.nNodes()[chainIdx] = static_cast<uint16_t>(newNodes);
        chains.nLayers()[chainIdx] = static_cast<uint8_t>(nLayersAfter);
        chains.score()[chainIdx] = edgeSum + config.lambdaLen * static_cast<float>(nLayersAfter);
        chains.trimAction()[chainIdx] = static_cast<int8_t>(drop);
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
