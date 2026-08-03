#ifndef RecoTracker_LSTCore_src_alpaka_ChainGraph_h
#define RecoTracker_LSTCore_src_alpaka_ChainGraph_h

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainIncidenceSoA.h"
#include "RecoTracker/LSTCore/interface/ChainNodesSoA.h"
#include "RecoTracker/LSTCore/interface/MiniDoubletsSoA.h"
#include "RecoTracker/LSTCore/interface/ModulesSoA.h"
#include "RecoTracker/LSTCore/interface/ObjectRangesSoA.h"
#include "RecoTracker/LSTCore/interface/SegmentsSoA.h"
#include "RecoTracker/LSTCore/interface/TripletsSoA.h"

// Chain-tracking graph construction, phase P2.0 of standalone/prototype/P2_PORT_MAP.md.
//
// Stages implemented here:
//   K0  CompactTriplets   - dense global node index over the module-segmented triplet store
//   K1b PrefixIncidence   - exclusive prefixes of the K1a tallies plus the degIn*degOut prefix
//   K1c ScatterIncidence  - CSR payload fill
// K1a (the four incidence tallies) is fused into the triplet builder, see Triplet.h.
//
// Nothing here is read by any existing LST stage: the whole file only ever runs when
// useChainTracking is true, and it only writes into the new ChainIncidence / ChainNodes
// collections. P2.1 is the first phase that consumes any of it.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  // Thread count of the single-block scan kernels. It also bounds the shared scratch arrays,
  // so it must stay a compile-time constant and must not be exceeded by the launch work division.
  static constexpr uint32_t kChainScanBlockThreads = 1024;

  // Number of independent workers cooperating inside a single-block scan. On a GPU backend this is
  // the block thread count; on a CPU backend alpaka gives one thread with many elements, so it is
  // 1 and the scan degenerates to a plain sequential pass. Either way the partition of the key
  // range into per-worker chunks is fixed, so the resulting offsets are key-ordered and identical
  // run to run. No warp-level primitive and no warp-width assumption is used anywhere.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainScanWorkerCount(TAcc const& acc) {
    return static_cast<uint32_t>(alpaka::getWorkDiv<alpaka::Block, alpaka::Threads>(acc)[0u]);
  }

  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainScanWorkerIndex(TAcc const& acc) {
    return static_cast<uint32_t>(alpaka::getIdx<alpaka::Block, alpaka::Threads>(acc)[0u]);
  }

  // K0, first half. Exclusive prefix over the per-lower-module triplet counts, giving each module
  // the base of its slice in the dense node numbering, and the total node count.
  struct ChainPrefixTripletModules {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  TripletsOccupancyConst tripletsOccupancy,
                                  uint32_t* moduleNodeOffsets,
                                  uint32_t* nNodes) const {
      // 1-block kernel
      ALPAKA_ASSERT_ACC((alpaka::getWorkDiv<alpaka::Grid, alpaka::Blocks>(acc)[0u] == 1));

      auto& partial = alpaka::declareSharedVar<uint32_t[kChainScanBlockThreads], __COUNTER__>(acc);

      uint32_t const nWorkers = chainScanWorkerCount(acc);
      uint32_t const worker = chainScanWorkerIndex(acc);
      ALPAKA_ASSERT_ACC(nWorkers <= kChainScanBlockThreads);

      uint32_t const nKeys = modules.nLowerModules();
      uint32_t const chunk = (nKeys + nWorkers - 1u) / nWorkers;
      uint32_t const begin = (worker * chunk < nKeys) ? worker * chunk : nKeys;
      uint32_t const end = (begin + chunk < nKeys) ? begin + chunk : nKeys;

      uint32_t local = 0u;
      for (uint32_t k = begin; k < end; ++k)
        local += tripletsOccupancy.nTriplets()[k];
      partial[worker] = local;

      alpaka::syncBlockThreads(acc);

      uint32_t base = 0u;
      uint32_t total = 0u;
      for (uint32_t w = 0; w < nWorkers; ++w) {
        if (w == worker)
          base = total;
        total += partial[w];
      }

      uint32_t running = base;
      for (uint32_t k = begin; k < end; ++k) {
        moduleNodeOffsets[k] = running;
        running += tripletsOccupancy.nTriplets()[k];
      }

      alpaka::syncBlockThreads(acc);
      if (cms::alpakatools::once_per_block(acc)) {
        moduleNodeOffsets[nKeys] = total;
        *nNodes = total;
      }
    }
  };

  // K0, second half. Scatter the module-segmented triplet indices into the dense node numbering.
  struct ChainScatterTripletModules {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  TripletsOccupancyConst tripletsOccupancy,
                                  ObjectRangesConst ranges,
                                  uint32_t const* moduleNodeOffsets,
                                  ChainNodes nodes) const {
      uint32_t const nNodes = static_cast<uint32_t>(nodes.metadata().size());
      uint32_t const nLowerModules = modules.nLowerModules();

      for (uint32_t module : cms::alpakatools::uniform_elements(acc, nLowerModules)) {
        uint32_t const nT3InModule = tripletsOccupancy.nTriplets()[module];
        if (nT3InModule == 0u)
          continue;

        uint32_t const nodeBase = moduleNodeOffsets[module];
        uint32_t const sparseBase = static_cast<uint32_t>(ranges.tripletModuleIndices()[module]);
        for (uint32_t i = 0; i < nT3InModule; ++i) {
          ALPAKA_ASSERT_ACC(nodeBase + i < nNodes);
          nodes.tripletIndex()[nodeBase + i] = sparseBase + i;
        }
      }
    }
  };

  // K1b. Exclusive prefixes of the two K1a tallies, and in the same pass the exclusive prefix of
  // the per-key edge products degIn * degOut, which yields the exact enumerable edge count for
  // this key family (E1 for the MD-keyed instance, E2 for the Segment-keyed one).
  struct ChainPrefixIncidence {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, ChainIncidence incidence, uint32_t nKeys) const {
      // 1-block kernel
      ALPAKA_ASSERT_ACC((alpaka::getWorkDiv<alpaka::Grid, alpaka::Blocks>(acc)[0u] == 1));
      ALPAKA_ASSERT_ACC(static_cast<uint32_t>(incidence.metadata().size()) == nKeys + 1u);

      auto& partial = alpaka::declareSharedVar<uint32_t[3 * kChainScanBlockThreads], __COUNTER__>(acc);

      uint32_t const nWorkers = chainScanWorkerCount(acc);
      uint32_t const worker = chainScanWorkerIndex(acc);
      ALPAKA_ASSERT_ACC(nWorkers <= kChainScanBlockThreads);

      uint32_t const chunk = (nKeys + nWorkers - 1u) / nWorkers;
      uint32_t const begin = (worker * chunk < nKeys) ? worker * chunk : nKeys;
      uint32_t const end = (begin + chunk < nKeys) ? begin + chunk : nKeys;

      uint32_t localOut = 0u, localIn = 0u, localProd = 0u;
      for (uint32_t k = begin; k < end; ++k) {
        uint32_t const degOut = incidence.t3OutCounts()[k];
        uint32_t const degIn = incidence.t3InCounts()[k];
        localOut += degOut;
        localIn += degIn;
        localProd += degIn * degOut;
      }
      partial[worker] = localOut;
      partial[nWorkers + worker] = localIn;
      partial[2u * nWorkers + worker] = localProd;

      alpaka::syncBlockThreads(acc);

      uint32_t baseOut = 0u, baseIn = 0u, baseProd = 0u;
      uint32_t totOut = 0u, totIn = 0u, totProd = 0u;
      for (uint32_t w = 0; w < nWorkers; ++w) {
        if (w == worker) {
          baseOut = totOut;
          baseIn = totIn;
          baseProd = totProd;
        }
        totOut += partial[w];
        totIn += partial[nWorkers + w];
        totProd += partial[2u * nWorkers + w];
      }

      uint32_t runOut = baseOut, runIn = baseIn, runProd = baseProd;
      for (uint32_t k = begin; k < end; ++k) {
        uint32_t const degOut = incidence.t3OutCounts()[k];
        uint32_t const degIn = incidence.t3InCounts()[k];
        incidence.t3OutOffsets()[k] = runOut;
        incidence.t3InOffsets()[k] = runIn;
        incidence.edgeProdPrefix()[k] = runProd;
        runOut += degOut;
        runIn += degIn;
        runProd += degIn * degOut;
      }

      alpaka::syncBlockThreads(acc);
      if (cms::alpakatools::once_per_block(acc)) {
        incidence.t3OutOffsets()[nKeys] = totOut;
        incidence.t3InOffsets()[nKeys] = totIn;
        incidence.edgeProdPrefix()[nKeys] = totProd;
        incidence.nEdgesExact() = totProd;
      }
    }
  };

  // Bijective 32-bit avalanche (the splitmix32 finalizer). Used to build the stable node identity
  // below: it is a permutation of uint32, so mixing cannot lose information, and it destroys the
  // strong positional structure of hit rows (neighbouring MDs differ in one low bit) that a plain
  // XOR or shift-add would leave intact.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainMix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
  }

  // The run- and backend-invariant identity of a chain node, see ChainNodesSoA.h. The six hit rows
  // of the node's three MDs are folded in a fixed positional order, so no two nodes with different
  // hit content can alias through a coincidental permutation.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainNodeStableId(
      MiniDoubletsConst mds, uint32_t md0, uint32_t md1, uint32_t md2) {
    uint32_t s = 0x9e3779b9u;
    uint32_t const h[6] = {static_cast<uint32_t>(mds.anchorHitIndices()[md0]),
                           static_cast<uint32_t>(mds.outerHitIndices()[md0]),
                           static_cast<uint32_t>(mds.anchorHitIndices()[md1]),
                           static_cast<uint32_t>(mds.outerHitIndices()[md1]),
                           static_cast<uint32_t>(mds.anchorHitIndices()[md2]),
                           static_cast<uint32_t>(mds.outerHitIndices()[md2])};
    for (int k = 0; k < 6; ++k)
      s = chainMix32(s ^ h[k]);
    return s;
  }

  // K1c. Fill the four CSR payloads. Arrival order within a key slice is irrelevant: the edge set
  // of a key is the full cross product of its in-slice and its out-slice, which is invariant under
  // permutation of either slice.
  //
  // The count columns arrive here zeroed again and are reused as the per-key write cursors.
  //
  // The same pass writes the node's stableId: every index it needs is already loaded here, so the
  // determinism anchor costs one extra MD load (the middle MD) and one write per node, once per
  // event, instead of anything inside the weld sweeps.
  struct ChainScatterIncidence {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  MiniDoubletsConst mds,
                                  ChainNodes nodes,
                                  ChainIncidence mdIncidence,
                                  ChainIncidence lsIncidence) const {
      uint32_t const nNodes = static_cast<uint32_t>(nodes.metadata().size());

      for (uint32_t node : cms::alpakatools::uniform_elements(acc, nNodes)) {
        uint32_t const t3 = nodes.tripletIndex()[node];
        uint32_t const innerSegmentIndex = triplets.segmentIndices()[t3][0];
        uint32_t const outerSegmentIndex = triplets.segmentIndices()[t3][1];
        uint32_t const firstMDIndex = segments.mdIndices()[innerSegmentIndex][0];
        uint32_t const lastMDIndex = segments.mdIndices()[outerSegmentIndex][1];
        uint32_t const midMDIndex = segments.mdIndices()[innerSegmentIndex][1];

        nodes.stableId()[node] = chainNodeStableId(mds, firstMDIndex, midMDIndex, lastMDIndex);

        uint32_t slot;

        slot = mdIncidence.t3OutOffsets()[firstMDIndex] +
               alpaka::atomicAdd(acc, &mdIncidence.t3OutCounts()[firstMDIndex], 1u, alpaka::hierarchy::Threads{});
        ALPAKA_ASSERT_ACC(slot < mdIncidence.t3OutOffsets()[firstMDIndex + 1u]);
        nodes.mdT3OutItems()[slot] = node;

        slot = mdIncidence.t3InOffsets()[lastMDIndex] +
               alpaka::atomicAdd(acc, &mdIncidence.t3InCounts()[lastMDIndex], 1u, alpaka::hierarchy::Threads{});
        ALPAKA_ASSERT_ACC(slot < mdIncidence.t3InOffsets()[lastMDIndex + 1u]);
        nodes.mdT3InItems()[slot] = node;

        slot = lsIncidence.t3OutOffsets()[innerSegmentIndex] +
               alpaka::atomicAdd(
                   acc, &lsIncidence.t3OutCounts()[innerSegmentIndex], 1u, alpaka::hierarchy::Threads{});
        ALPAKA_ASSERT_ACC(slot < lsIncidence.t3OutOffsets()[innerSegmentIndex + 1u]);
        nodes.lsT3OutItems()[slot] = node;

        slot = lsIncidence.t3InOffsets()[outerSegmentIndex] +
               alpaka::atomicAdd(
                   acc, &lsIncidence.t3InCounts()[outerSegmentIndex], 1u, alpaka::hierarchy::Threads{});
        ALPAKA_ASSERT_ACC(slot < lsIncidence.t3InOffsets()[outerSegmentIndex + 1u]);
        nodes.lsT3InItems()[slot] = node;
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
