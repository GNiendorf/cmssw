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

// Pixel-seed attach, stage B: BARE-TRIPLET TARGETS.
//
// A bare triplet is a triplet that is NOT a member of any accepted chain. Stage B offers those
// triplets to the same attach head and the same contention as stage A, and delivers one
// pixel-triplet-class track candidate per surviving (triplet, seed) pair.
//
// Consumes the accepted chains (to mark consumed triplets), the triplet nodes, and the candidate
// grid stage A built. Produces pT3-type track candidate rows, the bare-triplet retirement evidence
// in its own key array (plsBestT3, read by the carried-row retirement predicate), and updates to
// the shared seed ownership map. It runs AFTER stage A and depends on it: one seed has one owner
// across both target kinds, so a seed stage A granted to a chain can never become a bare triplet's
// pick.
//
// ELIGIBILITY. The consumed mask is built AFTER arbitration and against the ACCEPTED set, not the
// welded set: a triplet that was welded into a chain and then REJECTED is still bare and still
// attachable, and that is exactly the population a pixel-triplet-class object recovers. There is no
// geometric filter -- in particular triplets already used by LST's own pixel-seeded objects stay in
// the universe, because those are the tracks this stage is meant to deliver itself. The only
// admission cut is the target fake-score gate in ChainAttachT3Keep.
//
// WHY THE GRID STAYS A SUPERSET FOR BARE-TRIPLET TARGETS
//
// Of the three axes of the argument in ChainAttach.h, only one mentions the target population.
// Axis (1), tanLambda, is a statement about the seed scatter and the width of the target's own
// window; a bare triplet's tanLambda is the same quantity on the same scale, so it transfers
// unchanged. Axis (3), phi, rests on phiDir being monotone in r, which is a property of the SEED
// helix alone and contains no target quantity at all.
//
// Axis (2), r, is where the populations differ: a 3-layer target's innermost anchor can sit on any
// layer, so its rtInner distribution is much broader and flatter than the chains'. But that axis
// does not care what the distribution IS, only that the hull is MEASURED over exactly the targets a
// seed will be compared with in that bin -- which is why ChainAttachGridBounds takes both target
// arrays in one launch and reduces them into one hull. Widening a hull costs candidates (a seed
// occupies more phi cells) and never correctness, so one grid serves both stages.
//
// That is an argument about the construction, not a proof of the implementation: ChainAttachAudit
// is re-run over the bare-triplet target array and its MISSING counter must be 0 on every event.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  namespace chainattacht3 {
    // Diagnostic counters for stage B (never read by a decision):
    //   0 (unused)  1 candidates iterated   2 pairs scored   3 per-target picks
    //   4 delivered pT3-type rows   5 dedup revocations
    //   6 carried bare-seed rows retired by the attach and the cross-clean (written by
    //     ChainTCKeepSuppress, which shares this buffer -- see the note below)
    //   7 seed-dedup owner-slot overflows   8 targets that saw at least one scored pair
    //   9 overlap-sweep revocations
    //  10 grid candidates skipped as a repeat inside one target's cell walk
    //  11 pairs whose logit reached the delivery margin
    // Slots 12-14 are written by the SHARED kernels ChainAttachOwnerHits / ChainAttachSeedConflicts
    // and MUST keep the meaning chainattach::kStats gives them:
    //  12 owners a final stage-A partner revokes outright   13 contentious owners
    //  14 max table entries one owner's keys resolve to
    // Stage-B dedup witnesses:
    //  15 max distinct partners one owner accumulated in seen[] (the kSeedDedupSeenMax headroom)
    //  16 owners whose seen[] reached the cap -- MUST BE 0, it is the one place the two forms of
    //     the walk could disagree
    //  17 table entries the LST_CHAIN_RDT_SERIAL control arm inserted (max over its walk)
    //  18 inserts that arm's load-factor cut-off refused -- MUST BE 0
    //  19 stage-B owners (the dedup walk length)
    //  20 owners the serial residue actually hash-walked
    //  21 overlap-sweep track-candidate row overflows   <-- MUST BE ZERO
    //
    // Slot 21 is deliberately its own slot: NEVER let a must-be-zero alarm share a counter with a
    // census. This buffer is written both here and by ChainTCKeepSuppress, and the alarm fires at
    // most once per event, so sharing a slot with a per-row census (which runs into the hundreds)
    // would make the alarm unreadable -- it could only ever add 1 to a large number.
    constexpr uint32_t kStats = 22u;
  }  // namespace chainattacht3

  // The bare-triplet kind's value of the targetType head input.
  constexpr float kAttachTargetTypeT3 = 1.f;

  // Mark every triplet consumed by an ACCEPTED chain. Its complement is the bare-triplet universe.
  struct ChainAttachT3MarkConsumed {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  ChainItemsConst items,
                                  uint32_t const* accepted,
                                  uint8_t* consumed) const {
      uint32_t const nAcc = chains.nAccepted();
      for (uint32_t acceptedIdx : cms::alpakatools::uniform_elements(acc, nAcc)) {
        uint32_t const chainIdx = accepted[acceptedIdx];
        uint32_t const base = chains.nodeOffset()[chainIdx];
        uint32_t const nNodes = chains.nNodes()[chainIdx];
        for (uint32_t k = 0; k < nNodes; ++k)
          consumed[items.nodeItems()[base + k]] = 1u;
      }
    }
  };

  // keep[] for the bare-triplet target selection, feeding the usual prefix-sum compaction.
  //
  // The fake-score admission is applied HERE, to the mask, so a rejected target never reaches the
  // candidate walk, is never scored, and never writes plsBestT3. The NaN-rejecting form
  // !(x <= maxFake) is deliberate. Node feature 12 is the triplet's fake score, computed at triplet
  // build time, so this costs no new dependency.
  struct ChainAttachT3Keep {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint8_t const* consumed,
                                  ChainNodesConst nodes,
                                  float maxFake,
                                  uint32_t* keep,
                                  uint32_t nNodes) const {
      for (uint32_t nodeIdx : cms::alpakatools::uniform_elements(acc, nNodes)) {
        keep[nodeIdx] = (consumed[nodeIdx] || !(nodes.features()[nodeIdx][12] <= maxFake)) ? 0u : 1u;
      }
    }
  };

  // The per-target record of a BARE TRIPLET, filling the same AttachTargetPre a chain target does:
  //
  //   rtInner / zInner : the innermost mini-doublet anchor's rt / z
  //   chordPhi         : atan2 of (second anchor - first anchor), in FLOAT -- the chain path
  //                      accumulates in double, this one does not
  //   tanLambda        : node feature 2, the node convention for dz/ds over the anchors, read off
  //                      the node so it cannot drift from the feature contract
  //   fitKappa         : node feature 0, the signed curvature
  //   rotSign          : sign(fitKappa), the same recovery ChainAttachTargetPre uses
  //   targetInputs[2] / targetInputs[3]    : innermost layer (node feature 9) and nLayers, which is 3 by construction
  //   targetInputs[4..6]         : 0, because no chain gate exists for a bare triplet; the targetType input
  //                      is what flags the absence
  //   centre           : the triplet's own circle-fit centre; non-finite gives centerValid 0
  //
  // `chain` carries the SPARSE TRIPLET INDEX rather than a chain row, which is the numbering the
  // offline dumps name a bare-triplet target in.
  struct ChainAttachT3TargetPre {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  MiniDoubletsConst miniDoublets,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  uint32_t const* targets,
                                  uint32_t nTargets,
                                  AttachTargetPre* outRecords) const {
      for (uint32_t targetIdx : cms::alpakatools::uniform_elements(acc, nTargets)) {
        uint32_t const nodeIdx = targets[targetIdx];
        uint32_t const tripletIdx = nodes.tripletIndex()[nodeIdx];
        uint32_t const innerSegment = triplets.segmentIndices()[tripletIdx][0];
        uint32_t const outerSegment = triplets.segmentIndices()[tripletIdx][1];
        uint32_t const mdFirst = segments.mdIndices()[innerSegment][0];
        uint32_t const mdSecond = segments.mdIndices()[innerSegment][1];
        (void)outerSegment;

        AttachTargetPre record;
        record.chain = tripletIdx;
        float const innerAnchorX = miniDoublets.anchorX()[mdFirst], innerAnchorY = miniDoublets.anchorY()[mdFirst];
        record.rtInner = alpaka::math::sqrt(acc, innerAnchorX * innerAnchorX + innerAnchorY * innerAnchorY);
        record.zInner = miniDoublets.anchorZ()[mdFirst];
        record.chordPhi = alpaka::math::atan2(
            acc, miniDoublets.anchorY()[mdSecond] - innerAnchorY, miniDoublets.anchorX()[mdSecond] - innerAnchorX);
        record.tanLambda = nodes.features()[nodeIdx][2];
        record.fitKappa = nodes.features()[nodeIdx][0];
        record.rotSign = (record.fitKappa >= 0.f) ? 1.f : -1.f;
        record.radius = chainAttachRadiusOf(acc, record.fitKappa);

        float const centreX = triplets.centerX()[tripletIdx], centreY = triplets.centerY()[tripletIdx];
        record.centerX = 0.f;
        record.centerY = 0.f;
        record.centerValid = 0u;
        if (!chainIsNan(centreX) && !chainIsInf(centreX) && !chainIsNan(centreY) && !chainIsInf(centreY)) {
          record.centerX = centreX;
          record.centerY = centreY;
          record.centerValid = 1u;
        }

        record.targetInputs[0] = attachStdz<7>(acc, record.fitKappa);
        record.targetInputs[1] = attachStdz<8>(acc, record.tanLambda);
        record.targetInputs[2] = attachStdz<9>(acc, nodes.features()[nodeIdx][9]);  // innermostLayer
        record.targetInputs[3] = attachStdz<10>(acc, 3.f);                          // nLayers
        // No chain gate exists for a bare triplet, so all three gate-logit inputs take the same 0
        // sentinel and the targetType input flags the absence.
        record.targetInputs[4] = attachStdz<11>(acc, 0.f);
        record.targetInputs[5] = attachStdz<12>(acc, 0.f);
        record.targetInputs[6] = attachStdz<13>(acc, 0.f);
        outRecords[targetIdx] = record;
      }
    }
  };

  // The stage-B candidate walk. It is ChainAttachScore with three differences, and a SEPARATE
  // struct on purpose: the stage-A kernel does not grow a branch or an argument for a target kind
  // it never sees.
  //   (1) the targetType input is overwritten with the bare-triplet kind AFTER attachEvalPairX has
  //       filled the feature vector; every other input, and the predicate itself, is shared code.
  //   (2) the per-seed evidence updates for EVERY scored pair, and only THEN is a seed stage A
  //       already owns skipped -- so a stage-A-owned seed can never become a bare triplet's pick,
  //       while the evidence that it matched one still counts. `plsBest` here is the separate
  //       bare-triplet key array plsBestT3: the carried-row retirement predicate reads it, and the
  //       overlap sweep below erases entries in it.
  //   (3) the delivery margin is a single scalar bar rather than an eta-banded one.
  // Batching and slicing are exactly as in ChainAttachScore, with the same exactness argument: the
  // per-target argmax becomes an atomicMax on the packed attachContendKey, unpacked by
  // ChainAttachUnpackBest, which also owns the two per-target censuses.
  struct ChainAttachT3Score {
    template <typename TAcc>
    ALPAKA_FN_ACC void operator()(TAcc const& acc,
                                  AttachTargetPre const* targets,
                                  uint32_t nTargets,
                                  uint32_t const* offsets,
                                  AttachPlsPre const* items,
                                  uint8_t const* plsOwned,
                                  uint64_t* tgtKey,
                                  uint32_t* tgtScored,
                                  uint32_t* plsBest,
                                  uint32_t* stats,
                                  // MEASUREMENT ONLY, inert when pairRows == nullptr (see
                                  // ChainAttachPairRow in ChainAttach.h).
                                  ChainAttachPairRow* pairRows,
                                  uint32_t* pairCtl,
                                  uint32_t pairCap,
                                  uint32_t pairKeep,
                                  float theta,
                                  uint32_t nSlices,
                                  ChainConfig config) const {
      constexpr bool kHost = cms::alpakatools::requires_single_thread_per_block_v<TAcc>;
      constexpr int kBatch = kHost ? kAttachScoreBatch : 1;
      constexpr int kInputs = dnn::attachmlp::kInput;
      uint32_t const sliceCount = kHost ? 1u : ((nSlices > 0u) ? nSlices : 1u);

      alignas(64) float inputsTransposed[kInputs * kBatch];
      int32_t batchRow[kBatch];
      float logits[kBatch];
      for (int i = 0; i < kInputs * kBatch; ++i)
        inputsTransposed[i] = 0.f;
      float const t3Type = attachStdz<20>(acc, kAttachTargetTypeT3);

      for (uint32_t flatIdx : cms::alpakatools::uniform_elements(acc, nTargets * sliceCount)) {
        uint32_t targetIdx, slice;
        if constexpr (kHost) {
          targetIdx = flatIdx;
          slice = 0u;
        } else {
          targetIdx = flatIdx / sliceCount;
          slice = flatIdx - targetIdx * sliceCount;
        }
        AttachTargetPre const target = targets[targetIdx];
        int32_t bestPls = -1;
        float bestLogit = kAttachNoLogit;
        uint32_t nCand = 0, nScored = 0, nDup = 0, nOverTheta = 0;
        int nStaged = 0;

        auto flush = [&]() {
          attachHeadBatch<kBatch>(inputsTransposed, logits);
          for (int batchIdx = 0; batchIdx < nStaged; ++batchIdx) {
            float const logit = logits[batchIdx];
            int32_t const seedIdx = batchRow[batchIdx];
            // MEASUREMENT ONLY: the stage-B pair row, taken here so the captured x INCLUDES the
            // target-kind overwrite above -- this is the vector the head actually consumed.
            if (pairRows != nullptr && attachPairKeep(target.chain, static_cast<uint32_t>(seedIdx), pairKeep)) {
              uint32_t const slot = alpaka::atomicAdd(acc, pairCtl, 1u, alpaka::hierarchy::Threads{});
              if (slot < pairCap) {
                ChainAttachPairRow& row = pairRows[slot];
                row.stage = 1u;
                row.target = target.chain;  // the sparse triplet index for the bare-triplet kind
                row.pls = static_cast<uint32_t>(seedIdx);
                row.logit = logit;
                for (int i = 0; i < kInputs; ++i)
                  row.x[i] = inputsTransposed[i * kBatch + batchIdx];
              } else {
                alpaka::atomicAdd(acc, pairCtl + 1u, 1u, alpaka::hierarchy::Threads{});
              }
            }
            alpaka::atomicMax(
                acc, &plsBest[static_cast<uint32_t>(seedIdx)], chainOrderFloat(logit), alpaka::hierarchy::Threads{});
            if (logit >= theta)
              ++nOverTheta;
            if (plsOwned[static_cast<uint32_t>(seedIdx)] != 0u)
              continue;  // stage A owns it: one seed, one owner, across all target kinds
            if (logit < theta)
              continue;
            if (bestPls < 0 || logit > bestLogit || (logit == bestLogit && seedIdx < bestPls)) {
              bestPls = seedIdx;
              bestLogit = logit;
            }
          }
          nScored += static_cast<uint32_t>(nStaged);
          nStaged = 0;
        };

        int const rBin = attachRBin(target.rtInner);
        int const tanLambdaBinLo = attachTanLBin(target.tanLambda - config.attachPrefDTanL, config.attachPrefDTanL);
        int const tanLambdaBinHi = attachTanLBin(target.tanLambda + config.attachPrefDTanL, config.attachPrefDTanL);
        int const phiBinLo = attachPhiBin(target.chordPhi - config.attachPrefDPhi);
        int const phiBinHi = attachPhiBin(target.chordPhi + config.attachPrefDPhi);
        int nPhiBinsScanned = phiBinHi - phiBinLo;
        if (nPhiBinsScanned < 0)
          nPhiBinsScanned += kAttachPhiBins;
        ++nPhiBinsScanned;
        if (nPhiBinsScanned > kAttachPhiBins)
          nPhiBinsScanned = kAttachPhiBins;

        for (int tanLambdaBin = tanLambdaBinLo; tanLambdaBin <= tanLambdaBinHi; ++tanLambdaBin) {
          for (int phiStep = 0; phiStep < nPhiBinsScanned; ++phiStep) {
            int const phiBin = (phiBinLo + phiStep) % kAttachPhiBins;
            // Cells of THIS scan already visited, i.e. those that would have supplied the same seed
            // earlier in the walk (see the duplicate-suppression note in ChainAttachScore).
            uint32_t const earlier = (1u << phiStep) - 1u;
            uint32_t const cell = attachCellId(rBin, tanLambdaBin, phiBin);
            uint32_t const cellBegin = offsets[cell], cellEnd = offsets[cell + 1u];
            for (uint32_t itemIdx = cellBegin + slice; itemIdx < cellEnd; itemIdx += sliceCount) {
              AttachPlsPre const& seed = items[itemIdx];
              uint32_t const seedIdx = seed.seedRow;
              ++nCand;
              uint32_t const seedMask = seed.phiMask;
              uint32_t const rotatedMask =
                  ((seedMask >> phiBinLo) | (seedMask << (kAttachPhiBins - phiBinLo))) & (kAttachPhiCellMask);
              if ((rotatedMask & earlier) != 0u) {
                ++nDup;
                continue;
              }
              float const dTanL = seed.tanLambda - target.tanLambda;
              if (!attachEvalPairX(acc, seed, target, dTanL, config, inputsTransposed + nStaged, kBatch))
                continue;
              inputsTransposed[20 * kBatch + nStaged] = t3Type;  // the ONLY input that differs by target kind
              batchRow[nStaged] = static_cast<int32_t>(seedIdx);
              ++nStaged;
              if (nStaged == kBatch)
                flush();
            }
          }
        }
        if (nStaged > 0)
          flush();

        // One packed-key atomicMax per slice retires that slice's local argmax; stats[3] (picks)
        // and stats[8] (targets that scored anything) are per-TARGET counts and move to the unpack.
        if (bestPls >= 0)
          alpaka::atomicMax(acc,
                            &tgtKey[targetIdx],
                            attachContendKey(bestLogit, static_cast<uint32_t>(bestPls)),
                            alpaka::hierarchy::Blocks{});
        if (nScored > 0)
          alpaka::atomicAdd(acc, &tgtScored[targetIdx], nScored, alpaka::hierarchy::Blocks{});
        alpaka::atomicAdd(acc, &stats[1], nCand, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[2], nScored, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[10], nDup, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[11], nOverTheta, alpaka::hierarchy::Threads{});
      }
    }
  };

  // Stage-B contention and the stage-B half of the seed-family dedup.
  //
  // The bidding targets are compacted and resolved in ascending target position, which is ascending
  // dense node index and therefore ascending triplet row, with the earlier position keeping a tie.
  // The dedup then runs over the stage-B owners AGAINST THE SAME hash table stage A left behind,
  // which is what stops a track already delivered as a chain-plus-seed object from being delivered
  // again as a triplet-plus-seed one by a sibling seed.
  //
  // Decomposed exactly like stage A's: the contention is an argmax over the packed key, so
  // ChainAttachArgmax runs unchanged on the raw arrays, and only the hash-table walk stays
  // sequential (ChainAttachT3Dedup). The decomposition pays on BOTH backends -- on a serial backend
  // it costs what a plain walk costs, and on a device it removes what was the single most expensive
  // kernel of the whole chain pass.

  // The serial residue of the stage-B dedup, over owners in gather (ascending triplet row) order
  // against the table stage A left behind. keep[] is maintained through the revocations, so after
  // this kernel it flags exactly the DELIVERIES, which is what the overlap sweep's gather reads.
  // The survivors are published into the shared plsOwned by ChainAttachPublish, one thread per
  // position, launched right after this one.
  //
  // The three-part split is stage A's, plus the one thing that makes it pay at a HIGH FLAGGED
  // FRACTION. In stage A only a handful of owners have a partner at all; in stage B about four
  // fifths do, so restricting the greedy to the flagged owners is not by itself a win. What pays is
  // the SECOND prefilter bit: a partner belonging to STAGE A is FINAL -- stage A's verdicts are
  // settled before stage B starts and nothing here can revoke one -- so an owner with such a
  // partner is revoked with NO table walk whatsoever. The hash walk is left with only the owners
  // whose partners are all other stage-B owners, where the order genuinely matters.
  //
  // EXACTNESS. For an owner, the entries this walk ACCEPTS are exactly those with `tag < self` that
  // are not marked dead, that is {all stage-A survivors} together with {the earlier stage-B owners
  // this walk kept}. That is precisely the multiset an incrementally built table would hold on
  // reaching that owner, so the accepted entry multiset, the seen[] population, its maximum size
  // and the verdict are identical owner for owner -- the split form is the same algorithm with the
  // insert hoisted and the visiting order unchanged. The one way the two could diverge is the
  // kSeedDedupSeenMax truncation, which is order-sensitive; stats[15] and stats[16] are the
  // standing witnesses that it never fires, and the LST_CHAIN_RDT_SERIAL control arm measures the
  // same two quantities on the unsplit walk.
  //
  // A revoked owner is retired by the kSeedContDead bit of its own cont[] word rather than by
  // tombstoning its entries, which saves the revocations -- most of the owners -- a second write
  // pass over the table and costs no new array. The table is dead the moment this kernel returns:
  // nothing reads hashKey or hashVal after stage B.
  struct ChainAttachT3Dedup {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint32_t const* order,
                                  uint32_t const* nOwnersPtr,
                                  int32_t const* ownerPls,
                                  uint32_t const* ownerHits,
                                  uint8_t const* ownerNHits,
                                  uint32_t* cont,  // nullptr == the LST_CHAIN_RDT_SERIAL control arm
                                  int32_t* tgtPls,
                                  float* tgtLogit,
                                  uint32_t* keep,
                                  uint32_t* hashKey,
                                  int32_t* hashVal,
                                  uint32_t const* hashOwner,
                                  uint32_t* stats,
                                  uint32_t ownerTag) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t const nOwners = *nOwnersPtr;
      stats[19] = nOwners;
      uint32_t nIns = 0, maxSeen = 0, nCap = 0, nWalked = 0;
      for (uint32_t ownerIdx = 0; ownerIdx < nOwners; ++ownerIdx) {
        int32_t const seedIdx = ownerPls[ownerIdx];
        if (seedIdx < 0)
          continue;
        uint32_t const* keys = ownerHits + static_cast<size_t>(ownerIdx) * kMaxPLSHitsInHitsSoA;
        int const nKeys = ownerNHits[ownerIdx];
        uint32_t const self = ownerTag + ownerIdx;
        bool isDup = false;
        int nSeen = 0;

        if (cont != nullptr) {
          uint32_t const contWord = cont[ownerIdx];
          if ((contWord & chainattach::kSeedContPartner) == 0u)
            continue;  // no partner anywhere: cannot be revoked, cannot revoke
          if ((contWord & chainattach::kSeedContEarlierStage) != 0u) {
            isDup = true;  // a FINAL earlier-stage owner shares at least 2 hit rows; no walk needed
          } else {
            ++nWalked;
            uint32_t seen[chainattach::kSeedDedupSeenMax];
            for (int keyIdx = 0; keyIdx < nKeys && !isDup; ++keyIdx) {
              uint32_t slot = attachSeedHash(keys[keyIdx]);
              for (uint32_t entryKey = hashKey[slot]; entryKey != chainattach::kSeedHashEmpty;
                   entryKey = hashKey[slot]) {
                if (entryKey == keys[keyIdx]) {
                  uint32_t const entryOwner = hashOwner[slot];
                  // Accept only DECIDED and STILL LIVE owners: our own entries and every later
                  // owner fail `entryOwner < self`, and a revoked earlier stage-B owner carries the
                  // dead bit. Stage-A tags are below ownerTag and are always live -- the revoked
                  // ones were tombstoned out of the table by ChainAttachSeedDedup.
                  if (entryOwner < self &&
                      (entryOwner < ownerTag || (cont[entryOwner - ownerTag] & chainattach::kSeedContDead) == 0u)) {
                    for (int seenIdx = 0; seenIdx < nSeen; ++seenIdx)
                      if (seen[seenIdx] == entryOwner) {
                        isDup = true;
                        break;
                      }
                    if (isDup)
                      break;
                    if (nSeen < chainattach::kSeedDedupSeenMax)
                      seen[nSeen++] = entryOwner;
                    else
                      ++nCap;
                  }
                }
                slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
              }
            }
          }
        } else {
          // ---- the unsplit walk: the table is built here, one kept owner at a time, and the dup
          // test keys on the entry's seed row. Reached only under LST_CHAIN_RDT_SERIAL, where
          // ChainAttachOwnerHits is launched WITHOUT the table arguments and
          // ChainAttachSeedConflicts is not launched at all, so this arm sees exactly the table an
          // incremental build produces.
          int32_t seen[chainattach::kSeedDedupSeenMax];
          for (int keyIdx = 0; keyIdx < nKeys && !isDup; ++keyIdx) {
            uint32_t slot = attachSeedHash(keys[keyIdx]);
            while (hashKey[slot] != chainattach::kSeedHashEmpty) {
              if (hashKey[slot] == keys[keyIdx]) {
                int32_t const entrySeed = hashVal[slot];
                for (int seenIdx = 0; seenIdx < nSeen; ++seenIdx)
                  if (seen[seenIdx] == entrySeed) {
                    isDup = true;
                    break;
                  }
                if (isDup)
                  break;
                if (nSeen < chainattach::kSeedDedupSeenMax)
                  seen[nSeen++] = entrySeed;
                else
                  ++nCap;
              }
              slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
            }
          }
          ++nWalked;
          if (!isDup) {
            for (int keyIdx = 0; keyIdx < nKeys; ++keyIdx) {
              if (nIns + 1u >= chainattach::kSeedHashSlots / 2u) {
                ++stats[18];
                break;
              }
              uint32_t slot = attachSeedHash(keys[keyIdx]);
              while (hashKey[slot] != chainattach::kSeedHashEmpty)
                slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
              hashKey[slot] = keys[keyIdx];
              hashVal[slot] = seedIdx;
              ++nIns;
            }
          }
        }

        if (static_cast<uint32_t>(nSeen) > maxSeen)
          maxSeen = static_cast<uint32_t>(nSeen);
        if (!isDup)
          continue;
        uint32_t const position = order[ownerIdx];
        tgtPls[position] = -1;
        tgtLogit[position] = kAttachNoLogit;
        keep[position] = 0u;
        ++stats[5];
        if (cont != nullptr)
          cont[ownerIdx] |= chainattach::kSeedContDead;
      }
      stats[15] = maxSeen;
      stats[16] = nCap;
      stats[17] = nIns;
      stats[20] = nWalked;
    }
  };

  // The hit-overlap contention on the stage-B deliveries, fused with the row emission. It is
  // ownership-map based and never pairwise: each delivery looks up ITS OWN three mini-doublets in
  // one claim map and decides alone.

  // The pT3-type rows carry the seed's DISTINCT pixel hit rows followed by the triplet's three
  // mini-doublets (six outer-tracker hits).
  //
  // directObjectIndices takes the sentinel kBareT3TCMarker because the row is backed by NO
  // PixelTriplets entry; objectIndices carries (seed row, sparse triplet row) instead, which is
  // what a consumer reads to recover the kinematics. chains.nChainTCs() is advanced by the number
  // of rows emitted, so the "this is a chain row" test covers them and skips every object-index map
  // that does not apply to them.
  static constexpr uint32_t kBareT3TCMarker = 0xFFFFFFFFu;

  // The sweep is SPLIT ALONG THE ONLY DEPENDENCE IT HAS. Per delivery it does three things:
  //   (i)   look up the delivery's seed and its three mini-doublets (nodes.tripletIndex ->
  //         segmentIndices -> mdIndices: a three-level dependent chase through global memory);
  //   (ii)  count how many of those mini-doublets the claim map already holds, revoke or claim, and
  //         take the next output row from a running counter;
  //   (iii) assemble the track-candidate row: about 55 stores.
  // Only (ii) reads state an earlier iteration wrote: (i) is a pure function of the delivery and
  // (iii) a pure function of (seed, triplet, mini-doublets, assigned row). So (i) and (iii) go to
  // the grid and the serial residue keeps exactly (ii), which is three byte loads and three byte
  // stores against a small record it reads sequentially.
  //
  // EXACTNESS. The serial kernel visits the SAME deliveries in the SAME order and hands out the
  // same rows from the same counter, so the emitted rows, their numbering, nEmit and the releases
  // are unchanged element for element. The row-overflow break is reproduced where it is: the
  // delivery that hit the bound has ALREADY claimed its mini-doublets, and no later delivery is
  // examined at all (rowOut stays kT3CCNoRow for them, which the emit pass skips). stats[7], the
  // layer-slot fallback census, becomes an atomicAdd of the same total.
  struct ChainT3CCRec {
    uint32_t tripletRow;
    uint32_t mdRows[3];
    int32_t seedRow;
  };
  static constexpr int32_t kT3CCNoRow = -1;

  // (i) -- the per-delivery lookup, on the grid. `seedRow` is the granted seed row, `tripletRow` the sparse
  // triplet row, `mdRows` its three mini-doublets.
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
      uint32_t const nDeliveries = *nDelivPtr;
      for (uint32_t delivIdx : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (delivIdx >= nDeliveries)
          continue;
        uint32_t const position = order[delivIdx];
        ChainT3CCRec record;
        record.seedRow = tgtPls[position];
        record.tripletRow = nodes.tripletIndex()[targets[position]];
        chainNodeMDs(triplets, segments, record.tripletRow, record.mdRows[0], record.mdRows[1], record.mdRows[2]);
        recs[delivIdx] = record;
        rowOut[delivIdx] = kT3CCNoRow;
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

      uint32_t const nDeliveries = *nDelivPtr;
      uint32_t tcRow = candsBase.nTrackCandidates();
      uint32_t nEmit = 0;
      for (uint32_t delivIdx = 0; delivIdx < nDeliveries; ++delivIdx) {
        ChainT3CCRec const record = recs[delivIdx];

        int nShared = 0;
        for (int k = 0; k < 3; ++k)
          if (ccClaimed[record.mdRows[k]] != 0u)
            ++nShared;
        if (nShared >= ccMinShared) {
          // Revoke, and release the seed completely: its ownership AND its bare-triplet evidence
          // are both cleared, so its carried pixel-only row genuinely survives downstream. order[]
          // is read only here, on the rare branch.
          tgtPls[order[delivIdx]] = -1;
          plsOwned[static_cast<uint32_t>(record.seedRow)] = 0u;
          plsBestT3[static_cast<uint32_t>(record.seedRow)] = 0u;  // orderFloat(-inf): erase the evidence
          ++stats[9];
          continue;
        }
        for (int k = 0; k < 3; ++k)
          ccClaimed[record.mdRows[k]] = 1u;

        if (tcRow >= nAllocated) {
          ++stats[21];  // out of TC rows; not seen at PU200, but it must be visible if it happens
          break;
        }
        rowOut[delivIdx] = static_cast<int32_t>(tcRow++);
        ++nEmit;
      }
      candsBase.nTrackCandidates() = tcRow;
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
                                  MiniDoubletsConst miniDoublets,
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
      uint32_t const nDeliveries = *nDelivPtr;
      for (uint32_t delivIdx : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (delivIdx >= nDeliveries)
          continue;
        int32_t const assigned = rowOut[delivIdx];
        if (assigned == kT3CCNoRow)
          continue;
        uint32_t const tcRow = static_cast<uint32_t>(assigned);
        ChainT3CCRec const record = recs[delivIdx];
        uint32_t const seedIdx = static_cast<uint32_t>(record.seedRow);

        candsBase.trackCandidateType()[tcRow] = LSTObjType::pT3;
        candsBase.pixelSeedIndex()[tcRow] = pixelSeeds.seedIdx()[seedIdx];
        candsExtended.directObjectIndices()[tcRow] = kBareT3TCMarker;
        candsExtended.objectIndices()[tcRow][0] = seedIdx;
        candsExtended.objectIndices()[tcRow][1] = record.tripletRow;
        for (int slot = 0; slot < Params_TC::kLayers; ++slot) {
          candsExtended.logicalLayers()[tcRow][slot] = 0;
          candsExtended.lowerModuleIndices()[tcRow][slot] = kTCEmptyLowerModule;
          candsBase.hitIndices()[tcRow][slot][0] = kTCEmptyHitIdx;
          candsBase.hitIndices()[tcRow][slot][1] = kTCEmptyHitIdx;
        }

        uint32_t const first = pixelSeeds.firstHit()[seedIdx];
        uint32_t const nSeedHits = static_cast<uint32_t>(pixelSeeds.nHits()[seedIdx]);
        uint32_t const nStored = nSeedHits < kMaxPLSHitsInHitsSoA ? nSeedHits : kMaxPLSHitsInHitsSoA;
        int slotPix = 0;
        for (uint32_t k = 0; k < nStored && slotPix < Params_TC::kPixelLayerSlots * Params_TC::kHitsPerLayer; ++k) {
          uint32_t const hitIdx = first + k;
          if (hitIdx >= nHits)
            continue;
          if (hitsBase.detid()[hitIdx] != kPixelModuleId)
            continue;
          int const layerSlot = slotPix / Params_TC::kHitsPerLayer;
          candsExtended.logicalLayers()[tcRow][layerSlot] = 0;
          candsExtended.lowerModuleIndices()[tcRow][layerSlot] = pixelModuleIndex;
          candsBase.hitIndices()[tcRow][layerSlot][slotPix % Params_TC::kHitsPerLayer] = hitIdx;
          ++slotPix;
        }

        // Each mini-doublet goes in the slot of its own layer; if that slot is out of range or
        // already taken, it falls back to the first free outer-tracker slot and the census records
        // it. A delivery with no free slot left stops there.
        for (int k = 0; k < 3; ++k) {
          uint32_t const mdIdx = record.mdRows[k];
          uint16_t const moduleIdx = miniDoublets.moduleIndices()[mdIdx];
          int const logical = chainMdLayer(modules, miniDoublets, mdIdx);
          int slot = (logical - 1) + Params_TC::kPixelLayerSlots;
          if (slot < Params_TC::kPixelLayerSlots || slot >= Params_TC::kLayers ||
              candsExtended.lowerModuleIndices()[tcRow][slot] != kTCEmptyLowerModule) {
            slot = -1;
            for (int freeSlot = Params_TC::kPixelLayerSlots; freeSlot < Params_TC::kLayers; ++freeSlot)
              if (candsExtended.lowerModuleIndices()[tcRow][freeSlot] == kTCEmptyLowerModule) {
                slot = freeSlot;
                break;
              }
            alpaka::atomicAdd(acc, &stats[7], 1u, alpaka::hierarchy::Blocks{});
            if (slot < 0)
              break;
          }
          candsExtended.logicalLayers()[tcRow][slot] = static_cast<uint8_t>(logical);
          candsExtended.lowerModuleIndices()[tcRow][slot] = moduleIdx;
          candsBase.hitIndices()[tcRow][slot][0] = miniDoublets.anchorHitIndices()[mdIdx];
          candsBase.hitIndices()[tcRow][slot][1] = miniDoublets.outerHitIndices()[mdIdx];
        }
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
