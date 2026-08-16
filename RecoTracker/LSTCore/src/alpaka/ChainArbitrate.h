#ifndef RecoTracker_LSTCore_src_alpaka_ChainArbitrate_h
#define RecoTracker_LSTCore_src_alpaka_ChainArbitrate_h

#include <numbers>
#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "HeterogeneousCore/AlpakaMath/interface/deltaPhi.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainConfig.h"
#include "RecoTracker/LSTCore/interface/ChainNodesSoA.h"
#include "RecoTracker/LSTCore/interface/ChainsSoA.h"
#include "RecoTracker/LSTCore/interface/LSTInputSoA.h"
#include "RecoTracker/LSTCore/interface/MiniDoubletsSoA.h"
#include "RecoTracker/LSTCore/interface/ModulesSoA.h"
#include "RecoTracker/LSTCore/interface/ObjectRangesSoA.h"
#include "RecoTracker/LSTCore/interface/SegmentsSoA.h"
#include "RecoTracker/LSTCore/interface/TrackCandidatesSoA.h"
#include "RecoTracker/LSTCore/interface/TripletsSoA.h"

#include "ChainEdges.h"
#include "ChainGate.h"
#include "ChainGraph.h"
#include "ChainWeld.h"

// Chain-tracking arbitration and assembly: the last two stages of the chain pipeline.
//
// Consumes the welded, trimmed and gate-scored chains (ChainsSoA + ChainItemsSoA) together with
// the track candidate rows the surviving upstream LST stages produced. Produces one
// TrackCandidatesBase row per chain that both wins its hits and is long enough to emit, and a
// compacted candidate collection in which the classes the chain pipeline replaces are gone.
//
// Kernels, in the order LSTEvent::arbitrateChains launches them:
//   ChainTCKeep* / Gather / Scatter / Finish  carried-row compaction, before and after the claim
//   ChainClaimPrep                            per-chain claim universe, order key, candidate mask
//   ChainCandScatter + ChainClaimRank*        the best-first order, as a rank instead of a sort
//   ChainPreClaimPixels + ChainClaimRounds    the greedy hit claim itself
//   ChainRowFlags / RowAssign / RowFinish     one output row per accepted, emittable chain
//   ChainEmitTCs                              fill those rows, including the pixel-seed upgrade
//
// THE CLAIM. Each chain owns a deduped list of hit rows -- its claim universe: both hits of every
// member mini-doublet. `owner[hitIdx]` names the one chain holding that hit, so hit ownership is
// EXCLUSIVE by design and a chain either takes its whole universe or takes none of it. Chains are
// considered best first under the order key
//     orderKey = score - alpha * max(0, hinge - marginX)
// which is the gate's chain score (already carrying the length term) reduced whenever the gate
// head's 3-class margin marginX falls short of the hinge. So the key balances "long and well
// scored" against "the head is confident this is a real track". It is a RANKING ONLY: whether a
// chain is a candidate at all is decided on chains.score against the per-branch bar, never on the
// key, so the two live on scales that are never compared with each other.
//
// A candidate is refused by either of two tests against what earlier claimants already hold:
//   * the count bar: too many of its hits are owned already (bandItems, or the bandFrac fraction).
//   * the BRAID test: it holds at least bandBraid of some SINGLE earlier owner's hits, i.e. it is
//     a near-copy of one chain rather than a track that happens to cross several.
// The braid loop is O(total^2) in the CHAIN'S OWN hit count -- a few dozen -- because it walks
// this chain's hits and reads the owner map at each. It is NOT a loop over pairs of chains; the
// owner map is exactly what buys the smaller bound.
//
// The claim, the row assignment and the two row compactions are parallel decompositions of a
// serial best-first walk. Each carries, at its own definition, the argument for why it reproduces
// that walk element for element, and one form runs on every backend.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  namespace chainarb {
    // Owner-map sentinels:
    //   kFree     no chain holds this hit
    //   >= 0      the accepted chain that holds it
    //   kPixOwner a carried pixel row holds it
    // Pixel owners are not distinguished from one another because they only ever contribute to the
    // claimed COUNT: the braid skips them, so they need no per-owner denominator.
    constexpr int32_t kFree = -1;
    constexpr int32_t kPixOwner = -2;
  }  // namespace chainarb

  // Diagnostic counters, written by the kernels below and read by no decision. Used slots:
  //   7  emission fell back to a non-native layer slot   8  emission ran out of slots on a row
  //   9  adjacent exact orderKey ties in the finished order
  //   11 claim rounds to convergence   12 peak undecided after a round   13 round cap hit
  static constexpr uint32_t kChainArbStats = 16u;

  // Everything the claim needs to know about a chain BEFORE the owner map exists. Four jobs in one
  // per-chain visit; each is elementwise and reads nothing another chain's thread writes:
  //
  //   (a) the claim universe: anchor hit + other hit of every member MD, sorted and deduped. The
  //       sort is an in-place insertion sort because a chain holds a few dozen hits at most. The
  //       ORDER inside the list never enters a decision -- the claim tests are counts over the
  //       whole list -- so only the SET matters.
  //   (b) the order key, score - alpha * max(0, hinge - marginX). A ranking, never a threshold.
  //   (c) the candidate mask, score >= the bar for this chain's branch. The IP branch admits every
  //       live chain (noCutTheta); a chain on the exempt (large-dcaXY) branch is cut per length by
  //       thetaExempt4/5/6 on the chain score scale. A gate-killed chain carries score -= gateKill
  //       and fails both bars.
  //   (d) the band decision and the three per-chain claim tolerances it selects. Hoisted out of
  //       the claim walk, which is legal because none of it depends on the owner map.
  struct ChainClaimPrep {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  MiniDoubletsConst miniDoublets,
                                  TripletsConst triplets,
                                  SegmentsConst segments,
                                  ChainNodesConst nodes,
                                  ChainItemsConst items,
                                  Chains chains,
                                  uint32_t* claimHits,
                                  uint32_t* candKeep,
                                  int32_t* bandItems,
                                  float* bandFrac,
                                  float* bandBraid,
                                  ChainConfig config) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());
      // The claim budgets are configured in MD units; the claim universe is hits, and every MD
      // contributes two of them, so the budget doubles here.
      int const maxItems = config.maxClaimedMDs >= 0 ? 2 * config.maxClaimedMDs : -1;
      int const maxItemsAlt = config.claimItemsAltMDs > -2 ? 2 * config.claimItemsAltMDs : -2;
      for (uint32_t chainIdx : cms::alpakatools::uniform_elements(acc, nChains)) {
        uint32_t const nodeBase = chains.nodeOffset()[chainIdx];
        uint32_t const mdBase = 3u * nodeBase;
        uint32_t const hitBase = 6u * nodeBase;
        int const nMDs = chains.nMDs()[chainIdx];

        // (a) the claim universe
        int nRaw = 0;
        for (int k = 0; k < nMDs; ++k) {
          uint32_t const mdIdx = items.mdItems()[mdBase + k];
          claimHits[hitBase + nRaw++] = miniDoublets.anchorHitIndices()[mdIdx];
          claimHits[hitBase + nRaw++] = miniDoublets.outerHitIndices()[mdIdx];
        }
        for (int i = 1; i < nRaw; ++i) {
          uint32_t const hitIdx = claimHits[hitBase + i];
          int insertPos = i;
          while (insertPos > 0 && claimHits[hitBase + insertPos - 1] > hitIdx) {
            claimHits[hitBase + insertPos] = claimHits[hitBase + insertPos - 1];
            --insertPos;
          }
          claimHits[hitBase + insertPos] = hitIdx;
        }
        int nUnique = 0;
        for (int i = 0; i < nRaw; ++i)
          if (nUnique == 0 || claimHits[hitBase + nUnique - 1] != claimHits[hitBase + i])
            claimHits[hitBase + nUnique++] = claimHits[hitBase + i];
        chains.nClaimHits()[chainIdx] = static_cast<uint16_t>(nUnique);

        // (b) + (c) the order key, the reset of the per-chain decision columns, the candidate mask
        chains.tcRow()[chainIdx] = -1;
        chains.attachPls()[chainIdx] = -1;  // no attach decision yet, and none at all when it is off
        chains.attachLogit()[chainIdx] = -1e30f;
        float const score = chains.score()[chainIdx];
        int const nLayers = chains.nLayers()[chainIdx];

        // |eta| of the innermost member triplet, hoisted above the key: the key's alpha is ramped
        // on it and block (d) below reuses the same value, so it costs one evaluation for both.
        // With orderAlphaCentral <= 0 the ramp is off and alphaEff is config.orderAlpha throughout.
        int const nNodes = chains.nNodes()[chainIdx];
        float absEtaInner = 0.f;
        if (nNodes > 0) {
          uint32_t const innerTripletIdx = nodes.tripletIndex()[items.nodeItems()[nodeBase]];
          unsigned int innerMD, middleMD, outerMD;
          chainNodeMDs(triplets, segments, innerTripletIdx, innerMD, middleMD, outerMD);
          absEtaInner = alpaka::math::abs(acc, chainT3Eta(acc, miniDoublets, outerMD));
        }
        float alphaEff = config.orderAlpha;
        if (config.orderAlphaCentral > 0.f && config.orderEtaRampHi > config.orderEtaRampLo) {
          float const rampRaw = (absEtaInner - config.orderEtaRampLo) / (config.orderEtaRampHi - config.orderEtaRampLo);
          float const ramp = alpaka::math::max(acc, 0.f, alpaka::math::min(acc, 1.f, rampRaw));
          alphaEff = config.orderAlpha + (config.orderAlphaCentral - config.orderAlpha) * (1.f - ramp);
        }
        chains.orderKey()[chainIdx] =
            score - alphaEff * alpaka::math::max(acc, 0.f, config.orderHinge - chains.marginX()[chainIdx]);

        bool const exempt = (chains.flags()[chainIdx] & kChainFlagExempt) != 0u;
        float const scoreBar =
            exempt ? (nLayers >= 6 ? config.thetaExempt6 : (nLayers == 5 ? config.thetaExempt5 : config.thetaExempt4))
                   : config.noCutTheta;
        candKeep[chainIdx] = (score >= scoreBar) ? 1u : 0u;

        // (d) the band tolerances. A chain with no nodes cannot be placed in the band and takes the
        // global tolerances, which is what the nNodes > 0 guard is for.
        bool const altBand = (nNodes > 0) && (absEtaInner >= config.braidAltEta) &&
                             (static_cast<float>(nNodes) <= config.braidAltMaxNodes);
        bandItems[chainIdx] = (altBand && maxItemsAlt != -2) ? maxItemsAlt : maxItems;
        bandFrac[chainIdx] = (altBand && config.claimFracAlt > 0.f) ? config.claimFracAlt : config.maxClaimedFrac;
        bandBraid[chainIdx] = (altBand && config.braidFracAlt > 0.f) ? config.braidFracAlt : config.braidFrac;
      }
    }
  };

  // Single-block exclusive prefix sum over nKeys counts, publishing offsets[0 .. nKeys] and the
  // grand total. Every compaction in the chain pipeline goes through it.
  //
  // Counts and offsets are separate arrays: in place, a worker's last key would read the count of
  // the next worker's first key after that worker had already overwritten it with an offset.
  struct ChainSegPrefix {
    ALPAKA_FN_ACC void operator()(
        Acc1D const& acc, uint32_t const* counts, uint32_t* offsets, uint32_t* totalOut, uint32_t nKeys) const {
      ALPAKA_ASSERT_ACC((alpaka::getWorkDiv<alpaka::Grid, alpaka::Blocks>(acc)[0u] == 1));
      auto& partial = alpaka::declareSharedVar<uint32_t[2 * kChainScanBlockThreads], __COUNTER__>(acc);

      uint32_t const nWorkers = chainScanWorkerCount(acc);
      uint32_t const worker = chainScanWorkerIndex(acc);
      ALPAKA_ASSERT_ACC(nWorkers <= kChainScanBlockThreads);
      uint32_t const chunk = (nKeys + nWorkers - 1u) / nWorkers;
      uint32_t const begin = (worker * chunk < nKeys) ? worker * chunk : nKeys;
      uint32_t const endKey = (begin + chunk < nKeys) ? begin + chunk : nKeys;

      uint32_t local[1] = {0u};
      for (uint32_t k = begin; k < endKey; ++k)
        local[0] += counts[k];

      uint32_t base[1], total[1];
      chainScanBlockExclusive<1>(acc, &partial[0], nWorkers, worker, local, base, total);

      uint32_t running = base[0];
      for (uint32_t k = begin; k < endKey; ++k) {
        offsets[k] = running;
        running += counts[k];
      }
      alpaka::syncBlockThreads(acc);
      if (cms::alpakatools::once_per_block(acc)) {
        offsets[nKeys] = total[0];
        *totalOut = total[0];
      }
    }
  };

  // The one stream compaction of the chain pass. For every i with keep[i] != 0 it writes
  //   outValues[offsets[i]] = (srcValues != nullptr) ? srcValues[i] : i
  // where offsets is ChainSegPrefix's exclusive prefix over keep, and publishes the compacted length
  // through totalOut when that is not null. Order-preserving by construction, which is what every
  // caller needs: the accepted-chain order, the ascending triplet-row order and the delivery
  // position order are all load-bearing downstream.
  struct ChainCompactSelect {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint32_t const* keep,
                                  uint32_t const* offsets,
                                  uint32_t nBound,
                                  uint32_t const* srcValues,
                                  uint32_t* outValues,
                                  uint32_t* totalOut) const {
      if (totalOut != nullptr && cms::alpakatools::once_per_grid(acc))
        *totalOut = offsets[nBound];
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (keep[i] == 0u)
          continue;
        outValues[offsets[i]] = (srcValues != nullptr) ? srcValues[i] : i;
      }
    }
  };

  // Accepted chains -> TrackCandidatesBase rows. Only chains ChainRowAssign gave a row (tcRow >= 0)
  // are written, so the row set and the row order are already fixed when this runs.
  //
  //   type   nLayers >= 5 -> T5 class, == 4 -> T4 class; a granted pixel seed upgrades it to pT5
  //   pt     LOWER median of the member triplet pt (element (n-1)/2, always an actual member value)
  //   eta    the innermost member's eta        phi  the innermost member's phi
  //   hits   per MD, innermost first, anchor hit then other hit
  //
  // Slot layout: an outer-tracker hit goes into the layer slot LST itself uses,
  // (logicalLayer - 1) + kPixelLayerSlots. That is collision-free because a chain's member MD
  // layers strictly increase along the weld direction. The fallback below exists only so a
  // hypothetical repeated layer cannot silently overwrite a hit, and it counts itself in stats[7].
  struct ChainEmitTCs {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst miniDoublets,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  HitsBaseConst hitsBase,
                                  PixelSeedsConst pixelSeeds,
                                  ChainNodesConst nodes,
                                  ChainItemsConst items,
                                  Chains chains,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  uint32_t nHits,
                                  uint16_t pixelModuleIndex,
                                  uint8_t* ccClaimed,
                                  uint32_t* stats) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());
      for (uint32_t chainIdx : cms::alpakatools::uniform_elements(acc, nChains)) {
        int32_t const tcRowSigned = chains.tcRow()[chainIdx];
        if (tcRowSigned < 0)
          continue;
        uint32_t const tcRow = static_cast<uint32_t>(tcRowSigned);
        int const nLayers = chains.nLayers()[chainIdx];
        uint32_t const nodeBase = chains.nodeOffset()[chainIdx];
        int const nNodes = chains.nNodes()[chainIdx];
        uint32_t const mdBase = 3u * nodeBase;
        int const nMDs = chains.nMDs()[chainIdx];

        // --- pt: lower median of the member triplet pt ----------------------------------------
        // Partial selection sort up to the (nNodes - 1) / 2 -th smallest, which leaves that element
        // where std::nth_element would. nNodes is bounded by kChainMaxNodes.
        float memberPts[kChainMaxNodes];
        for (int k = 0; k < nNodes && k < static_cast<int>(kChainMaxNodes); ++k)
          memberPts[k] = chaingate::t3Pt(triplets, nodes.tripletIndex()[items.nodeItems()[nodeBase + k]]);
        int const nPts = (nNodes < static_cast<int>(kChainMaxNodes)) ? nNodes : static_cast<int>(kChainMaxNodes);
        int const medianIdx = (nPts - 1) / 2;
        for (int i = 0; i <= medianIdx; ++i) {
          int best = i;
          for (int j = i + 1; j < nPts; ++j)
            if (memberPts[j] < memberPts[best])
              best = j;
          float const swapPt = memberPts[i];
          memberPts[i] = memberPts[best];
          memberPts[best] = swapPt;
        }
        chains.tcPt()[chainIdx] = memberPts[medianIdx];

        // --- eta / phi from the innermost member triplet ---------------------------------------
        uint32_t const innerTripletIdx = nodes.tripletIndex()[items.nodeItems()[nodeBase]];
        unsigned int innerMD, middleMD, outerMD;
        chainNodeMDs(triplets, segments, innerTripletIdx, innerMD, middleMD, outerMD);
        chains.tcEta()[chainIdx] = chainT3Eta(acc, miniDoublets, outerMD);
        chains.tcPhi()[chainIdx] =
            cms::alpakatools::phi(acc, miniDoublets.anchorX()[innerMD], miniDoublets.anchorY()[innerMD]);

        // --- an attached pixel seed is an IN-PLACE UPGRADE of this row -------------------------
        // A granted pLS turns the row from its bare class into the seeded class of the same length,
        // with the seed's pixel hits prepended and pt taken from the seed (better measured than the
        // member-triplet median). eta, phi and the outer-tracker hits stay the chain's: no row is
        // added and none is skipped, so the attach never changes how many candidates come out.
        int32_t const attachedPls = chains.attachPls()[chainIdx];

        // --- the TC row ------------------------------------------------------------------------
        // The length split is the same on both sides: 5+ layers is the T5/pT5 pair, 4 is T4/pT4.
        // Collapsing the seeded side onto pT5 would make a 4-layer seeded chain indistinguishable
        // from a genuine 5-layer one in every per-type count.
        bool const isLong = (nLayers >= 5);
        candsBase.trackCandidateType()[tcRow] =
            (attachedPls >= 0) ? (isLong ? LSTObjType::pT5 : LSTObjType::pT4)
                               : (isLong ? LSTObjType::T5 : LSTObjType::T4);
        candsBase.pixelSeedIndex()[tcRow] =
            (attachedPls >= 0) ? pixelSeeds.seedIdx()[attachedPls] : static_cast<unsigned int>(-1);
        candsExtended.directObjectIndices()[tcRow] = chainIdx;
        candsExtended.objectIndices()[tcRow][0] = chainIdx;
        candsExtended.objectIndices()[tcRow][1] = chainIdx;
        for (int slot = 0; slot < Params_TC::kLayers; ++slot) {
          candsExtended.logicalLayers()[tcRow][slot] = 0;
          candsExtended.lowerModuleIndices()[tcRow][slot] = kTCEmptyLowerModule;
          candsBase.hitIndices()[tcRow][slot][0] = kTCEmptyHitIdx;
          candsBase.hitIndices()[tcRow][slot][1] = kTCEmptyHitIdx;
        }
        if (attachedPls >= 0) {
          chains.tcPt()[chainIdx] = pixelSeeds.ptIn()[attachedPls];
          // The seed's DISTINCT pixel hit rows, in seed order, filling the two pixel layer slots.
          // Only genuine pixel rows are kept (the kPixelModuleId test), so a 3-hit seed contributes
          // three rows rather than the duplicated fourth that LST's own bare-pLS rows carry.
          uint32_t const firstSeedHit = pixelSeeds.firstHit()[attachedPls];
          uint32_t const nSeedHits = static_cast<uint32_t>(pixelSeeds.nHits()[attachedPls]);
          uint32_t const nStored = nSeedHits < kMaxPLSHitsInHitsSoA ? nSeedHits : kMaxPLSHitsInHitsSoA;
          int slotPix = 0;
          for (uint32_t k = 0; k < nStored && slotPix < Params_TC::kPixelLayerSlots * Params_TC::kHitsPerLayer; ++k) {
            uint32_t const hitIdx = firstSeedHit + k;
            if (hitIdx >= nHits)
              continue;
            if (hitsBase.detid()[hitIdx] != kPixelModuleId)
              continue;
            int const pixSlot = slotPix / Params_TC::kHitsPerLayer;
            candsExtended.logicalLayers()[tcRow][pixSlot] = 0;
            candsExtended.lowerModuleIndices()[tcRow][pixSlot] = pixelModuleIndex;
            candsBase.hitIndices()[tcRow][pixSlot][slotPix % Params_TC::kHitsPerLayer] = hitIdx;
            ++slotPix;
          }
        }
        for (int k = 0; k < nMDs; ++k) {
          uint32_t const mdIdx = items.mdItems()[mdBase + k];
          // The pre-claim map read by the bare-triplet contention sweep: the MDs of every EMITTED
          // chain TC. Same rows and same walk, so it is built here instead of in a second pass.
          if (ccClaimed != nullptr)
            ccClaimed[mdIdx] = 1u;
          uint16_t const moduleIdx = miniDoublets.moduleIndices()[mdIdx];
          int const logicalLayer = chainMdLayer(modules, miniDoublets, mdIdx);
          int slot = (logicalLayer - 1) + Params_TC::kPixelLayerSlots;
          if (slot < Params_TC::kPixelLayerSlots || slot >= Params_TC::kLayers ||
              candsExtended.lowerModuleIndices()[tcRow][slot] != kTCEmptyLowerModule) {
            // The fallback must stay OUT of the two pixel layer slots: RecoTracker/LST's
            // LSTOutputConverter only scans [kPixelLayerSlots, kLayers) for the outer-tracker hits
            // of a T5 / T4 row, so a hit parked in slot 0 or 1 would be silently dropped there.
            slot = -1;
            for (int trySlot = Params_TC::kPixelLayerSlots; trySlot < Params_TC::kLayers; ++trySlot)
              if (candsExtended.lowerModuleIndices()[tcRow][trySlot] == kTCEmptyLowerModule) {
                slot = trySlot;
                break;
              }
            alpaka::atomicAdd(acc, &stats[7], 1u, alpaka::hierarchy::Threads{});
            if (slot < 0) {
              alpaka::atomicAdd(acc, &stats[8], 1u, alpaka::hierarchy::Threads{});
              break;  // the row is full: no slot left, cannot happen for nMDs <= 13
            }
          }
          candsExtended.logicalLayers()[tcRow][slot] = static_cast<uint8_t>(logicalLayer);
          candsExtended.lowerModuleIndices()[tcRow][slot] = moduleIdx;
          candsBase.hitIndices()[tcRow][slot][0] = miniDoublets.anchorHitIndices()[mdIdx];
          candsBase.hitIndices()[tcRow][slot][1] = miniDoublets.outerHitIndices()[mdIdx];
        }
      }
    }
  };

  // Multi-kernel forms of the order-dependent claim, row-assignment and carried-row stages.

  namespace chainpar {
    constexpr uint32_t kNoPos = 0xFFFFFFFFu;
    constexpr uint8_t kUndecided = 0u;
    constexpr uint8_t kRejected = 1u;
    constexpr uint8_t kAccepted = 2u;
    // Hard stop on the claim round loop. Progress is guaranteed (at least one chain is decided per
    // round), so this can only fire on a logic bug; it is reported through stats[13].
    constexpr uint32_t kMaxClaimRounds = 4096u;
  }  // namespace chainpar

  // Stable stream compaction of the TrackCandidates rows.
  //
  // An in-place compaction writes row w while reading row r with w <= r, which a thread-parallel
  // form cannot do: the thread compacting row r would write into row w while the thread for row w
  // is still reading it. The payload is staged through a scratch array instead -- two passes over
  // ~15k x 160 B, a few microseconds, against the 1.5 ms an in-place serial walk costs on device.
  struct ChainTCRowPayload {
    unsigned int hitIndices[Params_TC::kLayers][Params_TC::kHitsPerLayer];
    unsigned int pixelSeedIndex;
    unsigned int directObjectIndices;
    unsigned int objectIndices[2];
    uint16_t lowerModuleIndices[Params_TC::kLayers];
    uint8_t logicalLayers[Params_TC::kLayers];
    LSTObjType type;
  };

  // Keep predicate of the FIRST compaction, run before the claim: it drops the carried candidate
  // classes that the chain pipeline rebuilds itself, so that the claim never competes for hits with
  // a row it is about to replace.
  //
  // nBound is the ALLOCATED row count, not the live one: the live count is a device scalar and
  // reading it on the host would cost a queue synchronisation, so the flag pass simply covers the
  // whole allocation and zeroes everything past the live end.
  struct ChainTCKeepCompact {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBaseConst candsBase,
                                  uint32_t* keep,
                                  uint32_t nBound,
                                  ChainConfig config) const {
      uint32_t const nInputRows = candsBase.nTrackCandidates();
      for (uint32_t rowIdx : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (rowIdx >= nInputRows) {
          keep[rowIdx] = 0u;
          continue;
        }
        LSTObjType const type = candsBase.trackCandidateType()[rowIdx];
        bool keepRow = false;
        if (type == LSTObjType::pT3)
          keepRow = !config.replacePT3;
        else if (type == LSTObjType::pLS)
          keepRow = true;
        else if (type == LSTObjType::pT5)
          keepRow = !config.replacePT5;
        // LSTObjType::T5 and LSTObjType::T4 are the classes the chain pipeline replaces outright.
        keep[rowIdx] = keepRow ? 1u : 0u;
      }
    }
  };

  // Keep predicate of the SECOND compaction, run after everything is emitted: it retires the
  // carried bare-pixel-seed rows that the attach and the seed crossclean condemned. A bare seed row
  // goes when its seed was attached to a chain, when it lost the contention but had a pair above
  // its retirement bar (either evidence array against its own bar), or when the crossclean retired
  // it. The chain rows are the tail of the collection and are kept verbatim -- `boundary` is where
  // they start. The kept per-class tallies are integer sums, so atomic accumulation is
  // order-independent.
  //   classCounts[0] pT5   [1] pT3   [2] pLS   [3] T5   [4] T4   [5] pT4
  struct ChainTCKeepSuppress {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBaseConst candsBase,
                                  TrackCandidatesExtendedConst candsExtended,
                                  ChainsConst chains,
                                  uint8_t const* plsOwned,
                                  uint32_t const* plsBestChain,
                                  uint32_t const* plsBestT3,
                                  uint8_t const* xcRetired,
                                  uint8_t const* plsMutual,
                                  uint32_t nPls,
                                  uint32_t* keep,
                                  uint32_t* classCounts,
                                  uint32_t nBound,
                                  uint32_t* stats,
                                  ChainConfig config) const {
      uint32_t const keyChain = chainOrderFloat(config.rpsThetaChain);
      uint32_t const keyT3 = chainOrderFloat(config.rpsThetaT3);
      uint32_t const nInputRows = candsBase.nTrackCandidates();
      uint32_t const nChainRows = chains.nChainTCs();
      uint32_t const boundary = (nChainRows <= nInputRows) ? (nInputRows - nChainRows) : 0u;
      for (uint32_t rowIdx : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (rowIdx >= nInputRows) {
          keep[rowIdx] = 0u;
          continue;
        }
        LSTObjType const type = candsBase.trackCandidateType()[rowIdx];
        bool drop = false;
        if (rowIdx < boundary && type == LSTObjType::pLS) {
          int32_t const plsIdx = static_cast<int32_t>(candsExtended.directObjectIndices()[rowIdx]);
          if (plsIdx >= 0 && static_cast<uint32_t>(plsIdx) < nPls) {
            drop = plsOwned[plsIdx] != 0u;
            if (config.attachSuppressBarePLS)
              drop = drop || (plsBestChain[plsIdx] >= keyChain) || (plsBestT3[plsIdx] >= keyT3);
            drop = drop || (xcRetired[plsIdx] != 0u);
            // The mutual-best channel: set only for a seed that is the pre-threshold argmax pair of
            // a delivered 5+-layer accepted chain AND whose own best chain is that same chain.
            // Inert when config.dupMutualDelta < 0, in which case the flag array is never written and
            // the caller passes a null pointer.
            drop = drop || (plsMutual != nullptr && plsMutual[plsIdx] != 0u);
          }
        }
        if (drop) {
          alpaka::atomicAdd(acc, &stats[6], 1u, alpaka::hierarchy::Blocks{});
          keep[rowIdx] = 0u;
          continue;
        }
        keep[rowIdx] = 1u;
        if (type == LSTObjType::pT5)
          alpaka::atomicAdd(acc, &classCounts[0], 1u, alpaka::hierarchy::Blocks{});
        else if (type == LSTObjType::pT3)
          alpaka::atomicAdd(acc, &classCounts[1], 1u, alpaka::hierarchy::Blocks{});
        else if (type == LSTObjType::pLS)
          alpaka::atomicAdd(acc, &classCounts[2], 1u, alpaka::hierarchy::Blocks{});
        else if (type == LSTObjType::T5)
          alpaka::atomicAdd(acc, &classCounts[3], 1u, alpaka::hierarchy::Blocks{});
        else if (type == LSTObjType::T4)
          alpaka::atomicAdd(acc, &classCounts[4], 1u, alpaka::hierarchy::Blocks{});
        else if (type == LSTObjType::pT4)
          alpaka::atomicAdd(acc, &classCounts[5], 1u, alpaka::hierarchy::Blocks{});
      }
    }
  };

  // Pass 1 of the staged compaction: every kept row's payload into its compacted position.
  struct ChainTCGather {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBaseConst candsBase,
                                  TrackCandidatesExtendedConst candsExtended,
                                  uint32_t const* keep,
                                  uint32_t const* offsets,
                                  uint32_t nBound,
                                  ChainTCRowPayload* stagedOut) const {
      for (uint32_t rowIdx : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (keep[rowIdx] == 0u)
          continue;
        ChainTCRowPayload payload;
        payload.type = candsBase.trackCandidateType()[rowIdx];
        payload.pixelSeedIndex = candsBase.pixelSeedIndex()[rowIdx];
        payload.directObjectIndices = candsExtended.directObjectIndices()[rowIdx];
        payload.objectIndices[0] = candsExtended.objectIndices()[rowIdx][0];
        payload.objectIndices[1] = candsExtended.objectIndices()[rowIdx][1];
        for (int slot = 0; slot < Params_TC::kLayers; ++slot) {
          payload.logicalLayers[slot] = candsExtended.logicalLayers()[rowIdx][slot];
          payload.lowerModuleIndices[slot] = candsExtended.lowerModuleIndices()[rowIdx][slot];
          payload.hitIndices[slot][0] = candsBase.hitIndices()[rowIdx][slot][0];
          payload.hitIndices[slot][1] = candsBase.hitIndices()[rowIdx][slot][1];
        }
        stagedOut[offsets[rowIdx]] = payload;
      }
    }
  };

  // Pass 2 of the staged compaction: the staged payloads back into the collection.
  struct ChainTCScatter {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  ChainTCRowPayload const* stagedIn,
                                  uint32_t const* offsets,
                                  uint32_t nInputRows) const {
      uint32_t const nOut = offsets[nInputRows];
      for (uint32_t outRow : cms::alpakatools::uniform_elements(acc, nOut)) {
        ChainTCRowPayload const payload = stagedIn[outRow];
        candsBase.trackCandidateType()[outRow] = payload.type;
        candsBase.pixelSeedIndex()[outRow] = payload.pixelSeedIndex;
        candsExtended.directObjectIndices()[outRow] = payload.directObjectIndices;
        candsExtended.objectIndices()[outRow][0] = payload.objectIndices[0];
        candsExtended.objectIndices()[outRow][1] = payload.objectIndices[1];
        for (int slot = 0; slot < Params_TC::kLayers; ++slot) {
          candsExtended.logicalLayers()[outRow][slot] = payload.logicalLayers[slot];
          candsExtended.lowerModuleIndices()[outRow][slot] = payload.lowerModuleIndices[slot];
          candsBase.hitIndices()[outRow][slot][0] = payload.hitIndices[slot][0];
          candsBase.hitIndices()[outRow][slot][1] = payload.hitIndices[slot][1];
        }
      }
    }
  };

  // The scalar tail of the pre-claim compaction: the new live count, and the class counters of the
  // classes the chain pipeline is about to rebuild.
  struct ChainTCFinishCompact {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  uint32_t const* offsets,
                                  uint32_t nInputRows,
                                  ChainConfig config) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      candsBase.nTrackCandidates() = offsets[nInputRows];
      candsExtended.nTrackCandidatespT5() = 0u;
      candsExtended.nTrackCandidatesT5() = 0u;
      candsExtended.nTrackCandidatesT4() = 0u;
      if (config.replacePT3)
        candsExtended.nTrackCandidatespT3() = 0u;
    }
  };

  // The scalar tail of the retirement pass. Every class count is recounted from the kept rows.
  struct ChainTCFinishSuppress {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  uint32_t const* offsets,
                                  uint32_t const* classCounts,
                                  uint32_t nInputRows) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      candsBase.nTrackCandidates() = offsets[nInputRows];
      candsExtended.nTrackCandidatespT5() = classCounts[0];
      candsExtended.nTrackCandidatespT3() = classCounts[1];
      candsExtended.nTrackCandidatespLS() = classCounts[2];
      candsExtended.nTrackCandidatesT5() = classCounts[3];
      candsExtended.nTrackCandidatesT4() = classCounts[4];
      candsExtended.nTrackCandidatespT4() = classCounts[5];
    }
  };

  // The claim.

  // The (orderKey, stableKey, chain index) comparison operands of one candidate, gathered into one
  // contiguous record so the O(n^2) rank pass reads 12 contiguous bytes per comparison instead of
  // chasing three SoA columns.
  struct ChainOrderKeyRec {
    float orderKey;
    uint32_t stable;
    uint32_t chain;
  };

  // Gathers the candidate chains (candKeep != 0) into a dense array of comparison records.
  struct ChainCandScatter {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* keep,
                                  uint32_t const* offsets,
                                  ChainOrderKeyRec* records) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());
      for (uint32_t chainIdx : cms::alpakatools::uniform_elements(acc, nChains)) {
        if (keep[chainIdx] == 0u)
          continue;
        ChainOrderKeyRec record;
        record.orderKey = chains.orderKey()[chainIdx];
        record.stable = chains.stableKey()[chainIdx];
        record.chain = chainIdx;
        records[offsets[chainIdx]] = record;
      }
    }
  };

  // The claim's priority order, produced as a RANK instead of a sort.
  //
  // The order is (orderKey desc, stableKey asc, chain index asc). That comparator is a strict TOTAL
  // order -- the chain index is unique, so no two candidates ever compare equal -- so the position
  // of a candidate in the sorted sequence is exactly the number of candidates that compare before
  // it. Counting that directly is a perfectly parallel O(n^2) pass over n ~ 2-4k cache-resident
  // records, and it yields the sorted permutation element for element by construction.
  //
  // stableKey (the identity of the chain's head node, see ChainsSoA.h) is what keeps the order
  // reproducible: the chain NUMBERING permutes between two identical runs, so it may only ever be
  // the last tie-break, under a key that does not.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool chainOrderBefore(ChainOrderKeyRec const& cand,
                                                       ChainOrderKeyRec const& reference) {
    return (cand.orderKey != reference.orderKey) ? (cand.orderKey > reference.orderKey)
                                                 : ((cand.stable != reference.stable) ? (cand.stable < reference.stable)
                                                                                      : (cand.chain < reference.chain));
  }

  // The rank is computed in slices for occupancy. One thread per candidate gives only nCand-wide
  // parallelism (~4.7k at PU200, an eighth of an L40's SMs) for nCand^2 work. The rank is a SUM of
  // independent predicates, so it splits: slice s of candidate i counts the j == s (mod kSlices)
  // part and the finish pass adds the kSlices partials. Integer addition is order-independent, so
  // the rank -- and therefore `order` -- is identical element for element, not merely equivalent.
  //
  // The slice layout is deliberate: consecutive threads take consecutive s of the SAME i, so a warp
  // broadcasts recs[i] and reads recs[s .. s+31] as one contiguous 384-byte line per step.
  static constexpr uint32_t kChainRankSlices = 32u;

  // Per-slice partial ranks: partial[i * kChainRankSlices + s] counts the candidates in slice s
  // that sort before candidate i.
  struct ChainClaimRankPartial {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainOrderKeyRec const* records,
                                  uint32_t const* nCandPtr,
                                  uint32_t nBound,
                                  uint32_t* partial) const {
      uint32_t const nCand = *nCandPtr;
      for (uint32_t workIdx : cms::alpakatools::uniform_elements(acc, nBound * kChainRankSlices)) {
        uint32_t const candIdx = workIdx / kChainRankSlices;
        if (candIdx >= nCand)
          continue;
        uint32_t const slice = workIdx % kChainRankSlices;
        ChainOrderKeyRec const selfRecord = records[candIdx];
        uint32_t nBefore = 0u;
        for (uint32_t otherIdx = slice; otherIdx < nCand; otherIdx += kChainRankSlices) {
          if (otherIdx == candIdx)
            continue;
          nBefore += chainOrderBefore(records[otherIdx], selfRecord) ? 1u : 0u;
        }
        partial[workIdx] = nBefore;
      }
    }
  };

  // Sums a candidate's slice partials into its rank and writes it into `order` at that position.
  struct ChainClaimRankFinish {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainOrderKeyRec const* records,
                                  uint32_t const* nCandPtr,
                                  uint32_t nBound,
                                  uint32_t const* partial,
                                  uint32_t* order) const {
      uint32_t const nCand = *nCandPtr;
      for (uint32_t candIdx : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (candIdx >= nCand)
          continue;
        uint32_t rank = 0u;
        for (uint32_t slice = 0; slice < kChainRankSlices; ++slice)
          rank += partial[candIdx * kChainRankSlices + slice];
        order[rank] = records[candIdx].chain;
      }
    }
  };

  // Pre-claim: the outer-tracker hits of the surviving carried pixel rows are marked owned before
  // the walk starts, so a chain cannot re-use a hit that a kept candidate already delivers.
  //
  // Order is immaterial here even though the walk that follows is order-dependent: every writer
  // stores the SAME value (kPixOwner) into a map that starts entirely free, so the result is the
  // plain union. Two threads storing the identical word to one address is a benign race on every
  // backend.
  struct ChainPreClaimPixels {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBaseConst candsBase,
                                  TrackCandidatesExtendedConst candsExtended,
                                  int32_t* owner,
                                  uint32_t nOwner) const {
      uint32_t const nCarried = candsBase.nTrackCandidates();
      for (uint32_t rowIdx : cms::alpakatools::uniform_elements(acc, nCarried)) {
        if (candsBase.trackCandidateType()[rowIdx] != LSTObjType::pT3)
          continue;
        for (int slot = 0; slot < Params_TC::kLayers; ++slot) {
          if (candsExtended.lowerModuleIndices()[rowIdx][slot] == kTCEmptyLowerModule)
            continue;
          if (candsExtended.logicalLayers()[rowIdx][slot] == 0)
            continue;  // pixel layer slot: not an outer-tracker hit
          for (int hitInLayer = 0; hitInLayer < Params_TC::kHitsPerLayer; ++hitInLayer) {
            unsigned int const hitIdx = candsBase.hitIndices()[rowIdx][slot][hitInLayer];
            if (hitIdx == kTCEmptyHitIdx || hitIdx >= nOwner)
              continue;
            owner[hitIdx] = chainarb::kPixOwner;
          }
        }
      }
    }
  };

  // THE GREEDY CLAIM, as conflict-free rounds. Single block, one internal loop over rounds.
  //
  // Serially, candidates would be visited in `order` and each would take its hits or be refused.
  // Here a round decides every candidate that is currently the FIRST claimant of all of its own
  // hits (phase A registers positions with atomicMin, phase B checks that this candidate holds the
  // minimum everywhere). Such a candidate's verdict cannot depend on any still-undecided one, so it
  // is exactly the verdict the serial walk would reach. Anything that loses a hit to an earlier
  // position waits for the next round. Progress is guaranteed because the globally first
  // undecided candidate always qualifies, so the loop terminates in at most nCand rounds
  // (measured: about 4 per event).
  //
  // The refusals themselves are the two tests in the file header: the count/fraction bar in phase A
  // (monotone -- an already-owned hit is never released, so the verdict can never turn around) and
  // the braid in phase B.
  //
  // The tie census in the prologue (stats[9]: adjacent exact orderKey ties in the finished order)
  // rides along here because it reads the same `order` and writes nothing the walk touches.
  //
  // stats[11] rounds to convergence   stats[12] peak undecided-after-a-round   stats[13] round cap hit
  struct ChainClaimRounds {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  Chains chains,
                                  uint32_t const* claimHits,
                                  uint32_t const* order,
                                  uint32_t const* nCandPtr,
                                  int32_t* owner,
                                  uint32_t* minPos,
                                  uint32_t* nClaimedScratch,
                                  uint8_t* state,
                                  uint8_t* participating,
                                  uint32_t* accepted,
                                  int32_t const* bandItems,
                                  float const* bandFrac,
                                  float const* bandBraid,
                                  int32_t* blockedBy,
                                  int32_t* blockedOther,
                                  uint32_t* stats,
                                  ChainConfig config) const {
      ALPAKA_ASSERT_ACC((alpaka::getWorkDiv<alpaka::Grid, alpaka::Blocks>(acc)[0u] == 1));
      auto& partial = alpaka::declareSharedVar<uint32_t[2 * kChainScanBlockThreads], __COUNTER__>(acc);
      auto& sRemaining = alpaka::declareSharedVar<uint32_t, __COUNTER__>(acc);

      uint32_t const nWorkers = chainScanWorkerCount(acc);
      uint32_t const worker = chainScanWorkerIndex(acc);
      ALPAKA_ASSERT_ACC(nWorkers <= kChainScanBlockThreads);

      uint32_t const nCand = *nCandPtr;
      bool const braidOn = config.braidFrac > 0.f || config.braidFracAlt > 0.f;

      for (uint32_t orderPos = worker; orderPos < nCand; orderPos += nWorkers) {
        state[orderPos] = chainpar::kUndecided;
        if (orderPos + 1u < nCand && chains.orderKey()[order[orderPos]] == chains.orderKey()[order[orderPos + 1u]])
          alpaka::atomicAdd(acc, &stats[9], 1u, alpaka::hierarchy::Threads{});
      }
      alpaka::syncBlockThreads(acc);

      uint32_t rounds = 0u;
      uint32_t peak = 0u;
      for (;;) {
        // The sync pair brackets the reset so that no worker can zero sRemaining before every
        // worker has read the previous round's value.
        alpaka::syncBlockThreads(acc);
        if (cms::alpakatools::once_per_block(acc))
          sRemaining = 0u;
        alpaka::syncBlockThreads(acc);

        // --- phase A: claim statistics, the monotone pre-reject, and the position registration ---
        for (uint32_t orderPos = worker; orderPos < nCand; orderPos += nWorkers) {
          participating[orderPos] = 0u;
          if (state[orderPos] != chainpar::kUndecided)
            continue;
          uint32_t const chainIdx = order[orderPos];
          uint32_t const hitBase = 6u * chains.nodeOffset()[chainIdx];
          int const nOwnHits = chains.nClaimHits()[chainIdx];

          int nClaimed = 0;
          for (int k = 0; k < nOwnHits; ++k)
            nClaimed += (owner[claimHits[hitBase + k]] != chainarb::kFree) ? 1 : 0;
          float const frac = (nOwnHits > 0) ? static_cast<float>(nClaimed) / static_cast<float>(nOwnHits) : 0.f;

          int const itemBar = bandItems[chainIdx];
          float const fracBar = bandFrac[chainIdx];

          // A negative item bar means "count bar disabled, fraction only". claimCountExclusive
          // makes the count bar the sole test rather than an alternative to the fraction.
          bool claimOk;
          if (itemBar < 0)
            claimOk = !(frac > fracBar);
          else if (config.claimCountExclusive)
            claimOk = nClaimed <= itemBar;
          else
            claimOk = (nClaimed <= itemBar) || !(frac > fracBar);
          if (!claimOk) {
            state[orderPos] = chainpar::kRejected;  // monotone: this verdict can never turn around
            continue;
          }

          nClaimedScratch[orderPos] = static_cast<uint32_t>(nClaimed);
          participating[orderPos] = 1u;
          for (int k = 0; k < nOwnHits; ++k)
            alpaka::atomicMin(acc, &minPos[claimHits[hitBase + k]], orderPos, alpaka::hierarchy::Threads{});
        }
        alpaka::syncBlockThreads(acc);

        // --- phase B: the position test, then the braid for whoever passed it --------------------
        for (uint32_t orderPos = worker; orderPos < nCand; orderPos += nWorkers) {
          if (participating[orderPos] == 0u)
            continue;
          uint32_t const chainIdx = order[orderPos];
          uint32_t const hitBase = 6u * chains.nodeOffset()[chainIdx];
          int const nOwnHits = chains.nClaimHits()[chainIdx];

          bool safe = true;
          for (int k = 0; k < nOwnHits && safe; ++k)
            safe = (minPos[claimHits[hitBase + k]] == orderPos);
          if (!safe) {
            alpaka::atomicAdd(acc, &sRemaining, 1u, alpaka::hierarchy::Threads{});
            continue;
          }

          int const nClaimed = static_cast<int>(nClaimedScratch[orderPos]);
          float const braidBar = bandBraid[chainIdx];
          bool killed = false;
          if (braidOn && braidBar > 0.f && nClaimed > 0) {
            // THE BRAID. Refuse this chain if it holds at least braidBar of the hits of some SINGLE
            // earlier owner. The loop is over the chain's OWN hits (a few dozen) and the owners are
            // read out of the owner map, so the cost is O(nOwnHits^2) per chain and NOT a pass over
            // pairs of chains. Distinct owners are visited exactly once each by counting only at
            // the FIRST appearance of an owner label, which needs no scratch list.
            for (int k = 0; k < nOwnHits && !killed; ++k) {
              int32_t const ownerIdx = owner[claimHits[hitBase + k]];
              if (ownerIdx == chainarb::kFree)
                continue;
              if (ownerIdx <= chainarb::kPixOwner)
                continue;  // pixel owners have no hit count of their own to be a fraction of
              bool first = true;
              for (int j = 0; j < k; ++j)
                if (owner[claimHits[hitBase + j]] == ownerIdx) {
                  first = false;
                  break;
                }
              if (!first)
                continue;
              int shared = 0;
              for (int j = 0; j < nOwnHits; ++j)
                shared += (owner[claimHits[hitBase + j]] == ownerIdx) ? 1 : 0;
              int const ownerHits = chains.nClaimHits()[static_cast<uint32_t>(ownerIdx)];
              if (ownerHits > 0 && static_cast<float>(shared) >= braidBar * static_cast<float>(ownerHits))
                killed = true;
            }
          }
          state[orderPos] = killed ? chainpar::kRejected : chainpar::kAccepted;
        }
        alpaka::syncBlockThreads(acc);

        // --- phase C: the owner writes of everything accepted THIS round, and the minPos reset ---
        // Two chains decided in the same round are the unique minimum at each of their own claim
        // hits, so their hit sets are disjoint and the owner stores never race.
        //
        // The minPos reset rides along on this phase's walk and needs no barrier of its own: it
        // writes minPos, which nothing in this phase reads; the owner stores go to a different
        // array; and two participants sharing a hit both store the SAME value (kNoPos).
        for (uint32_t orderPos = worker; orderPos < nCand; orderPos += nWorkers) {
          if (participating[orderPos] == 0u)
            continue;
          uint32_t const chainIdx = order[orderPos];
          uint32_t const hitBase = 6u * chains.nodeOffset()[chainIdx];
          int const nOwnHits = chains.nClaimHits()[chainIdx];
          bool const acceptedNow = (state[orderPos] == chainpar::kAccepted);
          for (int k = 0; k < nOwnHits; ++k) {
            uint32_t const hitIdx = claimHits[hitBase + k];
            if (acceptedNow)
              owner[hitIdx] = static_cast<int32_t>(chainIdx);
            minPos[hitIdx] = chainpar::kNoPos;
          }
        }
        alpaka::syncBlockThreads(acc);

        ++rounds;
        uint32_t const remaining = sRemaining;
        if (remaining > peak)
          peak = remaining;
        if (remaining == 0u)
          break;
        if (rounds >= chainpar::kMaxClaimRounds) {
          if (cms::alpakatools::once_per_block(acc))
            stats[13] = 1u;
          break;
        }
      }

      // --- [RESCUE bookkeeping] who ate each rejected candidate --------------------------------
      // For every REJECTED candidate, the accepted owner holding the LARGEST share of its claim
      // hits (ties to the lower chain index), and the count of its claimed hits held by owners
      // OTHER than that one. Read by the attach rescue (ChainRescueSelect / ChainRescueSwap);
      // pure bookkeeping -- nothing in this kernel or the epilogue reads it back, and the claim
      // verdicts are untouched. Runs over the FINAL owner map, after the walk has converged.
      if (blockedBy != nullptr) {
        for (uint32_t orderPos = worker; orderPos < nCand; orderPos += nWorkers) {
          if (state[orderPos] != chainpar::kRejected)
            continue;
          uint32_t const chainIdx = order[orderPos];
          uint32_t const hitBase = 6u * chains.nodeOffset()[chainIdx];
          int const nOwnHits = chains.nClaimHits()[chainIdx];
          int maxShared = 0;
          int32_t argOwner = chainarb::kFree;
          int ownedTotal = 0;
          for (int k = 0; k < nOwnHits; ++k) {
            int32_t const ownerIdx = owner[claimHits[hitBase + k]];
            if (ownerIdx == chainarb::kFree)
              continue;
            ++ownedTotal;
            if (ownerIdx <= chainarb::kPixOwner)
              continue;  // pixel owners cannot be replaced, so they are never the blocker
            bool first = true;
            for (int j = 0; j < k; ++j)
              if (owner[claimHits[hitBase + j]] == ownerIdx) {
                first = false;
                break;
              }
            if (!first)
              continue;
            int shared = 0;
            for (int j = 0; j < nOwnHits; ++j)
              shared += (owner[claimHits[hitBase + j]] == ownerIdx) ? 1 : 0;
            if (shared > maxShared || (shared == maxShared && argOwner != chainarb::kFree && ownerIdx < argOwner) ||
                (shared == maxShared && argOwner == chainarb::kFree && shared > 0)) {
              maxShared = shared;
              argOwner = ownerIdx;
            }
          }
          if (argOwner >= 0) {
            blockedBy[chainIdx] = argOwner;
            blockedOther[chainIdx] = ownedTotal - maxShared;
          }
        }
      }
      alpaka::syncBlockThreads(acc);

      // --- the accepted list, still in best-first order ----------------------------------------
      // Every later stage (the attach targets, the bare-triplet universe, the row assignment) reads
      // this array and relies on that order, so the epilogue compacts through a block-exclusive
      // scan rather than through anything that could permute it.
      uint32_t const chunk = (nCand + nWorkers - 1u) / nWorkers;
      uint32_t const begin = (worker * chunk < nCand) ? worker * chunk : nCand;
      uint32_t const endIdx = (begin + chunk < nCand) ? begin + chunk : nCand;
      uint32_t local[1] = {0u};
      for (uint32_t i = begin; i < endIdx; ++i)
        local[0] += (state[i] == chainpar::kAccepted) ? 1u : 0u;

      uint32_t base[1], total[1];
      chainScanBlockExclusive<1>(acc, &partial[0], nWorkers, worker, local, base, total);

      uint32_t outPos = base[0];
      for (uint32_t i = begin; i < endIdx; ++i)
        if (state[i] == chainpar::kAccepted)
          accepted[outPos++] = order[i];
      alpaka::syncBlockThreads(acc);

      if (cms::alpakatools::once_per_block(acc)) {
        chains.nAccepted() = total[0];
        stats[11] = rounds;
        stats[12] = peak;
      }
    }
  };

  // Row assignment: the output row of every accepted chain long enough to emit a candidate.

  // Emission flag, one per position in the accepted list. Length is the only criterion: the claim
  // has already run, so suppressing a class here removes what it DELIVERS without changing what its
  // hits cost the other chains. t4EmitMinLayers overrides the floor; 0 means kChainTCMinLayers.
  struct ChainRowFlags {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* accepted,
                                  uint32_t nBound,
                                  uint32_t* keep,
                                  ChainConfig config) const {
      uint32_t const nAcc = chains.nAccepted();
      int const minLayers = (config.t4EmitMinLayers > 0) ? config.t4EmitMinLayers : kChainTCMinLayers;
      for (uint32_t acceptedIdx : cms::alpakatools::uniform_elements(acc, nBound))
        keep[acceptedIdx] = (acceptedIdx < nAcc && chains.nLayers()[accepted[acceptedIdx]] >= minLayers) ? 1u : 0u;
    }
  };

  // Hands each flagged chain its output row, appended after the carried rows, and tallies the class
  // it will be emitted as. Rows past the allocation are simply not handed out, which over a
  // prefix-summed list in accepted order means "the first nAllocated - base qualifying chains get a
  // row" -- the same truncation a serial hand-out that stopped at nAllocated would produce.
  struct ChainRowAssign {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBaseConst candsBase,
                                  Chains chains,
                                  uint32_t const* accepted,
                                  uint32_t const* keep,
                                  uint32_t const* offsets,
                                  uint32_t nBound,
                                  uint32_t nAllocated,
                                  uint32_t* classCounts) const {
      uint32_t const base = candsBase.nTrackCandidates();
      for (uint32_t acceptedIdx : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (keep[acceptedIdx] == 0u)
          continue;
        uint32_t const rowIdx = base + offsets[acceptedIdx];
        if (rowIdx >= nAllocated)
          continue;
        uint32_t const chainIdx = accepted[acceptedIdx];
        chains.tcRow()[chainIdx] = static_cast<int32_t>(rowIdx);
        // A chain that won a pixel seed is UPGRADED in place, so it counts in the pT5 class rather
        // than in T5 -- the attach never creates a row of its own.
        //   classCounts[0] T5   [1] T4   [2] pT5
        if (chains.attachPls()[chainIdx] >= 0)
          alpaka::atomicAdd(acc, &classCounts[2], 1u, alpaka::hierarchy::Blocks{});
        else if (chains.nLayers()[chainIdx] >= 5)
          alpaka::atomicAdd(acc, &classCounts[0], 1u, alpaka::hierarchy::Blocks{});
        else
          alpaka::atomicAdd(acc, &classCounts[1], 1u, alpaka::hierarchy::Blocks{});
      }
    }
  };

  // Scalar tail of the row assignment: publishes the new live row count and the class counters.
  struct ChainRowFinish {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  Chains chains,
                                  uint32_t const* offsets,
                                  uint32_t const* classCounts,
                                  uint32_t nBound,
                                  uint32_t nAllocated) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t const base = candsBase.nTrackCandidates();
      uint32_t const want = base + offsets[nBound];
      // Clamped to the allocation, and to `base` itself when the collection was already full, so an
      // overflowing event keeps a consistent count instead of advertising rows nobody wrote.
      uint32_t const rowIdx = (base >= nAllocated) ? base : ((want < nAllocated) ? want : nAllocated);
      chains.nChainTCs() = rowIdx - base;
      candsBase.nTrackCandidates() = rowIdx;
      candsExtended.nTrackCandidatesT5() = classCounts[0];
      candsExtended.nTrackCandidatesT4() = classCounts[1];
      // ADD, not assign: the earlier compaction has already counted whatever carried pT5 rows
      // survived it, and the chains upgraded by the attach are additional to those.
      candsExtended.nTrackCandidatespT5() = candsExtended.nTrackCandidatespT5() + classCounts[2];
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
