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

  // JET ROUND: the per-shared-key degree cap (ChainConfig::degreeCap). ONE expression, used by the
  // K1b lane-2 product and by the K2 index decode, so the two can never disagree about how many of
  // a key's triplets exist -- which they must not, since the decode inverts the prefix the scan
  // wrote. At kChainDegreeCapOff this is the identity on every reachable degree.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainCappedDegree(uint32_t deg, uint32_t cap) {
    return (deg < cap) ? deg : cap;
  }

  // Block-wide exclusive scan over ONE value per worker, for NLanes independent value streams at
  // once. Every single-block scan in the chain path used to close with
  //     for (uint32_t w = 0; w < nWorkers; ++w) { if (w == worker) base = total; total += partial[w]; }
  // which is nWorkers serial iterations run REDUNDANTLY BY EVERY WORKER -- 1024 dependent shared
  // loads and adds per thread, and therefore a fixed cost that dominates every one of these kernels
  // whatever nKeys is. This replaces it with a Hillis-Steele scan: ceil(log2(nWorkers)) = 10 steps.
  //
  // EXACTNESS. The only operation performed on the data is uint32_t addition, i.e. addition in
  // Z/2^32, which is associative and commutative -- wraparound included, since it IS the group
  // operation. Both forms return, for each worker, the sum of the SAME multiset of addends
  // (partial[0..worker) for `base`, partial[0..nWorkers) for `total`), so they agree bit for bit for
  // every possible input, on every backend, with no assumption about the values. This is a
  // regrouping of a sum and nothing else; it cannot move a physics bit.
  //
  // `scratch` must hold 2 * NLanes * Stride words. It is double buffered so that a
  // step needs ONE syncBlockThreads (write to the other buffer) rather than two (read, sync, write
  // in place, sync). Every worker in the block executes every sync -- the loop bound depends only on
  // nWorkers, which is block-uniform -- so the barriers are never divergent.
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
  // never runs, and this degenerates to base = 0 / total = value exactly as the serial form did.
  template <uint32_t NLanes, uint32_t Stride = kChainScanBlockThreads, typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void chainScanBlockExclusive(TAcc const& acc,
                                                              uint32_t* scratch,
                                                              uint32_t nWorkers,
                                                              uint32_t worker,
                                                              uint32_t const (&value)[NLanes],
                                                              uint32_t (&base)[NLanes],
                                                              uint32_t (&total)[NLanes]) {
    constexpr uint32_t kStride = Stride;
    uint32_t* cur = scratch;
    uint32_t* nxt = scratch + NLanes * kStride;

    alpaka::syncBlockThreads(acc);
    for (uint32_t l = 0; l < NLanes; ++l)
      cur[l * kStride + worker] = value[l];
    alpaka::syncBlockThreads(acc);

    for (uint32_t d = 1u; d < nWorkers; d <<= 1) {
      if (worker >= d) {
        for (uint32_t l = 0; l < NLanes; ++l)
          nxt[l * kStride + worker] = cur[l * kStride + worker - d] + cur[l * kStride + worker];
      } else {
        for (uint32_t l = 0; l < NLanes; ++l)
          nxt[l * kStride + worker] = cur[l * kStride + worker];
      }
      alpaka::syncBlockThreads(acc);
      uint32_t* const swap = cur;
      cur = nxt;
      nxt = swap;
    }

    // cur[] now holds the INCLUSIVE prefix over workers, and the last sync above published it.
    for (uint32_t l = 0; l < NLanes; ++l) {
      total[l] = cur[l * kStride + nWorkers - 1u];
      base[l] = cur[l * kStride + worker] - value[l];
    }
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

      auto& partial = alpaka::declareSharedVar<uint32_t[2 * kChainScanBlockThreads], __COUNTER__>(acc);

      uint32_t const nWorkers = chainScanWorkerCount(acc);
      uint32_t const worker = chainScanWorkerIndex(acc);
      ALPAKA_ASSERT_ACC(nWorkers <= kChainScanBlockThreads);

      uint32_t const nKeys = modules.nLowerModules();
      uint32_t const chunk = (nKeys + nWorkers - 1u) / nWorkers;
      uint32_t const begin = (worker * chunk < nKeys) ? worker * chunk : nKeys;
      uint32_t const end = (begin + chunk < nKeys) ? begin + chunk : nKeys;

      uint32_t local[1] = {0u};
      for (uint32_t k = begin; k < end; ++k)
        local[0] += tripletsOccupancy.nTriplets()[k];

      uint32_t base[1], total[1];
      chainScanBlockExclusive<1>(acc, &partial[0], nWorkers, worker, local, base, total);

      uint32_t running = base[0];
      for (uint32_t k = begin; k < end; ++k) {
        moduleNodeOffsets[k] = running;
        running += tripletsOccupancy.nTriplets()[k];
      }

      alpaka::syncBlockThreads(acc);
      if (cms::alpakatools::once_per_block(acc)) {
        moduleNodeOffsets[nKeys] = total[0];
        *nNodes = total[0];
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

  // K0c (phase P2.6c). The per-module bias that turns a RAW MiniDoublet / Segment index into a
  // DENSE one, plus the two dense totals.
  //
  // LST sizes each module's MD and LS slice from a worst-case occupancy estimate and fills only
  // part of it, so the raw index space is much larger than the produced-object count: measured on
  // PU200RelVal, 82049 MD slots for 40187 produced MDs (2.0x) and 528062 Segment slots for 112491
  // produced Segments (4.7x). The two ChainIncidence instances used to be keyed by the raw index
  // and so paid for all of that slack; keying them by the dense index instead is what makes them
  // affordable.
  //
  // With  dense(module, i) = denseBase[module] + i  and  raw(module, i) = sparseBase[module] + i,
  // both for i < count[module], the bias is
  //     bias[module] = denseBase[module] - sparseBase[module]   and   dense = raw + bias[module].
  // denseBase <= sparseBase always, so the subtraction wraps; it is stored and applied in uint32_t,
  // where wrapping is defined and exact - the recovered dense index is a valid index, hence inside
  // [0, 2^32), hence the unique representative of the modular result. Storing one biased word per
  // module keeps the conversion to a single load and a single add at every use site.
  //
  // WHY THIS CANNOT MOVE ANY BIT: denseBase and sparseBase are both non-decreasing in module, so
  // the raw and the dense numbering order the produced objects identically - the remap is strictly
  // increasing on the produced set. The slack slots that disappear were never referenced by any
  // triplet, so every key they occupied had degIn == degOut == 0 and contributed nothing to
  // t3OutOffsets, t3InOffsets or edgeProdPrefix. Deleting zero-degree keys from a CSR whose
  // surviving keys keep their relative order leaves the payload arrays, the prefix values at the
  // surviving keys, and therefore the K2 edge enumeration order, unchanged.
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
      uint32_t const chunk = (nKeys + nWorkers - 1u) / nWorkers;
      uint32_t const begin = (worker * chunk < nKeys) ? worker * chunk : nKeys;
      uint32_t const end = (begin + chunk < nKeys) ? begin + chunk : nKeys;

      uint32_t local[2] = {0u, 0u};
      for (uint32_t k = begin; k < end; ++k) {
        local[0] += mdsOccupancy.nMDs()[k];
        local[1] += segmentsOccupancy.nSegments()[k];
      }

      uint32_t base[2], total[2];
      chainScanBlockExclusive<2>(acc, &partial[0], nWorkers, worker, local, base, total);

      uint32_t runMd = base[0], runLs = base[1];
      for (uint32_t k = begin; k < end; ++k) {
        mdKeyBias[k] = runMd - static_cast<uint32_t>(ranges.miniDoubletModuleIndices()[k]);
        lsKeyBias[k] = runLs - static_cast<uint32_t>(ranges.segmentModuleIndices()[k]);
        runMd += mdsOccupancy.nMDs()[k];
        runLs += segmentsOccupancy.nSegments()[k];
      }

      alpaka::syncBlockThreads(acc);
      if (cms::alpakatools::once_per_block(acc)) {
        totals[0] = total[0];
        totals[1] = total[1];
      }
    }
  };

  // Tiling of the multi-block incidence scan. The tile COUNT is a launch parameter, not a constant,
  // so that one binary can sweep it in a single measurement slot -- including nTiles == 1, which is
  // the direct experiment for "is one block enough once the fixed tail is gone?". Only the MAXIMUM is
  // a compile-time constant, because it fixes the tileSums stride and that buffer's size.
  // 256 threads is the block width every other chain kernel already uses (chainFlat_workDiv).
  static constexpr uint32_t kChainScanTilesMax = 512u;
  static constexpr uint32_t kChainScanTilesDefault = 128u;
  static constexpr uint32_t kChainScanTileThreads = 256u;

  // K1b. Exclusive prefixes of the two K1a tallies, and in the same pass the exclusive prefix of the
  // per-key edge products degIn * degOut, which yields the exact enumerable edge count for this key
  // family (E1 for the MD-keyed instance, E2 for the Segment-keyed one).
  //
  // JET ROUND: lane 2 (and lane 2 ONLY) carries the CAPPED product
  // min(degIn, degCap) * min(degOut, degCap) -- see ChainConfig::degreeCap. Lanes 0 and 1 stay the
  // full tallies, because they are the CSR offsets that K1c fills against and its slices must keep
  // room for every triplet. K2 inverts this prefix, so it applies the identical cap to the
  // out-degree it divides by (both go through chainCappedDegree). At kChainDegreeCapOff the
  // expression is the identity and every word written is what the uncapped form wrote.
  //
  // This REPLACES a single-block form that had the same three prefixes and the same values but gave
  // each of 1024 workers a contiguous chunk of ceil(nKeys/1024) keys. Here the work is spread over
  // nTiles blocks and read COALESCED.
  //
  // THE TWO DEFECTS IT FIXES, both measured. (i) One block = 1 SM of 142. (ii) inside that block
  // each of the 1024 workers owned a CONTIGUOUS chunk of ceil(nKeys/1024) keys, so the 32 threads of
  // a warp read addresses ~1.5 kB apart and every load was its own 32-byte sector -- for nLSKeys =
  // 157k that is ~5 x 157k sectors of 32 B to move 3.1 MB of useful data. Here block b owns the
  // contiguous tile [b*span, (b+1)*span) and walks it in chunks of blockThreads keys with thread w
  // taking key chunk + w, so each warp's load is one 128-byte line.
  //
  // TWO PASSES, ONE KERNEL STRUCT, selected by `phase` -- so the kernel count does not move:
  //   phase 0: block b sums its tile into tileSums[lane * kChainScanTilesMax + b]
  //   phase 1: block b re-derives its own tile base by summing tileSums[lane][0 .. b), then walks
  //            its tile writing the three offset columns
  // The two passes need a grid-wide barrier between them, which on any backend means two launches.
  //
  // EXACTNESS. Every value written is a sum of exactly the same multiset of uint32_t addends as
  // before -- for key k, the counts of all keys before k -- reassociated. uint32_t addition is the
  // group operation of Z/2^32, associative and commutative including its wraparound, so each written
  // word is bit-identical to the single-block form for every possible input. The KEY ORDER of the
  // partition is preserved at both levels (tiles ascend with b, and within a chunk worker w takes
  // key chunk + w), so this is a regrouping of a sum and nothing else. No warp primitive and no
  // warp-width assumption; on a host backend chainScanWorkerCount() is 1 and each block degenerates
  // to a sequential pass over its own tile, which is still the same sum.
  //
  // tileSums is 3 * kChainScanTilesMax words, a COMPILE-TIME size, so it is a persistent LSTEvent
  // member allocated once -- not a per-event device buffer (see the rdHashOwner_ note in LSTEvent.h
  // for why that distinction is worth 26 ms on the CPU backend). The MD and LS instances share it
  // because their launches are queue-ordered.
  // JET ROUND, phase 2: THE 64-BIT RECOUNT that keeps the allocation guard honest. `nEdgesExact` is
  // a uint32 sum of uint32 products, so at 2^32 edges it WRAPS SILENTLY (FINDINGS_JET.md ceiling 3)
  // and a guard fed the wrapped value would wave a corrupting event through. The certificate that
  // the uint32 count is exact is `min(nT3, C) * nT3 < 2^32` -- because sum_k degIn*degOut <=
  // (sum_k degIn) * max_k degOut <= nT3 * min(nT3, C) -- and the HOST evaluates it for free. It
  // holds for every PU200 event and for every capped event, so this phase does not run in the
  // shipping configuration at all; it exists for the cap-off jet arm, which is exactly the arm whose
  // count cannot be trusted.
  //
  // It rides in this kernel rather than in a new one so the ptxas entry count does not move (`phase`
  // is already an argument, so the argument PACK is unchanged too, which is the thing alpaka forks
  // on). Worker 0 of each block walks its whole tile serially and writes one uint64 as lo/hi into
  // lanes 0 and 1 of the scratch, which the host sums: no cross-worker 64-bit reduction, no atomic,
  // no new buffer. The other workers return immediately, and no barrier is executed on this path, so
  // the divergence is safe.
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
      auto& scratch =
          alpaka::declareSharedVar<uint32_t[2u * kLanes * kChainScanTileThreads], __COUNTER__>(acc);

      uint32_t const nWorkers = chainScanWorkerCount(acc);
      uint32_t const worker = chainScanWorkerIndex(acc);
      uint32_t const tile = static_cast<uint32_t>(alpaka::getIdx<alpaka::Grid, alpaka::Blocks>(acc)[0u]);
      ALPAKA_ASSERT_ACC(nWorkers <= kChainScanTileThreads);
      ALPAKA_ASSERT_ACC(nTiles >= 1u && nTiles <= kChainScanTilesMax);
      ALPAKA_ASSERT_ACC(tile < nTiles);

      uint32_t const span = (nKeys + nTiles - 1u) / nTiles;
      uint32_t const begin = (tile * span < nKeys) ? tile * span : nKeys;
      uint32_t const end = (begin + span < nKeys) ? begin + span : nKeys;

      if (phase == 2u) {
        if (worker != 0u)
          return;
        uint64_t sum = 0;
        for (uint32_t k = begin; k < end; ++k) {
          uint64_t const degOut = chainCappedDegree(incidence.t3OutCounts()[k], degCap);
          uint64_t const degIn = chainCappedDegree(incidence.t3InCounts()[k], degCap);
          sum += degIn * degOut;
        }
        tileSums[tile] = static_cast<uint32_t>(sum & 0xffffffffu);
        tileSums[kChainScanTilesMax + tile] = static_cast<uint32_t>(sum >> 32);
        return;
      }

      if (phase == 0u) {
        uint32_t local[kLanes] = {0u, 0u, 0u};
        for (uint32_t k = begin + worker; k < end; k += nWorkers) {
          uint32_t const degOut = incidence.t3OutCounts()[k];
          uint32_t const degIn = incidence.t3InCounts()[k];
          local[0] += degOut;
          local[1] += degIn;
          local[2] += chainCappedDegree(degIn, degCap) * chainCappedDegree(degOut, degCap);
        }
        uint32_t base[kLanes], total[kLanes];
        chainScanBlockExclusive<kLanes, kChainScanTileThreads>(
            acc, &scratch[0], nWorkers, worker, local, base, total);
        if (cms::alpakatools::once_per_block(acc))
          for (uint32_t l = 0; l < kLanes; ++l)
            tileSums[l * kChainScanTilesMax + tile] = total[l];
        return;
      }

      // This tile's base, and the grand totals. At the default nTiles = 128 this walk is an eighth
      // of the 1024-iteration one it replaces, and it is over words every block reads from the same
      // three cache lines -- it is not worth a launch of its own, and keeping it here is what holds
      // the kernel count. It DOES grow with nTiles, which is one of the things the sweep measures.
      uint32_t running[kLanes] = {0u, 0u, 0u};
      uint32_t grand[kLanes] = {0u, 0u, 0u};
      for (uint32_t t = 0; t < nTiles; ++t)
        for (uint32_t l = 0; l < kLanes; ++l) {
          uint32_t const s = tileSums[l * kChainScanTilesMax + t];
          if (t < tile)
            running[l] += s;
          grand[l] += s;
        }

      // The tile itself, one chunk of blockThreads keys at a time. `begin` and `end` are block
      // uniform, so every worker executes every iteration and therefore every barrier inside the
      // scan -- no divergent barrier is possible here.
      for (uint32_t chunk = begin; chunk < end; chunk += nWorkers) {
        uint32_t const k = chunk + worker;
        bool const live = k < end;
        uint32_t local[kLanes] = {0u, 0u, 0u};
        if (live) {
          uint32_t const degOut = incidence.t3OutCounts()[k];
          uint32_t const degIn = incidence.t3InCounts()[k];
          local[0] = degOut;
          local[1] = degIn;
          local[2] = chainCappedDegree(degIn, degCap) * chainCappedDegree(degOut, degCap);
        }
        uint32_t base[kLanes], total[kLanes];
        chainScanBlockExclusive<kLanes, kChainScanTileThreads>(
            acc, &scratch[0], nWorkers, worker, local, base, total);
        if (live) {
          incidence.t3OutOffsets()[k] = running[0] + base[0];
          incidence.t3InOffsets()[k] = running[1] + base[1];
          incidence.edgeProdPrefix()[k] = running[2] + base[2];
        }
        for (uint32_t l = 0; l < kLanes; ++l)
          running[l] += total[l];
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
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainNodeStableId(MiniDoubletsConst mds,
                                                            uint32_t md0,
                                                            uint32_t md1,
                                                            uint32_t md2) {
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
                                  ChainIncidence lsIncidence,
                                  uint32_t const* mdKeyBias,
                                  uint32_t const* lsKeyBias) const {
      uint32_t const nNodes = static_cast<uint32_t>(nodes.metadata().size());

      for (uint32_t node : cms::alpakatools::uniform_elements(acc, nNodes)) {
        uint32_t const t3 = nodes.tripletIndex()[node];
        uint32_t const innerSegmentIndex = triplets.segmentIndices()[t3][0];
        uint32_t const outerSegmentIndex = triplets.segmentIndices()[t3][1];
        uint32_t const firstMDIndex = segments.mdIndices()[innerSegmentIndex][0];
        uint32_t const lastMDIndex = segments.mdIndices()[outerSegmentIndex][1];
        uint32_t const midMDIndex = segments.mdIndices()[innerSegmentIndex][1];

        nodes.stableId()[node] = chainNodeStableId(mds, firstMDIndex, midMDIndex, lastMDIndex);

        // Raw -> dense incidence keys (ChainPrefixKeyModules). The triplet's three lower modules
        // own, in order: its first MD and its inner Segment (layer 0), its outer Segment
        // (layer 1), and its last MD (layer 2).
        uint32_t const m0 = triplets.lowerModuleIndices()[t3][0];
        uint32_t const m1 = triplets.lowerModuleIndices()[t3][1];
        uint32_t const m2 = triplets.lowerModuleIndices()[t3][2];
        uint32_t const firstMDKey = firstMDIndex + mdKeyBias[m0];
        uint32_t const lastMDKey = lastMDIndex + mdKeyBias[m2];
        uint32_t const innerLSKey = innerSegmentIndex + lsKeyBias[m0];
        uint32_t const outerLSKey = outerSegmentIndex + lsKeyBias[m1];

        // The "in" side keys are re-read once per incident edge by K5 and K7a; keep them.
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
