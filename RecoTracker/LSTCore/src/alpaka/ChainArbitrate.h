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
// as conflict-free-round / rank decompositions (the argument is carried at their definitions in
// the second half of this file) and run on EVERY backend: the single-thread forms that used to
// shadow them on the CPU are gone.

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

        // KEY (jet round 3). The -WZ band's |eta(innermost T3)| is HOISTED above the key so the key
        // can be conditioned on it at zero extra cost; block (d) below consumes the same value.
        // With orderAlphaCentral <= 0 (the shipped default) alphaEff == cfg.orderAlpha and this
        // whole block is the frozen expression bit for bit.
        int const nNodesC = chains.nNodes()[c];
        float aEta = 0.f;
        if (nNodesC > 0) {
          uint32_t const t3In = nodes.tripletIndex()[items.nodeItems()[off]];
          unsigned int m0, m1, m2;
          chainNodeMDs(triplets, segments, t3In, m0, m1, m2);
          aEta = alpaka::math::abs(acc, chainT3Eta(acc, mds, m2));
        }
        float alphaEff = cfg.orderAlpha;
        if (cfg.orderAlphaCentral > 0.f && cfg.orderEtaRampHi > cfg.orderEtaRampLo) {
          float const t = (aEta - cfg.orderEtaRampLo) / (cfg.orderEtaRampHi - cfg.orderEtaRampLo);
          float const ramp = chainMaxf(0.f, chainMinf(1.f, t));
          alphaEff = cfg.orderAlpha + (cfg.orderAlphaCentral - cfg.orderAlpha) * (1.f - ramp);
        }
        chains.orderKey()[c] = score - alphaEff * chainMaxf(0.f, cfg.orderHinge - chains.marginX()[c]);

        bool const exempt = (chains.flags()[c] & kChainFlagExempt) != 0u;
        float const thr =
            exempt ? (nL >= 6 ? cfg.thetaExempt6 : (nL == 5 ? cfg.thetaExempt5 : cfg.thetaExempt4)) : cfg.noCutTheta;

        // U5 DEAD STORE REMOVED: `chains.claimFlags()[c] = claim;` stood here. The column is
        // WRITE-ONLY in the whole tree (its only other writer is the phase-C store below, also
        // removed) and `candKeep` is computed from the LOCAL `claim`, not from the column.
        // The COLUMN ITSELF IS DELIBERATELY LEFT IN ChainsSoA: see ChainsSoA.h:100-104, removing a
        // member mid-layout shifts every later column's base address and this codebase has a
        // RECORDED ~1 ms/event regression from exactly that. Equivalent to the retired two-step
        // form: kChainClaimCandidate is 0x1, so `(claim & bit) != 0` is exactly `score >= thr`.
        candKeep[c] = (score >= thr) ? 1u : 0u;

        // (d) the -WE / -WZ band tolerances. aEta is the value hoisted above the key; the nN > 0
        // guard is preserved exactly, so a node-less chain still takes the global band as before.
        bool const altBand = (nNodesC > 0) && (aEta >= cfg.braidAltEta) &&
                             (static_cast<float>(nNodesC) <= cfg.braidAltMaxNodes);
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
      auto& partial = alpaka::declareSharedVar<uint32_t[2 * kChainScanBlockThreads], __COUNTER__>(acc);

      uint32_t const nWorkers = chainScanWorkerCount(acc);
      uint32_t const worker = chainScanWorkerIndex(acc);
      ALPAKA_ASSERT_ACC(nWorkers <= kChainScanBlockThreads);
      uint32_t const chunk = (nKeys + nWorkers - 1u) / nWorkers;
      uint32_t const begin = (worker * chunk < nKeys) ? worker * chunk : nKeys;
      uint32_t const end = (begin + chunk < nKeys) ? begin + chunk : nKeys;

      uint32_t local[1] = {0u};
      for (uint32_t k = begin; k < end; ++k)
        local[0] += counts[k];

      uint32_t base[1], total[1];
      chainScanBlockExclusive<1>(acc, &partial[0], nWorkers, worker, local, base, total);

      uint32_t running = base[0];
      for (uint32_t k = begin; k < end; ++k) {
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


  // ==========================================================================================
  // Multi-kernel forms of the order-dependent K9 / K10 / carried-row stages (was ChainParallel.h).

  namespace chainpar {
    constexpr uint32_t kNoPos = 0xFFFFFFFFu;
    constexpr uint8_t kUndecided = 0u;
    constexpr uint8_t kRejected = 1u;
    constexpr uint8_t kAccepted = 2u;
    // Hard stop on the claim round loop. Progress is proved (>= 1 chain decided per round), so this
    // can only fire on a logic bug; it is reported through stats[13].
    constexpr uint32_t kMaxClaimRounds = 4096u;
  }  // namespace chainpar

  // ==========================================================================================
  // Stable stream compaction of the TrackCandidates rows.
  //
  // The serial kernels rewrite rows in place with `w <= r`, which a thread-parallel form cannot do:
  // the thread compacting row r would be writing into row w while the thread for row w is still
  // reading it. The payload is staged through a scratch array instead, which is two passes over
  // ~15k x 160 B -- a few microseconds -- against the 1.5 ms the in-place serial walk costs on the
  // device.
  struct ChainTCRowPayload {
    unsigned int hitIndices[Params_TC::kLayers][Params_TC::kHitsPerLayer];
    unsigned int pixelSeedIndex;
    unsigned int directObjectIndices;
    unsigned int objectIndices[2];
    uint16_t lowerModuleIndices[Params_TC::kLayers];
    uint8_t logicalLayers[Params_TC::kLayers];
    LSTObjType type;
  };

  // The -RT5 1 keep predicate, transcribed from ChainCompactCarriedTCs.
  //
  // nBound is the ALLOCATED row count, not the live one: the live count is a device scalar and
  // reading it on the host would cost a queue synchronisation, so the flag pass simply covers the
  // whole allocation and zeroes everything past the live end.
  struct ChainTCKeepCompact {
    ALPAKA_FN_ACC void operator()(
        Acc1D const& acc, TrackCandidatesBaseConst candsBase, uint32_t* keep, uint32_t nBound, ChainConfig cfg) const {
      uint32_t const nIn = candsBase.nTrackCandidates();
      for (uint32_t r : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (r >= nIn) {
          keep[r] = 0u;
          continue;
        }
        LSTObjType const ty = candsBase.trackCandidateType()[r];
        bool k = false;
        if (ty == LSTObjType::pT3)
          k = !cfg.replacePT3;
        else if (ty == LSTObjType::pLS)
          k = true;
        else if (ty == LSTObjType::pT5)
          k = !cfg.replacePT5;
        // LSTObjType::T5 and LSTObjType::T4 are the classes the chain pipeline replaces outright.
        keep[r] = k ? 1u : 0u;
      }
    }
  };

  // The K8d contention / -RPS / -XC keep predicate, transcribed from ChainSuppressCarriedTCs (see
  // the semantics there: two evidence arrays against two bars, the xcRetired channel, and the
  // chain-row tail kept verbatim). The kept per-class tallies are integer sums, so accumulating
  // them with atomics is order-independent.
  //   classCounts[0] pT5   [1] pT3   [2] pLS   [3] T5   [4] T4
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
                                  ChainConfig cfg) const {
      uint32_t const keyChain = chainOrderFloat(cfg.rpsThetaChain);
      uint32_t const keyT3 = chainOrderFloat(cfg.attachThetaT3);
      uint32_t const nIn = candsBase.nTrackCandidates();
      uint32_t const nChainRows = chains.nChainTCs();
      uint32_t const boundary = (nChainRows <= nIn) ? (nIn - nChainRows) : 0u;
      for (uint32_t r : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (r >= nIn) {
          keep[r] = 0u;
          continue;
        }
        LSTObjType const ty = candsBase.trackCandidateType()[r];
        bool drop = false;
        if (r < boundary && ty == LSTObjType::pLS) {
          int32_t const p = static_cast<int32_t>(candsExtended.directObjectIndices()[r]);
          if (p >= 0 && static_cast<uint32_t>(p) < nPls) {
            drop = plsOwned[p] != 0u;
            if (cfg.attachSuppressBarePLS)
              drop = drop || (plsBestChain[p] >= keyChain) || (plsBestT3[p] >= keyT3);
            drop = drop || (xcRetired[p] != 0u);
            // JET ROUND 2 (D): the MUTUAL-BEST retirement channel. Set only for a seed that is the
            // pre-threshold argmax pair of a delivered 5+-layer accepted chain AND whose own best
            // chain is that chain (ChainAttachUnpackBest). Inert when cfg.dupMutualDelta < 0, which
            // is the shipped default -- the flag array is then never written.
            drop = drop || (plsMutual != nullptr && plsMutual[p] != 0u);
          }
        }
        if (drop) {
          alpaka::atomicAdd(acc, &stats[6], 1u, alpaka::hierarchy::Blocks{});
          keep[r] = 0u;
          continue;
        }
        keep[r] = 1u;
        if (ty == LSTObjType::pT5)
          alpaka::atomicAdd(acc, &classCounts[0], 1u, alpaka::hierarchy::Blocks{});
        else if (ty == LSTObjType::pT3)
          alpaka::atomicAdd(acc, &classCounts[1], 1u, alpaka::hierarchy::Blocks{});
        else if (ty == LSTObjType::pLS)
          alpaka::atomicAdd(acc, &classCounts[2], 1u, alpaka::hierarchy::Blocks{});
        else if (ty == LSTObjType::T5)
          alpaka::atomicAdd(acc, &classCounts[3], 1u, alpaka::hierarchy::Blocks{});
        else if (ty == LSTObjType::T4)
          alpaka::atomicAdd(acc, &classCounts[4], 1u, alpaka::hierarchy::Blocks{});
      }
    }
  };

  struct ChainTCGather {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBaseConst candsBase,
                                  TrackCandidatesExtendedConst candsExtended,
                                  uint32_t const* keep,
                                  uint32_t const* offs,
                                  uint32_t nBound,
                                  ChainTCRowPayload* out) const {
      for (uint32_t r : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (keep[r] == 0u)
          continue;
        ChainTCRowPayload p;
        p.type = candsBase.trackCandidateType()[r];
        p.pixelSeedIndex = candsBase.pixelSeedIndex()[r];
        p.directObjectIndices = candsExtended.directObjectIndices()[r];
        p.objectIndices[0] = candsExtended.objectIndices()[r][0];
        p.objectIndices[1] = candsExtended.objectIndices()[r][1];
        for (int s = 0; s < Params_TC::kLayers; ++s) {
          p.logicalLayers[s] = candsExtended.logicalLayers()[r][s];
          p.lowerModuleIndices[s] = candsExtended.lowerModuleIndices()[r][s];
          p.hitIndices[s][0] = candsBase.hitIndices()[r][s][0];
          p.hitIndices[s][1] = candsBase.hitIndices()[r][s][1];
        }
        out[offs[r]] = p;
      }
    }
  };

  struct ChainTCScatter {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  ChainTCRowPayload const* in,
                                  uint32_t const* offs,
                                  uint32_t nIn) const {
      uint32_t const nOut = offs[nIn];
      for (uint32_t w : cms::alpakatools::uniform_elements(acc, nOut)) {
        ChainTCRowPayload const p = in[w];
        candsBase.trackCandidateType()[w] = p.type;
        candsBase.pixelSeedIndex()[w] = p.pixelSeedIndex;
        candsExtended.directObjectIndices()[w] = p.directObjectIndices;
        candsExtended.objectIndices()[w][0] = p.objectIndices[0];
        candsExtended.objectIndices()[w][1] = p.objectIndices[1];
        for (int s = 0; s < Params_TC::kLayers; ++s) {
          candsExtended.logicalLayers()[w][s] = p.logicalLayers[s];
          candsExtended.lowerModuleIndices()[w][s] = p.lowerModuleIndices[s];
          candsBase.hitIndices()[w][s][0] = p.hitIndices[s][0];
          candsBase.hitIndices()[w][s][1] = p.hitIndices[s][1];
        }
      }
    }
  };

  // The scalar tail of the -RT5 compaction.
  struct ChainTCFinishCompact {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  uint32_t const* offs,
                                  uint32_t nIn,
                                  ChainConfig cfg) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      candsBase.nTrackCandidates() = offs[nIn];
      candsExtended.nTrackCandidatespT5() = 0u;
      candsExtended.nTrackCandidatesT5() = 0u;
      candsExtended.nTrackCandidatesT4() = 0u;
      if (cfg.replacePT3)
        candsExtended.nTrackCandidatespT3() = 0u;
    }
  };

  // The scalar tail of the K8d retirement pass. Recounts every class from the kept rows, exactly
  // as the serial form does.
  struct ChainTCFinishSuppress {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  uint32_t const* offs,
                                  uint32_t const* classCounts,
                                  uint32_t nIn) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      candsBase.nTrackCandidates() = offs[nIn];
      candsExtended.nTrackCandidatespT5() = classCounts[0];
      candsExtended.nTrackCandidatespT3() = classCounts[1];
      candsExtended.nTrackCandidatespLS() = classCounts[2];
      candsExtended.nTrackCandidatesT5() = classCounts[3];
      candsExtended.nTrackCandidatesT4() = classCounts[4];
    }
  };

  // ==========================================================================================
  // K9. The claim.

  // The (orderKey, stableKey, chain index) comparison operands of one candidate, gathered into one
  // contiguous record so the O(n^2) rank pass reads 12 contiguous bytes per comparison instead of
  // chasing three SoA columns.
  struct ChainOrderKeyRec {
    float key;
    uint32_t stable;
    uint32_t chain;
  };

  struct ChainCandScatter {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* keep,
                                  uint32_t const* offs,
                                  ChainOrderKeyRec* recs) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());
      for (uint32_t c : cms::alpakatools::uniform_elements(acc, nChains)) {
        if (keep[c] == 0u)
          continue;
        ChainOrderKeyRec r;
        r.key = chains.orderKey()[c];
        r.stable = chains.stableKey()[c];
        r.chain = c;
        recs[offs[c]] = r;
      }
    }
  };

  // The K9 priority order, as a RANK instead of a sort.
  //
  // The reference builds `order` with a bottom-up merge sort under the strict total order
  // (orderKey desc, stableKey asc, chain index asc). Because that comparator is a strict TOTAL
  // order -- the chain index is unique, so no two candidates ever compare equal -- the position of
  // a candidate in the sorted sequence is exactly the number of candidates that compare before it.
  // Counting that directly is a perfectly parallel O(n^2) pass over n ~ 2-4k records that all fit
  // in cache, and it reproduces the merge sort's permutation element for element by construction.
  // The comparator itself, so the one-pass and the sliced form cannot drift apart.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool chainOrderBefore(ChainOrderKeyRec const& b, ChainOrderKeyRec const& a) {
    return (b.key != a.key) ? (b.key > a.key)
                            : ((b.stable != a.stable) ? (b.stable < a.stable) : (b.chain < a.chain));
  }

  // T4 (GPU timing): the SAME rank, sliced.
  //
  // ChainClaimRank is one thread per candidate, so its parallelism is nCand (~4.7k at PU200 =
  // ~18 blocks of 256, an eighth of an L40's SMs) while its work is nCand^2. The rank is a SUM of
  // independent predicates, so it splits: slice s of candidate i counts the j == s (mod kSlices)
  // part, and the finish pass adds the kSlices partials. An integer sum does not care in what
  // order it is accumulated, so the rank -- and therefore `order` -- is identical element for
  // element, not merely equivalent.
  //
  // The slice layout is deliberate: consecutive threads take consecutive s of the SAME i, so a
  // warp broadcasts recs[i] and reads recs[s .. s+31] as one contiguous 384-byte line per step.
  static constexpr uint32_t kChainRankSlices = 32u;

  struct ChainClaimRankPartial {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainOrderKeyRec const* recs,
                                  uint32_t const* nCandPtr,
                                  uint32_t nBound,
                                  uint32_t* partial) const {
      uint32_t const n = *nCandPtr;
      for (uint32_t w : cms::alpakatools::uniform_elements(acc, nBound * kChainRankSlices)) {
        uint32_t const i = w / kChainRankSlices;
        if (i >= n)
          continue;
        uint32_t const s = w % kChainRankSlices;
        ChainOrderKeyRec const a = recs[i];
        uint32_t cnt = 0u;
        for (uint32_t j = s; j < n; j += kChainRankSlices) {
          if (j == i)
            continue;
          cnt += chainOrderBefore(recs[j], a) ? 1u : 0u;
        }
        partial[w] = cnt;
      }
    }
  };

  struct ChainClaimRankFinish {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainOrderKeyRec const* recs,
                                  uint32_t const* nCandPtr,
                                  uint32_t nBound,
                                  uint32_t const* partial,
                                  uint32_t* order) const {
      uint32_t const n = *nCandPtr;
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (i >= n)
          continue;
        uint32_t rank = 0u;
        for (uint32_t s = 0; s < kChainRankSlices; ++s)
          rank += partial[i * kChainRankSlices + s];
        order[rank] = recs[i].chain;
      }
    }
  };

  // K9a, the -PU 1 pre-claim of the SURVIVING carried pixel rows' outer-tracker hits.
  //
  // The reference walks the rows in TC order first come first served, but every writer stores the
  // SAME value (kPixOwner) into a map that starts entirely free, so the result is the plain union
  // and the order is immaterial. Two threads storing the identical word to the same address is a
  // benign race on every backend.
  struct ChainPreClaimPixels {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBaseConst candsBase,
                                  TrackCandidatesExtendedConst candsExtended,
                                  int32_t* owner,
                                  uint32_t nOwner) const {
      uint32_t const nCarried = candsBase.nTrackCandidates();
      for (uint32_t row : cms::alpakatools::uniform_elements(acc, nCarried)) {
        if (candsBase.trackCandidateType()[row] != LSTObjType::pT3)
          continue;
        for (int slot = 0; slot < Params_TC::kLayers; ++slot) {
          if (candsExtended.lowerModuleIndices()[row][slot] == kTCEmptyLowerModule)
            continue;
          if (candsExtended.logicalLayers()[row][slot] == 0)
            continue;  // pixel layer slot: not an outer-tracker hit
          for (int q = 0; q < Params_TC::kHitsPerLayer; ++q) {
            unsigned int const h = candsBase.hitIndices()[row][slot][q];
            if (h == kTCEmptyHitIdx || h >= nOwner)
              continue;
            owner[h] = chainarb::kPixOwner;
          }
        }
      }
    }
  };

  // K9b + K9c, the conflict-free-round form of the greedy claim. Single block, one internal loop
  // over rounds; see the file header for the exactness and termination argument.
  //
  // The P2.5 tie-exercise census (stats[9]: adjacent exact orderKey ties in the finished order) is
  // the prologue below -- it reads the same finished `order` the walk reads and writes nothing the
  // walk touches, so it needs no launch of its own.
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
                                  uint8_t* part,
                                  uint32_t* accepted,
                                  int32_t const* bandItems,
                                  float const* bandFrac,
                                  float const* bandBraid,
                                  uint32_t* stats,
                                  ChainConfig cfg) const {
      ALPAKA_ASSERT_ACC((alpaka::getWorkDiv<alpaka::Grid, alpaka::Blocks>(acc)[0u] == 1));
      auto& partial = alpaka::declareSharedVar<uint32_t[2 * kChainScanBlockThreads], __COUNTER__>(acc);
      auto& sRemaining = alpaka::declareSharedVar<uint32_t, __COUNTER__>(acc);

      uint32_t const nWorkers = chainScanWorkerCount(acc);
      uint32_t const worker = chainScanWorkerIndex(acc);
      ALPAKA_ASSERT_ACC(nWorkers <= kChainScanBlockThreads);

      uint32_t const n = *nCandPtr;
      bool const braidOn = cfg.braidFrac > 0.f || cfg.braidFracAlt > 0.f;

      for (uint32_t oi = worker; oi < n; oi += nWorkers) {
        state[oi] = chainpar::kUndecided;
        if (oi + 1u < n && chains.orderKey()[order[oi]] == chains.orderKey()[order[oi + 1u]])
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
        for (uint32_t oi = worker; oi < n; oi += nWorkers) {
          part[oi] = 0u;
          if (state[oi] != chainpar::kUndecided)
            continue;
          uint32_t const c = order[oi];
          uint32_t const hitBase = 6u * chains.nodeOffset()[c];
          int const total = chains.nClaimHits()[c];

          int nClaimed = 0;
          for (int k = 0; k < total; ++k)
            nClaimed += (owner[claimHits[hitBase + k]] != chainarb::kFree) ? 1 : 0;
          float const frac = (total > 0) ? static_cast<float>(nClaimed) / static_cast<float>(total) : 0.f;

          int const cItems = bandItems[c];
          float const cFrac = bandFrac[c];

          // prototype/K9K10.cc claimOkV, same operand order and the same float comparison.
          bool claimOk;
          if (cItems < 0)
            claimOk = !(frac > cFrac);
          else if (cfg.claimCountExclusive)
            claimOk = nClaimed <= cItems;
          else
            claimOk = (nClaimed <= cItems) || !(frac > cFrac);
          if (!claimOk) {
            state[oi] = chainpar::kRejected;  // monotone: this verdict can never turn around
            continue;
          }

          nClaimedScratch[oi] = static_cast<uint32_t>(nClaimed);
          part[oi] = 1u;
          for (int k = 0; k < total; ++k)
            alpaka::atomicMin(acc, &minPos[claimHits[hitBase + k]], oi, alpaka::hierarchy::Threads{});
        }
        alpaka::syncBlockThreads(acc);

        // --- phase B: the position test, then the -W braid for whoever passed it ----------------
        for (uint32_t oi = worker; oi < n; oi += nWorkers) {
          if (part[oi] == 0u)
            continue;
          uint32_t const c = order[oi];
          uint32_t const hitBase = 6u * chains.nodeOffset()[c];
          int const total = chains.nClaimHits()[c];

          bool safe = true;
          for (int k = 0; k < total && safe; ++k)
            safe = (minPos[claimHits[hitBase + k]] == oi);
          if (!safe) {
            alpaka::atomicAdd(acc, &sRemaining, 1u, alpaka::hierarchy::Threads{});
            continue;
          }

          int const nClaimed = static_cast<int>(nClaimedScratch[oi]);
          float const bFrac = bandBraid[c];
          bool killed = false;
          if (braidOn && bFrac > 0.f && nClaimed > 0) {
            // Owner-relative braid, per-chain and allocation-free: the reference's touched-owner
            // list is replaced by "count the occurrences of the FIRST appearance of each owner
            // label", which visits every distinct owner exactly once with the same denominator.
            //
            for (int k = 0; k < total && !killed; ++k) {
              int32_t const o = owner[claimHits[hitBase + k]];
              if (o == chainarb::kFree)
                continue;
              if (o <= chainarb::kPixOwner)
                continue;  // -PU 1: pixel owners do not take part in the braid
              bool first = true;
              for (int j = 0; j < k; ++j)
                if (owner[claimHits[hitBase + j]] == o) {
                  first = false;
                  break;
                }
              if (!first)
                continue;
              int cnt = 0;
              for (int j = 0; j < total; ++j)
                cnt += (owner[claimHits[hitBase + j]] == o) ? 1 : 0;
              int const oTot = chains.nClaimHits()[static_cast<uint32_t>(o)];
              if (oTot > 0 && static_cast<float>(cnt) >= bFrac * static_cast<float>(oTot))
                killed = true;
            }
          }
          state[oi] = killed ? chainpar::kRejected : chainpar::kAccepted;
        }
        alpaka::syncBlockThreads(acc);

        // --- phase C: the owner writes of everything accepted THIS round, and the minPos reset ---
        // Two chains decided in the same round are the unique minimum at each of their own claim
        // hits, so their hit sets are disjoint and the owner stores never race.
        //
        // T4: the minPos reset used to be a fourth phase behind its own barrier. It needs none.
        // It writes minPos, which nothing in this phase reads; the owner stores go to a different
        // array; and two participants sharing a hit both store the SAME value (kNoPos) there. So
        // the reset rides along on the claim-hit walk this phase already makes, and one of the
        // four random-access passes over claimHits per round disappears.
        for (uint32_t oi = worker; oi < n; oi += nWorkers) {
          if (part[oi] == 0u)
            continue;
          uint32_t const c = order[oi];
          uint32_t const hitBase = 6u * chains.nodeOffset()[c];
          int const total = chains.nClaimHits()[c];
          bool const acceptedNow = (state[oi] == chainpar::kAccepted);
          for (int k = 0; k < total; ++k) {
            uint32_t const h = claimHits[hitBase + k];
            if (acceptedNow)
              owner[h] = static_cast<int32_t>(c);
            minPos[h] = chainpar::kNoPos;
          }
          // U5 DEAD STORE REMOVED: `if (acceptedNow) chains.claimFlags()[c] |= kChainClaimAccepted;`
          // stood here. A GLOBAL READ-MODIFY-WRITE at a random index, once per accepted chain PER
          // ROUND (rounds = 4.1 mean, 7 max) inside this single-block kernel -- and
          // kChainClaimAccepted has NO READER ANYWHERE IN THE TREE.
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

      // --- the accepted list, in K9 accepted (best-first) order --------------------------------
      uint32_t const chunk = (n + nWorkers - 1u) / nWorkers;
      uint32_t const begin = (worker * chunk < n) ? worker * chunk : n;
      uint32_t const end = (begin + chunk < n) ? begin + chunk : n;
      uint32_t local[1] = {0u};
      for (uint32_t i = begin; i < end; ++i)
        local[0] += (state[i] == chainpar::kAccepted) ? 1u : 0u;

      // The epilogue is the same block-exclusive scan as everywhere else in this family. The round
      // loop above is untouched; this replaces only its closing O(nWorkers)-per-thread walk.
      uint32_t base[1], total[1];
      chainScanBlockExclusive<1>(acc, &partial[0], nWorkers, worker, local, base, total);

      uint32_t run = base[0];
      for (uint32_t i = begin; i < end; ++i)
        if (state[i] == chainpar::kAccepted)
          accepted[run++] = order[i];
      alpaka::syncBlockThreads(acc);

      if (cms::alpakatools::once_per_block(acc)) {
        chains.nAccepted() = total[0];
        stats[11] = rounds;
        stats[12] = peak;
      }
    }
  };

  // ==========================================================================================
  // EX. The extension.

  // ==========================================================================================
  // K10, first half: the output row of every accepted chain long enough to emit a TC.

  struct ChainRowFlags {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* accepted,
                                  uint32_t nBound,
                                  uint32_t* keep,
                                  ChainConfig cfg) const {
      uint32_t const nAcc = chains.nAccepted();
      // JET ROUND 3 (T4): t4EmitMinLayers suppresses a whole length class at EMISSION while the
      // claim those chains just won stays bit-identical -- which is what separates "what does the
      // class DELIVER" from "what do its hits cost everyone else". 0 = frozen kChainTCMinLayers.
      int const minL = (cfg.t4EmitMinLayers > 0) ? cfg.t4EmitMinLayers : kChainTCMinLayers;
      for (uint32_t ai : cms::alpakatools::uniform_elements(acc, nBound))
        keep[ai] = (ai < nAcc && chains.nLayers()[accepted[ai]] >= minL) ? 1u : 0u;
    }
  };

  // The serial kernel stops handing out rows at nAllocated ("if (row >= nAllocated) break"), which
  // over a filtered list in accepted order is exactly "the first nAllocated - base qualifying
  // chains get a row", i.e. a bound on the prefix index.
  struct ChainRowAssign {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBaseConst candsBase,
                                  Chains chains,
                                  uint32_t const* accepted,
                                  uint32_t const* keep,
                                  uint32_t const* offs,
                                  uint32_t nBound,
                                  uint32_t nAllocated,
                                  uint32_t* classCounts) const {
      uint32_t const base = candsBase.nTrackCandidates();
      for (uint32_t ai : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (keep[ai] == 0u)
          continue;
        uint32_t const row = base + offs[ai];
        if (row >= nAllocated)
          continue;
        uint32_t const c = accepted[ai];
        chains.tcRow()[c] = static_cast<int32_t>(row);
        // P2.4: an accepted chain that won a pLS is UPGRADED in place, so it is counted in the
        // pT5 class rather than in T5 -- no extra row is ever created by the attach.
        if (chains.attachPls()[c] >= 0)
          alpaka::atomicAdd(acc, &classCounts[2], 1u, alpaka::hierarchy::Blocks{});
        else if (chains.nLayers()[c] >= 5)
          alpaka::atomicAdd(acc, &classCounts[0], 1u, alpaka::hierarchy::Blocks{});
        else
          alpaka::atomicAdd(acc, &classCounts[1], 1u, alpaka::hierarchy::Blocks{});
      }
    }
  };

  struct ChainRowFinish {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  Chains chains,
                                  uint32_t const* offs,
                                  uint32_t const* classCounts,
                                  uint32_t nBound,
                                  uint32_t nAllocated) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t const base = candsBase.nTrackCandidates();
      uint32_t const want = base + offs[nBound];
      // The serial kernel breaks out of the row hand-out as soon as row >= nAllocated, which also
      // means it hands out nothing at all (and leaves the count at base) if base is already there.
      uint32_t const row = (base >= nAllocated) ? base : ((want < nAllocated) ? want : nAllocated);
      chains.nChainTCs() = row - base;
      candsBase.nTrackCandidates() = row;
      candsExtended.nTrackCandidatesT5() = classCounts[0];
      candsExtended.nTrackCandidatesT4() = classCounts[1];
      // ADD, not assign: ChainSuppressCarriedTCs has already counted whatever carried pT5 rows
      // survived (none under the frozen -RT5 1), and the attach upgrades are additional.
      candsExtended.nTrackCandidatespT5() = candsExtended.nTrackCandidatespT5() + classCounts[2];
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
