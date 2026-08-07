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

// Chain-tracking weld and terminal trim, phase P2.2 of standalone/prototype/P2_PORT_MAP.md.
//
// Stages implemented here:
//   K6a WeldArgmax     - per-node best eligible edge, packed-key atomicMax (x kChainWeldSweeps)
//   K6b WeldMutual     - apply the mutual-best pairs                       (x kChainWeldSweeps)
//   K6c CountChains    - head detection plus the path length walk
//   K6d PrefixChains   - exclusive prefixes -> chain index and node offset
//   K6e EmitChains     - node / edge / deduped-MD CSR fill, nLayers, K6 score
//   K6f TrimTerminals  - the -TR terminal trim
//
// Reference implementation: prototype/K6Weld.cc and prototype/Trim.cc. Every arithmetic
// expression is a transcription in the same operation order, because the phase gate is per-chain
// float parity against that implementation.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  // std::max / std::min semantics for doubles (plain comparison, NOT fmax / fmin): the reference
  // uses std::max everywhere in the fit code and the two differ on NaN.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE double chainMaxd(double a, double b) { return (a < b) ? b : a; }
  ALPAKA_FN_ACC ALPAKA_FN_INLINE double chainMind(double a, double b) { return (b < a) ? b : a; }

  // ------------------------------------------------------------------------------------------
  // The packed weld key.
  //
  // prototype/K6Weld.cc beats(a, b) is "higher logOdds first, lower EDGE INDEX on ties". Phase P2.5
  // replaces the second half of that rule: the edge index is a position in the K2 enumeration,
  // whose CSR slices are filled by an atomicAdd cursor over a node numbering that itself descends
  // from LST's atomicAdd triplet slots, so it permutes between two identical runs and between the
  // CPU and CUDA backends. Exact-logit ties are common (~1e4 same-key adjacent pairs per event,
  // duplicate feature rows give bit-identical MLP outputs), so that made the welded chain set
  // run-dependent. The tie operand is now ChainEdges::tie, the XOR of the two endpoints' stableId
  // (ChainNodesSoA.h), which is a function of hit rows alone and therefore fixed by the event data.
  //
  // The packed 64-bit key is a single unsigned total order:
  //   high word = the standard monotone float -> uint32 map, so a larger float is a larger word;
  //   low  word = the stable tie word, larger wins.
  // Key 0 is a safe "no edge" sentinel: only edges with logOdds >= thetaEdge (0 in the frozen
  // configuration) take part, and every non-negative float maps to a high word >= 0x80000000.
  //
  // The key is no longer decodable back to an edge index. It does not need to be: K6b is now
  // edge-parallel and each edge tests whether it is itself the argmax at both of its endpoints.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainOrderFloat(float x) {
    uint32_t const b = std::bit_cast<uint32_t>(x);
    return (b >> 31) ? (b ^ 0xffffffffu) : (b ^ 0x80000000u);
  }

  // Its exact inverse: a value that went through an atomicMax on the order key comes back unchanged.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainUnorderFloat(uint32_t k) {
    uint32_t const b = (k & 0x80000000u) ? (k & 0x7fffffffu) : ~k;
    return std::bit_cast<float>(b);
  }

  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint64_t chainWeldKey(float logOdds, uint32_t tie) {
    return (static_cast<uint64_t>(chainOrderFloat(logOdds)) << 32) | static_cast<uint64_t>(tie);
  }

  // ------------------------------------------------------------------------------------------
  // K6a. Per-sweep argmax over a frozen snapshot of the weld slots.
  struct ChainWeldArgmax {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainEdgesConst edges,
                                  int32_t const* outWeld,
                                  int32_t const* inWeld,
                                  uint64_t* bestOut,
                                  uint64_t* bestIn,
                                  float thetaEdge) const {
      uint32_t const nEdges = static_cast<uint32_t>(edges.metadata().size());

      for (uint32_t e : cms::alpakatools::uniform_elements(acc, nEdges)) {
        // type 0 rows are the enumeration holes K2 never filled; the reference edge list has no
        // such entries at all, so they must not be able to win an argmax.
        if (edges.type()[e] == 0u)
          continue;
        float const lo = edges.logOdds()[e];
        if (lo < thetaEdge)
          continue;  // eligibility gate
        uint32_t const n = edges.inner()[e];
        uint32_t const m = edges.outer()[e];
        if (outWeld[n] != -1 || inWeld[m] != -1)
          continue;  // tail's out-slot or head's in-slot already taken
        uint64_t const key = chainWeldKey(lo, edges.tie()[e]);
        alpaka::atomicMax(acc, &bestOut[n], key, alpaka::hierarchy::Threads{});
        alpaka::atomicMax(acc, &bestIn[m], key, alpaka::hierarchy::Threads{});
      }
    }
  };

  // K6b. Apply the mutual-best pairs, edge-parallel: an edge is welded exactly when it is the
  // argmax of its tail's out-slot AND of its head's in-slot, which is the definition of a mutual
  // best pair. Walking edges instead of nodes is what lets the packed key carry a stable tie word
  // rather than the edge index -- the node-parallel form had to decode the winner out of the key.
  //
  // No atomic is needed and the two stores cannot race: the key is unique inside each node's
  // incident-edge list (chainWeldKey, see the header note), so at most one edge per node satisfies
  // bestOut[n] == key and at most one satisfies bestIn[m] == key.
  //
  // The type and thetaEdge gates mirror K6a exactly, so an ineligible row can never collide with
  // the 0 sentinel. The weld-slot gates are deliberately NOT repeated: an edge whose tail or head
  // was welded in an earlier sweep was skipped by K6a, so that node's best-key is still 0 and the
  // equality test rejects it anyway. Not reading the weld slots also removes the only
  // read-after-write pair this kernel would otherwise have had.
  struct ChainWeldMutual {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainEdgesConst edges,
                                  int32_t* outWeld,
                                  int32_t* inWeld,
                                  uint64_t const* bestOut,
                                  uint64_t const* bestIn,
                                  float thetaEdge) const {
      uint32_t const nEdges = static_cast<uint32_t>(edges.metadata().size());

      for (uint32_t e : cms::alpakatools::uniform_elements(acc, nEdges)) {
        if (edges.type()[e] == 0u)
          continue;  // enumeration hole: its logOdds column was never given a meaning
        uint32_t const n = edges.inner()[e];
        uint64_t const bestKey = bestOut[n];
        // The tail either has no eligible edge in this sweep or was welded in an earlier one; K6a
        // left the 0 sentinel either way and no key can match it. Testing that before the logOdds
        // and tie loads keeps the later sweeps -- where nearly every tail is already welded -- at
        // two loads per edge, which is what makes walking edges instead of nodes here free.
        if (bestKey == 0u)
          continue;
        float const lo = edges.logOdds()[e];
        if (lo < thetaEdge)
          continue;
        if (bestKey != chainWeldKey(lo, edges.tie()[e]))
          continue;
        uint32_t const m = edges.outer()[e];
        if (bestIn[m] != bestKey)
          continue;
        outWeld[n] = static_cast<int32_t>(e);
        inWeld[m] = static_cast<int32_t>(e);
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // K6c. Heads are nodes with no in-weld and at least one out-weld; the path walk gives the node
  // count. In / out degree <= 1 makes the welded graph a set of disjoint simple paths, so one walk
  // per head visits every welded node exactly once.
  struct ChainCountChains {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainEdgesConst edges,
                                  uint32_t nNodes,
                                  int32_t const* outWeld,
                                  int32_t const* inWeld,
                                  uint32_t* headNodeCount) const {
      for (uint32_t n : cms::alpakatools::uniform_elements(acc, nNodes)) {
        uint32_t count = 0u;
        if (inWeld[n] == -1 && outWeld[n] != -1) {
          uint32_t cur = n;
          count = 1u;
          while (outWeld[cur] != -1 && count < kChainMaxNodes) {
            cur = edges.outer()[static_cast<uint32_t>(outWeld[cur])];
            ++count;
          }
          ALPAKA_ASSERT_ACC(count < kChainMaxNodes);
        }
        headNodeCount[n] = count;
      }
    }
  };

  // K6d. Exclusive prefixes over the node index space: the number of heads strictly before n gives
  // the chain index of a head n, and the node-count prefix gives its CSR base. Chains therefore
  // come out in ascending head-node order, which is exactly the reference's emission order.
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

      uint32_t const chunk = (nNodes + nWorkers - 1u) / nWorkers;
      uint32_t const begin = (worker * chunk < nNodes) ? worker * chunk : nNodes;
      uint32_t const end = (begin + chunk < nNodes) ? begin + chunk : nNodes;

      uint32_t local[2] = {0u, 0u};
      for (uint32_t n = begin; n < end; ++n) {
        uint32_t const c = headNodeCount[n];
        local[0] += (c != 0u) ? 1u : 0u;
        local[1] += c;
      }

      uint32_t base[2], total[2];
      chainScanBlockExclusive<2>(acc, &partial[0], nWorkers, worker, local, base, total);

      uint32_t runHeads = base[0], runNodes = base[1];
      for (uint32_t n = begin; n < end; ++n) {
        uint32_t const c = headNodeCount[n];
        chainIndexOf[n] = runHeads;
        nodeOffsetOf[n] = runNodes;
        runHeads += (c != 0u) ? 1u : 0u;
        runNodes += c;
      }

      alpaka::syncBlockThreads(acc);
      if (cms::alpakatools::once_per_block(acc)) {
        *nChains = total[0];
        *nChainNodesTotal = total[1];
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // K6e. The second walk: node list, weld-edge list, edge-logit sum, then the deduped MD union in
  // first-appearance order with its layer bitmask.
  struct ChainEmitChains {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ModulesConst modules,
                                  MiniDoubletsConst mds,
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
      for (uint32_t n : cms::alpakatools::uniform_elements(acc, nNodes)) {
        uint32_t const nChainNodes = headNodeCount[n];
        if (nChainNodes == 0u)
          continue;

        uint32_t const c = chainIndexOf[n];
        uint32_t const off = nodeOffsetOf[n];

        float edgeSum = 0.f;
        uint32_t cur = n;
        for (uint32_t k = 0; k < nChainNodes; ++k) {
          items.nodeItems()[off + k] = cur;
          int32_t const e = outWeld[cur];
          if (e == -1)
            break;
          items.edgeItems()[off + k] = static_cast<uint32_t>(e);
          edgeSum += edges.logOdds()[static_cast<uint32_t>(e)];
          cur = edges.outer()[static_cast<uint32_t>(e)];
        }

        // Deduped MD union of the members' {md0, md1, md2}, first-appearance order walking
        // innermost-first, with the linear dedup scan the reference uses.
        uint32_t const mdBase = 3u * off;
        uint32_t nMD = 0u;
        uint32_t layerMask = 0u;
        for (uint32_t k = 0; k < nChainNodes; ++k) {
          uint32_t const t3 = nodes.tripletIndex()[items.nodeItems()[off + k]];
          unsigned int m0, m1, m2;
          chainNodeMDs(triplets, segments, t3, m0, m1, m2);
          unsigned int const mdTriple[3] = {m0, m1, m2};
          for (int t = 0; t < 3; ++t) {
            uint32_t const md = static_cast<uint32_t>(mdTriple[t]);
            bool seen = false;
            for (uint32_t q = 0; q < nMD && !seen; ++q)
              seen = (items.mdItems()[mdBase + q] == md);
            if (!seen) {
              items.mdItems()[mdBase + nMD] = md;
              ++nMD;
              layerMask |= (1u << chainMdLayer(modules, mds, md));
            }
          }
        }
        int nLayers = 0;
        for (uint32_t b = layerMask; b != 0u; b &= b - 1u)
          ++nLayers;

        chains.nodeOffset()[c] = off;
        chains.stableKey()[c] = nodes.stableId()[n];  // the head node names the chain, see ChainsSoA.h
        chains.nNodes()[c] = static_cast<uint16_t>(nChainNodes);
        chains.nMDs()[c] = static_cast<uint16_t>(nMD);
        chains.nLayers()[c] = static_cast<uint8_t>(nLayers);
        chains.score()[c] = edgeSum + lambdaLen * static_cast<float>(nLayers);
        chains.trimAction()[c] = 0;
        chains.branch()[c] = -1;
        chains.flags()[c] = 0u;
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // Combined chain fit chi2/hit over an MD list: xy Kasa circle chi2/hit + rz line chi2/hit, both
  // in cm^2, both with the exact prototype/ChainFeatures.cc guards. Transcription of
  // prototype/Trim.cc chainFitChi2Combined; the MD list is read from global memory instead of
  // being staged in a vector, and the rz arc length is recomputed per pass in the same order the
  // reference accumulates it, so every partial sum is bit-identical.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE double chainFitChi2Combined(TAcc const& acc,
                                                             MiniDoubletsConst mds,
                                                             uint32_t const* mdList,
                                                             int nMD) {
    if (nMD < 1)
      return 0.0;

    double xyChi2 = 0.0;
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
      if (det > 1e-12 * scale * scale) {
        double const uc = (Svv * (0.5 * Suw) - Suv * (0.5 * Svw)) / det;
        double const vc = (Suu * (0.5 * Svw) - Suv * (0.5 * Suw)) / det;
        double const R = alpaka::math::sqrt(acc, chainMaxd(uc * uc + vc * vc + Sw / nMD, 0.0));
        double chi2 = 0.0;
        for (int k = 0; k < nMD; ++k) {
          double const du = (mds.anchorX()[mdList[k]] - xbar) - uc, dv = (mds.anchorY()[mdList[k]] - ybar) - vc;
          double const resid = alpaka::math::sqrt(acc, du * du + dv * dv) - R;
          chi2 += resid * resid;
        }
        xyChi2 = chi2 / nMD;
      }
    }

    double rzChi2 = 0.0;
    {
      double s = 0.0, sbar = 0.0, zbar = 0.0;
      for (int k = 0; k < nMD; ++k) {
        if (k > 0) {
          // The reference stages the anchor hits into a double array BEFORE differencing, so the
          // subtraction is a double subtraction; doing it in float first would round.
          double const dx =
              static_cast<double>(mds.anchorX()[mdList[k]]) - static_cast<double>(mds.anchorX()[mdList[k - 1]]);
          double const dy =
              static_cast<double>(mds.anchorY()[mdList[k]]) - static_cast<double>(mds.anchorY()[mdList[k - 1]]);
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
          // The reference stages the anchor hits into a double array BEFORE differencing, so the
          // subtraction is a double subtraction; doing it in float first would round.
          double const dx =
              static_cast<double>(mds.anchorX()[mdList[k]]) - static_cast<double>(mds.anchorX()[mdList[k - 1]]);
          double const dy =
              static_cast<double>(mds.anchorY()[mdList[k]]) - static_cast<double>(mds.anchorY()[mdList[k - 1]]);
          s += alpaka::math::sqrt(acc, dx * dx + dy * dy);
        }
        double const ds = s - sbar;
        Sss += ds * ds;
        Ssz += ds * (mds.anchorZ()[mdList[k]] - zbar);
      }
      if (Sss > 1e-12) {
        double const b = Ssz / Sss;
        double const a = zbar - b * sbar;
        double chi2 = 0.0;
        s = 0.0;
        for (int k = 0; k < nMD; ++k) {
          if (k > 0) {
            double const dx =
                static_cast<double>(mds.anchorX()[mdList[k]]) - static_cast<double>(mds.anchorX()[mdList[k - 1]]);
            double const dy =
                static_cast<double>(mds.anchorY()[mdList[k]]) - static_cast<double>(mds.anchorY()[mdList[k - 1]]);
            s += alpaka::math::sqrt(acc, dx * dx + dy * dy);
          }
          double const r = mds.anchorZ()[mdList[k]] - a - b * s;
          chi2 += r * r;
        }
        rzChi2 = chi2 / nMD;
      }
    }

    return xyChi2 + rzChi2;
  }

  // K6f. Terminal trim (-TR), post-weld / pre-gate / pre-claim. Transcription of
  // prototype/Trim.cc k6TrimTerminals, expressed as an endpoint move instead of a rebuild:
  //   - the MD union of the OUTER-dropped variant is, by construction, the PREFIX of the full
  //     first-appearance union that the first nNodes - 1 members produce, so it needs no rebuild;
  //   - the INNER-dropped variant is built into the per-chain mdScratch region and copied into
  //     place only if it wins, which keeps the read of the full union hazard-free.
  // Both moves leave the chain inside its original 3 * nNodes MD allocation (see ChainsSoA.h).
  struct ChainTrimTerminals {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ModulesConst modules,
                                  MiniDoubletsConst mds,
                                  ChainNodesConst nodes,
                                  ChainEdgesConst edges,
                                  Chains chains,
                                  ChainItems items,
                                  ChainConfig cfg) const {
      // chi2 denominator floor (prototype/Trim.cc kChi2Floor): a numerically perfect or degenerate
      // remainder must not produce a NaN ratio.
      constexpr double kChi2Floor = 1e-9;

      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());

      for (uint32_t c : cms::alpakatools::uniform_elements(acc, nChains)) {
        uint32_t const off = chains.nodeOffset()[c];
        int const nN = chains.nNodes()[c];
        int const nMD = chains.nMDs()[c];
        uint32_t const mdBase = 3u * off;
        if (nN < 3)
          continue;

        double const chi2Full = chainFitChi2Combined(acc, mds, &items.mdItems()[mdBase], nMD);
        if (chi2Full <= static_cast<double>(cfg.trimAbsChi2))
          continue;  // concentrating guard: a chain that already fits well has no parasitic arm

        // Outer-dropped union == prefix of the full union over the first nN - 1 members.
        int nMDOut = 0;
        uint32_t maskOut = 0u;
        for (int k = 0; k + 1 < nN; ++k) {
          uint32_t const t3 = nodes.tripletIndex()[items.nodeItems()[off + k]];
          unsigned int m0, m1, m2;
          chainNodeMDs(triplets, segments, t3, m0, m1, m2);
          unsigned int const mdTriple[3] = {m0, m1, m2};
          for (int t = 0; t < 3; ++t) {
            uint32_t const md = static_cast<uint32_t>(mdTriple[t]);
            bool seen = false;
            for (int q = 0; q < nMDOut && !seen; ++q)
              seen = (items.mdItems()[mdBase + q] == md);
            if (!seen) {
              ALPAKA_ASSERT_ACC(items.mdItems()[mdBase + nMDOut] == md);
              ++nMDOut;
              maskOut |= (1u << chainMdLayer(modules, mds, md));
            }
          }
        }
        int nLayO = 0;
        for (uint32_t b = maskOut; b != 0u; b &= b - 1u)
          ++nLayO;

        // Inner-dropped union, built into the scratch region.
        int nMDIn = 0;
        uint32_t maskIn = 0u;
        for (int k = 1; k < nN; ++k) {
          uint32_t const t3 = nodes.tripletIndex()[items.nodeItems()[off + k]];
          unsigned int m0, m1, m2;
          chainNodeMDs(triplets, segments, t3, m0, m1, m2);
          unsigned int const mdTriple[3] = {m0, m1, m2};
          for (int t = 0; t < 3; ++t) {
            uint32_t const md = static_cast<uint32_t>(mdTriple[t]);
            bool seen = false;
            for (int q = 0; q < nMDIn && !seen; ++q)
              seen = (items.mdScratch()[mdBase + q] == md);
            if (!seen) {
              items.mdScratch()[mdBase + nMDIn] = md;
              ++nMDIn;
              maskIn |= (1u << chainMdLayer(modules, mds, md));
            }
          }
        }
        int nLayI = 0;
        for (uint32_t b = maskIn; b != 0u; b &= b - 1u)
          ++nLayI;

        double rI = -1.0, rO = -1.0;
        if (nLayI >= cfg.trimMinLayersAfter)
          rI = chi2Full / chainMaxd(chainFitChi2Combined(acc, mds, &items.mdScratch()[mdBase], nMDIn), kChi2Floor);
        if (nLayO >= cfg.trimMinLayersAfter)
          rO = chi2Full / chainMaxd(chainFitChi2Combined(acc, mds, &items.mdItems()[mdBase], nMDOut), kChi2Floor);

        // Larger improvement wins; inner wins exact ties (deterministic, matches the reference).
        int drop = 0;
        int nLayAfter = 0;
        if (rI >= rO && rI > static_cast<double>(cfg.trimFactor)) {
          drop = 1;
          nLayAfter = nLayI;
        } else if (rO > rI && rO > static_cast<double>(cfg.trimFactor)) {
          drop = 2;
          nLayAfter = nLayO;
        }
        if (drop == 0)
          continue;

        uint32_t const newOff = (drop == 1) ? off + 1u : off;
        int const newNodes = nN - 1;
        float edgeSum = 0.f;
        for (int k = 0; k < newNodes - 1; ++k)
          edgeSum += edges.logOdds()[items.edgeItems()[newOff + k]];

        if (drop == 1) {
          uint32_t const newMdBase = 3u * newOff;
          for (int q = 0; q < nMDIn; ++q)
            items.mdItems()[newMdBase + q] = items.mdScratch()[mdBase + q];
          chains.nMDs()[c] = static_cast<uint16_t>(nMDIn);
        } else {
          chains.nMDs()[c] = static_cast<uint16_t>(nMDOut);
        }
        chains.nodeOffset()[c] = newOff;
        chains.nNodes()[c] = static_cast<uint16_t>(newNodes);
        chains.nLayers()[c] = static_cast<uint8_t>(nLayAfter);
        chains.score()[c] = edgeSum + cfg.lambdaLen * static_cast<float>(nLayAfter);
        chains.trimAction()[c] = static_cast<int8_t>(drop);
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
