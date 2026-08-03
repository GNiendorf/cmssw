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
//   K9a  (inside ChainArbitrateSerial) -PU 1 pre-claim of the carried pixel rows' OT hits
//   K9b  (inside ChainArbitrateSerial) score-ordered greedy hit claim, -F / -FC / -FBC
//   K9c  (inside ChainArbitrateSerial) owner-relative braid kill, -W with the -WE/-WZ band
//   EX   ChainExtendSerial      - chain extension at assembly (-EX 1 -EXW -EXR -EXS)
//   K10  ChainAssignTCRows + ChainEmitTCs - accepted chains into TrackCandidatesBase
//   plus ChainCompactCarriedTCs - the -RT5 1 wholesale drop of the carried type-7 rows
//
// Reference implementation: prototype/K9K10.cc, prototype/Extend.cc and the -A 4 delivery block
// of prototype/main.cc, run with the M19 frozen flags MINUS the attach block (attach inert).
// Every arithmetic expression and every comparison is a transcription in the reference's own
// operation order, because the phase gate is an exact TC multiset match against it.
//
// PARALLELISATION NOTE (port map section 1 K9b, risk R3). The reference K9 is a SERIAL greedy
// walk whose acceptances depend on what earlier chains claimed, and the port map's propose-verify
// form is NOT proven to reach the same fixed point -- the R3 pre-port divergence measurement was
// never done. P2.3's gate is exact parity, so the claim, the extension and the TC-row assignment
// run in single-thread kernels here, which is bit-exact by construction. Parallelising them is
// P2.6 work and must carry the R3 measurement with it.

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
  // K9-0. The per-chain claim universe: anchor hit + other hit of every member MD, sorted unique.
  // prototype/K9K10.cc builds it with sort + unique over a scratch vector; the chains here are a
  // few dozen entries at most, so an insertion sort in place is both simpler and faster, and it
  // produces the identical set (the ORDER inside the list never enters a decision: the claim tests
  // are counts over the whole list).
  struct ChainBuildClaimHits {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  MiniDoubletsConst mds,
                                  ChainItemsConst items,
                                  Chains chains,
                                  uint32_t* claimHits) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());
      for (uint32_t c : cms::alpakatools::uniform_elements(acc, nChains)) {
        uint32_t const off = chains.nodeOffset()[c];
        uint32_t const mdBase = 3u * off;
        uint32_t const hitBase = 6u * off;
        int const nMD = chains.nMDs()[c];

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
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // K9-1. The K9 order key and the candidate set.
  //
  // Order key (-B 10 -BK 1 -BT 5): score - alpha * max(0, hinge - marginX). It is NEVER a
  // threshold; acceptance always cuts on chains.score (the M9 cross-scale-inversion lesson).
  //
  // Candidate set = the reference's two pre-claim gates, in order:
  //   (1) score >= thetaForChain: with -G 6 the base threshold is kNoCutTheta for every length,
  //       while a chain on the EXEMPT (large-dcaXY) branch is cut by -U4/-U5/-U6 on the legacy
  //       sum-logit scale. A gate-killed chain carries score -= 1e9 and fails both.
  //   (2) the pixel-consumed drop: any member T3 flagged partOfPT5 (when -RT5 leaves that half on)
  //       or partOfPT3 (when -RT3 does). Under the frozen -RT5 1 / -RT3 0 only the partOfPT3 half
  //       is live.
  struct ChainOrderAndSelect {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  ChainItemsConst items,
                                  Chains chains,
                                  ChainConfig cfg) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());
      for (uint32_t c : cms::alpakatools::uniform_elements(acc, nChains)) {
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
        if (score >= thr) {
          bool consumed = false;
          if (cfg.dropPixelConsumed) {
            uint32_t const off = chains.nodeOffset()[c];
            int const nN = chains.nNodes()[c];
            for (int k = 0; k < nN && !consumed; ++k) {
              uint32_t const t3 = nodes.tripletIndex()[items.nodeItems()[off + k]];
              consumed = (triplets.partOfPT5()[t3] && cfg.dropPartOfPT5) ||
                         (triplets.partOfPT3()[t3] && cfg.dropPartOfPT3);
            }
          }
          if (!consumed)
            claim = kChainClaimCandidate;
        }
        chains.claimFlags()[c] = claim;
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // K9a + K9b + K9c. The serial arbitration.
  //
  // Order of business, exactly as prototype/main.cc composes it under -A 4 with attach inert:
  //   1. collect the candidates in ascending chain index, then sort by (orderKey desc, index asc);
  //   2. -PU 1 pre-claim: the SURVIVING carried pixel rows' outer-tracker hits, in TC row order,
  //      first come first served. -RT5 1 has already retired every type-7 row, so the surviving
  //      owners are exactly the carried pT3 rows (a type-8 pLS row owns no outer-tracker hit);
  //   3. the greedy walk with the -F / -FC claim tolerance, its -FBC band variant, and the
  //      owner-relative -W braid kill with its -WE band variant.
  struct ChainArbitrateSerial {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  MiniDoubletsConst mds,
                                  TripletsConst triplets,
                                  SegmentsConst segments,
                                  ChainNodesConst nodes,
                                  ChainItemsConst items,
                                  Chains chains,
                                  uint32_t const* claimHits,
                                  TrackCandidatesBaseConst candsBase,
                                  TrackCandidatesExtendedConst candsExtended,
                                  int32_t* owner,
                                  uint32_t nOwner,
                                  uint32_t* order,
                                  uint32_t* orderScratch,
                                  uint32_t* accepted,
                                  int32_t* braidCount,
                                  uint32_t* braidTouched,
                                  uint32_t braidTouchedCapacity,
                                  ChainConfig cfg) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;

      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());

      // --- 1) candidate list, ascending chain index ------------------------------------------
      uint32_t n = 0;
      for (uint32_t c = 0; c < nChains; ++c)
        if (chains.claimFlags()[c] & kChainClaimCandidate)
          order[n++] = c;

      // Bottom-up merge sort: key desc, index asc. Stable merges over an index-ascending input
      // reproduce prototype/K9K10.cc's std::sort comparator exactly (that comparator is a strict
      // total order, so the sorted sequence is unique and the algorithm choice cannot matter).
      uint32_t* src = order;
      uint32_t* dst = orderScratch;
      for (uint32_t width = 1; width < n; width *= 2u) {
        for (uint32_t lo = 0; lo < n; lo += 2u * width) {
          uint32_t mid = lo + width;
          uint32_t hi = lo + 2u * width;
          if (mid > n)
            mid = n;
          if (hi > n)
            hi = n;
          uint32_t i = lo, j = mid, k = lo;
          while (i < mid && j < hi) {
            uint32_t const a = src[i], b = src[j];
            float const ka = chains.orderKey()[a], kb = chains.orderKey()[b];
            bool const aFirst = (ka != kb) ? (ka > kb) : (a < b);
            dst[k++] = aFirst ? src[i++] : src[j++];
          }
          while (i < mid)
            dst[k++] = src[i++];
          while (j < hi)
            dst[k++] = src[j++];
        }
        uint32_t* tmp = src;
        src = dst;
        dst = tmp;
      }
      if (src != order)
        for (uint32_t i = 0; i < n; ++i)
          order[i] = src[i];

      // --- 2) -PU 1 pre-claim ----------------------------------------------------------------
      for (uint32_t h = 0; h < nOwner; ++h)
        owner[h] = chainarb::kFree;
      if (cfg.preClaim) {
        // The SURVIVING carried rows only: ChainCompactCarriedTCs has already run, so
        // nTrackCandidates is the post-compaction count and the rows above it are stale copies
        // that no longer exist in the output.
        uint32_t const nCarried = candsBase.nTrackCandidates();
        for (uint32_t row = 0; row < nCarried; ++row) {
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
              if (owner[h] != chainarb::kFree)
                continue;
              owner[h] = chainarb::kPixOwner;
            }
          }
        }
      }

      // --- 3) the greedy walk ----------------------------------------------------------------
      for (uint32_t c = 0; c < nChains; ++c)
        braidCount[c] = 0;

      // -FC is given in MD units; the claim universe is hits, so the budget doubles.
      int const maxItems = cfg.maxClaimedMDs >= 0 ? 2 * cfg.maxClaimedMDs : -1;
      int const maxItemsAlt = cfg.claimItemsAltMDs > -2 ? 2 * cfg.claimItemsAltMDs : -2;
      bool const braidOn = cfg.braidFrac > 0.f || cfg.braidFracAlt > 0.f;

      uint32_t nAcc = 0;
      for (uint32_t oi = 0; oi < n; ++oi) {
        uint32_t const c = order[oi];
        uint32_t const hitBase = 6u * chains.nodeOffset()[c];
        int const total = chains.nClaimHits()[c];

        int nClaimed = 0;
        for (int k = 0; k < total; ++k)
          nClaimed += (owner[claimHits[hitBase + k]] != chainarb::kFree) ? 1 : 0;
        float const frac = (total > 0) ? static_cast<float>(nClaimed) / static_cast<float>(total) : 0.f;

        // Band membership (-WZ / -WN): |eta| of the innermost member T3 -- the same coordinate K10
        // writes into the TC -- and the member-node count. A per-chain property, never a
        // proximity test between two tracks.
        bool altBand = false;
        {
          uint32_t const off = chains.nodeOffset()[c];
          int const nN = chains.nNodes()[c];
          if (nN > 0) {
            uint32_t const t3In = nodes.tripletIndex()[items.nodeItems()[off]];
            unsigned int m0, m1, m2;
            chainNodeMDs(triplets, segments, t3In, m0, m1, m2);
            float const aEta = alpaka::math::abs(acc, chainT3Eta(acc, mds, m2));
            altBand = (aEta >= cfg.braidAltEta) && (static_cast<float>(nN) <= cfg.braidAltMaxNodes);
          }
        }

        int const cItems = (altBand && maxItemsAlt != -2) ? maxItemsAlt : maxItems;
        float const cFrac = (altBand && cfg.claimFracAlt > 0.f) ? cfg.claimFracAlt : cfg.maxClaimedFrac;

        // prototype/K9K10.cc claimOkV, same operand order and the same float comparison.
        bool claimOk;
        if (cItems < 0)
          claimOk = !(frac > cFrac);
        else if (cfg.claimCountExclusive)
          claimOk = nClaimed <= cItems;
        else
          claimOk = (nClaimed <= cItems) || !(frac > cFrac);
        if (!claimOk)
          continue;

        float const bFrac = (altBand && cfg.braidFracAlt > 0.f) ? cfg.braidFracAlt : cfg.braidFrac;
        if (braidOn && bFrac > 0.f && nClaimed > 0) {
          uint32_t nTouched = 0;
          for (int k = 0; k < total; ++k) {
            int32_t const o = owner[claimHits[hitBase + k]];
            if (o == chainarb::kFree)
              continue;
            if (o <= chainarb::kPixOwner)
              continue;  // -PU 1: pixel owners do not take part in the braid
            if (braidCount[o] == 0) {
              ALPAKA_ASSERT_ACC(nTouched < braidTouchedCapacity);
              if (nTouched < braidTouchedCapacity)
                braidTouched[nTouched++] = static_cast<uint32_t>(o);
            }
            ++braidCount[o];
          }
          bool killed = false;
          for (uint32_t t = 0; t < nTouched; ++t) {
            uint32_t const slot = braidTouched[t];
            int const oTot = chains.nClaimHits()[slot];
            if (oTot > 0 && static_cast<float>(braidCount[slot]) >= bFrac * static_cast<float>(oTot))
              killed = true;
            braidCount[slot] = 0;
          }
          if (killed)
            continue;
        }

        for (int k = 0; k < total; ++k)
          owner[claimHits[hitBase + k]] = static_cast<int32_t>(c);
        chains.claimFlags()[c] |= kChainClaimAccepted;
        accepted[nAcc++] = c;
      }
      chains.nAccepted() = nAcc;
    }
  };

  // ------------------------------------------------------------------------------------------
  // The claimed-hit map the extension reads.
  //
  // prototype/main.cc assembles it from three supersets: the final K9 owner map, every accepted
  // chain's MD hits, and every SURVIVING carried pixel row's outer-tracker hits. Under the frozen
  // configuration all three collapse onto the first: an accepted chain claimed its own hits inside
  // the walk, the surviving carried rows are exactly the pT3 rows that pre-claimed, and -RT5 1
  // retired every type-7 row before the pre-claim ran. The map is therefore (owner != free).
  struct ChainMarkClaimedHits {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, int32_t const* owner, uint8_t* claimedHit, uint32_t nOwner) const {
      for (uint32_t h : cms::alpakatools::uniform_elements(acc, nOwner))
        claimedHit[h] = (owner[h] != chainarb::kFree) ? 1u : 0u;
    }
  };

  // ------------------------------------------------------------------------------------------
  // MiniDoublet -> outgoing LineSegment CSR, the -EXS 1 adjacency.
  //
  // LST only builds a LineSegment between two MDs that already passed its module map and its
  // segment geometry cuts, so walking these edges asks the detector "is this a legal next hit?"
  // instead of inventing a window. Only outer-tracker segments take part (a pixel segment's inner
  // MD is a pLS pseudo-MD, which the layer test rejects anyway).
  // The segment store is module-segmented with gaps, so both passes walk the module slices; the
  // loop over lower modules alone already excludes every pixel segment.
  struct ChainSegCount {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  SegmentsConst segments,
                                  SegmentsOccupancyConst segmentsOccupancy,
                                  ObjectRangesConst ranges,
                                  uint32_t* counts) const {
      uint32_t const nLowerModules = modules.nLowerModules();
      for (uint32_t mod : cms::alpakatools::uniform_elements(acc, nLowerModules)) {
        uint32_t const base = static_cast<uint32_t>(ranges.segmentModuleIndices()[mod]);
        uint32_t const nSeg = segmentsOccupancy.nSegments()[mod];
        for (uint32_t i = 0; i < nSeg; ++i)
          alpaka::atomicAdd(acc, &counts[segments.mdIndices()[base + i][0]], 1u, alpaka::hierarchy::Threads{});
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

  struct ChainSegScatter {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  SegmentsConst segments,
                                  SegmentsOccupancyConst segmentsOccupancy,
                                  ObjectRangesConst ranges,
                                  uint32_t const* offsets,
                                  uint32_t* cursor,
                                  uint32_t* itemsOut) const {
      uint32_t const nLowerModules = modules.nLowerModules();
      for (uint32_t mod : cms::alpakatools::uniform_elements(acc, nLowerModules)) {
        uint32_t const base = static_cast<uint32_t>(ranges.segmentModuleIndices()[mod]);
        uint32_t const nSeg = segmentsOccupancy.nSegments()[mod];
        for (uint32_t i = 0; i < nSeg; ++i) {
          uint32_t const s = base + i;
          uint32_t const m = segments.mdIndices()[s][0];
          uint32_t const slot = alpaka::atomicAdd(acc, &cursor[m], 1u, alpaka::hierarchy::Threads{});
          itemsOut[offsets[m] + slot] = s;
        }
      }
    }
  };

  // The scatter above is atomic, so a bucket arrives in an arbitrary order. prototype/Extend.cc
  // walks the neighbours in ASCENDING LineSegment index and keeps the first of an exact residual
  // tie, so the buckets are sorted back into that order. Buckets hold ~1 entry on average.
  struct ChainSegSort {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, uint32_t const* offsets, uint32_t* itemsOut, uint32_t nMD) const {
      for (uint32_t m : cms::alpakatools::uniform_elements(acc, nMD)) {
        uint32_t const b = offsets[m], e = offsets[m + 1u];
        for (uint32_t i = b + 1u; i < e; ++i) {
          uint32_t const v = itemsOut[i];
          uint32_t j = i;
          while (j > b && itemsOut[j - 1u] > v) {
            itemsOut[j] = itemsOut[j - 1u];
            --j;
          }
          itemsOut[j] = v;
        }
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // The chain fit the extension extrapolates along: the VERBATIM ChainFeatures / Trim pair (Kasa
  // algebraic circle in mean-centred coordinates plus a straight z against the cumulative xy
  // chord), kept instead of thrown away so a new point can be tested against it without refitting.
  // Transcription of prototype/Extend.cc buildChainFit; anchor hits are promoted to double BEFORE
  // any differencing, which is the P2.2 ported-fit rule.
  struct ChainFit {
    double cx, cy, R;
    double a, b;
    double sLast;
    double xIn, yIn, zIn;
    double xOut, yOut, zOut;
    double chi2;
  };

  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool chainBuildFit(
      TAcc const& acc, MiniDoubletsConst mds, uint32_t const* mdList, int nMD, ChainFit& f) {
    if (nMD < 3)
      return false;

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
    if (!(det > 1e-12 * scale * scale))
      return false;
    double const uc = (Svv * (0.5 * Suw) - Suv * (0.5 * Svw)) / det;
    double const vc = (Suu * (0.5 * Svw) - Suv * (0.5 * Suw)) / det;
    double const R = alpaka::math::sqrt(acc, chainMaxd(uc * uc + vc * vc + Sw / nMD, 0.0));
    double xyChi2 = 0.0;
    for (int k = 0; k < nMD; ++k) {
      double const du = (mds.anchorX()[mdList[k]] - xbar) - uc, dv = (mds.anchorY()[mdList[k]] - ybar) - vc;
      double const resid = alpaka::math::sqrt(acc, du * du + dv * dv) - R;
      xyChi2 += resid * resid;
    }
    xyChi2 /= nMD;

    // Cumulative xy chord length. The reference stages the anchor hits into a double array before
    // differencing, so the subtraction is a double subtraction.
    double s = 0.0, sbar = 0.0, zbar = 0.0;
    for (int k = 0; k < nMD; ++k) {
      if (k > 0) {
        double const dx =
            static_cast<double>(mds.anchorX()[mdList[k]]) - static_cast<double>(mds.anchorX()[mdList[k - 1]]);
        double const dy =
            static_cast<double>(mds.anchorY()[mdList[k]]) - static_cast<double>(mds.anchorY()[mdList[k - 1]]);
        s += alpaka::math::sqrt(acc, dx * dx + dy * dy);
      }
      sbar += s;
      zbar += mds.anchorZ()[mdList[k]];
    }
    double const sLast = s;
    sbar /= nMD;
    zbar /= nMD;
    double Sss = 0.0, Ssz = 0.0;
    s = 0.0;
    for (int k = 0; k < nMD; ++k) {
      if (k > 0) {
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
    if (!(Sss > 1e-12))
      return false;
    double const bb = Ssz / Sss;
    double const aa = zbar - bb * sbar;
    double rzChi2 = 0.0;
    s = 0.0;
    for (int k = 0; k < nMD; ++k) {
      if (k > 0) {
        double const dx =
            static_cast<double>(mds.anchorX()[mdList[k]]) - static_cast<double>(mds.anchorX()[mdList[k - 1]]);
        double const dy =
            static_cast<double>(mds.anchorY()[mdList[k]]) - static_cast<double>(mds.anchorY()[mdList[k - 1]]);
        s += alpaka::math::sqrt(acc, dx * dx + dy * dy);
      }
      double const r = mds.anchorZ()[mdList[k]] - aa - bb * s;
      rzChi2 += r * r;
    }
    rzChi2 /= nMD;

    f.cx = xbar + uc;
    f.cy = ybar + vc;
    f.R = R;
    f.a = aa;
    f.b = bb;
    f.sLast = sLast;
    f.xIn = mds.anchorX()[mdList[0]];
    f.yIn = mds.anchorY()[mdList[0]];
    f.zIn = mds.anchorZ()[mdList[0]];
    f.xOut = mds.anchorX()[mdList[nMD - 1]];
    f.yOut = mds.anchorY()[mdList[nMD - 1]];
    f.zOut = mds.anchorZ()[mdList[nMD - 1]];
    f.chi2 = xyChi2 + rzChi2;
    return true;
  }

  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE double chainDist3(
      TAcc const& acc, double x0, double y0, double z0, double x1, double y1, double z1) {
    double const dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    return alpaka::math::sqrt(acc, dx * dx + dy * dy + dz * dz);
  }

  // std::fabs semantics for doubles, kept local so no backend math overload can substitute a
  // float version of the reference's double comparison.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE double chainAbsd(double v) { return (v < 0.0) ? -v : v; }

  // ------------------------------------------------------------------------------------------
  // EX. Chain extension at assembly (prototype/Extend.cc, -EX 1 -EXW 0.25 -EXR 2.0 -EXS 1).
  //
  // Runs POST-claim, PRE-K10: it only ever changes the emitted hit list / nhitOT / nLayers of a
  // chain that was going to be a TC anyway. Candidates are MDs whose BOTH hits are unclaimed by
  // any DELIVERED object, so an extension can never manufacture an overlap. Chains are visited in
  // K9 accepted (best-first) order and each accepted extension marks its hits claimed, so two
  // chains can never absorb the same free MD -- which is why this is serial.
  struct ChainExtendSerial {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  ChainItems items,
                                  Chains chains,
                                  uint32_t const* accepted,
                                  uint8_t* claimedHit,
                                  uint32_t nHitUniverse,
                                  uint32_t const* segOffsets,
                                  uint32_t const* segItems,
                                  uint32_t nMDall,
                                  uint32_t* stats,
                                  ChainConfig cfg) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      if (cfg.extendMode <= 0)
        return;

      uint32_t const nAcc = chains.nAccepted();
      double const win = static_cast<double>(cfg.extendWindow);
      double const win2 = win * win;

      for (uint32_t ai = 0; ai < nAcc; ++ai) {
        uint32_t const c = accepted[ai];
        if (chains.nLayers()[c] < cfg.extendMinLayers)
          continue;
        uint32_t const mdBase = 3u * chains.nodeOffset()[c];
        int nMD = chains.nMDs()[c];
        if (nMD < 3)
          continue;

        ChainFit fit;
        if (!chainBuildFit(acc, mds, &items.mdItems()[mdBase], nMD, fit)) {
          ++stats[1];  // nNoFit
          continue;
        }
        if (cfg.extendMaxChi2 > 0.f && fit.chi2 > static_cast<double>(cfg.extendMaxChi2)) {
          ++stats[6];  // nRejFit
          continue;
        }
        ++stats[0];  // nChains examined

        uint32_t layerMask = 0u;
        for (int k = 0; k < nMD; ++k)
          layerMask |= (1u << chainMdLayer(modules, mds, items.mdItems()[mdBase + k]));

        // end 0 = outer (append), end 1 = inner (prepend). The frozen -EX 1 runs the outer end
        // only; the inner branch is kept so the flag surface stays complete.
        for (int end = 0; end < 2; ++end) {
          bool const outer = (end == 0);
          if (outer && !(cfg.extendMode == 1 || cfg.extendMode == 3))
            continue;
          if (!outer && !(cfg.extendMode == 2 || cfg.extendMode == 3))
            continue;

          uint32_t tMd = outer ? items.mdItems()[mdBase + nMD - 1] : items.mdItems()[mdBase];
          double tx = outer ? fit.xOut : fit.xIn;
          double ty = outer ? fit.yOut : fit.yIn;
          double tz = outer ? fit.zOut : fit.zIn;
          double sT = outer ? fit.sLast : 0.0;

          for (int rep = 0; rep < cfg.extendMaxPerEnd; ++rep) {
            int const tLay = chainMdLayer(modules, mds, tMd);
            double const ox = outer ? fit.xIn : fit.xOut;
            double const oy = outer ? fit.yIn : fit.yOut;
            double const oz = outer ? fit.zIn : fit.zOut;
            double const dRef = chainDist3(acc, ox, oy, oz, tx, ty, tz);

            int32_t bestMd = -1;
            double bestRes = 1e30, secondRes = 1e30;

            // -EXS 1: only MDs the detector already declared segment-compatible with the terminal.
            uint32_t const nb = outer ? segOffsets[tMd] : 0u;
            uint32_t const ne = outer ? segOffsets[tMd + 1u] : 0u;
            for (uint32_t q = nb; q < ne; ++q) {
              uint32_t const m = segments.mdIndices()[segItems[q]][1];
              if (m >= nMDall)
                continue;
              int const L = chainMdLayer(modules, mds, m);
              if (L < 1 || L >= kChainMaxMdLayer)
                continue;
              int const jump = outer ? (L - tLay) : (tLay - L);
              if (jump < 1 || jump > cfg.extendMaxJump)
                continue;
              if (layerMask & (1u << L))
                continue;  // the chain already occupies this layer: no length to gain
              unsigned int const ha = mds.anchorHitIndices()[m], hb = mds.outerHitIndices()[m];
              if (ha >= nHitUniverse || hb >= nHitUniverse)
                continue;
              if (claimedHit[ha] || claimedHit[hb])
                continue;  // owned by a delivered object, or taken by an earlier extension
              double const mx = mds.anchorX()[m], my = mds.anchorY()[m], mz = mds.anchorZ()[m];
              if (chainDist3(acc, tx, ty, tz, mx, my, mz) > static_cast<double>(cfg.extendMaxDist))
                continue;
              if (chainDist3(acc, ox, oy, oz, mx, my, mz) <= dRef)
                continue;  // not beyond the terminal -> not an extension
              ++stats[2];  // nCand
              double const dcx = mx - fit.cx, dcy = my - fit.cy;
              double const rxy = alpaka::math::sqrt(acc, dcx * dcx + dcy * dcy) - fit.R;
              double const chord = alpaka::math::sqrt(acc, (mx - tx) * (mx - tx) + (my - ty) * (my - ty));
              double const sC = outer ? (sT + chord) : (sT - chord);
              double const rrz = mz - (fit.a + fit.b * sC);
              double res;
              if (cfg.extendRzWindow > 0.f) {
                if (chainAbsd(rxy) > win || chainAbsd(rrz) > static_cast<double>(cfg.extendRzWindow))
                  continue;
                res = chainAbsd(rxy);
              } else {
                res = alpaka::math::sqrt(acc, rxy * rxy + rrz * rrz);
                if (res > win)
                  continue;
              }
              if (res < bestRes) {
                secondRes = bestRes;
                bestRes = res;
                bestMd = static_cast<int32_t>(m);
              } else if (res < secondRes) {
                secondRes = res;
              }
            }
            if (bestMd < 0)
              break;

            if (cfg.extendUniqMargin > 0.f && secondRes < 1e29 &&
                (secondRes - bestRes) < static_cast<double>(cfg.extendUniqMargin)) {
              ++stats[4];  // nRejUniq
              break;
            }

            // The REFIT combined chi2/hit over the enlarged MD list must stay within chi2Factor of
            // the original, with an absolute floor of window^2 so a numerically perfect chain is
            // not barred from ever extending. The candidate is written into the slot past the
            // chain's used MD range for the test; a rejected candidate simply leaves a dead entry
            // there, still inside the chain's own 3 * nNodes allocation and never read again.
            if (nMD + 1 > 3 * static_cast<int>(chains.nNodes()[c]))
              break;
            items.mdItems()[mdBase + nMD] = static_cast<uint32_t>(bestMd);
            double const chi2New = chainFitChi2Combined(acc, mds, &items.mdItems()[mdBase], nMD + 1);
            if (chi2New > static_cast<double>(cfg.extendChi2Factor) * chainMaxd(fit.chi2, win2)) {
              ++stats[5];  // nRejChi2
              break;
            }

            claimedHit[mds.anchorHitIndices()[bestMd]] = 1u;
            claimedHit[mds.outerHitIndices()[bestMd]] = 1u;
            layerMask |= (1u << chainMdLayer(modules, mds, bestMd));
            ++nMD;
            chains.nMDs()[c] = static_cast<uint16_t>(nMD);
            chains.nLayers()[c] = static_cast<uint8_t>(chains.nLayers()[c] + 1);
            ++stats[3];  // nExtOuter

            double const nx = mds.anchorX()[bestMd], ny = mds.anchorY()[bestMd];
            double const chordAcc = alpaka::math::sqrt(acc, (nx - tx) * (nx - tx) + (ny - ty) * (ny - ty));
            sT = outer ? (sT + chordAcc) : (sT - chordAcc);
            tMd = static_cast<uint32_t>(bestMd);
            tx = nx;
            ty = ny;
            tz = mds.anchorZ()[bestMd];
          }
        }
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // The -RT5 1 wholesale drop of the carried type-7 rows.
  //
  // The whole baseline TC pipeline runs UNCHANGED first, so CrossCleanpT3 / CrossCleanT5 /
  // CrossCleanT4 / CrossCleanpLS all see exactly the collection they see at baseline and the
  // carried pT3 and bare-pLS rows are bit-identical to what LST builds. Only afterwards are the
  // classes the chains replace removed: type-7 by -RT5 1, and the LST T5 / T4 rows because the
  // chain pipeline IS the outer-tracker builder in this mode. Compaction is stable, so the
  // surviving rows keep their relative order -- which is the order the -PU pre-claim walks.
  struct ChainCompactCarriedTCs {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  ChainConfig cfg) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t const nIn = candsBase.nTrackCandidates();
      uint32_t w = 0;
      for (uint32_t r = 0; r < nIn; ++r) {
        LSTObjType const ty = candsBase.trackCandidateType()[r];
        bool keep = false;
        if (ty == LSTObjType::pT3)
          keep = !cfg.replacePT3;
        else if (ty == LSTObjType::pLS)
          keep = true;
        else if (ty == LSTObjType::pT5)
          keep = !cfg.replacePT5;
        // LSTObjType::T5 and LSTObjType::T4 are the classes the chain pipeline replaces outright.
        if (!keep)
          continue;
        if (w != r) {
          candsBase.trackCandidateType()[w] = candsBase.trackCandidateType()[r];
          candsBase.pixelSeedIndex()[w] = candsBase.pixelSeedIndex()[r];
          candsExtended.directObjectIndices()[w] = candsExtended.directObjectIndices()[r];
          candsExtended.objectIndices()[w][0] = candsExtended.objectIndices()[r][0];
          candsExtended.objectIndices()[w][1] = candsExtended.objectIndices()[r][1];
          for (int s = 0; s < Params_TC::kLayers; ++s) {
            candsExtended.logicalLayers()[w][s] = candsExtended.logicalLayers()[r][s];
            candsExtended.lowerModuleIndices()[w][s] = candsExtended.lowerModuleIndices()[r][s];
            candsBase.hitIndices()[w][s][0] = candsBase.hitIndices()[r][s][0];
            candsBase.hitIndices()[w][s][1] = candsBase.hitIndices()[r][s][1];
          }
        }
        ++w;
      }
      candsBase.nTrackCandidates() = w;
      candsExtended.nTrackCandidatespT5() = 0u;
      candsExtended.nTrackCandidatesT5() = 0u;
      candsExtended.nTrackCandidatesT4() = 0u;
      if (cfg.replacePT3)
        candsExtended.nTrackCandidatespT3() = 0u;
    }
  };

  // ------------------------------------------------------------------------------------------
  // K10, first half. Give every accepted chain that will emit a TC its output row, in the K9
  // accepted (best-first) order, so the row assignment is deterministic and free of atomics.
  struct ChainAssignTCRows {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  Chains chains,
                                  uint32_t const* accepted,
                                  uint32_t nAllocated) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t const nAcc = chains.nAccepted();
      uint32_t row = candsBase.nTrackCandidates();
      uint32_t nT5 = 0, nT4 = 0, nPT5 = 0;
      for (uint32_t ai = 0; ai < nAcc; ++ai) {
        uint32_t const c = accepted[ai];
        int const nL = chains.nLayers()[c];
        if (nL < kChainTCMinLayers)
          continue;  // too short for a TC without pixel help
        if (row >= nAllocated)
          break;
        chains.tcRow()[c] = static_cast<int32_t>(row++);
        // P2.4: an accepted chain that won a pLS is UPGRADED in place, so it is counted in the
        // pT5 class rather than in T5 -- no extra row is ever created by the attach.
        if (chains.attachPls()[c] >= 0)
          ++nPT5;
        else
          (nL >= 5 ? nT5 : nT4) += 1;
      }
      chains.nChainTCs() = row - candsBase.nTrackCandidates();
      candsBase.nTrackCandidates() = row;
      candsExtended.nTrackCandidatesT5() = nT5;
      candsExtended.nTrackCandidatesT4() = nT4;
      // ADD, not assign: ChainSuppressCarriedTCs has already counted whatever carried pT5 rows
      // survived (none under the frozen -RT5 1), and the attach upgrades are additional.
      candsExtended.nTrackCandidatespT5() = candsExtended.nTrackCandidatespT5() + nPT5;
    }
  };

  // t3_phi as the ntuple writer computes it (standalone/code/core/lst_math.h Hit::phi over the
  // anchor hit of the triplet's FIRST MD): Phi_mpi_pi(M_PI + ATan2(-y, -x)), with the ATan2 zero-x
  // special case and the float narrowing at the Phi_mpi_pi call reproduced exactly.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainHitPhi(TAcc const& acc, float x, float y) {
    float at;
    if (x != 0.f)
      at = alpaka::math::atan2(acc, -y, -x);
    else if (y == 0.f)
      at = 0.f;
    else
      at = (-y > 0.f) ? static_cast<float>(chainarb::kPi / 2.0) : static_cast<float>(-chainarb::kPi / 2.0);
    float p = static_cast<float>(chainarb::kPi + static_cast<double>(at));
    while (static_cast<double>(p) >= chainarb::kPi)
      p = static_cast<float>(static_cast<double>(p) - 2.0 * chainarb::kPi);
    while (static_cast<double>(p) < -chainarb::kPi)
      p = static_cast<float>(static_cast<double>(p) + 2.0 * chainarb::kPi);
    return p;
  }

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
