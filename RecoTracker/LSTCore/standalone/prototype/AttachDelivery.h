#ifndef PROTOTYPE_ATTACHDELIVERY_H
#define PROTOTYPE_ATTACHDELIVERY_H

// M16 -- ATTACH AS THE DELIVERY PATH (plan section 11 "PIXEL-REPLACEMENT VALIDATION").
//
// The M7 attach head was validated as a CLASSIFIER only: every hybrid winner ran attach
// OFF and carried the baseline pixel TCs verbatim. This file is the delivery layer that
// makes the general pLS->OT attach actually PRODUCE the pixel-class track candidates, so
// the replacement A/Bs the maintainer requires can be measured:
//   * type-7 (pT5-class) TCs from (pLS, chain) pairs,
//   * type-5 (pT3-class) TCs from (pLS, bare-T3) pairs,
//   * one contention rule (ONE pLS, ONE owner) replacing CrossCleanpT5/pT3/pLS.
//
// SCOPE / OWNERSHIP: the pair enumeration, the features, the prefilter and the head are
// PixelAttach.{h,cc} / AttachInference (M16 Task A). This file only (a) scores the
// enumerated pairs, (b) runs the cross-type contention, and (c) hands main.cc a decision
// record. It adds no physics of its own beyond the per-class acceptance margins.
//
// ---------------------------------------------------------------------------------------
// THE TWO-STAGE ORDER (and why it is not one global logit sort)
//
// The delivery order main.cc must obey (M16 mandate) is
//     attach decisions -> suppression set -> pre-claim owner list -> K9
// because a carried pixel row that attach has REPLACED must not pre-claim hits it no
// longer owns. That forces the CHAIN-target attach to run BEFORE K9 (over the theta-
// passing chains, exactly as -A 2/-A 3 already do).
//
// The BARE-T3 universe, however, is only defined AFTER K9: a "bare" T3 is one that no
// ACCEPTED chain consumed (k8BuildBareT3Mask's documented contract). A T3 welded into a
// chain that K9 then rejected is still bare and still attachable -- that is exactly the
// pT3-class population LST recovers with its superbin machinery, so defining bareness
// against the theta-passing set instead would throw it away.
//
// Hence two stages:
//   STAGE A (pre-K9)  chain targets  -> gaStageChains
//   STAGE B (post-K9) bare-T3 targets -> gaStageT3, competing only for pLS that stage A
//                                        left UNOWNED.
// The ONE-pLS-ONE-OWNER invariant holds globally across both stages. What the split
// gives up is a single global logit sort: a pLS whose best T3 pair outscores its best
// chain pair still goes to the chain. That is the deliberate PER-LENGTH ordering (a
// 5+-layer target is a stronger object than a 3-layer one; the same principle that makes
// -AT3 tight by default), and it is documented here as the hybrid-order approximation --
// at real integration both universes exist simultaneously and the sort can be global.
//
// PER-CLASS MARGINS: chain targets accept at params.pref.thetaAttach (-a); bare-T3
// targets accept at params.thetaAttachT3 (-AT3), TIGHT by default. Rationale (plan 11,
// "TC class = label from target layer count with per-class margins, 3-layer objects
// tightest"): a 3-layer target contributes far less independent evidence, so the pair
// head must be far more certain before a pT3-class TC is delivered.

#include <vector>

#include "ChainFeatures.h"
#include "EventData.h"
#include "PixelAttach.h"
#include "Stages.h"

struct GeneralAttachParams {
  AttachParams pref;           // prefilter windows + thetaAttach (chain-target margin, -a)
  float thetaAttachT3 = 6.0f;  // -AT3: bare-T3-target margin on the SAME logit scale
};

struct GeneralAttach {
  // Per CHAIN row (size = nChains): the pLS this chain owns, or -1.
  std::vector<int> chainPls;
  std::vector<float> chainLogit;
  // Per T3 row (size = nT3): the pLS this bare T3 owns, or -1.
  std::vector<int> t3Pls;
  std::vector<float> t3Logit;
  // Per pLS row: 1 once ANY target owns it (the cross-type exclusivity bookkeeping).
  std::vector<char> plsOwned;
  // Per pLS row: best scored-pair logit seen in each stage, BEFORE any threshold and
  // BEFORE contention. "This pLS had real outer-tracker evidence but is not an owner"
  // is exactly (plsBest*Logit >= the class margin) && !plsOwned -- the -RPS
  // "lost a contention above -a" predicate.
  std::vector<float> plsBestChainLogit, plsBestT3Logit;
  long long nPairs = 0, nScored = 0, nChainAttached = 0, nT3Attached = 0;

  // ---- ATTACH CONFUSION-MATRIX INSTRUMENT (harness-invisible, off by default) ---------
  // The maintainer judging protocol for pLS->OT matching requires classifying EVERY
  // attach decision AND every non-attached true pair by the sim truth of both sides.
  // Doing that from a re-enumeration would risk drifting from the decision actually
  // taken, so the instrument records the pairs the decision was made from: when
  // recordPairs is set, each SCORED pair is appended verbatim (target identity, pLS row,
  // head logit) as the stages run. Nothing else in this struct or in any stage changes,
  // so with recordPairs == false the delivery is bit-identical to the uninstrumented
  // tree and costs one untaken branch per pair.
  bool recordPairs = false;
  struct ScoredPair {
    int8_t ttype;  // kAttachTargetChain (0) or kAttachTargetT3 (1)
    int tgtRow;    // CHAIN ROW (not the targetChains position) for ttype 0, t3 row for ttype 1
    int plsRow;
    float logit;
  };
  std::vector<ScoredPair> pairLog;
};

// Sizes the per-row vectors and clears the decision state. Call once per event before
// gaStageChains.
void gaInit(const LSTEventData& ev, const Chains& chains, GeneralAttach& out);

// STAGE A -- chain targets. targetChains = the chain rows eligible to bid (main.cc
// applies the theta gate and, optionally, a dca eligibility gate first); k8 itself only
// bids nLayers >= 5. Per target: best pLS with logit >= pref.thetaAttach (ties -> lower
// pLS row, the k8AttachPixels convention). Then pLS contention: higher logit wins, tie
// keeps the earlier target position. Losers get nothing (no fallback, the M7 v1 rule).
void gaStageChains(const LSTEventData& ev,
                   const Chains& chains,
                   const std::vector<int>& targetChains,
                   const ChainFeatures& cf,
                   const std::vector<float>& chainGateLogits,
                   const GeneralAttachParams& params,
                   GeneralAttach& io);

// STAGE B -- bare-T3 targets. acceptedChains = the K9-accepted set (defines bareness via
// k8BuildBareT3Mask). Only pLS with io.plsOwned == 0 may be bid for, so stage A's
// decisions are final. Acceptance margin = params.thetaAttachT3.
void gaStageT3(const LSTEventData& ev,
               const Chains& chains,
               const std::vector<int>& acceptedChains,
               const ChainFeatures& cf,
               const std::vector<float>& chainGateLogits,
               const GeneralAttachParams& params,
               GeneralAttach& io);

#endif
