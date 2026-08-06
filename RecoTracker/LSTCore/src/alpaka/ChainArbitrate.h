#ifndef RecoTracker_LSTCore_src_alpaka_ChainArbitrate_h
#define RecoTracker_LSTCore_src_alpaka_ChainArbitrate_h

#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

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

// Chain-tracking arbitration, extension and assembly: phase P2.3 of
// standalone/prototype/P2_PORT_MAP.md.
//
// Stages implemented here:
//   K9-0 ChainBuildClaimHits    - the per-chain deduped hit-claim universe (-H 1)
//   K9-1 ChainOrderAndSelect    - the -BK 1 order key and the candidate set (theta + pixel drop)
//   K10  ChainEmitTCs           - accepted chains into TrackCandidatesBase
//
// Reference implementation: prototype/K9K10.cc and the -A 4 delivery block
// of prototype/main.cc, run with the M19 frozen flags MINUS the attach block (attach inert).
// Every arithmetic expression and every comparison is a transcription in the reference's own
// operation order, because the phase gate is an exact TC multiset match against it.
//
// PARALLELISATION NOTE (port map section 1 K9b, risk R3). The reference K9 is a SERIAL greedy
// walk whose acceptances depend on what earlier chains claimed. The claim, the extension, the
// TC-row assignment, the two row compactions and the stage-A contention are therefore expressed
// as the conflict-free-round / rank decompositions of ChainParallel.h, which are bit-exact by
// construction (that file's header carries the argument) and run on EVERY backend: the
// single-thread forms that used to shadow them on the CPU are gone.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  namespace chainarb {
    // Sentinels of the claim-universe owner map, matching prototype/K9K10.cc's encoding:
    //   -1        free
    //   >= 0      accepted chain index
    //   kPixOwner a carried pixel row
    // The reference distinguishes individual pixel owners (-(p + 2)) because at -PU 2 they take
    // part in the owner-relative braid and need their own denominator. The frozen configuration is
    // -PU 1, where pixel owners only ever contribute to nClaimed and are explicitly SKIPPED by the
    // braid, so a single sentinel is decision-identical and is what is stored.
    constexpr int32_t kFree = -1;
    constexpr int32_t kPixOwner = -2;
    constexpr double kPi = 3.14159265358979323846;
  }  // namespace chainarb

  // Diagnostic counter block written by the P2.3 kernels (never read by any decision):
  //   0 extension chains examined   1 no-fit   2 candidates   3 outer extensions
  //   4 uniq-rejected   5 chi2-rejected   6 own-fit-rejected
  //   7 K10 layer-slot fallbacks    8 K10 rows that ran out of slots
  static constexpr uint32_t kChainArbStats = 16u;

  // ------------------------------------------------------------------------------------------
  // K9-0 + K9-1. Everything the claim needs to know about a chain BEFORE the owner map exists.
  // Four passes that used to be four launches; each one is per-chain elementwise and reads nothing
  // another chain's thread writes, so they are the same computation in one visit:
  //
  //   (a) the claim universe: anchor hit + other hit of every member MD, sorted unique.
  //       prototype/K9K10.cc builds it with sort + unique over a scratch vector; the chains here
  //       are a few dozen entries at most, so an insertion sort in place is both simpler and
  //       faster, and it produces the identical set (the ORDER inside the list never enters a
  //       decision: the claim tests are counts over the whole list).
  //   (b) the order key (-B 10 -BK 1 -BT 5): score - alpha * max(0, hinge - marginX). It is NEVER
  //       a threshold; acceptance always cuts on chains.score (the M9 cross-scale-inversion
  //       lesson).
  //   (c) the candidate set = score >= thetaForChain. With -G 6 the base threshold is kNoCutTheta
  //       for every length, while a chain on the EXEMPT (large-dcaXY) branch is cut by
  //       -U4/-U5/-U6 on the legacy sum-logit scale; a gate-killed chain carries score -= 1e9 and
  //       fails both. (The pixel-consumed drop of the pre-deletion hybrid is GONE: the pT5 / pT3
  //       builders that wrote partOfPT5 / partOfPT3 are deleted.) candKeep is the flag array the
  //       candidate compaction prefixes.
  //   (d) the -WZ / -WN band decision and the three per-chain tolerances it selects, hoisted out
  //       of the claim walk -- legal because none of it depends on the owner map.
  struct ChainClaimPrep {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  MiniDoubletsConst mds,
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
                                  ChainConfig cfg) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());
      // -FC is given in MD units; the claim universe is hits, so the budget doubles.
      int const maxItems = cfg.maxClaimedMDs >= 0 ? 2 * cfg.maxClaimedMDs : -1;
      int const maxItemsAlt = cfg.claimItemsAltMDs > -2 ? 2 * cfg.claimItemsAltMDs : -2;
      for (uint32_t c : cms::alpakatools::uniform_elements(acc, nChains)) {
        uint32_t const off = chains.nodeOffset()[c];
        uint32_t const mdBase = 3u * off;
        uint32_t const hitBase = 6u * off;
        int const nMD = chains.nMDs()[c];

        // (a) the claim universe
        int n = 0;
        for (int k = 0; k < nMD; ++k) {
          uint32_t const md = items.mdItems()[mdBase + k];
          claimHits[hitBase + n++] = mds.anchorHitIndices()[md];
          claimHits[hitBase + n++] = mds.outerHitIndices()[md];
        }
        for (int i = 1; i < n; ++i) {
          uint32_t const v = claimHits[hitBase + i];
          int j = i;
          while (j > 0 && claimHits[hitBase + j - 1] > v) {
            claimHits[hitBase + j] = claimHits[hitBase + j - 1];
            --j;
          }
          claimHits[hitBase + j] = v;
        }
        int m = 0;
        for (int i = 0; i < n; ++i)
          if (m == 0 || claimHits[hitBase + m - 1] != claimHits[hitBase + i])
            claimHits[hitBase + m++] = claimHits[hitBase + i];
        chains.nClaimHits()[c] = static_cast<uint16_t>(m);

        // (b) + (c) the order key, the reset of the per-chain decision columns, the candidate mask
        chains.tcRow()[c] = -1;
        chains.attachPls()[c] = -1;  // P2.4: no attach decision yet, and none at all when K8 is off
        chains.attachLogit()[c] = -1e30f;
        float const score = chains.score()[c];
        int const nL = chains.nLayers()[c];

        chains.orderKey()[c] = score - cfg.orderAlpha * chainMaxf(0.f, cfg.orderHinge - chains.marginX()[c]);

        bool const exempt = (chains.flags()[c] & kChainFlagExempt) != 0u;
        float const thr =
            exempt ? (nL >= 6 ? cfg.thetaExempt6 : (nL == 5 ? cfg.thetaExempt5 : cfg.thetaExempt4)) : cfg.noCutTheta;

        uint8_t claim = 0u;
        if (score >= thr)
          claim = kChainClaimCandidate;
        chains.claimFlags()[c] = claim;
        candKeep[c] = (claim & kChainClaimCandidate) ? 1u : 0u;

        // (d) the -WE / -WZ band tolerances
        bool altBand = false;
        {
          int const nN = chains.nNodes()[c];
          if (nN > 0) {
            uint32_t const t3In = nodes.tripletIndex()[items.nodeItems()[off]];
            unsigned int m0, m1, m2;
            chainNodeMDs(triplets, segments, t3In, m0, m1, m2);
            float const aEta = alpaka::math::abs(acc, chainT3Eta(acc, mds, m2));
            altBand = (aEta >= cfg.braidAltEta) && (static_cast<float>(nN) <= cfg.braidAltMaxNodes);
          }
        }
        bandItems[c] = (altBand && maxItemsAlt != -2) ? maxItemsAlt : maxItems;
        bandFrac[c] = (altBand && cfg.claimFracAlt > 0.f) ? cfg.claimFracAlt : cfg.maxClaimedFrac;
        bandBraid[c] = (altBand && cfg.braidFracAlt > 0.f) ? cfg.braidFracAlt : cfg.braidFrac;
      }
    }
  };

  // Counts and offsets are separate arrays: a worker's last key would otherwise read the count of
  // the next worker's first key after that worker had already overwritten it with an offset.
  struct ChainSegPrefix {
    ALPAKA_FN_ACC void operator()(
        Acc1D const& acc, uint32_t const* counts, uint32_t* offsets, uint32_t* totalOut, uint32_t nKeys) const {
      ALPAKA_ASSERT_ACC((alpaka::getWorkDiv<alpaka::Grid, alpaka::Blocks>(acc)[0u] == 1));
      auto& partial = alpaka::declareSharedVar<uint32_t[kChainScanBlockThreads], __COUNTER__>(acc);

      uint32_t const nWorkers = chainScanWorkerCount(acc);
      uint32_t const worker = chainScanWorkerIndex(acc);
      ALPAKA_ASSERT_ACC(nWorkers <= kChainScanBlockThreads);
      uint32_t const chunk = (nKeys + nWorkers - 1u) / nWorkers;
      uint32_t const begin = (worker * chunk < nKeys) ? worker * chunk : nKeys;
      uint32_t const end = (begin + chunk < nKeys) ? begin + chunk : nKeys;

      uint32_t local = 0u;
      for (uint32_t k = begin; k < end; ++k)
        local += counts[k];
      partial[worker] = local;

      alpaka::syncBlockThreads(acc);

      uint32_t base = 0u, total = 0u;
      for (uint32_t w = 0; w < nWorkers; ++w) {
        if (w == worker)
          base = total;
        total += partial[w];
      }

      uint32_t running = base;
      for (uint32_t k = begin; k < end; ++k) {
        offsets[k] = running;
        running += counts[k];
      }
      alpaka::syncBlockThreads(acc);
      if (cms::alpakatools::once_per_block(acc)) {
        offsets[nKeys] = total;
        *totalOut = total;
      }
    }
  };

  // The one stream compaction of the chain pass. For every i with keep[i] != 0 it writes
  //   out[offs[i]] = (src != nullptr) ? src[i] : i
  // where offs is ChainSegPrefix's exclusive prefix over keep, and publishes the compacted length
  // through totalOut when that is not null. Order-preserving by construction, which is what every
  // caller needs (the K9 accepted order, the ascending T3 row order, the delivery position order).
  struct ChainCompactSelect {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint32_t const* keep,
                                  uint32_t const* offs,
                                  uint32_t nBound,
                                  uint32_t const* src,
                                  uint32_t* out,
                                  uint32_t* totalOut) const {
      if (totalOut != nullptr && cms::alpakatools::once_per_grid(acc))
        *totalOut = offs[nBound];
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (keep[i] == 0u)
          continue;
        out[offs[i]] = (src != nullptr) ? src[i] : i;
      }
    }
  };

  // chainHitPhi (the ntuple writer's t3_phi) now lives in ChainGate.h so the attach pre-record
  // kernels can use it too.

  // ------------------------------------------------------------------------------------------
  // K10, second half. Accepted chains -> TrackCandidatesBase rows.
  //
  // prototype/K9K10.cc k10AssembleChainTCs:
  //   type   nLayers >= 5 -> T5-class (4), == 4 -> T4-class (9), < 4 dropped
  //   pt     LOWER median of the member t3_pt (sorted element (n-1)/2, an actual member value)
  //   eta    the innermost member's t3_eta      phi  the innermost member's t3_phi
  //   hits   per MD, innermost first, anchor hit then other hit;  nhitOT = 2 * nMDs
  //
  // Slot layout: the outer-tracker hits go into the layer slot LST itself uses,
  // (logicalLayer - 1) + kPixelLayerSlots, which is collision-free because a chain's member MD
  // layers strictly increase along the weld direction. The fallback below exists only so that a
  // hypothetical repeated layer cannot silently overwrite a hit; it bumps stats[7] if it ever
  // fires, and the 300-event gate reports that counter.
  struct ChainEmitTCs {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst mds,
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
      for (uint32_t c : cms::alpakatools::uniform_elements(acc, nChains)) {
        int32_t const row = chains.tcRow()[c];
        if (row < 0)
          continue;
        uint32_t const tc = static_cast<uint32_t>(row);
        int const nL = chains.nLayers()[c];
        uint32_t const off = chains.nodeOffset()[c];
        int const nN = chains.nNodes()[c];
        uint32_t const mdBase = 3u * off;
        int const nMD = chains.nMDs()[c];

        // --- pt: lower median of the member t3_pt ---------------------------------------------
        // Selection of the (nN - 1) / 2 -th smallest, which is the element std::nth_element leaves
        // at that position. nNodes is bounded by kChainMaxNodes.
        float pts[kChainMaxNodes];
        for (int k = 0; k < nN && k < static_cast<int>(kChainMaxNodes); ++k)
          pts[k] = chaingate::t3Pt(triplets, nodes.tripletIndex()[items.nodeItems()[off + k]]);
        int const nP = (nN < static_cast<int>(kChainMaxNodes)) ? nN : static_cast<int>(kChainMaxNodes);
        int const mid = (nP - 1) / 2;
        for (int i = 0; i <= mid; ++i) {
          int best = i;
          for (int j = i + 1; j < nP; ++j)
            if (pts[j] < pts[best])
              best = j;
          float const t = pts[i];
          pts[i] = pts[best];
          pts[best] = t;
        }
        chains.tcPt()[c] = pts[mid];

        // --- eta / phi from the innermost member T3 -------------------------------------------
        uint32_t const t3In = nodes.tripletIndex()[items.nodeItems()[off]];
        unsigned int m0, m1, m2;
        chainNodeMDs(triplets, segments, t3In, m0, m1, m2);
        chains.tcEta()[c] = chainT3Eta(acc, mds, m2);
        chains.tcPhi()[c] = chainHitPhi(acc, mds.anchorX()[m0], mds.anchorY()[m0]);

        // --- P2.4: the attach delivery is an IN-PLACE UPGRADE of this row -----------------------
        // prototype/main.cc, the -A 4 assembly loop: a granted pLS turns the chain's TC from the
        // bare class into type 7 with the pLS's PIXEL hits prepended and pt taken from the pixel
        // seed (better measured than the member-T3 median); eta and phi stay the chain's, nhitOT
        // stays the chain's outer-tracker count. Nothing is added and nothing is skipped.
        int32_t const attachedPls = chains.attachPls()[c];

        // --- the TC row ------------------------------------------------------------------------
        candsBase.trackCandidateType()[tc] =
            (attachedPls >= 0) ? LSTObjType::pT5 : ((nL >= 5) ? LSTObjType::T5 : LSTObjType::T4);
        candsBase.pixelSeedIndex()[tc] =
            (attachedPls >= 0) ? pixelSeeds.seedIdx()[attachedPls] : static_cast<unsigned int>(-1);
        candsExtended.directObjectIndices()[tc] = c;
        candsExtended.objectIndices()[tc][0] = c;
        candsExtended.objectIndices()[tc][1] = c;
        for (int s = 0; s < Params_TC::kLayers; ++s) {
          candsExtended.logicalLayers()[tc][s] = 0;
          candsExtended.lowerModuleIndices()[tc][s] = kTCEmptyLowerModule;
          candsBase.hitIndices()[tc][s][0] = kTCEmptyHitIdx;
          candsBase.hitIndices()[tc][s][1] = kTCEmptyHitIdx;
        }
        if (attachedPls >= 0) {
          chains.tcPt()[c] = pixelSeeds.ptIn()[attachedPls];
          // The seed's DISTINCT pixel hit rows, in seed order, filling the two pixel layer slots.
          // The reference keeps only the see_hitType == Pixel entries, which is exactly the
          // kPixelModuleId test here, and a 3-hit seed therefore contributes three rows, not the
          // duplicated fourth that LST's own bare-pLS rows carry.
          uint32_t const first = pixelSeeds.firstHit()[attachedPls];
          uint32_t const nSeedHits = static_cast<uint32_t>(pixelSeeds.nHits()[attachedPls]);
          uint32_t const nStored = nSeedHits < kMaxPLSHitsInHitsSoA ? nSeedHits : kMaxPLSHitsInHitsSoA;
          int slotPix = 0;
          for (uint32_t k = 0; k < nStored && slotPix < Params_TC::kPixelLayerSlots * Params_TC::kHitsPerLayer; ++k) {
            uint32_t const h = first + k;
            if (h >= nHits)
              continue;
            if (hitsBase.detid()[h] != kPixelModuleId)
              continue;
            int const ls = slotPix / Params_TC::kHitsPerLayer;
            candsExtended.logicalLayers()[tc][ls] = 0;
            candsExtended.lowerModuleIndices()[tc][ls] = pixelModuleIndex;
            candsBase.hitIndices()[tc][ls][slotPix % Params_TC::kHitsPerLayer] = h;
            ++slotPix;
          }
        }
        for (int k = 0; k < nMD; ++k) {
          uint32_t const md = items.mdItems()[mdBase + k];
          // The -CC pre-claim map: the MDs of every EMITTED chain TC. Same rows, same walk, so the
          // sweep's claimed set is built here rather than in a second pass over the same items.
          if (ccClaimed != nullptr)
            ccClaimed[md] = 1u;
          uint16_t const mod = mds.moduleIndices()[md];
          int const logical = chainMdLayer(modules, mds, md);
          int slot = (logical - 1) + Params_TC::kPixelLayerSlots;
          if (slot < Params_TC::kPixelLayerSlots || slot >= Params_TC::kLayers ||
              candsExtended.lowerModuleIndices()[tc][slot] != kTCEmptyLowerModule) {
            // The fallback must stay OUT of the two pixel layer slots: RecoTracker/LST's
            // LSTOutputConverter only scans [kPixelLayerSlots, kLayers) for the outer-tracker hits
            // of a T5 / T4 row, so a hit parked in slot 0 or 1 would be silently dropped there.
            slot = -1;
            for (int s = Params_TC::kPixelLayerSlots; s < Params_TC::kLayers; ++s)
              if (candsExtended.lowerModuleIndices()[tc][s] == kTCEmptyLowerModule) {
                slot = s;
                break;
              }
            alpaka::atomicAdd(acc, &stats[7], 1u, alpaka::hierarchy::Threads{});
            if (slot < 0) {
              alpaka::atomicAdd(acc, &stats[8], 1u, alpaka::hierarchy::Threads{});
              break;  // the row is full: no slot left, cannot happen for nMDs <= 13
            }
          }
          candsExtended.logicalLayers()[tc][slot] = static_cast<uint8_t>(logical);
          candsExtended.lowerModuleIndices()[tc][slot] = mod;
          candsBase.hitIndices()[tc][slot][0] = mds.anchorHitIndices()[md];
          candsBase.hitIndices()[tc][slot][1] = mds.outerHitIndices()[md];
        }
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
