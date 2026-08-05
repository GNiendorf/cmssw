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
// P2.4b-1 MEASUREMENT PATH -- stage B of the general attach: BARE-T3 TARGETS.
//
// THIS FILE IS A MEASUREMENT INSTRUMENT, NOT A DELIVERY PATH. Nothing here runs unless
// LST_CHAIN_T3ATTACH is set in the environment, nothing here writes a TrackCandidate row,
// nothing here changes a chain, a pLS ownership flag or a carried-row retirement, and
// ChainAttach.h is not modified by it at all. Its whole job is to answer the P2.4b
// feasibility questions with measured numbers:
//   (1) how many bare-T3 attach targets exist per event,
//   (2) whether the K8a grid stays a superset on the bare-T3 target geometry,
//   (3) what the candidate volume and the head cost actually are, CPU and GPU,
//   (4) what a pT3-class delivery WOULD be, so the harness can sim-match it against
//       LST's own pT3 rows.
//
// REFERENCE (frozen): prototype/AttachDelivery.cc gaStageT3 + prototype/PixelAttach.cc
// makeT3Pre / makeT3PreGeom / k8BuildBareT3Mask + the -RT3 blocks of prototype/main.cc.
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
    //   4 attached after contention   5 -RD revocations   6 (unused)
    //   7 seed-dedup owner-slot overflows   8 targets that saw at least one scored pair
    //   9 (unused)  10 grid candidates skipped as a repeat inside one target's cell walk
    //  11 pairs whose logit reached the class margin
    constexpr uint32_t kStats = 12u;
    // Logit histogram of every SCORED pair, 1 bin per 0.25 over [-30, 30].
    constexpr int kLogitBins = 240;
    constexpr float kLogitLo = -30.f;
    constexpr float kLogitStep = 0.25f;
  }  // namespace chainattacht3

  // The bare-T3 target kind's value of head input 18 (prototype/PixelAttach.h kAttachTargetT3).
  constexpr float kAttachTargetTypeT3 = 1.f;

  // Snapshot of the live pLS ownership array. Stage B must not write the array the delivery path
  // owns, so it reads a copy; this is the copy.
  struct ChainAttachT3CopyOwned {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, uint8_t const* src, uint8_t* dst, uint32_t n) const {
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, n))
        dst[i] = src[i];
    }
  };

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
  // maxFake is a MEASUREMENT knob, not part of the M16 eligibility definition (which applies no
  // filter beyond bareness). It exists because the affordability answer turns entirely on the
  // size of this universe, and the T3-level fake score is the one discriminant that is already
  // computed for every triplet by production and therefore costs nothing to gate on -- node
  // feature 12 IS triplets.fakeScore(). 1e9 = the frozen, unfiltered universe.
  //
  // maxClaimed is the second measurement knob and the more important one. The M16 eligibility rule
  // only asks whether the T3 is a MEMBER of an accepted chain -- but a T3 that is not a member can
  // still have every one of its six hits already CLAIMED by accepted chains (the shifted triplet
  // along a track a chain already delivers). Those targets are the structural duplicate source:
  // they deliver a pT3-class TC for a sim the chain pipeline has already delivered. K9's hit-owner
  // map is the exact production-native test for it and is alive at attach time, so "at most
  // maxClaimed of the six hits are claimed" is measurable here for free. 6 = the frozen,
  // unfiltered universe.
  struct ChainAttachT3Keep {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint8_t const* consumed,
                                  ChainNodesConst nodes,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  MiniDoubletsConst mds,
                                  int32_t const* hitOwner,
                                  float maxFake,
                                  int maxClaimed,
                                  uint32_t* keep,
                                  uint32_t nNodes) const {
      for (uint32_t n : cms::alpakatools::uniform_elements(acc, nNodes)) {
        if (consumed[n] || !(nodes.features()[n][12] <= maxFake)) {
          keep[n] = 0u;
          continue;
        }
        if (maxClaimed < 6) {
          uint32_t const t3 = nodes.tripletIndex()[n];
          unsigned int m[3];
          chainNodeMDs(triplets, segments, t3, m[0], m[1], m[2]);
          int nClaimed = 0;
          for (int k = 0; k < 3; ++k) {
            if (hitOwner[mds.anchorHitIndices()[m[k]]] != chainarb::kFree)
              ++nClaimed;
            if (hitOwner[mds.outerHitIndices()[m[k]]] != chainarb::kFree)
              ++nClaimed;
          }
          if (nClaimed > maxClaimed) {
            keep[n] = 0u;
            continue;
          }
        }
        keep[n] = 1u;
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
  //   (3) it fills a logit histogram over every scored pair, which is what answers the head
  //       calibration question, and it reports the UNTHRESHOLDED per-target best as well as the
  //       thresholded pick, so a margin sweep needs no re-run.
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
                                  float* tgtBestAny,
                                  uint32_t* plsBest,
                                  uint32_t* stats,
                                  uint32_t* hist,
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
        float bestAny = kAttachNoLogit;
        uint32_t nCand = 0, nScored = 0, nDup = 0, nOverTheta = 0;
        int nb = 0;

        auto flush = [&]() {
          attachHeadBatch<kB>(xT, logits);
          for (int b = 0; b < nb; ++b) {
            float const lo = logits[b];
            int32_t const p = rowB[b];
            int hb = static_cast<int>((lo - chainattacht3::kLogitLo) / chainattacht3::kLogitStep);
            if (hb < 0)
              hb = 0;
            if (hb >= chainattacht3::kLogitBins)
              hb = chainattacht3::kLogitBins - 1;
            alpaka::atomicAdd(acc, &hist[hb], 1u, alpaka::hierarchy::Threads{});
            alpaka::atomicMax(
                acc, &plsBest[static_cast<uint32_t>(p)], attachOrderFloat(lo), alpaka::hierarchy::Threads{});
            if (lo > bestAny)
              bestAny = lo;
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
        tgtBestAny[t] = bestAny;
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
  // Serial by construction (both rules are order-dependent greedy walks over a few hundred
  // entries) and it is a measurement kernel, so no parallel variant is offered.
  struct ChainAttachT3Contend {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  PixelSeedsConst pixelSeeds,
                                  HitsBaseConst hitsBase,
                                  uint32_t const* targets,
                                  uint32_t nTargets,
                                  int32_t* tgtPls,
                                  float* tgtLogit,
                                  int32_t* plsOwnerPos,
                                  uint8_t* plsOwned,
                                  uint32_t nPls,
                                  uint32_t* hashKey,
                                  int32_t* hashVal,
                                  uint32_t nHits,
                                  uint32_t* order,
                                  uint32_t* stats,
                                  uint8_t seedDedup) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;

      for (uint32_t p = 0; p < nPls; ++p)
        plsOwnerPos[p] = -1;

      for (uint32_t pos = 0; pos < nTargets; ++pos) {
        int32_t const p = tgtPls[pos];
        if (p < 0)
          continue;
        int32_t const prev = plsOwnerPos[p];
        if (prev < 0) {
          plsOwnerPos[p] = static_cast<int32_t>(pos);
          continue;
        }
        if (tgtLogit[pos] > tgtLogit[prev]) {
          tgtPls[prev] = -1;
          tgtLogit[prev] = kAttachNoLogit;
          plsOwnerPos[p] = static_cast<int32_t>(pos);
        } else {
          tgtPls[pos] = -1;
          tgtLogit[pos] = kAttachNoLogit;
        }
      }

      uint32_t nOwners = 0;
      for (uint32_t pos = 0; pos < nTargets; ++pos)
        if (tgtPls[pos] >= 0)
          order[nOwners++] = pos;

      if (seedDedup && nOwners > 0) {
        // Selection pass over (logit desc, target position asc) == (logit desc, T3 row asc).
        for (uint32_t i = 0; i < nOwners; ++i) {
          uint32_t best = i;
          float lb = tgtLogit[order[best]];
          for (uint32_t j = i + 1; j < nOwners; ++j) {
            float const lj = tgtLogit[order[j]];
            bool const jFirst = (lj != lb) ? (lj > lb) : (order[j] < order[best]);
            if (jFirst) {
              best = j;
              lb = lj;
            }
          }
          uint32_t const tmp = order[i];
          order[i] = order[best];
          order[best] = tmp;
        }

        uint32_t nIns = 0;
        for (uint32_t i = 0; i < nOwners; ++i) {
          uint32_t const pos = order[i];
          int32_t const p = tgtPls[pos];
          if (p < 0)
            continue;

          uint32_t g[kMaxPLSHitsInHitsSoA];
          int nh = 0;
          uint32_t const first = pixelSeeds.firstHit()[p];
          uint32_t const nSeedHits = static_cast<uint32_t>(pixelSeeds.nHits()[p]);
          uint32_t const nStored = nSeedHits < kMaxPLSHitsInHitsSoA ? nSeedHits : kMaxPLSHitsInHitsSoA;
          for (uint32_t k = 0; k < nStored; ++k) {
            uint32_t const h = first + k;
            if (h >= nHits)
              continue;
            if (hitsBase.detid()[h] != kPixelModuleId)
              continue;
            g[nh++] = hitsBase.idxs()[h];
          }

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
            tgtPls[pos] = -1;
            tgtLogit[pos] = kAttachNoLogit;
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
      }

      uint32_t nAttached = 0;
      for (uint32_t pos = 0; pos < nTargets; ++pos) {
        if (tgtPls[pos] < 0)
          continue;
        // MEASUREMENT ONLY: plsOwned is a scratch copy in the caller, never the live array,
        // so marking it here cannot retire a carried row or change any delivered TC.
        plsOwned[tgtPls[pos]] = 1u;
        ++nAttached;
      }
      (void)targets;
      stats[4] = nAttached;
    }
  };

  // ==========================================================================================
  // REPLACEMENT MODE (LST_CHAIN_T3REPLACE) -- the -RT3 equivalent, mirroring the -RT5 1 the CTL
  // already runs for the pT5 class. Still a MEASUREMENT CONFIG: default OFF, and with the env
  // variable unset none of the kernels below is ever launched.
  //
  //   (1) ChainConfig::replacePT3 goes true, so ChainCompactCarriedTCs drops every carried LST
  //       type-5 row exactly as it already drops type-7 under -RT5 1, and dropPartOfPT3 goes
  //       false (the partOfPT3 half of the pixel-consumed drop would otherwise kill chains for
  //       colliding with rows that no longer exist -- the reasoning ChainConfig.h already
  //       records for dropPartOfPT5).
  //   (2) stage B runs BEFORE ChainSuppressCarriedTCs and publishes its ownership, so a seed
  //       delivered as a pT3-class object retires its own carried bare-pLS row.
  //   (3) the owners are emitted as type-5 rows appended after the chain rows.
  // ==========================================================================================

  // Inverse of attachOrderFloat (the monotone float -> uint32 key).
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float attachUnorderFloat(uint32_t key) {
    uint32_t const b = (key & 0x80000000u) ? (key & 0x7FFFFFFFu) : ~key;
    return std::bit_cast<float>(b);
  }

  // Publish stage B's ownership onto the LIVE arrays, so ChainSuppressCarriedTCs sees it.
  //
  // plsBest is the -RPS predicate's input and is compared against orderFloat(cfg.attachTheta).
  // The bare-T3 evidence lives on a DIFFERENT class margin (-AT3), so it is folded in SHIFTED by
  // (attachTheta - attachThetaT3): "the seed had a bare-T3 pair at or above -AT3" is then exactly
  // "the shifted value is at or above -a" on the one scale the shipped kernel tests. The shift is
  // an order isomorphism, so this reproduces the reference's two-margin predicate without
  // touching the shipped kernel.
  struct ChainAttachT3PublishOwnership {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint8_t const* ownedAfterStageB,
                                  uint32_t const* plsBestT3,
                                  uint8_t* plsOwnedLive,
                                  uint32_t* plsBestLive,
                                  float shift,
                                  uint32_t nPls) const {
      for (uint32_t p : cms::alpakatools::uniform_elements(acc, nPls)) {
        plsOwnedLive[p] = ownedAfterStageB[p];
        uint32_t const k = plsBestT3[p];
        if (k == 0u)
          continue;  // orderFloat(-inf): no scored pair for this seed
        uint32_t const shifted = attachOrderFloat(attachUnorderFloat(k) + shift);
        if (shifted > plsBestLive[p])
          plsBestLive[p] = shifted;
      }
    }
  };

  // The type-5 (pT3-class) rows. prototype/main.cc:3547-3573 assembles exactly this object: the
  // seed's DISTINCT pixel hit rows followed by the T3's three MDs (six outer-tracker hits).
  //
  // directObjectIndices is set to the sentinel kBareT3TCMarker because the row is backed by NO
  // PixelTriplets entry; objectIndices carries (pLS row, sparse triplet row) instead, which is
  // what the harness reads to recover the kinematics. chains.nChainTCs() is advanced by the number
  // of rows emitted so the harness's isChainTCRow test covers them and skips every object-index
  // map that does not apply to them.
  static constexpr uint32_t kBareT3TCMarker = 0xFFFFFFFFu;

  struct ChainEmitBareT3TCs {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  HitsBaseConst hitsBase,
                                  PixelSeedsConst pixelSeeds,
                                  Chains chains,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  uint32_t const* t3rows,
                                  int32_t const* plsrows,
                                  uint32_t nDeliv,
                                  uint32_t nHits,
                                  uint16_t pixelModuleIndex,
                                  uint32_t nAllocated,
                                  uint32_t* stats) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t row = candsBase.nTrackCandidates();
      uint32_t nEmit = 0;
      for (uint32_t i = 0; i < nDeliv; ++i) {
        int32_t const pls = plsrows[i];
        if (pls < 0)
          continue;
        if (row >= nAllocated) {
          ++stats[6];  // out of TC rows; not seen at PU200, but it must be visible if it happens
          break;
        }
        uint32_t const tc = row++;
        uint32_t const t3 = t3rows[i];

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

        unsigned int m[3];
        chainNodeMDs(triplets, segments, t3, m[0], m[1], m[2]);
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
