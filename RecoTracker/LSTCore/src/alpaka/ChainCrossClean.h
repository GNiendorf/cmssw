#ifndef RecoTracker_LSTCore_src_alpaka_ChainCrossClean_h
#define RecoTracker_LSTCore_src_alpaka_ChainCrossClean_h

#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainConfig.h"
#include "RecoTracker/LSTCore/interface/ChainsSoA.h"
#include "RecoTracker/LSTCore/interface/LSTInputSoA.h"
#include "RecoTracker/LSTCore/interface/TrackCandidatesSoA.h"

#include "ChainAttach.h"

// =====================================================================================
// A15 -XC: the ported CrossCleanpLS (SPEC_XC). One output: xcRetired[pLS row], consumed only by
// the final carried-row retirement (the type-8 channel). Two arms:
//
//   PIXEL-ANCHORED arm (LST's pT5 + pT3 arms fused): retire a bare quad seed that shares >= 1 pixel
//   hit row with the seed of ANY delivery. The anchor set is exactly plsOwned != 0 -- every
//   pixel-anchored TC of the chain pipeline is one of our own deliveries, so the per-type dispatch
//   of LST's kernel collapses. LST's SECOND test here, dR^2 < 1e-6 between the two SEEDS, is
//   DELETED: measured inert (it fired on 5.7 of the arm's 2987.7 seeds/evt and moved no metric at
//   5 decimals, 977 evt), and it cost a linear scan over every anchor per candidate row.
//
//   BARE-CHAIN arm (LST's T5 arm with the working-point substitution): resolved from the pass-1
//   filtered pair compaction the attach scorers appended (threshold on the |seed eta|-banded
//   xcTheta, no geometric window -- LST's dR^2 < 0.02 centroid window was dropped, see
//   ChainAttach.h pass 1). Pass 2 keeps a pair only when the chain actually EMITTED a seedless TC
//   (tcRow >= 0 -- which already excludes the too-short and the out-of-rows -- and
//   attachPls < 0) and the seed is still unowned. The two passes together are exactly "retire the
//   seed iff its MAX pair logit over DELIVERED seedless chain TCs reaches the band bar".
//
// Candidacy is restricted to the carried type-8 rows (the admitted set), the SPEC_XC 2.4
// restriction: every other seed's verdict would be inert, at ~70x the work.
//
// Ordering: after the -CC sweep (its -CCR 2 release un-anchors seeds and makes them candidates),
// before ChainSuppressCarriedTCs. No candidate-vs-candidate loop anywhere.
// =====================================================================================

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  namespace chainxc {
    // Open-addressed presence set of the anchors' pixel hit indices (tracking-ntuple numbering,
    // hitsBase.idxs(), the same key space the -RD dedup table uses). Anchors are a few hundred
    // seeds x <= 4 hits, so 16k slots never exceed a quarter load. Power of two: masked probe.
    constexpr uint32_t kHitHashSlots = 16384u;
    constexpr uint32_t kHitHashEmpty = 0xFFFFFFFFu;
    // Probe-length stop for the concurrent insert below. It stands in for the serial form's running
    // load-factor test: at a quarter load the expected probe is ~1.3 slots, so it can only fire if
    // the table is far outside its design point, and it is counted rather than silently ignored.
    constexpr uint32_t kHitHashMaxProbe = 64u;
    ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t hitHash(uint32_t key) {
      uint32_t h = key * 2654435761u;
      h ^= h >> 15;
      return h & (kHitHashSlots - 1u);
    }
  }  // namespace chainxc

  // The compact anchor list: every pLS the deliveries own.
  struct ChainXcAnchorList {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  PixelSeedsConst pixelSeeds,
                                  uint8_t const* plsOwned,
                                  uint32_t nPls,
                                  uint32_t* anchorPls,
                                  uint32_t* nAnchors) const {
      for (uint32_t p : cms::alpakatools::uniform_elements(acc, nPls)) {
        if (plsOwned[p] == 0u)
          continue;
        uint32_t const slot = alpaka::atomicAdd(acc, nAnchors, 1u, alpaka::hierarchy::Blocks{});
        anchorPls[slot] = p;
      }
    }
  };

  // Concurrent insert of every anchor's pixel hit indices into the presence set, one thread per
  // anchor on every backend. The table is a SET WITH NO DELETIONS, so any interleaving of
  // linear-probe CAS inserts leaves the same MEMBERSHIP: a probe either claims an empty slot for its
  // key or discovers that key already in the table. Only the slot LAYOUT can differ from a serial
  // insert, and no reader ever observes it -- the lookups probe to the first empty slot in a later
  // launch, after every insert has retired -- so the outputs are bit-identical. The serial form this
  // replaces was a single device thread chasing a few thousand dependent global accesses.
  struct ChainXcAnchorHits {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  PixelSeedsConst pixelSeeds,
                                  HitsBaseConst hitsBase,
                                  uint32_t const* anchorPls,
                                  uint32_t const* nAnchors,
                                  uint32_t nAnchorBound,
                                  uint32_t nHits,
                                  uint32_t* hashKey,
                                  uint32_t* stats) const {
      uint32_t const n = *nAnchors;
      // Local copy: atomicCas takes its compare value by reference, and a constexpr in namespace
      // scope has no device-side storage to bind to.
      uint32_t const empty = chainxc::kHitHashEmpty;
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, nAnchorBound)) {
        if (i >= n)
          continue;
        uint32_t const p = anchorPls[i];
        uint32_t const first = pixelSeeds.firstHit()[p];
        uint32_t const nSeedHits = static_cast<uint32_t>(pixelSeeds.nHits()[p]);
        uint32_t const nStored = nSeedHits < kMaxPLSHitsInHitsSoA ? nSeedHits : kMaxPLSHitsInHitsSoA;
        for (uint32_t k = 0; k < nStored; ++k) {
          uint32_t const h = first + k;
          if (h >= nHits)
            continue;
          if (hitsBase.detid()[h] != kPixelModuleId)
            continue;
          uint32_t const g = hitsBase.idxs()[h];
          uint32_t slot = chainxc::hitHash(g);
          for (uint32_t probe = 0; probe < chainxc::kHitHashMaxProbe; ++probe) {
            uint32_t const prev = alpaka::atomicCas(acc, &hashKey[slot], empty, g, alpaka::hierarchy::Blocks{});
            if (prev == empty || prev == g)
              break;  // claimed the slot, or this key is already present
            slot = (slot + 1u) & (chainxc::kHitHashSlots - 1u);
            if (probe + 1u == chainxc::kHitHashMaxProbe)
              alpaka::atomicAdd(acc, &stats[0], 1u, alpaka::hierarchy::Blocks{});  // never at PU200
          }
        }
      }
    }
  };

  // The pixel-anchored arm, one thread per CARRIED TC row: shared pixel hit row with any anchor
  // seed (>= 1 of <= 4 -- LST pixelHitsOverlapAny), a hash lookup per hit and nothing else.
  struct ChainXcPixelArm {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  PixelSeedsConst pixelSeeds,
                                  HitsBaseConst hitsBase,
                                  TrackCandidatesBaseConst candsBase,
                                  TrackCandidatesExtendedConst candsExtended,
                                  ChainsConst chains,
                                  uint8_t const* plsOwned,
                                  uint32_t const* hashKey,
                                  uint32_t const* anchorPls,
                                  uint32_t nPls,
                                  uint32_t nHits,
                                  uint32_t nBound,
                                  uint8_t* xcRetired,
                                  uint32_t* stats) const {
      uint32_t const nIn = candsBase.nTrackCandidates();
      uint32_t const nChainRows = chains.nChainTCs();
      uint32_t const boundary = (nChainRows <= nIn) ? (nIn - nChainRows) : 0u;
      for (uint32_t r : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (r >= boundary)
          continue;
        if (candsBase.trackCandidateType()[r] != LSTObjType::pLS)
          continue;
        uint32_t const p = candsExtended.directObjectIndices()[r];
        if (p >= nPls)
          continue;
        if (plsOwned[p] != 0u)
          continue;  // an anchor is never a candidate; the plsOwned term retires its row anyway
        if (!pixelSeeds.isQuad()[p])
          continue;

        // Test 1: >= 1 shared pixel hit row with any anchor seed.
        bool retired = false;
        uint32_t const first = pixelSeeds.firstHit()[p];
        uint32_t const nSeedHits = static_cast<uint32_t>(pixelSeeds.nHits()[p]);
        uint32_t const nStored = nSeedHits < kMaxPLSHitsInHitsSoA ? nSeedHits : kMaxPLSHitsInHitsSoA;
        for (uint32_t k = 0; k < nStored && !retired; ++k) {
          uint32_t const h = first + k;
          if (h >= nHits)
            continue;
          if (hitsBase.detid()[h] != kPixelModuleId)
            continue;
          uint32_t const g = hitsBase.idxs()[h];
          uint32_t slot = chainxc::hitHash(g);
          while (hashKey[slot] != chainxc::kHitHashEmpty) {
            if (hashKey[slot] == g) {
              retired = true;
              break;
            }
            slot = (slot + 1u) & (chainxc::kHitHashSlots - 1u);
          }
        }
        if (retired) {
          xcRetired[p] = 1u;
          alpaka::atomicAdd(acc, &stats[1], 1u, alpaka::hierarchy::Blocks{});
        }
      }
    }
  };

  // The bare-chain arm, pass 2: resolve the pass-1 candidates against the delivery outcome.
  struct ChainXcChainArm {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint8_t const* plsOwned,
                                  ChainXcPair const* xcPairs,
                                  uint32_t const* xcCursor,
                                  uint32_t xcCap,
                                  uint32_t nPls,
                                  uint8_t* xcRetired,
                                  uint32_t* stats) const {
      uint32_t nPairs = *xcCursor;
      if (nPairs > xcCap)
        nPairs = xcCap;
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, nPairs)) {
        uint32_t const c = xcPairs[i].chain;
        uint32_t const p = xcPairs[i].pls;
        if (p >= nPls)
          continue;
        if (chains.tcRow()[c] < 0)
          continue;  // not emitted (K9-rejected, < 4 layers, out of rows)
        if (chains.attachPls()[c] >= 0)
          continue;  // seeded (type 7): the pixel-anchored arm's business
        if (plsOwned[p] != 0u)
          continue;  // the seed became an anchor; never a candidate
        if (xcRetired[p] == 0u) {
          xcRetired[p] = 1u;
          alpaka::atomicAdd(acc, &stats[3], 1u, alpaka::hierarchy::Blocks{});
        }
      }
    }
  };

  // =====================================================================================
  // -CC9: the T4-class crossclean against the DELIVERED SEEDED rows. A delivered T4-class bare
  // chain (type 9) sharing >= cc9MinShared outer-tracker hits -- one full mini-doublet -- with a
  // delivered SEEDED row (type 7 pT5-class or type 5 pT3-class) is a fragment of that seeded track
  // rather than a track of its own: 98.4% of the barrel rows in this cell are truth-fake and 0.002
  // per event are the sole cover of any sim. It is the missing arm of LST's own CrossCleanT5
  // (TrackCandidate.h:207), which went away with the T5 pipeline -- the 4-layer bare class never
  // had one. Set-safe BY CONSTRUCTION: the reference row is a class this rule cannot delete.
  //
  // Both kernels run on the K8d KEEP array, i.e. on exactly the rows the compaction is about to
  // emit, and only ever clear a type-9 keep bit. A seeded row's survival never depends on this
  // rule, so there is no ordering hazard and no second compaction.
  // =====================================================================================
  namespace chaincc9 {
    // Open-addressed MULTIMAP of (OT hit row -> owning seeded row). Every (hit, owner) pair claims
    // its own slot rather than merging, so a hit belonging to two seeded rows is found under both,
    // and a lookup scans from the hash to the first empty slot. Seeded rows carry <= 22 OT hits and
    // number a few hundred per event, so 32k slots stay under a fifth load.
    constexpr uint32_t kSlots = 32768u;
    constexpr uint32_t kEmpty = 0xFFFFFFFFu;
    // Distinct seeded partners one type-9 row is scored against. Its OT hits number <= 22 and the
    // measured sharing is a single partner, so 8 is far above the design point; an overflow is
    // counted rather than silently changing a verdict.
    constexpr int kMaxPartners = 8;
    ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t hash(uint32_t key) {
      uint32_t h = key * 2654435761u;
      h ^= h >> 15;
      return h & (kSlots - 1u);
    }
  }  // namespace chaincc9

  // Pass 1: every surviving SEEDED row publishes its outer-tracker hit rows.
  struct ChainCc9Publish {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBaseConst candsBase,
                                  TrackCandidatesExtendedConst candsExtended,
                                  uint32_t const* keep,
                                  uint32_t nBound,
                                  uint32_t* hashKey,
                                  uint32_t* hashVal,
                                  uint32_t* stats) const {
      uint32_t const nIn = candsBase.nTrackCandidates();
      uint32_t const empty = chaincc9::kEmpty;
      for (uint32_t r : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (r >= nIn || keep[r] == 0u)
          continue;
        LSTObjType const ty = candsBase.trackCandidateType()[r];
        if (ty != LSTObjType::pT5 && ty != LSTObjType::pT3)
          continue;
        for (int s = 0; s < Params_TC::kLayers; ++s) {
          if (candsExtended.logicalLayers()[r][s] == 0)
            continue;  // a pixel slot carries no outer-tracker hit
          for (int k = 0; k < Params_TC::kHitsPerLayer; ++k) {
            uint32_t const h = candsBase.hitIndices()[r][s][k];
            if (h == kTCEmptyHitIdx)
              continue;
            uint32_t slot = chaincc9::hash(h);
            for (uint32_t probe = 0; probe < chaincc9::kSlots; ++probe) {
              uint32_t const prev = alpaka::atomicCas(acc, &hashKey[slot], empty, h, alpaka::hierarchy::Blocks{});
              if (prev == empty) {
                hashVal[slot] = r;  // the slot is ours; no reader runs before pass 2
                break;
              }
              slot = (slot + 1u) & (chaincc9::kSlots - 1u);
              if (probe + 1u == chaincc9::kSlots)
                alpaka::atomicAdd(acc, &stats[0], 1u, alpaka::hierarchy::Blocks{});  // table full
            }
          }
        }
      }
    }
  };

  // Pass 2: drop a type-9 row sharing >= cc9MinShared OT hits with ONE seeded row. The count is per
  // PARTNER, not a flat hit tally -- two hits shared with two different seeded rows is not a shared
  // mini-doublet and must not delete anything.
  struct ChainCc9Apply {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBaseConst candsBase,
                                  TrackCandidatesExtendedConst candsExtended,
                                  Chains chains,
                                  uint32_t const* hashKey,
                                  uint32_t const* hashVal,
                                  uint32_t* keep,
                                  uint32_t* classCounts,
                                  uint32_t nBound,
                                  uint32_t* stats,
                                  ChainConfig cfg) const {
      if (cfg.cc9MinShared <= 0)
        return;
      uint32_t const nIn = candsBase.nTrackCandidates();
      for (uint32_t r : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (r >= nIn || keep[r] == 0u)
          continue;
        if (candsBase.trackCandidateType()[r] != LSTObjType::T4)
          continue;
        uint32_t partner[chaincc9::kMaxPartners];
        int count[chaincc9::kMaxPartners];
        int nPartners = 0;
        bool drop = false;
        for (int s = 0; s < Params_TC::kLayers && !drop; ++s) {
          if (candsExtended.logicalLayers()[r][s] == 0)
            continue;
          for (int k = 0; k < Params_TC::kHitsPerLayer && !drop; ++k) {
            uint32_t const h = candsBase.hitIndices()[r][s][k];
            if (h == kTCEmptyHitIdx)
              continue;
            uint32_t slot = chaincc9::hash(h);
            // Bounded like the insert side: a table with no empty slot would otherwise spin here.
            for (uint32_t probe = 0; probe < chaincc9::kSlots && hashKey[slot] != chaincc9::kEmpty && !drop;
                 ++probe) {
              if (hashKey[slot] == h) {
                uint32_t const owner = hashVal[slot];
                int idx = -1;
                for (int q = 0; q < nPartners; ++q)
                  if (partner[q] == owner) {
                    idx = q;
                    break;
                  }
                if (idx < 0) {
                  if (nPartners < chaincc9::kMaxPartners) {
                    idx = nPartners++;
                    partner[idx] = owner;
                    count[idx] = 0;
                  } else {
                    alpaka::atomicAdd(acc, &stats[1], 1u, alpaka::hierarchy::Blocks{});  // never at PU200
                  }
                }
                if (idx >= 0 && ++count[idx] >= cfg.cc9MinShared)
                  drop = true;
              }
              slot = (slot + 1u) & (chaincc9::kSlots - 1u);
            }
          }
        }
        if (drop) {
          keep[r] = 0u;
          // Two counters describe the emitted collection and BOTH must follow a cleared keep bit.
          //   classCounts[4]: ChainTCKeepSuppress already tallied this row as T4, and
          //     ChainTCFinishSuppress copies the tallies into the collection's per-class counters.
          //   chains.nChainTCs(): the chain rows are APPENDED after the carried ones, and every
          //     consumer -- including the ntuple writer's isChainTCRow -- identifies them as the
          //     LAST nChainTCs rows rather than by type. This is the first mechanism in the
          //     pipeline to drop a CHAIN row (ChainTCKeepSuppress only ever drops carried ones), so
          //     without this decrement the boundary slides and a carried row is parsed as a chain.
          alpaka::atomicSub(acc, &classCounts[4], 1u, alpaka::hierarchy::Blocks{});
          alpaka::atomicSub(acc, &chains.nChainTCs(), 1u, alpaka::hierarchy::Blocks{});
          alpaka::atomicAdd(acc, &stats[2], 1u, alpaka::hierarchy::Blocks{});
        }
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
