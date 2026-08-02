#ifndef PROTOTYPE_STAGES_H
#define PROTOTYPE_STAGES_H

// Chain-tracking stage functions, one per planned Alpaka kernel (plan section 3 K1-K10).
// Each stage is a pure function over flat arrays: reads LSTEventData + earlier ChainGraph
// fields, writes its own ChainGraph fields. No hidden state, no pointer-chasing — the GPU
// port wraps each function in a kernel and grid-strides the outer loop (plan 10.3).
//
// Only K1/K2 are declared for the M1 milestone (incidence + edge enumeration + counts).
// K3-K10 are added as their milestones start.

#include <cstdint>
#include <vector>

#include "EventData.h"

struct ChainGraph {
  // K1 output: CSR incidence, MD -> T3s. "out" = T3s whose first MD is m (chain continues
  // outward through them), "in" = T3s whose last MD is m.
  std::vector<int> mdT3OutOffsets, mdT3OutItems;  // offsets size nMD+1
  std::vector<int> mdT3InOffsets, mdT3InItems;
  // K1 output: CSR incidence, LS -> T3s (for E2 / T4-relation edges).
  std::vector<int> lsT3OutOffsets, lsT3OutItems;  // T3s whose first LS is l
  std::vector<int> lsT3InOffsets, lsT3InItems;    // T3s whose second LS is l

  // K2 output: edges. E1 = shared middle MD (inner.md2 == outer.md0), the T5 relation;
  // E2 = shared LS (inner.ls1 == outer.ls0), the T4 relation. E2 edges that duplicate an
  // E1 edge (same T3 pair) are not stored.
  struct Edge {
    int inner = -1, outer = -1;  // T3 node indices
    uint8_t type = 0;            // 1 = E1 (shared MD), 2 = E2 (shared LS)
  };
  std::vector<Edge> edges;
  // Exact-count bookkeeping (the degree-arithmetic allocation check, plan 3b):
  long long e1CountExact = 0, e2CountExact = 0;  // sum over MDs/LSs of degIn*degOut
};

// K1: build the four incidence CSRs. Count -> prefix -> scatter, no sorting.
void k1BuildIncidence(const LSTEventData& ev, ChainGraph& g);

// K2: enumerate edges from the incidence index (every MD-matched / LS-matched T3 pair),
// verify degree-arithmetic exact counts, fill g.edges. Gates come later (M2); at M1 this
// measures the raw E distribution (the plan's E-count calibration gate).
void k2BuildEdges(const LSTEventData& ev, ChainGraph& g);

// ---------------------------------------------------------------------------------------
// K6: mutual-best welding (plan section 3 K6). Edges carry a log-odds score (the MLP
// LOGIT, so summing scores = summing log-odds); only edges with logOdds >= thetaEdge
// participate. Each node may take at most one in-weld and one out-weld:
//   sweep: bestOut(n) = highest-score eligible out-edge of n whose partner's in-slot is
//   free; bestIn(m) analogous; weld e=(n,m) iff e == bestOut(n) AND e == bestIn(m).
//   Repeat kWeldSweeps times (later sweeps let losers pair with remaining partners —
//   the "second-best fallback").
// Result: in/out degree <= 1 => disjoint simple paths of T3 nodes.
// Chains = maximal paths with >= 2 nodes (a single T3 is not a TC without pixel help).
// Chain score = sum of member edge logOdds + lambdaLen * nLayers, where nLayers = number
// of DISTINCT md_layer values over the chain's MD set (plan 5a length prior).

struct EdgeScores {
  std::vector<float> logOdds;  // size g.edges.size(); MLP logit or +/-inf for oracle
};

struct Chains {
  // CSR: chain c -> ordered T3 node list, innermost first (weld direction inner->outer).
  std::vector<int> offsets, items;
  std::vector<float> score;
  std::vector<int> nLayers;
  // Deduped MD list per chain (CSR), innermost-first order, for hit lists / arbitration:
  std::vector<int> mdOffsets, mdItems;
  // Weld-edge indices per chain (CSR into g.edges): the edges K6 actually welded between
  // consecutive items (nNodes - 1 per chain, innermost-first). Needed by ChainFeatures
  // (M6 chain gate): logit aggregates / junction degrees must use the WELDED edges --
  // recomputing them would be ambiguous for parallel E1/E2 edges of the same node pair.
  std::vector<int> edgeOffsets, edgeItems;
};

constexpr int kWeldSweeps = 3;

void k6WeldChains(const LSTEventData& ev,
                  const ChainGraph& g,
                  const EdgeScores& s,
                  float thetaEdge,
                  float lambdaLen,
                  Chains& out);

// ---------------------------------------------------------------------------------------
// K9 (prototype v1): score-ordered hit-claim arbitration over chains (plan section 3 K9,
// simplified: greedy serial stand-in for the packed-atomicMax kernel — physics-equivalent
// on CPU, order by (score desc, chain index asc) for determinism).
//   - Only chains with score >= thetaChain(nLayers) participate; the threshold is
//     per final track length (plan 10.5 per-length care: ==4 -> thetaChain4,
//     ==5 -> thetaChain5, >=6 -> thetaChain6; <4 uses thetaChain4).
//   - Chains whose T3s include any t3_partOfPT5 / t3_partOfPT3 member are DROPPED first
//     (structural mimic of the baseline CrossCleanT5/pT3 consumption: those tracks are
//     already delivered by the kept baseline pixel TCs in hybrid mode).
//   - Walk chains best-first; a chain is ACCEPTED iff (claimed MDs)/(total MDs) <=
//     maxClaimedFrac; accepted chains claim all their MDs.
// Output: accepted chain indices, best-first.
struct ArbitrationParams {
  // Per-length chain-score acceptance thresholds (plan 10.5: LST tunes T4s and T5s very
  // differently, so acceptance is per chain nLayers at minimum).
  float thetaChain4 = 0.f;     // nLayers <= 4 (T4-class)
  float thetaChain5 = 0.f;     // nLayers == 5 (T5-class)
  float thetaChain6 = 0.f;     // nLayers >= 6
  // M9 (-G 3/-G 4 -U4/-U5/-U6): the DCA-split gate scores chains on TWO scales -- gate
  // logit for IP-compatible chains, legacy sum-logit for dca-exempt ones -- so ONE
  // threshold set cannot serve both (a gate-scale T4=2 is nearly a no-op on the legacy
  // scale: the m9_v1 exempt-T4 fake flood). Chains flagged in altThreshold use the
  // thetaAlt* set (legacy scale); everyone else uses thetaChain*. nullptr = legacy
  // behavior, bit-exact for -G 0/1/2 and every pre-M9 caller.
  float thetaAlt4 = 0.f;       // exempt nLayers <= 4
  float thetaAlt5 = 0.f;       // exempt nLayers == 5
  float thetaAlt6 = 0.f;       // exempt nLayers >= 6
  const std::vector<char>* altThreshold = nullptr;  // per-chain: 1 = use thetaAlt*
  float maxClaimedFrac = 0.3f; // max fraction of already-claimed MDs tolerated
  // B4 (-FC / -FCX): LENGTH-NORMALIZED claim tolerance. The fractional test is
  // length-monotone in the WRONG direction for short displaced chains: at F=0.3 a 5-MD
  // chain dies at 2 shared MDs (0.4 > 0.3) while a 7-MD chain survives 2 -- the M10
  // forensics "losers are PURE 5-MD displaced chains starved by the claim" population.
  // maxClaimedItems >= 0 turns on an ABSOLUTE tolerance in claim-universe items (hits
  // when hitLevelClaim, MDs otherwise; main.cc does the MD->hit x2 conversion):
  //   claimCountExclusive == false (default): accept iff nClaimed <= maxClaimedItems OR
  //     frac <= maxClaimedFrac  -- a pure LOOSENING that only binds on short chains
  //     (for long chains the fraction is already the looser of the two).
  //   claimCountExclusive == true: the count REPLACES the fraction entirely (tightens
  //     long chains as well).
  // maxClaimedItems < 0 = off => bit-exact legacy fractional test.
  int maxClaimedItems = -1;
  bool claimCountExclusive = false;
  // DUPCUT (-FCE): restrict the claimCountExclusive TIGHTENING to the chains flagged 0
  // here, i.e. leave the flagged ones on the legacy loosen-only OR. The mask supplied is
  // the -G 5/-G 6 exempt mask (dcaXY >= dcaSplit), so the strict count binds on
  // IP-compatible chains only and the displaced population keeps the tolerance it needs.
  // nullptr = every chain is strict == bit-exact pre-DUPCUT behavior.
  const std::vector<char>* strictExemptMask = nullptr;
  // DUPCUT (-FCE 2): the same mask additionally exempts those chains from the -W braid
  // kill, so a braidFrac tightening can be aimed at the IP population alone.
  bool strictExemptBraid = false;
  // M17 claimshare (-FS): SUBORDINATE SHARE PASS. Raising maxClaimedFrac outright is not a
  // pure loosening of the accepted set: a chain that only the looser tolerance admits
  // CLAIMS its hits, which can push a later (lower-key) chain that the tight tolerance
  // would have accepted over the limit. Measured at -F 0.5: +36k duplicate TCs and +6.4k
  // fakes for a net +68 matched sims, because the evictions eat most of the recovery.
  // sharePassFrac > maxClaimedFrac runs a SECOND greedy pass over exactly the candidates
  // the first pass rejected, in the same order, continuing on the first pass's owner map
  // with the looser tolerance. The pass-1 accepted set is therefore bit-identical to the
  // -FS-off run BY CONSTRUCTION (efficiency can only go up), and pass-2 chains arbitrate
  // among themselves but can never evict a pass-1 chain. 0 = off (bit-exact legacy).
  float sharePassFrac = 0.f;
  bool dropPixelConsumed = true;
  // M16 (-A 4 replacement modes): the pixel-consumed drop exists ONLY because the kept
  // baseline pixel TC already delivers that track. Under -RT5 1 every carried type-7 row
  // is dropped and the attached chains deliver instead, so the partOfPT5 half of the
  // crossclean would be killing chains for colliding with rows that NO LONGER EXIST;
  // -RT3 1 says the same about partOfPT3. These two switches turn the halves off
  // independently. Both true (with dropPixelConsumed) == bit-exact legacy.
  bool dropPartOfPT5 = true;
  bool dropPartOfPT3 = true;
  // M16 (-A 4): chains flagged here are NOT K9 candidates at all. Used for the attached
  // chains, which are PIXEL-BACKED deliveries: they pre-claim their hits alongside the
  // surviving carried pixel rows (via preClaimOwners) and are emitted unconditionally,
  // exactly like a carried row -- so letting them also walk the greedy claim would
  // deliver them twice. nullptr = legacy (bit-exact).
  const std::vector<char>* excludeChain = nullptr;
  // A8 (fake-aware ordering, -B): K9's best-first ORDER key. nullptr = order by
  // chains.score (legacy, bit-exact). When set (size nChains) the greedy walk visits
  // chains by orderKey desc / index asc, while ACCEPTANCE thresholds still cut on
  // chains.score -- the M9 "only fake-specific ordering/kills move FR" lesson applied on
  // the ordering axis WITHOUT the cross-scale inversion (thresholds never see the key).
  const std::vector<float>* orderKey = nullptr;
  // A8 (braid suppression, -W): kill a candidate outright when it overlaps an
  // ALREADY-ACCEPTED chain by >= braidFrac of THAT ACCEPTED chain's MDs (owner-relative,
  // as opposed to the candidate-relative maxClaimedFrac test). Targets the genuine
  // welder-braid duplicates: a long sibling can swallow a short accepted chain whole
  // while its own claimed fraction stays under F. 0 = off (bit-exact legacy).
  float braidFrac = 0.f;
  // EX_DUPCC (-WE/-WZ/-WN): BAND-AWARE BRAID. Candidates flagged in braidAltMask use
  // braidFracAlt instead of braidFrac. The mask is a per-chain geometry/topology band
  // (main.cc builds it from |eta| >= -WZ and nNodes <= -WN), so the disks can run a
  // tighter owner-relative duplicate test than the barrel without touching the
  // candidate-relative claim budget. nullptr / 0 = bit-exact legacy: every chain uses
  // braidFrac, and a 0 effective fraction means "no braid test for this candidate".
  float braidFracAlt = 0.f;
  const std::vector<char>* braidAltMask = nullptr;
  // EX_DUPCC (-FB/-FBC): PER-BAND CLAIM TOLERANCE. Same band mask as the band braid.
  // maxClaimedFracAlt <= 0 / maxClaimedItemsAlt == -2 mean "band uses the global value",
  // so the defaults are bit-exact legacy.
  float maxClaimedFracAlt = 0.f;
  int maxClaimedItemsAlt = -2;
  // A8 (-H 1): run the claim (and the braid test) on HIT indices instead of MD indices.
  // Genuine chain-chain duplicates are built from DIFFERENT T3/MD objects sitting on the
  // same hits, so an MD-level claim map cannot see them at all; the hit-level map can.
  bool hitLevelClaim = false;
  // B1 (claim-universe unification, -PU): the KEPT baseline pixel TCs' outer-tracker hits
  // are PRE-CLAIMED before the greedy walk, so a chain riding on a pixel-delivered track's
  // hits faces exactly the same maxClaimedFrac (and, at mode 2, braid) rules it would face
  // against another chain. Today K9 arbitrates chains vs chains ONLY, which is why 48.9%
  // of surviving fake chains sit on a kept pixel TC's hits (M13 recon) and why the
  // chain-vs-pixel dup artifact cannot be removed by any chain-side lever.
  // preClaimOwners[p] = the ph2 hit rows of pixel owner p (type-7 pT5 -> t5_hitIndices,
  // type-5 pT3 -> pT3_otHitIndices; type-8 pLS TCs own no OT hits). Pixel owners claim
  // FIRST and unconditionally (they are carried verbatim into the output in hybrid mode),
  // in list order, first-come-first-served among themselves.
  //   preClaimMode 0 = off (nullptr semantics, bit-exact legacy)
  //                1 = pre-claim; pixel owners participate in the maxClaimedFrac test
  //                2 = 1 + pixel owners also participate in the owner-relative braid test
  // At MD-level claim (-H 0) an MD is pixel-owned iff BOTH of its hits are pixel hits --
  // the exact "this MD sits on the pixel TC's hits" statement, which also catches
  // duplicate MD objects built from the same hit pair.
  const std::vector<std::vector<int>>* preClaimOwners = nullptr;
  int preClaimMode = 0;
  // Optional diagnostics sink (B1): filled by k9Arbitrate, never read by it. nullptr = off.
  struct Stats {
    long long preClaimedSlots = 0;  // universe slots owned by pixel owners at walk start
    long long killedByPixFrac = 0;  // candidates failing maxClaimedFrac WITH >=1 pixel slot
    long long killedByPixBraid = 0; // candidates braid-killed by a PIXEL owner (mode 2)
  };
  Stats* stats = nullptr;

  float thetaFor(int nLayers) const {
    return nLayers >= 6 ? thetaChain6 : (nLayers == 5 ? thetaChain5 : thetaChain4);
  }
  float thetaForChain(int chain, int nLayers) const {
    if (altThreshold != nullptr && chain < static_cast<int>(altThreshold->size()) && (*altThreshold)[chain])
      return nLayers >= 6 ? thetaAlt6 : (nLayers == 5 ? thetaAlt5 : thetaAlt4);
    return thetaFor(nLayers);
  }
};
// bypassPT5Drop (optional, size nChains; M7 K8 attach): chains flagged 1 skip the
// partOfPT5 HALF of the pixel-consumed drop -- a K8-attached chain IS the pT5
// replacement for its pixel seed, so the crossclean that exists only because the track
// was delivered by a kept baseline pixel TC must not kill it. The partOfPT3 half still
// drops (pT3 rows are untouched by attach in v1). nullptr = legacy behavior (bit-exact).
// ownerOut (optional): a COPY of the final claim-universe owner map (-1 = free, >= 0 =
// accepted chain, <= -2 = pixel owner), indexed by HIT row when params.hitLevelClaim and
// by MD row otherwise. Written once at the end; k9Arbitrate never reads it, so passing it
// cannot change any decision. Consumed by the EXPLOIT chain-extension pass, which needs
// to know which rows are still unclaimed after arbitration.
void k9Arbitrate(const LSTEventData& ev,
                 const Chains& chains,
                 const ArbitrationParams& params,
                 std::vector<int>& acceptedChains,
                 const std::vector<char>* bypassPT5Drop = nullptr,
                 std::vector<int>* ownerOut = nullptr);

// K9 two-pass (M7b, hybrid -A 2): SUBORDINATE claim for K8-attached chains. The v1 (-A 1)
// bypass re-admitted ~550 prompt chains/evt into the ONE shared MD claim, which evicted
// displaced chains (vxy[10,30) regressed monotonically with attach count). The fix is
// structural ordering:
//   Pass 1 = EXACTLY the legacy pipeline (theta gate -> pixel-consumed drop WITHOUT any
//   bypass -> greedy claim). Implemented as a call to k9Arbitrate(..., nullptr), so the
//   accepted set is bit-identical to -A 0 BY CONSTRUCTION, whatever K8 decided.
//   Pass 2 = chains that ATTACHED a pLS (attachedPls[c] >= 0) and were dropped in pass 1
//   SOLELY by the partOfPT5 half of the pixel drop (chains with a partOfPT3 member still
//   drop -- the v1 rule; chains that survived the pixdrop but lost the pass-1 claim get
//   no retry: the final claim map is a superset of what they failed against). Candidates
//   run the same greedy claim (score desc, index asc; same maxClaimedFrac) starting from
//   the claimed-MD map LEFT BY pass 1, which keeps evolving as pass-2 chains accept --
//   so pass-2 chains arbitrate among themselves but can NEVER evict a pass-1 chain.
// acceptedPass2 is disjoint from acceptedPass1; the caller turns pass-2 acceptances into
// type-7 TCs (pixel hits + OT hits) and upgrades attached pass-1 chains in place.
void k9ArbitrateTwoPass(const LSTEventData& ev,
                        const Chains& chains,
                        const ArbitrationParams& params,
                        const std::vector<int>& attachedPls,  // per-chain pLS row or -1
                        std::vector<int>& acceptedPass1,
                        std::vector<int>& acceptedPass2);

// K10 (prototype v1): accepted chains -> OutTC-ready records.
//   type: nLayers >= 5 -> 4 (T5-class), nLayers == 4 -> 9 (T4-class); nLayers < 4 dropped.
//   pt = median of member t3_pt; eta/phi = innermost member's t3_eta / t3_phi;
//   nhitOT = 2 * nMDs; hit list = md_anchorHitIdx + md_otherHitIdx of mdItems (all
//   Phase2OT; chains never contain pLS pseudo-MDs).
// Declared here, defined in the assembly TU; OutTC comes from OutputWriter.h.
struct ChainTC {
  float pt = 0, eta = 0, phi = 0;
  int type = 4, nhitOT = 0;
  std::vector<unsigned int> hitIdxs;  // ph2 rows
};
void k10AssembleChainTCs(const LSTEventData& ev,
                         const Chains& chains,
                         const std::vector<int>& acceptedChains,
                         std::vector<ChainTC>& out);

#endif
