#ifndef RecoTracker_LSTCore_src_alpaka_ChainParallel_h
#define RecoTracker_LSTCore_src_alpaka_ChainParallel_h

#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainConfig.h"
#include "RecoTracker/LSTCore/interface/ChainNodesSoA.h"
#include "RecoTracker/LSTCore/interface/ChainsSoA.h"
#include "RecoTracker/LSTCore/interface/MiniDoubletsSoA.h"
#include "RecoTracker/LSTCore/interface/SegmentsSoA.h"
#include "RecoTracker/LSTCore/interface/TrackCandidatesSoA.h"
#include "RecoTracker/LSTCore/interface/TripletsSoA.h"

#include "ChainArbitrate.h"
#include "ChainAttach.h"
#include "ChainGraph.h"

// P2.6a. The data-parallel form of the seven order-dependent stages of P2.3 / P2.4. These are the
// ONLY implementation of those stages: the single-thread kernels they were verified against were
// deleted in the twin-collapse pass, so every backend runs the code below (on the serial CPU
// accelerator each kernel's element loop simply runs sequentially in one thread).
//
// Every kernel here was REQUIRED to produce output bit-identical to the serial reference it
// replaced. The stages, and the cascade that implements each:
//
//   stage                                  form here
//   -------------------------------------  ---------------------------------------------------
//   -RT5 carried-row compaction            ChainTCKeepCompact + prefix + gather/scatter + finish
//   K9a/K9b/K9c claim                      ChainClaimPrep (ChainArbitrate.h), prefix + scatter,
//                                          ChainClaimRank, ChainPreClaimPixels, ChainClaimRounds
//   EX extension                           ChainExtendReach + ChainExtendRound x4 + finisher
//   K10 row assignment                     ChainRowFlags + prefix + ChainRowAssign + ChainRowFinish
//   K8-0b stage-A target list              ChainTargetFlags + prefix + compaction
//   K8c stage-A contend + -RD dedup        ChainAttachArgmax / Resolve / compaction /
//                                          ChainAttachOwnerHits / SeedDedup / Publish
//   K8d carried-row retirement             ChainTCKeepSuppress + prefix + gather/scatter + finish
//
// WHY THE GREEDY WALKS COME OUT THE SAME (port map risk R3).
//
// K9b decides a chain from NOTHING but the owner labels at that chain's OWN claim-hit list: the
// claim fraction, the -FC item budget and the -W braid are all statistics over `owner[claimHits]`.
// The claim-hit list is a static property of the chain. So if, at the moment a chain is evaluated,
// no not-yet-decided chain of HIGHER priority can still write into any of those hits, the chain's
// verdict is already final and can be taken out of order.
//
// That gives the conflict-free-round form: each round, every undecided chain registers its
// priority position on each of its claim hits with an atomicMin; a chain whose every claim hit
// carries its own position is the highest-priority undecided claimer of all of them, so it is
// decided exactly. Two such chains cannot share a hit (each would have to be the unique minimum at
// it), so their owner writes never collide either. The lowest-position undecided chain always
// passes this test, which is the termination argument: at least one chain leaves the undecided set
// every round, so the loop ends after at most nCandidates rounds.
//
// One extra rule makes the round count small instead of merely finite: the claim test is MONOTONE.
// Hits are only ever claimed, never released, so nClaimed and frac can only grow, and each of the
// three -F / -FC / -FBC forms of claimOk is a non-increasing function of them. A chain that fails
// claimOk against the current owner map can therefore be rejected on the spot, with no position
// test at all -- and, being decided, it stops blocking everything behind it.
//
// Each of these forms needs per-event scratch (flags, prefixes, staging arrays). None of the call
// sites drains the queue to retire it: the CMS caching device allocator records an event on the
// queue when a block is released and only hands it out again once that event has completed, so a
// buffer going out of scope on the host cannot be reused under a kernel that is still reading it.
//
// The extension has the same shape with one difference: what it reads depends on where it walks.
// Its read set is nevertheless STATIC, because the candidates it may examine are exactly the MDs
// reachable from its outer terminal through at most -EXN segment hops, and that neighbourhood does
// not depend on the claimed-hit map -- only on which of those candidates survive does. Precomputing
// that neighbourhood (ChainExtendReach) restores the same argument. Rounds are capped at two and a
// single-thread finisher sweeps whatever is left in position order, which is exact for the same
// reason (every lower position is either already applied or is visited earlier in the sweep).

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  namespace chainpar {
    constexpr uint32_t kNoPos = 0xFFFFFFFFu;
    constexpr uint8_t kUndecided = 0u;
    constexpr uint8_t kRejected = 1u;
    constexpr uint8_t kAccepted = 2u;
    // Reachable-MD storage for the -EXN > 1 extension read set. At the frozen -EXN 1 nothing is
    // stored at all: the read set IS the terminal MiniDoublet's slice of the LineSegment adjacency,
    // which is already materialised, so it is walked in place and has no capacity at all. The
    // buffered multi-hop form keeps a cap; an overflow there is not a correctness problem (it is
    // counted and the chain waits for the serial finisher) but it is expensive, because an
    // overflowed chain has to block every later position, so it is reported in the K9 stats block.
    constexpr uint32_t kExtReachCap = 256u;
    // "this chain does not extend", i.e. it has no read set at all.
    constexpr uint32_t kNoTerminal = 0xFFFFFFFFu;
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
                                  uint32_t nPls,
                                  uint32_t* keep,
                                  uint32_t* classCounts,
                                  uint32_t nBound,
                                  uint32_t* stats,
                                  ChainConfig cfg) const {
      uint32_t const keyChain = attachOrderFloat(cfg.rpsThetaChain);
      uint32_t const keyT3 = attachOrderFloat(cfg.attachThetaT3);
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
  struct ChainClaimRank {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainOrderKeyRec const* recs,
                                  uint32_t const* nCandPtr,
                                  uint32_t nBound,
                                  uint32_t* order) const {
      uint32_t const n = *nCandPtr;
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (i >= n)
          continue;
        ChainOrderKeyRec const a = recs[i];
        uint32_t rank = 0u;
        for (uint32_t j = 0; j < n; ++j) {
          if (j == i)
            continue;
          ChainOrderKeyRec const b = recs[j];
          bool const bFirst = (b.key != a.key)
                                  ? (b.key > a.key)
                                  : ((b.stable != a.stable) ? (b.stable < a.stable) : (b.chain < a.chain));
          rank += bFirst ? 1u : 0u;
        }
        order[rank] = a.chain;
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
      auto& partial = alpaka::declareSharedVar<uint32_t[kChainScanBlockThreads], __COUNTER__>(acc);
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

        // --- phase C: the owner writes of everything accepted THIS round -------------------------
        // Two chains decided in the same round are the unique minimum at each of their own claim
        // hits, so their hit sets are disjoint and these stores never race.
        for (uint32_t oi = worker; oi < n; oi += nWorkers) {
          if (part[oi] == 0u || state[oi] != chainpar::kAccepted)
            continue;
          uint32_t const c = order[oi];
          uint32_t const hitBase = 6u * chains.nodeOffset()[c];
          int const total = chains.nClaimHits()[c];
          for (int k = 0; k < total; ++k)
            owner[claimHits[hitBase + k]] = static_cast<int32_t>(c);
          chains.claimFlags()[c] |= kChainClaimAccepted;
        }
        alpaka::syncBlockThreads(acc);

        // --- phase D: put minPos back to its empty state, touching only the hits used ------------
        for (uint32_t oi = worker; oi < n; oi += nWorkers) {
          if (part[oi] == 0u)
            continue;
          uint32_t const c = order[oi];
          uint32_t const hitBase = 6u * chains.nodeOffset()[c];
          int const total = chains.nClaimHits()[c];
          for (int k = 0; k < total; ++k)
            minPos[claimHits[hitBase + k]] = chainpar::kNoPos;
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
      uint32_t local = 0u;
      for (uint32_t i = begin; i < end; ++i)
        local += (state[i] == chainpar::kAccepted) ? 1u : 0u;
      partial[worker] = local;
      alpaka::syncBlockThreads(acc);

      uint32_t base = 0u, total = 0u;
      for (uint32_t w = 0; w < nWorkers; ++w) {
        if (w == worker)
          base = total;
        total += partial[w];
      }
      uint32_t run = base;
      for (uint32_t i = begin; i < end; ++i)
        if (state[i] == chainpar::kAccepted)
          accepted[run++] = order[i];
      alpaka::syncBlockThreads(acc);

      if (cms::alpakatools::once_per_block(acc)) {
        chains.nAccepted() = total;
        stats[11] = rounds;
        stats[12] = peak;
      }
    }
  };

  // ==========================================================================================
  // EX. The extension.

  // The STATIC read set of one accepted chain's extension: every MiniDoublet the candidate loop can
  // reach from the outer terminal in at most -EXN segment hops. Which of them survive the filters
  // depends on the claimed-hit map; WHICH ONES ARE LOOKED AT does not, and that is all the conflict
  // test needs. The inner end is not expanded because the reference's candidate range is literally
  // empty there (nb = ne = 0), so it can neither read nor write.
  //
  // At the frozen -EXN 1 the read set is exactly the terminal MiniDoublet's slice of the
  // LineSegment adjacency, which the -EXS build already materialised: nothing is copied and there is
  // no capacity to overflow. Only the multi-hop configuration needs the buffered breadth-first
  // expansion below, and only that one can overflow.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainReachCount(
      uint32_t const* segOffsets, uint8_t const* reachN, uint32_t const* reachTerm, bool direct, uint32_t ai) {
    if (!direct)
      return reachN[ai];
    uint32_t const t = reachTerm[ai];
    return (t == chainpar::kNoTerminal) ? 0u : (segOffsets[t + 1u] - segOffsets[t]);
  }

  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t chainReachAt(SegmentsConst segments,
                                                       uint32_t const* segOffsets,
                                                       uint32_t const* segItems,
                                                       uint32_t const* reachMd,
                                                       uint32_t const* reachTerm,
                                                       bool direct,
                                                       uint32_t ai,
                                                       uint32_t k) {
    if (!direct)
      return reachMd[static_cast<size_t>(ai) * chainpar::kExtReachCap + k];
    return segments.mdIndices()[segItems[segOffsets[reachTerm[ai]] + k]][1];
  }

  struct ChainExtendReach {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  SegmentsConst segments,
                                  ChainItemsConst items,
                                  ChainsConst chains,
                                  uint32_t const* accepted,
                                  uint32_t nBound,
                                  uint32_t const* segOffsets,
                                  uint32_t const* segItems,
                                  uint32_t nMDall,
                                  bool direct,
                                  uint32_t* reachMd,
                                  uint8_t* reachN,
                                  uint8_t* reachOvf,
                                  uint32_t* reachTerm,
                                  uint32_t* stats,
                                  ChainConfig cfg) const {
      uint32_t const nAcc = chains.nAccepted();
      for (uint32_t ai : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (ai >= nAcc)
          continue;
        uint32_t nR = 0u;
        uint8_t ovf = 0u;
        uint32_t term = chainpar::kNoTerminal;

        uint32_t const c = accepted[ai];
        bool const outerOn = (cfg.extendMode == 1 || cfg.extendMode == 3);
        int const nMD = chains.nMDs()[c];
        if (outerOn && chains.nLayers()[c] >= cfg.extendMinLayers && nMD >= 3 && cfg.extendMaxPerEnd > 0) {
          uint32_t const mdBase = 3u * chains.nodeOffset()[c];
          uint32_t const terminal = items.mdItems()[mdBase + nMD - 1];
          if (terminal < nMDall)
            term = terminal;

          if (!direct && term != chainpar::kNoTerminal) {
            uint32_t* buf = reachMd + static_cast<size_t>(ai) * chainpar::kExtReachCap;
            uint32_t prevBeg = 0u, prevEnd = 0u;
            for (int rep = 0; rep < cfg.extendMaxPerEnd; ++rep) {
              uint32_t const levelBeg = nR;
              uint32_t const srcBeg = (rep == 0) ? 0u : prevBeg;
              uint32_t const srcEnd = (rep == 0) ? 1u : prevEnd;
              for (uint32_t s = srcBeg; s < srcEnd; ++s) {
                uint32_t const t = (rep == 0) ? term : buf[s];
                if (t >= nMDall)
                  continue;
                for (uint32_t q = segOffsets[t]; q < segOffsets[t + 1u]; ++q) {
                  uint32_t const m = segments.mdIndices()[segItems[q]][1];
                  if (m >= nMDall)
                    continue;
                  bool dup = false;
                  for (uint32_t u = 0; u < nR; ++u)
                    if (buf[u] == m) {
                      dup = true;
                      break;
                    }
                  if (dup)
                    continue;
                  if (nR >= chainpar::kExtReachCap) {
                    ovf = 1u;
                    break;
                  }
                  buf[nR++] = m;
                }
                if (ovf)
                  break;
              }
              if (ovf)
                break;
              prevBeg = levelBeg;
              prevEnd = nR;
              if (prevBeg == prevEnd)
                break;
            }
          }
        }
        reachN[ai] = static_cast<uint8_t>(nR);
        reachOvf[ai] = ovf;
        reachTerm[ai] = term;
        if (ovf)
          alpaka::atomicAdd(acc, &stats[14], 1u, alpaka::hierarchy::Blocks{});
      }
    }
  };

  // The reach-set walk over minPos, in its two roles: `reset` puts the entries every accepted
  // chain can touch back to kNoPos (round > 0 preamble, over ALL accepted chains, no done or
  // overflow test), and the registration pass atomicMins the undecided chains' positions onto the
  // same entries. One walk, one source of truth for "which minPos entries belong to chain ai";
  // the two roles stay two launches because the reset must be globally complete before the first
  // atomicMin lands.
  struct ChainExtendMinPos {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  ChainsConst chains,
                                  uint32_t const* segOffsets,
                                  uint32_t const* segItems,
                                  uint32_t nMDall,
                                  bool direct,
                                  uint32_t const* reachMd,
                                  uint8_t const* reachN,
                                  uint8_t const* reachOvf,
                                  uint32_t const* reachTerm,
                                  uint8_t const* done,
                                  uint32_t nBound,
                                  uint32_t* minPos,
                                  uint32_t nHitUniverse,
                                  uint32_t* blockPos,
                                  bool reset) const {
      if (reset && cms::alpakatools::once_per_grid(acc))
        *blockPos = chainpar::kNoPos;
      uint32_t const nAcc = chains.nAccepted();
      for (uint32_t ai : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (ai >= nAcc)
          continue;
        if (!reset) {
          if (done[ai] != 0u)
            continue;
          if (reachOvf[ai] != 0u)
            alpaka::atomicMin(acc, blockPos, ai, alpaka::hierarchy::Blocks{});
        }
        uint32_t const nR = chainReachCount(segOffsets, reachN, reachTerm, direct, ai);
        for (uint32_t k = 0; k < nR; ++k) {
          uint32_t const m = chainReachAt(segments, segOffsets, segItems, reachMd, reachTerm, direct, ai, k);
          if (m >= nMDall)
            continue;
          unsigned int const ha = mds.anchorHitIndices()[m], hb = mds.outerHitIndices()[m];
          if (reset) {
            if (ha < nHitUniverse)
              minPos[ha] = chainpar::kNoPos;
            if (hb < nHitUniverse)
              minPos[hb] = chainpar::kNoPos;
          } else {
            if (ha < nHitUniverse)
              alpaka::atomicMin(acc, &minPos[ha], ai, alpaka::hierarchy::Blocks{});
            if (hb < nHitUniverse)
              alpaka::atomicMin(acc, &minPos[hb], ai, alpaka::hierarchy::Blocks{});
          }
        }
      }
    }
  };

  struct ChainExtendRound {
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
                                  bool direct,
                                  uint32_t const* reachMd,
                                  uint8_t const* reachN,
                                  uint32_t const* reachTerm,
                                  uint8_t* done,
                                  uint32_t nBound,
                                  uint32_t const* minPos,
                                  uint32_t const* blockPos,
                                  uint32_t* stats,
                                  ChainConfig cfg) const {
      uint32_t const nAcc = chains.nAccepted();
      uint32_t const bp = *blockPos;
      for (uint32_t ai : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (ai >= nAcc || done[ai] != 0u)
          continue;
        if (ai >= bp)
          continue;  // an earlier chain overflowed its read set: wait for the finisher
        uint32_t const nR = chainReachCount(segOffsets, reachN, reachTerm, direct, ai);
        bool safe = true;
        for (uint32_t k = 0; k < nR && safe; ++k) {
          uint32_t const m = chainReachAt(segments, segOffsets, segItems, reachMd, reachTerm, direct, ai, k);
          if (m >= nMDall)
            continue;
          unsigned int const ha = mds.anchorHitIndices()[m], hb = mds.outerHitIndices()[m];
          if (ha < nHitUniverse && minPos[ha] != ai)
            safe = false;
          if (hb < nHitUniverse && minPos[hb] != ai)
            safe = false;
        }
        if (!safe)
          continue;
        chainExtendChain(acc,
                         modules,
                         mds,
                         segments,
                         items,
                         chains,
                         accepted[ai],
                         claimedHit,
                         nHitUniverse,
                         segOffsets,
                         segItems,
                         nMDall,
                         stats,
                         cfg);
        done[ai] = 1u;
      }
    }
  };

  // Whatever the rounds could not decide, in position order. Exact for the same reason the serial
  // kernel is: every lower position is either already applied or is visited earlier in this sweep.
  // stats[15] counts what it had to do, which is the measurement of how well the rounds worked.
  struct ChainExtendFinish {
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
                                  uint8_t* done,
                                  uint32_t* stats,
                                  ChainConfig cfg) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t const nAcc = chains.nAccepted();
      for (uint32_t ai = 0; ai < nAcc; ++ai) {
        if (done[ai] != 0u)
          continue;
        chainExtendChain(acc,
                         modules,
                         mds,
                         segments,
                         items,
                         chains,
                         accepted[ai],
                         claimedHit,
                         nHitUniverse,
                         segOffsets,
                         segItems,
                         nMDall,
                         stats,
                         cfg);
        done[ai] = 1u;
        ++stats[15];
      }
    }
  };

  // ==========================================================================================
  // K10, first half: the output row of every accepted chain long enough to emit a TC.

  struct ChainRowFlags {
    ALPAKA_FN_ACC void operator()(
        Acc1D const& acc, ChainsConst chains, uint32_t const* accepted, uint32_t nBound, uint32_t* keep) const {
      uint32_t const nAcc = chains.nAccepted();
      for (uint32_t ai : cms::alpakatools::uniform_elements(acc, nBound))
        keep[ai] = (ai < nAcc && chains.nLayers()[accepted[ai]] >= kChainTCMinLayers &&
                    (chains.flags()[accepted[ai]] & kChainFlagCcsSuppressed) == 0u)
                       ? 1u
                       : 0u;
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

  // ==========================================================================================
  // K8. The attach contention and the -RD seed-family dedup.
  // (attachContendKey, the packed argmax key, lives in ChainAttach.h next to attachOrderFloat:
  // the stage-B kernels of ChainAttachT3.h share it.)

  struct ChainTargetFlags {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* accepted,
                                  uint32_t nBound,
                                  uint32_t* keep,
                                  ChainConfig cfg) const {
      uint32_t const nAcc = chains.nAccepted();
      for (uint32_t ai : cms::alpakatools::uniform_elements(acc, nBound)) {
        bool k = false;
        if (ai < nAcc) {
          uint32_t const c = accepted[ai];
          k = (chains.nLayers()[c] >= kAttachMinLayers) && !(chains.dcaXY()[c] >= cfg.attachDcaMax);
        }
        keep[ai] = k ? 1u : 0u;
      }
    }
  };

  // K8c contention. The reference walks the target positions and lets a later target take a
  // contested pLS only on a STRICTLY higher logit -- which is the argmax over the positions that
  // picked that pLS, with the EARLIER position keeping an exact tie. An argmax has no order, so it
  // is a plain atomicMax over the packed (logit, ~position) key.
  struct ChainAttachArgmax {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  int32_t const* tgtPls,
                                  float const* tgtLogit,
                                  uint32_t nTargets,
                                  uint64_t* plsKey) const {
      for (uint32_t pos : cms::alpakatools::uniform_elements(acc, nTargets)) {
        int32_t const p = tgtPls[pos];
        if (p < 0)
          continue;
        alpaka::atomicMax(
            acc, &plsKey[static_cast<uint32_t>(p)], attachContendKey(tgtLogit[pos], pos), alpaka::hierarchy::Blocks{});
      }
    }
  };

  // Both attach stages resolve their argmax with this kernel. `targets == nullptr` is stage B,
  // whose verdicts stay on the position arrays; stage A passes its target list and the winners are
  // published onto the chain rows as well, because its -RD pass and its delivery are chain-row
  // keyed.
  struct ChainAttachResolve {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  Chains chains,
                                  uint32_t const* targets,
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
        if (targets != nullptr) {
          uint32_t const c = targets[pos];
          chains.attachPls()[c] = tgtPls[pos];
          chains.attachLogit()[c] = tgtLogit[pos];
        }
        keep[pos] = (tgtPls[pos] >= 0) ? 1u : 0u;
      }
    }
  };

  // (The -RD rank kernel is gone: per the zero-sorts directive the dedup walk visits owners in
  // the gather's ascending-position order -- simp change 3, NONEXACT, n300-gated. The serial
  // walk below consumes the gathered list directly.)

  // The pLS's DISTINCT pixel hit indices, staged so the serial dedup walk below touches a compact
  // array instead of chasing the hits SoA. Keyed by owner slot, not by pLS row.
  //
  // Both stages stage their owners here. The owner list holds chain rows in stage A (its grant
  // lives on the chain row, so `tgtPls == nullptr` reads chains.attachPls()) and target positions
  // in stage B (`tgtPls` is its position-keyed grant array). Nothing else differs.
  struct ChainAttachOwnerHits {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  PixelSeedsConst pixelSeeds,
                                  HitsBaseConst hitsBase,
                                  ChainsConst chains,
                                  int32_t const* tgtPls,
                                  uint32_t const* owners,
                                  uint32_t const* nOwnersPtr,
                                  uint32_t nBound,
                                  uint32_t nHits,
                                  uint32_t* ownerHits,
                                  uint8_t* ownerNHits,
                                  int32_t* ownerPls) const {
      uint32_t const n = *nOwnersPtr;
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (i >= n)
          continue;
        uint32_t const c = owners[i];
        int32_t const p = (tgtPls != nullptr) ? tgtPls[c] : chains.attachPls()[c];
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
            ownerHits[i * kMaxPLSHitsInHitsSoA + nh] = hitsBase.idxs()[h];
            ++nh;
          }
        }
        ownerNHits[i] = static_cast<uint8_t>(nh);
      }
    }
  };

  // K8c, the -RD seed-family dedup. This one stays SEQUENTIAL: the decision for an owner depends on
  // the hash table state left by every kept owner before it, and there is no static read set to
  // bound the dependency with (the table is keyed on hit indices that any owner may touch). It is a
  // few hundred owners with at most four pixel hits each; with ownerHits staged into a compact array
  // by the kernel above, the whole walk is a few thousand accesses.
  struct ChainAttachSeedDedup {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  Chains chains,
                                  uint32_t const* owners,
                                  uint32_t const* nOwnersPtr,
                                  uint32_t const* ownerHits,
                                  uint8_t const* ownerNHits,
                                  int32_t const* ownerPls,
                                  uint32_t* hashKey,
                                  int32_t* hashVal,
                                  uint32_t* stats) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t const nOwners = *nOwnersPtr;
      uint32_t nIns = 0;

      for (uint32_t i = 0; i < nOwners; ++i) {
        int32_t const p = ownerPls[i];
        if (p < 0)
          continue;
        uint32_t const* g = ownerHits + static_cast<size_t>(i) * kMaxPLSHitsInHitsSoA;
        int const nh = ownerNHits[i];

        // "shares >= 2 hit rows with an already-kept owner" <=> some kept pLS shows up under two
        // DIFFERENT hits of p (the rows are distinct and a key holds each owner at most once).
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
          chains.attachPls()[owners[i]] = -1;
          chains.attachLogit()[owners[i]] = kAttachNoLogit;
          ++stats[5];
          continue;
        }
        for (int a = 0; a < nh; ++a) {
          if (nIns + 1u >= chainattach::kSeedHashSlots / 2u) {
            ++stats[7];
            break;  // never reached at PU200 (a few hundred owners x <= 4 hits)
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
  };

  // The stage-A grants, after the -RD revocations: the one-pLS-one-owner authority array plsOwned,
  // and its inversion plsOwnerChain (pLS row -> owning chain row, single-valued by that same
  // invariant, so no atomics) which the A14 -CCS second pass reads. A chain can only carry a grant
  // if it is a target, so one pass over the target list covers both arrays.
  struct ChainAttachPublish {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* targets,
                                  uint32_t nTargets,
                                  uint8_t* plsOwned,
                                  int32_t* plsOwnerChain,
                                  uint32_t* stats) const {
      for (uint32_t pos : cms::alpakatools::uniform_elements(acc, nTargets)) {
        uint32_t const c = targets[pos];
        int32_t const p = chains.attachPls()[c];
        if (p < 0)
          continue;
        plsOwned[static_cast<uint32_t>(p)] = 1u;
        plsOwnerChain[static_cast<uint32_t>(p)] = static_cast<int32_t>(c);
        alpaka::atomicAdd(acc, &stats[4], 1u, alpaka::hierarchy::Blocks{});
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
