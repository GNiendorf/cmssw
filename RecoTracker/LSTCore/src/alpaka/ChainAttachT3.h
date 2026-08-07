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
// the -CC hit-overlap contention sweep (ChainT3CCPrep / ChainT3CCSweep / ChainT3CCEmit below),
// which
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
        // Head input 19 needs the target's own circle radius here exactly as the chain kind does.
        o.radius = chainAttachRadiusOf(o.fitKappa);

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
  //   (3) P2.6c, the same DEVICE SLICING as ChainAttachScore: nS threads share one target's
  //       candidate walk, thread `sl` taking items b+sl, b+sl+nS, ... of every scanned cell, so
  //       every candidate is visited exactly once and nothing is re-walked. The per-target argmax
  //       becomes an atomicMax on the packed attachContendKey (the same strict total order as the
  //       serial `lo > best || (lo == best && p < best)`), unpacked by ChainAttachUnpackBest; the
  //       two per-target censuses move there with it. nS == 1 on host backends, where this is the
  //       previous code verbatim. Reason: nTargets here is the bare-T3 list, another ~1e3 entries,
  //       so one thread per target left this kernel on ~3% of the device exactly as stage A was.
  struct ChainAttachT3Score {
    template <typename TAcc>
    ALPAKA_FN_ACC void operator()(TAcc const& acc,
                                  AttachTargetPre const* tgt,
                                  uint32_t nTargets,
                                  uint32_t const* offsets,
                                  AttachPlsPre const* items,
                                  uint8_t const* plsOwned,
                                  uint64_t* tgtKey,
                                  uint32_t* tgtScored,
                                  uint32_t* plsBest,
                                  uint32_t* stats,
                                  float theta,
                                  uint32_t nSlices,
                                  ChainConfig cfg) const {
      constexpr bool kHost = cms::alpakatools::requires_single_thread_per_block_v<TAcc>;
      constexpr int kB = kHost ? kAttachScoreBatch : 1;
      constexpr int kIn = dnn::attachmlp::kInput;
      uint32_t const nS = kHost ? 1u : ((nSlices > 0u) ? nSlices : 1u);

      alignas(64) float xT[kIn * kB];
      int32_t rowB[kB];
      float logits[kB];
      for (int i = 0; i < kIn * kB; ++i)
        xT[i] = 0.f;
      float const t3Type = attachStdz<18>(acc, kAttachTargetTypeT3);

      for (uint32_t g : cms::alpakatools::uniform_elements(acc, nTargets * nS)) {
        uint32_t t, sl;
        if constexpr (kHost) {
          t = g;
          sl = 0u;
        } else {
          t = g / nS;
          sl = g - t * nS;
        }
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
                acc, &plsBest[static_cast<uint32_t>(p)], chainOrderFloat(lo), alpaka::hierarchy::Threads{});
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
            for (uint32_t i = b + sl; i < e; i += nS) {
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

        // One packed-key atomicMax per slice retires the slice's local argmax; stats[3] (picks) and
        // stats[8] (targets that scored anything) are per-TARGET counts and move to the unpack.
        if (bestPls >= 0)
          alpaka::atomicMax(acc,
                            &tgtKey[t],
                            attachContendKey(bestLogit, static_cast<uint32_t>(bestPls)),
                            alpaka::hierarchy::Blocks{});
        if (nScored > 0)
          alpaka::atomicAdd(acc, &tgtScored[t], nScored, alpaka::hierarchy::Blocks{});
        alpaka::atomicAdd(acc, &stats[1], nCand, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[2], nScored, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[10], nDup, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[11], nOverTheta, alpaka::hierarchy::Threads{});
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
  // Decomposed exactly like stage A's parallel form (the stage-A form in ChainAttach.h): the contention is the
  // argmax over the packed (logit, earlier position) key -- ChainAttachArgmax runs unchanged on
  // the raw arrays -- and the -RDT visiting order is a rank count under a strict total order, so
  // the reference's selection sort and the rank produce the same permutation element for
  // element. Only the hash-table walk itself stays sequential (ChainAttachT3Dedup). This one
  // decomposition runs on BOTH backends: on the serial backend it costs what the old walk cost,
  // and on a device it removes the single most expensive kernel of the whole chain pass
  // (an O(n^2) selection sort chased through global memory by one thread, ~15 ms/event).

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

  // The type-5 (pT3-class) rows. prototype/main.cc:4470-4488 assembles exactly this object: the
  // seed's DISTINCT pixel hit rows followed by the T3's three MDs (six outer-tracker hits).
  //
  // directObjectIndices is set to the sentinel kBareT3TCMarker because the row is backed by NO
  // PixelTriplets entry; objectIndices carries (pLS row, sparse triplet row) instead, which is
  // what the harness reads to recover the kinematics. chains.nChainTCs() is advanced by the number
  // of rows emitted so the harness's isChainTCRow test covers them and skips every object-index
  // map that does not apply to them.
  static constexpr uint32_t kBareT3TCMarker = 0xFFFFFFFFu;

  // ==========================================================================================
  // T4 (GPU timing): the -CC sweep, SPLIT ALONG THE ONLY DEPENDENCE IT ACTUALLY HAS.
  //
  // ChainT3CCSweepEmit below is the reference's one loop and it runs on ONE THREAD. Read it as
  // three things per delivery:
  //   (i)   look up the delivery's pLS and its three MDs (nodes.tripletIndex -> segmentIndices ->
  //         mdIndices: a three-level dependent chase through global memory);
  //   (ii)  count how many of those MDs the claim map already holds, revoke or claim, and take the
  //         next output row from a running counter;
  //   (iii) assemble the type-5 TrackCandidate row: ~55 stores (type, seedIdx, objectIndices, the
  //         kLayers slot reset, the pixel-hit walk, the three MD slot writes).
  // Only (ii) reads state an earlier iteration wrote. (i) is a pure function of the delivery and
  // (iii) is a pure function of (pls, t3, MDs, assigned row) -- nothing any other delivery decided.
  // So (i) and (iii) go to the grid and the serial residue keeps exactly (ii), which is 3 byte
  // loads and 3 byte stores against a 20-byte record it now reads sequentially.
  //
  // EXACTNESS. The serial kernel visits the SAME deliveries in the SAME order and hands out the
  // same rows from the same counter, so the emitted multiset, the row numbering, nEmit and the
  // -CCR 2 releases are unchanged element for element. The row-overflow break is reproduced where
  // it is: the delivery that hit the bound has ALREADY claimed its MDs, and no later delivery is
  // examined at all (rowOut stays -1 for them, which the emit pass skips). stats[7] (layer-slot
  // fallbacks) becomes an atomicAdd of the same total.
  struct ChainT3CCRec {
    uint32_t t3;
    uint32_t md[3];
    int32_t pls;
  };
  static constexpr int32_t kT3CCNoRow = -1;

  // (i) -- the per-delivery lookup, on the grid.
  struct ChainT3CCPrep {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  uint32_t const* targets,
                                  int32_t const* tgtPls,
                                  uint32_t const* order,
                                  uint32_t const* nDelivPtr,
                                  uint32_t nBound,
                                  ChainT3CCRec* recs,
                                  int32_t* rowOut) const {
      uint32_t const n = *nDelivPtr;
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (i >= n)
          continue;
        uint32_t const pos = order[i];
        ChainT3CCRec r;
        r.pls = tgtPls[pos];
        r.t3 = nodes.tripletIndex()[targets[pos]];
        chainNodeMDs(triplets, segments, r.t3, r.md[0], r.md[1], r.md[2]);
        recs[i] = r;
        rowOut[i] = kT3CCNoRow;
      }
    }
  };

  // (ii) -- the serial residue: the claim map and the row counter, nothing else.
  struct ChainT3CCSweep {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  Chains chains,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  ChainT3CCRec const* recs,
                                  uint32_t const* order,
                                  uint32_t const* nDelivPtr,
                                  int32_t* tgtPls,
                                  int32_t* rowOut,
                                  uint8_t* ccClaimed,
                                  uint8_t* plsOwned,
                                  uint32_t* plsBestT3,
                                  int ccMinShared,
                                  uint32_t nAllocated,
                                  uint32_t* stats) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;

      uint32_t const n = *nDelivPtr;
      uint32_t row = candsBase.nTrackCandidates();
      uint32_t nEmit = 0;
      for (uint32_t i = 0; i < n; ++i) {
        ChainT3CCRec const r = recs[i];

        int nShared = 0;
        for (int k = 0; k < 3; ++k)
          if (ccClaimed[r.md[k]] != 0u)
            ++nShared;
        if (nShared >= ccMinShared) {
          // REVOKE + -CCR 2 release. order[] is read only here, on the rare branch.
          tgtPls[order[i]] = -1;
          plsOwned[static_cast<uint32_t>(r.pls)] = 0u;
          plsBestT3[static_cast<uint32_t>(r.pls)] = 0u;  // orderFloat(-inf): erase the evidence
          ++stats[9];
          continue;
        }
        for (int k = 0; k < 3; ++k)
          ccClaimed[r.md[k]] = 1u;

        if (row >= nAllocated) {
          ++stats[6];  // out of TC rows; not seen at PU200, but it must be visible if it happens
          break;
        }
        rowOut[i] = static_cast<int32_t>(row++);
        ++nEmit;
      }
      candsBase.nTrackCandidates() = row;
      candsExtended.nTrackCandidatespT3() = candsExtended.nTrackCandidatespT3() + nEmit;
      chains.nChainTCs() = chains.nChainTCs() + nEmit;
      stats[4] = nEmit;
    }
  };

  // (iii) -- the row assembly, on the grid. Rows are distinct per delivery, so no two threads
  // touch the same TrackCandidate row and nothing here needs an ordering.
  struct ChainT3CCEmit {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst mds,
                                  HitsBaseConst hitsBase,
                                  PixelSeedsConst pixelSeeds,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  ChainT3CCRec const* recs,
                                  int32_t const* rowOut,
                                  uint32_t const* nDelivPtr,
                                  uint32_t nBound,
                                  uint32_t nHits,
                                  uint16_t pixelModuleIndex,
                                  uint32_t* stats) const {
      uint32_t const n = *nDelivPtr;
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (i >= n)
          continue;
        int32_t const assigned = rowOut[i];
        if (assigned == kT3CCNoRow)
          continue;
        uint32_t const tc = static_cast<uint32_t>(assigned);
        ChainT3CCRec const r = recs[i];
        uint32_t const pls = static_cast<uint32_t>(r.pls);

        candsBase.trackCandidateType()[tc] = LSTObjType::pT3;
        candsBase.pixelSeedIndex()[tc] = pixelSeeds.seedIdx()[pls];
        candsExtended.directObjectIndices()[tc] = kBareT3TCMarker;
        candsExtended.objectIndices()[tc][0] = pls;
        candsExtended.objectIndices()[tc][1] = r.t3;
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
          uint32_t const md = r.md[k];
          uint16_t const mod = mds.moduleIndices()[md];
          int const logical = chainMdLayer(modules, mds, md);
          int slot = (logical - 1) + Params_TC::kPixelLayerSlots;
          if (slot < Params_TC::kPixelLayerSlots || slot >= Params_TC::kLayers ||
              candsExtended.lowerModuleIndices()[tc][slot] != kTCEmptyLowerModule) {
            slot = -1;
            for (int s = Params_TC::kPixelLayerSlots; s < Params_TC::kLayers; ++s)
              if (candsExtended.lowerModuleIndices()[tc][s] == kTCEmptyLowerModule) {
                slot = s;
                break;
              }
            alpaka::atomicAdd(acc, &stats[7], 1u, alpaka::hierarchy::Blocks{});
            if (slot < 0)
              break;
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
