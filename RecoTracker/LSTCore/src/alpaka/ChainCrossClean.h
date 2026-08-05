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
//   PIXEL-ANCHORED arm (LST's pT5 + pT3 arms fused; windows verbatim): retire a bare quad seed
//   that shares >= 1 pixel hit row with, or sits within dR^2 < xcDR2Pix (1e-6) of, the seed of
//   ANY delivery. The anchor set is exactly plsOwned != 0 -- every pixel-anchored TC of the chain
//   pipeline is one of our own deliveries, so the per-type dispatch of LST's kernel collapses.
//
//   BARE-CHAIN arm (LST's T5 arm with the working-point substitution): resolved from the pass-1
//   filtered pair compaction the attach scorers appended (threshold on the |seed eta|-banded
//   xcTheta, window dR^2 < xcDR2Chain against the chain's innermost-T3 direction). Pass 2 keeps a
//   pair only when the chain actually EMITTED a seedless TC (tcRow >= 0 -- which already excludes
//   the -CCS-suppressed and the too-short -- and attachPls < 0) and the seed is still unowned.
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
    ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t hitHash(uint32_t key) {
      uint32_t h = key * 2654435761u;
      h ^= h >> 15;
      return h & (kHitHashSlots - 1u);
    }
  }  // namespace chainxc

  // The compact anchor list: every pLS the deliveries own, with its eta/phi staged for the dR arm.
  struct ChainXcAnchorList {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  PixelSeedsConst pixelSeeds,
                                  uint8_t const* plsOwned,
                                  uint32_t nPls,
                                  uint32_t* anchorPls,
                                  float* anchorEta,
                                  float* anchorPhi,
                                  uint32_t* nAnchors) const {
      for (uint32_t p : cms::alpakatools::uniform_elements(acc, nPls)) {
        if (plsOwned[p] == 0u)
          continue;
        uint32_t const slot = alpaka::atomicAdd(acc, nAnchors, 1u, alpaka::hierarchy::Blocks{});
        anchorPls[slot] = p;
        anchorEta[slot] = pixelSeeds.eta()[p];
        anchorPhi[slot] = pixelSeeds.phi()[p];
      }
    }
  };

  // Serial insert of every anchor's pixel hit indices into the presence set (a few thousand
  // insertions; parallel insertion would need CAS loops for no measurable gain).
  struct ChainXcAnchorHits {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  PixelSeedsConst pixelSeeds,
                                  HitsBaseConst hitsBase,
                                  uint32_t const* anchorPls,
                                  uint32_t const* nAnchors,
                                  uint32_t nHits,
                                  uint32_t* hashKey,
                                  uint32_t* stats) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t const n = *nAnchors;
      uint32_t nIns = 0;
      for (uint32_t i = 0; i < n; ++i) {
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
          if (nIns + 1u >= chainxc::kHitHashSlots / 2u) {
            ++stats[0];  // overflow census; never reached at PU200
            return;
          }
          uint32_t slot = chainxc::hitHash(g);
          bool present = false;
          while (hashKey[slot] != chainxc::kHitHashEmpty) {
            if (hashKey[slot] == g) {
              present = true;
              break;
            }
            slot = (slot + 1u) & (chainxc::kHitHashSlots - 1u);
          }
          if (!present) {
            hashKey[slot] = g;
            ++nIns;
          }
        }
      }
    }
  };

  // The pixel-anchored arm, one thread per CARRIED TC row. Test 1 (shared pixel hit row, >= 1 of
  // <= 4 -- LST pixelHitsOverlapAny) first; test 2 (anchor dR^2 < xcDR2Pix on SEED eta/phi of
  // both sides) only when test 1 misses. The anchor loop is linear in the anchors (~hundreds).
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
                                  float const* anchorEta,
                                  float const* anchorPhi,
                                  uint32_t const* nAnchors,
                                  uint32_t nPls,
                                  uint32_t nHits,
                                  uint32_t nBound,
                                  uint8_t* xcRetired,
                                  uint32_t* stats,
                                  ChainConfig cfg) const {
      uint32_t const nIn = candsBase.nTrackCandidates();
      uint32_t const nChainRows = chains.nChainTCs();
      uint32_t const boundary = (nChainRows <= nIn) ? (nIn - nChainRows) : 0u;
      uint32_t const nAnc = *nAnchors;
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
          continue;
        }

        // Test 2: dR^2 < xcDR2Pix against any anchor seed, SEED eta/phi on both sides.
        float const etaP = pixelSeeds.eta()[p];
        float const phiP = pixelSeeds.phi()[p];
        for (uint32_t i = 0; i < nAnc; ++i) {
          float const dEta = etaP - anchorEta[i];
          float const dPhi = chainWrapPhi(phiP - anchorPhi[i]);
          if (dEta * dEta + dPhi * dPhi < cfg.xcDR2Pix) {
            xcRetired[p] = 1u;
            alpaka::atomicAdd(acc, &stats[2], 1u, alpaka::hierarchy::Blocks{});
            break;
          }
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
          continue;  // not emitted (K9-rejected, < 4 layers, -CCS-suppressed, out of rows)
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

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
