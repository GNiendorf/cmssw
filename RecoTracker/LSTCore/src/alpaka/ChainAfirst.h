#ifndef RecoTracker_LSTCore_src_alpaka_ChainAfirst_h
#define RecoTracker_LSTCore_src_alpaka_ChainAfirst_h

#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainConfig.h"
#include "RecoTracker/LSTCore/interface/ChainsSoA.h"

#include "ChainAttach.h"

// [ARM-AFIRST] Seed evidence BEFORE the exclusivity decision.
//
// The greedy hit claim (ChainArbitrate.h) picks one representative of every shared-hit family with
// a SEED-BLIND key, and the attach only ever sees the survivor. Master's pT5 matching runs before
// any hit exclusivity, so no shorter object can hold the hits of a seed-compatible longer one. This
// file is the smallest structure that gives our claim the same information: a pre-claim pass scores
// the gate-alive CANDIDATES (not the accepted survivors) through the ordinary stage-A scorer, keeps
// one bit per chain -- "a pixel seed says this chain is deliverable" -- and adds one term to the
// claim's order key for the chains that carry it.
//
// Everything here is advisory. The pass writes no owner map, no seed ownership, no retirement
// evidence, no cross-clean arm and no chain-row grant; its only durable output is the evidence byte
// and the order-key term. The claim kernel, the attach, the contention, the delivery bars, stage B,
// the row assignment and the emission are untouched, so the conflict-free-rounds proof holds
// verbatim: `order` is still a strict total order (orderKey desc, stableKey asc, chain asc) fixed
// before the walk starts, and only the VALUE of orderKey moves.
//
// Off by default: with mode 0 no kernel below is launched and no buffer is allocated.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  namespace chainafirst {
    // Evidence modes. BAR is the chain's own argmax against its own delivery bar; MUTUAL adds "and
    // it is the highest-logit bidder for that seed"; LONGEST adds "and it is the LONGEST bidder for
    // that seed" (ties by logit), which is master's class order rather than the head's logit order.
    constexpr int kModeOff = 0;
    constexpr int kModeBar = 1;
    constexpr int kModeMutual = 2;
    constexpr int kModeLongest = 3;
    // THE CONTROL, and it is the one the memory's "always run the control" rule demands: the same
    // order-key term granted to every evidence-pass target with NO seed evidence at all, i.e. a
    // pure length prior at the claim. If mode 4 reproduces mode 1 then the arm is `lambdaLen` in
    // disguise and the seed pass is dead weight; if it does not, the conditioning is the mechanism.
    constexpr int kModeLenOnly = 4;

    // Per-event census slots (LST_CHAIN_AFIRST_DEBUG).
    //   [0] evidence-pass targets   [1] targets with an above-bar argmax   [2] chains granted
    //   evidence   [3] order keys moved
    constexpr uint32_t kStats = 8u;

    // The per-seed bidder key. LONGEST puts the layer count above the ordered logit so the
    // comparison is one 64-bit atomicMax either way; MUTUAL uses the logit alone. Both are
    // associative, commutative and idempotent, so the reduction is schedule-independent.
    ALPAKA_FN_ACC ALPAKA_FN_INLINE uint64_t afirstBidKey(int mode, int nLayers, uint32_t orderedLogit) {
      uint64_t const logitPart = static_cast<uint64_t>(orderedLogit);
      if (mode == kModeLongest) {
        uint32_t const lenPart = (nLayers < 0) ? 0u : (nLayers > 255 ? 255u : static_cast<uint32_t>(nLayers));
        return (static_cast<uint64_t>(lenPart) << 32) | logitPart;
      }
      return logitPart;
    }
  }  // namespace chainafirst

  // The evidence-pass target list: gate-alive candidates long enough to be granted evidence and
  // eligible for stage A's dcaXY gate. Indexed by chain, so ChainCompactSelect with a null source
  // publishes chain indices in ascending order.
  struct ChainAfirstTargetFlags {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* candKeep,
                                  uint32_t* keep,
                                  int minLayers,
                                  ChainConfig config) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());
      for (uint32_t chainIdx : cms::alpakatools::uniform_elements(acc, nChains)) {
        bool const alive = candKeep[chainIdx] != 0u;
        bool const longEnough = chains.nLayers()[chainIdx] >= minLayers;
        bool const fittable = !(chains.dcaXY()[chainIdx] >= config.attachDcaMax);
        keep[chainIdx] = (alive && longEnough && fittable) ? 1u : 0u;
      }
    }
  };

  // Per-seed reduction of the bidders. One entry per evidence target, the same packed key the
  // deployed scorer reduces (attachContendKey), so the seed row and the ordered logit are read back
  // exactly. Targets with no above-bar pair (key 0) bid nothing.
  struct ChainAfirstSeedReduce {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* targets,
                                  uint64_t const* tgtKey,
                                  uint32_t nTargets,
                                  uint32_t nPls,
                                  uint64_t* plsBid,
                                  int mode,
                                  uint32_t* stats) const {
      for (uint32_t position : cms::alpakatools::uniform_elements(acc, nTargets)) {
        uint64_t const key = tgtKey[position];
        if (key == 0u)
          continue;
        if (stats != nullptr)
          alpaka::atomicAdd(acc, &stats[1], 1u, alpaka::hierarchy::Threads{});
        uint32_t const seedIdx = 0xFFFFFFFFu - static_cast<uint32_t>(key & 0xFFFFFFFFu);
        if (seedIdx >= nPls)
          continue;
        uint32_t const orderedLogit = static_cast<uint32_t>(key >> 32);
        int const nLayers = chains.nLayers()[targets[position]];
        alpaka::atomicMax(acc,
                          &plsBid[seedIdx],
                          chainafirst::afirstBidKey(mode, nLayers, orderedLogit),
                          alpaka::hierarchy::Blocks{});
      }
    }
  };

  // The evidence byte, per chain. BAR grants it to every above-bar argmax; MUTUAL and LONGEST also
  // require this chain to hold the seed's winning bid. A tie in the bid key grants evidence to both
  // holders, which is deliberate: the tie is exact and the claim resolves it under its own total
  // order rather than here.
  struct ChainAfirstEvidence {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* targets,
                                  uint64_t const* tgtKey,
                                  uint32_t nTargets,
                                  uint32_t nPls,
                                  uint64_t const* plsBid,
                                  uint8_t* evidence,
                                  int mode,
                                  uint32_t* stats) const {
      for (uint32_t position : cms::alpakatools::uniform_elements(acc, nTargets)) {
        uint64_t const key = tgtKey[position];
        if (key == 0u && mode != chainafirst::kModeLenOnly)
          continue;
        uint32_t const chainIdx = targets[position];
        bool grant = (mode == chainafirst::kModeBar) || (mode == chainafirst::kModeLenOnly);
        if (!grant) {
          uint32_t const seedIdx = 0xFFFFFFFFu - static_cast<uint32_t>(key & 0xFFFFFFFFu);
          if (seedIdx < nPls) {
            uint32_t const orderedLogit = static_cast<uint32_t>(key >> 32);
            int const nLayers = chains.nLayers()[chainIdx];
            grant = (plsBid[seedIdx] == chainafirst::afirstBidKey(mode, nLayers, orderedLogit));
          }
        }
        if (!grant)
          continue;
        evidence[chainIdx] = 1u;
        if (stats != nullptr)
          alpaka::atomicAdd(acc, &stats[2], 1u, alpaka::hierarchy::Threads{});
      }
    }
  };

  // THE DISPLACED GUARD (LST_CHAIN_AFIRST_DISPFREE=1, default off).
  //
  // Evidence is a PROMPT instrument: it asks whether a pixel seed explains the chain, and a
  // displaced track's own seed frequently does not exist at all. Requiring it therefore ranks every
  // displaced chain behind every prompt seed-carrying one, which is a systematic demotion of
  // exactly the population the round protects. This kernel grants the same order-key term to a
  // long candidate on the DISPLACED branch (the dcaSplit test the attach itself uses to choose the
  // displaced delivery row) without asking for seed evidence, so the arm reorders prompt against
  // prompt and leaves the displaced chains where the shipped key put them relative to them.
  struct ChainAfirstDispGrant {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* candKeep,
                                  uint8_t* evidence,
                                  int minLayers,
                                  float dcaSplit,
                                  uint32_t* stats) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());
      for (uint32_t chainIdx : cms::alpakatools::uniform_elements(acc, nChains)) {
        if (candKeep[chainIdx] == 0u || chains.nLayers()[chainIdx] < minLayers)
          continue;
        // NaN-rejecting form, the same one ChainAttachTargetPre uses: an unfittable dca counts as
        // displaced, never prompt.
        if (chains.dcaXY()[chainIdx] < dcaSplit)
          continue;
        if (evidence[chainIdx] == 0u && stats != nullptr)
          alpaka::atomicAdd(acc, &stats[4], 1u, alpaka::hierarchy::Threads{});
        evidence[chainIdx] = 1u;
      }
    }
  };

  // The one line of the arm that changes a decision: the evidence term of the claim's order key.
  // A pure addition on a per-chain column that the rank reads afterwards -- the comparator, the
  // rank and the walk are untouched.
  struct ChainAfirstKey {
    ALPAKA_FN_ACC void operator()(
        Acc1D const& acc, Chains chains, uint8_t const* evidence, float bonus, uint32_t* stats) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());
      for (uint32_t chainIdx : cms::alpakatools::uniform_elements(acc, nChains)) {
        if (evidence[chainIdx] == 0u)
          continue;
        chains.orderKey()[chainIdx] += bonus;
        if (stats != nullptr)
          alpaka::atomicAdd(acc, &stats[3], 1u, alpaka::hierarchy::Threads{});
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
