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

// Cross-cleaning of pixel line segments (pLS) against the chain pipeline's own deliveries: a bare
// pixel seed that is really part of a track we already delivered must not also be emitted as a
// standalone candidate. It is the chain-pipeline counterpart of LST's CrossCleanpLS.
//
// One output, xcRetired[pLS row], consumed only by the final carried-row retirement. Two arms:
//
//   PIXEL-ANCHORED arm: retire a bare quad seed that shares >= 1 pixel hit row with the seed of ANY
//   delivery. The anchor set is exactly plsOwned != 0 -- every pixel-anchored track candidate of
//   the chain pipeline is one of our own deliveries, so LST's per-type dispatch collapses to that
//   one test. LST's second test, a direction match between the two seeds, is not reproduced: it is
//   measured inert here (it fired on 5.7 of the arm's 2987.7 seeds per event and moved no metric at
//   5 decimals over 977 events) and it costs a linear scan over every anchor per candidate row.
//
//   BARE-CHAIN arm: the substitution for LST's pLS/T5 embedding test. It is resolved from the
//   filtered pair list the attach scorers append (pairs whose attach-head logit passes the |seed
//   eta|-banded xcTheta; there is no geometric window, see ChainAttach.h). Pass 2 keeps a pair only
//   when the chain actually EMITTED a seedless candidate (tcRow >= 0, which already excludes the
//   too-short and the out-of-rows, and attachPls < 0) and the seed is still unowned. The two passes
//   together mean exactly "retire the seed iff its MAX pair logit over DELIVERED seedless chain
//   candidates reaches the band bar".
//
// Candidacy is restricted to the carried pLS rows that survived earlier stages: every other seed's
// verdict would be inert, at roughly 70x the work.
//
// Ordering: after the stage-B contention sweep, whose revoke un-anchors seeds and so creates
// candidates, and before the carried-row suppression. No candidate-vs-candidate loop anywhere,
// which is what keeps both arms one pass over their own rows.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  namespace chainxc {
    // Open-addressed presence set of the anchors' pixel hit indices, in the global hit numbering
    // (hitsBase.idxs()). Anchors are a few hundred seeds x <= 4 hits, so 16k slots never exceed a
    // quarter load. Power of two, so the probe wraps with a mask.
    constexpr uint32_t kHitHashSlots = 16384u;
    constexpr uint32_t kHitHashEmpty = 0xFFFFFFFFu;
    // Probe-length stop for the concurrent insert below. It stands in for a running load-factor
    // test: at a quarter load the expected probe is ~1.3 slots, so it can only fire if the table is
    // far outside its design point, and when it does the loss is counted rather than silent.
    constexpr uint32_t kHitHashMaxProbe = 64u;
    ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t hitHash(uint32_t hitKey) {
      uint32_t hashValue = hitKey * 2654435761u;
      hashValue ^= hashValue >> 15;
      return hashValue & (kHitHashSlots - 1u);
    }
  }  // namespace chainxc

  // Compact the anchors into a dense list: every pLS row a delivery owns.
  struct ChainXcAnchorList {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  PixelSeedsConst pixelSeeds,
                                  uint8_t const* plsOwned,
                                  uint32_t nPls,
                                  uint32_t* anchorPls,
                                  uint32_t* nAnchors) const {
      for (uint32_t plsIdx : cms::alpakatools::uniform_elements(acc, nPls)) {
        if (plsOwned[plsIdx] == 0u)
          continue;
        // The list order is whatever the atomic hands out; only its CONTENTS are read, by the
        // insert kernel below, and that insert builds a set.
        uint32_t const slot = alpaka::atomicAdd(acc, nAnchors, 1u, alpaka::hierarchy::Blocks{});
        anchorPls[slot] = plsIdx;
      }
    }
  };

  // Concurrent insert of every anchor's pixel hit indices into the presence set, one thread per
  // anchor on every backend. The table is a SET WITH NO DELETIONS, so any interleaving of
  // linear-probe CAS inserts leaves the same MEMBERSHIP: a probe either claims an empty slot for
  // its key or discovers that key already in the table. Only the slot LAYOUT can differ between
  // interleavings, and no reader ever observes it -- the lookups probe to the first empty slot in a
  // later launch, after every insert has retired -- so the result is reproducible.
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
      uint32_t const nAnchor = *nAnchors;
      // Local copy: atomicCas takes its compare value by reference, and a constexpr in namespace
      // scope has no device-side storage to bind to.
      uint32_t const empty = chainxc::kHitHashEmpty;
      for (uint32_t anchorIdx : cms::alpakatools::uniform_elements(acc, nAnchorBound)) {
        if (anchorIdx >= nAnchor)
          continue;
        uint32_t const plsIdx = anchorPls[anchorIdx];
        uint32_t const firstHit = pixelSeeds.firstHit()[plsIdx];
        uint32_t const nSeedHits = static_cast<uint32_t>(pixelSeeds.nHits()[plsIdx]);
        uint32_t const nStored = nSeedHits < kMaxPLSHitsInHitsSoA ? nSeedHits : kMaxPLSHitsInHitsSoA;
        for (uint32_t k = 0; k < nStored; ++k) {
          uint32_t const hitIdx = firstHit + k;
          if (hitIdx >= nHits)
            continue;
          if (hitsBase.detid()[hitIdx] != kPixelModuleId)
            continue;
          uint32_t const hitKey = hitsBase.idxs()[hitIdx];
          uint32_t slot = chainxc::hitHash(hitKey);
          for (uint32_t probe = 0; probe < chainxc::kHitHashMaxProbe; ++probe) {
            uint32_t const prev = alpaka::atomicCas(acc, &hashKey[slot], empty, hitKey, alpaka::hierarchy::Blocks{});
            if (prev == empty || prev == hitKey)
              break;  // claimed the slot, or this key is already present
            slot = (slot + 1u) & (chainxc::kHitHashSlots - 1u);
            if (probe + 1u == chainxc::kHitHashMaxProbe)
              alpaka::atomicAdd(acc, &stats[0], 1u, alpaka::hierarchy::Blocks{});  // never at PU200
          }
        }
      }
    }
  };

  // The pixel-anchored arm, one thread per CARRIED candidate row: retire the row's seed if it
  // shares any pixel hit with an anchor seed (>= 1 of <= 4, the same rule as LST's
  // pixelHitsOverlapAny), which is one hash lookup per hit and nothing else.
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
      uint32_t const nCandidates = candsBase.nTrackCandidates();
      uint32_t const nChainRows = chains.nChainTCs();
      // The chain rows are APPENDED after the carried ones, so everything below this boundary is a
      // carried row and everything at or above it is one of ours.
      uint32_t const boundary = (nChainRows <= nCandidates) ? (nCandidates - nChainRows) : 0u;
      for (uint32_t rowIdx : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (rowIdx >= boundary)
          continue;
        if (candsBase.trackCandidateType()[rowIdx] != LSTObjType::pLS)
          continue;
        uint32_t const plsIdx = candsExtended.directObjectIndices()[rowIdx];
        if (plsIdx >= nPls)
          continue;
        if (plsOwned[plsIdx] != 0u)
          continue;  // an anchor is never a candidate; the plsOwned term retires its row anyway
        if (!pixelSeeds.isQuad()[plsIdx])
          continue;

        bool retired = false;
        uint32_t const firstHit = pixelSeeds.firstHit()[plsIdx];
        uint32_t const nSeedHits = static_cast<uint32_t>(pixelSeeds.nHits()[plsIdx]);
        uint32_t const nStored = nSeedHits < kMaxPLSHitsInHitsSoA ? nSeedHits : kMaxPLSHitsInHitsSoA;
        for (uint32_t k = 0; k < nStored && !retired; ++k) {
          uint32_t const hitIdx = firstHit + k;
          if (hitIdx >= nHits)
            continue;
          if (hitsBase.detid()[hitIdx] != kPixelModuleId)
            continue;
          uint32_t const hitKey = hitsBase.idxs()[hitIdx];
          uint32_t slot = chainxc::hitHash(hitKey);
          // Every insert has retired by now, so probing to the first empty slot is a complete
          // lookup: a present key is always found before one.
          while (hashKey[slot] != chainxc::kHitHashEmpty) {
            if (hashKey[slot] == hitKey) {
              retired = true;
              break;
            }
            slot = (slot + 1u) & (chainxc::kHitHashSlots - 1u);
          }
        }
        if (retired) {
          xcRetired[plsIdx] = 1u;
          alpaka::atomicAdd(acc, &stats[1], 1u, alpaka::hierarchy::Blocks{});
        }
      }
    }
  };

  // The bare-chain arm, pass 2: resolve the pairs the attach scorers filtered against what the
  // chain actually delivered. A pair only means anything once its chain is known to have been
  // emitted seedless -- the scorers run before that is decided.
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
      for (uint32_t pairIdx : cms::alpakatools::uniform_elements(acc, nPairs)) {
        uint32_t const chainIdx = xcPairs[pairIdx].chain;
        uint32_t const plsIdx = xcPairs[pairIdx].plsRow;
        if (plsIdx >= nPls)
          continue;
        if (chains.tcRow()[chainIdx] < 0)
          continue;  // not emitted: lost the claim, too few layers, or out of rows
        if (chains.attachPls()[chainIdx] >= 0)
          continue;  // seeded chain: the pixel-anchored arm's business
        if (plsOwned[plsIdx] != 0u)
          continue;  // the seed became an anchor; never a candidate
        if (xcRetired[plsIdx] == 0u) {
          xcRetired[plsIdx] = 1u;
          alpaka::atomicAdd(acc, &stats[3], 1u, alpaka::hierarchy::Blocks{});
        }
      }
    }
  };

  // The 4-layer bare class cross-cleaned against the DELIVERED SEEDED rows. A delivered 4-layer
  // bare chain sharing >= config.cc9MinShared outer-tracker hits -- 2 hits is one full mini-doublet --
  // with a delivered seeded row is a fragment of that seeded track rather than a track of its own:
  // 98.4% of the barrel rows in this cell are truth-fake, and 0.002 per event are the sole cover of
  // any simulated track. It is the missing arm of LST's own CrossCleanT5, which went away with the
  // T5 pipeline; the 4-layer bare class never had one.
  //
  // Safe by construction in both directions. The reference row is always a SEEDED row, a class this
  // rule cannot delete, so it can never cascade. And both kernels run on the KEEP array, i.e. on
  // exactly the rows the compaction is about to emit, and only ever clear a bare 4-layer keep bit,
  // so a seeded row's survival never depends on this rule -- no ordering hazard, no second
  // compaction.
  namespace chaincc9 {
    // Open-addressed MULTIMAP of (OT hit row -> owning seeded row). Every (hit, owner) pair claims
    // its own slot rather than merging, so a hit belonging to two seeded rows is found under both,
    // and a lookup scans from the hash to the first empty slot. Seeded rows carry <= 22 OT hits and
    // number a few hundred per event, so 32k slots stay under a fifth load.
    constexpr uint32_t kSlots = 32768u;
    constexpr uint32_t kEmpty = 0xFFFFFFFFu;
    // Distinct seeded partners one bare row is scored against. Its outer-tracker hits number <= 22
    // and the measured sharing is a single partner, so 8 is far above the design point; an overflow
    // is counted rather than silently changing a verdict.
    constexpr int kMaxPartners = 8;
    ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t hash(uint32_t hitKey) {
      uint32_t hashValue = hitKey * 2654435761u;
      hashValue ^= hashValue >> 15;
      return hashValue & (kSlots - 1u);
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
      uint32_t const nCandidates = candsBase.nTrackCandidates();
      uint32_t const empty = chaincc9::kEmpty;
      for (uint32_t rowIdx : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (rowIdx >= nCandidates || keep[rowIdx] == 0u)
          continue;
        LSTObjType const candidateType = candsBase.trackCandidateType()[rowIdx];
        if (candidateType != LSTObjType::pT5 && candidateType != LSTObjType::pT3)
          continue;
        for (int layer = 0; layer < Params_TC::kLayers; ++layer) {
          if (candsExtended.logicalLayers()[rowIdx][layer] == 0)
            continue;  // a pixel slot carries no outer-tracker hit
          for (int k = 0; k < Params_TC::kHitsPerLayer; ++k) {
            uint32_t const hitIdx = candsBase.hitIndices()[rowIdx][layer][k];
            if (hitIdx == kTCEmptyHitIdx)
              continue;
            uint32_t slot = chaincc9::hash(hitIdx);
            for (uint32_t probe = 0; probe < chaincc9::kSlots; ++probe) {
              uint32_t const prev = alpaka::atomicCas(acc, &hashKey[slot], empty, hitIdx, alpaka::hierarchy::Blocks{});
              if (prev == empty) {
                hashVal[slot] = rowIdx;  // the slot is ours; no reader runs before pass 2
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

  // Pass 2: drop a bare 4-layer row sharing >= cc9MinShared outer-tracker hits with ONE seeded row.
  // The count is per PARTNER, not a flat hit tally -- two hits shared with two different seeded
  // rows is not a shared mini-doublet and must not delete anything.
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
                                  ChainConfig config) const {
      if (config.cc9MinShared <= 0)
        return;
      uint32_t const nCandidates = candsBase.nTrackCandidates();
      for (uint32_t rowIdx : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (rowIdx >= nCandidates || keep[rowIdx] == 0u)
          continue;
        if (candsBase.trackCandidateType()[rowIdx] != LSTObjType::T4)
          continue;
        uint32_t partnerRows[chaincc9::kMaxPartners];
        int sharedCounts[chaincc9::kMaxPartners];
        int nPartners = 0;
        bool drop = false;
        for (int layer = 0; layer < Params_TC::kLayers && !drop; ++layer) {
          if (candsExtended.logicalLayers()[rowIdx][layer] == 0)
            continue;
          for (int k = 0; k < Params_TC::kHitsPerLayer && !drop; ++k) {
            uint32_t const hitIdx = candsBase.hitIndices()[rowIdx][layer][k];
            if (hitIdx == kTCEmptyHitIdx)
              continue;
            uint32_t slot = chaincc9::hash(hitIdx);
            // Bounded like the insert side: a table with no empty slot would otherwise spin here.
            for (uint32_t probe = 0; probe < chaincc9::kSlots && hashKey[slot] != chaincc9::kEmpty && !drop; ++probe) {
              if (hashKey[slot] == hitIdx) {
                uint32_t const owner = hashVal[slot];
                int partnerIdx = -1;
                for (int existingIdx = 0; existingIdx < nPartners; ++existingIdx)
                  if (partnerRows[existingIdx] == owner) {
                    partnerIdx = existingIdx;
                    break;
                  }
                if (partnerIdx < 0) {
                  if (nPartners < chaincc9::kMaxPartners) {
                    partnerIdx = nPartners++;
                    partnerRows[partnerIdx] = owner;
                    sharedCounts[partnerIdx] = 0;
                  } else {
                    alpaka::atomicAdd(acc, &stats[1], 1u, alpaka::hierarchy::Blocks{});  // never at PU200
                  }
                }
                if (partnerIdx >= 0 && ++sharedCounts[partnerIdx] >= config.cc9MinShared)
                  drop = true;
              }
              slot = (slot + 1u) & (chaincc9::kSlots - 1u);
            }
          }
        }
        if (drop) {
          keep[rowIdx] = 0u;
          // Two counters describe the emitted collection and BOTH must follow a cleared keep bit.
          //   classCounts[4]: ChainTCKeepSuppress already tallied this row as T4, and
          //     ChainTCFinishSuppress copies the tallies into the collection's per-class counters.
          //   chains.nChainTCs(): the chain rows are APPENDED after the carried ones, and every
          //     consumer identifies them as the LAST nChainTCs rows rather than by type. This is
          //     the first mechanism in the pipeline to drop a CHAIN row (ChainTCKeepSuppress only
          //     ever drops carried ones), so without this decrement the boundary slides and a
          //     carried row is parsed as a chain.
          alpaka::atomicSub(acc, &classCounts[4], 1u, alpaka::hierarchy::Blocks{});
          alpaka::atomicSub(acc, &chains.nChainTCs(), 1u, alpaka::hierarchy::Blocks{});
          alpaka::atomicAdd(acc, &stats[2], 1u, alpaka::hierarchy::Blocks{});
        }
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
