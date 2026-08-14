#ifndef RecoTracker_LSTCore_src_alpaka_ChainGraph_h
#define RecoTracker_LSTCore_src_alpaka_ChainGraph_h

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainConfig.h"
#include "RecoTracker/LSTCore/interface/ChainIncidenceSoA.h"
#include "RecoTracker/LSTCore/interface/ChainNodesSoA.h"
#include "RecoTracker/LSTCore/interface/MiniDoubletsSoA.h"
#include "RecoTracker/LSTCore/interface/ModulesSoA.h"
#include "RecoTracker/LSTCore/interface/ObjectRangesSoA.h"
#include "RecoTracker/LSTCore/interface/SegmentsSoA.h"
#include "RecoTracker/LSTCore/interface/TripletsSoA.h"

// Chain-tracking graph construction: the first stage of the chain pipeline.
//
// It turns LST's module-segmented triplet store into a graph whose NODES are triplets and whose
// incidence is keyed by the detector element two triplets can share -- a mini-doublet or a line
// segment. Nothing here decides any physics; it only builds the index structures the edge builder
// (ChainEdges.h) enumerates over. Stages:
//   dense global node index over the module-segmented triplet store, plus the dense
//       mini-doublet / segment key numbering the two incidence CSRs are built on
//   exclusive prefixes of the incidence tallies, and of the per-key degIn * degOut product that
//       gives the exact enumerable edge count
//   CSR payload fill, plus each node's run-invariant identity
// The four incidence tallies is fused into the triplet builder, see Triplet.h.
//
// Nothing here is read by any existing LST stage: the whole file only ever runs when
// useChainTracking is true, and it only writes into the ChainIncidence / ChainNodes collections.

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

  // The per-shared-key degree cap (ChainConfig::degreeCap). ONE expression, used by the incidence-prefix lane-2
  // product and by the edge index decode, so the two can never disagree about how many of a key's
  // triplets exist -- which they must not, since the decode inverts the prefix the scan wrote. At
  // kChainDegreeCapOff this is the identity on every reachable degree.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainCappedDegree(uint32_t degree, uint32_t degreeCap) {
    return (degree < degreeCap) ? degree : degreeCap;
  }

  // Block-wide exclusive scan over ONE value per worker, for NLanes independent value streams at
  // once. Hillis-Steele, so ceil(log2(nWorkers)) = 10 steps at the 1024-wide block rather than the
  // nWorkers serial iterations a per-worker accumulation costs -- and that serial cost is fixed,
  // i.e. it dominates these kernels no matter how few keys there are.
  //
  // EXACTNESS. The only operation performed on the data is uint32_t addition, i.e. addition in
  // Z/2^32, which is associative and commutative -- wraparound included, since it IS the group
  // operation. Each worker receives the sum of a fixed multiset of addends (values[0..worker) for
  // `base`, values[0..nWorkers) for `total`), so any regrouping of that sum is bit-identical for
  // every possible input on every backend. Keep it that way: a scan that carried floats here would
  // not be reproducible.
  //
  // `scratch` must hold 2 * NLanes * Stride words. It is double buffered so that a step needs ONE
  // syncBlockThreads (write to the other buffer) rather than two (read, sync, write in place, sync).
  // Every worker in the block executes every sync -- the loop bound depends only on nWorkers, which
  // is block-uniform -- so the barriers are never divergent.
  //
  // `Stride` is the number of words reserved per lane in `scratch`; it must be at least the block's
  // thread count. It is a parameter only so that the tiled scan below, whose blocks are 256 wide,
  // does not have to reserve 1024 words per lane.
  //
  // The ENTRY barrier is what makes the primitive safe to call REPEATEDLY on the same scratch:
  // without it one thread could overwrite scratch for call n+1 while another was still reading call
  // n's result, which the tiled scan (one call per chunk) hits on its second chunk.
  //
  // On a CPU backend alpaka gives one thread with many elements, so nWorkers == 1, the loop body
  // never runs, and this degenerates to base = 0 / total = value.
  template <uint32_t NLanes, uint32_t Stride = kChainScanBlockThreads, typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void chainScanBlockExclusive(TAcc const& acc,
                                                              uint32_t* scratch,
                                                              uint32_t nWorkers,
                                                              uint32_t worker,
                                                              uint32_t const (&value)[NLanes],
                                                              uint32_t (&base)[NLanes],
                                                              uint32_t (&total)[NLanes]) {
    constexpr uint32_t kStride = Stride;
    uint32_t* current = scratch;
    uint32_t* next = scratch + NLanes * kStride;

    alpaka::syncBlockThreads(acc);
    for (uint32_t lane = 0; lane < NLanes; ++lane)
      current[lane * kStride + worker] = value[lane];
    alpaka::syncBlockThreads(acc);

    for (uint32_t step = 1u; step < nWorkers; step <<= 1) {
      if (worker >= step) {
        for (uint32_t lane = 0; lane < NLanes; ++lane)
          next[lane * kStride + worker] = current[lane * kStride + worker - step] + current[lane * kStride + worker];
      } else {
        for (uint32_t lane = 0; lane < NLanes; ++lane)
          next[lane * kStride + worker] = current[lane * kStride + worker];
      }
      alpaka::syncBlockThreads(acc);
      uint32_t* const previous = current;
      current = next;
      next = previous;
    }

    // current[] now holds the INCLUSIVE prefix over workers, and the last sync above published it.
    for (uint32_t lane = 0; lane < NLanes; ++lane) {
      total[lane] = current[lane * kStride + nWorkers - 1u];
      base[lane] = current[lane * kStride + worker] - value[lane];
    }
  }

  // ChainPrefixTripletModules. Exclusive prefix over the per-lower-module triplet counts, giving each module
  // the base of its slice in the dense node numbering, and the total node count.
  struct ChainPrefixTripletModules {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  TripletsOccupancyConst tripletsOccupancy,
                                  uint32_t* moduleNodeOffsets,
                                  uint32_t* nNodes) const {
      // 1-block kernel
      ALPAKA_ASSERT_ACC((alpaka::getWorkDiv<alpaka::Grid, alpaka::Blocks>(acc)[0u] == 1));

      auto& partial = alpaka::declareSharedVar<uint32_t[2 * kChainScanBlockThreads], __COUNTER__>(acc);

      uint32_t const nWorkers = chainScanWorkerCount(acc);
      uint32_t const worker = chainScanWorkerIndex(acc);
      ALPAKA_ASSERT_ACC(nWorkers <= kChainScanBlockThreads);

      uint32_t const nKeys = modules.nLowerModules();
      uint32_t const chunkSize = (nKeys + nWorkers - 1u) / nWorkers;
      uint32_t const beginModule = (worker * chunkSize < nKeys) ? worker * chunkSize : nKeys;
      uint32_t const endModule = (beginModule + chunkSize < nKeys) ? beginModule + chunkSize : nKeys;

      uint32_t local[1] = {0u};
      for (uint32_t moduleIdx = beginModule; moduleIdx < endModule; ++moduleIdx)
        local[0] += tripletsOccupancy.nTriplets()[moduleIdx];

      uint32_t base[1], total[1];
      chainScanBlockExclusive<1>(acc, &partial[0], nWorkers, worker, local, base, total);

      uint32_t running = base[0];
      for (uint32_t moduleIdx = beginModule; moduleIdx < endModule; ++moduleIdx) {
        moduleNodeOffsets[moduleIdx] = running;
        running += tripletsOccupancy.nTriplets()[moduleIdx];
      }

      alpaka::syncBlockThreads(acc);
      if (cms::alpakatools::once_per_block(acc)) {
        moduleNodeOffsets[nKeys] = total[0];
        *nNodes = total[0];
      }
    }
  };

  // ChainScatterTripletModules. Scatter the module-segmented triplet indices into the dense node numbering.
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

  // The dense-index bias. The per-module bias that turns a RAW MiniDoublet / Segment index into a DENSE one, plus
  // the two dense totals.
  //
  // LST sizes each module's MD and LS slice from a worst-case occupancy estimate and fills only
  // part of it, so the raw index space is much larger than the produced-object count: on PU200,
  // 82049 MD slots for 40187 produced MDs (2.0x) and 528062 Segment slots for 112491 produced
  // Segments (4.7x). Keying the two ChainIncidence instances by the dense index rather than the raw
  // one is what makes them affordable, since a CSR pays per KEY whether or not the key exists.
  //
  // With  dense(module, i) = denseBase[module] + i  and  raw(module, i) = sparseBase[module] + i,
  // both for i < count[module], the bias is
  //     bias[module] = denseBase[module] - sparseBase[module]   and   dense = raw + bias[module].
  // denseBase <= sparseBase always, so the subtraction wraps; it is stored and applied in uint32_t,
  // where wrapping is defined and exact - the recovered dense index is a valid index, hence inside
  // [0, 2^32), hence the unique representative of the modular result.
  //
  // The remap changes no downstream value: denseBase and sparseBase are both non-decreasing in
  // module, so the two numberings order the produced objects identically, and the slack slots that
  // disappear were never referenced by any triplet -- every key they occupied had
  // degIn == degOut == 0 and contributed nothing to any prefix.
  struct ChainPrefixKeyModules {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsOccupancyConst mdsOccupancy,
                                  SegmentsOccupancyConst segmentsOccupancy,
                                  ObjectRangesConst ranges,
                                  uint32_t* mdKeyBias,
                                  uint32_t* lsKeyBias,
                                  uint32_t* totals) const {
      // 1-block kernel
      ALPAKA_ASSERT_ACC((alpaka::getWorkDiv<alpaka::Grid, alpaka::Blocks>(acc)[0u] == 1));

      auto& partial = alpaka::declareSharedVar<uint32_t[4 * kChainScanBlockThreads], __COUNTER__>(acc);

      uint32_t const nWorkers = chainScanWorkerCount(acc);
      uint32_t const worker = chainScanWorkerIndex(acc);
      ALPAKA_ASSERT_ACC(nWorkers <= kChainScanBlockThreads);

      uint32_t const nKeys = modules.nLowerModules();
      uint32_t const chunkSize = (nKeys + nWorkers - 1u) / nWorkers;
      uint32_t const beginModule = (worker * chunkSize < nKeys) ? worker * chunkSize : nKeys;
      uint32_t const endModule = (beginModule + chunkSize < nKeys) ? beginModule + chunkSize : nKeys;

      uint32_t local[2] = {0u, 0u};
      for (uint32_t moduleIdx = beginModule; moduleIdx < endModule; ++moduleIdx) {
        local[0] += mdsOccupancy.nMDs()[moduleIdx];
        local[1] += segmentsOccupancy.nSegments()[moduleIdx];
      }

      uint32_t base[2], total[2];
      chainScanBlockExclusive<2>(acc, &partial[0], nWorkers, worker, local, base, total);

      uint32_t runMd = base[0], runLs = base[1];
      for (uint32_t moduleIdx = beginModule; moduleIdx < endModule; ++moduleIdx) {
        mdKeyBias[moduleIdx] = runMd - static_cast<uint32_t>(ranges.miniDoubletModuleIndices()[moduleIdx]);
        lsKeyBias[moduleIdx] = runLs - static_cast<uint32_t>(ranges.segmentModuleIndices()[moduleIdx]);
        runMd += mdsOccupancy.nMDs()[moduleIdx];
        runLs += segmentsOccupancy.nSegments()[moduleIdx];
      }

      alpaka::syncBlockThreads(acc);
      if (cms::alpakatools::once_per_block(acc)) {
        totals[0] = total[0];
        totals[1] = total[1];
      }
    }
  };

  // Tiling of the multi-block incidence scan. The tile COUNT is a launch parameter rather than a
  // constant so it can be tuned without a rebuild; only the MAXIMUM is compile-time, because it
  // fixes the tileSums stride and that buffer's size. 256 threads is the block width every other
  // chain kernel already uses (chainFlat_workDiv).
  static constexpr uint32_t kChainScanTilesMax = 512u;
  static constexpr uint32_t kChainScanTilesDefault = 128u;
  static constexpr uint32_t kChainScanTileThreads = 256u;

  // ChainPrefixIncidence. Exclusive prefixes of the two incidence tallies, and in the same pass the exclusive prefix of the
  // per-key edge products degIn * degOut, which yields the exact enumerable edge count for this key
  // family (E1 for the MD-keyed instance, E2 for the Segment-keyed one).
  //
  // Lane 2 (and lane 2 ONLY) carries the CAPPED product min(degIn, degCap) * min(degOut, degCap) --
  // see ChainConfig::degreeCap. Lanes 0 and 1 stay the full tallies, because they are the CSR
  // offsets ChainScatterIncidence fills against and its slices must keep room for every triplet. ChainBuildEdges inverts this
  // prefix, so it applies the identical cap to the out-degree it divides by (both go through
  // chainCappedDegree). At kChainDegreeCapOff the expression is the identity.
  //
  // WHY TILED. Block b owns the contiguous tile [b*span, (b+1)*span) and walks it in chunks of
  // blockThreads keys with worker w taking key chunkStart + w, so a warp's loads fall in one 128-byte
  // line. A single-block form with one contiguous chunk per worker uses one SM of 142 and reads
  // addresses ~1.5 kB apart, one 32-byte sector per load: ~5 sectors per key to move 4 useful bytes.
  //
  // THREE PASSES, ONE KERNEL STRUCT, selected by `phase`, so the kernel count does not move:
  //   phase 0: block b sums its tile into tileSums[lane * kChainScanTilesMax + b]
  //   phase 1: block b re-derives its own tile base by summing tileSums[lane][0 .. b), then walks
  //            its tile writing the three offset columns
  //   phase 2: the 64-bit recount below
  // Phases 0 and 1 need a grid-wide barrier between them, which on any backend means two launches.
  //
  // EXACTNESS. Every value written is a sum of a fixed multiset of uint32_t addends -- for key k,
  // the counts of all keys before k -- and uint32_t addition is the group operation of Z/2^32,
  // associative and commutative including its wraparound. The KEY ORDER of the partition is
  // preserved at both levels (tiles ascend with b, and within a chunk worker w takes chunkStart + w).
  // No warp primitive and no warp-width assumption is used; on a host backend
  // chainScanWorkerCount() is 1 and each block degenerates to a sequential pass over its own tile.
  //
  // tileSums is 3 * kChainScanTilesMax words, a COMPILE-TIME size, so it is a persistent LSTEvent
  // member allocated once rather than a per-event device buffer. The MD and LS instances share it
  // because their launches are queue-ordered.
  //
  // PHASE 2, the 64-bit recount that keeps the host's allocation guard honest. `nEdgesExact` is a
  // uint32 sum of uint32 products, so beyond 2^32 edges it wraps silently and a guard fed the
  // wrapped value would wave a corrupting event through. The host can certify the uint32 count is
  // exact whenever min(nT3, degCap) * nT3 < 2^32, because
  // sum_k degIn*degOut <= (sum_k degIn) * max_k degOut <= nT3 * min(nT3, degCap); that holds for
  // every capped configuration, so this phase exists for the uncapped one, which is exactly the
  // configuration whose count cannot be trusted.
  //
  // It rides in this kernel rather than in one of its own because `phase` is already an argument,
  // so neither the kernel count nor the argument pack moves. Worker 0 of each block walks its whole
  // tile serially and writes one uint64 as lo/hi into lanes 0 and 1 of the scratch, which the host
  // sums: no cross-worker 64-bit reduction, no atomic, no new buffer. The other workers return
  // immediately and no barrier is executed on this path, so the divergence is safe.
  struct ChainPrefixIncidenceTiled {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainIncidence incidence,
                                  uint32_t nKeys,
                                  uint32_t* tileSums,
                                  uint32_t nTiles,
                                  uint32_t phase,
                                  uint32_t degCap) const {
      ALPAKA_ASSERT_ACC(static_cast<uint32_t>(incidence.metadata().size()) == nKeys + 1u);
      constexpr uint32_t kLanes = 3u;
      auto& scratch = alpaka::declareSharedVar<uint32_t[2u * kLanes * kChainScanTileThreads], __COUNTER__>(acc);

      uint32_t const nWorkers = chainScanWorkerCount(acc);
      uint32_t const worker = chainScanWorkerIndex(acc);
      uint32_t const tile = static_cast<uint32_t>(alpaka::getIdx<alpaka::Grid, alpaka::Blocks>(acc)[0u]);
      ALPAKA_ASSERT_ACC(nWorkers <= kChainScanTileThreads);
      ALPAKA_ASSERT_ACC(nTiles >= 1u && nTiles <= kChainScanTilesMax);
      ALPAKA_ASSERT_ACC(tile < nTiles);

      uint32_t const span = (nKeys + nTiles - 1u) / nTiles;
      uint32_t const beginKey = (tile * span < nKeys) ? tile * span : nKeys;
      uint32_t const endKey = (beginKey + span < nKeys) ? beginKey + span : nKeys;

      if (phase == 2u) {
        if (worker != 0u)
          return;
        uint64_t productSum = 0;
        for (uint32_t keyIdx = beginKey; keyIdx < endKey; ++keyIdx) {
          uint64_t const degOut = chainCappedDegree(incidence.t3OutCounts()[keyIdx], degCap);
          uint64_t const degIn = chainCappedDegree(incidence.t3InCounts()[keyIdx], degCap);
          productSum += degIn * degOut;
        }
        tileSums[tile] = static_cast<uint32_t>(productSum & 0xffffffffu);
        tileSums[kChainScanTilesMax + tile] = static_cast<uint32_t>(productSum >> 32);
        return;
      }

      if (phase == 0u) {
        uint32_t local[kLanes] = {0u, 0u, 0u};
        for (uint32_t keyIdx = beginKey + worker; keyIdx < endKey; keyIdx += nWorkers) {
          uint32_t const degOut = incidence.t3OutCounts()[keyIdx];
          uint32_t const degIn = incidence.t3InCounts()[keyIdx];
          local[0] += degOut;
          local[1] += degIn;
          local[2] += chainCappedDegree(degIn, degCap) * chainCappedDegree(degOut, degCap);
        }
        uint32_t base[kLanes], total[kLanes];
        chainScanBlockExclusive<kLanes, kChainScanTileThreads>(acc, &scratch[0], nWorkers, worker, local, base, total);
        if (cms::alpakatools::once_per_block(acc))
          for (uint32_t lane = 0; lane < kLanes; ++lane)
            tileSums[lane * kChainScanTilesMax + tile] = total[lane];
        return;
      }

      // This tile's base, and the grand totals. Every block repeats this walk over the same few
      // cache lines, which is cheaper than a launch of its own; it costs nTiles iterations, so it
      // is the term that grows if the tile count is raised.
      uint32_t running[kLanes] = {0u, 0u, 0u};
      uint32_t grand[kLanes] = {0u, 0u, 0u};
      for (uint32_t tileIdx = 0; tileIdx < nTiles; ++tileIdx)
        for (uint32_t lane = 0; lane < kLanes; ++lane) {
          uint32_t const tileSum = tileSums[lane * kChainScanTilesMax + tileIdx];
          if (tileIdx < tile)
            running[lane] += tileSum;
          grand[lane] += tileSum;
        }

      // The tile itself, one chunk of blockThreads keys at a time. `beginKey` and `endKey` are
      // block uniform, so every worker executes every iteration and therefore every barrier inside
      // the scan -- no divergent barrier is possible here.
      for (uint32_t chunkStart = beginKey; chunkStart < endKey; chunkStart += nWorkers) {
        uint32_t const keyIdx = chunkStart + worker;
        bool const live = keyIdx < endKey;
        uint32_t local[kLanes] = {0u, 0u, 0u};
        if (live) {
          uint32_t const degOut = incidence.t3OutCounts()[keyIdx];
          uint32_t const degIn = incidence.t3InCounts()[keyIdx];
          local[0] = degOut;
          local[1] = degIn;
          local[2] = chainCappedDegree(degIn, degCap) * chainCappedDegree(degOut, degCap);
        }
        uint32_t base[kLanes], total[kLanes];
        chainScanBlockExclusive<kLanes, kChainScanTileThreads>(acc, &scratch[0], nWorkers, worker, local, base, total);
        if (live) {
          incidence.t3OutOffsets()[keyIdx] = running[0] + base[0];
          incidence.t3InOffsets()[keyIdx] = running[1] + base[1];
          incidence.edgeProdPrefix()[keyIdx] = running[2] + base[2];
        }
        for (uint32_t lane = 0; lane < kLanes; ++lane)
          running[lane] += total[lane];
      }

      if (tile == 0u && cms::alpakatools::once_per_block(acc)) {
        incidence.t3OutOffsets()[nKeys] = grand[0];
        incidence.t3InOffsets()[nKeys] = grand[1];
        incidence.edgeProdPrefix()[nKeys] = grand[2];
        incidence.nEdgesExact() = grand[2];
      }
    }
  };

  // Bijective 32-bit avalanche (the splitmix32 finalizer). Used to build the stable node identity
  // below: it is a permutation of uint32, so mixing cannot lose information, and it destroys the
  // strong positional structure of hit rows (neighbouring MDs differ in one low bit) that a plain
  // XOR or shift-add would leave intact.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainMix32(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
  }

  // The run- and backend-invariant identity of a chain node, see ChainNodesSoA.h. The six hit rows
  // of the node's three MDs are folded in a fixed positional order, so no two nodes with different
  // hit content can alias through a coincidental permutation.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainNodeStableId(MiniDoubletsConst miniDoublets,
                                                            uint32_t firstMD,
                                                            uint32_t midMD,
                                                            uint32_t lastMD) {
    uint32_t hash = 0x9e3779b9u;
    uint32_t const hitIdx[6] = {static_cast<uint32_t>(miniDoublets.anchorHitIndices()[firstMD]),
                                static_cast<uint32_t>(miniDoublets.outerHitIndices()[firstMD]),
                                static_cast<uint32_t>(miniDoublets.anchorHitIndices()[midMD]),
                                static_cast<uint32_t>(miniDoublets.outerHitIndices()[midMD]),
                                static_cast<uint32_t>(miniDoublets.anchorHitIndices()[lastMD]),
                                static_cast<uint32_t>(miniDoublets.outerHitIndices()[lastMD])};
    for (int k = 0; k < 6; ++k)
      hash = chainMix32(hash ^ hitIdx[k]);
    return hash;
  }

  // ChainScatterIncidence. Fill the four CSR payloads. Arrival order within a key slice is irrelevant: the edge set
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
                                  MiniDoubletsConst miniDoublets,
                                  ChainNodes nodes,
                                  ChainIncidence mdIncidence,
                                  ChainIncidence lsIncidence,
                                  uint32_t const* mdKeyBias,
                                  uint32_t const* lsKeyBias) const {
      uint32_t const nNodes = static_cast<uint32_t>(nodes.metadata().size());

      for (uint32_t node : cms::alpakatools::uniform_elements(acc, nNodes)) {
        uint32_t const tripletIdx = nodes.tripletIndex()[node];
        uint32_t const innerSegmentIndex = triplets.segmentIndices()[tripletIdx][0];
        uint32_t const outerSegmentIndex = triplets.segmentIndices()[tripletIdx][1];
        uint32_t const firstMDIndex = segments.mdIndices()[innerSegmentIndex][0];
        uint32_t const lastMDIndex = segments.mdIndices()[outerSegmentIndex][1];
        uint32_t const midMDIndex = segments.mdIndices()[innerSegmentIndex][1];

        nodes.stableId()[node] = chainNodeStableId(miniDoublets, firstMDIndex, midMDIndex, lastMDIndex);

        // Raw -> dense incidence keys (ChainPrefixKeyModules). The triplet's three lower modules
        // own, in order: its first MD and its inner Segment (layer 0), its outer Segment
        // (layer 1), and its last MD (layer 2).
        uint32_t const module0 = triplets.lowerModuleIndices()[tripletIdx][0];
        uint32_t const module1 = triplets.lowerModuleIndices()[tripletIdx][1];
        uint32_t const module2 = triplets.lowerModuleIndices()[tripletIdx][2];
        uint32_t const firstMDKey = firstMDIndex + mdKeyBias[module0];
        uint32_t const lastMDKey = lastMDIndex + mdKeyBias[module2];
        uint32_t const innerLSKey = innerSegmentIndex + lsKeyBias[module0];
        uint32_t const outerLSKey = outerSegmentIndex + lsKeyBias[module1];

        // The "in" side keys are re-read once per incident edge by the edge and chain feature
        // builders; storing them here saves that re-derivation.
        nodes.mdKeyIn()[node] = lastMDKey;
        nodes.lsKeyIn()[node] = outerLSKey;

        uint32_t slot;

        slot = mdIncidence.t3OutOffsets()[firstMDKey] +
               alpaka::atomicAdd(acc, &mdIncidence.t3OutCounts()[firstMDKey], 1u, alpaka::hierarchy::Threads{});
        ALPAKA_ASSERT_ACC(slot < mdIncidence.t3OutOffsets()[firstMDKey + 1u]);
        nodes.mdT3OutItems()[slot] = node;

        slot = mdIncidence.t3InOffsets()[lastMDKey] +
               alpaka::atomicAdd(acc, &mdIncidence.t3InCounts()[lastMDKey], 1u, alpaka::hierarchy::Threads{});
        ALPAKA_ASSERT_ACC(slot < mdIncidence.t3InOffsets()[lastMDKey + 1u]);
        nodes.mdT3InItems()[slot] = node;

        slot = lsIncidence.t3OutOffsets()[innerLSKey] +
               alpaka::atomicAdd(acc, &lsIncidence.t3OutCounts()[innerLSKey], 1u, alpaka::hierarchy::Threads{});
        ALPAKA_ASSERT_ACC(slot < lsIncidence.t3OutOffsets()[innerLSKey + 1u]);
        nodes.lsT3OutItems()[slot] = node;

        slot = lsIncidence.t3InOffsets()[outerLSKey] +
               alpaka::atomicAdd(acc, &lsIncidence.t3InCounts()[outerLSKey], 1u, alpaka::hierarchy::Threads{});
        ALPAKA_ASSERT_ACC(slot < lsIncidence.t3InOffsets()[outerLSKey + 1u]);
        nodes.lsT3InItems()[slot] = node;
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
