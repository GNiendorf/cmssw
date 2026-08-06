#ifndef RecoTracker_LSTCore_src_alpaka_ChainAttachT3_h
#define RecoTracker_LSTCore_src_alpaka_ChainAttachT3_h

#include <bit>
#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainConfig.h"
#include "RecoTracker/LSTCore/interface/ChainsSoA.h"
#include "RecoTracker/LSTCore/interface/ChainNodesSoA.h"
#include "RecoTracker/LSTCore/interface/LSTInputSoA.h"
#include "RecoTracker/LSTCore/interface/MiniDoubletsSoA.h"
#include "RecoTracker/LSTCore/interface/SegmentsSoA.h"
#include "RecoTracker/LSTCore/interface/TripletsSoA.h"

#include "ChainArbitrate.h"
#include "ChainAttach.h"
#include "ChainEdges.h"

// =====================================================================================
// STAGE B of the general attach: BARE-T3 TARGETS -- the pT3-class DELIVERY path.
//
// CHAINFINAL2 production form (promoted from the P2.4b-1 measurement probe): stage B runs
// whenever the chain master switch is on, writes the LIVE pLS ownership array (one pLS, one
// owner, across target kinds -- invariant I1), records the bare-T3 retirement evidence in its
// OWN key array (plsBestT3, read by the -RPS predicate against the -AT3 bar), admits targets
// through the -T3F fake-score gate, and delivers one type-5 (pT3-class) TC per owner through
// the -CC hit-overlap contention sweep (ChainT3CCPreclaim + ChainT3CCSweepEmit below), which
// revokes a delivery whose MDs are already claimed by an emitted chain TC or by an earlier
// delivery (-CCN 1 at MD granularity) and applies the -CCR 2 release: the seed's ownership AND
// its bare-T3 evidence are both cleared, so its carried type-8 row genuinely survives.
//
// REFERENCE (frozen): prototype/AttachDelivery.cc gaStageT3 + prototype/PixelAttach.cc
// makeT3Pre / makeT3PreGeom / k8BuildBareT3Mask + the -RT3 / -CC blocks of prototype/main.cc
// (winner config CHAINFINAL2: -T3E 1 -T3F 0.10 -AT3 6.0 -CC 1 -CCG 1 -CCN 1 -CCK 0 -CCP 1
// -CCR 2 -RDT follow -RD).
//
// ELIGIBILITY (prototype/PixelAttach.h:107-119, transcribed): a BARE T3 is a T3 that is
// NOT a member of ANY K9-ACCEPTED chain. The mask is built AFTER arbitration and against
// the ACCEPTED set, not the welded set -- a T3 welded into a chain K9 then REJECTED is
// still bare and still attachable, and that is exactly the pT3-class population LST
// recovers with its superbin machinery. No other filter: in particular partOfPT3 /
// partOfPT5 triplets stay in the universe, because those are the tracks the general
// attach is meant to deliver itself.
//
// =====================================================================================
// WHY THE GRID STAYS A SUPERSET FOR BARE-T3 TARGETS
// =====================================================================================
// The proof in ChainAttach.h has three axes and only one of them mentions the target
// population at all:
//
//  (1) tanLambda. Cells of width prefDTanL over a clamped [-30, 30]; a pLS occupies its
//      own single cell, a target scans every cell overlapping its own window. This is a
//      statement about the pLS scatter and the target's window width. NOTHING about the
//      target kind enters. A bare T3's tanLambda is the same quantity on the same scale
//      (the Features.cc dz02/ds02 node convention), so the argument transfers verbatim.
//
//  (2) r. Fixed 16 cm bins used ONLY as a bucketing device: for every bin the kernel
//      MEASURES the min and max rtInner of the targets that actually landed in it, and
//      scatters each pLS against that measured hull. The bin EDGES never enter the
//      argument. This is the axis where the bare-T3 population differs -- a 3-layer
//      target's innermost anchor sits on any layer 1..11, so the rtInner distribution is
//      much broader and much flatter than the 5+-layer chains' -- but the argument does
//      not care WHAT the distribution is, only that the hull is measured over exactly the
//      targets the pLS will be compared with in that bin. That is why the bounds kernel
//      is re-run over the bare-T3 target array into ITS OWN rMin/rMax buffers, giving a
//      second, independent grid. A wider hull costs candidates (a pLS occupies more phi
//      cells), never correctness.
//
//  (3) phi. For pLS p and r bin j the kernel computes the EXACT range of phiDir_p over
//      [rMin_j, rMax_j] and occupies every phi cell that range touches; the range is
//      exact because phiDir_p is monotone in r (the derivation in ChainAttach.h is a
//      property of the pLS helix ALONE -- d beta / d r = s r / (d h), constant sign --
//      and contains no target quantity whatsoever). A target scans every cell overlapping
//      [chordPhi +- prefDPhi]. Since rtInner is inside [rMin_j, rMax_j], phiDir_p(rtInner)
//      is inside the scattered range; if the pair passes the window, phiDir_p(rtInner) is
//      inside the scanned interval; so the cell that contains it is both occupied and
//      scanned. SUPERSET, no slack term.
//
// So the ONLY thing the bare-T3 geometry changes is the measured hull of axis (2), and
// the hull is measured, not assumed. The construction is therefore superset-safe by the
// same argument, with the same kAttachPhiPad covering float rounding. This is an
// ARGUMENT, not a proof of the implementation: the ChainAttachAudit kernel of
// ChainAttach.h is re-run over the bare-T3 target array and its MISSING counter must be
// 0 on every event before any of the numbers below may be believed.
// =====================================================================================

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  namespace chainattacht3 {
    // Diagnostic counters for stage B (never read by a decision):
    //   0 (unused)  1 candidates iterated   2 pairs scored   3 per-target picks
    //   4 delivered type-5 rows   5 -RDT revocations   6 TC-row overflows
    //   7 seed-dedup owner-slot overflows   8 targets that saw at least one scored pair
    //   9 -CC revocations   10 grid candidates skipped as a repeat inside one target's cell walk
    //  11 pairs whose logit reached the class margin
    constexpr uint32_t kStats = 12u;
  }  // namespace chainattacht3

  // The bare-T3 target kind's value of head input 18 (prototype/PixelAttach.h kAttachTargetT3).
  constexpr float kAttachTargetTypeT3 = 1.f;

  // ------------------------------------------------------------------------------------------
  // K8B-0a. Mark every triplet consumed by a K9-ACCEPTED chain.
  struct ChainAttachT3MarkConsumed {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  ChainItemsConst items,
                                  uint32_t const* accepted,
                                  uint8_t* consumed) const {
      uint32_t const nAcc = chains.nAccepted();
      for (uint32_t ai : cms::alpakatools::uniform_elements(acc, nAcc)) {
        uint32_t const c = accepted[ai];
        uint32_t const base = chains.nodeOffset()[c];
        uint32_t const n = chains.nNodes()[c];
        for (uint32_t k = 0; k < n; ++k)
          consumed[items.nodeItems()[base + k]] = 1u;
      }
    }
  };

  // K8B-0b. keep[] for the bare-T3 selection (the CSR idiom of every other selection here).
  //
  // The -T3F target admission (winner 0.10) is applied HERE, to the mask, so cut targets never
  // reach the candidate finder, never get scored, and never write plsBestT3 (reference
  // AttachDelivery.cc:132-144). The NaN-rejecting form !(x <= maxFake) is deliberate. Node
  // feature 12 IS triplets.fakeScore(), computed at T3 build time -- no new dependency.
  // (The probe-era maxClaimed pre-filter was NOT in the signed-off config and is deleted; the
  // delivered-duplicate control is the -CC contention sweep below.)
  struct ChainAttachT3Keep {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint8_t const* consumed,
                                  ChainNodesConst nodes,
                                  float maxFake,
                                  uint32_t* keep,
                                  uint32_t nNodes) const {
      for (uint32_t n : cms::alpakatools::uniform_elements(acc, nNodes)) {
        keep[n] = (consumed[n] || !(nodes.features()[n][12] <= maxFake)) ? 0u : 1u;
      }
    }
  };

  struct ChainAttachT3Scatter {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint32_t const* keep,
                                  uint32_t const* offs,
                                  uint32_t nNodes,
                                  uint32_t* targets) const {
      for (uint32_t n : cms::alpakatools::uniform_elements(acc, nNodes))
        if (keep[n])
          targets[offs[n]] = n;
    }
  };

  // ------------------------------------------------------------------------------------------
  // K8B-0c. The per-target record of a BARE T3, field for field prototype/PixelAttach.cc
  // makeT3Pre + makeT3PreGeom.
  //
  //   rtInner / zInner : md0's anchor rt / z
  //   chordPhi         : atan2 of (md1 anchor - md0 anchor), in FLOAT (the reference's T3 path
  //                      differs from its chain path here, which accumulates in double)
  //   tanLambda        : dz02 / ds02 over the anchors -- the Features.cc node convention, read
  //                      straight off node feature 2 so it cannot drift from the production
  //                      feature contract (src/alpaka/ChainEdges.h:363 computes exactly
  //                      c02z / max(c02xy, kEps))
  //   fitKappa         : node feature 0 = rotSign / max(cleanRadius, kEps), with rotSign the
  //                      sign of cross(md0->md1, md1->md2)_z -- identical to makeT3Pre
  //   rotSign          : sign(fitKappa), the same recovery ChainAttachTargetPre uses
  //   innermostLayer   : node feature 9 = md_layer[md0]
  //   nLayers          : 3 by construction
  //   gateLogit        : 0 -- no chain gate exists for a bare T3; feature 18 flags the absence
  //   centre           : the T3's own circle-fit centre; non-finite -> centerValid 0
  //
  // `chain` carries the SPARSE TRIPLET INDEX (not a chain row) so the host dump can name the
  // target in the numbering the --allobj ntuple's t3_rawIdx branch uses.
  struct ChainAttachT3TargetPre {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  uint32_t const* targets,
                                  uint32_t nTargets,
                                  AttachTargetPre* out) const {
      for (uint32_t t : cms::alpakatools::uniform_elements(acc, nTargets)) {
        uint32_t const node = targets[t];
        uint32_t const t3 = nodes.tripletIndex()[node];
        uint32_t const ls0 = triplets.segmentIndices()[t3][0];
        uint32_t const ls1 = triplets.segmentIndices()[t3][1];
        uint32_t const md0 = segments.mdIndices()[ls0][0];
        uint32_t const md1 = segments.mdIndices()[ls0][1];
        (void)ls1;

        AttachTargetPre o;
        o.chain = t3;
        o.tcEta = 0.f;  // chain-target-only fields (the -XC pass-1 window); unused for bare T3s
        o.tcPhi = 0.f;
        float const x0 = mds.anchorX()[md0], y0 = mds.anchorY()[md0];
        o.rtInner = alpaka::math::sqrt(acc, x0 * x0 + y0 * y0);
        o.zInner = mds.anchorZ()[md0];
        o.chordPhi = alpaka::math::atan2(acc, mds.anchorY()[md1] - y0, mds.anchorX()[md1] - x0);
        o.tanLambda = nodes.features()[node][2];
        o.fitKappa = nodes.features()[node][0];
        o.rotSign = (o.fitKappa >= 0.f) ? 1.f : -1.f;

        float const cx = triplets.centerX()[t3], cy = triplets.centerY()[t3];
        o.centerX = 0.f;
        o.centerY = 0.f;
        o.centerValid = 0u;
        if (!chainIsNan(cx) && !chainIsInf(cx) && !chainIsNan(cy) && !chainIsInf(cy)) {
          o.centerX = cx;
          o.centerY = cy;
          o.centerValid = 1u;
        }

        o.xs[0] = attachStdz<7>(acc, o.fitKappa);
        o.xs[1] = attachStdz<8>(acc, o.tanLambda);
        o.xs[2] = attachStdz<9>(acc, nodes.features()[node][9]);  // innermostLayer
        o.xs[3] = attachStdz<10>(acc, 3.f);                       // nLayers
        o.xs[4] = attachStdz<11>(acc, 0.f);                       // no chain gate
        out[t] = o;
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // K8B-b. The stage-B scorer.
  //
  // This is ChainAttachScore with three deltas, and it is a SEPARATE struct on purpose: the
  // shipped kernel must not gain a branch or an argument for a measurement that is off by
  // default, so ChainAttach.h is left byte-for-byte alone.
  //   (1) head input 18 is overwritten with the bare-T3 target type AFTER attachEvalPairX has
  //       filled the frozen 19; every other input, and the predicate itself, is the shared code.
  //   (2) prototype/AttachDelivery.cc gaStageT3 updates plsBestT3 for EVERY scored pair and
  //       only THEN skips a pLS stage A already owns, so a stage-A-owned pLS can never become a
  //       bare T3's pick. Both halves are reproduced in that order.
  // plsBest here is the SEPARATE bare-T3 evidence key array (plsBestT3): the -RPS predicate reads
  // it against the -AT3 bar, and the -CC revoke (-CCR 2) erases entries in it.
  struct ChainAttachT3Score {
    template <typename TAcc>
    ALPAKA_FN_ACC void operator()(TAcc const& acc,
                                  AttachTargetPre const* tgt,
                                  uint32_t nTargets,
                                  uint32_t const* offsets,
                                  AttachPlsPre const* items,
                                  uint8_t const* plsOwned,
                                  int32_t* tgtPls,
                                  float* tgtLogit,
                                  uint32_t* plsBest,
                                  uint32_t* stats,
                                  float theta,
                                  ChainConfig cfg) const {
      constexpr int kB = cms::alpakatools::requires_single_thread_per_block_v<TAcc> ? kAttachScoreBatch : 1;
      constexpr int kIn = dnn::attachmlp::kInput;

      alignas(64) float xT[kIn * kB];
      int32_t rowB[kB];
      float logits[kB];
      for (int i = 0; i < kIn * kB; ++i)
        xT[i] = 0.f;
      float const t3Type = attachStdz<18>(acc, kAttachTargetTypeT3);

      for (uint32_t t : cms::alpakatools::uniform_elements(acc, nTargets)) {
        AttachTargetPre const cp = tgt[t];
        int32_t bestPls = -1;
        float bestLogit = kAttachNoLogit;
        uint32_t nCand = 0, nScored = 0, nDup = 0, nOverTheta = 0;
        int nb = 0;

        auto flush = [&]() {
          attachHeadBatch<kB>(xT, logits);
          for (int b = 0; b < nb; ++b) {
            float const lo = logits[b];
            int32_t const p = rowB[b];
            alpaka::atomicMax(
                acc, &plsBest[static_cast<uint32_t>(p)], attachOrderFloat(lo), alpaka::hierarchy::Threads{});
            if (lo >= theta)
              ++nOverTheta;
            if (plsOwned[static_cast<uint32_t>(p)] != 0u)
              continue;  // stage A owns it: one pLS, one owner, across all target types
            if (lo < theta)
              continue;
            if (bestPls < 0 || lo > bestLogit || (lo == bestLogit && p < bestPls)) {
              bestPls = p;
              bestLogit = lo;
            }
          }
          nScored += static_cast<uint32_t>(nb);
          nb = 0;
        };

        int const rb = attachRBin(cp.rtInner);
        int const tbLo = attachTanLBin(cp.tanLambda - cfg.attachPrefDTanL, cfg.attachPrefDTanL);
        int const tbHi = attachTanLBin(cp.tanLambda + cfg.attachPrefDTanL, cfg.attachPrefDTanL);
        int const pbLo = attachPhiBin(cp.chordPhi - cfg.attachPrefDPhi);
        int const pbHi = attachPhiBin(cp.chordPhi + cfg.attachPrefDPhi);
        int nPb = pbHi - pbLo;
        if (nPb < 0)
          nPb += kAttachPhiBins;
        ++nPb;
        if (nPb > kAttachPhiBins)
          nPb = kAttachPhiBins;

        for (int tb = tbLo; tb <= tbHi; ++tb) {
          for (int k = 0; k < nPb; ++k) {
            int const pb = (pbLo + k) % kAttachPhiBins;
            uint32_t const earlier = (1u << k) - 1u;
            uint32_t const cell = attachCellId(rb, tb, pb);
            uint32_t const b = offsets[cell], e = offsets[cell + 1u];
            for (uint32_t i = b; i < e; ++i) {
              AttachPlsPre const& pp = items[i];
              uint32_t const p = pp.row;
              ++nCand;
              uint32_t const m = pp.phiMask;
              uint32_t const rot = ((m >> pbLo) | (m << (kAttachPhiBins - pbLo))) & (kAttachPhiCellMask);
              if ((rot & earlier) != 0u) {
                ++nDup;
                continue;
              }
              float const dTanL = pp.tanLambda - cp.tanLambda;
              if (!attachEvalPairX(acc, pp, cp, dTanL, cfg, xT + nb, kB))
                continue;
              xT[18 * kB + nb] = t3Type;  // the ONLY feature that differs by target kind
              rowB[nb] = static_cast<int32_t>(p);
              ++nb;
              if (nb == kB)
                flush();
            }
          }
        }
        if (nb > 0)
          flush();

        tgtPls[t] = bestPls;
        tgtLogit[t] = bestLogit;
        alpaka::atomicAdd(acc, &stats[1], nCand, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[2], nScored, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[10], nDup, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[11], nOverTheta, alpaka::hierarchy::Threads{});
        if (nScored > 0)
          alpaka::atomicAdd(acc, &stats[8], 1u, alpaka::hierarchy::Threads{});
        if (bestPls >= 0)
          alpaka::atomicAdd(acc, &stats[3], 1u, alpaka::hierarchy::Threads{});
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // K8B-c. Stage-B contention and the stage-B half of the -RD seed-family dedup.
  //
  // prototype/AttachDelivery.cc gaStageT3 compacts to the BIDDING targets and resolves them in
  // ascending T3-row order (its resolveContention keeps the earlier position on a tie); the
  // target array here is already ascending in dense node index, which is ascending in triplet
  // row, so the compaction is the same sequence. prototype/main.cc:3524-3546 then runs the -RD
  // dedup over the stage-B owners ordered by (logit desc, T3 row asc) AGAINST THE SAME hash
  // table stage A left behind -- which is what stops a track already delivered as a pT5-class
  // object from being delivered again as a pT3-class one by a sibling seed.
  //
  // Decomposed exactly like stage A's parallel form (ChainParallel.h T7): the contention is the
  // argmax over the packed (logit, earlier position) key -- ChainAttachArgmax runs unchanged on
  // the raw arrays -- and the -RDT visiting order is a rank count under a strict total order, so
  // the reference's selection sort and the rank produce the same permutation element for
  // element. Only the hash-table walk itself stays sequential (ChainAttachT3Dedup). This one
  // decomposition runs on BOTH backends: on the serial backend it costs what the old walk cost,
  // and on a device it removes the single most expensive kernel of the whole chain pass
  // (an O(n^2) selection sort chased through global memory by one thread, ~15 ms/event).

  // K8B-c1. Zap every position that did not win its pLS's argmax; keep[] flags the winners for
  // the CSR gather. Identical verdicts to the reference walk (strictly-greater displaces, the
  // earlier position keeps a tie).
  struct ChainAttachT3Resolve {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  int32_t* tgtPls,
                                  float* tgtLogit,
                                  uint32_t nTargets,
                                  uint64_t const* plsKey,
                                  uint32_t* keep) const {
      for (uint32_t pos : cms::alpakatools::uniform_elements(acc, nTargets)) {
        int32_t const p = tgtPls[pos];
        if (p >= 0 && plsKey[static_cast<uint32_t>(p)] != attachContendKey(tgtLogit[pos], pos)) {
          tgtPls[pos] = -1;
          tgtLogit[pos] = kAttachNoLogit;
        }
        keep[pos] = (tgtPls[pos] >= 0) ? 1u : 0u;
      }
    }
  };

  // K8B-c2. Stage each owner's pLS row and DISTINCT pixel hit rows at its gather slot, so the
  // serial walk below touches compact arrays only (the ChainAttachOwnerHits idiom). The visiting
  // order IS the gather order -- ascending position == ascending T3 row -- per the zero-sorts
  // directive (the old (logit desc, pos asc) rank count is gone; simp change 3, NONEXACT,
  // n300-gated).
  struct ChainAttachT3StageOwners {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  PixelSeedsConst pixelSeeds,
                                  HitsBaseConst hitsBase,
                                  int32_t const* tgtPls,
                                  uint32_t const* ownersIn,
                                  uint32_t const* nOwnersPtr,
                                  uint32_t nBound,
                                  uint32_t nHits,
                                  int32_t* ownerPls,
                                  uint32_t* ownerHits,
                                  uint8_t* ownerNHits) const {
      uint32_t const n = *nOwnersPtr;
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (i >= n)
          continue;
        uint32_t const a = ownersIn[i];
        int32_t const p = tgtPls[a];
        ownerPls[i] = p;
        int nh = 0;
        if (p >= 0) {
          uint32_t const first = pixelSeeds.firstHit()[p];
          uint32_t const nSeedHits = static_cast<uint32_t>(pixelSeeds.nHits()[p]);
          uint32_t const nStored = nSeedHits < kMaxPLSHitsInHitsSoA ? nSeedHits : kMaxPLSHitsInHitsSoA;
          for (uint32_t k = 0; k < nStored; ++k) {
            uint32_t const h = first + k;
            if (h >= nHits)
              continue;
            if (hitsBase.detid()[h] != kPixelModuleId)
              continue;
            ownerHits[static_cast<size_t>(i) * kMaxPLSHitsInHitsSoA + nh] = hitsBase.idxs()[h];
            ++nh;
          }
        }
        ownerNHits[i] = static_cast<uint8_t>(nh);
      }
    }
  };

  // K8B-c3, the serial residue: the -RDT hash walk in gather (ascending T3 row) order against the table stage A
  // left behind, then the publish of the surviving owners into the LIVE plsOwned (invariant I1
  // -- a stage-B owner is visible to the -XC anchor set and to the carried-row retirement, and
  // the -CC sweep can still release it, -CCR 2). keep[] is maintained through the revocations:
  // after this kernel it flags exactly the DELIVERIES, which is what the -CC sweep's gather
  // reads.
  struct ChainAttachT3Dedup {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint32_t const* order,
                                  uint32_t const* nOwnersPtr,
                                  int32_t const* ownerPls,
                                  uint32_t const* ownerHits,
                                  uint8_t const* ownerNHits,
                                  int32_t* tgtPls,
                                  float* tgtLogit,
                                  uint32_t* keep,
                                  uint8_t* plsOwned,
                                  uint32_t* hashKey,
                                  int32_t* hashVal,
                                  uint32_t* stats,
                                  uint8_t seedDedup) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t const nOwners = *nOwnersPtr;
      uint32_t nIns = 0;
      for (uint32_t i = 0; i < nOwners; ++i) {
        int32_t const p = ownerPls[i];
        if (p < 0)
          continue;
        if (seedDedup) {
          uint32_t const* g = ownerHits + static_cast<size_t>(i) * kMaxPLSHitsInHitsSoA;
          int const nh = ownerNHits[i];

          bool dup = false;
          int32_t seen[chainattach::kSeedDedupSeenMax];
          int nSeen = 0;
          for (int a = 0; a < nh && !dup; ++a) {
            uint32_t slot = attachSeedHash(g[a]);
            while (hashKey[slot] != chainattach::kSeedHashEmpty) {
              if (hashKey[slot] == g[a]) {
                int32_t const q = hashVal[slot];
                for (int u = 0; u < nSeen; ++u)
                  if (seen[u] == q) {
                    dup = true;
                    break;
                  }
                if (dup)
                  break;
                if (nSeen < chainattach::kSeedDedupSeenMax)
                  seen[nSeen++] = q;
              }
              slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
            }
          }

          if (dup) {
            uint32_t const pos = order[i];
            tgtPls[pos] = -1;
            tgtLogit[pos] = kAttachNoLogit;
            keep[pos] = 0u;
            ++stats[5];
            continue;
          }
          for (int a = 0; a < nh; ++a) {
            if (nIns + 1u >= chainattach::kSeedHashSlots / 2u) {
              ++stats[7];
              break;
            }
            uint32_t slot = attachSeedHash(g[a]);
            while (hashKey[slot] != chainattach::kSeedHashEmpty)
              slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
            hashKey[slot] = g[a];
            hashVal[slot] = p;
            ++nIns;
          }
        }
        plsOwned[static_cast<uint32_t>(p)] = 1u;
      }
    }
  };

  // ==========================================================================================
  // -CC: the hit-overlap contention on the stage-B deliveries, FUSED with the type-5 emission
  // (the reference's one loop, main.cc:4293-4489). Ownership-map based, never pairwise: each
  // delivery looks up ITS OWN 3 MDs in one claim map and decides alone.
  // ==========================================================================================

  // -CCP 1 pre-claim: every EMITTED chain TC (bare and attach-upgraded alike) claims its deduped
  // MD union -- the POST-extension mdItems CSR, matching the reference's ordering (EX before -CC).
  // The surviving carried pixel rows of the reference contribute nothing here: replacePT5 and
  // replacePT3 dropped them all, so the hit2md inversion machinery is not needed.
  struct ChainT3CCPreclaim {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  ChainItemsConst items,
                                  uint8_t* ccClaimed) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());
      for (uint32_t c : cms::alpakatools::uniform_elements(acc, nChains)) {
        if (chains.tcRow()[c] < 0)
          continue;  // not emitted (K9-rejected, too short, -CCS-suppressed, or out of rows)
        uint32_t const mdBase = 3u * chains.nodeOffset()[c];
        int const nMD = chains.nMDs()[c];
        for (int k = 0; k < nMD; ++k)
          ccClaimed[items.mdItems()[mdBase + k]] = 1u;
      }
    }
  };

  // The type-5 (pT3-class) rows. prototype/main.cc:4470-4488 assembles exactly this object: the
  // seed's DISTINCT pixel hit rows followed by the T3's three MDs (six outer-tracker hits).
  //
  // directObjectIndices is set to the sentinel kBareT3TCMarker because the row is backed by NO
  // PixelTriplets entry; objectIndices carries (pLS row, sparse triplet row) instead, which is
  // what the harness reads to recover the kinematics. chains.nChainTCs() is advanced by the number
  // of rows emitted so the harness's isChainTCRow test covers them and skips every object-index
  // map that does not apply to them.
  static constexpr uint32_t kBareT3TCMarker = 0xFFFFFFFFu;

  // -CC sweep + emission, one serial kernel:
  //   (1) the deliveries (tgtPls >= 0) are walked in ASCENDING position order == ascending T3
  //       row, PRE-GATHERED into `order` by the keep[] CSR (prefix + scatter preserve position
  //       order, so the compacted list IS the row-order walk; the compaction only spares the
  //       serial thread the skip-scan over ~4x more positions). The reference swept in -CCK 0
  //       order (logit desc, T3 row asc on ties); the sweep order was A/B'd NULL twice (a05 M13
  //       -CCK 1 pt-order, t3attach R4 -CCK 3 quality-order: eff/dup deltas ~1e-4 or below --
  //       the contention's outcome is set by WHICH MDs are claimed, not the order), so the rank
  //       machinery is gone. NONEXACT vs the logit-order sweep by construction; gated NULL on
  //       the n300 scoreboard (simp change 2: max |delta| 0.0003, displaced bands 0.0000).
  //   (2) for each delivery count its MDs already in the claim map; >= ccMinShared -> REVOKE and
  //       apply -CCR 2: tgtPls = -1, plsOwned[p] = 0, plsBestT3[p] = 0 (orderFloat(-inf)) -- the
  //       seed is genuinely released, its carried type-8 row survives unless other evidence
  //       retires it;
  //   (3) otherwise claim the 3 MDs and emit the type-5 row.
  // Runs AFTER ChainEmitTCs (the chain rows pre-claimed) and BEFORE the -XC kernels and the final
  // ChainSuppressCarriedTCs (invariant I7: the revoke's erasure must be visible to the
  // retirement).
  struct ChainT3CCSweepEmit {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  HitsBaseConst hitsBase,
                                  PixelSeedsConst pixelSeeds,
                                  ChainNodesConst nodes,
                                  Chains chains,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  uint32_t const* targets,
                                  int32_t* tgtPls,
                                  uint32_t const* order,
                                  uint32_t const* nDelivPtr,
                                  uint8_t* ccClaimed,
                                  uint8_t* plsOwned,
                                  uint32_t* plsBestT3,
                                  int ccMinShared,
                                  uint32_t nHits,
                                  uint16_t pixelModuleIndex,
                                  uint32_t nAllocated,
                                  uint32_t* stats) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;

      uint32_t const n = *nDelivPtr;
      uint32_t row = candsBase.nTrackCandidates();
      uint32_t nEmit = 0;
      for (uint32_t i = 0; i < n; ++i) {
        uint32_t const pos = order[i];
        int32_t const pls = tgtPls[pos];
        uint32_t const t3 = nodes.tripletIndex()[targets[pos]];
        unsigned int m[3];
        chainNodeMDs(triplets, segments, t3, m[0], m[1], m[2]);

        // (2) the -CCN verdict on the MD unit set.
        int nShared = 0;
        for (int k = 0; k < 3; ++k)
          if (ccClaimed[m[k]] != 0u)
            ++nShared;
        if (nShared >= ccMinShared) {
          // REVOKE + -CCR 2 release.
          tgtPls[pos] = -1;
          plsOwned[static_cast<uint32_t>(pls)] = 0u;
          plsBestT3[static_cast<uint32_t>(pls)] = 0u;  // orderFloat(-inf): erase the evidence
          ++stats[9];
          continue;
        }
        for (int k = 0; k < 3; ++k)
          ccClaimed[m[k]] = 1u;

        // (3) emit.
        if (row >= nAllocated) {
          ++stats[6];  // out of TC rows; not seen at PU200, but it must be visible if it happens
          break;
        }
        uint32_t const tc = row++;

        candsBase.trackCandidateType()[tc] = LSTObjType::pT3;
        candsBase.pixelSeedIndex()[tc] = pixelSeeds.seedIdx()[pls];
        candsExtended.directObjectIndices()[tc] = kBareT3TCMarker;
        candsExtended.objectIndices()[tc][0] = static_cast<uint32_t>(pls);
        candsExtended.objectIndices()[tc][1] = t3;
        for (int s = 0; s < Params_TC::kLayers; ++s) {
          candsExtended.logicalLayers()[tc][s] = 0;
          candsExtended.lowerModuleIndices()[tc][s] = kTCEmptyLowerModule;
          candsBase.hitIndices()[tc][s][0] = kTCEmptyHitIdx;
          candsBase.hitIndices()[tc][s][1] = kTCEmptyHitIdx;
        }

        uint32_t const first = pixelSeeds.firstHit()[pls];
        uint32_t const nSeedHits = static_cast<uint32_t>(pixelSeeds.nHits()[pls]);
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

        for (int k = 0; k < 3; ++k) {
          uint16_t const mod = mds.moduleIndices()[m[k]];
          int const logical = chainMdLayer(modules, mds, m[k]);
          int slot = (logical - 1) + Params_TC::kPixelLayerSlots;
          if (slot < Params_TC::kPixelLayerSlots || slot >= Params_TC::kLayers ||
              candsExtended.lowerModuleIndices()[tc][slot] != kTCEmptyLowerModule) {
            slot = -1;
            for (int s = Params_TC::kPixelLayerSlots; s < Params_TC::kLayers; ++s)
              if (candsExtended.lowerModuleIndices()[tc][s] == kTCEmptyLowerModule) {
                slot = s;
                break;
              }
            ++stats[7];
            if (slot < 0)
              break;
          }
          candsExtended.logicalLayers()[tc][slot] = static_cast<uint8_t>(logical);
          candsExtended.lowerModuleIndices()[tc][slot] = mod;
          candsBase.hitIndices()[tc][slot][0] = mds.anchorHitIndices()[m[k]];
          candsBase.hitIndices()[tc][slot][1] = mds.outerHitIndices()[m[k]];
        }
        ++nEmit;
      }
      candsBase.nTrackCandidates() = row;
      candsExtended.nTrackCandidatespT3() = candsExtended.nTrackCandidatespT3() + nEmit;
      chains.nChainTCs() = chains.nChainTCs() + nEmit;
      stats[4] = nEmit;
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
