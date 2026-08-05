// Driver for the chain-tracking offline prototype (plan section 10).
// Modes:
//   identity (M0): read the LST ntuple, re-emit tc_* / sim_tcIdx verbatim through
//                  OutputWriter so the production efficiency harness validates the loop.
//   graph    (M1): run K1 (incidence) + K2 (edge enumeration) per event and report
//                  exact-vs-emitted edge counts and stage wall times.
//   dump     (M2): graph + node/edge features + track-level edge labels, written as a
//                  flat per-edge training TTree via DumpWriter.
//   chains   (M3): graph + features + MLP edge inference (EdgeInference.h) + K6 welding;
//                  per-event chain multiplicity / nLayers histograms and stage timings.
//   oracle   (M3): K6 welding with TRUTH edge scores (logOdds +10 for label==1 edges,
//                  -10 otherwise, thetaEdge fixed to 0) instead of the MLP; reports the
//                  oracle ceiling table (chain-formable sims reached by oracle chains)
//                  against the baseline sim_tcIdx outcome.
//   hybrid   (M4): full A/B chain: graph + features + MLP inference + K6 welding + K9
//                  hit-claim arbitration + K10 TC assembly; output file = kept baseline
//                  pixel TCs (pT5/pT3/pLS) + the prototype chain TCs, written through
//                  OutputWriter::fillEventHybrid for the unchanged efficiency harness.
//                  -A 1 (M7, REJECTED v1, kept for reference) adds K8 pixel attach:
//                  after the theta gate and BEFORE the pixel-consumed drop, 5+-layer
//                  chains bid for pLS seeds via the trained attach head (threshold -a);
//                  attached chains bypass the partOfPT5 drop, become type-7 (pT5-class)
//                  TCs (pixel hits + OT hits, pt = pLS ptIn), and their pLS's baseline
//                  pT5/pLS rows are suppressed. Rejected: the bypass floods the ONE
//                  shared MD claim with re-admitted prompt chains which EVICT displaced
//                  chains, and own-row suppression leaves the dup floor unchanged.
//                  -A 2 (M7b) is the corrected integration: SUBORDINATE TWO-PASS claim
//                  (pass 1 = exactly the -A 0 pipeline, bit-identical accepted set by
//                  construction; pass 2 = attached chains pixdropped in pass 1 claim
//                  only against the post-pass-1 MD map -- they can never evict a pass-1
//                  chain) + attached pass-1 chains upgraded in place to type 7 + sim-
//                  blind SEED-FAMILY suppression (kept-baseline pT5/pT3/pLS rows whose
//                  own pLS shares >= 2 pixel hits with any attached pLS are dropped,
//                  mirroring production pixelHitsOverlapAny).
//                  M7c refinements (all -A 2 only, all sim-blind): (a) IP-compatibility
//                  gate on attach ELIGIBILITY -- only chains whose full-fit circle has
//                  transverse DCA to the origin < dcaMax (-D) may attach (pass-2
//                  candidacy AND in-place upgrade); displaced chains keep their bare
//                  deliveries (fixes the M7b upgrade dilution: wrong pLS on displaced
//                  chains poisoned the >0.75 match). (b) KINEMATIC SUPPRESSION GUARD --
//                  a family-suppressed row is actually dropped only if it is
//                  kinematically compatible with the attaching chain+pLS TC
//                  (dR < -S AND pt ratio < 2; fixes the M7b suppression collateral:
//                  seed-sharing rows of DIFFERENT tracks were dropped). (c) K7-LITE
//                  kinematic dedup (-K 1, default) -- after attach + suppression, bare
//                  (non-attached) chain TCs that are IP-compatible (-D) and
//                  kinematically match a KEPT baseline type-7 row (dR < -R, pt ratio
//                  < 2) are dropped as redundant re-deliveries of pixel-delivered
//                  tracks; the dca gate protects displaced chains from qualifying.
//                  M9 additions (hybrid mode):
//                  -G 3 (DCA-split gate): the K9 acceptance/ordering score is the
//                  chain-gate LOGIT for chains whose full-fit circle has transverse DCA
//                  to the origin < dcaSplit (-X, default 0.5 cm; IP-compatible chains --
//                  where the fakes live and the gate's discrimination is strong) and the
//                  LEGACY sum-logit + lambdaLen*nLayers score for chains with dcaXY >=
//                  dcaSplit (the large-dxy secondaries the full gate keeps killing).
//                  Per-length thresholds -T4/-T5/-T6 cut on whichever scale governs that
//                  chain: gate-logit scale for IP-compatible chains, legacy scale for
//                  exempt ones -- so exempt chains at T5=0/T6=0 face effectively no cut,
//                  exactly like the -G 2 anchor's >=5-layer path. Structural limits:
//                  -X 0 makes every chain exempt (= -G 0 at the same thresholds);
//                  -X 1e9 makes every chain gated (= -G 1).
//                  M9 MEASURED (300-evt A/Bs m9_v1/v1b/v2/v3): with ONE threshold set
//                  shared across the two score scales, -G 3 FAILS -- a gate-scale
//                  T4=2 is nearly a no-op on the legacy scale, so the exempt
//                  (dca >= dcaSplit) T4-class population floods in (accepted T4s x7
//                  vs the -G 2 anchor), exploding fake AND evicting gated 5+
//                  displaced chains in the MD claim (vxy bands drop below base); the
//                  -X/-T4/-T5 axes move the two failures in opposite directions.
//                  FIX (maintainer steer): the exempt branch has its OWN per-length
//                  thresholds -U4/-U5/-U6 (legacy scale, defaults 6/0/0 -- U4=6 is
//                  the pre-gate r5-winner legacy T4 threshold); -T4/-T5/-T6 keep
//                  governing the gate-scored chains.
//                  -G 4 (M9 repair, length+DCA split): T4-class (nLayers <= 4) chains
//                  are ALWAYS gate-scored (identical to the -G 2 anchor treatment --
//                  no exempt-T4 flood); 5+-layer chains are DCA-split as in -G 3 (gate
//                  logit iff dcaXY < dcaSplit, legacy score for the large-DCA
//                  secondaries the full gate keeps killing). Structural limits:
//                  -X 0 == -G 2 (all 5+ exempt); -X 1e9 == -G 1 (all gated).
//                  -G 5 (M9 order-preserving DCA split; the v1c diagnosis fix): the
//                  m9_v1c failure was CROSS-SCALE ORDERING INVERSION -- fake chains
//                  are braids of true-track MDs, so an exempt (legacy-scored, large
//                  numeric score) fake orders ABOVE its gate-scored (small logit) true
//                  sibling and evicts it in the saturated MD claim (claim volume is
//                  conserved: upstream cuts only change WHO wins). -G 5 therefore
//                  never rewrites chains.score: ORDERING stays legacy for everyone
//                  (the anchor's braid resolution), and the gate acts as a CUT only --
//                  IP-compatible chains (dcaXY < dcaSplit) are killed (score -= 1e9)
//                  when their gate logit is below -T4/-T5/-T6 (gate scale); exempt
//                  chains face the -U4/-U5/-U6 legacy-scale thresholds. K9's base
//                  per-length threshold is internally -1e5 in this mode (no-op for
//                  live chains, rejects killed and -A 3-demoted ones). Structural
//                  limit: -X 0 with -U set u == -G 0 with -T set u.
//                  -Z <cm> (mode 5 only, default 0 = no-op): displaced-oriented
//                  acceptance for the exempt T4 branch (the plan-10.5 LST-style
//                  length-4 restriction, mirroring LST's T4 dxy anti-prompt veto).
//                  The M9 U4 scan showed the exempt-T4 branch is degenerate: the
//                  dxy[1,5)/[5,10) secondaries need U4 <= ~2.7 but the same-U4
//                  small-dca fake/prompt T4 braids dilute barrel/transition track
//                  length below baseline. With -Z, a T4-class chain is EXEMPT only
//                  if dcaXY >= max(dcaSplit, Z); T4s with dca in [dcaSplit, Z) get
//                  the IP treatment (gate kill at -T4). 5+ chains unaffected.
//                  -A 3 (attach-as-evidence, the QUEUED maintainer physics point): run
//                  k8AttachPixels over theta-passing chains exactly as -A 2 does (K8
//                  itself only bids nLayers >= 5 chains), but consume the outcome ONLY
//                  as an acceptance modifier: for IP-compatible chains (dcaXY < dcaSplit,
//                  same -X), a FAILED attach -- no scored pair above thetaAttach,
//                  pre-contention (Attachments::bestLogit) -- DEMOTES the chain by
//                  subtracting -Y from its acceptance score BEFORE K9 ordering AND
//                  thresholds (default -Y 1e6 = reject outright). Large-DCA chains are
//                  exempt (pLS absence is expected off the IP). NO type upgrade, NO
//                  pixel-row suppression, NO hit-list change: chain TCs stay bare
//                  type 4/9 and the writer path is identical to -A 0. Structural limit:
//                  -X 0 exempts every chain -> bit-identical to -A 0.
//   chaindump(M6): graph + features + MLP inference + K6 welding (same -e/-L knobs as
//                  hybrid, r5 shape = -e 0 -L 0.5), then EVERY welded chain
//                  PRE-arbitration is written to a flat TTree "chains" (one entry per
//                  chain: cf_00..cf_15 ChainFeatures + label/simVxy/simPt/nLayers/evt)
//                  -- the chain-gate training factory (plan 5a hard gate).
//   pairdump (M7, GENERALIZED at M16): anchor-shaped pipeline (same knobs as hybrid)
//                  through K9 arbitration, then the K8 analytic PREFILTER runs over all
//                  pLS for EVERY target of the GENERAL pLS->OT attach and every surviving
//                  pair is written to a flat TTree "pairs" (af_00..af_18 PixelAttach
//                  features + ttype/label/wgt/simVxy/simPt/chainNLayers/evt) -- the
//                  attach pair-head training factory. Target kinds (-PDT):
//                    ttype 0 = ACCEPTED CHAIN with nLayers >= 5 (the M7 universe),
//                    ttype 1 = BARE T3, i.e. a T3 in NO K9-accepted chain (mask built
//                              AFTER arbitration) -- the pT3-class delivery universe.
//                  ONE prefilter and ONE feature builder serve both (target-side slots
//                  7-11 mapped for T3s, new feature 18 = targetType). Reports pair
//                  counts, true fractions, DISPLACED STRATA and the prefilter true-pair
//                  efficiency proxy PER TARGET KIND (binding window attributed for
//                  failures). -PDC/-PDS downsample FAKE writes per kind (wgt records the
//                  stride); reported statistics always use the full enumeration.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <unistd.h>

#include "AttachDelivery.h"
#include "AttachInference.h"
#include "ChainFeatures.h"
#include "ChainInference.h"
#include "DumpWriter.h"
#include "EdgeInference.h"
#include "EventData.h"
#include "Extend.h"
#include "Features.h"
#include "Labels.h"
#include "NtupleReader.h"
#include "OutputWriter.h"
#include "PixelAttach.h"
#include "PixelAttachCand.h"
#include "PixelAttachPairs.h"
#include "Stages.h"
#include "Trim.h"

namespace {

void usage(const char* prog) {
  std::fprintf(stderr,
               "Usage: %s -i <lst_ntuple.root> -t <tracking file-or-dir> [options]\n"
               "  -i <file>   LST --allobj ntuple (required)\n"
               "  -t <path>   tracking ntuple file or directory (required)\n"
               "  -o <file>   output ROOT file (required in identity, dump, chaindump, pairdump,\n"
               "              and hybrid modes)\n"
               "  -n <N>      max events to process (default -1 = all)\n"
               "  -m <mode>   identity | graph | dump | chains | oracle | hybrid | chaindump |\n"
               "              pairdump (default identity)\n"
               "  -l <label>  input label for the output TNamed (default PU200)\n"
               "  -e <theta>  thetaEdge: edge log-odds threshold for K6 welding in chains\n"
               "              and hybrid modes (default 0.0; oracle always uses thetaEdge=0)\n"
               "  -L <lam>    lambdaLen: chain-length prior weight passed to K6 in chains,\n"
               "              oracle, and hybrid modes (default 0.0)\n"
               "  -T4/-T5/-T6 <theta>  per-length thetaChain: K9 chain-score acceptance\n"
               "              threshold in hybrid mode, applied by chain nLayers\n"
               "              (==4 -> -T4, ==5 -> -T5, >=6 -> -T6; defaults 0.0)\n"
               "  -T <theta>  shorthand: set thetaChain4/5/6 all to <theta>\n"
               "  -F <frac>   maxClaimedFrac: max fraction of already-claimed MDs a chain may\n"
               "              tolerate in K9 arbitration, hybrid mode (default 0.3)\n"
               "  -FC <n>     (B4) absolute K9 claim tolerance in MD units: a chain also passes when\n"
               "              it has <= floor(n) already-claimed MDs, regardless of its length\n"
               "              (with -H 1 the budget is 2*floor(n) hits). <0 = off (legacy).\n"
               "  -FCX <0|1>  (B4) 1 = the -FC count REPLACES the -F fraction; 0 (default) =\n"
               "              OR with it (loosen-only).\n"
               "  -FCE <0|1|2>  (DUPCUT) 0 (default, bit-exact) = -FCX binds on every chain;\n"
               "              2 = as 1, and the exempt chains additionally skip the -W braid\n"
               "              kill, so -W can be tightened on the IP population alone.\n"
               "              1 = it binds on IP-COMPATIBLE chains only (dcaXY < -X), while\n"
               "              the dca-exempt (displaced) chains keep the loosen-only OR.\n"
               "              The chain-vs-chain duplicates the strict count removes are an\n"
               "              IP-population artifact; the displaced chains are the ones that\n"
               "              legitimately have to ride a few already-claimed hits.\n"
               "  -FS <frac>  (M17) SUBORDINATE share pass: after the -F greedy walk, a SECOND\n"
               "              greedy pass over exactly the chains pass 1 rejected on the claim\n"
               "              budget, same order, same owner map, tolerance <frac>. The pass-1\n"
               "              accepted set is bit-identical to the -FS-off run, so nothing is\n"
               "              evicted (raising -F outright DOES evict). <= -F = off.\n"
               "  -DD <n>     (M17) POST-ARBITRATION structural dedup of the ACCEPTED chains:\n"
               "              walking the accepted list best-first (K9 order), a chain sharing\n"
               "              >= n outer-tracker HIT rows with an already-kept chain is dropped\n"
               "              from the TC output (it frees nothing: the claim map is final).\n"
               "              Shared-hit structure ONLY -- no dR/dEta/dPhi/embedding test --\n"
               "              so hit-disjoint neighbours in a jet can never interact.\n"
               "              <= 0 = off (bit-exact legacy).\n"
               "  -DDF <f>    (M17) extra requirement for -DD: shared >= f * min(hits of the\n"
               "              two chains), so two long chains crossing on a few stray hits are\n"
               "              not called copies. 0 (default) = count test alone.\n"
               "  -DDP <0|1>  (M17) 1 = the -PU pre-claim pixel owners join the dedup as\n"
               "              un-killable kept entries (a chain duplicating a CARRIED pixel TC\n"
               "              is dropped too). 0 (default) = chains only.\n"
               "  -DDK <0|1>  (M17) -DD keep-best key: 0 (default) = the K9 accepted order,\n"
               "              1 = longest chain first (nLayers desc, K9 order on ties).\n"
               "  -OK <0..3>  (B4) K9 ordering-key shape (thresholds stay on chains.score):\n"
               "              0 legacy score, 1 score - lambdaLen*nLayers (pure sum-logit),\n"
               "              2 score/nLayers, 3 mean edge logit. The -B fake penalty is\n"
               "              subtracted after the reshape.\n"
               "  -P          keep pixel-consumed chains in K9 (default: chains containing a\n"
               "              t3_partOfPT5/pT3 member are dropped in hybrid mode)\n"
               "  -PU <0|1|2> B1 claim-universe unification (default 0 = off, bit-exact legacy).\n"
               "              1 = PRE-CLAIM the kept baseline pixel TCs' outer-tracker hits\n"
               "              (type 7 -> pT5_t5Idx -> t5_hitIndices incl. the T5 extension,\n"
               "              type 5 -> pT3_otHitIndices; type 8 owns no OT hits) before the K9\n"
               "              greedy walk, so a chain riding on a pixel-delivered track's hits\n"
               "              faces the same -F maxClaimedFrac test as chain-vs-chain.\n"
               "              2 = 1 + pixel owners also participate in the -W braid test.\n"
               "              Defined for -A 0 (attach off) only.\n"
               "  -G <0..6>   chain gate (hybrid mode, default 1). ANGLE-1 -G 6 = 3-CLASS gate\n"
               "              (fake/prompt-true/displaced-true softmax over 17 inputs = the 16\n"
               "              ChainFeatures + the chain dcaXY), kill-only, LEGACY ordering:\n"
               "              nLayers<=4 killed if mX < -M4; 5+ with dca < -X killed if\n"
               "              mP < -MP; 5+ with dca >= -X killed if mD < -MD; either 5+ kill\n"
               "              is vetoed when mX >= -MR. mP/mD/mX = prompt/displaced/max logit\n"
               "              minus fake logit. Exempt masks (-U4/-U5/-U6) as in -G 5.\n"
               "  -M4 -M5 -M6 -MD -M4D -MR  -G 6 margin thresholds (defaults -1e9 .. +1e9 =\n"
               "              no kill). -M4 IP T4-class (mX), -M5/-M6 IP 5+/6+ (mP), -MD\n"
               "              exempt 5+ (mD), -M4D exempt T4-class (mD), -MR mX OR-rescue.\n"
               "              -MP is an alias setting both -M5 and -M6.\n"
               "  -MRI <v>    (M14) IP-5+ mX OR-rescue threshold; the exempt-5+ branch keeps\n"
               "              -MR. Unset = follow -MR (bit-exact legacy).\n"
               "  -C25 <v>    (M15 angle C1) PER-CELL margin for (nNodes=2,nLayers=5) chains\n"
               "              only, additive to their branch rule, in BOTH dca branches:\n"
               "              killed iff mP < -C25 AND mD < -C25D (displaced head respected).\n"
               "  -C25D <v>   displaced-margin floor of that cell kill; unset = follow -C25\n"
               "              (then the rule is exactly mX < -C25). -C25 -1e9 = off.\n"
               "  -Q4 -Q5 <v> (M14) POST-CLAIM absolute mX floors on the IP-compatible\n"
               "              T4-class / 5+ branches (-G 6 only, default -1e9 = off). Applied\n"
               "              after K9 arbitration, so removed chains free NO hits and no\n"
               "              runner-up backfills -- the escape hatch from the M9 claim\n"
               "              conservation that makes pre-claim acceptance cuts self-defeating.\n"
               "  -G <0|1|2|3>  1 = K9 acceptance score per\n"
               "              chain is the chain-gate MLP LOGIT (ChainInference over the\n"
               "              ChainFeatures.h vector; thetaChain4/5/6 cut on that scale and\n"
               "              arbitration order uses it too); 0 = legacy K6 sum-logit score\n"
               "              (regression path); 2 = split: gate logit for nLayers <= 4 only\n"
               "              (thetaChain4 on the gate scale), legacy score for nLayers >= 5;\n"
               "              3 = DCA split (M9): gate logit for chains with transverse DCA\n"
               "              < dcaSplit (-X), legacy score for dcaXY >= dcaSplit (large-DCA\n"
               "              secondaries exempt); thresholds cut on the governing scale\n"
               "              (-X 0 == -G 0, -X 1e9 == -G 1 at equal thresholds);\n"
               "              4 = length+DCA split (M9 repair): T4-class always gate-scored\n"
               "              (as -G 2), 5+-layer chains DCA-split as in -G 3\n"
               "              (-X 0 == -G 2, -X 1e9 == -G 1 at equal thresholds);\n"
               "              5 = order-preserving DCA split: ordering stays LEGACY for all\n"
               "              chains (no cross-scale eviction); gate acts as a CUT only --\n"
               "              IP-compatible chains killed if gate logit < -T4/-T5/-T6,\n"
               "              exempt chains cut by -U4/-U5/-U6 (legacy scale)\n"
               "              (-G 5 -X 0 -U4 u == -G 0 -T4 u)\n"
               "  -X <cm>     dcaSplit (M9, default 0.5): IP-compatibility boundary for the\n"
               "              -G 3/-G 4 score split and the -A 3 evidence rule (transverse DCA\n"
               "              of the chain's full-fit circle to the origin, k8ChainDcaXY)\n"
               "  -Z <cm>     t4ExemptDcaMin (M9, -G 5 only, default 0 = no-op): displaced-\n"
               "              oriented exempt-T4 acceptance -- a T4-class chain rides the\n"
               "              exempt (legacy, -U4) branch only if dcaXY >= max(-X, -Z); T4s\n"
               "              with dca in [-X, -Z) are gate-cut like IP chains (-T4). The\n"
               "              LST-style displaced-only length-4 restriction (plan 10.5)\n"
               "  -V4/-V5/-V6 <theta>  a2 exempt-branch GATE-SCALE kill (-G 5 only,\n"
               "              defaults -1e9 = no-op): an exempt (dca >= -X) chain is ALSO\n"
               "              killed when its chain-gate logit is below this. Additive to\n"
               "              -U4/-U5/-U6 (legacy scale); ordering stays legacy for everyone\n"
               "              (the -G 5 order-preserving form). Targets the M9 residual-fake\n"
               "              localization: ~85%% of it lives in the exempt 5+ branch\n"
               "  -U4/-U5/-U6 <theta>  M9 exempt-branch thresholds (defaults 6/0/0): dca-exempt\n"
               "              chains in -G 3/-G 4 keep the LEGACY sum-logit score, where the\n"
               "              gate-scale -T thresholds are nearly no-ops; their per-length\n"
               "              acceptance cuts therefore come from this separate set (legacy\n"
               "              scale; U4=6 is the pre-gate r5-winner legacy T4 threshold).\n"
               "              -T4/-T5/-T6 keep governing the gate-scored chains.\n"
               "  -Y <pen>    demotion penalty for -A 3 (default 1e6 = reject outright):\n"
               "              subtracted from the acceptance score of IP-compatible 5+-layer\n"
               "              theta-passing chains whose K8 attach found no pair above\n"
               "              thetaAttach (pre-contention), BEFORE K9 ordering/thresholds\n"
               "  -A <0|1|2|3>  hybrid mode (default 0): 1 = K8 pixel attach v1 (REJECTED,\n"
               "              reference): attached chains bypass the partOfPT5 drop, become\n"
               "              type-7 TCs, own pT5/pLS rows suppressed. 2 = M7b subordinate\n"
               "              two-pass claim: pass 1 is exactly the -A 0 pipeline (no bypass);\n"
               "              attached chains pixdropped in pass 1 claim only against the\n"
               "              post-pass-1 MD map (never evicting pass-1 chains) and become\n"
               "              type-7 TCs; attached pass-1 chains upgrade in place to type 7;\n"
               "              seed-family suppression drops kept-baseline pT5/pT3/pLS rows\n"
               "              whose pLS shares >= 2 pixel hits with any attached pLS.\n"
               "              3 = M9 attach-as-EVIDENCE: K8 outcome consumed only as an\n"
               "              acceptance modifier -- IP-compatible (dcaXY < -X) theta-passing\n"
               "              5+-layer chains with no pair above thetaAttach are demoted by\n"
               "              -Y before K9; large-DCA chains exempt; no type upgrade, no\n"
               "              suppression, no hit-list change (TCs stay bare type 4/9)\n"
               "  -a <theta>  thetaAttach: attach-head logit threshold for K8 (default 0.0;\n"
               "              only used with -A 1/2)\n"
               "  -D <cm>     dcaMax (M7c, -A 2 only, default 1.0): IP-compatibility gate --\n"
               "              transverse DCA of the chain's full-fit circle to the origin must\n"
               "              be < dcaMax for the chain to be attach-ELIGIBLE (pass-2 candidacy\n"
               "              and in-place upgrade) and for K7-lite to consider dropping it\n"
               "  -K <0|1>    K7-lite kinematic dedup (M7c, -A 2 only, default 1): after attach\n"
               "              + suppression, drop bare (non-attached) chain TCs that are\n"
               "              IP-compatible (-D) and kinematically match a KEPT baseline type-7\n"
               "              row (dR < -R, pt ratio < 2)\n"
               "  -R <dR>     K7-lite dR window (M7c, default 0.03)\n"
               "  -S <dR>     suppression-guard dR window (M7c, -A 2 only, default 0.05): a\n"
               "              family-suppressed row is actually dropped only if\n"
               "              dR(row, attaching chain+pLS TC) < -S and pt ratio < 2; rows\n"
               "              failing the test survive\n"
               "  -TR <0|1>   ANGLE B2 TERMINAL TRIM (hybrid mode, default 0 = off, bit-exact\n"
               "              legacy). After K6 welding and BEFORE the chain gate / DCA split /\n"
               "              K9 claim, each chain with >= 3 member T3s tests dropping its\n"
               "              innermost and its outermost T3: the combined full-chain fit\n"
               "              chi2/hit (xy Kasa circle + rz line, the ChainFeatures 5+6\n"
               "              arithmetic) is recomputed over the remaining MD union and the end\n"
               "              with the larger improvement factor is dropped iff that factor\n"
               "              exceeds -TT and the remainder still has >= 2 T3s and >= -TL\n"
               "              distinct layers. Trimmed chains are rebuilt exactly as K6 would\n"
               "              emit them (node/edge/MD lists, nLayers, score = sum of remaining\n"
               "              weld-edge logits + -L * nLayers), so EVERYTHING downstream --\n"
               "              ChainFeatures, the 3-class gate, dcaXY, the exempt masks, K9 --\n"
               "              is recomputed on the trimmed object.\n"
               "  -TT <fac>   terminal-trim chi2 improvement factor (default 5.0)\n"
               "  -TL <n>     minimum distinct layers the trimmed remainder must keep\n"
               "              (default 5; measured -- see main.cc: at 4 the trim is net\n"
               "              NEGATIVE, at 5 it converts 110-155 fakes per true destroyed)\n"
               "  -TP <n>     terminal-trim passes (default 1); >1 allows a chain to lose one\n"
               "              terminal per pass\n"
               "  -A 4        M16 GENERAL pLS->OT ATTACH AS THE DELIVERY PATH (plan 11).\n"
               "              One target-agnostic attach over {5+-layer chains, bare T3s};\n"
               "              ONE contention rule (one pLS, one owner across target types);\n"
               "              attached chains are PIXEL-BACKED deliveries -- they pre-claim\n"
               "              their OT hits next to the surviving carried pixel rows and are\n"
               "              emitted unconditionally as type-7 TCs (pixel hits + OT hits,\n"
               "              pt = pLS ptIn), so they never walk the K9 greedy claim.\n"
               "              ORDER (the M16 mandate): attach decisions -> suppression set ->\n"
               "              pre-claim owner list -> K9 -> bare-T3 attach -> delivery.\n"
               "              Composes with -PU (which is what makes the order matter: a\n"
               "              carried row that attach replaced must NOT pre-claim).\n"
               "  -RT5 <0|1>  (-A 4) REPLACEMENT A/B, pT5 class: 1 = drop EVERY carried type-7\n"
               "              row; the attached chains are the whole pT5-class delivery. Also\n"
               "              disables the partOfPT5 half of the K9 pixel-consumed drop (the\n"
               "              rows it protects no longer exist). 0 = additive (only the\n"
               "              attached pLS's own carried rows are suppressed).\n"
               "  -RT3 <0|1>  (-A 4) REPLACEMENT A/B, pT3 class: 1 = drop EVERY carried type-5\n"
               "              row; (pLS, bare-T3) pairs deliver type-5 TCs instead (pixel hits\n"
               "              + the T3's 6 OT hits, pt = pLS ptIn). Also disables the\n"
               "              partOfPT3 half of the pixel-consumed drop.\n"
               "  -AT3 <v>    (-A 4) PER-CLASS attach margin for bare-T3 targets on the SAME\n"
               "              attach-logit scale as -a (default 6.0 = tight). The per-length\n"
               "              principle: a 3-layer target carries the least independent\n"
               "              evidence, so it must be the most certain pair to deliver.\n"
               "  -RPS <0|1>  (-A 4) bare-pLS contention: 1 = drop carried type-8 rows whose\n"
               "              pLS attached anywhere OR lost a contention above -a / -AT3\n"
               "              (i.e. had real OT evidence but is not the owner). The\n"
               "              contention rule replacing CrossCleanpLS / plsembdnn.\n"
               "  -RD <0|1>   (-A 4) SEED-FAMILY dedup of the attach owners (default 0 = off):\n"
               "              among attached pLS, one sharing >= 2 pixel hit rows with a\n"
               "              higher-logit attached pLS loses its attachment (its target\n"
               "              returns to the K9 walk and its carried rows survive). This is\n"
               "              plan 11's 'retain only the upstream pLS seed dup-clean', moved\n"
               "              inside the contention rule; sim-blind, mirroring production\n"
               "              pixelHitsOverlapAny. Without it a track with N duplicate pixel\n"
               "              seeds gets N attach deliveries.\n"
               "  -D4 <cm>    (-A 4) attach-eligibility dcaXY gate, default 1e9 = OFF.\n"
               "              Deliberately NOT the -A 2 default of 1.0: the M7c gate blocks\n"
               "              displaced chains from attaching, and the plan-11 mandate is that\n"
               "              the low-vxy displaced-with-pixel-seed population is upside to\n"
               "              CLAIM. Set it only to A/B the M7c dilution guard back on.\n"
               "  -TA <cm^2>  absolute full-chain combined chi2/hit floor for trim eligibility\n"
               "              (default 0 = off). The CONCENTRATING guard: -TT alone is a pure\n"
               "              volume knob (conversion purity flat at ~5.7%% for every -TT), so\n"
               "              it buys fake and pays track length at a fixed rate; -TA restricts\n"
               "              the trim to chains that genuinely mis-fit (purity 11.9%% at 3.0,\n"
               "              ~3x the conversions at equal trim volume)\n"
               " ---- M20 (T3ATTACH) CANDIDATE FINDING, all default to the frozen behaviour ----\n"
               "  -CF <0|1|2> (-A 4) candidate finding for the general attach:\n"
               "              0 = the frozen FULL ANALYTIC SCAN (default, bit-exact),\n"
               "              1 = the scalar BINNED PREFILTER (provable superset of 0; see\n"
               "                  PixelAttachCand.h for the proof and -CFA for the audit),\n"
               "              2 = an EXTERNAL candidate-pair list from -CFM (LST's pixel map\n"
               "                  used as a prefilter only, no map port).\n"
               "  -CFA <0|1>  run the analytic full scan ALONGSIDE the candidate finder and\n"
               "              count the analytic-accepted pairs the candidate set missed.\n"
               "              MUST report 0 for -CF 1. Expensive: audit runs only.\n"
               "  -CFB <mult> bin width as a multiple of the analytic window (default 1.0).\n"
               "              Smaller = tighter candidate volume, more cells.\n"
               "  -CFR <cm>   rt bin width (default 8.0). -CFP <rad> phi arc pad (default 0.02).\n"
               "  -CFC <0|1>  also route CHAIN targets through the candidate finder (default 0:\n"
               "              chain targets keep the full scan so the frozen pT5 line cannot\n"
               "              move while only the bare-T3 side is being developed).\n"
               "  -CFM <path> candidate-pair file for -CF 2 (text or binary; format documented\n"
               "              in PixelAttachCand.h). -CFW <0|1> additionally enforces the\n"
               "              analytic windows on map candidates (default 0: the map IS the\n"
               "              prefilter and the windows are features only).\n"
               " ---- M20 pT3-CLASS HIT-OVERLAP CONTENTION (the CrossCleanpT3 analogue) ----\n"
               "  -CC <0|1>   (-A 4) run a post-assembly hit-overlap contention over the\n"
               "              bare-T3 (type-5) deliveries. They are decided AFTER the K9 claim\n"
               "              and otherwise never compete for hits with anything, which is the\n"
               "              measured cause of the pT3-class duplicate explosion. Shared-hit\n"
               "              structure ONLY -- no dR/dEta/embedding proximity, by rule.\n"
               "  -CCG <0|1>  ownership-map unit: 1 (default) MD rows, 0 outer-tracker hits.\n"
               "  -CCN <n>    kill a delivery that finds >= n of its own units already\n"
               "              claimed. A delivery is 3 MDs / 6 OT hits, so at -CCG 1 the\n"
               "              steps are 1 (any shared MD), 2 (default: 2 of 3 == same\n"
               "              track), 3 (identical MD triple only).\n"
               "  -CCP <0|1>  1 (default) = everything already delivered (chain TCs and the\n"
               "              surviving carried pixel rows) pre-claims its units; 0 = the\n"
               "              deliveries contend only with each other.\n"
               "  -CCK <0|1|2> keep-best key: 0 attach logit (default), 1 pLS pt, 2 t3 row.\n"
               "  -CCR <0|1|2> on revoke: 1 (default) release plsOwned (MEASURED no-op at\n"
               "              -RPS 1), 0 keep the pLS retired, 2 also erase the seed's\n"
               "              bare-T3 evidence so its carried type-8 row survives.\n"
               "  -RDT <n>    stage-B PIXEL-side seed-family dedup: -1 (default) follow -RD,\n"
               "              0 off, 1 on. Splitting it from -RD is what lets OT-only be\n"
               "              measured against pixel-assisted.\n"
               "  -T3E <n>    bare-T3 stage B: -1 (default) follow -RT3, 0 force off, 1 force\n"
               "              on even with LST's pT3 rows carried (DIAGNOSTIC: double-counts).\n",
               prog);
}

double msBetween(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

// M7c kinematic tests (suppression guard / K7-lite): wrap a phi difference to (-pi, pi].
float wrapDPhi(float d) {
  constexpr float kPi = 3.14159265358979323846f;
  while (d > kPi)
    d -= 2.f * kPi;
  while (d < -kPi)
    d += 2.f * kPi;
  return d;
}

// Chain node-count histogram bins: 2, 3, 4, 5+ (K6 chains have >= 2 nodes by contract).
int nodeBin(int n) { return n >= 5 ? 3 : (n <= 2 ? 0 : n - 2); }

// Chain nLayers histogram bins (chains mode): <=4, 5, 6, 7, 8, 9+.
int layerBin9(int n) { return n <= 4 ? 0 : (n >= 9 ? 5 : n - 4); }

// Max-nLayers-per-sim bins (oracle table): <=4, 5, 6, 7, 8+.
int layerBin8(int n) { return n <= 4 ? 0 : (n >= 8 ? 4 : n - 4); }

// Oracle chain -> sim assignment: the sim (Labels simIdx space = FULL tracking-ntuple sim
// rows) present in the sim-sets of >= 2 member T3s, majority count; ties prefer accepted
// sims (row < nAccepted, i.e. kinematics known), then the smaller row. -1 if none reaches 2.
int chainSimIdx(const Chains& ch, int c, const T3SimSets& t3sims, int nAccepted) {
  std::unordered_map<int, int> cnt;
  for (int k = ch.offsets[c]; k < ch.offsets[c + 1]; ++k)
    for (int s : t3sims.sims[ch.items[k]])
      ++cnt[s];  // per-T3 sim sets are unique, so this counts member T3s per sim
  int best = -1, bestCnt = 0;
  bool bestAcc = false;
  for (const auto& kv : cnt) {
    if (kv.second < 2)
      continue;
    const bool acc = kv.first < nAccepted;
    bool better;
    if (kv.second != bestCnt)
      better = kv.second > bestCnt;
    else if (acc != bestAcc)
      better = acc;
    else
      better = best < 0 || kv.first < best;
    if (better) {
      best = kv.first;
      bestCnt = kv.second;
      bestAcc = acc;
    }
  }
  return best;
}

}  // namespace

int main(int argc, char** argv) {
  std::string lstPath, trkPath, outPath;
  std::string mode = "identity";
  std::string label = "PU200";
  long long maxEvents = -1;
  float thetaEdge = 0.0f;         // -e: K6 edge log-odds threshold (chains/hybrid modes)
  float lambdaLen = 0.0f;         // -L: K6 chain-length prior weight
  float thetaChain4 = 0.0f;       // -T4: K9 acceptance threshold, chain nLayers <= 4 (hybrid)
  float thetaChain5 = 0.0f;       // -T5: K9 acceptance threshold, chain nLayers == 5 (hybrid)
  float thetaChain6 = 0.0f;       // -T6: K9 acceptance threshold, chain nLayers >= 6 (hybrid)
  float thetaExempt4 = 6.0f;      // -U4: M9 exempt-branch (legacy-scale) threshold, nLayers <= 4
                                  // (default 6 = the pre-gate r5-winner legacy T4 threshold)
  float thetaExempt5 = 0.0f;      // -U5: M9 exempt-branch threshold, nLayers == 5
  float thetaExempt6 = 0.0f;      // -U6: M9 exempt-branch threshold, nLayers >= 6
  // a2 (fan-out angle 2): ADDITIONAL gate-scale kill for the EXEMPT branch in -G 5.
  // M9 localized ~85% of the residual fake in the exempt (dca >= -X) 5+ branch, which
  // -G 5 leaves entirely to the legacy -U thresholds -- the gate never touches it. With
  // a gate whose exempt-5+ discrimination is strong enough (a2: displaced AUC 0.836 vs
  // 0.818 for the 16-feature gate), a gate-scale kill there is exactly the
  // "fake-specific kill" the saturated K9 claim responds to. Ordering is UNCHANGED
  // (legacy for everyone, the mandatory -G 5 form); this is a pure additional cut.
  // Defaults -1e9 = no-op, so -G 5 stays bit-identical unless -V* is passed.
  float thetaVeto4 = -1e9f;       // -V4: exempt-branch GATE-scale kill, nLayers <= 4
  float thetaVeto5 = -1e9f;       // -V5: exempt-branch GATE-scale kill, nLayers == 5
  float thetaVeto6 = -1e9f;       // -V6: exempt-branch GATE-scale kill, nLayers >= 6
  float maxClaimedFrac = 0.3f;    // -F: K9 max already-claimed-MD fraction (hybrid mode)
  bool dropPixelConsumed = true;  // -P clears it: K9 pixel-consumed chain drop (hybrid mode)
  int chainGateMode = 1;          // -G: 0 = legacy K6 sum-logit score, 1 = gate logit for ALL
                                  // chains, 2 = split: gate logit for nLayers <= 4 only,
                                  // legacy score for nLayers >= 5, 3 = M9 DCA split: gate
                                  // logit iff chain dcaXY < dcaSplit, 4 = M9 length+DCA
                                  // split: gate for nLayers <= 4 always AND for 5+ iff
                                  // dcaXY < dcaSplit (hybrid mode)
  int attachMode = 0;             // -A: 1 = K8 pixel attach v1 (rejected, reference), 2 = M7b
                                  // subordinate two-pass claim + seed-family suppression,
                                  // 3 = M9 attach-as-evidence (acceptance demotion only)
                                  // (default 0 so the -A-less regression path is untouched)
  float thetaAttach = 0.0f;       // -a: attach-head logit threshold (hybrid -A 1)
  float dcaSplit = 0.5f;          // -X: M9 IP-compatibility boundary (-G 3 score split and
                                  // -A 3 evidence scope), cm
  float demotePenalty = 1e6f;     // -Y: M9 -A 3 failed-attach demotion (default = reject)
  float t4ExemptDcaMin = 0.0f;    // -Z: M9 -G 5 exempt-T4 dca floor (0 = no-op; T4 exempt
                                  // iff dca >= max(dcaSplit, t4ExemptDcaMin))
  // ANGLE-1 -G 6 (3-class gate) kill thresholds on the softmax-logit MARGIN scale
  // (mP = prompt - fake, mD = displaced - fake, mX = max(prompt,displaced) - fake).
  // Defaults -1e9 = "never kill" so -G 6 with no -M flags is the legacy -G 0 pipeline
  // plus the exempt masks.
  float m3Theta4 = -1e9f;   // -M4: IP T4-class (nLayers <= 4) kill iff mX < m3Theta4
  float m3Theta5 = -1e9f;   // -M5: IP nLayers == 5 kill iff mP < m3Theta5 (M12: the -G 6
                            // analogue of -G 5's -T5, which is per-length; a single 5+
                            // threshold cannot reproduce the c5 acceptance shape)
  float m3Theta6 = -1e9f;   // -M6: IP nLayers >= 6 kill iff mP < m3Theta6 (analogue of -T6)
  float m3ThetaP = -1e9f;   // -MP: convenience alias -- sets BOTH -M5 and -M6 (a1 compat)
  float m3ThetaD = -1e9f;   // -MD: exempt 5+ (dca >= dcaSplit) kill iff mD < m3ThetaD
  float m3Theta4D = -1e9f;  // -M4D: exempt T4-class (nLayers <= 4 AND dca >= max(-X,-Z))
                            // kill iff mD < m3Theta4D -- the LST-style DISPLACED-ORIENTED
                            // length-4 acceptance (plan 10.5 per-length care). Default
                            // -1e9 = no kill (the branch is then governed by -U4 alone,
                            // so -U4 1e9 reproduces the v1j closed-exempt-T4 shape).
  float m3ThetaR = 1e9f;    // -MR: OR-rescue on mX for BOTH 5+ branches -- a chain that
                            // would be killed survives if mX >= m3ThetaR (1e9 = disabled;
                            // set -MP/-MD to +1e9 and -MR to t for a pure-mX 5+ gate)
  // M14 lever 2 (branch-aware rescue): -MR is shared by both 5+ branches, but M13 measured
  // that the rescue owns 73.9% of the residual fake and that the two branches want very
  // different operating points (the IP branch is under-cut, the exempt branch is the
  // displaced home and must stay loose). -MRI overrides the rescue threshold for the
  // IP-compatible (dca < -X) 5+ branch only; the exempt branch keeps -MR. Sentinel 1e30 =
  // "not given" -> falls back to m3ThetaR, so every pre-M14 command line is bit-exact.
  constexpr float kMrUnset = 1e30f;
  float m3ThetaRI = kMrUnset;  // -MRI: IP-5+ OR-rescue on mX (default: follow -MR)
  // M14 lever 4 (post-claim mX floors). Every other -G 6 kill happens BEFORE the K9 claim,
  // so freed hits are re-claimed by the runners-up and the claim volume is conserved (the
  // M9 "K9 claim is SATURATED" finding: acceptance cuts backfill). A POST-claim floor
  // removes an accepted chain TC after arbitration is finished, with no backfill -- the
  // M13 shadow-recon lever (absolute mX floor, jet-blind, no proximity). -1e9 = no-op.
  float q3Floor5 = -1e9f;   // -Q5: post-claim mX floor, IP-compatible (dca < -X) 5+ chains
  float q3Floor4 = -1e9f;   // -Q4: post-claim mX floor, IP-compatible (dca < -X) T4 chains
  // ANGLE C1 (M15): PER-CELL acceptance margin for the (nNodes == 2, nLayers == 5) cell --
  // the classic single-shared-MD "T5-shaped" chain. M10 measured that cell alone carries
  // 59.4% of all fake chain TCs, M13 43% of the residual fakes at the w7/j1 anchor, and it
  // is also the |dLen| = 2 braid-duplicate driver. Every kill in the tree today is
  // per-BRANCH (IP / large-DCA) and per-LENGTH, never per-CELL, so the only way to cut this
  // cell harder is to tighten its whole branch (-MR / -MRI) -- which prices displaced
  // efficiency on chains that are NOT in the cell (M14: -MRI 2.5 saturates on fake while
  // vxy[1,5)/[5,10) fall .011/.030; -MR -0.2 costs 1 dxy[5,10) sim and 5 vxy[5,10) sims).
  // -C25 is an ADDITIONAL 3-class-margin kill applied to that cell ONLY, on the same margin
  // scale the branch kill uses, in BOTH dca branches, and it RESPECTS THE DISPLACED HEAD:
  // the chain dies only if BOTH margins fail (mP < -C25 AND mD < -C25D). With -C25D unset
  // (= -C25) that is exactly an mX floor for the cell; setting -C25D below -C25 spares any
  // cell chain the gate's displaced head still likes, however bad its prompt margin is.
  // -1e9 = off => bit-exact anchor.
  constexpr float kC25Unset = 1e30f;
  float c25Theta = -1e9f;        // -C25 : cell prompt-margin floor (mP)
  float c25ThetaD = kC25Unset;   // -C25D: cell displaced-margin floor (mD); unset = follow -C25
  // ============ FANOUT4 "transition" BAND-AWARE LEVERS (all default = bit-exact no-op) ==
  // Motivation: |eta| in [1.1, 1.7) is the worst band on BOTH axes at once (fake .0765 vs
  // LST .0459, eff .8603 vs .8826). Production LST already ships eta-binned WP tables, so
  // an eta-conditioned threshold is an accepted design pattern here. Every lever below is
  // an ADDITIVE delta applied to an EXISTING -G 6 / K9 threshold, and ONLY for chains whose
  // eta falls in the band. The chain's eta is the K10 TC eta by construction --
  // ev.t3_eta of the innermost member T3 (K9K10.cc: tc.eta = ev.t3_eta[t3Inner]) -- so the
  // band a chain is tightened in is exactly the band its TC is counted in.
  //   -ZE1 / -ZE2 : band edges (defaults 1.1 / 1.7). -ZE2 <= -ZE1 disables every lever.
  //   -ZRI  : added to -MRI, the IP-5+ OR-rescue mX floor  (the anchor's live IP-5+ cut,
  //           since -M5/-M6 are 1e9: an IP 5+ chain survives iff mX >= -MRI).
  //   -ZR   : added to -MR, the exempt-5+ OR-rescue mX floor (same reasoning, -MD = 1e9).
  //   -ZM4  : added to -M4,  the T4-class IP mX kill.
  //   -ZM4D : added to -M4D, the exempt T4-class mD kill.
  //   -ZCP / -ZCD : added to -C25 / -C25D, the (nNodes=2, nLayers=5) cell margins.
  //   -ZBA  : EXTRA -B order-key alpha in the band (ordering only; thresholds untouched,
  //           so this can never kill a chain -- it only makes fake-suspect band chains
  //           claim later, which frees their hits for real band chains).
  // POSITIVE values TIGHTEN (kill more); negative values LOOSEN (keep more).
  //   -ZR5 / -ZR6 : PER-LENGTH refinements of -ZR, added on top of it for exempt 5+ chains
  //           with nLayers == 5 / nLayers >= 6 respectively. The two lengths behave very
  //           differently in the band: at an mX floor of 0 the nL=5 exempt slice kills
  //           1532 fakes for 745 trues (2.06:1) while the nL>=6 slice kills 357 fakes for
  //           436 trues (0.82:1) -- the long exempt chains are where the genuinely
  //           displaced tracks live, so the tightening must not reach them.
  float zEta1 = 1.1f, zEta2 = 1.7f;
  float zdRI = 0.f, zdR = 0.f, zdM4 = 0.f, zdM4D = 0.f, zdCP = 0.f, zdCD = 0.f, zdAlpha = 0.f;
  //   -ZIL 1 : restrict EVERY band kill lever to chains whose INNERMOST chain MD sits in
  //           layer 1. Rationale (measured): a band chain that starts at layer >= 2 is
  //           6.0% of the band population but 21.9% of the band fakes AND is where the
  //           genuinely large-dxy tracks live -- a real dxy>5 track physically cannot
  //           leave a layer-1 MD. Tightening the layer-1 slice at an mX floor of 0.5
  //           puts 2 dxy>=5 sims at risk; the same floor without the restriction puts 21
  //           at risk, for a comparable fake gain. So the restriction buys the displaced
  //           floors back almost for free. 0 = no restriction (bit-exact).
  float zdR5 = 0.f, zdR6 = 0.f, zInLay1 = 0.f;
  constexpr float kGateKill = 1e9f;    // -G 5: score subtraction for IP chains failing the gate cut
  constexpr float kNoCutTheta = -1e5f; // -G 5: internal K9 base threshold (live chains always pass;
                                       // killed (-1e9) and -A 3-demoted (-1e6) chains always fail)
  float fakeOrderAlpha = 0.0f;    // -B: A8 fake-aware K9 ordering. Claim order key becomes
                                  // chains.score - alpha * max(0, -gateLogit): chains the
                                  // chain gate calls fake-suspect claim LATER, thresholds
                                  // untouched (no cross-scale inversion). 0 = legacy order.
  // OKR (order-key redesign). -B alone builds the K9 ordering penalty from the OLD
  // 2-class chain gate (chain_mlp_weights.h), which was trained on a ~6x smaller
  // claim-reaching population than the one authentic-replacement mode now feeds it.
  // -BK selects the penalty SOURCE, -BT its hinge point. Both leave every ACCEPTANCE
  // threshold on chains.score (the M9 cross-scale-inversion rule) and -BK 0 / -BT 0 is
  // the bit-exact legacy key.
  //   0 = 2-class gate logit (legacy) : pen = max(0, BT - gateLogit)
  //   1 = 3-class mX margin           : pen = max(0, BT - mX), mX = max(zP,zD) - zF
  //   2 = 3-class softmax fake prob   : pen = pFake in [0,1] (BT ignored)
  //   3 = 3-class mX, unhinged        : pen = BT - mX  (key = score + alpha*mX + const)
  //   4 = 3-class mX only             : key = mX; chains.score dropped from the ORDER
  //   5 = 3-class mX hinge / nLayers  : pen = max(0, BT - mX) / nLayers
  //   6 = nLayers-major               : rank-exact lexicographic (nLayers desc, then the
  //                                     -BK 0 key); alpha still shapes the inner key
  //   7 = matchFrac regressor         : pen = max(0, BT - predFrac) on the [0,1] scale
  //   8 = matchFrac only              : key = predFrac; chains.score dropped from the ORDER
  //   9 = matchFrac, logit scale      : pen = max(0, BT - predFracLogit)
  //  10 = SECONDARY 3-class head mX   : pen = max(0, BT - mXb); ranking network swapped,
  //                                     gate (and every kill threshold) left resident
  //  11 = BANDED composite            : coarse quality classes from the 3-class mX (band
  //                                     width BT), the LEGACY score ordering inside each
  //                                     band. Rank-exact. The point: mode 1 at a large
  //                                     hinge is quality-major and buys efficiency but
  //                                     costs duplicates, because it also discards the
  //                                     score's length preference -- which is exactly
  //                                     what keeps a short duplicate behind the long
  //                                     chain it shadows. Banding keeps both.
  // Modes 1..5 need -G 6 (the 3-class head runs only there); modes 7..9 need a trained
  // mf_mlp_weights.h. Without either, the key falls back to mode 0 with a warning.
  float orderKeySrc = 0.f;        // -BK
  float orderKeyHinge = 0.f;      // -BT
  int hitLevelClaim = 0;          // -H: A8 claim universe. 0 = MDs (legacy), 1 = HITS --
                                  // duplicate MD objects on the same hits make MD-disjoint
                                  // chains that are hit-identical; only the hit map sees them.
  float braidFrac = 0.0f;         // -W: A8 braid suppression. A candidate that covers >=
                                  // braidFrac of an ALREADY-ACCEPTED chain's MDs is killed
                                  // outright instead of passing the candidate-relative -F
                                  // test. 0 = off (bit-exact legacy).
  // ---- EX_DUPCC (M19 exploit wave): endcap chain-chain + carried-pT3 duplicate cells ----
  // (a) BAND-AWARE BRAID (-WE/-WZ/-WN). The flagship's -W 0.50 is provably inert (a
  //     candidate that passes -F 0.20 / -FC 1 can never cover half of an owner), so the
  //     owner-relative duplicate test is dead weight everywhere. RECON-0 measured that the
  //     surviving chain-chain overlaps are exactly 1 or 2 hit rows and that a 2-hit overlap
  //     is a REAL duplicate 44-57% of the time at |eta| 1.5-2.5 but only 8% in the barrel.
  //     -WE is therefore a SECOND braid fraction applied only to candidates in a geometry
  //     band (|eta| >= -WZ, nNodes <= -WN); the barrel keeps -W. Killing at claim time (not
  //     post-hoc like -DD) leaves the loser's hits FREE, so a longer runner-up can back-fill.
  float braidFracAlt = 0.f;       // -WE <frac>: braid fraction inside the band. 0 = off.
  float braidAltEta = 1e9f;       // -WZ <etaMin>: band lower |eta| edge. 1e9 = nobody.
  float braidAltMaxNodes = 1e9f;  // -WN <n>: band upper node count (short chains only).
  // (a2) PER-BAND CLAIM TOLERANCE (-FB/-FBC), same band. -FC 1 at -H 1 is a TWO-HIT
  //      budget, which is exactly the tolerance that lets every measured chain-chain
  //      duplicate through (RECON-0: overlaps are 1 or 2 hits, never more). -FBC 0 removes
  //      that budget inside the band; -FB tightens the fractional test there.
  float claimFracAlt = 0.f;       // -FB  <frac>: band maxClaimedFrac. <= 0 = use -F.
  float claimItemsAlt = -2.f;     // -FBC <n>: band claim budget in MD units. -2 = use -FC.
  // (b) CARRIED-pT3 CROSSCLEAN (-XP3/-XP3Z/-XP3L). The kept baseline type-5 rows are the
  //     only carried rows with outer-tracker hits under -RT5 1, so chain-vs-carried-pT3 is
  //     the whole cross-class half of the OT+OT duplicate cell. Structural, shared-object:
  //     a carried pT3 row is retired when an ACCEPTED chain covers >= -XP3 of its 6 OT hit
  //     rows. No dR / dphi / embedding proximity enters anywhere.
  float xp3Share = 0.f;           // -XP3 <n>: min shared OT hit rows. 0 = off.
  float xp3Eta = 0.f;             // -XP3Z <etaMin>: only retire rows with |tc_eta| >= this.
  float xp3MinLay = 0.f;          // -XP3L <n>: covering chain must have >= n layers.
  // (c) pT3 <-> CHAIN EXCHANGE (-XT3). 1 = drop the partOfPT3 half of the K9 pixel-consumed
  //     drop (the chains that reuse a pT3's T3 become candidates again); 2 = 1 + the carried
  //     type-5 rows stop PRE-CLAIMING, so those chains can actually win the claim. Meant to
  //     be paired with -XP3 6, which retires the pT3 row the winning chain replaced.
  float xt3Mode = 0.f;            // -XT3 <0|1|2>
  int preClaimMode = 0;           // -PU: B1 claim-universe unification. 0 = off (legacy: K9
                                  // arbitrates chains vs chains only), 1 = pre-claim the kept
                                  // baseline pixel TCs' OT hits so chains riding on a
                                  // pixel-delivered track's hits face the same -F test,
                                  // 2 = 1 + pixel owners also participate in the -W braid test.
  // B4 (M15 structural): length-normalized claim + de-lengthed ordering. Both default OFF
  // (bit-exact legacy). See Stages.h ArbitrationParams for the claim semantics.
  float claimCountMD = -1.f;      // -FC: absolute claim tolerance in MD units (<0 = off).
                                  // Converted to hit units (x2) when -H 1 is active.
  float claimCountExcl = 0.f;     // -FCX: 0 = OR with the -F fraction (loosen-only),
                                  // 1 = the count REPLACES the fraction.
  float claimStrictIpOnly = 0.f;  // -FCE: 0 = -FCX binds on every chain (legacy),
                                  // 1 = it binds on IP-compatible chains only; the
                                  // dca-exempt (displaced) ones keep the loosen-only OR.
  // M17 claimshare: POST-ARBITRATION STRUCTURAL DEDUP over the ACCEPTED chains. Loosening
  // -F recovers claim-starved sims but re-admits same-sim braid copies (extra TCs matching
  // a sim that is ALREADY matched -- exactly what the dup rate counts). This pass removes
  // those copies without un-matching anything: among accepted chains, best-first, a
  // candidate sharing >= dedupMinShared outer-tracker HITS with an already-kept chain is
  // dropped from the TC output. HIT-STRUCTURE ONLY (no dR / dEta / dPhi / embedding
  // proximity): two nearby but hit-disjoint tracks can never interact, which is the jet
  // safety requirement. All defaults OFF => bit-identical to the pre-M17 binary.
  float dedupMinShared = -1.f;    // -DD  <n>: min shared OT hits to call two accepted
                                  // chains copies (<= 0 = off).
  float dedupShareFrac = 0.f;     // -DDF <f>: additionally require the shared count to be
                                  // >= f * (hits of the SHORTER chain), so two long chains
                                  // crossing on a few stray hits are never merged.
                                  // 0 = off (count test alone).
  float dedupPix = 0.f;           // -DDP <0|1>: 1 = the pre-claim pixel owners (-PU) join
                                  // the dedup as un-killable kept entries, so a chain that
                                  // duplicates a CARRIED pixel TC is dropped too.
  float sharePassFrac = 0.f;      // -FS <frac>: M17 SUBORDINATE share pass. A second greedy
                                  // walk over the pass-1 rejects at this looser tolerance,
                                  // on the pass-1 owner map -- so the anchor accepted set is
                                  // preserved exactly and nothing is evicted. <= -F = off.
  float dedupKeyMode = 0.f;       // -DDK <0|1>: keep-best key. 0 = the K9 accepted order
                                  // (the arbitration order key); 1 = nLayers desc, K9 order
                                  // breaking ties (keep the LONGEST copy).
  float orderKeyMode = 0.f;       // -OK: K9 best-first ORDER key shape (thresholds never
                                  // see it; the M9 cross-scale-inversion rule).
                                  //   0 = legacy chains.score  (bit-exact)
                                  //   1 = de-lengthed: score - lambdaLen*nLayers
                                  //   2 = length-normalized: score / nLayers
                                  //   3 = mean edge logit
  // ANGLE B2 TERMINAL TRIM (Trim.h). Flag-gated, default OFF -> every pre-B2 command line
  // is bit-exact (the trim call is skipped entirely, chains stay K6's output object).
  float trimEnable = 0.f;   // -TR: 0 = off (default), 1 = on
  float trimFactor = 5.0f;  // -TT: chi2/hit improvement factor required to drop a terminal
  // -TL default 5, NOT 4: the offline trimdump ledger (30 evt, 40288 candidates, harness
  // matchFrac of every full/drop-inner/drop-outer variant) shows the guard axis is the
  // REMAINING LAYER COUNT, not -TT. At -TL 4 the trim's true-positive:false-positive ratio
  // is 0.7-1.3 (NET NEGATIVE below TT ~10). At -TL 5 the same test scores 110-155 F->T
  // per T->F at every TT in [1,30].
  float trimMinLay = 5.f;   // -TL: distinct layers the remainder must keep
  float trimPasses = 1.f;   // -TP: number of trim passes (one terminal per chain per pass)
  float trimAbsChi2 = 0.f;  // -TA: absolute full-chain chi2/hit floor for trim eligibility
                            // (the concentrating guard; rationale in Trim.h). 0 = off.

  // EXPLOIT: CHAIN EXTENSION AT ASSEMBLY (Extend.h). The inverse of the trim: after the
  // K9 claim, an accepted chain may RECLAIM a still-unclaimed MD on an adjacent layer
  // that lies on its own fitted trajectory. Flag-gated, default OFF -> every pre-EX
  // command line is bit-exact (extendChains is never called and the free-MD index is
  // never built).
  float extendMode = 0.f;     // -EX : 0 off, 1 outer end, 2 inner end, 3 both
  float extendWindow = 0.5f;  // -EXW: xy residual window (combined if -EXR <= 0), cm
  float extendRz = 0.f;       // -EXR: separate |rz| window, cm. 0 = combined test
  float extendChi2F = 2.0f;   // -EXF: refit chi2/hit <= f * max(chi2Full, window^2)
  float extendUniq = 0.f;     // -EXU: runner-up ambiguity margin, cm. 0 = off
  float extendMaxD = 60.f;    // -EXD: max 3D distance terminal MD -> candidate MD, cm
  float extendJump = 1.f;     // -EXJ: max md_layer index jump from the terminal layer
  float extendPerEnd = 1.f;   // -EXN: max MDs appended per chain end
  float extendMinLay = 4.f;   // -EXL: minimum chain nLayers eligible to extend
  float extendSeg = 0.f;      // -EXS: 1 = candidate must be LS-linked to the terminal MD
  float extendMaxChi2 = 0.f;  // -EXC: only extend chains whose own fit chi2/hit is below this
  float dcaAttachMax = 1.0f;      // -D: M7c IP-compatibility gate (attach eligibility + K7-lite)
  int k7Lite = 1;                 // -K: M7c K7-lite kinematic dedup on/off (-A 2 only)
  float k7DR = 0.03f;             // -R: K7-lite dR window
  float suppDR = 0.05f;           // -S: suppression-guard dR window
  constexpr float kKinPtRatioMax = 2.0f;  // shared pt-ratio window for -S guard and K7-lite

  // M16 GENERAL ATTACH DELIVERY (-A 4). All default to the legacy value, so -A 0..3 and
  // every pre-M16 command line are bit-exact.
  float replT5 = 0.f;              // -RT5: drop all carried type-7 rows (full pT5 replacement)
  float replT3 = 0.f;              // -RT3: drop all carried type-5 rows (full pT3 replacement)
  float replPls = 0.f;             // -RPS: drop contested carried type-8 rows
  float thetaAttachT3 = 6.0f;      // -AT3: per-class bare-T3 attach margin (tight by default)
  float dcaAttach4 = 1e9f;         // -D4: -A 4 attach-eligibility dca gate (default OFF)
  float seedDupClean = 0.f;        // -RD: seed-family dedup of the attach owners. Plan 11
                                   // keeps exactly ONE piece of the old crossclean stack --
                                   // "retain only the upstream pLS seed dup-clean" -- and
                                   // this is it, expressed inside the contention rule.
                                   // Sim-blind (>= 2 shared pixel hit rows, the production
                                   // pixelHitsOverlapAny criterion; pLS_isDuplicate is
                                   // TRUTH-derived in the ntuple and must never be read).

  // M16 pairdump scope / volume knobs (read ONLY by pairdump mode; inert everywhere else,
  // so no other mode's bit-exactness can be touched by them).
  //   -PDT 0 = chain targets only (the M7 dump), 1 = bare T3 targets only, 2 = BOTH
  //        (default: the general attach's full target universe).
  //   -PDC / -PDS = keep 1 in N FAKE pairs of the chain / bare-T3 target kind when
  //        WRITING (TRUE pairs are never downsampled; every reported statistic is
  //        computed on the FULL enumeration). The bare-T3 universe is ~35x the chain
  //        universe, so an undownsampled joint dump is tens of GB with no training value
  //        beyond the fake variety it already has; the kept fakes carry wgt = N.
  //   -PDN <first> = FIRST LST entry to process (default 0). The bare-T3 universe is
  //        ~200x the chain universe per event, so a full 300-event general dump runs
  //        ~16 min; -PDN lets it be produced in equal chunks (-PDN 0/100/200 with
  //        -n 100) whose "pairs" trees concatenate into one training set. Chunking
  //        cannot change any result: every event is processed independently.
  float pdTargetMode = 2.f;
  float pdFakeStrideChain = 1.f;
  float pdFakeStrideT3 = 1.f;
  float pdFirstEvt = 0.f;

  // M20 (T3ATTACH-BUILD) CANDIDATE FINDING. See PixelAttachCand.h. Every default here is
  // the FROZEN behaviour (full analytic scan, no audit, stage B tied to -RT3), so any
  // pre-M20 command line is bit-exact.
  float candMode = 0.f;      // -CF   0 = analytic full scan, 1 = binned prefilter, 2 = map
  float candAudit = 0.f;     // -CFA  1 = run the analytic scan alongside and count misses
  float candBinMult = 1.f;   // -CFB  bin width as a multiple of the analytic window
  float candRtBinW = 8.f;    // -CFR  rt bin width [cm]
  float candPhiPad = 0.02f;  // -CFP  extra pad on the inserted phi arc [rad]
  float candMapWin = 0.f;    // -CFW  mode 2: ALSO enforce the analytic windows
  float candChainToo = 0.f;  // -CFC  apply the candidate finder to CHAIN targets too
  float t3StageEnable = -1.f;  // -T3E bare-T3 stage B: -1 = follow -RT3 (frozen coupling),
                               //      0 = force off, 1 = force on even with pT3 carried
                               //      (a DIAGNOSTIC: carrying LST's pT3 rows AND
                               //      delivering ours double-counts the class)
  std::string candMapPath;   // -CFM  candidate-pair file for mode 2

  // M20 pT3-CLASS HIT-OVERLAP CONTENTION (the CrossCleanpT3 analogue). Bare-T3 deliveries
  // are decided post-claim and otherwise never compete for hits with anything; measured
  // consequence is 472 rows/evt vs LST's ~150 and a 5.5x duplicate rate. Default OFF so
  // the frozen line is bit-exact; this is the campaign's PRIMARY tuning axis.
  float ccMode = 0.f;      // -CC   1 = run the OT-side contention
  float ccGran = 1.f;      // -CCG  ownership-map granularity: 1 = MD rows (DEFAULT; see
                           //       the granularity note at the implementation site),
                           //       0 = outer-tracker HIT rows
  float ccMinShared = 2.f; // -CCN  kill when the number of SHARED (already-claimed) units
                           //       REACHES this. A delivery has exactly 3 MDs, so at
                           //       -CCG 1: 2 = "2 of 3 MDs shared == same track" (the
                           //       maintainer rule), 1 = any shared MD, 3 = identical
                           //       MD triple only. At -CCG 0 it counts OT hit rows (of 6).
  float ccPreclaim = 1.f;  // -CCP  1 = already-delivered TCs pre-claim into the map
  float ccOrder = 0.f;     // -CCK  keep-best key: 0 attach logit, 1 pLS pt, 2 t3 row
  float ccRelPls = 1.f;    // -CCR  on revoke: 1 (default) release the delivery's pLS back
                           //       to the carried universe, 0 keep it retired. Measures the
                           //       "does the revoked seed come back as a bare-pLS row"
                           //       fork. NOTE at -RPS 1 the -RPS predicate also fires on
                           //       plsBestT3Logit >= AT3, which is recorded for every
                           //       scored pair, so the release is expected to be a no-op
                           //       there; -CCR is what turns that reading into a number.
  float rdT3 = -1.f;       // -RDT  stage-B PIXEL-SIDE seed-family dedup: -1 follow -RD
                           //       (default), 0 off, 1 on. Splitting it from -RD is what
                           //       lets the campaign measure OT-only vs pixel-assisted.

  // Pre-scan for the multi-char flags -T4/-T5/-T6 and -U4/-U5/-U6 (getopt cannot
  // express them: "-T4" would parse as -T with value "4"); consume flag+value pairs
  // here and hand the compacted argv to getopt. "-T <v>" stays in getopt as
  // set-all-three shorthand.
  std::vector<char*> args;
  args.push_back(argv[0]);
  for (int a = 1; a < argc; ++a) {
    const std::string s = argv[a];
    // B1 -PU is an INT flag; handled before the float table so "-P" (a getopt no-arg
    // flag) can never swallow it.
    if (s == "-PU") {
      if (a + 1 >= argc) {
        std::fprintf(stderr, "Error: -PU requires a value.\n");
        usage(argv[0]);
        return 1;
      }
      preClaimMode = std::atoi(argv[++a]);
      if (preClaimMode < 0 || preClaimMode > 2) {
        std::fprintf(stderr, "Error: -PU expects 0, 1 or 2.\n");
        return 1;
      }
      continue;
    }
    float* dst = nullptr;
    // OKR -BK/-BT must be consumed HERE: getopt's "-B" would otherwise swallow "K"/"T".
    if (s == "-BK")
      dst = &orderKeySrc;
    else if (s == "-BT")
      dst = &orderKeyHinge;
    else if (s == "-T4")
      dst = &thetaChain4;
    else if (s == "-T5")
      dst = &thetaChain5;
    else if (s == "-T6")
      dst = &thetaChain6;
    else if (s == "-U4")
      dst = &thetaExempt4;
    else if (s == "-U5")
      dst = &thetaExempt5;
    else if (s == "-U6")
      dst = &thetaExempt6;
    else if (s == "-V4")
      dst = &thetaVeto4;
    else if (s == "-V5")
      dst = &thetaVeto5;
    else if (s == "-V6")
      dst = &thetaVeto6;
    else if (s == "-M4")
      dst = &m3Theta4;
    else if (s == "-M4D")
      dst = &m3Theta4D;
    else if (s == "-M5")
      dst = &m3Theta5;
    else if (s == "-M6")
      dst = &m3Theta6;
    else if (s == "-MP")
      dst = &m3ThetaP;
    else if (s == "-MD")
      dst = &m3ThetaD;
    else if (s == "-MRI")
      dst = &m3ThetaRI;
    else if (s == "-MR")
      dst = &m3ThetaR;
    else if (s == "-Q5")
      dst = &q3Floor5;
    else if (s == "-Q4")
      dst = &q3Floor4;
    else if (s == "-FCE")
      dst = &claimStrictIpOnly;
    else if (s == "-FCX")
      dst = &claimCountExcl;
    else if (s == "-FC")
      dst = &claimCountMD;
    else if (s == "-FS")
      dst = &sharePassFrac;
    // EX_DUPCC. Consumed here so getopt's "-W" / "-X" cannot swallow the suffix letters.
    else if (s == "-WE")
      dst = &braidFracAlt;
    else if (s == "-WZ")
      dst = &braidAltEta;
    else if (s == "-WN")
      dst = &braidAltMaxNodes;
    else if (s == "-FBC")
      dst = &claimItemsAlt;
    else if (s == "-FB")
      dst = &claimFracAlt;
    else if (s == "-XP3Z")
      dst = &xp3Eta;
    else if (s == "-XP3L")
      dst = &xp3MinLay;
    else if (s == "-XP3")
      dst = &xp3Share;
    else if (s == "-XT3")
      dst = &xt3Mode;
    // M17 claimshare post-arbitration dedup. Consumed here so getopt's "-D" cannot
    // swallow "-DD"/"-DDF"/"-DDP" as its value.
    else if (s == "-DDF")
      dst = &dedupShareFrac;
    else if (s == "-DDP")
      dst = &dedupPix;
    else if (s == "-DDK")
      dst = &dedupKeyMode;
    else if (s == "-DD")
      dst = &dedupMinShared;
    else if (s == "-OK")
      dst = &orderKeyMode;
    else if (s == "-TR")
      dst = &trimEnable;
    else if (s == "-TT")
      dst = &trimFactor;
    else if (s == "-TL")
      dst = &trimMinLay;
    else if (s == "-TP")
      dst = &trimPasses;
    else if (s == "-TA")
      dst = &trimAbsChi2;
    // EXPLOIT chain extension. Longest-first so no shorter name can shadow a longer one,
    // and consumed HERE so getopt's "-e" cannot swallow them.
    else if (s == "-EXW")
      dst = &extendWindow;
    else if (s == "-EXR")
      dst = &extendRz;
    else if (s == "-EXF")
      dst = &extendChi2F;
    else if (s == "-EXU")
      dst = &extendUniq;
    else if (s == "-EXD")
      dst = &extendMaxD;
    else if (s == "-EXJ")
      dst = &extendJump;
    else if (s == "-EXN")
      dst = &extendPerEnd;
    else if (s == "-EXL")
      dst = &extendMinLay;
    else if (s == "-EXS")
      dst = &extendSeg;
    else if (s == "-EXC")
      dst = &extendMaxChi2;
    else if (s == "-EX")
      dst = &extendMode;
    else if (s == "-C25D")
      dst = &c25ThetaD;
    else if (s == "-C25")
      dst = &c25Theta;
    // FANOUT4 transition band-aware levers (see the block next to kGateKill).
    else if (s == "-ZE1")
      dst = &zEta1;
    else if (s == "-ZE2")
      dst = &zEta2;
    else if (s == "-ZRI")
      dst = &zdRI;
    else if (s == "-ZIL")
      dst = &zInLay1;
    else if (s == "-ZR5")
      dst = &zdR5;
    else if (s == "-ZR6")
      dst = &zdR6;
    else if (s == "-ZR")
      dst = &zdR;
    else if (s == "-ZM4D")
      dst = &zdM4D;
    else if (s == "-ZM4")
      dst = &zdM4;
    else if (s == "-ZCP")
      dst = &zdCP;
    else if (s == "-ZCD")
      dst = &zdCD;
    else if (s == "-ZBA")
      dst = &zdAlpha;
    // M16 general-attach delivery flags. They MUST be consumed here: getopt's "-R"/"-A"
    // would otherwise swallow "-RT5"/"-AT3" as a value.
    else if (s == "-RT5")
      dst = &replT5;
    else if (s == "-RT3")
      dst = &replT3;
    else if (s == "-RPS")
      dst = &replPls;
    else if (s == "-AT3")
      dst = &thetaAttachT3;
    else if (s == "-D4")
      dst = &dcaAttach4;
    else if (s == "-RD")
      dst = &seedDupClean;
    else if (s == "-PDT")  // M16 pairdump: target-kind scope
      dst = &pdTargetMode;
    else if (s == "-PDC")  // M16 pairdump: fake downsample stride, chain targets
      dst = &pdFakeStrideChain;
    else if (s == "-PDS")  // M16 pairdump: fake downsample stride, bare-T3 targets
      dst = &pdFakeStrideT3;
    else if (s == "-PDN")  // M16 pairdump: first LST entry (chunked dumps)
      dst = &pdFirstEvt;
    // M20 candidate finding. -CFM is a PATH, so it is consumed here explicitly; the rest
    // are floats and join the table. All must be pre-scanned: getopt's "-C"/"-T" would
    // otherwise swallow them.
    else if (s == "-CFM") {
      if (a + 1 >= argc) {
        std::fprintf(stderr, "Error: -CFM requires a path.\n");
        usage(argv[0]);
        return 1;
      }
      candMapPath = argv[++a];
      continue;
    } else if (s == "-CFA")
      dst = &candAudit;
    else if (s == "-CFB")
      dst = &candBinMult;
    else if (s == "-CFR")
      dst = &candRtBinW;
    else if (s == "-CFP")
      dst = &candPhiPad;
    else if (s == "-CFW")
      dst = &candMapWin;
    else if (s == "-CFC")
      dst = &candChainToo;
    else if (s == "-CF")
      dst = &candMode;
    else if (s == "-T3E")
      dst = &t3StageEnable;
    // M20 pT3-class hit-overlap contention. -CCT/-CCP/-CCK MUST precede -CC in this
    // chain: the scan uses exact string equality, but keeping the longer names first
    // documents the intent and survives a future switch to prefix matching.
    else if (s == "-CCG")
      dst = &ccGran;
    else if (s == "-CCN")
      dst = &ccMinShared;
    else if (s == "-CCP")
      dst = &ccPreclaim;
    else if (s == "-CCK")
      dst = &ccOrder;
    else if (s == "-CCR")
      dst = &ccRelPls;
    else if (s == "-CC")
      dst = &ccMode;
    else if (s == "-RDT")
      dst = &rdT3;
    if (dst == nullptr) {
      args.push_back(argv[a]);
      continue;
    }
    if (a + 1 >= argc) {
      std::fprintf(stderr, "Error: %s requires a value.\n", s.c_str());
      usage(argv[0]);
      return 1;
    }
    *dst = static_cast<float>(std::atof(argv[++a]));
  }
  int nArgs = static_cast<int>(args.size());

  int opt;
  // -MP is a convenience alias for "-M5 v -M6 v" (a1's single IP-5+ threshold). An
  // explicit -M5/-M6 always wins over it.
  if (m3ThetaP > -1e9f) {
    if (m3Theta5 <= -1e9f)
      m3Theta5 = m3ThetaP;
    if (m3Theta6 <= -1e9f)
      m3Theta6 = m3ThetaP;
  }
  // M14: -MRI defaults to -MR (bit-exact fallback for every pre-M14 command line).
  if (m3ThetaRI >= kMrUnset)
    m3ThetaRI = m3ThetaR;
  // M15 angle C1: -C25D defaults to -C25 (then "both margins fail" == "mX < -C25").
  if (c25ThetaD >= kC25Unset)
    c25ThetaD = c25Theta;

  while ((opt = getopt(nArgs, args.data(), "i:t:o:n:m:l:e:L:T:F:G:A:a:B:W:H:D:K:R:S:X:Y:Z:Ph")) != -1) {
    switch (opt) {
      case 'i':
        lstPath = optarg;
        break;
      case 't':
        trkPath = optarg;
        break;
      case 'o':
        outPath = optarg;
        break;
      case 'n':
        maxEvents = std::atoll(optarg);
        break;
      case 'm':
        mode = optarg;
        break;
      case 'l':
        label = optarg;
        break;
      case 'e':
        thetaEdge = static_cast<float>(std::atof(optarg));
        break;
      case 'L':
        lambdaLen = static_cast<float>(std::atof(optarg));
        break;
      case 'T':
        thetaChain4 = thetaChain5 = thetaChain6 = static_cast<float>(std::atof(optarg));
        break;
      case 'F':
        maxClaimedFrac = static_cast<float>(std::atof(optarg));
        break;
      case 'P':
        dropPixelConsumed = false;
        break;
      case 'G':
        chainGateMode = std::atoi(optarg);
        if (chainGateMode < 0 || chainGateMode > 6) {
          std::fprintf(stderr, "Error: -G expects 0, 1, 2, 3, 4, 5, or 6.\n");
          return 1;
        }
        break;
      case 'A':
        attachMode = std::atoi(optarg);
        if (attachMode < 0 || attachMode > 4) {
          std::fprintf(stderr, "Error: -A expects 0, 1, 2, 3, or 4.\n");
          return 1;
        }
        break;
      case 'a':
        thetaAttach = static_cast<float>(std::atof(optarg));
        break;
      case 'B':
        fakeOrderAlpha = static_cast<float>(std::atof(optarg));
        break;
      case 'W':
        braidFrac = static_cast<float>(std::atof(optarg));
        break;
      case 'H':
        hitLevelClaim = std::atoi(optarg);
        if (hitLevelClaim < 0 || hitLevelClaim > 1) {
          std::fprintf(stderr, "Error: -H expects 0 or 1.\n");
          return 1;
        }
        break;
      case 'X':
        dcaSplit = static_cast<float>(std::atof(optarg));
        break;
      case 'Y':
        demotePenalty = static_cast<float>(std::atof(optarg));
        break;
      case 'Z':
        t4ExemptDcaMin = static_cast<float>(std::atof(optarg));
        break;
      case 'D':
        dcaAttachMax = static_cast<float>(std::atof(optarg));
        break;
      case 'K':
        k7Lite = std::atoi(optarg);
        if (k7Lite < 0 || k7Lite > 1) {
          std::fprintf(stderr, "Error: -K expects 0 or 1.\n");
          return 1;
        }
        break;
      case 'R':
        k7DR = static_cast<float>(std::atof(optarg));
        break;
      case 'S':
        suppDR = static_cast<float>(std::atof(optarg));
        break;
      case 'h':
        usage(argv[0]);
        return 0;
      default:
        usage(argv[0]);
        return 1;
    }
  }

  if (lstPath.empty() || trkPath.empty()) {
    std::fprintf(stderr, "Error: -i and -t are required.\n");
    usage(argv[0]);
    return 1;
  }
  if (mode != "identity" && mode != "graph" && mode != "dump" && mode != "chains" && mode != "oracle" &&
      mode != "hybrid" && mode != "chaindump" && mode != "pairdump" && mode != "trimdump") {
    std::fprintf(stderr,
                 "Error: unknown mode '%s' (expected identity, graph, dump, chains, oracle, hybrid, chaindump,"
                 " or pairdump).\n",
                 mode.c_str());
    return 1;
  }
  if ((mode == "identity" || mode == "dump" || mode == "hybrid" || mode == "chaindump" || mode == "pairdump" ||
       mode == "trimdump") &&
      outPath.empty()) {
    std::fprintf(stderr, "Error: -o is required in %s mode.\n", mode.c_str());
    return 1;
  }
  // M16: the replacement modes only mean anything inside the general attach (-A 4). Fail
  // loudly rather than silently ignore -- an A/B run with a mistyped -A is a wasted run.
  const bool wantRepl = replT5 >= 0.5f || replT3 >= 0.5f || replPls >= 0.5f;
  if (wantRepl && attachMode != 4) {
    std::fprintf(stderr, "Error: -RT5/-RT3/-RPS require -A 4 (M16 general attach delivery).\n");
    return 1;
  }
  if (attachMode == 4 && mode != "hybrid") {
    std::fprintf(stderr, "Error: -A 4 is a hybrid-mode delivery path.\n");
    return 1;
  }
  // M20: candidate-finder validation. Same discipline -- a mistyped -CF must not silently
  // fall back to the full scan and be read as a prefilter result.
  {
    const int cm = static_cast<int>(candMode + 0.5f);
    if (cm < 0 || cm > 2) {
      std::fprintf(stderr, "Error: -CF expects 0 (analytic), 1 (binned prefilter) or 2 (map candidates).\n");
      return 1;
    }
    if (cm == kCandMap && candMapPath.empty()) {
      std::fprintf(stderr, "Error: -CF 2 requires -CFM <candidate pair file>.\n");
      return 1;
    }
    if (cm != kCandMap && !candMapPath.empty())
      std::fprintf(stderr, "WARNING: -CFM given without -CF 2; the candidate file is IGNORED.\n");
    // Consumers: the -A 4 delivery path, and the pairdump (same enumeration, so the
    // prefilter is a pure speedup there). -CF 2 is delivery-only: a map candidate list is
    // a decision input, not a training-set definition.
    if (cm != kCandAnalytic && attachMode != 4 && mode != "pairdump") {
      std::fprintf(stderr, "Error: -CF %d requires -A 4 or -m pairdump.\n", cm);
      return 1;
    }
    if (cm == kCandMap && mode == "pairdump") {
      std::fprintf(stderr, "Error: -CF 2 (map candidates) is a delivery-path mode; pairdump enumerates the\n"
                           "       analytic universe by construction. Use -CF 0 or -CF 1 for dumps.\n");
      return 1;
    }
    if (ccMode >= 0.5f && attachMode != 4) {
      std::fprintf(stderr, "Error: -CC requires -A 4 (it cleans the pT3-class attach deliveries).\n");
      return 1;
    }
    const int ck = static_cast<int>(ccOrder + 0.5f);
    if (ccMode >= 0.5f && (ck < 0 || ck > 2)) {
      std::fprintf(stderr, "Error: -CCK expects 0 (attach logit), 1 (pLS pt) or 2 (t3 row).\n");
      return 1;
    }
  }

  NtupleReader reader(lstPath, trkPath);
  const long long nTotal = reader.nEntries();
  const long long nRun = (maxEvents < 0 || maxEvents > nTotal) ? nTotal : maxEvents;
  std::printf("Input: %s (%lld entries), tracking: %s, processing %lld event(s), mode=%s\n",
              lstPath.c_str(), nTotal, trkPath.c_str(), nRun, mode.c_str());

  LSTEventData ev;
  TrkEventData trk;

  if (mode == "identity") {
    OutputWriter writer(outPath, label);
    long long totalTCs = 0;
    for (long long i = 0; i < nRun; ++i) {
      if (!reader.loadEntry(i, ev, trk)) {
        std::fprintf(stderr, "Error: no aligned tracking event for LST entry %lld.\n", i);
        return 1;
      }
      writer.fillEventIdentity(ev);
      totalTCs += static_cast<long long>(ev.tc_pt.size());
    }
    writer.writeAndClose();
    std::printf("identity summary: %lld events, %lld TCs total, wrote %s\n", nRun, totalTCs, outPath.c_str());
    return 0;
  }

  if (mode == "dump") {
    DumpWriter dumpWriter(outPath);
    long long totEdges = 0, totTrue = 0;
    for (long long i = 0; i < nRun; ++i) {
      if (!reader.loadEntry(i, ev, trk)) {
        std::fprintf(stderr, "Error: no aligned tracking event for LST entry %lld.\n", i);
        return 1;
      }
      ChainGraph g;
      k1BuildIncidence(ev, g);
      k2BuildEdges(ev, g);
      NodeFeatures nf;
      computeNodeFeatures(ev, nf);
      EdgeFeatures ef;
      computeEdgeFeatures(ev, g, nf, ef);
      T3SimSets t3sims;
      buildT3SimSets(ev, t3sims);
      EdgeLabels labels;
      labelEdges(ev, g, t3sims, labels);
      dumpWriter.fillEvent(ev, g, nf, ef, labels);

      long long nTrue = 0;
      for (int8_t l : labels.label)
        if (l == 1)
          ++nTrue;
      totEdges += static_cast<long long>(g.edges.size());
      totTrue += nTrue;
      std::printf("evt %lld (run %u lumi %u event %llu): edges=%zu true=%lld\n", i, ev.run, ev.lumi, ev.evt,
                  g.edges.size(), nTrue);
    }
    dumpWriter.writeAndClose();
    std::printf("dump summary: %lld events, %lld edges dumped, true fraction=%.4f, wrote %s\n", nRun, totEdges,
                totEdges > 0 ? static_cast<double>(totTrue) / static_cast<double>(totEdges) : 0.0, outPath.c_str());
    return 0;
  }

  if (mode == "chaindump") {
    // M6: chain-gate training dump. r5-shape pipeline up to and including K6 (the -e/-L
    // knobs are the same ones hybrid uses; the winner shape is -e 0 -L 0.5), NO K9
    // arbitration: EVERY welded chain is dumped, so the gate classifier sees the full
    // pre-decision population it will be applied to (plan 5a: the hard decision trains
    // on maximum evidence, before any irreversible trimming).
    std::printf("chaindump mode: thetaEdge=%.3f lambdaLen=%.3f kWeldSweeps=%d\n", thetaEdge, lambdaLen, kWeldSweeps);
    ChainDumpWriter chainWriter(outPath);
    long long totChains = 0, totTrue = 0;
    constexpr int kMaxLayBin = 12;  // chains top out at 7 distinct layers (M3); headroom
    long long layAll[kMaxLayBin + 1] = {};
    long long layTrue[kMaxLayBin + 1] = {};
    // M12 flip matrix: [old label][new label] over all dumped chains.
    long long flip[2][2] = {{0, 0}, {0, 0}};
    double totInferMs = 0.0, totWeldMs = 0.0, totFeatMs = 0.0;
    long long totPixPT5 = 0, totPixPT3 = 0, totStagePass = 0;

    for (long long i = 0; i < nRun; ++i) {
      if (!reader.loadEntry(i, ev, trk)) {
        std::fprintf(stderr, "Error: no aligned tracking event for LST entry %lld.\n", i);
        return 1;
      }
      ChainGraph g;
      k1BuildIncidence(ev, g);
      k2BuildEdges(ev, g);
      NodeFeatures nf;
      computeNodeFeatures(ev, nf);
      EdgeFeatures ef;
      computeEdgeFeatures(ev, g, nf, ef);

      EdgeScores scores;
      const auto t0 = std::chrono::steady_clock::now();
      runEdgeInference(g, nf, ef, scores);
      const auto t1 = std::chrono::steady_clock::now();
      Chains chains;
      k6WeldChains(ev, g, scores, thetaEdge, lambdaLen, chains);
      const auto t2 = std::chrono::steady_clock::now();

      ChainFeatures cf;
      computeChainFeatures(ev, g, chains, scores, cf);
      T3SimSets t3sims;
      buildT3SimSets(ev, t3sims);
      ChainLabels labels;
      // M12 LABEL RETARGET: the dumped `label` is the HARNESS coverage rule (production
      // matcher over the chain's full hit list, best fraction strictly > 0.75); the old
      // >=2/3-MD-intersection label rides along as `label_old` for the flip matrix.
      // The chain's transverse DCA (k8ChainDcaXY -- the SAME function -G 3/4/5/6 use at
      // inference time) is computed inside the writer and dumped as `dcaXY`, so the
      // 3-class gate's dca input is train/serve identical by construction.
      labelChainsHarness(ev, trk, chains, t3sims, labels);
      const auto t3 = std::chrono::steady_clock::now();
      chainWriter.fillEvent(ev, chains, cf, labels);

      const long long nChains = chains.offsets.empty() ? 0 : static_cast<long long>(chains.offsets.size()) - 1;
      long long nTrue = 0;
      // M17: staging census (the two K9 pixel-consumed predicates). The dump carries them
      // per chain; these counters make the per-event funnel visible in the log too.
      {
        const int nT3d = static_cast<int>(ev.t3_lsIdx0.size());
        const bool havePixFlagsD = static_cast<int>(ev.t3_partOfPT5.size()) == nT3d &&
                                   static_cast<int>(ev.t3_partOfPT3.size()) == nT3d;
        long long nP5 = 0, nP3 = 0, nStage = 0;
        for (long long c = 0; c < nChains && havePixFlagsD; ++c) {
          bool p5 = false, p3 = false;
          for (int k = chains.offsets[c]; k < chains.offsets[c + 1]; ++k) {
            const int t3n = chains.items[k];
            p5 = p5 || ev.t3_partOfPT5[t3n];
            p3 = p3 || ev.t3_partOfPT3[t3n];
          }
          nP5 += p5 ? 1 : 0;
          nP3 += p3 ? 1 : 0;
          nStage += (!p3) ? 1 : 0;  // ctl_noatt staging: -RT5 1 (no PT5 drop) / -RT3 0 (PT3 drops)
        }
        totPixPT5 += nP5;
        totPixPT3 += nP3;
        totStagePass += nStage;
      }
      for (long long c = 0; c < nChains; ++c) {
        const int lb = std::min(chains.nLayers[c], kMaxLayBin);
        ++layAll[lb];
        if (labels.label[c] == 1) {
          ++nTrue;
          ++layTrue[lb];
        }
        const int lo = labels.labelOld.empty() ? 0 : (labels.labelOld[c] == 1 ? 1 : 0);
        ++flip[lo][labels.label[c] == 1 ? 1 : 0];
      }
      const double inferMs = msBetween(t0, t1);
      const double weldMs = msBetween(t1, t2);
      const double featMs = msBetween(t2, t3);
      std::printf("evt %lld (run %u lumi %u event %llu): chains=%lld true=%lld (%.3f)"
                  " | infer=%.3f weld=%.3f feat+label=%.3f ms\n",
                  i, ev.run, ev.lumi, ev.evt, nChains, nTrue,
                  nChains > 0 ? static_cast<double>(nTrue) / static_cast<double>(nChains) : 0.0, inferMs, weldMs,
                  featMs);
      totChains += nChains;
      totTrue += nTrue;
      totInferMs += inferMs;
      totWeldMs += weldMs;
      totFeatMs += featMs;
    }
    chainWriter.writeAndClose();

    const double nEvD = nRun > 0 ? static_cast<double>(nRun) : 1.0;
    std::printf("chaindump summary: %lld events (thetaEdge=%.3f lambdaLen=%.3f)\n", nRun, thetaEdge, lambdaLen);
    std::printf("  chains total=%lld mean=%.1f | label 1 total=%lld fraction=%.4f\n", totChains, totChains / nEvD,
                totTrue, totChains > 0 ? static_cast<double>(totTrue) / static_cast<double>(totChains) : 0.0);
    std::printf("  M17 staging census: partOfPT5=%lld (%.4f) partOfPT3=%lld (%.4f) | ctl_noatt stage-pass"
                " (-RT5 1 -RT3 0 => !partOfPT3) = %lld (%.4f)\n",
                totPixPT5, totChains > 0 ? static_cast<double>(totPixPT5) / static_cast<double>(totChains) : 0.0,
                totPixPT3, totChains > 0 ? static_cast<double>(totPixPT3) / static_cast<double>(totChains) : 0.0,
                totStagePass,
                totChains > 0 ? static_cast<double>(totStagePass) / static_cast<double>(totChains) : 0.0);
    std::printf("  per-nLayers label balance (all / true / trueFrac):\n");
    for (int b = 0; b <= kMaxLayBin; ++b) {
      if (layAll[b] == 0)
        continue;
      std::printf("    nLayers%s%2d : %10lld %10lld  %.4f\n", b == kMaxLayBin ? ">=" : " =", b, layAll[b], layTrue[b],
                  static_cast<double>(layTrue[b]) / static_cast<double>(layAll[b]));
    }
    // M12: old (>=2/3-MD intersection) vs new (harness coverage > 0.75) label flip matrix.
    const double flipTot = static_cast<double>(flip[0][0] + flip[0][1] + flip[1][0] + flip[1][1]);
    std::printf("  LABEL FLIP MATRIX (rows = old >=2/3-MD rule, cols = new harness > 0.75 rule):\n");
    std::printf("               new=0        new=1\n");
    for (int o = 0; o < 2; ++o)
      std::printf("    old=%d  %11lld  %11lld   (%.4f / %.4f of all)\n", o, flip[o][0], flip[o][1],
                  flipTot > 0 ? flip[o][0] / flipTot : 0.0, flipTot > 0 ? flip[o][1] / flipTot : 0.0);
    std::printf("    demoted (old 1 -> new 0) = %lld (%.4f of old-true); promoted (old 0 -> new 1) = %lld\n",
                flip[1][0], (flip[1][0] + flip[1][1]) > 0 ? static_cast<double>(flip[1][0]) /
                                                                static_cast<double>(flip[1][0] + flip[1][1])
                                                          : 0.0,
                flip[0][1]);
    std::printf("  time mean/evt infer=%.3f weld=%.3f feat+label=%.3f ms\n", totInferMs / nEvD, totWeldMs / nEvD,
                totFeatMs / nEvD);
    std::printf("  wrote %s\n", outPath.c_str());
    return 0;
  }

  if (mode == "trimdump") {
    // ANGLE B2 offline TT study. Pipeline through K6 EXACTLY as hybrid (-e/-L), then every
    // chain with >= 3 nodes is expanded into three variants (full / drop-innermost /
    // drop-outermost) and labelChainsHarness() is run over all of them at once, so the
    // production matcher's match fraction is measured for the SAME hit list the trimmed TC
    // would carry. One text line per candidate chain; the -TT working point is chosen
    // offline from the true-positive (fake -> true) vs false-positive (true -> fake) ledger.
    std::FILE* tf = std::fopen(outPath.c_str(), "w");
    if (tf == nullptr) {
      std::fprintf(stderr, "Error: cannot open %s for writing.\n", outPath.c_str());
      return 1;
    }
    std::fprintf(tf, "# evt chain nNodes nLayF nLayI nLayO xyF rzF xyI rzI xyO rzO mfF mfI mfO\n");
    long long totChains = 0, totCand = 0;
    for (long long i = 0; i < nRun; ++i) {
      if (!reader.loadEntry(i, ev, trk)) {
        std::fprintf(stderr, "Error: no aligned tracking event for LST entry %lld.\n", i);
        return 1;
      }
      ChainGraph g;
      k1BuildIncidence(ev, g);
      k2BuildEdges(ev, g);
      NodeFeatures nf;
      computeNodeFeatures(ev, nf);
      EdgeFeatures ef;
      computeEdgeFeatures(ev, g, nf, ef);
      EdgeScores scores;
      runEdgeInference(g, nf, ef, scores);
      Chains chains;
      k6WeldChains(ev, g, scores, thetaEdge, lambdaLen, chains);

      Chains study;
      std::vector<int> srcChain;
      std::vector<int8_t> variant;
      buildTrimStudyChains(ev, chains, scores, lambdaLen, study, srcChain, variant);
      T3SimSets t3sims;
      buildT3SimSets(ev, t3sims);
      ChainLabels sl;
      labelChainsHarness(ev, trk, study, t3sims, sl);

      const int nStudy = static_cast<int>(srcChain.size());
      for (int k = 0; k + 2 < nStudy; k += 3) {
        const int c = srcChain[k];
        double xy[3], rz[3];
        for (int v = 0; v < 3; ++v) {
          const int mb = study.mdOffsets[k + v], me = study.mdOffsets[k + v + 1];
          chainFitChi2Combined(ev, &study.mdItems[mb], me - mb, &xy[v], &rz[v]);
        }
        std::fprintf(tf, "%llu %d %d %d %d %d %.8g %.8g %.8g %.8g %.8g %.8g %.4f %.4f %.4f\n",
                     static_cast<unsigned long long>(ev.evt), c,
                     chains.offsets[c + 1] - chains.offsets[c], study.nLayers[k], study.nLayers[k + 1],
                     study.nLayers[k + 2], xy[0], rz[0], xy[1], rz[1], xy[2], rz[2], sl.matchFrac[k],
                     sl.matchFrac[k + 1], sl.matchFrac[k + 2]);
        ++totCand;
      }
      totChains += chains.offsets.empty() ? 0 : static_cast<long long>(chains.offsets.size()) - 1;
      if (i % 10 == 0)
        std::printf("trimdump evt %lld: chains=%lld candidates(>=3 nodes)=%lld\n", i, totChains, totCand);
    }
    std::fclose(tf);
    std::printf("trimdump summary: %lld events, %lld chains, %lld trim candidates -> %s\n", nRun, totChains, totCand,
                outPath.c_str());
    return 0;
  }

  if (mode == "pairdump") {
    // M7 / M16: K8 attach pair dump (mode contract in the header comment). The pipeline
    // through K9 mirrors hybrid mode EXACTLY (same knobs, same -G semantics);
    // ChainFeatures + gate logits are computed unconditionally because attach features
    // 7-11 need them (feature 11 is ALWAYS the gate logit, whatever -G used for K9).
    //
    // M16 GENERALIZATION: the dump now enumerates BOTH target kinds of the general
    // pLS->OT attach -- accepted chains (ttype 0) and BARE T3s (ttype 1, T3s in no
    // K9-accepted chain, mask computed AFTER arbitration) -- through ONE prefilter and
    // ONE feature builder. Everything is reported PER TARGET KIND, including the
    // displaced strata of the true-pair population, because the pT5-class and pT3-class
    // deliveries are judged separately (plan 11).
    AttachParams apre;  // default prefilter windows (PixelAttach.h: 0.6 / 0.4)
    // M20: pairdump-local candidate-finder state (the hybrid block owns its own).
    const int candModeI = static_cast<int>(candMode + 0.5f);
    PlsCandIndex pdCandIdx;
    CandStats pdCandStats;
    CandIndexParams pdCandIdxParams;
    pdCandIdxParams.rtBinW = candRtBinW;
    pdCandIdxParams.binMult = candBinMult;
    pdCandIdxParams.phiPad = candPhiPad;
    const int pdMode = static_cast<int>(pdTargetMode);
    const long long strideChain = std::max(1LL, static_cast<long long>(pdFakeStrideChain));
    const long long strideT3 = std::max(1LL, static_cast<long long>(pdFakeStrideT3));
    if (pdMode < 0 || pdMode > 2) {
      std::fprintf(stderr, "Error: -PDT expects 0 (chains only), 1 (bare T3 only) or 2 (both).\n");
      return 1;
    }
    const bool doChainTargets = (pdMode == 0 || pdMode == 2);
    const bool doT3Targets = (pdMode == 1 || pdMode == 2);
    if (q3Floor5 > -1e9f || q3Floor4 > -1e9f)
      std::fprintf(stderr,
                   "WARNING: pairdump does not apply the -Q4/-Q5 POST-claim mX floors;"
                   " the accepted set (and hence the bare-T3 universe) will differ from hybrid.\n");
    std::printf(
        "pairdump mode: thetaEdge=%.3f lambdaLen=%.3f thetaChain4/5/6=%.3f/%.3f/%.3f"
        " maxClaimedFrac=%.3f dropPixelConsumed=%s chainGateMode=%d kWeldSweeps=%d"
        " | prefDTanL=%.3f prefDPhi=%.3f attachHead=%s\n"
        "  M16 targets: chains=%s bareT3=%s (-PDT %d) | fake write stride chain=%lld T3=%lld"
        " | kAttachFeat=%d\n",
        thetaEdge, lambdaLen, thetaChain4, thetaChain5, thetaChain6, maxClaimedFrac,
        dropPixelConsumed ? "on" : "off", chainGateMode, kWeldSweeps, apre.prefDTanL, apre.prefDPhi,
        attachHeadAvailable() ? "trained" : "sentinel", doChainTargets ? "on" : "off",
        doT3Targets ? "on" : "off", pdMode, strideChain, strideT3, kAttachFeat);
    PairDumpWriter pairWriter(outPath);

    // Per-target-kind accounting (index 0 = chain targets, 1 = bare T3 targets).
    long long totTargets[2] = {0, 0};
    long long totPairsT[2] = {0, 0};
    long long totTrueT[2] = {0, 0};
    long long totWrittenT[2] = {0, 0};
    long long totProxyDenT[2] = {0, 0}, totProxyNumT[2] = {0, 0};
    long long totBindDTanLT[2] = {0, 0}, totBindDPhiT[2] = {0, 0}, totBindBothT[2] = {0, 0};
    // Displaced strata of the sim vertex radius, shared by the pair-level and the
    // target-level (proxy) tables. Band 5 = "pileup-only shared sim" (no accepted row =>
    // no kinematics); it is counted, never silently dropped.
    constexpr int kNVxy = 6;
    const float kVxyEdge[kNVxy] = {1.f, 5.f, 10.f, 30.f, 1e9f, -1.f};
    const char* const kVxyName[kNVxy] = {"[0,1)", "[1,5)", "[5,10)", "[10,30)", ">=30", "PU-only"};
    auto vxyBand = [&](float vxy) -> int {
      if (vxy < 0.f)
        return 5;  // -999 sentinel: shared sim is pileup-only
      for (int b = 0; b < 4; ++b)
        if (vxy < kVxyEdge[b])
          return b;
      return 4;
    };
    long long truePairVxy[2][kNVxy] = {{0}, {0}};
    long long proxyDenVxy[2][kNVxy] = {{0}, {0}};
    long long proxyNumVxy[2][kNVxy] = {{0}, {0}};
    double totEnumMs = 0.0, totLabelMs = 0.0;

    const long long pdFirst = static_cast<long long>(pdFirstEvt);
    if (pdFirst != 0)
      std::printf("  chunked dump: first LST entry = %lld (-PDN)\n", pdFirst);
    for (long long k = 0; k < nRun; ++k) {
      const long long i = pdFirst + k;
      if (!reader.loadEntry(i, ev, trk)) {
        std::fprintf(stderr, "Error: no aligned tracking event for LST entry %lld.\n", i);
        return 1;
      }
      ChainGraph g;
      k1BuildIncidence(ev, g);
      k2BuildEdges(ev, g);
      NodeFeatures nf;
      computeNodeFeatures(ev, nf);
      EdgeFeatures ef;
      computeEdgeFeatures(ev, g, nf, ef);
      EdgeScores scores;
      runEdgeInference(g, nf, ef, scores);
      Chains chains;
      k6WeldChains(ev, g, scores, thetaEdge, lambdaLen, chains);

      ChainFeatures cf;
      computeChainFeatures(ev, g, chains, scores, cf);
      std::vector<float> gateLogit;
      runChainInference(cf, gateLogit);
      // M16: the K9 configuration below MIRRORS hybrid mode through the M15 anchor
      // (-G 6 3-class gate + C1 cell kill, -OK/-B ordering, -W braid, -H hit-level
      // claim, -FC absolute claim budget, -PU claim-universe unification). The M7 dump
      // predates all of it and only knew -G 1..5 + -F, so the accepted set it produced
      // was NOT the anchor's -- and the accepted set is exactly what defines both target
      // universes here (chains directly, bare T3s by complement). ATTACH IS NEVER RUN in
      // this mode (-A is ignored): the dump is the pre-head training factory.
      std::vector<char> exemptMask;  // dca-exempt chains use the -U thresholds
      std::vector<float> dcaCache;
      auto chainDca = [&](int c) -> float {
        if (dcaCache.empty())
          dcaCache.assign(chains.score.size(), -1.f);
        if (dcaCache[c] < 0.f)
          dcaCache[c] = k8ChainDcaXY(ev, chains, c);
        return dcaCache[c];
      };
      if (chainGateMode >= 1) {
        if (chainGateMode == 6) {
          // ANGLE-1 -G 6 (the anchor's gate), verbatim from hybrid mode.
          const std::size_t nC = gateLogit.size();
          std::vector<float> dcaAll(nC);
          for (std::size_t c = 0; c < nC; ++c)
            dcaAll[c] = chainDca(static_cast<int>(c));
          std::vector<float> z3;
          runChainInference3(cf, dcaAll, z3);
          exemptMask.assign(nC, 0);
          for (std::size_t c = 0; c < nC; ++c) {
            const float* z = &z3[3 * c];
            const float mP = z[1] - z[0];
            const float mD = z[2] - z[0];
            const float mX = std::max(z[1], z[2]) - z[0];
            const int nL = chains.nLayers[c];
            if (nL <= 4) {
              if (dcaAll[c] >= std::max(dcaSplit, t4ExemptDcaMin)) {
                if (mD < m3Theta4D)
                  chains.score[c] -= kGateKill;
                exemptMask[c] = 1;
              } else if (mX < m3Theta4) {
                chains.score[c] -= kGateKill;
              }
            } else if (dcaAll[c] < dcaSplit) {
              const float thr = nL >= 6 ? m3Theta6 : m3Theta5;
              if (mP < thr && mX < m3ThetaRI)
                chains.score[c] -= kGateKill;
            } else {
              if (mD < m3ThetaD && mX < m3ThetaR)
                chains.score[c] -= kGateKill;
              exemptMask[c] = 1;
            }
            if (c25Theta > -1e9f && nL == 5 && (chains.offsets[c + 1] - chains.offsets[c]) == 2) {
              if (chains.score[c] > -0.5f * kGateKill && mP < c25Theta && mD < c25ThetaD)
                chains.score[c] -= kGateKill;
            }
          }
        } else {
          if (chainGateMode >= 3)
            exemptMask.assign(gateLogit.size(), 0);
          for (std::size_t c = 0; c < gateLogit.size(); ++c) {
            if (chainGateMode == 5) {
              // Same -G 5 semantics as hybrid: legacy ordering kept, gate as a kill,
              // -Z exempt-T4 dca floor.
              const int nL = chains.nLayers[c];
              const float exemptFloor = nL <= 4 ? std::max(dcaSplit, t4ExemptDcaMin) : dcaSplit;
              if (chainDca(static_cast<int>(c)) < exemptFloor) {
                const float thr = nL >= 6 ? thetaChain6 : (nL == 5 ? thetaChain5 : thetaChain4);
                if (gateLogit[c] < thr)
                  chains.score[c] -= kGateKill;
              } else {
                exemptMask[c] = 1;
              }
              continue;
            }
            const bool useGate = chainGateMode == 1 || (chainGateMode == 2 && chains.nLayers[c] <= 4) ||
                                 (chainGateMode == 3 && chainDca(static_cast<int>(c)) < dcaSplit) ||
                                 (chainGateMode == 4 &&
                                  (chains.nLayers[c] <= 4 || chainDca(static_cast<int>(c)) < dcaSplit));
            if (useGate)
              chains.score[c] = gateLogit[c];
            else if (chainGateMode >= 3)
              exemptMask[c] = 1;
          }
        }
      }

      ArbitrationParams ap;
      ap.thetaChain4 = chainGateMode >= 5 ? kNoCutTheta : thetaChain4;
      ap.thetaChain5 = chainGateMode >= 5 ? kNoCutTheta : thetaChain5;
      ap.thetaChain6 = chainGateMode >= 5 ? kNoCutTheta : thetaChain6;
      ap.thetaAlt4 = thetaExempt4;
      ap.thetaAlt5 = thetaExempt5;
      ap.thetaAlt6 = thetaExempt6;
      if (!exemptMask.empty())
        ap.altThreshold = &exemptMask;
      ap.maxClaimedFrac = maxClaimedFrac;
      if (claimCountMD >= 0.f) {  // B4 -FC/-FCX (MD units; -H 1 doubles the budget)
        const int nMDbudget = static_cast<int>(std::floor(claimCountMD));
        ap.maxClaimedItems = hitLevelClaim ? 2 * nMDbudget : nMDbudget;
        ap.claimCountExclusive = claimCountExcl >= 0.5f;
        // DUPCUT -FCE: exempt (dcaXY >= dcaSplit) chains keep the loosen-only OR;
        // at -FCE 2 they are additionally exempt from the -W braid kill.
        if (claimStrictIpOnly >= 0.5f && !exemptMask.empty()) {
          ap.strictExemptMask = &exemptMask;
          ap.strictExemptBraid = claimStrictIpOnly >= 1.5f;
        }
      }
      ap.sharePassFrac = sharePassFrac;
      ap.dropPixelConsumed = dropPixelConsumed;
      // B4 -OK / A8 -B: ordering key reshape (thresholds stay on chains.score).
      const int okMode = static_cast<int>(std::lround(orderKeyMode));
      std::vector<float> orderKeyVec;
      const bool haveGateKey = gateLogit.size() == chains.score.size();
      const bool wantPenalty = fakeOrderAlpha > 0.f && haveGateKey;
      if (wantPenalty || okMode != 0) {
        orderKeyVec.resize(chains.score.size());
        for (std::size_t c = 0; c < orderKeyVec.size(); ++c) {
          float base = chains.score[c];
          const float nL = static_cast<float>(std::max(1, chains.nLayers[c]));
          if (okMode == 1) {
            base -= lambdaLen * nL;
          } else if (okMode == 2) {
            base /= nL;
          } else if (okMode == 3) {
            const int nEdges = std::max(1, chains.offsets[c + 1] - chains.offsets[c] - 1);
            base = (base - lambdaLen * nL) / static_cast<float>(nEdges);
          }
          orderKeyVec[c] = base - (wantPenalty ? fakeOrderAlpha * std::max(0.f, -gateLogit[c]) : 0.f);
        }
        ap.orderKey = &orderKeyVec;
      }
      ap.braidFrac = braidFrac;                 // A8 -W
      ap.hitLevelClaim = hitLevelClaim != 0;    // A8 -H
      // B1 -PU: pre-claim the kept baseline pixel TCs' outer-tracker hits.
      std::vector<std::vector<int>> pixOwnerHits;
      if (preClaimMode > 0) {
        const std::size_t nTCb = ev.tc_type.size();
        pixOwnerHits.reserve(nTCb);
        for (std::size_t it = 0; it < nTCb; ++it) {
          const int ty = ev.tc_type[it];
          if (ty == 7) {
            const int p5 = (it < ev.tc_pt5Idx.size()) ? ev.tc_pt5Idx[it] : -999;
            if (p5 < 0 || p5 >= static_cast<int>(ev.pT5_t5Idx.size()))
              continue;
            const int t5 = ev.pT5_t5Idx[p5];
            if (t5 < 0 || t5 >= static_cast<int>(ev.t5_hitIndices.size()))
              continue;
            pixOwnerHits.push_back(ev.t5_hitIndices[t5]);
          } else if (ty == 5) {
            const int p3 = (it < ev.tc_pt3Idx.size()) ? ev.tc_pt3Idx[it] : -999;
            if (p3 < 0 || p3 >= static_cast<int>(ev.pT3_otHitIndices.size()))
              continue;
            pixOwnerHits.push_back(ev.pT3_otHitIndices[p3]);
          }
        }
        ap.preClaimOwners = &pixOwnerHits;
        ap.preClaimMode = preClaimMode;
      }
      std::vector<int> accepted;
      k9Arbitrate(ev, chains, ap, accepted);

      // --- M16 target universe (b): BARE T3s = T3s in no K9-ACCEPTED chain. The mask is
      // built AFTER arbitration, so a T3 welded into a REJECTED chain is still bare and
      // still attachable -- exactly the pT3-class population.
      std::vector<char> bareMask;
      k8BuildBareT3Mask(ev, chains, accepted, bareMask);
      long long evBareT3 = 0;
      for (char m : bareMask)
        evBareT3 += (m != 0);
      if (!doT3Targets)
        bareMask.assign(bareMask.size(), 0);

      // --- truth: target-side sim sets and pLS-side sim sets (pLS_simIdxAll = FULL
      // tracking-ntuple sim rows, the EventData.h M7 rule -- SAME space as T3SimSets,
      // direct intersection).
      //   chain target -> intersection of ALL member T3 sim sets (M7 rule, unchanged)
      //   bare T3      -> that T3's own >=2/3-MD sim set (T3SimSets entry): the degenerate
      //                   case of the same rule for a one-node target, so the two kinds
      //                   are labelled by ONE definition.
      T3SimSets t3sims;
      buildT3SimSets(ev, t3sims);
      const int nAcc = static_cast<int>(accepted.size());
      const int nAccSim = static_cast<int>(ev.sim_pt.size());
      const int nT3 = static_cast<int>(ev.t3_lsIdx0.size());
      // Unified target list, in the EXACT order k8EnumeratePrefilteredPairsGeneral emits:
      // accepted chains with nLayers >= 5 (acceptedChains order), then bare T3s ascending.
      std::vector<int8_t> tgtType;
      std::vector<int> tgtRow;      // chainPos for ttype 0, t3 row for ttype 1
      std::vector<int> tgtNLayers;  // 3 for every bare T3
      std::vector<const std::vector<int>*> tgtSims;
      std::vector<std::vector<int>> chainSimsStore;
      chainSimsStore.reserve(nAcc);
      {
        std::vector<int> tmp;
        for (int pos = 0; pos < nAcc; ++pos) {
          const int c = accepted[pos];
          if (chains.nLayers[c] < 5)
            continue;
          if (!doChainTargets)
            continue;
          const int ib = chains.offsets[c], ie = chains.offsets[c + 1];
          std::vector<int> common = t3sims.sims[chains.items[ib]];
          for (int k = ib + 1; k < ie && !common.empty(); ++k) {
            const auto& s = t3sims.sims[chains.items[k]];
            tmp.clear();
            std::set_intersection(common.begin(), common.end(), s.begin(), s.end(), std::back_inserter(tmp));
            common.swap(tmp);
          }
          chainSimsStore.push_back(std::move(common));
          tgtType.push_back(0);
          tgtRow.push_back(pos);
          tgtNLayers.push_back(chains.nLayers[c]);
          tgtSims.push_back(nullptr);  // patched below (chainSimsStore may reallocate)
        }
        for (std::size_t i = 0, k = 0; i < tgtType.size(); ++i)
          tgtSims[i] = &chainSimsStore[k++];
        for (int t = 0; t < nT3; ++t) {
          if (!bareMask[t])
            continue;
          tgtType.push_back(1);
          tgtRow.push_back(t);
          tgtNLayers.push_back(3);
          tgtSims.push_back(&t3sims.sims[t]);
        }
      }
      const int nTgt = static_cast<int>(tgtType.size());
      long long evTargets[2] = {0, 0};
      for (int i = 0; i < nTgt; ++i)
        ++evTargets[tgtType[i]];

      const int nPls = static_cast<int>(ev.pLS_pt.size());
      std::vector<std::vector<int>> plsSims(nPls);
      std::unordered_map<int, std::vector<int>> simToPls;
      for (int p = 0; p < nPls; ++p) {
        std::vector<int> v = ev.pLS_simIdxAll[p];
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
        for (int s : v)
          simToPls[s].push_back(p);
        plsSims[p] = std::move(v);
      }

      // --- prefilter-only pair enumeration over BOTH target kinds (shared K8 path) -----
      const auto t0 = std::chrono::steady_clock::now();
      std::vector<AttachPair> pairs;
      // M20: the pairdump runs the SAME enumeration as the delivery path, so the binned
      // prefilter applies verbatim -- and it is proven to emit the identical pair list, so
      // -CF 1 here is a pure speedup for the campaign's training-data generation. -CFA
      // audits it exactly as in hybrid mode.
      if (candModeI == kCandBinned) {
        apre.candMode = candModeI;
        apre.candAudit = (candAudit >= 0.5f);
        apre.candChainToo = (candChainToo >= 0.5f);
        apre.candStats = &pdCandStats;
        k8BuildPlsCandIndex(ev, apre, pdCandIdxParams, pdCandIdx);
        apre.cand = &pdCandIdx;
      }
      // BUGFIX (T3DEDUP-1): the enumerator assigns targetOrd over ITS OWN target list --
      // every accepted chain with nLayers >= 5 first, then the mask's bare T3s -- while
      // the target list built above honours -PDT. At -PDT 1 the chain targets are absent
      // from the local list but were still enumerated, so targetOrd ran past nTgt and
      // `++pairBegin[pr.targetOrd + 1]` wrote off the end of the heap block (observed as
      // "malloc(): invalid size (unsorted)" inside the first TTree::Fill). -PDT 0 was
      // never affected because it suppresses its unwanted kind through bareMask, which
      // the enumerator DOES see. Do the same on the chain side: hand the enumerator an
      // empty accepted list so both lists agree. `accepted` itself is untouched -- the
      // bare-T3 mask above is built from the REAL accepted set, as it must be.
      static const std::vector<int> kNoChainTargets;
      k8EnumeratePrefilteredPairsGeneral(
          ev, chains, doChainTargets ? accepted : kNoChainTargets, cf, gateLogit, bareMask, apre, pairs);
      const auto t1 = std::chrono::steady_clock::now();
      const double enumMs = msBetween(t0, t1);

      // Per-target spans (the enumeration emits targetOrd-ascending, plsRow-ascending
      // within a target -- the same CSR trick as M7, now over the unified target list).
      std::vector<int> pairBegin(nTgt + 1, 0);
      for (const AttachPair& pr : pairs) {
        // Guard the invariant the bugfix above restores, instead of trusting it: an
        // ordinal past the local target list is a silent heap overwrite otherwise.
        if (pr.targetOrd < 0 || pr.targetOrd >= nTgt) {
          std::fprintf(stderr,
                       "Error: pairdump target ordinal %d outside the local target list"
                       " (nTgt=%d). Enumerator and target list disagree.\n",
                       pr.targetOrd, nTgt);
          return 1;
        }
        ++pairBegin[pr.targetOrd + 1];
      }
      for (int i = 0; i < nTgt; ++i)
        pairBegin[i + 1] += pairBegin[i];

      // --- label + write every prefiltered pair ---------------------------------------
      // Statistics below are over the FULL enumeration; the write is downsampled on FAKE
      // pairs only (per target kind), with wgt = stride recorded on the survivors.
      long long evPairs[2] = {0, 0}, evTrue[2] = {0, 0}, evWritten[2] = {0, 0};
      long long fakeSeen[2] = {0, 0};
      const auto t2 = std::chrono::steady_clock::now();
      {
        std::vector<int> shared;
        for (const AttachPair& pr : pairs) {
          const int tt = pr.ttype;
          const auto& cs = *tgtSims[pr.targetOrd];
          const auto& ps = plsSims[pr.plsRow];
          shared.clear();
          std::set_intersection(cs.begin(), cs.end(), ps.begin(), ps.end(), std::back_inserter(shared));
          const int label = shared.empty() ? 0 : 1;
          ++evPairs[tt];
          float simVxy = -999.f, simPt = -999.f;
          if (label == 1) {
            ++evTrue[tt];
            int best = -1;
            float bestPt = -1.f;
            for (int s : shared) {
              if (s < nAccSim && ev.sim_pt[s] > bestPt) {
                best = s;
                bestPt = ev.sim_pt[s];
              }
            }
            if (best >= 0) {  // pileup-only shared sim keeps label 1 with -999 kinematics
              simPt = ev.sim_pt[best];
              simVxy = std::sqrt(ev.sim_vx[best] * ev.sim_vx[best] + ev.sim_vy[best] * ev.sim_vy[best]);
            }
            ++truePairVxy[tt][vxyBand(simVxy)];
          }
          // Deterministic fake downsampling: every stride-th fake of this kind, in
          // enumeration order (no RNG -> the dump is reproducible from the flags alone).
          float wgt = 1.f;
          if (label == 0) {
            const long long stride = (tt == 0) ? strideChain : strideT3;
            const long long seen = fakeSeen[tt]++;
            if (stride > 1) {
              if (seen % stride != 0)
                continue;
              wgt = static_cast<float>(stride);
            }
          }
          ++evWritten[tt];
          pairWriter.fillPair(ev.evt, pr.f, label, tgtNLayers[pr.targetOrd], simVxy, simPt, tt, wgt);
        }
      }
      const double labelMs = msBetween(t2, std::chrono::steady_clock::now());

      // --- prefilter true-pair efficiency proxy, PER TARGET KIND ----------------------
      // Denominator: targets with a non-empty sim set whose sim(s) have a matching pLS
      // ANYWHERE in the event. Numerator: one of those pLS survived the prefilter FOR
      // THIS TARGET. Failures are window-attributed per candidate pLS. Both are also
      // stratified by the target's sim vertex radius -- the displaced-strata judging the
      // maintainer requires: a candidate finder that loses displaced true pairs would
      // cap the pT3-class displaced upside before any head ever sees them.
      long long evDen[2] = {0, 0}, evNum[2] = {0, 0};
      std::vector<int> cand;
      for (int ord = 0; ord < nTgt; ++ord) {
        const int tt = tgtType[ord];
        const auto& cs = *tgtSims[ord];
        if (cs.empty())
          continue;  // fake target: no truth to measure
        cand.clear();
        for (int s : cs) {
          auto it = simToPls.find(s);
          if (it != simToPls.end())
            cand.insert(cand.end(), it->second.begin(), it->second.end());
        }
        if (cand.empty())
          continue;  // the target's sim has no matched pLS anywhere
        std::sort(cand.begin(), cand.end());
        cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
        // Stratum from the target's own best ACCEPTED sim (highest pt), same convention
        // as the pair labels.
        float tVxy = -999.f;
        {
          int best = -1;
          float bestPt = -1.f;
          for (int s : cs)
            if (s < nAccSim && ev.sim_pt[s] > bestPt) {
              best = s;
              bestPt = ev.sim_pt[s];
            }
          if (best >= 0)
            tVxy = std::sqrt(ev.sim_vx[best] * ev.sim_vx[best] + ev.sim_vy[best] * ev.sim_vy[best]);
        }
        const int band = vxyBand(tVxy);
        ++evDen[tt];
        ++proxyDenVxy[tt][band];
        bool survived = false;
        const auto spanB = pairs.begin() + pairBegin[ord];
        const auto spanE = pairs.begin() + pairBegin[ord + 1];
        for (int p : cand) {
          const auto it =
              std::lower_bound(spanB, spanE, p, [](const AttachPair& a, int v) { return a.plsRow < v; });
          if (it != spanE && it->plsRow == p) {
            survived = true;
            break;
          }
        }
        if (survived) {
          ++evNum[tt];
          ++proxyNumVxy[tt][band];
          continue;
        }
        for (int p : cand) {
          float aDT = 0.f, aDP = 0.f;
          if (tt == 0)
            k8ProbePairWindows(ev, chains, accepted[tgtRow[ord]], apre, p, aDT, aDP);
          else
            k8ProbePairWindowsT3(ev, tgtRow[ord], apre, p, aDT, aDP);
          const bool fDT = aDT >= apre.prefDTanL;
          const bool fDP = aDP >= apre.prefDPhi;
          if (fDT && fDP)
            ++totBindBothT[tt];
          else if (fDT)
            ++totBindDTanLT[tt];
          else if (fDP)
            ++totBindDPhiT[tt];
        }
      }

      std::printf(
          "evt %lld (run %u lumi %u event %llu): accepted=%d nPls=%d nT3=%d bareT3=%lld"
          " | CHAIN tgt=%lld pairs=%lld true=%lld (%.4f) proxy=%lld/%lld (%.3f)"
          " | T3 tgt=%lld pairs=%lld true=%lld (%.4f) proxy=%lld/%lld (%.3f)"
          " | enum=%.1f ms label=%.1f ms\n",
          i, ev.run, ev.lumi, ev.evt, nAcc, nPls, nT3, evBareT3, evTargets[0], evPairs[0], evTrue[0],
          evPairs[0] > 0 ? static_cast<double>(evTrue[0]) / static_cast<double>(evPairs[0]) : 0.0, evNum[0],
          evDen[0], evDen[0] > 0 ? static_cast<double>(evNum[0]) / static_cast<double>(evDen[0]) : 0.0,
          evTargets[1], evPairs[1], evTrue[1],
          evPairs[1] > 0 ? static_cast<double>(evTrue[1]) / static_cast<double>(evPairs[1]) : 0.0, evNum[1],
          evDen[1], evDen[1] > 0 ? static_cast<double>(evNum[1]) / static_cast<double>(evDen[1]) : 0.0, enumMs,
          labelMs);

      for (int tt = 0; tt < 2; ++tt) {
        totTargets[tt] += evTargets[tt];
        totPairsT[tt] += evPairs[tt];
        totTrueT[tt] += evTrue[tt];
        totWrittenT[tt] += evWritten[tt];
        totProxyDenT[tt] += evDen[tt];
        totProxyNumT[tt] += evNum[tt];
      }
      totEnumMs += enumMs;
      totLabelMs += labelMs;
    }
    pairWriter.writeAndClose();

    const double nEvD = nRun > 0 ? static_cast<double>(nRun) : 1.0;
    const char* const kKindName[2] = {"CHAIN (ttype 0, pT5-class)", "BARE T3 (ttype 1, pT3-class)"};
    if (candModeI == kCandBinned)
      std::printf("pairdump candfind: -CF 1 binned prefilter | targets=%lld examined=%lld (%.4gx of"
                  " full scan %lld) emitted=%lld | -CFA audit: analytic=%lld MISSING=%lld %s\n",
                  pdCandStats.nTargets, pdCandStats.nExamined,
                  pdCandStats.nFullScan > 0
                      ? static_cast<double>(pdCandStats.nExamined) / static_cast<double>(pdCandStats.nFullScan)
                      : 0.0,
                  pdCandStats.nFullScan, pdCandStats.nEmitted, pdCandStats.nAnalytic, pdCandStats.nMissing,
                  (candAudit >= 0.5f) ? (pdCandStats.nMissing == 0 ? "PASS" : "*** FAIL ***") : "(not run)");
    std::printf("pairdump summary: %lld events (prefDTanL=%.3f prefDPhi=%.3f, kAttachFeat=%d, -PDT %d)\n", nRun,
                apre.prefDTanL, apre.prefDPhi, kAttachFeat, pdMode);
    for (int tt = 0; tt < 2; ++tt) {
      if (totTargets[tt] == 0 && totPairsT[tt] == 0)
        continue;
      const double proxyEff =
          totProxyDenT[tt] > 0 ? static_cast<double>(totProxyNumT[tt]) / static_cast<double>(totProxyDenT[tt]) : 0.0;
      std::printf("  --- %s ---\n", kKindName[tt]);
      std::printf("    targets total=%lld mean=%.1f | pairs total=%lld mean=%.1f | true=%lld fraction=%.6f\n",
                  totTargets[tt], totTargets[tt] / nEvD, totPairsT[tt], totPairsT[tt] / nEvD, totTrueT[tt],
                  totPairsT[tt] > 0 ? static_cast<double>(totTrueT[tt]) / static_cast<double>(totPairsT[tt]) : 0.0);
      std::printf("    pairs/target=%.1f | written=%lld (fake stride %lld)\n",
                  totTargets[tt] > 0 ? static_cast<double>(totPairsT[tt]) / static_cast<double>(totTargets[tt]) : 0.0,
                  totWrittenT[tt], (tt == 0) ? strideChain : strideT3);
      std::printf("    PREFILTER TRUE-PAIR EFFICIENCY (targets w/ matched pLS anywhere): %lld / %lld = %.4f\n",
                  totProxyNumT[tt], totProxyDenT[tt], proxyEff);
      std::printf("    failed true-pair window attribution (per candidate pLS): dTanL-only=%lld dPhi-only=%lld"
                  " both=%lld\n",
                  totBindDTanLT[tt], totBindDPhiT[tt], totBindBothT[tt]);
      if (proxyEff < 0.97 && totProxyDenT[tt] > 0) {
        const char* binding =
            (totBindDTanLT[tt] >= totBindDPhiT[tt] && totBindDTanLT[tt] >= totBindBothT[tt]) ? "prefDTanL"
            : (totBindDPhiT[tt] >= totBindBothT[tt])                                         ? "prefDPhi"
                                                                                             : "both windows";
        std::printf("    NOTE: efficiency < 0.97 -- binding window by failure count: %s\n", binding);
      }
      // Displaced strata, the plan-11 judging requirement: the true-pair POPULATION the
      // head will train on, and the candidate-finder efficiency, both by sim vxy.
      std::printf("    displaced strata (sim vxy, cm):\n");
      std::printf("      %-9s %12s %8s | %10s %10s %8s\n", "band", "truePairs", "frac", "proxyNum", "proxyDen", "eff");
      for (int b = 0; b < kNVxy; ++b) {
        const double frac =
            totTrueT[tt] > 0 ? static_cast<double>(truePairVxy[tt][b]) / static_cast<double>(totTrueT[tt]) : 0.0;
        const double eff = proxyDenVxy[tt][b] > 0
                               ? static_cast<double>(proxyNumVxy[tt][b]) / static_cast<double>(proxyDenVxy[tt][b])
                               : 0.0;
        std::printf("      %-9s %12lld %8.4f | %10lld %10lld %8.4f\n", kVxyName[b], truePairVxy[tt][b], frac,
                    proxyNumVxy[tt][b], proxyDenVxy[tt][b], eff);
      }
    }
    std::printf("  enum time total=%.1f ms mean=%.1f ms | label+write mean=%.1f ms\n", totEnumMs, totEnumMs / nEvD,
                totLabelMs / nEvD);
    std::printf("  wrote %s\n", outPath.c_str());
    return 0;
  }

  if (mode == "chains") {
    std::printf("chains mode: thetaEdge=%.3f lambdaLen=%.3f kWeldSweeps=%d\n", thetaEdge, lambdaLen, kWeldSweeps);
    long long totT3 = 0, totEdges = 0, totPass = 0, totChains = 0;
    long long nodeHistTot[4] = {0, 0, 0, 0};
    long long layHistTot[6] = {0, 0, 0, 0, 0, 0};
    double totInferMs = 0.0, totWeldMs = 0.0;

    for (long long i = 0; i < nRun; ++i) {
      if (!reader.loadEntry(i, ev, trk)) {
        std::fprintf(stderr, "Error: no aligned tracking event for LST entry %lld.\n", i);
        return 1;
      }
      ChainGraph g;
      k1BuildIncidence(ev, g);
      k2BuildEdges(ev, g);
      NodeFeatures nf;
      computeNodeFeatures(ev, nf);
      EdgeFeatures ef;
      computeEdgeFeatures(ev, g, nf, ef);

      EdgeScores scores;
      const auto t0 = std::chrono::steady_clock::now();
      runEdgeInference(g, nf, ef, scores);
      const auto t1 = std::chrono::steady_clock::now();
      Chains chains;
      k6WeldChains(ev, g, scores, thetaEdge, lambdaLen, chains);
      const auto t2 = std::chrono::steady_clock::now();
      const double inferMs = msBetween(t0, t1);
      const double weldMs = msBetween(t1, t2);

      long long nPass = 0;
      for (float lo : scores.logOdds)
        if (lo >= thetaEdge)
          ++nPass;
      const long long nT3 = static_cast<long long>(ev.t3_lsIdx0.size());
      const long long nEdges = static_cast<long long>(g.edges.size());
      const long long nChains = chains.offsets.empty() ? 0 : static_cast<long long>(chains.offsets.size()) - 1;

      long long nodeHist[4] = {0, 0, 0, 0};
      long long layHist[6] = {0, 0, 0, 0, 0, 0};
      for (long long c = 0; c < nChains; ++c) {
        ++nodeHist[nodeBin(chains.offsets[c + 1] - chains.offsets[c])];
        ++layHist[layerBin9(chains.nLayers[c])];
      }

      std::printf(
          "evt %lld (run %u lumi %u event %llu): nT3=%lld edges=%lld pass=%lld chains=%lld"
          " nodes[2/3/4/5+]=%lld/%lld/%lld/%lld"
          " nLayers[<=4/5/6/7/8/9+]=%lld/%lld/%lld/%lld/%lld/%lld"
          " | infer=%.3f ms weld=%.3f ms\n",
          i, ev.run, ev.lumi, ev.evt, nT3, nEdges, nPass, nChains, nodeHist[0], nodeHist[1], nodeHist[2], nodeHist[3],
          layHist[0], layHist[1], layHist[2], layHist[3], layHist[4], layHist[5], inferMs, weldMs);

      totT3 += nT3;
      totEdges += nEdges;
      totPass += nPass;
      totChains += nChains;
      for (int b = 0; b < 4; ++b)
        nodeHistTot[b] += nodeHist[b];
      for (int b = 0; b < 6; ++b)
        layHistTot[b] += layHist[b];
      totInferMs += inferMs;
      totWeldMs += weldMs;
    }

    const double nEvD = nRun > 0 ? static_cast<double>(nRun) : 1.0;
    std::printf("chains summary: %lld events (thetaEdge=%.3f lambdaLen=%.3f)\n", nRun, thetaEdge, lambdaLen);
    std::printf("  nT3    total=%lld mean=%.1f\n", totT3, totT3 / nEvD);
    std::printf("  edges  total=%lld mean=%.1f | passing thetaEdge total=%lld mean=%.1f (%.2f%%)\n", totEdges,
                totEdges / nEvD, totPass, totPass / nEvD,
                totEdges > 0 ? 100.0 * static_cast<double>(totPass) / static_cast<double>(totEdges) : 0.0);
    std::printf("  chains total=%lld mean=%.1f\n", totChains, totChains / nEvD);
    std::printf("  chain nodes   [2/3/4/5+]        = %lld/%lld/%lld/%lld\n", nodeHistTot[0], nodeHistTot[1],
                nodeHistTot[2], nodeHistTot[3]);
    std::printf("  chain nLayers [<=4/5/6/7/8/9+]  = %lld/%lld/%lld/%lld/%lld/%lld\n", layHistTot[0], layHistTot[1],
                layHistTot[2], layHistTot[3], layHistTot[4], layHistTot[5]);
    std::printf("  time infer total=%.3f ms mean=%.3f ms | weld total=%.3f ms mean=%.3f ms\n", totInferMs,
                totInferMs / nEvD, totWeldMs, totWeldMs / nEvD);
    return 0;
  }

  if (mode == "hybrid") {
    // M4 A/B: kept baseline pixel TCs + prototype chain TCs through the impersonating
    // writer (OutputWriter::fillEventHybrid), so createPerfNumDenHists / compare_ab.py
    // judge the swap against base300_hists.root with production-identical definitions.
    std::printf(
        "hybrid mode: thetaEdge=%.3f lambdaLen=%.3f thetaChain4/5/6=%.3f/%.3f/%.3f"
        " maxClaimedFrac=%.3f dropPixelConsumed=%s chainGateMode=%d kWeldSweeps=%d\n",
        thetaEdge, lambdaLen, thetaChain4, thetaChain5, thetaChain6, maxClaimedFrac,
        dropPixelConsumed ? "on" : "off", chainGateMode, kWeldSweeps);
    if (attachMode == 1) {
      AttachParams apDefaults;
      std::printf(
          "attach (-A 1): thetaAttach=%.3f attachHead=%s prefDTanL=%.3f prefDPhi=%.3f"
          " (K8 runs after the theta gate and BEFORE the pixel-consumed drop; attached"
          " chains bypass partOfPT5, still respect partOfPT3, become type-7 TCs with"
          " pt = pLS ptIn + the pLS pixel hits prepended, and suppress their pLS's"
          " baseline pT5/pLS rows; pT3 rows untouched)\n",
          thetaAttach, attachHeadAvailable() ? "trained" : "sentinel", apDefaults.prefDTanL, apDefaults.prefDPhi);
    } else if (attachMode == 2) {
      AttachParams apDefaults;
      std::printf(
          "attach (-A 2, M7b): thetaAttach=%.3f attachHead=%s prefDTanL=%.3f prefDPhi=%.3f"
          " (subordinate two-pass claim: pass 1 = exact -A 0 pipeline, no bypass;"
          " pass 2 = attached chains pixdropped by partOfPT5 claim against the"
          " post-pass-1 MD map only, becoming type-7 TCs; attached pass-1 chains"
          " upgrade in place to type 7; seed-family suppression drops kept-baseline"
          " pT5/pT3/pLS rows sharing >= 2 pixel hits with any attached pLS)\n",
          thetaAttach, attachHeadAvailable() ? "trained" : "sentinel", apDefaults.prefDTanL, apDefaults.prefDPhi);
      std::printf(
          "attach M7c: dcaMax=%.3f cm (IP gate on attach eligibility + K7-lite),"
          " suppression guard dR<%.3f ptRatio<%.1f, K7-lite=%s (dR<%.3f ptRatio<%.1f,"
          " bare IP-compatible chains vs kept type-7 rows)\n",
          dcaAttachMax, suppDR, kKinPtRatioMax, k7Lite ? "on" : "off", k7DR, kKinPtRatioMax);
    } else if (attachMode == 3) {
      AttachParams apDefaults;
      std::printf(
          "attach (-A 3, M9 evidence): thetaAttach=%.3f attachHead=%s prefDTanL=%.3f"
          " prefDPhi=%.3f dcaSplit=%.3f cm demotePenalty=%g"
          " (K8 outcome consumed ONLY as an acceptance modifier: IP-compatible"
          " (dca < dcaSplit) theta-passing 5+-layer chains with no scored pair above"
          " thetaAttach (pre-contention) are demoted by demotePenalty before K9;"
          " large-DCA chains exempt; no type upgrade, no suppression, no hit-list"
          " change -- TCs stay bare type 4/9)\n",
          thetaAttach, attachHeadAvailable() ? "trained" : "sentinel", apDefaults.prefDTanL, apDefaults.prefDPhi,
          dcaSplit, static_cast<double>(demotePenalty));
    } else if (attachMode == 4) {
      AttachParams apDefaults;
      std::printf(
          "attach (-A 4, M16 GENERAL DELIVERY): thetaAttach=%.3f (chain class) thetaAttachT3=%.3f"
          " (bare-T3 class) attachHead=%s prefDTanL=%.3f prefDPhi=%.3f dcaEligibility=%.4g cm\n"
          "  ORDER (M16b): wholesale suppression set -> pre-claim owner list -> K9 over ALL"
          " chains (no exclusions) -> stage-A chain attach OVER THE ACCEPTED SET ->"
          " stage-B bare-T3 attach (vs the same ACCEPTED set) -> delivery\n"
          "  replacement: -RT5=%d (drop ALL carried type-7 rows) -RT3=%d (drop ALL carried"
          " type-5 rows) -RPS=%d (drop contested carried type-8 rows)\n"
          "  attached chains are K9 CLAIM WINNERS upgraded IN PLACE to type-7 TCs (pixel hits"
          " + OT hits, pt = pLS ptIn): mutual hit contention is inherited from the claim, so"
          " a chain that lost the claim can never attach and never delivers\n",
          thetaAttach, thetaAttachT3, attachHeadAvailable() ? "trained" : "sentinel", apDefaults.prefDTanL,
          apDefaults.prefDPhi, dcaAttach4, replT5 >= 0.5f ? 1 : 0, replT3 >= 0.5f ? 1 : 0, replPls >= 0.5f ? 1 : 0);
      if (preClaimMode == 0)
        std::printf("  NOTE: -PU 0 -- the pre-claim is OFF, so the suppression/owner-list ordering"
                    " has no effect on K9 (the accepted set, and hence the attach universe,"
                    " is unchanged by the suppression).\n");
    }
    if (chainGateMode == 6)
      std::printf("gate (-G 6, 3-class fake/prompt/displaced, head=%s): dcaSplit=%.3f cm;"
                  " ordering legacy for ALL (kill-only); IP T4-class killed if mX < %.4g;"
                  " exempt T4-class killed if mD < %.4g;"
                  " IP nL=5 killed if mP < %.4g; IP nL>=6 killed if mP < %.4g;"
                  " exempt 5+ killed if mD < %.4g;"
                  " OR-rescue mX >= %.4g (IP-5+) / %.4g (exempt-5+); exempt-T4 dca floor Z=%.3f cm;"
                  " exempt-branch legacy thresholds U4/5/6=%.3f/%.3f/%.3f;"
                  " post-claim mX floors Q4=%.4g Q5=%.4g;"
                  " C1 cell (nNodes=2,nLayers=5) kill iff mP < %.4g AND mD < %.4g\n",
                  chainGate3Available() ? "trained" : "sentinel", dcaSplit, m3Theta4, m3Theta4D, m3Theta5, m3Theta6,
                  m3ThetaD, m3ThetaRI, m3ThetaR, t4ExemptDcaMin, thetaExempt4, thetaExempt5, thetaExempt6, q3Floor4,
                  q3Floor5, c25Theta, c25ThetaD);
    else if (chainGateMode == 3)
      std::printf("gate (-G 3, M9 DCA split): dcaSplit=%.3f cm (gate logit below, legacy score at/above);"
                  " exempt-branch thresholds U4/5/6=%.3f/%.3f/%.3f (legacy scale)\n",
                  dcaSplit, thetaExempt4, thetaExempt5, thetaExempt6);
    else if (chainGateMode == 4)
      std::printf("gate (-G 4, M9 length+DCA split): dcaSplit=%.3f cm (T4-class always gated;"
                  " 5+ layers gate logit below dcaSplit, legacy score at/above);"
                  " exempt-branch thresholds U5/6=%.3f/%.3f (legacy scale)\n",
                  dcaSplit, thetaExempt5, thetaExempt6);
    else if (chainGateMode == 5)
      std::printf("gate (-G 5, M9 order-preserving DCA split): dcaSplit=%.3f cm; ordering legacy for ALL;"
                  " IP chains killed if gate logit < T4/5/6=%.3f/%.3f/%.3f;"
                  " exempt thresholds U4/5/6=%.3f/%.3f/%.3f (legacy scale);"
                  " exempt-T4 dca floor Z=%.3f cm;"
                  " exempt gate-scale kill V4/5/6=%.3g/%.3g/%.3g\n",
                  dcaSplit, thetaChain4, thetaChain5, thetaChain6, thetaExempt4, thetaExempt5, thetaExempt6,
                  t4ExemptDcaMin, thetaVeto4, thetaVeto5, thetaVeto6);
    if (zdRI != 0.f || zdR != 0.f || zdR5 != 0.f || zdR6 != 0.f || zdM4 != 0.f || zdM4D != 0.f ||
        zdCP != 0.f || zdCD != 0.f || zdAlpha != 0.f)
      std::printf("FANOUT4 transition band |eta| in [%.2f, %.2f): dRI=%+.3f dR=%+.3f (dR5=%+.3f"
                  " dR6=%+.3f) dM4=%+.3f dM4D=%+.3f dCP=%+.3f dCD=%+.3f dAlpha=%+.3f (additive to"
                  " -MRI/-MR/-M4/-M4D/-C25/-C25D/-B; chain eta = innermost T3 eta = the K10 TC eta;"
                  " inLay1-only=%s)\n",
                  zEta1, zEta2, zdRI, zdR, zdR5, zdR6, zdM4, zdM4D, zdCP, zdCD, zdAlpha,
                  zInLay1 >= 0.5f ? "yes" : "no");
    if (fakeOrderAlpha > 0.f || braidFrac > 0.f || hitLevelClaim)
      std::printf("K9 arbitration (A8): claim=%s fakeOrderAlpha=%.2f (order key = score - alpha*max(0,-gateLogit);"
                  " thresholds still on score) braidFrac=%.2f (kill candidate covering >= braidFrac of an"
                  " already-accepted chain's claim items)\n",
                  hitLevelClaim ? "HIT" : "MD", fakeOrderAlpha, braidFrac);
    if (claimCountMD >= 0.f || orderKeyMode != 0.f)
      std::printf("K9 arbitration (B4): claimCount=%d %s (units=%s, -F %.2f still %s) orderKeyMode=%d\n",
                  static_cast<int>(std::floor(claimCountMD)) * (hitLevelClaim ? 2 : 1),
                  claimCountMD >= 0.f ? "ON" : "off", hitLevelClaim ? "HIT" : "MD", maxClaimedFrac,
                  claimCountExcl >= 0.5f ? "DISABLED" : "OR'd", static_cast<int>(std::lround(orderKeyMode)));
    if (orderKeySrc != 0.f || orderKeyHinge != 0.f) {
      static const char* kBkName[12] = {"2class-gate-logit",  "3class-mX-hinge",  "3class-pFake",
                                        "3class-mX-linear",   "3class-mX-only",   "3class-mX-hinge/nL",
                                        "nLayers-major",      "matchFrac-hinge",  "matchFrac-only",
                                        "matchFrac-logit-hinge", "3classB-mX-hinge", "mX-band-major"};
      const int bk = static_cast<int>(std::lround(orderKeySrc));
      std::printf("K9 order key (OKR): -BK %d (%s) -BT %.3f alpha=%.2f\n", bk,
                  (bk >= 0 && bk <= 11) ? kBkName[bk] : "?", orderKeyHinge, fakeOrderAlpha);
    }
    OutputWriter writer(outPath, label);

    // M7c dca-distribution study hook: PROTO_DCA_DUMP=<path> writes one line per
    // pass-1-accepted chain (nLayers, dcaXY, chain label, matched sim vxy, sim pt;
    // labelChains truth, -999 kinematics for pileup-only/unmatched). Off by default;
    // truth is used for the offline justification report only, never for decisions.
    std::FILE* dcaDump = nullptr;
    if (const char* dcaPath = std::getenv("PROTO_DCA_DUMP")) {
      dcaDump = std::fopen(dcaPath, "w");
      if (dcaDump)
        std::fprintf(dcaDump, "# nLayers dcaXY label simVxy simPt\n");
      else
        std::fprintf(stderr, "WARNING: cannot open PROTO_DCA_DUMP path %s\n", dcaPath);
    }

    // ---- M20 (T3ATTACH-BUILD) per-run candidate-finder state ----------------------
    // candIdx is REUSED across events (its vectors keep their capacity); the binned
    // index is rebuilt per event, the map projection re-selected per event.
    const int candModeI = static_cast<int>(candMode + 0.5f);
    PlsCandIndex candIdx;
    CandIndexParams candIdxParams;
    candIdxParams.rtBinW = candRtBinW;
    candIdxParams.binMult = candBinMult;
    candIdxParams.phiPad = candPhiPad;
    MapCandFile mapCandFile;
    if (candModeI == kCandMap && !k8LoadMapCandFile(candMapPath, mapCandFile))
      return 1;  // a half-read candidate list must never be mistaken for a physics result
    double totCandBuildMs = 0.0;
    // -T3E: bare-T3 stage B is tied to -RT3 by default (delivering our pT3 class while
    // LST's carried type-5 rows are ALSO kept double-counts the class). -T3E 1 forces it
    // on anyway as a DIAGNOSTIC, -T3E 0 forces it off.
    const bool doT3Stage = (t3StageEnable < -0.5f) ? (replT3 >= 0.5f) : (t3StageEnable >= 0.5f);

    long long totPixKept = 0;
    long long totChainsIn = 0, totAfterTheta = 0, totAfterPixDrop = 0, totAfterClaim = 0;
    long long totChainTCs = 0, totT5c = 0, totT4c = 0;
    long long totAttached = 0, totPixSuppressed = 0, totPairsPref = 0, totPairsScored = 0;
    long long totPass2Cand = 0, totPass2Acc = 0, totUpgraded = 0;
    long long totSuppByType[3] = {0, 0, 0};  // {pT5 rows, pT3 rows, pLS rows} (-A 2)
    long long totDcaBlocked = 0, totK7Dropped = 0, totGuardKept = 0;  // M7c (-A 2)
    long long totDemoted = 0, totEvidenceOk = 0;                      // M9 (-A 3)
    // M16 (-A 4 general attach delivery)
    long long totDelivT5 = 0, totDelivT3 = 0, totGaDcaBlocked = 0, totM16ChainOwners = 0;
    long long totGaPairs = 0, totGaScored = 0, totGaChainAtt = 0, totGaT3Att = 0;
    long long totSeedDedup = 0;
    long long totCCDropped = 0;      // M20 -CC OT-side crossclean ledger
    long long totSeedDedupT3 = 0;    // M20: the PIXEL-side (-RDT) half, reported apart
    CandStats candStats;         // M20 candidate-finder volume / superset audit
    long long totM16Supp[3] = {0, 0, 0};  // {type 7, type 5, type 8} rows retired by attach
    long long nPostClaimKilled = 0;                                   // M14 (-Q4/-Q5)
    long long nDedupKilled = 0;                                       // M17 (-DD)
    long long nXp3Killed = 0;                                         // EX_DUPCC (-XP3)
    long long nCellSeen = 0, nCellKilled = 0, nCellPreKilled = 0;     // M15 angle C1 (-C25)
    ArbitrationParams::Stats pixClaimStats;                           // B1 (-PU)
    long long totPixOwners = 0;                                       // B1 (-PU)
    TrimStats trimTot;                                                // ANGLE B2 (-TR)
    ExtendStats exStats;                                              // EXPLOIT (-EX)
    double totInferMs = 0.0, totWeldMs = 0.0, totArbMs = 0.0, totFillMs = 0.0, totAttachMs = 0.0;
    double totTrimMs = 0.0, extendMs = 0.0;

    // ================= ATTACH CONFUSION MATRIX (PROTO_ATTACH_CM=1) ====================
    // Diagnostic only: enabling it changes NO decision and NO output row -- it reads the
    // pair log the stages already produced (GeneralAttach::recordPairs) plus sim truth,
    // and prints one block after the normal summary. The harness never sees it.
    //
    // WHY IT EXISTS (maintainer judging protocol): pLS->OT matching errors are
    // metric-coupled, so an aggregate scoreboard cannot tell WHICH error a config is
    // making. RECALL failures (a true pLS/target pair left unconnected) pay in DUP -- the
    // pLS row and the chain row both survive and deliver the same sim twice. PRECISION
    // failures pay in EFF and FR at once: a wrong pLS welded onto a real chain dilutes the
    // combined hit fraction below 0.75 (the TC stops matching, so the sim can lose its
    // ONLY delivery -> eff down, and the TC counts fake -> FR up), and the contention rule
    // additionally suppresses the innocent pLS row that might have been that sim's only
    // remaining delivery.
    //
    // TRUTH CONVENTIONS -- the ones already used by the pairdump factory in this file, so
    // the instrument and the training labels agree:
    //   target sims: chain -> intersection of the >=2/3-MD sim sets of ALL member T3s;
    //                bare T3 -> that T3's own >=2/3-MD sim set (the one-node degenerate
    //                case of the same rule).
    //   pLS sims:    ev.pLS_simIdxAll (>75%-matched sims), deduped. SAME index space
    //                (full tracking-ntuple sim rows), so the intersection is direct.
    //   "true X"  = X's sim set is non-empty; "same-sim pair" = the two sets intersect.
    const bool attachCM = std::getenv("PROTO_ATTACH_CM") != nullptr;
    // Decision classes (denominator = attach decisions actually taken).
    long long cmAttSame = 0;      // same-sim attach: the intended outcome
    long long cmAttCross = 0;     // cross-sim: both sides real, DIFFERENT sims (precision)
    long long cmAttTgtFakePls = 0;   // real target + fake pLS (precision; dilutes the TC)
    long long cmAttFakeTgtRealPls = 0;  // fake target eats a real pLS (precision; also
                                        // suppresses that pLS's own row via contention)
    long long cmAttFakeBoth = 0;  // fake target + fake pLS (harmless to eff, feeds FR)
    long long cmAttSameT3 = 0, cmAttBadT3 = 0;  // stage-B split of the same classes
    // Recall classes (denominator = TRUE pairs the machinery could have made).
    long long cmTruePairPref = 0;     // distinct same-sim pairs present in the PREFILTER
    long long cmTruePairPoss = 0;     // same-sim (target,pLS) combos that EXIST at all in
                                      // the bidding universe -- prefilter recall's denom
    long long cmMissBelowTheta = 0;   // true pair scored BELOW its class margin (head)
    long long cmMissContention = 0;   // true pair ABOVE margin but the pLS/target went
                                      // elsewhere (arbitration, not head quality)
    long long cmMissOther = 0;        // above margin, neither side attached (target lost
                                      // its own best-pair pick to a different pLS)
    // TARGET-LEVEL view. The pair-level recall above has a pileup-inflated denominator
    // (one sim reconstructed with N duplicate pixel seeds offers N true pairs, but only
    // ONE can attach by design), so it cannot answer the question the maintainer asked.
    // These four count TARGETS that had the right answer available in the prefilter:
    //   ...attached the right pLS        -> the machine worked
    //   ...attached a DIFFERENT pLS      -> pure HEAD failure (it ranked a wrong pLS over
    //                                       an available correct one). Pays in EFF+FR.
    //   ...attached nothing              -> RECALL failure (threshold or contention).
    //                                       Pays in DUP.
    long long cmTgtHadTrue = 0, cmTgtGotRight = 0, cmTgtGotWrong = 0, cmTgtGotNone = 0;
    // Same split for targets that had NO true pLS available at all: any attach they make
    // is necessarily wrong, so this isolates head failures the head could not have avoided.
    long long cmTgtNoTrue = 0, cmTgtNoTrueAttached = 0;
    // Suppression accounting: what the contention rule actually retired, by truth.
    long long cmSuppRealPls = 0, cmSuppFakePls = 0;  // -RPS'd type-8 rows by pLS truth
    long long cmSuppRealPlsSameSimOwner = 0;         // ...of which the owner shares the sim
    // Logit distribution, used to CHOOSE the -a scan points. Bin i covers
    // [kCMLo + i*kCMW, kCMLo + (i+1)*kCMW); the two ends are clamped into bins 0/N-1.
    constexpr int kCMBins = 96;
    constexpr float kCMLo = -24.0f, kCMW = 0.5f;
    std::vector<long long> cmHistSame(kCMBins, 0), cmHistBad(kCMBins, 0);
    std::vector<long long> cmHistBestSame(kCMBins, 0), cmHistBestBad(kCMBins, 0);
    auto cmBin = [&](float lo) {
      int b = static_cast<int>(std::floor((lo - kCMLo) / kCMW));
      return b < 0 ? 0 : (b >= kCMBins ? kCMBins - 1 : b);
    };

    for (long long i = 0; i < nRun; ++i) {
      if (!reader.loadEntry(i, ev, trk)) {
        std::fprintf(stderr, "Error: no aligned tracking event for LST entry %lld.\n", i);
        return 1;
      }
      ChainGraph g;
      k1BuildIncidence(ev, g);
      k2BuildEdges(ev, g);
      NodeFeatures nf;
      computeNodeFeatures(ev, nf);
      EdgeFeatures ef;
      computeEdgeFeatures(ev, g, nf, ef);

      EdgeScores scores;
      const auto t0 = std::chrono::steady_clock::now();
      runEdgeInference(g, nf, ef, scores);
      const auto t1 = std::chrono::steady_clock::now();
      Chains chains;
      k6WeldChains(ev, g, scores, thetaEdge, lambdaLen, chains);

      // ANGLE B2 TERMINAL TRIM (-TR 1): post-weld, PRE-gate, PRE-claim. Contract in Trim.h.
      // Placed here on purpose -- ChainFeatures, the -G 6 3-class margins, the chain dcaXY
      // (and therefore the IP/exempt branch assignment), the -U masks and the whole K9 claim
      // are all computed BELOW this line, so every one of them sees the TRIMMED chain. With
      // -TR 0 the call is skipped entirely and `chains` is K6's object untouched.
      if (trimEnable != 0.f) {
        const auto tt0 = std::chrono::steady_clock::now();
        for (int pass = 0; pass < static_cast<int>(trimPasses); ++pass)
          k6TrimTerminals(ev, scores, lambdaLen, trimFactor, static_cast<int>(trimMinLay), trimAbsChi2, chains,
                          nullptr, trimTot);
        totTrimMs += msBetween(tt0, std::chrono::steady_clock::now());
      }

      // Chain gate (-G 1, default): the K9 acceptance score per chain becomes the
      // chain-gate MLP LOGIT (plan 5a hard gate) -- thetaChain4/5/6 cut on that scale
      // and arbitration ORDER uses it too. -G 0 keeps the legacy K6 sum-logit score
      // (chains.score untouched; regression path, no ChainFeatures computed).
      // -G 2 (split / T4-class-only gate): chains with nLayers <= 4 get the gate logit
      // (thetaChain4 cuts on the gate scale); chains with nLayers >= 5 keep the legacy
      // K6 sum-logit score (thetaChain5/6 on the legacy scale, 0 = structural no-op) --
      // ordering mixes the two scales, which sinks short chains to the end of the
      // claim order (long legacy scores dominate the gate logit range).
      // -G 3 (M9 DCA split): the split axis is IP compatibility instead of length --
      // chains whose full-fit circle passes within dcaSplit (-X) of the origin get the
      // gate logit (fakes are IP-compatible and the gate's AUC is strong there); chains
      // with dcaXY >= dcaSplit keep the legacy score (the large-dxy secondaries the
      // full gate keeps zeroing -- M8 finding). Thresholds -T4/-T5/-T6 always cut on
      // chains.score, i.e. on whichever scale governs that chain; exempt chains at
      // T5=0/T6=0 face effectively no cut, like the -G 2 anchor's >=5-layer path.
      // Structural limits (dca >= 0 always): -X 0 -> all exempt == -G 0; -X 1e9 -> all
      // gated == -G 1, at equal thresholds.
      // With -A 1, ChainFeatures + gate logits are computed regardless of -G because
      // attach feature 11 is ALWAYS the gate logit (the pairdump training convention).
      // M7c per-chain transverse DCA (lazy, memoized): the IP-compatibility gate for
      // attach eligibility and K7-lite; M9 reuses it for the -G 3 score split and the
      // -A 3 evidence scope. -1 = not yet computed (dca is always >= 0).
      std::vector<float> dcaCache;
      auto chainDca = [&](int c) -> float {
        if (dcaCache.empty())
          dcaCache.assign(chains.score.size(), -1.f);
        if (dcaCache[c] < 0.f)
          dcaCache[c] = k8ChainDcaXY(ev, chains, c);
        return dcaCache[c];
      };
      ChainFeatures cfHyb;
      std::vector<float> gateLogit;
      // M9 -U thresholds: dca-exempt chains in -G 3/-G 4 keep the legacy score, so their
      // acceptance cuts come from the separate thetaExempt4/5/6 set (legacy scale) via
      // ArbitrationParams::altThreshold; empty mask (modes 0/1/2) = bit-exact legacy.
      std::vector<char> exemptMask;
      // M14 (-Q4/-Q5): the -G 6 per-chain 3-class margin mX and the chain dcaXY, kept
      // alive past the kill block so the POST-claim floors can be applied to the accepted
      // set. Empty for every other -G mode (the floors are -G 6 only).
      std::vector<float> m3mX, m3dcaAll;
      // OKR: the raw 3-class logits, kept alive for the -BK order-key variants (the
      // softmax fake probability needs all three, not just the margin). Empty unless -G 6.
      std::vector<float> m3z3;
      // OKR -BK 7: per-chain predicted harness match fraction (matchFrac regressor head).
      // Empty unless a trained regressor is compiled in.
      std::vector<float> mfPred;
      // FANOUT4 transition diagnostics: per-chain prompt/displaced margins and the -G 6
      // branch code (0 = T4 IP, 1 = T4 exempt, 2 = 5+ IP, 3 = 5+ exempt); -1 elsewhere.
      std::vector<float> dbgMP, dbgMD;
      std::vector<signed char> dbgBr;
      std::vector<char> zBandChain;  // 1 = chain eta inside [-ZE1, -ZE2)
      if (chainGateMode >= 1 || attachMode) {
        computeChainFeatures(ev, g, chains, scores, cfHyb);
        runChainInference(cfHyb, gateLogit);
        if (chainGateMode >= 1) {
        if (chainGateMode == 6) {
          // ANGLE-1 -G 6: THREE-CLASS gate (fake / prompt-true / displaced-true), the
          // LST t3dnn/t4dnn shape, with the chain's transverse DCA as a 17th INPUT
          // instead of only as a hard branch axis. ORDERING STAYS LEGACY for every
          // chain (the M9 cross-scale-inversion lesson): the gate is a KILL only.
          // Branch rules (kill => score -= kGateKill, K9 base threshold kNoCutTheta):
          //   nLayers <= 4  : kill iff mX < -M4                       (T4-class)
          //   5+, dca <  -X : kill iff mP < -MP (unless mX >= -MR)    (IP-compatible)
          //   5+, dca >= -X : kill iff mD < -MD (unless mX >= -MR)    (exempt/large-DCA,
          //                   the M9 residual-fake home)
          // where mP = promptLogit - fakeLogit, mD = displacedLogit - fakeLogit,
          // mX = max(promptLogit, displacedLogit) - fakeLogit.
          // Exempt masks (-> the -U4/-U5/-U6 legacy-scale thresholds) are set exactly as
          // in -G 5: 5+ chains with dca >= -X, and T4-class chains with
          // dca >= max(-X, -Z). So -U4 1e9 still closes the exempt-T4 branch (the v1j
          // shape) and -U4 0 opens it under the 3-class T4 kill.
          const std::size_t nC = gateLogit.size();
          std::vector<float> dcaAll(nC);
          for (std::size_t c = 0; c < nC; ++c)
            dcaAll[c] = chainDca(static_cast<int>(c));
          std::vector<float> z3;
          runChainInference3(cfHyb, dcaAll, z3);
          exemptMask.assign(nC, 0);
          m3mX.assign(nC, 0.f);
          dbgMP.assign(nC, 0.f);
          dbgMD.assign(nC, 0.f);
          dbgBr.assign(nC, -1);
          m3dcaAll = dcaAll;
          m3z3 = z3;  // OKR: -BK order-key variants read the raw logits
          // FANOUT4 transition band levers. inBand uses the chain's K10 eta (innermost
          // member T3), so a chain is tightened in exactly the eta band its TC is counted
          // in. All deltas default to 0 => the whole block is a no-op.
          const bool zOn = zEta2 > zEta1 &&
                           (zdRI != 0.f || zdR != 0.f || zdR5 != 0.f || zdR6 != 0.f || zdM4 != 0.f ||
                            zdM4D != 0.f || zdCP != 0.f || zdCD != 0.f);
          zBandChain.assign(nC, 0);
          for (std::size_t c = 0; c < nC; ++c) {
            const float* z = &z3[3 * c];
            const float mP = z[1] - z[0];
            const float mD = z[2] - z[0];
            const float mX = std::max(z[1], z[2]) - z[0];
            m3mX[c] = mX;
            dbgMP[c] = mP;
            dbgMD[c] = mD;
            const int nL = chains.nLayers[c];
            dbgBr[c] = nL <= 4 ? (dcaAll[c] >= std::max(dcaSplit, t4ExemptDcaMin) ? 1 : 0)
                               : (dcaAll[c] < dcaSplit ? 2 : 3);
            // Band membership (also needed by the -ZBA ordering lever, which is live even
            // when zOn is false because it changes no threshold).
            bool inZ = false;
            if (zEta2 > zEta1 && chains.offsets[c + 1] > chains.offsets[c]) {
              const int t3In = chains.items[chains.offsets[c]];
              if (t3In >= 0 && t3In < static_cast<int>(ev.t3_eta.size())) {
                const float ae = std::fabs(ev.t3_eta[t3In]);
                inZ = ae >= zEta1 && ae < zEta2;
              }
            }
            zBandChain[c] = inZ ? 1 : 0;
            // -ZIL 1: the kill levers only reach chains starting in layer 1.
            bool ilOk = true;
            if (zInLay1 >= 0.5f) {
              ilOk = false;
              if (chains.mdOffsets[c + 1] > chains.mdOffsets[c]) {
                const int m0 = chains.mdItems[chains.mdOffsets[c]];
                ilOk = m0 >= 0 && m0 < static_cast<int>(ev.md_layer.size()) && ev.md_layer[m0] == 1;
              }
            }
            const bool zz = zOn && inZ && ilOk;
            const float dRI = zz ? zdRI : 0.f;
            const float dR = zz ? (zdR + (nL == 5 ? zdR5 : zdR6)) : 0.f;
            const float d4 = zz ? zdM4 : 0.f;
            const float d4D = zz ? zdM4D : 0.f;
            const float dCP = zz ? zdCP : 0.f;
            const float dCD = zz ? zdCD : 0.f;
            if (nL <= 4) {
              if (dcaAll[c] >= std::max(dcaSplit, t4ExemptDcaMin)) {
                // Exempt (large-DCA) T4-class: displaced-oriented acceptance on mD.
                if (mD < m3Theta4D + d4D)
                  chains.score[c] -= kGateKill;
                exemptMask[c] = 1;
              } else if (mX < m3Theta4 + d4) {
                chains.score[c] -= kGateKill;
              }
            } else if (dcaAll[c] < dcaSplit) {
              // IP-compatible 5+: per-length threshold, the -G 6 analogue of -T5/-T6.
              // M14: the OR-rescue here is -MRI (defaults to -MR); the exempt branch below
              // keeps -MR, so the two 5+ branches can be cut independently.
              const float thr = nL >= 6 ? m3Theta6 : m3Theta5;
              if (mP < thr && mX < m3ThetaRI + dRI)
                chains.score[c] -= kGateKill;
            } else {
              // Exempt (large-DCA) 5+: the M9/M10 residual-fake home. -MD is the -G 6
              // analogue of -G 5's a2 -V5/-V6 gate-scale kill; -U5/-U6 still apply on
              // the legacy scale through the exempt mask.
              if (mD < m3ThetaD && mX < m3ThetaR + dR)
                chains.score[c] -= kGateKill;
              exemptMask[c] = 1;
            }
            // ANGLE C1 (M15) -C25/-C25D: extra margin for the (nNodes == 2, nLayers == 5)
            // CELL only, applied on top of whichever branch rule already ran (IP or exempt)
            // and on the same 3-class margin scale. Displaced head respected: kill only
            // when BOTH heads fail. Never re-kills an already-killed chain (counters and
            // the K9 order key stay honest).
            if (c25Theta > -1e9f && nL == 5 && (chains.offsets[c + 1] - chains.offsets[c]) == 2) {
              ++nCellSeen;
              if (chains.score[c] > -0.5f * kGateKill) {
                if (mP < c25Theta + dCP && mD < c25ThetaD + dCD) {
                  chains.score[c] -= kGateKill;
                  ++nCellKilled;
                }
              } else {
                ++nCellPreKilled;
              }
            }
          }
        } else {
          if (chainGateMode >= 3)
            exemptMask.assign(gateLogit.size(), 0);
          for (std::size_t c = 0; c < gateLogit.size(); ++c) {
            if (chainGateMode == 5) {
              // Order-preserving DCA split: chains.score is NEVER rewritten (legacy
              // ordering for everyone); the gate only KILLS IP-compatible chains whose
              // logit fails the gate-scale -T threshold. Exempt chains face -U via the
              // altThreshold mask; K9's base threshold is kNoCutTheta in this mode.
              // -Z: T4-class chains need dca >= max(dcaSplit, t4ExemptDcaMin) to be
              // exempt (displaced-oriented length-4 acceptance); otherwise IP path.
              const int nL = chains.nLayers[c];
              const float exemptFloor = nL <= 4 ? std::max(dcaSplit, t4ExemptDcaMin) : dcaSplit;
              if (chainDca(static_cast<int>(c)) < exemptFloor) {
                const float thr = nL >= 6 ? thetaChain6 : (nL == 5 ? thetaChain5 : thetaChain4);
                if (gateLogit[c] < thr)
                  chains.score[c] -= kGateKill;
              } else {
                exemptMask[c] = 1;
                // a2: optional gate-scale kill on the exempt branch (-V4/-V5/-V6).
                // Additive to the legacy -U cut; ordering untouched. -1e9 = no-op.
                const float vthr = nL >= 6 ? thetaVeto6 : (nL == 5 ? thetaVeto5 : thetaVeto4);
                if (gateLogit[c] < vthr)
                  chains.score[c] -= kGateKill;
              }
              continue;
            }
            const bool useGate = chainGateMode == 1 || (chainGateMode == 2 && chains.nLayers[c] <= 4) ||
                                 (chainGateMode == 3 && chainDca(static_cast<int>(c)) < dcaSplit) ||
                                 (chainGateMode == 4 &&
                                  (chains.nLayers[c] <= 4 || chainDca(static_cast<int>(c)) < dcaSplit));
            if (useGate)
              chains.score[c] = gateLogit[c];
            else if (chainGateMode >= 3)
              exemptMask[c] = 1;
          }
          }
        }
      }
      const auto t2 = std::chrono::steady_clock::now();

      ArbitrationParams ap;
      // -G 5: the gate cut was already applied to chains.score (kill), so K9's base
      // per-length threshold must be a no-op for live chains while still rejecting
      // killed / -A 3-demoted ones.
      ap.thetaChain4 = chainGateMode >= 5 ? kNoCutTheta : thetaChain4;
      ap.thetaChain5 = chainGateMode >= 5 ? kNoCutTheta : thetaChain5;
      ap.thetaChain6 = chainGateMode >= 5 ? kNoCutTheta : thetaChain6;
      ap.thetaAlt4 = thetaExempt4;
      ap.thetaAlt5 = thetaExempt5;
      ap.thetaAlt6 = thetaExempt6;
      if (!exemptMask.empty())
        ap.altThreshold = &exemptMask;
      ap.maxClaimedFrac = maxClaimedFrac;
      // B4 (-FC/-FCX): absolute claim tolerance. -FC is expressed in MD units for
      // physics readability ("allow N already-claimed MDs"); with -H 1 the claim
      // universe is HITS and each MD contributes 2 of them, so the budget doubles.
      if (claimCountMD >= 0.f) {
        const int nMDbudget = static_cast<int>(std::floor(claimCountMD));
        ap.maxClaimedItems = hitLevelClaim ? 2 * nMDbudget : nMDbudget;
        ap.claimCountExclusive = claimCountExcl >= 0.5f;
        // DUPCUT -FCE: exempt (dcaXY >= dcaSplit) chains keep the loosen-only OR;
        // at -FCE 2 they are additionally exempt from the -W braid kill.
        if (claimStrictIpOnly >= 0.5f && !exemptMask.empty()) {
          ap.strictExemptMask = &exemptMask;
          ap.strictExemptBraid = claimStrictIpOnly >= 1.5f;
        }
      }
      ap.sharePassFrac = sharePassFrac;
      ap.dropPixelConsumed = dropPixelConsumed;

      // K8 pixel attach (-A 1 and -A 2): the attach decision is made over ALL
      // theta-passing chains BEFORE any pixel-consumed drop or claim (K8 itself
      // considers only nLayers >= 5; one chain per pLS, contention by logit).
      // -A 1 (rejected v1): pixdrop with a per-chain partOfPT5 bypass for attached
      // chains -> single MD-claim arbitration (the bypass floods the claim; kept as
      // reference). -A 2 (M7b): NO bypass anywhere -- pass 1 of the two-pass claim is
      // the exact -A 0 pipeline; attachment only (a) upgrades attached pass-1-accepted
      // chains to type 7 in place and (b) qualifies attached-but-pixdropped chains for
      // the subordinate pass 2. In both modes a chain that attached but produced no TC
      // triggers NO suppression (its pLS keeps its baseline rows; no fallback).
      // -A 3 (M9 attach-as-EVIDENCE): the SAME K8 call over theta-passing chains, but
      // the outcome is consumed ONLY as an acceptance modifier -- an IP-compatible
      // (dcaXY < dcaSplit, same -X as -G 3) 5+-layer theta-passing chain whose best
      // scored pair is below thetaAttach (PRE-contention, Attachments::bestLogit --
      // losing the one-chain-per-pLS exclusivity is not absence of pixel evidence)
      // gets demotePenalty subtracted from chains.score BEFORE K9, so the per-length
      // thresholds AND the claim order both see the demotion (default 1e6 = reject
      // outright). Large-DCA chains are skipped from the K8 call entirely (exempt:
      // pLS absence is expected off the IP; skipping also saves attach-scoring cost).
      // No bypass, no chainAttachPls fill, no suppression -> TC assembly and writer
      // behave exactly as -A 0 (chain TCs stay bare type 4/9); with -X 0 every chain
      // is exempt and the output is bit-identical to -A 0 by construction.
      // -A 4 (M16 GENERAL ATTACH DELIVERY, plan 11 "PIXEL-REPLACEMENT VALIDATION"):
      // the attach stops being a classifier and becomes the DELIVERY path for the pixel
      // classes.
      //
      // ---- M16b STAGING FIX (judge option B, 2026-08-02) ------------------------------
      // The ORIGINAL M16 order ran stage A BEFORE K9 over every theta-passing chain and
      // then EXCLUDED the attached chains from the claim (ArbitrationParams::excludeChain),
      // emitting them unconditionally. That made attach a SECOND, UNARBITRATED delivery
      // channel with no mutual hit contention among its own deliveries. Measured cost:
      // 565 attach/evt where K9 would have accepted 130, attach-vs-attach braid dups
      // 487 -> 12,807 pairs (every sibling chain of one track delivered side by side), and
      // a starved bare-chain slice (-0.24 displaced eff at vxy[5,10) / [10,30)) because
      // the chains that DID walk the claim were the leftovers. The aggregate failed on
      // eff, dup and fake simultaneously; the judge located all of it to this one defect.
      //
      // M16b resolves attach ON THE K9-ACCEPTED CHAIN SET instead:
      //   (1) SUPPRESSION SET -- the WHOLESALE half only (-RT5 / -RT3), which is
      //       attach-independent by definition. The contention half (a carried row whose
      //       pLS now has an outer-tracker owner) and the -RPS predicate are refreshed
      //       after each attach stage, since attach has not run yet.
      //   (2) PRE-CLAIM OWNER LIST (-PU) -- built from the SURVIVING carried rows. No
      //       attached chains are appended any more: an attached chain is a K9 winner, so
      //       it claims its own hits inside the walk. The pre-claimed volume is conserved
      //       the same way, one stage later.
      //   (3) K9 over ALL chains, NO exclusions. This is the fix: mutual hit contention
      //       among the would-be pixel deliveries is decided exactly once, by the claim
      //       that already exists. Siblings of one track cannot all deliver, because all
      //       but one lost the claim; and no chain skips the walk, so the bare-chain
      //       slice sees the same universe it sees at -A 0.
      //   (4) STAGE A -- chain-target attach OFFERED to the accepted nLayers >= 5 chains
      //       (subject to the -D4 dca gate). A granted pLS turns that chain's TC from
      //       type 4 into type 7 IN PLACE (an upgrade of a claim winner, never an extra
      //       TC); an accepted chain that is not granted one delivers bare, as at -A 0.
      //   (5) STAGE B -- bare-T3 attach, against the same ACCEPTED set
      //       (k8BuildBareT3Mask contract). Bids only for pLS stage A left free: ONE pLS,
      //       ONE owner, across target types.
      //   (6) DELIVERY -- upgraded type-7 chain TCs, type-5 TCs from stage B, bare chain
      //       TCs from K9, carried rows minus the suppression set.
      //
      // Residual approximations, stated rather than hidden: (i) both attach stages are
      // decided after K9, so the deliveries' pixel hits do not participate in the claim
      // and the contention-suppressed carried rows DID pre-claim (the chains faced a
      // slightly stricter claim than the final output implies) -- at real integration all
      // universes resolve before the single claim; (ii) chain targets get first refusal on
      // a contested pLS instead of a single global logit sort (the per-length ordering,
      // see AttachDelivery.h).
      GeneralAttachParams gap;
      gap.pref.thetaAttach = thetaAttach;
      gap.thetaAttachT3 = thetaAttachT3;
      // ---- M20 CANDIDATE FINDING (PixelAttachCand.h). candMode 0 leaves every field at
      // its default, so the frozen full analytic scan is untouched.
      gap.pref.candMode = candModeI;
      gap.pref.candAudit = (candAudit >= 0.5f);
      gap.pref.candMapWindows = (candMapWin >= 0.5f);
      gap.pref.candChainToo = (candChainToo >= 0.5f);
      if (candModeI != kCandAnalytic) {
        gap.pref.candStats = &candStats;
        const auto tcb0 = std::chrono::steady_clock::now();
        if (candModeI == kCandBinned)
          k8BuildPlsCandIndex(ev, gap.pref, candIdxParams, candIdx);
        else
          k8SelectMapCandidates(ev, mapCandFile, candIdx, &candStats);
        totCandBuildMs += msBetween(tcb0, std::chrono::steady_clock::now());
        gap.pref.cand = &candIdx;
      }
      GeneralAttach ga;
      long long nGaDcaBlocked = 0;

      Attachments att;
      std::vector<int> thetaPass;
      std::vector<char> attachBypass;
      std::vector<int> chainAttachPls;  // per-chain: attached pLS row or -1
      double attachMs = 0.0;
      long long nDcaBlocked = 0, nDemoted = 0, nEvidenceOk = 0;
      if (attachMode == 4) {
        // M16b: nothing attach-dependent may run before K9 any more. Only the decision
        // state is sized here; stage A runs on `accepted`, further down.
        const int nChainsAll = static_cast<int>(chains.score.size());
        gaInit(ev, chains, ga);
        ga.recordPairs = attachCM;  // diagnostic pair log; no effect on any decision
        chainAttachPls.assign(nChainsAll, -1);
        // The pixel-consumed drop exists only because a kept baseline pixel row already
        // delivers that track. Wholesale replacement removes those rows, so the
        // corresponding half of the crossclean must go with them.
        if (replT5 >= 0.5f)
          ap.dropPartOfPT5 = false;
        if (replT3 >= 0.5f)
          ap.dropPartOfPT3 = false;
        // EX_DUPCC (c) -XT3: the pT3 <-> chain EXCHANGE. The partOfPT3 half of the drop
        // says "a kept pT3 row already delivers this T3", which is only an argument for
        // killing the chain if the pT3 row is the better delivery. -XT3 1 lets those
        // chains compete instead; -XP3 then retires the pT3 row the winner replaced.
        if (xt3Mode >= 0.5f)
          ap.dropPartOfPT3 = false;
      } else if (attachMode) {
        const int nChainsAll = static_cast<int>(chains.score.size());
        for (int c = 0; c < nChainsAll; ++c) {
          if (chains.score[c] < ap.thetaForChain(c, chains.nLayers[c]))
            continue;
          // M7c (a): IP-compatibility gate on attach ELIGIBILITY (-A 2 only). Chains
          // whose full-fit circle misses the origin by >= dcaMax never bid for a pLS:
          // no in-place upgrade, no pass-2 candidacy, no suppression -- displaced
          // chains keep their bare deliveries (the M7b upgrade-dilution fix). Gate
          // evaluated only for the K8 scope (nLayers >= 5; shorter chains never bid).
          if (attachMode == 2 && chains.nLayers[c] >= 5 && chainDca(c) >= dcaAttachMax) {
            ++nDcaBlocked;
            continue;
          }
          // M9 -A 3: large-DCA chains are EXEMPT from the evidence rule (and from the
          // K8 call). Same shape as the M7c gate, keyed on dcaSplit (-X).
          if (attachMode == 3 && chains.nLayers[c] >= 5 && chainDca(c) >= dcaSplit) {
            ++nDcaBlocked;
            continue;
          }
          thetaPass.push_back(c);
        }
        AttachParams apar;
        apar.thetaAttach = thetaAttach;
        const auto ta0 = std::chrono::steady_clock::now();
        k8AttachPixels(ev, chains, thetaPass, cfHyb, gateLogit, apar, att);
        const auto ta1 = std::chrono::steady_clock::now();
        attachMs = msBetween(ta0, ta1);
        attachBypass.assign(nChainsAll, 0);
        chainAttachPls.assign(nChainsAll, -1);
        if (attachMode == 3) {
          // Evidence consumption: demote IP-compatible 5+-layer chains with no pair
          // above thetaAttach. attachBypass/chainAttachPls stay empty -- no downstream
          // effect beyond the score.
          for (std::size_t pos = 0; pos < thetaPass.size(); ++pos) {
            const int c = thetaPass[pos];
            if (chains.nLayers[c] < 5)
              continue;  // K8 scope; T4-class chains carry no attach evidence
            if (att.bestLogit[pos] >= thetaAttach) {
              ++nEvidenceOk;
            } else {
              chains.score[c] -= demotePenalty;
              ++nDemoted;
            }
          }
        } else {
          for (std::size_t pos = 0; pos < thetaPass.size(); ++pos) {
            if (att.plsRow[pos] >= 0) {
              attachBypass[thetaPass[pos]] = 1;
              chainAttachPls[thetaPass[pos]] = att.plsRow[pos];
            }
          }
        }
      }

      // Arbitration. -A 0: legacy. -A 1: single pass with the (rejected) partOfPT5
      // bypass for attached chains. -A 2 (M7b): subordinate two-pass claim -- pass 1 is
      // the exact -A 0 call (bit-identical accepted set by construction), pass 2 lets
      // attached-but-pixdropped chains claim only what pass 1 left free. The accepted
      // list is pass 1 followed by pass 2 (nPass1 marks the boundary).
      // A8 (-B / -W): fake-specific ORDERING + braid suppression, the only two axes M9
      // found still able to move FR once the claim volume saturated. -B builds a separate
      // ordering key (score minus a gate-suspicion penalty) so fake-suspect chains claim
      // LAST while every acceptance threshold keeps cutting on chains.score; -W adds the
      // owner-relative braid kill inside the greedy walk. Both default to legacy.
      // B4 (-OK): DE-LENGTHED ordering. chains.score = sum(edge logits) +
      // lambdaLen*nLayers is monotone in length twice over (more edges AND the explicit
      // prior), so in the greedy claim a long chain always walks ahead of a short one of
      // equal per-edge quality -- the M10 forensics population "pure 5-MD displaced
      // chains starved by the length-monotone score". -OK reshapes ONLY the ordering key;
      // every acceptance threshold still cuts on chains.score (M9 lesson).
      // OKR (-BK / -BT): the ordering PENALTY SOURCE. Same contract as -B: the key is
      // never a threshold. See the flag declaration above for the mode table.
      const int okMode = static_cast<int>(std::lround(orderKeyMode));
      std::vector<float> orderKeyVec;
      const bool haveGateKey = gateLogit.size() == chains.score.size();
      int bkMode = static_cast<int>(std::lround(orderKeySrc));
      // -BK 7/8/9: run the matchFrac regressor once per event. mfPred is the [0,1]
      // fraction, mfLogitV the raw pre-sigmoid output (a wider, unsaturated scale that
      // behaves like the gate logits the alpha scan is calibrated on).
      std::vector<float> mfLogitV;
      // -BK 10: the SECONDARY 3-class head's mX margin. Same key shape as -BK 1 but a
      // different ranking network, with the resident gate (and therefore every kill
      // decision and threshold) left exactly as it is.
      std::vector<float> m3bmX;
      if (bkMode == 10 && !cfHyb.f.empty()) {
        const std::size_t nC = chains.score.size();
        std::vector<float> dcaAll(nC), z3b;
        for (std::size_t c = 0; c < nC; ++c)
          dcaAll[c] = m3dcaAll.size() == nC ? m3dcaAll[c] : chainDca(static_cast<int>(c));
        runChainInference3b(cfHyb, dcaAll, z3b);
        m3bmX.resize(nC);
        for (std::size_t c = 0; c < nC; ++c) {
          const float* z = &z3b[3 * c];
          m3bmX[c] = std::max(z[1], z[2]) - z[0];
        }
      }
      if (bkMode >= 7 && bkMode <= 9 && !cfHyb.f.empty()) {
        if (!chainMatchFracAvailable()) {
          std::fprintf(stderr, "-BK %d needs a TRAINED mf_mlp_weights.h (sentinel found)\n", bkMode);
        } else {
          const std::size_t nC = chains.score.size();
          std::vector<float> dcaAll(nC);
          for (std::size_t c = 0; c < nC; ++c)
            dcaAll[c] = m3dcaAll.size() == nC ? m3dcaAll[c] : chainDca(static_cast<int>(c));
          runChainInferenceMF(cfHyb, dcaAll, mfLogitV);
          mfPred.resize(nC);
          for (std::size_t c = 0; c < nC; ++c)
            mfPred[c] = 1.f / (1.f + std::exp(-std::min(60.f, std::max(-60.f, mfLogitV[c]))));
        }
      }
      const bool have3 = m3mX.size() == chains.score.size() && m3z3.size() == 3 * chains.score.size();
      if (bkMode >= 1 && bkMode <= 5 && !have3) {
        std::fprintf(stderr, "-BK %d needs the -G 6 three-class head; falling back to -BK 0\n", bkMode);
        bkMode = 0;
      }
      if (bkMode >= 7 && bkMode <= 9 && mfPred.size() != chains.score.size()) {
        std::fprintf(stderr, "-BK %d has no matchFrac predictions; falling back to -BK 0\n", bkMode);
        bkMode = 0;
      }
      if (bkMode == 10 && m3bmX.size() != chains.score.size()) {
        std::fprintf(stderr, "-BK 10 has no secondary-head margins; falling back to -BK 0\n");
        bkMode = 0;
      }
      if (bkMode == 11 && !have3) {
        std::fprintf(stderr, "-BK 11 needs the -G 6 three-class head; falling back to -BK 0\n");
        bkMode = 0;
      }
      // COMPOSE: the okredesign -BK guard (bkMode != 0 supplies its own key source, so a
      // missing 2-class gate no longer disables the penalty) OR'd with the transition
      // -ZBA trigger (a band-only alpha must arm the penalty even at -B 0). With
      // bkMode == 0 and zdAlpha == 0 this is the anchor's `fakeOrderAlpha > 0 && haveGateKey`.
      const bool wantPenalty = (fakeOrderAlpha > 0.f || zdAlpha != 0.f) && (bkMode != 0 || haveGateKey);
      if (wantPenalty || okMode != 0 || bkMode == 4 || bkMode == 6 || bkMode == 8 || bkMode == 11) {
        orderKeyVec.resize(chains.score.size());
        // -BK 6 / -BK 11 need the EXACT legacy ordering inside each class, so the inner
        // key is rank-encoded (a float cannot hold class*stride plus a fine-grained
        // score without dropping low bits). rank[c] = position of c in the inner order.
        std::vector<int> bkRank;
        if (bkMode == 6 || bkMode == 11) {
          const int n = static_cast<int>(chains.score.size());
          std::vector<float> inner(n);
          for (int c = 0; c < n; ++c)
            inner[c] = bkMode == 11
                           ? chains.score[c]
                           : chains.score[c] - (haveGateKey ? fakeOrderAlpha *
                                                                  std::max(0.f, orderKeyHinge - gateLogit[c])
                                                            : 0.f);
          std::vector<int> idx(n);
          for (int c = 0; c < n; ++c)
            idx[c] = c;
          std::sort(idx.begin(), idx.end(), [&inner](int a, int b) {
            if (inner[a] != inner[b])
              return inner[a] > inner[b];
            return a < b;
          });
          bkRank.assign(n, 0);
          for (int r = 0; r < n; ++r)
            bkRank[idx[r]] = r;
        }
        for (std::size_t c = 0; c < orderKeyVec.size(); ++c) {
          float base = chains.score[c];
          const float nL = static_cast<float>(std::max(1, chains.nLayers[c]));
          if (okMode == 1) {
            base -= lambdaLen * nL;
          } else if (okMode == 2) {
            base /= nL;
          } else if (okMode == 3) {
            const int nEdges = std::max(1, chains.offsets[c + 1] - chains.offsets[c] - 1);
            base = (base - lambdaLen * nL) / static_cast<float>(nEdges);
          }
          // -ZBA (transition): extra fake-order alpha for band chains (ORDERING ONLY --
          // thresholds still cut on chains.score, so this can never kill a chain).
          // zdAlpha == 0 => alphaC == fakeOrderAlpha, i.e. the okredesign key verbatim.
          const float alphaC =
              fakeOrderAlpha + ((zdAlpha != 0.f && c < zBandChain.size() && zBandChain[c]) ? zdAlpha : 0.f);
          // Penalty term (>= 0 subtracts from the key, i.e. the chain claims later).
          float pen = 0.f;
          if (bkMode == 0) {
            pen = haveGateKey ? std::max(0.f, orderKeyHinge - gateLogit[c]) : 0.f;
          } else if (bkMode == 1) {
            pen = std::max(0.f, orderKeyHinge - m3mX[c]);
          } else if (bkMode == 2) {
            // softmax fake probability = 1 / (1 + exp(mP) + exp(mD)); written from the
            // margins so the shared zF cancels and large logits cannot overflow.
            const float* z = &m3z3[3 * c];
            const float mP = std::min(60.f, z[1] - z[0]);
            const float mD = std::min(60.f, z[2] - z[0]);
            pen = 1.f / (1.f + std::exp(mP) + std::exp(mD));
          } else if (bkMode == 3) {
            pen = orderKeyHinge - m3mX[c];
          } else if (bkMode == 5) {
            pen = std::max(0.f, orderKeyHinge - m3mX[c]) / nL;
          } else if (bkMode == 7) {
            pen = std::max(0.f, orderKeyHinge - mfPred[c]);
          } else if (bkMode == 9) {
            pen = std::max(0.f, orderKeyHinge - mfLogitV[c]);
          } else if (bkMode == 10) {
            pen = std::max(0.f, orderKeyHinge - m3bmX[c]);
          }
          if (bkMode == 4) {
            orderKeyVec[c] = m3mX[c];
          } else if (bkMode == 8) {
            orderKeyVec[c] = mfPred[c];
          } else if (bkMode == 6) {
            // nLayers-major, legacy key within: rank-exact, no float precision loss.
            const int n = static_cast<int>(orderKeyVec.size());
            orderKeyVec[c] = nL * static_cast<float>(n) + static_cast<float>(n - 1 - bkRank[c]);
          } else if (bkMode == 11) {
            // mX BAND-major (band width -BT, default 1), legacy score order within a band.
            // Bands are clamped so band*n stays exactly representable in a float.
            const int n = static_cast<int>(orderKeyVec.size());
            const float w = orderKeyHinge > 0.f ? orderKeyHinge : 1.f;
            int band = static_cast<int>(std::floor(m3mX[c] / w));
            band = std::min(40, std::max(-8, band));
            orderKeyVec[c] = static_cast<float>(band + 8) * static_cast<float>(n) +
                             static_cast<float>(n - 1 - bkRank[c]);
          } else {
            orderKeyVec[c] = base - (wantPenalty ? alphaC * pen : 0.f);
          }
        }
        ap.orderKey = &orderKeyVec;
      }
      ap.braidFrac = braidFrac;
      ap.hitLevelClaim = hitLevelClaim != 0;

      // ---- EX_DUPCC (a): BAND-AWARE BRAID MASK (-WE/-WZ/-WN) ------------------------
      // Band membership is pure geometry/topology of the CANDIDATE: |eta| of the innermost
      // member T3 (the same coordinate K10 writes into the TC) and the member-T3 count.
      // Nothing about any other object enters, so this is a per-chain property, not a
      // proximity test between two tracks -- the braid test itself stays hit-structural.
      std::vector<char> braidAltMaskV;
      const bool bandOn = braidFracAlt > 0.f || claimFracAlt > 0.f || claimItemsAlt > -1.5f;
      if (bandOn) {
        const int nChAlt = static_cast<int>(chains.score.size());
        braidAltMaskV.assign(nChAlt, 0);
        for (int c = 0; c < nChAlt; ++c) {
          const int ib = chains.offsets[c];
          if (ib >= chains.offsets[c + 1])
            continue;
          const float aEta = std::fabs(ev.t3_eta[chains.items[ib]]);
          const float nNodes = static_cast<float>(chains.offsets[c + 1] - ib);
          if (aEta >= braidAltEta && nNodes <= braidAltMaxNodes)
            braidAltMaskV[c] = 1;
        }
        ap.braidFracAlt = braidFracAlt;
        ap.braidAltMask = &braidAltMaskV;
        if (claimFracAlt > 0.f)
          ap.maxClaimedFracAlt = claimFracAlt;
        if (claimItemsAlt > -1.5f)
          ap.maxClaimedItemsAlt =
              static_cast<int>(std::floor(claimItemsAlt)) * (hitLevelClaim ? 2 : 1);
      }

      // B1 (-PU): CLAIM-UNIVERSE UNIFICATION. The hybrid keeps every baseline pixel TC
      // verbatim, but K9 arbitrates chains against chains ONLY -- a chain sitting on a
      // kept pT5's / pT3's outer-tracker hits pays nothing for it. M13 measured the cost:
      // 48.9% of surviving fake chains sit on a pixel TC's hits and 66% of the residual
      // fake is "contaminated" (one wrong arm on a real, pixel-delivered track), plus the
      // whole chain-vs-pixel dup artifact. Here the kept pixel TCs' OT hits are
      // PRE-CLAIMED, so those chains face the same maxClaimedFrac (-PU 2 also: braid)
      // rules as chain-vs-chain. Displaced chains are unaffected by construction: they
      // have no pLS partner, hence no pixel TC to collide with (verified by the vxy/dxy
      // bands in the A/B).
      // Owners are the KEPT baseline pixel rows: type 7 -> pT5_t5Idx -> t5_hitIndices
      // (which INCLUDES the ExtendT5FromDupT5 layer-6/7 hits the T3 route cannot reach),
      // type 5 -> pT3_otHitIndices, type 8 -> no OT hits at all. Both branches are ph2
      // rows, the same space as md_anchorHitIdx/md_otherHitIdx (see EventData.h).
      // Attach modes rewrite which pixel rows survive. -A 0..3: -PU sees every carried
      // row (the M15 behavior, unchanged). -A 4: the owner list is built from the
      // POST-SUPPRESSION rows -- see the M16 suppression block immediately below.

      // Helper (hoisted from the TC-assembly block below so the M16 delivery path can use
      // it too): pixel hit rows of a pLS (seedIdx -> trk see_hitIdx, Pixel-type entries).
      auto plsPixelHits = [&](int p, std::vector<int>& hits) {
        hits.clear();
        const int seed = ev.pLS_seedIdx[p];
        if (seed < 0 || seed >= static_cast<int>(trk.see_hitIdx.size()))
          return;
        const auto& hIdx = trk.see_hitIdx[seed];
        const auto& hTyp = trk.see_hitType[seed];
        for (std::size_t h = 0; h < hIdx.size() && h < hTyp.size(); ++h)
          if (hTyp[h] == static_cast<int>(proto::HitType::Pixel))
            hits.push_back(hIdx[h]);
      };

      // The pLS a carried baseline pixel row was built from (-1 if none/unavailable).
      auto rowPls = [&](std::size_t it, int ty) -> int {
        if (ty == 7 && it < ev.tc_pt5Idx.size()) {
          const int i5 = ev.tc_pt5Idx[it];
          if (i5 >= 0 && i5 < static_cast<int>(ev.pT5_plsIdx.size()))
            return ev.pT5_plsIdx[i5];
        } else if (ty == 5 && it < ev.tc_pt3Idx.size()) {
          const int i3 = ev.tc_pt3Idx[it];
          if (i3 >= 0 && i3 < static_cast<int>(ev.pT3_plsIdx.size()))
            return ev.pT3_plsIdx[i3];
        } else if (ty == 8 && it < ev.tc_plsIdx.size()) {
          return ev.tc_plsIdx[it];
        }
        return -1;
      };

      // ---- (1b) SEED-FAMILY DEDUP OF THE ATTACH OWNERS (-RD) -------------------------
      // The contention rule is ONE pLS, ONE owner -- but a track reconstructed with N
      // duplicate pixel seeds offers N distinct pLS, so N chains attach and N pT5-class
      // TCs are delivered for one track. Plan 11 keeps exactly one piece of the retired
      // crossclean stack for this ("retain only the upstream pLS seed dup-clean"); here
      // it lives inside the contention rule. Sim-blind: two pLS are the same seed when
      // they share >= 2 pixel hit rows (production pixelHitsOverlapAny). Highest attach
      // logit keeps the family; the losers' targets go back to the K9 walk and their
      // carried rows survive untouched.
      // The kept-seed family map persists across BOTH attach stages: a track whose pT5
      // delivery already used one of its duplicate seeds must not also get a pT3
      // delivery from a sibling seed. seedFamilyDup(p) reports whether pLS p shares
      // >= 2 pixel hit rows with an already-kept owner; seedFamilyKeep(p) registers it.
      long long nSeedDedup = 0, nSeedDedupT3 = 0;
      std::unordered_map<int, std::vector<int>> hit2keptPls;  // pixel hit row -> kept pLS
      std::vector<int> seedPixHits;
      auto seedFamilyDup = [&](int p) {
        plsPixelHits(p, seedPixHits);
        std::unordered_map<int, int> shareCnt;
        for (int h : seedPixHits) {
          const auto it = hit2keptPls.find(h);
          if (it == hit2keptPls.end())
            continue;
          for (int q : it->second)
            if (++shareCnt[q] >= 2)
              return true;
        }
        return false;
      };
      auto seedFamilyKeep = [&](int p) {
        plsPixelHits(p, seedPixHits);
        for (int h : seedPixHits)
          hit2keptPls[h].push_back(p);
      };
      // (M16b: the stage-A application of this rule moved BELOW K9, next to stage A.)

      // ---- (2) M16 SUPPRESSION SET (-A 4) -------------------------------------------
      // Which carried baseline pixel rows has the attach REPLACED? Two independent
      // reasons, both sim-blind:
      //   CONTENTION BOOKKEEPING: the row's own pLS now has an outer-tracker owner, so
      //     the row is a second delivery of the same seed. This single rule replaces
      //     CrossCleanpT5 + CrossCleanpT3 + CrossCleanpLS (plan 11).
      //   WHOLESALE REPLACEMENT (-RT5 / -RT3): the A/B that actually answers the
      //     maintainer's question -- can our machinery deliver the class at all?
      // M16b: only the WHOLESALE half can be computed here -- it is attach-independent by
      // definition -- because attach no longer runs before K9. The contention half and the
      // -RPS predicate are applied by m16RefreshSupp() after each attach stage. The
      // wholesale half is what the pre-claim owner list needs, and it is the half that
      // matters for it: a class being replaced wholesale must not pre-claim.
      std::vector<char> m16RowSuppressed;
      long long nM16SuppT7 = 0, nM16SuppT5 = 0, nM16SuppT8 = 0;
      if (attachMode == 4) {
        m16RowSuppressed.assign(ev.tc_type.size(), 0);
        for (std::size_t it = 0; it < ev.tc_type.size(); ++it) {
          const int ty = ev.tc_type[it];
          if ((ty == 7 && replT5 >= 0.5f) || (ty == 5 && replT3 >= 0.5f)) {
            m16RowSuppressed[it] = 1;
            (ty == 7 ? nM16SuppT7 : nM16SuppT5) += 1;
          }
        }
      }
      // CONTENTION + -RPS refresh. Idempotent (already-suppressed rows are skipped), so it
      // can be called after every attach stage; each call only ever adds rows.
      auto m16RefreshSupp = [&]() {
        for (std::size_t it = 0; it < ev.tc_type.size(); ++it) {
          if (m16RowSuppressed[it])
            continue;
          const int ty = ev.tc_type[it];
          if (ty != 7 && ty != 5 && ty != 8)
            continue;
          const int pls = rowPls(it, ty);
          if (pls < 0 || pls >= static_cast<int>(ga.plsOwned.size()))
            continue;
          bool drop = ga.plsOwned[pls] != 0;
          if (ty == 8 && replPls >= 0.5f)
            // "attached anywhere OR lost a contention above the class margin": the pLS had
            // a scored pair above its margin but is not the owner. A bare-pLS TC of a seed
            // the outer tracker can explain is precisely what plsembdnn is for.
            drop = drop || ga.plsBestChainLogit[pls] >= thetaAttach || ga.plsBestT3Logit[pls] >= thetaAttachT3;
          if (drop) {
            m16RowSuppressed[it] = 1;
            (ty == 7 ? nM16SuppT7 : (ty == 5 ? nM16SuppT5 : nM16SuppT8)) += 1;
          }
        }
      };

      // ---- (3) PRE-CLAIM OWNER LIST --------------------------------------------------
      std::vector<std::vector<int>> pixOwnerHits;
      long long nM16ChainOwners = 0;
      if (preClaimMode > 0) {
        const std::size_t nTCb = ev.tc_type.size();
        pixOwnerHits.reserve(nTCb);
        for (std::size_t it = 0; it < nTCb; ++it) {
          const int ty = ev.tc_type[it];
          // M16: a row attach has replaced owns nothing any more -- skip it, or the
          // chains would pay a claim cost for hits that no delivery holds.
          if (!m16RowSuppressed.empty() && m16RowSuppressed[it])
            continue;
          if (ty == 7) {
            const int p5 = (it < ev.tc_pt5Idx.size()) ? ev.tc_pt5Idx[it] : -999;
            if (p5 < 0 || p5 >= static_cast<int>(ev.pT5_t5Idx.size()))
              continue;
            const int t5 = ev.pT5_t5Idx[p5];
            if (t5 < 0 || t5 >= static_cast<int>(ev.t5_hitIndices.size()))
              continue;
            pixOwnerHits.push_back(ev.t5_hitIndices[t5]);
          } else if (ty == 5) {
            // EX_DUPCC (c) -XT3 2: carried pT3 rows stop pre-claiming, so a chain that
            // reuses their T3 can actually win the greedy walk instead of being priced
            // out by 6 pre-claimed hits out of its 8-12. Without this, -XT3 1 is inert.
            if (xt3Mode >= 1.5f)
              continue;
            const int p3 = (it < ev.tc_pt3Idx.size()) ? ev.tc_pt3Idx[it] : -999;
            if (p3 < 0 || p3 >= static_cast<int>(ev.pT3_otHitIndices.size()))
              continue;
            pixOwnerHits.push_back(ev.pT3_otHitIndices[p3]);
          }
        }
        // M16b: NO attached chains are appended here any more. Under the fixed staging an
        // attached chain is a K9 CLAIM WINNER, so it claims its own outer-tracker hits
        // inside the greedy walk -- the pre-claimed volume the suppressed carried rows gave
        // up is conserved one stage later, by the claim itself. Appending them here would
        // now double-count (and could not be done at all: attach has not run yet).
        ap.preClaimOwners = &pixOwnerHits;
        ap.preClaimMode = preClaimMode;
        ap.stats = &pixClaimStats;
        totPixOwners += static_cast<long long>(pixOwnerHits.size());
      }

      std::vector<int> accepted;
      std::size_t nPass1 = 0;
      std::vector<int> k9Owner;  // EXPLOIT extension: final claim map (empty = not asked)
      if (attachMode == 2) {
        std::vector<int> acceptedP2;
        k9ArbitrateTwoPass(ev, chains, ap, chainAttachPls, accepted, acceptedP2);
        nPass1 = accepted.size();
        accepted.insert(accepted.end(), acceptedP2.begin(), acceptedP2.end());
      } else {
        k9Arbitrate(ev, chains, ap, accepted, attachMode == 1 ? &attachBypass : nullptr,
                    extendMode >= 0.5f ? &k9Owner : nullptr);
        nPass1 = accepted.size();
      }
      // M14 lever 4 (-Q4/-Q5): POST-CLAIM absolute mX floors on the IP-compatible branches.
      // Applied AFTER k9Arbitrate, so the removed chains' hits stay claimed and no
      // runner-up backfills (the M9 claim-conservation escape hatch). Order-preserving
      // erase keeps the accepted/chainTCs lockstep K10 and the -A 2 pass-1/pass-2 split
      // rely on. No-op unless -G 6 AND a floor was passed, so all pre-M14 runs are exact.
      if (!m3mX.empty() && (q3Floor5 > -1e9f || q3Floor4 > -1e9f)) {
        std::size_t w = 0, nP1 = 0;
        for (std::size_t ai = 0; ai < accepted.size(); ++ai) {
          const int c = accepted[ai];
          const int nL = chains.nLayers[c];
          const bool ipT4 = nL <= 4 && m3dcaAll[c] < std::max(dcaSplit, t4ExemptDcaMin);
          const bool ip5 = nL >= 5 && m3dcaAll[c] < dcaSplit;
          if ((ip5 && m3mX[c] < q3Floor5) || (ipT4 && m3mX[c] < q3Floor4))
            continue;
          if (ai < nPass1)
            ++nP1;
          accepted[w++] = c;
        }
        nPostClaimKilled += static_cast<long long>(accepted.size() - w);
        accepted.resize(w);
        nPass1 = nP1;
      }

      // ---- M17 claimshare: POST-ARBITRATION STRUCTURAL DEDUP (-DD/-DDF/-DDP/-DDK) -----
      // A looser -F re-admits claim-starved chains, and the TCs it buys are two populations
      // at once: chains that match a sim NOTHING ELSE matched (efficiency) and EXTRA COPIES
      // of sims that are already matched (that second set is, by definition, what the dup
      // rate counts). This pass removes the second population structurally: walking the
      // accepted list best-first, a candidate sharing >= floor(-DD) outer-tracker HIT ROWS
      // with an already-KEPT accepted chain is dropped from the TC output. It frees nothing
      // -- the claim map is final, no runner-up backfills -- so the only effect is fewer
      // rows. Dropping a copy cannot un-match a sim as long as the kept representative
      // matches it too, which is why the pass is expected to be eff-neutral.
      // JET SAFETY: the predicate reads HIT INDICES ONLY (the same rows K9 claims). No
      // dR / dEta / dPhi / embedding proximity enters anywhere, so two tracks that are
      // angularly adjacent but hit-disjoint can never interact.
      if (dedupMinShared >= 1.f && !accepted.empty()) {
        const int kShare = static_cast<int>(std::floor(dedupMinShared));
        const float fShare = dedupShareFrac;
        const int nAcc = static_cast<int>(accepted.size());
        // Visit order: default = the accepted (K9 best-first) order; -DDK 1 = longest
        // chain first, K9 order breaking ties (keep the most-hit representative).
        std::vector<int> visit(nAcc);
        for (int i = 0; i < nAcc; ++i)
          visit[i] = i;
        if (dedupKeyMode >= 0.5f) {
          std::stable_sort(visit.begin(), visit.end(), [&](int a, int b) {
            return chains.nLayers[accepted[a]] > chains.nLayers[accepted[b]];
          });
        }
        // Deduped OT hit rows per accepted entry -- identical construction to the K9
        // -H 1 claim universe (anchor + other hit of every member MD, sorted unique).
        std::vector<std::vector<int>> accHits(nAcc);
        for (int i = 0; i < nAcc; ++i) {
          const int c = accepted[i];
          if (chains.nLayers[c] < 4)
            continue;  // K10 emits no TC for these; they neither kill nor die
          std::vector<int>& h = accHits[i];
          h.reserve(2 * static_cast<std::size_t>(chains.mdOffsets[c + 1] - chains.mdOffsets[c]));
          for (int k = chains.mdOffsets[c]; k < chains.mdOffsets[c + 1]; ++k) {
            const int md = chains.mdItems[k];
            h.push_back(ev.md_anchorHitIdx[md]);
            h.push_back(ev.md_otherHitIdx[md]);
          }
          std::sort(h.begin(), h.end());
          h.erase(std::unique(h.begin(), h.end()), h.end());
        }
        // Kept-entry registry. Entries 0..nPixKeep-1 are the -DDP pixel owners (un-killable,
        // they are carried rows); chain entries follow.
        const int nPixKeep = (dedupPix >= 0.5f) ? static_cast<int>(pixOwnerHits.size()) : 0;
        std::vector<int> keptSize;
        keptSize.reserve(static_cast<std::size_t>(nPixKeep) + accepted.size());
        std::unordered_map<int, std::vector<int>> hitOwners;
        hitOwners.reserve(static_cast<std::size_t>(16) * accepted.size());
        for (int p = 0; p < nPixKeep; ++p) {
          std::vector<int> h = pixOwnerHits[p];
          std::sort(h.begin(), h.end());
          h.erase(std::unique(h.begin(), h.end()), h.end());
          const int slot = static_cast<int>(keptSize.size());
          keptSize.push_back(static_cast<int>(h.size()));
          for (int x : h)
            hitOwners[x].push_back(slot);
        }
        std::vector<int> cnt(static_cast<std::size_t>(nPixKeep) + accepted.size(), 0);
        std::vector<int> touched;
        std::vector<char> killed(nAcc, 0);
        long long nKilledHere = 0;
        for (int vi : visit) {
          const std::vector<int>& h = accHits[vi];
          if (h.empty())
            continue;
          touched.clear();
          for (int x : h) {
            const auto it = hitOwners.find(x);
            if (it == hitOwners.end())
              continue;
            for (int slot : it->second) {
              if (cnt[slot] == 0)
                touched.push_back(slot);
              ++cnt[slot];
            }
          }
          bool drop = false;
          for (int slot : touched) {
            const int nSh = cnt[slot];
            cnt[slot] = 0;
            if (drop || nSh < kShare)
              continue;
            if (fShare > 0.f) {
              const int shorter = std::min(static_cast<int>(h.size()), keptSize[slot]);
              if (static_cast<float>(nSh) < fShare * static_cast<float>(shorter))
                continue;
            }
            drop = true;
          }
          if (drop) {
            killed[vi] = 1;
            ++nKilledHere;
            continue;
          }
          const int slot = static_cast<int>(keptSize.size());
          keptSize.push_back(static_cast<int>(h.size()));
          for (int x : h)
            hitOwners[x].push_back(slot);
        }
        nDedupKilled += nKilledHere;
        if (nKilledHere > 0) {
          std::size_t w = 0, nP1 = 0;
          for (int i = 0; i < nAcc; ++i) {
            if (killed[i])
              continue;
            if (static_cast<std::size_t>(i) < nPass1)
              ++nP1;
            accepted[w++] = accepted[i];
          }
          accepted.resize(w);
          nPass1 = nP1;
        }
      }

      // ---- M16b (4) STAGE A: attach OFFERED TO THE K9 CLAIM WINNERS ------------------
      // The claim has already decided mutual hit contention among the chains, so the
      // targets here are exactly the chains that will emit a TC anyway. A granted pLS
      // upgrades that TC in place (type 4 -> 7, pixel hits prepended) in the assembly loop
      // below; a chain with no grant delivers bare. Nothing is added, nothing is skipped:
      // the chain slice is the -A 0 slice, re-labelled where the pixel evidence exists.
      // Eligibility: nLayers >= 5 (k8 bids only 5+-layer targets) and dcaXY < -D4. -D4
      // defaults to 1e9 = OFF, because plan 11 makes the low-vxy displaced-with-pixel-seed
      // population a structural upside to CLAIM, not a dilution to guard against; it stays
      // available as an A/B lever.
      if (attachMode == 4) {
        for (int c : accepted) {
          if (chains.nLayers[c] < 5)
            continue;
          if (chainDca(c) >= dcaAttach4) {
            ++nGaDcaBlocked;
            continue;
          }
          thetaPass.push_back(c);
        }
        const auto ta0 = std::chrono::steady_clock::now();
        gaStageChains(ev, chains, thetaPass, cfHyb, gateLogit, gap, ga);
        attachMs = msBetween(ta0, std::chrono::steady_clock::now());
        // (1b) SEED-FAMILY DEDUP OF THE ATTACH OWNERS (-RD), stage A. One track
        // reconstructed with N duplicate pixel seeds offers N distinct pLS; without this
        // N accepted chains would each be upgraded and the track delivered N times as a
        // pT5-class TC. A revoked chain is NOT removed -- it is an accepted claim winner
        // either way -- it simply delivers bare.
        if (seedDupClean >= 0.5f) {
          std::vector<int> owners;
          for (int c = 0; c < static_cast<int>(ga.chainPls.size()); ++c)
            if (ga.chainPls[c] >= 0)
              owners.push_back(c);
          // P2.5 stable tie-break (production ChainsSoA.h names the attach -RD dedup
          // order as a stableKey consumer): logit desc, chain stableKey asc, index asc.
          const bool haveSkRd = (chains.stableKey.size() == chains.score.size());
          std::sort(owners.begin(), owners.end(), [&ga, &chains, haveSkRd](int a, int b) {
            if (ga.chainLogit[a] != ga.chainLogit[b])
              return ga.chainLogit[a] > ga.chainLogit[b];
            if (haveSkRd && chains.stableKey[a] != chains.stableKey[b])
              return chains.stableKey[a] < chains.stableKey[b];
            return a < b;
          });
          for (int c : owners) {
            const int p = ga.chainPls[c];
            if (seedFamilyDup(p)) {
              ga.chainPls[c] = -1;
              ga.plsOwned[p] = 0;
              ++nSeedDedup;
              continue;
            }
            seedFamilyKeep(p);
          }
          ga.nChainAttached -= nSeedDedup;
        }
        chainAttachPls = ga.chainPls;
        m16RefreshSupp();
      }

      // ---- EXPLOIT: CHAIN EXTENSION AT ASSEMBLY (-EX, Extend.h) ----------------------
      // Runs POST-claim, POST-attach-stage, PRE-K10: it can only change the hit list /
      // nhitOT / nLayers of chains that were already going to emit a TC. Candidates are
      // MDs whose BOTH hits are unclaimed by any DELIVERED object, so an extension can
      // never manufacture an overlap with another TC.
      //
      // The claimed-hit map is assembled from three sources, all supersets of "owned":
      //   (a) the final K9 owner map (hit-indexed at -H 1, MD-indexed otherwise);
      //   (b) every chain still in `accepted` (covers the -A 2 two-pass path, where K9
      //       does not export a map, and the pass-2 acceptances);
      //   (c) the OT hit rows of every SURVIVING carried pixel TC (type 7 / type 5) --
      //       independent of -PU, because those rows are delivered whether or not they
      //       pre-claimed.
      // Being a superset is the conservative direction: it can only forbid extensions.
      if (extendMode >= 0.5f) {
        const auto tx0 = std::chrono::steady_clock::now();
        int maxHit = -1;
        for (std::size_t m = 0; m < ev.md_anchorHitIdx.size(); ++m)
          maxHit = std::max(maxHit, std::max(ev.md_anchorHitIdx[m], ev.md_otherHitIdx[m]));
        for (const std::vector<int>& v : ev.t5_hitIndices)
          for (int h : v)
            maxHit = std::max(maxHit, h);
        for (const std::vector<int>& v : ev.pT3_otHitIndices)
          for (int h : v)
            maxHit = std::max(maxHit, h);
        std::vector<char> claimedHit(static_cast<std::size_t>(maxHit) + 1, 0);
        auto markHit = [&](int h) {
          if (h >= 0 && h <= maxHit)
            claimedHit[h] = 1;
        };
        // (a) K9 owner map.
        if (!k9Owner.empty()) {
          if (ap.hitLevelClaim) {
            const int n = std::min(static_cast<int>(k9Owner.size()), maxHit + 1);
            for (int h = 0; h < n; ++h)
              if (k9Owner[h] != -1)
                claimedHit[h] = 1;
          } else {
            const int n = std::min(static_cast<int>(k9Owner.size()),
                                   static_cast<int>(ev.md_anchorHitIdx.size()));
            for (int m = 0; m < n; ++m)
              if (k9Owner[m] != -1) {
                markHit(ev.md_anchorHitIdx[m]);
                markHit(ev.md_otherHitIdx[m]);
              }
          }
        }
        // (b) every accepted chain.
        for (int c : accepted)
          for (int k = chains.mdOffsets[c]; k < chains.mdOffsets[c + 1]; ++k) {
            markHit(ev.md_anchorHitIdx[chains.mdItems[k]]);
            markHit(ev.md_otherHitIdx[chains.mdItems[k]]);
          }
        // (c) surviving carried pixel TCs' outer-tracker hits.
        for (std::size_t it = 0; it < ev.tc_type.size(); ++it) {
          if (!m16RowSuppressed.empty() && m16RowSuppressed[it])
            continue;
          const int ty = ev.tc_type[it];
          if (ty == 7) {
            const int p5 = (it < ev.tc_pt5Idx.size()) ? ev.tc_pt5Idx[it] : -999;
            if (p5 < 0 || p5 >= static_cast<int>(ev.pT5_t5Idx.size()))
              continue;
            const int t5 = ev.pT5_t5Idx[p5];
            if (t5 < 0 || t5 >= static_cast<int>(ev.t5_hitIndices.size()))
              continue;
            for (int h : ev.t5_hitIndices[t5])
              markHit(h);
          } else if (ty == 5) {
            const int p3 = (it < ev.tc_pt3Idx.size()) ? ev.tc_pt3Idx[it] : -999;
            if (p3 < 0 || p3 >= static_cast<int>(ev.pT3_otHitIndices.size()))
              continue;
            for (int h : ev.pT3_otHitIndices[p3])
              markHit(h);
          }
        }
        ExtendParams exp;
        exp.mode = static_cast<int>(extendMode + 0.5f);
        exp.window = extendWindow;
        exp.rzWindow = extendRz;
        exp.chi2Factor = extendChi2F;
        exp.uniqMargin = extendUniq;
        exp.maxDist = extendMaxD;
        exp.maxJump = std::max(1, static_cast<int>(extendJump + 0.5f));
        exp.maxPerEnd = std::max(1, static_cast<int>(extendPerEnd + 0.5f));
        exp.minLayers = std::max(4, static_cast<int>(extendMinLay + 0.5f));
        exp.segLinked = static_cast<int>(extendSeg + 0.5f);
        exp.maxChi2 = extendMaxChi2;
        extendChains(ev, accepted, exp, claimedHit, chains, exStats);
        extendMs += msBetween(tx0, std::chrono::steady_clock::now());
      }

      std::vector<ChainTC> chainTCs;
      k10AssembleChainTCs(ev, chains, accepted, chainTCs);
      const auto t3 = std::chrono::steady_clock::now();

      // M7c dca study dump (PROTO_DCA_DUMP): pass-1-accepted chains only -- at
      // -a 999 (nothing attaches) that set is exactly the h4b delivery set.
      if (dcaDump != nullptr) {
        T3SimSets t3simsDca;
        buildT3SimSets(ev, t3simsDca);
        ChainLabels clDca;
        labelChains(ev, chains, t3simsDca, clDca);
        for (std::size_t ai = 0; ai < nPass1; ++ai) {
          const int c = accepted[ai];
          std::fprintf(dcaDump, "%d %.5f %d %.3f %.3f\n", chains.nLayers[c], chainDca(c),
                       static_cast<int>(clDca.label[c]), clDca.simVxy[c], clDca.simPt[c]);
        }
      }

      // Funnel counts for the report: replicate K9's two pre-claim gates (theta gate,
      // then pixel-consumed drop) so the per-stage attrition is visible per event.
      const long long nChainsIn = chains.offsets.empty() ? 0 : static_cast<long long>(chains.offsets.size()) - 1;
      const int nT3 = static_cast<int>(ev.t3_lsIdx0.size());
      const bool havePixFlags = static_cast<int>(ev.t3_partOfPT5.size()) == nT3 &&
                                static_cast<int>(ev.t3_partOfPT3.size()) == nT3;
      long long nAfterTheta = 0, nAfterPixDrop = 0, nPass2Cand = 0;
      for (long long c = 0; c < nChainsIn; ++c) {
        // Mirror of any claim exclusion (M16b leaves this unused at -A 4: every chain
        // walks the claim now, so the funnel counts them all).
        if (ap.excludeChain != nullptr && (*ap.excludeChain)[c] != 0)
          continue;
        if (chains.score[c] < ap.thetaForChain(static_cast<int>(c), chains.nLayers[c]))
          continue;
        ++nAfterTheta;
        bool hasPT5 = false, hasPT3 = false;
        if (dropPixelConsumed && havePixFlags) {
          for (int k = chains.offsets[c]; k < chains.offsets[c + 1] && !hasPT3; ++k) {
            const int t3n = chains.items[k];
            hasPT5 = hasPT5 || ev.t3_partOfPT5[t3n];
            hasPT3 = ev.t3_partOfPT3[t3n];
          }
        }
        // Mirror of the K9 attach bypass (-A 1 ONLY): attached chains skip the partOfPT5
        // half of the drop but still respect partOfPT3. -A 2 pass 1 has NO bypass.
        // M16: -RT5/-RT3 switch the corresponding half off entirely (ap.dropPartOf*).
        const bool bypassPT5 = (attachMode == 1 && attachBypass[c] != 0) || !ap.dropPartOfPT5;
        const bool dropPT3 = hasPT3 && ap.dropPartOfPT3;
        if (!((hasPT5 && !bypassPT5) || dropPT3))
          ++nAfterPixDrop;
        else if (attachMode == 2 && chainAttachPls[c] >= 0 && hasPT5 && !hasPT3)
          ++nPass2Cand;  // mirror of the k9ArbitrateTwoPass pass-2 candidate predicate
      }
      const long long nAfterClaim = static_cast<long long>(nPass1);
      const long long nPass2Acc = static_cast<long long>(accepted.size() - nPass1);

      long long nT5c = 0, nT4c = 0;
      for (const ChainTC& ctc : chainTCs)
        (ctc.type == 4 ? nT5c : nT4c) += 1;

      long long nPixKept = 0;
      for (int t : ev.tc_type)
        if (t == 7 || t == 5 || t == 8)
          ++nPixKept;

      // ChainTC -> OutTC: bare chains are pure outer-tracker objects, every hit a ph2
      // row. With -A 1, a chain with a K8 attachment becomes a type-7 (pT5-class) TC:
      //   hit list = the pLS's PIXEL hits (pix rows via pLS_seedIdx -> trk.see_hitIdx,
      //   entries with see_hitType == Pixel) followed by the chain's OT hits;
      //   nhitOT stays the OT count; pt = the pLS ptIn (the pixel-seed pt is better
      //   measured than the chain's median T3 pt -- documented v1 choice); eta/phi stay
      //   the chain's. Its pLS's baseline pT5/pLS rows are dropped in fillEventHybrid.
      // The chainTCs/accepted lockstep below relies on the K10 contract (accepted order,
      // skipping nLayers < 4).
      // (plsPixelHits is hoisted above, next to the M16 suppression/owner-list block.)
      // M16b: -A 4 uses THIS SAME BRANCH -- the general-attach pT5-class delivery IS the
      // in-place upgrade of an accepted chain (that is the whole fix). The only -A 4
      // addition is the `deliv` provenance mark so compare_types can separate carried pT5
      // rows from attach deliveries. The resulting OutTC is byte-for-byte what the retired
      // standalone -A 4 delivery block emitted (same eta/phi from the innermost T3, same
      // pt = pLS ptIn, same pixel-then-OT hit list, same nhitOT).
      std::vector<OutTC> outTCs;
      outTCs.reserve(chainTCs.size());
      std::vector<int> outTCChain;  // per outTC: source chain index (K7-lite dca lookup)
      outTCChain.reserve(chainTCs.size());
      std::vector<char> plsSuppressed;
      // M7c (b): per-pLS kinematics of the attaching chain+pLS TC (valid where
      // plsSuppressed != 0; family members inherit their anchor's reference).
      std::vector<float> attachRefPt, attachRefEta, attachRefPhi;
      long long nAttached = 0, nUpgraded = 0;
      long long nDelivT5 = 0, nDelivT3 = 0, nBareT3Targets = 0;
      long long nCCDropped = 0;  // M20 -CC: pT3-class deliveries revoked by hit overlap
      double attachT3Ms = 0.0;
      if (attachMode) {
        plsSuppressed.assign(ev.pLS_pt.size(), 0);
        attachRefPt.assign(ev.pLS_pt.size(), 0.f);
        attachRefEta.assign(ev.pLS_pt.size(), 0.f);
        attachRefPhi.assign(ev.pLS_pt.size(), 0.f);
      }
      {
        std::vector<int> pixHits;
        std::size_t tcPos = 0;
        for (std::size_t ai = 0; ai < accepted.size(); ++ai) {
          const int c = accepted[ai];
          if (chains.nLayers[c] < 4)
            continue;  // K10 dropped it; keep the lockstep aligned
          ChainTC& ctc = chainTCs[tcPos++];
          OutTC otc;
          otc.pt = ctc.pt;
          otc.eta = ctc.eta;
          otc.phi = ctc.phi;
          otc.type = ctc.type;
          otc.nhitOT = ctc.nhitOT;
          // FANOUT4 transition diagnostics (harness-invisible extra branches).
          otc.dbgNL = chains.nLayers[c];
          otc.dbgNNodes = chains.offsets[c + 1] - chains.offsets[c];
          otc.dbgNMD = chains.mdOffsets[c + 1] - chains.mdOffsets[c];
          {
            int nB = 0, nPS = 0;
            for (int mk = chains.mdOffsets[c]; mk < chains.mdOffsets[c + 1]; ++mk) {
              const int m = chains.mdItems[mk];
              if (ev.md_layer[m] <= 6)
                ++nB;
              if (ev.md_type[m] == 1)
                ++nPS;
            }
            otc.dbgNB = nB;
            otc.dbgNPS = nPS;
            otc.dbgInLay = chains.mdOffsets[c + 1] > chains.mdOffsets[c] ? ev.md_layer[chains.mdItems[chains.mdOffsets[c]]] : 0;
          }
          if (!dbgBr.empty()) {
            otc.dbgBranch = dbgBr[c];
            otc.dbgMP = dbgMP[c];
            otc.dbgMDm = dbgMD[c];
          }
          if (!m3dcaAll.empty())
            otc.dbgDca = m3dcaAll[c];
          const int p = attachMode ? chainAttachPls[c] : -1;
          if (p >= 0) {
            ++nAttached;
            if (attachMode == 4 || (attachMode == 2 && ai < nPass1)) {
              ++nUpgraded;  // accepted chain upgraded in place (no claim change)
              if (attachMode == 4) {
                otc.deliv = kDelivAttachT5;  // M16 general-attach pT5-class delivery
                ++nDelivT5;
              }
            }
            plsSuppressed[p] = 1;
            otc.type = 7;           // pT5-class: chain + attached pLS
            otc.pt = ev.pLS_pt[p];  // pLS ptIn (pixel pt is better measured)
            attachRefPt[p] = otc.pt;
            attachRefEta[p] = otc.eta;
            attachRefPhi[p] = otc.phi;
            plsPixelHits(p, pixHits);
            for (int hi : pixHits) {
              otc.hitIdxs.push_back(static_cast<unsigned int>(hi));
              otc.hitTypes.push_back(proto::HitType::Pixel);
            }
          }
          for (unsigned int hi : ctc.hitIdxs) {
            otc.hitIdxs.push_back(hi);
            otc.hitTypes.push_back(proto::HitType::Phase2OT);
          }
          outTCs.push_back(std::move(otc));
          outTCChain.push_back(c);
        }
      }

      // ================== M16 (-A 4) DELIVERY: steps (5) and (6b) =====================
      // (6a) -- the type-7 (pT5-class) deliveries -- is now the in-place upgrade in the
      // assembly loop above (M16b); only stage B and its type-5 deliveries live here.
      if (attachMode == 4) {
        std::vector<int> pixHits;

        // (5) STAGE B + (6b) type-5 (pT3-class) deliveries. Tied to -RT3 by default:
        // without the carried type-5 rows being replaced there is nothing to measure and
        // the extra TCs would be pure duplication of a class the baseline still
        // delivers. -T3E overrides the coupling in either direction (diagnostic).
        if (doT3Stage) {
          const auto tb0 = std::chrono::steady_clock::now();
          gaStageT3(ev, chains, accepted, cfHyb, gateLogit, gap, ga);
          attachT3Ms = msBetween(tb0, std::chrono::steady_clock::now());
          // (5b) SEED-FAMILY DEDUP, stage B. Without it a track with N duplicate pixel
          // seeds gets N pT3-class deliveries (measured DR 0.82-0.93 before this) --
          // and, because the map carries stage A's kept seeds, a track already
          // delivered as a pT5 cannot ALSO be delivered as a pT3 by a sibling seed.
          // -RDT: the PIXEL-SIDE half of the pT3-class dedup, split from -RD so the
          // campaign can run OT-only (-RDT 0) against pixel-assisted (-RDT 1).
          const bool doRdT3 = (rdT3 < -0.5f) ? (seedDupClean >= 0.5f) : (rdT3 >= 0.5f);
          if (doRdT3) {
            std::vector<int> t3Owners;
            for (int t = 0; t < static_cast<int>(ga.t3Pls.size()); ++t)
              if (ga.t3Pls[t] >= 0)
                t3Owners.push_back(t);
            std::sort(t3Owners.begin(), t3Owners.end(), [&ga](int a, int b) {
              if (ga.t3Logit[a] != ga.t3Logit[b])
                return ga.t3Logit[a] > ga.t3Logit[b];
              return a < b;
            });
            for (int t : t3Owners) {
              const int p = ga.t3Pls[t];
              if (seedFamilyDup(p)) {
                ga.t3Pls[t] = -1;
                ga.plsOwned[p] = 0;
                ++nSeedDedupT3;
                continue;
              }
              seedFamilyKeep(p);
            }
            ga.nT3Attached -= nSeedDedupT3;
          }
          // ---- (6c) M20: pT3-CLASS HIT-OVERLAP CONTENTION (-CC), the CrossCleanpT3
          // analogue. --------------------------------------------------------------
          // Bare-T3 deliveries are decided AFTER the K9 claim, so NOTHING has ever made
          // them compete for hits: not with the chain TCs already assembled, and not with
          // each other. Measured consequence (sibling production recon, 300 evts): 472
          // delivered rows/evt against LST's ~150, 82% of them for a sim that another TC
          // already delivers, and a 5.5x duplicate rate. The pair head cannot repair that
          // -- it ranks pair COMPATIBILITY, and a duplicate of a real track is a perfectly
          // compatible pair -- and neither can a K9-claim-overlap veto, which was measured
          // to remove duplicates and signal together. The missing stage is a hit-overlap
          // crossclean, exactly the role LST's CrossCleanpT3 plays.
          //
          // Mechanism = the two shapes this codebase already uses:
          //   (i)  an OWNERSHIP MAP pre-loaded from everything already delivered (the
          //        K9 / Extend claim shape) -- gated by -CCP;
          //   (ii) a greedy BEST-FIRST sweep over the deliveries (the -RD attach dedup
          //        shape): a delivery that finds >= -CCN of its own units already in the
          //        map is dropped, otherwise it claims them.
          //
          // TWO HARD CONSTRAINTS, both maintainer requirements, both structural to this
          // implementation rather than parameters of it:
          //   * OWNERSHIP-MAP BASED, NEVER PAIRWISE. Every candidate looks up ITS OWN
          //     units in one map and decides alone. Cost is linear in the candidate's
          //     unit count (3 MDs, or 6 hits), never quadratic in candidates. This is
          //     exactly why the claim does not explode in a jet core, where LST's
          //     pairwise CrossClean loops do -- it is a timing requirement as much as a
          //     physics one. There is no candidate-vs-candidate comparison anywhere below.
          //   * NO PROXIMITY CRITERIA. LST's own CrossCleanpT3 kills a pT3 whose pixel
          //     direction is within dR^2 < 1e-5 of a pT5's. That shape is NOT copied.
          //     Shared structure only.
          //
          // GRANULARITY (-CCG), the lesson from the sibling recon's failed attempt: it
          // reused the CHAIN claim budget (<= 2 hits, <= 20% fraction), which is tuned for
          // 10-14-hit objects, and killed signal with the duplicates (unique pT3-only sims
          // recovered collapsed 241/265 -> 15/218). A 6-hit / 3-MD T3 shares MDs with
          // other T3s by the nature of the graph, so the right unit is the MD and the
          // right rule is COUNTING: >= 2 shared MDs of 3 == the same track (kill), exactly
          // 1 shared MD == two tracks crossing (keep both). -CCG 1 -CCN 2 is that rule and
          // is the default; -CCG 0 counts OT hit rows instead, for the A/B.
          std::vector<int> t3Deliv;
          for (int t = 0; t < static_cast<int>(ga.t3Pls.size()); ++t)
            if (ga.t3Pls[t] >= 0)
              t3Deliv.push_back(t);

          const bool ccMd = (ccGran >= 0.5f);
          const int ccNeed = std::max(1, static_cast<int>(ccMinShared + 0.5f));
          std::vector<char> ccClaimed;  // the ownership map: MD rows, or ph2 hit rows
          int ccMaxUnit = -1;
          if (ccMode >= 0.5f) {
            if (ccMd) {
              ccMaxUnit = static_cast<int>(ev.md_anchorHitIdx.size()) - 1;
            } else {
              for (std::size_t m = 0; m < ev.md_anchorHitIdx.size(); ++m)
                ccMaxUnit = std::max(ccMaxUnit, std::max(ev.md_anchorHitIdx[m], ev.md_otherHitIdx[m]));
              for (const std::vector<int>& v : ev.t5_hitIndices)
                for (int h : v)
                  ccMaxUnit = std::max(ccMaxUnit, h);
              for (const std::vector<int>& v : ev.pT3_otHitIndices)
                for (int h : v)
                  ccMaxUnit = std::max(ccMaxUnit, h);
              for (const OutTC& o : outTCs)
                for (std::size_t h = 0; h < o.hitIdxs.size() && h < o.hitTypes.size(); ++h)
                  if (o.hitTypes[h] == proto::HitType::Phase2OT)
                    ccMaxUnit = std::max(ccMaxUnit, static_cast<int>(o.hitIdxs[h]));
            }
            ccClaimed.assign(static_cast<std::size_t>(std::max(ccMaxUnit, -1)) + 1, 0);
            auto ccMark = [&](int u) {
              if (u >= 0 && u <= ccMaxUnit)
                ccClaimed[u] = 1;
            };
            // -CCP 1: everything ALREADY DELIVERED writes its units into the map -- the
            // assembled chain TCs (bare and in-place-upgraded type-7 alike) and the
            // carried pixel rows that survived the M16 suppression. A pT3-class delivery
            // sitting on top of one of those is the duplicate we are removing.
            if (ccPreclaim >= 0.5f) {
              if (ccMd) {
                // MD granularity: the chain TCs' MD lists are the chains' own dedup MD
                // CSR (outTCChain maps an emitted TC back to its chain row).
                for (std::size_t j = 0; j < outTCs.size() && j < outTCChain.size(); ++j) {
                  const int c = outTCChain[j];
                  if (c < 0 || c + 1 >= static_cast<int>(chains.mdOffsets.size()))
                    continue;
                  for (int k = chains.mdOffsets[c]; k < chains.mdOffsets[c + 1]; ++k)
                    ccMark(chains.mdItems[k]);
                }
                // Carried pixel rows do not expose an MD list, only hit rows, so their
                // contribution is routed through the hit->MD map built once below.
              } else {
                for (const OutTC& o : outTCs)
                  for (std::size_t h = 0; h < o.hitIdxs.size() && h < o.hitTypes.size(); ++h)
                    if (o.hitTypes[h] == proto::HitType::Phase2OT)
                      ccMark(static_cast<int>(o.hitIdxs[h]));
              }
              // Surviving carried pixel rows (type 7 / type 5) hold outer-tracker hits.
              // At MD granularity they are mapped onto MD rows through hit2md, which is
              // built ONCE per event -- still one map lookup per unit, never a pairwise
              // loop.
              if (!ccMd || true) {
                std::vector<int> hit2md;
                if (ccMd) {
                  int mh = -1;
                  for (std::size_t m = 0; m < ev.md_anchorHitIdx.size(); ++m)
                    mh = std::max(mh, std::max(ev.md_anchorHitIdx[m], ev.md_otherHitIdx[m]));
                  for (const std::vector<int>& v : ev.t5_hitIndices)
                    for (int h : v)
                      mh = std::max(mh, h);
                  for (const std::vector<int>& v : ev.pT3_otHitIndices)
                    for (int h : v)
                      mh = std::max(mh, h);
                  hit2md.assign(static_cast<std::size_t>(std::max(mh, -1)) + 1, -1);
                  for (int m = 0; m < static_cast<int>(ev.md_anchorHitIdx.size()); ++m) {
                    const int ha = ev.md_anchorHitIdx[m], hb = ev.md_otherHitIdx[m];
                    if (ha >= 0 && ha <= mh)
                      hit2md[ha] = m;
                    if (hb >= 0 && hb <= mh)
                      hit2md[hb] = m;
                  }
                }
                auto markCarriedHit = [&](int h) {
                  if (!ccMd) {
                    ccMark(h);
                    return;
                  }
                  if (h >= 0 && h < static_cast<int>(hit2md.size()) && hit2md[h] >= 0)
                    ccMark(hit2md[h]);
                };
                for (std::size_t it = 0; it < ev.tc_type.size(); ++it) {
                  if (!m16RowSuppressed.empty() && m16RowSuppressed[it])
                    continue;
                  const int ty = ev.tc_type[it];
                  if (ty == 7) {
                    const int p5 = (it < ev.tc_pt5Idx.size()) ? ev.tc_pt5Idx[it] : -999;
                    if (p5 < 0 || p5 >= static_cast<int>(ev.pT5_t5Idx.size()))
                      continue;
                    const int t5 = ev.pT5_t5Idx[p5];
                    if (t5 < 0 || t5 >= static_cast<int>(ev.t5_hitIndices.size()))
                      continue;
                    for (int h : ev.t5_hitIndices[t5])
                      markCarriedHit(h);
                  } else if (ty == 5) {
                    const int p3 = (it < ev.tc_pt3Idx.size()) ? ev.tc_pt3Idx[it] : -999;
                    if (p3 < 0 || p3 >= static_cast<int>(ev.pT3_otHitIndices.size()))
                      continue;
                    for (int h : ev.pT3_otHitIndices[p3])
                      markCarriedHit(h);
                  }
                }
              }
            }
            // KEEP-BEST ORDER (-CCK). 0 = attach logit desc (the pair head's own ranking),
            // 1 = pLS pt desc, 2 = t3 row ascending (order-free control). Ties always
            // break on the lower t3 row, so the sweep is deterministic.
            const int ccKey = static_cast<int>(ccOrder + 0.5f);
            std::sort(t3Deliv.begin(), t3Deliv.end(), [&](int a, int b) {
              if (ccKey == 1) {
                const float pa = ev.pLS_pt[ga.t3Pls[a]], pb = ev.pLS_pt[ga.t3Pls[b]];
                if (pa != pb)
                  return pa > pb;
              } else if (ccKey == 0) {
                if (ga.t3Logit[a] != ga.t3Logit[b])
                  return ga.t3Logit[a] > ga.t3Logit[b];
              }
              return a < b;
            });
          }

          std::vector<int> t3OtHits;
          std::vector<int> ccUnits;
          for (int t : t3Deliv) {
            ++nBareT3Targets;
            const int p = ga.t3Pls[t];
            const int mds[3] = {ev.t3_md0[t], ev.t3_md1[t], ev.t3_md2[t]};
            t3OtHits.clear();
            for (int md : mds) {
              if (md < 0 || md >= static_cast<int>(ev.md_anchorHitIdx.size()))
                continue;
              t3OtHits.push_back(ev.md_anchorHitIdx[md]);
              t3OtHits.push_back(ev.md_otherHitIdx[md]);
            }
            if (ccMode >= 0.5f) {
              ccUnits.clear();
              if (ccMd) {
                for (int md : mds)
                  if (md >= 0 && md <= ccMaxUnit)
                    ccUnits.push_back(md);
              } else {
                for (int h : t3OtHits)
                  if (h >= 0 && h <= ccMaxUnit)
                    ccUnits.push_back(h);
              }
              int nShared = 0;
              for (int u : ccUnits)
                nShared += ccClaimed[u] ? 1 : 0;
              if (!ccUnits.empty() && nShared >= ccNeed) {
                ++nCCDropped;
                ga.t3Pls[t] = -1;  // the delivery is revoked...
                if (ccRelPls >= 0.5f) {
                  // ...-CCR 1 (default, = what M9 shipped) hands the pLS back to the
                  // carried universe. MEASURED to be a no-op at -RPS 1: m16RefreshSupp()
                  // only ever ADDS suppressions, and the -RPS predicate also fires on
                  // plsBestT3Logit >= AT3, which AttachDelivery records for EVERY scored
                  // pair rather than only for owners. So the revoked seed disappears
                  // entirely -- no pT3-class row and no carried type-8 row either.
                  ga.plsOwned[p] = 0;
                  if (ccRelPls >= 1.5f)
                    // -CCR 2 is the release that actually releases: erase the T3 evidence
                    // too, so the -RPS predicate stops firing on this seed and the carried
                    // bare-pLS row survives. Costs duplicate rate if the revoked delivery
                    // really was a duplicate; buys efficiency if it was the seed's only row.
                    ga.plsBestT3Logit[p] = -std::numeric_limits<float>::infinity();
                }
                continue;
              }
              for (int u : ccUnits)
                ccClaimed[u] = 1;
            }
            OutTC otc;
            otc.type = 5;  // pT3-class
            otc.deliv = kDelivAttachT3;
            otc.pt = ev.pLS_pt[p];
            otc.eta = ev.t3_eta[t];
            otc.phi = ev.t3_phi[t];
            plsPixelHits(p, pixHits);
            for (int hi : pixHits) {
              otc.hitIdxs.push_back(static_cast<unsigned int>(hi));
              otc.hitTypes.push_back(proto::HitType::Pixel);
            }
            for (int hi : t3OtHits) {
              otc.hitIdxs.push_back(static_cast<unsigned int>(hi));
              otc.hitTypes.push_back(proto::HitType::Phase2OT);
            }
            otc.nhitOT = 6;
            outTCs.push_back(std::move(otc));
            outTCChain.push_back(-1);
            ++nDelivT3;
          }
          ga.nT3Attached -= nCCDropped;
          // Stage B can newly own pLS, which retires more carried rows (type-8 above all:
          // the -RPS predicate now sees the bare-T3 evidence too). Those rows DID
          // pre-claim -- they were still owners when K9 ran -- so the chains faced a
          // slightly STRICTER claim than the final output implies. Conservative and
          // stated; at real integration both universes resolve before the single claim.
          m16RefreshSupp();
        }
        attachMs += attachT3Ms;
      }

      // ============ ATTACH CONFUSION MATRIX -- per-event accumulation =================
      // Runs only under PROTO_ATTACH_CM and only at -A 4. Reads the pair log the stages
      // already wrote plus sim truth; writes nothing but its own counters, so the
      // delivered TCs and every scoreboard number are untouched.
      if (attachCM && attachMode == 4) {
        T3SimSets cmT3Sims;
        buildT3SimSets(ev, cmT3Sims);
        const int nPlsCM = static_cast<int>(ev.pLS_pt.size());
        std::vector<std::vector<int>> plsSimsCM(nPlsCM);
        std::unordered_map<int, std::vector<int>> simToPlsCM;
        for (int p = 0; p < nPlsCM; ++p) {
          std::vector<int> v = ev.pLS_simIdxAll[p];
          std::sort(v.begin(), v.end());
          v.erase(std::unique(v.begin(), v.end()), v.end());
          for (int s : v)
            simToPlsCM[s].push_back(p);
          plsSimsCM[p] = std::move(v);
        }
        // Chain sims = intersection over member T3 sim sets (the pairdump rule).
        auto chainSimsOf = [&](int c) {
          const int ib = chains.offsets[c], ie = chains.offsets[c + 1];
          if (ib >= ie)
            return std::vector<int>();
          std::vector<int> common = cmT3Sims.sims[chains.items[ib]], tmp;
          for (int k = ib + 1; k < ie && !common.empty(); ++k) {
            const auto& s = cmT3Sims.sims[chains.items[k]];
            tmp.clear();
            std::set_intersection(common.begin(), common.end(), s.begin(), s.end(), std::back_inserter(tmp));
            common.swap(tmp);
          }
          return common;
        };
        // Memoize per target: chain sims are an intersection over up to a dozen sets and
        // a chain appears once per prefiltered pLS.
        std::unordered_map<int, std::vector<int>> chainSimCache;
        auto tgtSimsOf = [&](int8_t ttype, int row) -> const std::vector<int>& {
          if (ttype == static_cast<int8_t>(kAttachTargetT3))
            return cmT3Sims.sims[row];
          auto it = chainSimCache.find(row);
          if (it == chainSimCache.end())
            it = chainSimCache.emplace(row, chainSimsOf(row)).first;
          return it->second;
        };
        auto intersects = [](const std::vector<int>& a, const std::vector<int>& b) {
          std::size_t i = 0, j = 0;
          while (i < a.size() && j < b.size()) {
            if (a[i] == b[j])
              return true;
            (a[i] < b[j]) ? ++i : ++j;
          }
          return false;
        };
        // ---- (a) the decisions actually taken ----------------------------------------
        std::unordered_map<int, std::pair<int8_t, int>> plsOwner;  // pLS -> (ttype, row)
        auto classifyDecision = [&](int8_t ttype, int row, int p) {
          plsOwner[p] = {ttype, row};
          const std::vector<int>& ts = tgtSimsOf(ttype, row);
          const std::vector<int>& ps = plsSimsCM[p];
          const bool tgtReal = !ts.empty(), plsReal = !ps.empty();
          const bool same = tgtReal && plsReal && intersects(ts, ps);
          if (same)
            ++cmAttSame;
          else if (tgtReal && plsReal)
            ++cmAttCross;
          else if (tgtReal)
            ++cmAttTgtFakePls;
          else if (plsReal)
            ++cmAttFakeTgtRealPls;
          else
            ++cmAttFakeBoth;
          if (ttype == static_cast<int8_t>(kAttachTargetT3))
            (same ? cmAttSameT3 : cmAttBadT3) += 1;
        };
        for (int c = 0; c < static_cast<int>(ga.chainPls.size()); ++c)
          if (ga.chainPls[c] >= 0)
            classifyDecision(static_cast<int8_t>(kAttachTargetChain), c, ga.chainPls[c]);
        for (int t = 0; t < static_cast<int>(ga.t3Pls.size()); ++t)
          if (ga.t3Pls[t] >= 0)
            classifyDecision(static_cast<int8_t>(kAttachTargetT3), t, ga.t3Pls[t]);
        // ---- (b) every scored pair: recall classes + the logit distribution ----------
        // Key a target as (ttype, row) so chain rows and T3 rows cannot collide.
        auto tkey = [](int8_t ty, int row) { return (static_cast<long long>(row) << 1) | ty; };
        std::unordered_map<long long, std::pair<float, char>> bestPerTgt;  // key -> (logit, isSame)
        for (const auto& pr : ga.pairLog) {
          if (pr.plsRow < 0 || pr.plsRow >= nPlsCM)
            continue;
          const bool isT3 = pr.ttype == static_cast<int8_t>(kAttachTargetT3);
          const std::vector<int>& ts = tgtSimsOf(pr.ttype, pr.tgtRow);
          const std::vector<int>& ps = plsSimsCM[pr.plsRow];
          const bool same = !ts.empty() && !ps.empty() && intersects(ts, ps);
          (same ? cmHistSame : cmHistBad)[cmBin(pr.logit)] += 1;
          const auto ins = bestPerTgt.emplace(tkey(pr.ttype, pr.tgtRow),
                                              std::make_pair(pr.logit, static_cast<char>(same)));
          if (!ins.second && pr.logit > ins.first->second.first)
            ins.first->second = {pr.logit, static_cast<char>(same)};
          if (!same)
            continue;
          ++cmTruePairPref;
          const int owned = isT3 ? (pr.tgtRow < static_cast<int>(ga.t3Pls.size()) ? ga.t3Pls[pr.tgtRow] : -1)
                                 : (pr.tgtRow < static_cast<int>(ga.chainPls.size()) ? ga.chainPls[pr.tgtRow] : -1);
          if (owned == pr.plsRow)
            continue;  // this true pair IS the attach -- already counted as cmAttSame
          const float margin = isT3 ? thetaAttachT3 : thetaAttach;
          if (pr.logit < margin)
            ++cmMissBelowTheta;  // HEAD failure: the true pair was scored too low
          else if (ga.plsOwned[pr.plsRow])
            ++cmMissContention;  // ARBITRATION: the pLS went to a different target
          else
            ++cmMissOther;  // the target itself picked a different (higher-logit) pLS
        }
        for (const auto& kv : bestPerTgt)
          (kv.second.second ? cmHistBestSame : cmHistBestBad)[cmBin(kv.second.first)] += 1;
        // ---- (b2) TARGET-LEVEL recall/precision (see the counter declarations) --------
        {
          // Which targets had a same-sim pLS anywhere in their prefiltered pairs, and
          // which pLS (if any) each target ended up owning.
          std::unordered_map<long long, char> tgtHadTrue;
          for (const auto& pr : ga.pairLog) {
            if (pr.plsRow < 0 || pr.plsRow >= nPlsCM)
              continue;
            const std::vector<int>& ts = tgtSimsOf(pr.ttype, pr.tgtRow);
            const std::vector<int>& ps = plsSimsCM[pr.plsRow];
            const bool same = !ts.empty() && !ps.empty() && intersects(ts, ps);
            auto& h = tgtHadTrue[tkey(pr.ttype, pr.tgtRow)];
            h = static_cast<char>(h | (same ? 1 : 0));
          }
          for (const auto& kv : tgtHadTrue) {
            const int8_t ty = static_cast<int8_t>(kv.first & 1);
            const int row = static_cast<int>(kv.first >> 1);
            const int owned = (ty == static_cast<int8_t>(kAttachTargetT3))
                                  ? (row < static_cast<int>(ga.t3Pls.size()) ? ga.t3Pls[row] : -1)
                                  : (row < static_cast<int>(ga.chainPls.size()) ? ga.chainPls[row] : -1);
            if (!kv.second) {
              ++cmTgtNoTrue;
              if (owned >= 0)
                ++cmTgtNoTrueAttached;
              continue;
            }
            ++cmTgtHadTrue;
            if (owned < 0) {
              ++cmTgtGotNone;
            } else if (intersects(tgtSimsOf(ty, row), plsSimsCM[owned])) {
              ++cmTgtGotRight;
            } else {
              ++cmTgtGotWrong;
            }
          }
        }
        // ---- (c) the achievable true-pair universe (prefilter recall denominator) -----
        // Every (bidding target, pLS) combination that SHARES a sim, whether or not the
        // prefilter windows kept it. cmTruePairPref / cmTruePairPoss is prefilter recall.
        {
          std::vector<int> cand;
          auto countPossible = [&](int8_t ttype, int row) {
            const std::vector<int>& ts = tgtSimsOf(ttype, row);
            if (ts.empty())
              return;
            cand.clear();
            for (int s : ts) {
              auto it = simToPlsCM.find(s);
              if (it != simToPlsCM.end())
                cand.insert(cand.end(), it->second.begin(), it->second.end());
            }
            std::sort(cand.begin(), cand.end());
            cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
            cmTruePairPoss += static_cast<long long>(cand.size());
          };
          for (int c : thetaPass)
            countPossible(static_cast<int8_t>(kAttachTargetChain), c);
          if (replT3 >= 0.5f) {
            std::vector<char> cmBare;
            k8BuildBareT3Mask(ev, chains, accepted, cmBare);
            for (int t = 0; t < static_cast<int>(cmBare.size()); ++t)
              if (cmBare[t])
                countPossible(static_cast<int8_t>(kAttachTargetT3), t);
          }
        }
        // ---- (d) what the contention rule retired, by pLS truth ----------------------
        if (!m16RowSuppressed.empty()) {
          for (std::size_t it = 0; it < ev.tc_type.size(); ++it) {
            if (!m16RowSuppressed[it] || ev.tc_type[it] != 8)
              continue;
            const int p = rowPls(it, 8);
            if (p < 0 || p >= nPlsCM)
              continue;
            if (plsSimsCM[p].empty()) {
              ++cmSuppFakePls;
              continue;
            }
            ++cmSuppRealPls;
            auto ow = plsOwner.find(p);
            if (ow != plsOwner.end() && intersects(tgtSimsOf(ow->second.first, ow->second.second), plsSimsCM[p]))
              ++cmSuppRealPlsSameSimOwner;
          }
        }
      }

      // M7b SEED-FAMILY suppression mask (-A 2 only; sim-blind, mirrors production
      // pixelHitsOverlapAny): expand the attached-pLS mark set to every pLS sharing
      // >= 2 pixel hit rows with any attached pLS. The attached pLS themselves are
      // family members trivially (full self-overlap), so this is a strict superset of
      // the v1 own-pLS mask. The writer then drops kept-baseline rows of ANY pixel
      // type (7, 5, AND 8) whose own pLS is in the family -- the M7 diagnosis (b):
      // own-row-only suppression left the same sims' OTHER pixel deliveries
      // (pLS-duplicate seeds, pT3 partners) alive, so the dup floor never drained.
      if (attachMode == 2) {
        std::unordered_map<int, std::vector<int>> hit2att;  // pixel hit row -> attached pLS
        std::vector<int> pixHits;
        const int nPls = static_cast<int>(ev.pLS_pt.size());
        for (int p = 0; p < nPls; ++p) {
          if (!plsSuppressed[p])
            continue;
          plsPixelHits(p, pixHits);
          for (int h : pixHits)
            hit2att[h].push_back(p);
        }
        if (!hit2att.empty()) {
          std::unordered_map<int, int> cnt;  // attached pLS -> shared-hit count with q
          for (int q = 0; q < nPls; ++q) {
            if (plsSuppressed[q])
              continue;
            plsPixelHits(q, pixHits);
            if (static_cast<int>(pixHits.size()) < 2)
              continue;
            cnt.clear();
            int anchorP = -1;  // the attached pLS whose shared-hit count reached 2 first
            for (int h : pixHits) {
              const auto it = hit2att.find(h);
              if (it == hit2att.end())
                continue;
              for (int p : it->second)
                if (++cnt[p] >= 2) {
                  anchorP = p;
                  break;
                }
              if (anchorP >= 0)
                break;
            }
            if (anchorP >= 0) {
              plsSuppressed[q] = 1;
              // M7c (b): family members inherit the anchor's attaching-TC kinematics
              // for the row-level suppression guard below.
              attachRefPt[q] = attachRefPt[anchorP];
              attachRefEta[q] = attachRefEta[anchorP];
              attachRefPhi[q] = attachRefPhi[anchorP];
            }
          }
        }
      }

      // M7c (b): resolve the pLS family mask to a per-ROW drop mask WITH the kinematic
      // suppression guard: a family row is actually dropped only if it is kinematically
      // compatible with the attaching chain+pLS TC (dR < suppDR AND pt ratio <
      // kKinPtRatioMax). Rows failing the test describe a DIFFERENT track sharing the
      // pixel seed (the M7b 193-lost-prompt-sims collateral) and survive.
      std::vector<char> rowSuppressed;
      long long nGuardKept = 0;  // family rows saved by the guard
      if (attachMode == 2) {
        rowSuppressed.assign(ev.tc_type.size(), 0);
        for (std::size_t in_idx = 0; in_idx < ev.tc_type.size(); ++in_idx) {
          const int type = ev.tc_type[in_idx];
          int pls = -1;
          if (type == 7 && in_idx < ev.tc_pt5Idx.size()) {
            const int i5 = ev.tc_pt5Idx[in_idx];
            if (i5 >= 0 && i5 < static_cast<int>(ev.pT5_plsIdx.size()))
              pls = ev.pT5_plsIdx[i5];
          } else if (type == 5 && in_idx < ev.tc_pt3Idx.size()) {
            const int i3 = ev.tc_pt3Idx[in_idx];
            if (i3 >= 0 && i3 < static_cast<int>(ev.pT3_plsIdx.size()))
              pls = ev.pT3_plsIdx[i3];
          } else if (type == 8 && in_idx < ev.tc_plsIdx.size()) {
            pls = ev.tc_plsIdx[in_idx];
          }
          if (pls < 0 || pls >= static_cast<int>(plsSuppressed.size()) || !plsSuppressed[pls])
            continue;
          const float dEta = ev.tc_eta[in_idx] - attachRefEta[pls];
          const float dPhi = wrapDPhi(ev.tc_phi[in_idx] - attachRefPhi[pls]);
          const float dR = std::sqrt(dEta * dEta + dPhi * dPhi);
          const float ptRow = ev.tc_pt[in_idx], ptRef = attachRefPt[pls];
          const float ptHi = std::max(ptRow, ptRef), ptLo = std::max(std::min(ptRow, ptRef), 1e-6f);
          if (dR < suppDR && ptHi < kKinPtRatioMax * ptLo)
            rowSuppressed[in_idx] = 1;
          else
            ++nGuardKept;
        }
      }

      // M7c (c): K7-LITE kinematic dedup. AFTER attach + suppression, drop bare
      // (non-attached, type != 7) chain TCs that are IP-compatible (same dca gate as
      // attach eligibility) AND kinematically match a KEPT baseline type-7 row
      // (dR < k7DR AND pt ratio < kKinPtRatioMax) -- redundant re-deliveries of
      // pixel-delivered tracks (the M7b dup-floor diagnosis: 92% of remaining dup
      // partners were pT5 rows). The dca gate keeps displaced chains untouchable.
      long long nK7Dropped = 0;
      if (attachMode == 2 && k7Lite) {
        std::vector<float> t7pt, t7eta, t7phi;
        for (std::size_t in_idx = 0; in_idx < ev.tc_type.size(); ++in_idx) {
          if (ev.tc_type[in_idx] != 7)
            continue;
          if (!rowSuppressed.empty() && rowSuppressed[in_idx])
            continue;  // suppressed rows are not "kept"
          t7pt.push_back(ev.tc_pt[in_idx]);
          t7eta.push_back(ev.tc_eta[in_idx]);
          t7phi.push_back(ev.tc_phi[in_idx]);
        }
        if (!t7pt.empty()) {
          std::vector<OutTC> keptTCs;
          keptTCs.reserve(outTCs.size());
          std::vector<int> keptChain;
          keptChain.reserve(outTCChain.size());
          for (std::size_t j = 0; j < outTCs.size(); ++j) {
            bool drop = false;
            if (outTCs[j].type != 7 && chainDca(outTCChain[j]) < dcaAttachMax) {
              const float pt = outTCs[j].pt, eta = outTCs[j].eta, phi = outTCs[j].phi;
              for (std::size_t r = 0; r < t7pt.size(); ++r) {
                const float dEta = eta - t7eta[r];
                if (std::fabs(dEta) >= k7DR)
                  continue;
                const float dPhi = wrapDPhi(phi - t7phi[r]);
                if (dEta * dEta + dPhi * dPhi >= k7DR * k7DR)
                  continue;
                const float ptHi = std::max(pt, t7pt[r]), ptLo = std::max(std::min(pt, t7pt[r]), 1e-6f);
                if (ptHi < kKinPtRatioMax * ptLo) {
                  drop = true;
                  break;
                }
              }
            }
            if (drop) {
              ++nK7Dropped;
            } else {
              keptTCs.push_back(std::move(outTCs[j]));
              keptChain.push_back(outTCChain[j]);
            }
          }
          outTCs.swap(keptTCs);
          outTCChain.swap(keptChain);
        }
      }

      // ---- EX_DUPCC (b): CARRIED-pT3 CROSSCLEAN (-XP3 / -XP3Z / -XP3L) ---------------
      // Under -RT5 1 the carried type-5 (pT3) rows are the ONLY carried rows that hold
      // outer-tracker hits, so chain-vs-carried-pT3 is the entire cross-class half of the
      // OT+OT duplicate cell. A carried pT3 row is retired when a SINGLE accepted chain
      // covers >= -XP3 of its 6 outer-tracker hit ROWS (>= -XP3L layers, |eta| >= -XP3Z).
      // Structural / shared-object: the predicate reads hit indices only -- the same rows
      // K9 claims -- so two angularly adjacent but hit-disjoint tracks never interact.
      // The pT3's pixel-side evidence is deliberately not consulted: the question is
      // whether the outer tracker already delivered this object inside a longer chain.
      if (attachMode == 4 && xp3Share >= 1.f && !m16RowSuppressed.empty() && !accepted.empty()) {
        const int kShare = static_cast<int>(std::floor(xp3Share));
        const int minLay = static_cast<int>(std::floor(xp3MinLay));
        std::unordered_map<int, std::vector<int>> hitOwn;  // ph2 hit row -> accepted slots
        hitOwn.reserve(static_cast<std::size_t>(16) * accepted.size());
        int nSlots = 0;
        std::vector<int> scratch;
        for (int c : accepted) {
          const int nL = chains.nLayers[c];
          if (nL < 4 || nL < minLay)
            continue;  // K10 emits no TC below 4 layers; below -XP3L it is not a trade up
          scratch.clear();
          for (int k = chains.mdOffsets[c]; k < chains.mdOffsets[c + 1]; ++k) {
            const int md = chains.mdItems[k];
            scratch.push_back(ev.md_anchorHitIdx[md]);
            scratch.push_back(ev.md_otherHitIdx[md]);
          }
          std::sort(scratch.begin(), scratch.end());
          scratch.erase(std::unique(scratch.begin(), scratch.end()), scratch.end());
          const int slot = nSlots++;
          for (int h : scratch)
            hitOwn[h].push_back(slot);
        }
        std::vector<int> cnt5(static_cast<std::size_t>(nSlots), 0);
        std::vector<int> touched5;
        for (std::size_t it = 0; it < ev.tc_type.size(); ++it) {
          if (m16RowSuppressed[it] || ev.tc_type[it] != 5)
            continue;
          if (it < ev.tc_eta.size() && std::fabs(ev.tc_eta[it]) < xp3Eta)
            continue;
          const int p3 = (it < ev.tc_pt3Idx.size()) ? ev.tc_pt3Idx[it] : -999;
          if (p3 < 0 || p3 >= static_cast<int>(ev.pT3_otHitIndices.size()))
            continue;
          touched5.clear();
          for (int h : ev.pT3_otHitIndices[p3]) {
            const auto f = hitOwn.find(h);
            if (f == hitOwn.end())
              continue;
            for (int slot : f->second) {
              if (cnt5[slot] == 0)
                touched5.push_back(slot);
              ++cnt5[slot];
            }
          }
          bool drop = false;
          for (int slot : touched5) {
            if (cnt5[slot] >= kShare)
              drop = true;
            cnt5[slot] = 0;
          }
          if (drop) {
            m16RowSuppressed[it] = 1;
            ++nM16SuppT5;
            ++nXp3Killed;
          }
        }
      }

      int nPixSuppressed = 0;
      int nSuppByType[3] = {0, 0, 0};  // {pT5 rows, pT3 rows, pLS rows}
      // -A 4 uses the same per-row verdict channel as M7c, fed by the M16 suppression set.
      const std::vector<char>* rowMaskOut =
          (attachMode == 4) ? &m16RowSuppressed : (attachMode == 2 ? &rowSuppressed : nullptr);
      writer.fillEventHybrid(ev, trk, outTCs, attachMode == 1 ? &plsSuppressed : nullptr, &nPixSuppressed,
                             attachMode == 2, (attachMode == 2 || attachMode == 4) ? nSuppByType : nullptr,
                             rowMaskOut);
      const auto t4 = std::chrono::steady_clock::now();

      const double inferMs = msBetween(t0, t1);
      const double weldMs = msBetween(t1, t2);
      // The K8 attach call sits inside the t2..t3 window; report it separately so the
      // arb bucket keeps meaning K9+K10 only.
      const double arbMs = msBetween(t2, t3) - attachMs;
      const double fillMs = msBetween(t3, t4);

      if (attachMode == 2) {
        // claim= is the PASS-1 count (bit-identical to -A 0 by construction); p2= is
        // subordinate pass-2 accepted/candidates; upg= attached pass-1 chains upgraded
        // in place to type 7; attach= all TCs with a pLS (upgrades + pass-2);
        // pixSupp= seed-family-suppressed kept-baseline rows [pT5/pT3/pLS];
        // dcaBlk= theta-passing 5+-layer chains blocked from attach by the M7c IP
        // gate; guardKept= family rows saved by the kinematic suppression guard;
        // k7drop= bare chain TCs removed by K7-lite.
        std::printf(
            "evt %lld (run %u lumi %u event %llu): pixKept=%lld chains=%lld -> theta=%lld ->"
            " pixdrop=%lld -> claim=%lld p2=%lld/%lld | chainTC=%zu (T5c=%lld T4c=%lld upg=%lld"
            " attach=%lld pixSupp=%d[%d/%d/%d] dcaBlk=%lld guardKept=%lld k7drop=%lld)"
            " | infer=%.3f weld=%.3f attach=%.3f arb=%.3f fill=%.3f ms\n",
            i, ev.run, ev.lumi, ev.evt, nPixKept, nChainsIn, nAfterTheta, nAfterPixDrop, nAfterClaim, nPass2Acc,
            nPass2Cand, chainTCs.size(), nT5c, nT4c, nUpgraded, nAttached, nPixSuppressed, nSuppByType[0],
            nSuppByType[1], nSuppByType[2], nDcaBlocked, nGuardKept, nK7Dropped, inferMs, weldMs, attachMs, arbMs,
            fillMs);
      } else if (attachMode == 3) {
        // dcaExempt= theta-passing 5+-layer chains exempt from the evidence rule
        // (dca >= dcaSplit, skipped from K8); evidOk= IP-compatible 5+-layer chains
        // with a pair above thetaAttach (kept); demoted= chains penalized before K9.
        std::printf(
            "evt %lld (run %u lumi %u event %llu): pixKept=%lld chains=%lld -> theta=%lld ->"
            " pixdrop=%lld -> claim=%lld | chainTC=%zu (T5c=%lld T4c=%lld dcaExempt=%lld"
            " evidOk=%lld demoted=%lld) | infer=%.3f weld=%.3f attach=%.3f arb=%.3f fill=%.3f ms\n",
            i, ev.run, ev.lumi, ev.evt, nPixKept, nChainsIn, nAfterTheta, nAfterPixDrop, nAfterClaim, chainTCs.size(),
            nT5c, nT4c, nDcaBlocked, nEvidenceOk, nDemoted, inferMs, weldMs, attachMs, arbMs, fillMs);
      } else if (attachMode == 4) {
        // M16b: claim= ALL chains accepted by K9 (attached chains walk the claim like any
        // other; there are no exclusions). delivT5 = accepted chains UPGRADED in place to
        // type 7 (a subset of chainTC, not extra TCs); delivT3 = stage-B type-5 TCs (extra);
        // supp= carried rows retired by attach [pT5/pT3/pLS]; dcaBlk= accepted 5+-layer
        // chains blocked from bidding by -D4.
        std::printf(
            "evt %lld (run %u lumi %u event %llu): pixKept=%lld chains=%lld -> theta=%lld ->"
            " pixdrop=%lld -> claim=%lld | chainTC=%zu (T5c=%lld T4c=%lld) | upgT5=%lld"
            " delivT3=%lld supp=%d[%d/%d/%d] dcaBlk=%lld"
            " | infer=%.3f weld=%.3f attach=%.3f arb=%.3f fill=%.3f ms\n",
            i, ev.run, ev.lumi, ev.evt, nPixKept, nChainsIn, nAfterTheta, nAfterPixDrop, nAfterClaim, chainTCs.size(),
            nT5c, nT4c, nDelivT5, nDelivT3, nPixSuppressed, nSuppByType[0], nSuppByType[1], nSuppByType[2],
            nGaDcaBlocked, inferMs, weldMs, attachMs, arbMs, fillMs);
      } else if (attachMode == 1) {
        // T5c/T4c stay the nLayers-class counts (attached chains are counted inside
        // T5c AND in attach=; their output tc_type is 7). pixSupp = kept-baseline rows
        // dropped by the K8 suppression (pixKept still counts pre-suppression rows).
        std::printf(
            "evt %lld (run %u lumi %u event %llu): pixKept=%lld chains=%lld -> theta=%lld ->"
            " pixdrop=%lld -> claim=%lld | chainTC=%zu (T5c=%lld T4c=%lld attach=%lld pixSupp=%d)"
            " | infer=%.3f weld=%.3f attach=%.3f arb=%.3f fill=%.3f ms\n",
            i, ev.run, ev.lumi, ev.evt, nPixKept, nChainsIn, nAfterTheta, nAfterPixDrop, nAfterClaim, chainTCs.size(),
            nT5c, nT4c, nAttached, nPixSuppressed, inferMs, weldMs, attachMs, arbMs, fillMs);
      } else {
        std::printf(
            "evt %lld (run %u lumi %u event %llu): pixKept=%lld chains=%lld -> theta=%lld ->"
            " pixdrop=%lld -> claim=%lld | chainTC=%zu (T5c=%lld T4c=%lld)"
            " | infer=%.3f weld=%.3f arb=%.3f fill=%.3f ms\n",
            i, ev.run, ev.lumi, ev.evt, nPixKept, nChainsIn, nAfterTheta, nAfterPixDrop, nAfterClaim, chainTCs.size(),
            nT5c, nT4c, inferMs, weldMs, arbMs, fillMs);
      }

      totPixKept += nPixKept;
      totChainsIn += nChainsIn;
      totAfterTheta += nAfterTheta;
      totAfterPixDrop += nAfterPixDrop;
      totAfterClaim += nAfterClaim;
      totChainTCs += static_cast<long long>(chainTCs.size());
      totT5c += nT5c;
      totT4c += nT4c;
      totAttached += nAttached;
      totPixSuppressed += nPixSuppressed;
      totPass2Cand += nPass2Cand;
      totPass2Acc += nPass2Acc;
      totUpgraded += nUpgraded;
      totDcaBlocked += nDcaBlocked;
      totGuardKept += nGuardKept;
      totK7Dropped += nK7Dropped;
      totDemoted += nDemoted;
      totEvidenceOk += nEvidenceOk;
      for (int t = 0; t < 3; ++t)
        totSuppByType[t] += nSuppByType[t];
      totPairsPref += att.nPairsPrefiltered;
      totPairsScored += att.nPairsScored;
      totDelivT5 += nDelivT5;
      totDelivT3 += nDelivT3;
      totCCDropped += nCCDropped;
      totGaDcaBlocked += nGaDcaBlocked;
      totM16ChainOwners += nM16ChainOwners;
      totGaPairs += ga.nPairs;
      totGaScored += ga.nScored;
      totGaChainAtt += ga.nChainAttached;
      totGaT3Att += ga.nT3Attached;
      totSeedDedup += nSeedDedup + nSeedDedupT3;
      totSeedDedupT3 += nSeedDedupT3;
      totM16Supp[0] += nM16SuppT7;
      totM16Supp[1] += nM16SuppT5;
      totM16Supp[2] += nM16SuppT8;
      totInferMs += inferMs;
      totWeldMs += weldMs;
      totAttachMs += attachMs;
      totArbMs += arbMs;
      totFillMs += fillMs;
    }
    writer.writeAndClose();
    if (dcaDump != nullptr)
      std::fclose(dcaDump);

    const double nEvD = nRun > 0 ? static_cast<double>(nRun) : 1.0;
    std::printf("hybrid summary: %lld events (thetaEdge=%.3f lambdaLen=%.3f thetaChain4/5/6=%.3f/%.3f/%.3f"
                " maxClaimedFrac=%.3f dropPixelConsumed=%s)\n",
                nRun, thetaEdge, lambdaLen, thetaChain4, thetaChain5, thetaChain6, maxClaimedFrac,
                dropPixelConsumed ? "on" : "off");
    const long long totAcceptedAll = totAfterClaim + totPass2Acc;
    std::printf("  pixel TCs kept  total=%lld mean=%.1f\n", totPixKept, totPixKept / nEvD);
    if (preClaimMode > 0)
      std::printf("  B1 claim-univ   mode=%d owners=%lld mean=%.1f | pre-claimed slots=%lld"
                  " mean=%.1f | killed by pixel: frac=%lld mean=%.1f braid=%lld mean=%.1f\n",
                  preClaimMode, totPixOwners, totPixOwners / nEvD, pixClaimStats.preClaimedSlots,
                  pixClaimStats.preClaimedSlots / nEvD, pixClaimStats.killedByPixFrac,
                  pixClaimStats.killedByPixFrac / nEvD, pixClaimStats.killedByPixBraid,
                  pixClaimStats.killedByPixBraid / nEvD);
    if (attachMode == 2)
      std::printf("  chain funnel    in=%lld -> theta=%lld -> pixdrop=%lld -> claim(pass1)=%lld"
                  " | pass2 cand=%lld accepted=%lld\n",
                  totChainsIn, totAfterTheta, totAfterPixDrop, totAfterClaim, totPass2Cand, totPass2Acc);
    else
      std::printf("  chain funnel    in=%lld -> theta=%lld -> pixdrop=%lld -> claim=%lld\n", totChainsIn, totAfterTheta,
                  totAfterPixDrop, totAfterClaim);
    std::printf("  chain TCs       total=%lld mean=%.1f (T5-class=%lld T4-class=%lld; %lld accepted chains"
                " below 4 layers dropped by K10)\n",
                totChainTCs, totChainTCs / nEvD, totT5c, totT4c, totAcceptedAll - totChainTCs);
    if (nPostClaimKilled > 0)
      std::printf("  post-claim kill total=%lld mean=%.1f (-Q4 %.4g / -Q5 %.4g on mX; no backfill)\n",
                  nPostClaimKilled, nPostClaimKilled / nEvD, q3Floor4, q3Floor5);
    if (dedupMinShared >= 1.f)
      std::printf("  post-arb dedup  total=%lld mean=%.1f (-DD %d shared hits, -DDF %.2f, -DDP %d,"
                  " -DDK %d; structural hit overlap only, no backfill)\n",
                  nDedupKilled, nDedupKilled / nEvD, static_cast<int>(std::floor(dedupMinShared)),
                  dedupShareFrac, dedupPix >= 0.5f ? 1 : 0, dedupKeyMode >= 0.5f ? 1 : 0);
    if (xp3Share >= 1.f)
      std::printf("  EX_DUPCC carried-pT3 crossclean retired=%lld (%.1f/evt) (-XP3 %d shared OT hit rows,"
                  " -XP3Z %.2f, -XP3L %d; shared-object only)\n",
                  nXp3Killed, nXp3Killed / nEvD, static_cast<int>(std::floor(xp3Share)), xp3Eta,
                  static_cast<int>(std::floor(xp3MinLay)));
    if (braidFracAlt > 0.f || claimFracAlt > 0.f || claimItemsAlt > -1.5f)
      std::printf("  EX_DUPCC band |eta| >= %.2f and nNodes <= %.4g : braid -WE %.4g (outside: -W %.4g),"
                  " claim -FB %.4g -FBC %.4g (outside: -F / -FC)\n",
                  braidAltEta, braidAltMaxNodes, braidFracAlt, braidFrac, claimFracAlt, claimItemsAlt);
    if (nCellSeen > 0)
      std::printf("  C1 cell (2n,5L) seen=%lld mean=%.1f | already killed by branch=%lld (%.1f%%)"
                  " | killed by -C25 %.4g / -C25D %.4g = %lld (%.1f%% of cell, %.1f%% of branch survivors)\n",
                  nCellSeen, nCellSeen / nEvD, nCellPreKilled, 100.0 * nCellPreKilled / nCellSeen, c25Theta, c25ThetaD,
                  nCellKilled, 100.0 * nCellKilled / nCellSeen,
                  nCellSeen > nCellPreKilled ? 100.0 * nCellKilled / (nCellSeen - nCellPreKilled) : 0.0);
    if (trimEnable != 0.f)
      std::printf("  B2 terminal trim examined=%lld (%.1f/evt) trimmed inner=%lld outer=%lld total=%lld"
                  " (%.1f/evt, %.4f of examined) | -TT %.4g -TL %d -TP %d -TA %.4g | %.3f ms/evt\n",
                  trimTot.nExamined, trimTot.nExamined / nEvD, trimTot.nTrimInner, trimTot.nTrimOuter,
                  trimTot.nTrimInner + trimTot.nTrimOuter, (trimTot.nTrimInner + trimTot.nTrimOuter) / nEvD,
                  trimTot.nExamined > 0 ? static_cast<double>(trimTot.nTrimInner + trimTot.nTrimOuter) /
                                              static_cast<double>(trimTot.nExamined)
                                        : 0.0,
                  trimFactor, static_cast<int>(trimMinLay), static_cast<int>(trimPasses), trimAbsChi2,
                  totTrimMs / nEvD);
    if (extendMode >= 0.5f)
      std::printf("  EX chain extend chains=%lld (%.1f/evt, noFit=%lld) cand=%lld | extended chains=%lld"
                  " (%.1f/evt, %.4f of examined) outer=%lld inner=%lld | chi2-rejected=%lld"
                  " uniq-rejected=%lld fit-rejected=%lld | -EX %d -EXW %.4g -EXR %.4g -EXF %.4g -EXU %.4g -EXD %.4g"
                  " -EXJ %d -EXN %d -EXL %d -EXS %d -EXC %.4g | %.3f ms/evt\n",
                  exStats.nChains, exStats.nChains / nEvD, exStats.nNoFit, exStats.nCand,
                  exStats.nExtChains, exStats.nExtChains / nEvD,
                  exStats.nChains > 0 ? static_cast<double>(exStats.nExtChains) /
                                            static_cast<double>(exStats.nChains)
                                      : 0.0,
                  exStats.nExtOuter, exStats.nExtInner, exStats.nRejChi2, exStats.nRejUniq, exStats.nRejFit,
                  static_cast<int>(extendMode + 0.5f), extendWindow, extendRz, extendChi2F,
                  extendUniq, extendMaxD, static_cast<int>(extendJump + 0.5f),
                  static_cast<int>(extendPerEnd + 0.5f), std::max(4, static_cast<int>(extendMinLay + 0.5f)),
                  static_cast<int>(extendSeg + 0.5f), extendMaxChi2, extendMs / nEvD);
    if (attachMode) {
      std::printf("  K8 attach       attached=%lld mean=%.1f | baseline pixel rows suppressed=%lld mean=%.1f"
                  " | pairs prefiltered=%lld scored=%lld (thetaAttach=%.3f head=%s)\n",
                  totAttached, totAttached / nEvD, totPixSuppressed, totPixSuppressed / nEvD, totPairsPref,
                  totPairsScored, thetaAttach, attachHeadAvailable() ? "trained" : "sentinel");
      if (attachMode == 3)
        std::printf("  M9 evidence     dca-exempt=%lld mean=%.1f (dcaSplit=%.2f) | evidence-ok=%lld mean=%.1f"
                    " | demoted=%lld mean=%.1f (penalty=%g)\n",
                    totDcaBlocked, totDcaBlocked / nEvD, dcaSplit, totEvidenceOk, totEvidenceOk / nEvD, totDemoted,
                    totDemoted / nEvD, static_cast<double>(demotePenalty));
      if (attachMode == 2) {
        std::printf("  M7b breakdown   pass1 upgrades=%lld mean=%.1f | pass2 TCs=%lld mean=%.1f"
                    " | suppressed rows by type pT5=%lld pT3=%lld pLS=%lld\n",
                    totUpgraded, totUpgraded / nEvD, totPass2Acc, totPass2Acc / nEvD, totSuppByType[0],
                    totSuppByType[1], totSuppByType[2]);
        std::printf("  M7c breakdown   dca-blocked chains=%lld mean=%.1f (dcaMax=%.2f) | guard-kept rows=%lld"
                    " mean=%.1f (dR<%.3f) | K7-lite drops=%lld mean=%.1f (%s, dR<%.3f)\n",
                    totDcaBlocked, totDcaBlocked / nEvD, dcaAttachMax, totGuardKept, totGuardKept / nEvD, suppDR,
                    totK7Dropped, totK7Dropped / nEvD, k7Lite ? "on" : "off", k7DR);
      }
      if (attachMode == 4) {
        std::printf("  M16 attach      pairs=%lld mean=%.0f scored=%lld | chain-attached=%lld mean=%.1f"
                    " | T3-attached=%lld mean=%.1f | dca-blocked=%lld (-D4 %.4g)"
                    " | seed-family dedup revoked=%lld mean=%.1f (-RD %d)\n",
                    totGaPairs, totGaPairs / nEvD, totGaScored, totGaChainAtt, totGaChainAtt / nEvD, totGaT3Att,
                    totGaT3Att / nEvD, totGaDcaBlocked, dcaAttach4, totSeedDedup, totSeedDedup / nEvD,
                    seedDupClean >= 0.5f ? 1 : 0);
        std::printf("  M16 delivery    type-7 in-place upgrades of accepted chains=%lld mean=%.1f"
                    " (of %lld accepted chain TCs) | extra type-5 TCs=%lld mean=%.1f\n",
                    totDelivT5, totDelivT5 / nEvD, totChainTCs, totDelivT3, totDelivT3 / nEvD);
        std::printf("  M16 suppression carried rows retired: pT5=%lld pT3=%lld pLS=%lld (total=%lld mean=%.1f)"
                    " | -RT5=%d -RT3=%d -RPS=%d -a=%.3f -AT3=%.3f\n",
                    totM16Supp[0], totM16Supp[1], totM16Supp[2], totM16Supp[0] + totM16Supp[1] + totM16Supp[2],
                    (totM16Supp[0] + totM16Supp[1] + totM16Supp[2]) / nEvD, replT5 >= 0.5f ? 1 : 0,
                    replT3 >= 0.5f ? 1 : 0, replPls >= 0.5f ? 1 : 0, thetaAttach, thetaAttachT3);
        // ---- M20 CANDIDATE FINDER (-CF) + SUPERSET AUDIT (-CFA) ---------------------
        if (candModeI != kCandAnalytic) {
          const char* cfName = (candModeI == kCandBinned) ? "binned prefilter" : "map candidates";
          std::printf("  M20 candfind    -CF %d (%s) | targets=%lld (%.0f/evt) full-scan pairs=%lld"
                      " (%.3g/evt) | examined=%lld (%.3g/evt, %.4gx of full scan)"
                      " | emitted=%lld (%.3g/evt) | build=%.3f ms/evt\n",
                      candModeI, cfName, candStats.nTargets, candStats.nTargets / nEvD, candStats.nFullScan,
                      candStats.nFullScan / nEvD, candStats.nExamined, candStats.nExamined / nEvD,
                      candStats.nFullScan > 0
                          ? static_cast<double>(candStats.nExamined) / static_cast<double>(candStats.nFullScan)
                          : 0.0,
                      candStats.nEmitted, candStats.nEmitted / nEvD, totCandBuildMs / nEvD);
          if (candModeI == kCandBinned)
            std::printf("  M20 candbins    rt %d x %.1f cm | tanLambda %d x %.4g (|t| <= %.4g)"
                        " | phi %d x %.4g rad | pad %.4g | wild seeds=%d wild targets=%lld\n",
                        candIdx.nRt, candIdx.rtW, candIdx.nTan, candIdx.tanW, candIdx.tanClamp, candIdx.nPhi,
                        candIdx.phiW, candPhiPad, static_cast<int>(candIdx.wildPls.size()),
                        candStats.nWildTargets);
          if (candModeI == kCandMap)
            std::printf("  M20 candmap     file=%s | events missing=%lld | bare targets with no"
                        " candidate=%lld | analytic windows %s (-CFW %d)\n",
                        candMapPath.c_str(), candStats.nMapEventsMissing, candStats.nMapTargetsEmpty,
                        candMapWin >= 0.5f ? "ENFORCED" : "features only", candMapWin >= 0.5f ? 1 : 0);
          if (candAudit >= 0.5f)
            std::printf("  M20 SUPERSET AUDIT (-CFA 1): analytic-accepted pairs=%lld (%.3g/evt)"
                        " | MISSING FROM THE CANDIDATE SET=%lld  ==> %s\n",
                        candStats.nAnalytic, candStats.nAnalytic / nEvD, candStats.nMissing,
                        candStats.nMissing == 0 ? "PASS (exact superset)" : "*** FAIL ***");
        }
        // ---- M20 pT3-CLASS HIT-OVERLAP CONTENTION (-CC) -----------------------------
        // ---- M20 pT3-CLASS DEDUP LEDGER: the two stages reported SEPARATELY -----------
        // PIXEL SIDE  = the -RD/-RDT seed-family dedup inside the contention (a delivery
        //               whose pLS shares >= 2 pixel hit rows with a kept owner's pLS).
        // OT SIDE     = the -CC ownership-map contention over what the pixel side left.
        // Reported apart because the maintainer question is exactly which of the two is
        // carrying the work, and whether the OT side alone suffices.
        std::printf("  M20 pT3 dedup   PIXEL side (-RDT %s): revoked=%lld (%.1f/evt)"
                    " | OT side (-CC %d -CCG %s -CCN %d -CCP %d -CCK %d -CCR %d):"
                    " revoked=%lld (%.1f/evt)"
                    " | DELIVERED=%lld (%.1f/evt)  [LST pT3 reference ~150/evt]\n",
                    (rdT3 < -0.5f) ? (seedDupClean >= 0.5f ? "follow -RD =1" : "follow -RD =0")
                                   : (rdT3 >= 0.5f ? "1" : "0"),
                    totSeedDedupT3, totSeedDedupT3 / nEvD, ccMode >= 0.5f ? 1 : 0,
                    ccGran >= 0.5f ? "MD" : "hit", std::max(1, static_cast<int>(ccMinShared + 0.5f)),
                    ccPreclaim >= 0.5f ? 1 : 0, static_cast<int>(ccOrder + 0.5f),
                    ccRelPls >= 0.5f ? 1 : 0, totCCDropped,
                    totCCDropped / nEvD, totDelivT3, totDelivT3 / nEvD);
        std::printf("  M20 stageB      %s | pT3-class candidates before any dedup=%lld (%.1f/evt)\n",
                    doT3Stage ? "ON" : "off", totSeedDedupT3 + totCCDropped + totDelivT3,
                    (totSeedDedupT3 + totCCDropped + totDelivT3) / nEvD);
      }
      // M16b: totDelivT5 are IN-PLACE upgrades already counted inside totChainTCs, so only
      // the stage-B type-5 TCs are additive at -A 4.
      const long long extraDeliv = (attachMode == 4) ? totDelivT3 : (totDelivT5 + totDelivT3);
      std::printf("  output TCs/evt  mean=%.1f (pixel kept %.1f - suppressed %.1f + chain %.1f - k7 %.1f"
                  " + attach deliveries %.1f)\n",
                  (totPixKept - totPixSuppressed + totChainTCs - totK7Dropped + extraDeliv) / nEvD,
                  totPixKept / nEvD, totPixSuppressed / nEvD, totChainTCs / nEvD, totK7Dropped / nEvD,
                  extraDeliv / nEvD);
      std::printf("  time mean/evt   infer=%.3f weld=%.3f attach=%.3f arb+asm=%.3f fill=%.3f ms\n", totInferMs / nEvD,
                  totWeldMs / nEvD, totAttachMs / nEvD, totArbMs / nEvD, totFillMs / nEvD);
      // ---- ATTACH CONFUSION MATRIX report (PROTO_ATTACH_CM) -------------------------
      if (attachCM) {
        const long long attTot = cmAttSame + cmAttCross + cmAttTgtFakePls + cmAttFakeTgtRealPls + cmAttFakeBoth;
        const double attD = attTot > 0 ? static_cast<double>(attTot) : 1.0;
        const long long trueTot = cmAttSame + cmMissBelowTheta + cmMissContention + cmMissOther;
        const double trueD = trueTot > 0 ? static_cast<double>(trueTot) : 1.0;
        std::printf("[ATTACHCM] a=%.4g AT3=%.4g RPS=%d RT3=%d over %lld events\n", thetaAttach, thetaAttachT3,
                    replPls >= 0.5f ? 1 : 0, replT3 >= 0.5f ? 1 : 0, nRun);
        std::printf("[ATTACHCM] DECISIONS n=%lld (%.2f/evt) | same_sim=%lld (%.4f) cross_sim=%lld (%.4f)"
                    " true_tgt_fake_pls=%lld (%.4f) fake_tgt_real_pls=%lld (%.4f) fake_both=%lld (%.4f)\n",
                    attTot, attTot / nEvD, cmAttSame, cmAttSame / attD, cmAttCross, cmAttCross / attD,
                    cmAttTgtFakePls, cmAttTgtFakePls / attD, cmAttFakeTgtRealPls, cmAttFakeTgtRealPls / attD,
                    cmAttFakeBoth, cmAttFakeBoth / attD);
        std::printf("[ATTACHCM] PRECISION same_sim/attaches=%.4f | stage-B(T3) same=%lld bad=%lld\n",
                    cmAttSame / attD, cmAttSameT3, cmAttBadT3);
        std::printf("[ATTACHCM] RECALL true pairs in prefilter=%lld (%.2f/evt) | attached=%lld (%.4f)"
                    " missed_below_theta=%lld (%.4f) missed_contention=%lld (%.4f) missed_other=%lld (%.4f)\n",
                    cmTruePairPref, cmTruePairPref / nEvD, cmAttSame, cmAttSame / trueD, cmMissBelowTheta,
                    cmMissBelowTheta / trueD, cmMissContention, cmMissContention / trueD, cmMissOther,
                    cmMissOther / trueD);
        std::printf("[ATTACHCM] PREFILTER true pairs possible=%lld (%.2f/evt) kept=%lld -> window recall=%.4f\n",
                    cmTruePairPoss, cmTruePairPoss / nEvD, cmTruePairPref,
                    cmTruePairPoss > 0 ? static_cast<double>(cmTruePairPref) / static_cast<double>(cmTruePairPoss)
                                       : 0.0);
        const double hadD = cmTgtHadTrue > 0 ? static_cast<double>(cmTgtHadTrue) : 1.0;
        std::printf("[ATTACHCM] TARGETS with a true pLS available=%lld (%.2f/evt) | got_right=%lld (%.4f)"
                    " got_wrong=%lld (%.4f) got_none=%lld (%.4f)\n",
                    cmTgtHadTrue, cmTgtHadTrue / nEvD, cmTgtGotRight, cmTgtGotRight / hadD, cmTgtGotWrong,
                    cmTgtGotWrong / hadD, cmTgtGotNone, cmTgtGotNone / hadD);
        std::printf("[ATTACHCM] TARGETS with NO true pLS available=%lld (%.2f/evt) | attached anyway=%lld (%.4f)\n",
                    cmTgtNoTrue, cmTgtNoTrue / nEvD, cmTgtNoTrueAttached,
                    cmTgtNoTrue > 0 ? static_cast<double>(cmTgtNoTrueAttached) / static_cast<double>(cmTgtNoTrue)
                                    : 0.0);
        std::printf("[ATTACHCM] SUPPRESSED type-8 rows: real_pLS=%lld (of which owner shares the sim=%lld)"
                    " fake_pLS=%lld\n",
                    cmSuppRealPls, cmSuppRealPlsSameSimOwner, cmSuppFakePls);
        // The threshold frontier: for every candidate cut t, what the BEST-pair-per-target
        // population looks like at or above t. This is what picks the -a scan points.
        std::printf("[ATTACHCM] BEST-PER-TARGET frontier (cut : n_attachable same cross/fake precision)\n");
        for (int b = 0; b < kCMBins; ++b) {
          long long cs = 0, cb = 0;
          for (int k = b; k < kCMBins; ++k) {
            cs += cmHistBestSame[k];
            cb += cmHistBestBad[k];
          }
          if (cs + cb == 0)
            continue;
          const float cut = kCMLo + b * kCMW;
          std::printf("[ATTACHCM]   cut %+7.2f : n=%-9lld same=%-9lld bad=%-9lld prec=%.4f (%.2f/evt)\n", cut, cs + cb,
                      cs, cb, static_cast<double>(cs) / static_cast<double>(cs + cb), (cs + cb) / nEvD);
        }
        std::printf("[ATTACHCM] ALL-PAIR logit histogram (bin_lo same bad)\n");
        for (int b = 0; b < kCMBins; ++b) {
          if (cmHistSame[b] == 0 && cmHistBad[b] == 0)
            continue;
          std::printf("[ATTACHCM]   %+7.2f %lld %lld\n", kCMLo + b * kCMW, cmHistSame[b], cmHistBad[b]);
        }
      }
    } else {
      std::printf("  output TCs/evt  mean=%.1f (pixel %.1f + chain %.1f)\n", (totPixKept + totChainTCs) / nEvD,
                  totPixKept / nEvD, totChainTCs / nEvD);
      std::printf("  time mean/evt   infer=%.3f weld=%.3f arb+asm=%.3f fill=%.3f ms\n", totInferMs / nEvD,
                  totWeldMs / nEvD, totArbMs / nEvD, totFillMs / nEvD);
    }
    std::printf("  wrote %s\n", outPath.c_str());
    return 0;
  }

  if (mode == "oracle") {
    // Truth-scored welding: logOdds = +10 for label==1 edges, -10 otherwise, thetaEdge=0,
    // so K6 welds exclusively along true edges -- the ceiling of what chain-tracking can
    // reach with a perfect edge classifier under the mutual-best weld rule.
    std::printf("oracle mode: thetaEdge=0 (forced) lambdaLen=%.3f kWeldSweeps=%d\n", lambdaLen, kWeldSweeps);
    long long totT3 = 0, totEdges = 0, totTrue = 0, totChains = 0;
    long long totChainsSim = 0, totChainsPileup = 0, totChainsNone = 0;
    double totWeldMs = 0.0;

    // Ceiling accumulators. Categories overlap by design: [0] prompt vxy<1,
    // [1] displaced vxy>=1 (includes very displaced), [2] very displaced vxy>=5.
    long long formable[3] = {0, 0, 0};
    long long chained[3] = {0, 0, 0};
    long long layHist[3][5] = {{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}};
    long long baseAny[3] = {0, 0, 0};
    long long baseT5T4[3] = {0, 0, 0};

    for (long long i = 0; i < nRun; ++i) {
      if (!reader.loadEntry(i, ev, trk)) {
        std::fprintf(stderr, "Error: no aligned tracking event for LST entry %lld.\n", i);
        return 1;
      }
      ChainGraph g;
      k1BuildIncidence(ev, g);
      k2BuildEdges(ev, g);
      T3SimSets t3sims;
      buildT3SimSets(ev, t3sims);
      EdgeLabels labels;
      labelEdges(ev, g, t3sims, labels);

      EdgeScores scores;
      scores.logOdds.resize(g.edges.size());
      for (std::size_t e = 0; e < g.edges.size(); ++e)
        scores.logOdds[e] = labels.label[e] == 1 ? 10.0f : -10.0f;

      Chains chains;
      const auto t0 = std::chrono::steady_clock::now();
      k6WeldChains(ev, g, scores, 0.0f, lambdaLen, chains);
      const auto t1 = std::chrono::steady_clock::now();
      const double weldMs = msBetween(t0, t1);

      const int nAccepted = static_cast<int>(ev.sim_pt.size());

      // Per-accepted-sim truth-edge mark: labels.simIdx keeps only ONE sim per edge, so
      // redo the sim-set intersection to credit EVERY accepted sim shared by both T3s.
      std::vector<char> hasTrueEdge(nAccepted, 0);
      std::vector<int> common;
      long long nTrue = 0;
      for (std::size_t e = 0; e < g.edges.size(); ++e) {
        if (labels.label[e] != 1)
          continue;
        ++nTrue;
        const auto& a = t3sims.sims[g.edges[e].inner];
        const auto& b = t3sims.sims[g.edges[e].outer];
        common.clear();
        std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(common));
        for (int s : common)
          if (s < nAccepted)
            hasTrueEdge[s] = 1;
      }

      // Chain -> sim assignment; per accepted sim keep the max chain nLayers.
      const long long nChains = chains.offsets.empty() ? 0 : static_cast<long long>(chains.offsets.size()) - 1;
      std::vector<int> maxLay(nAccepted, -1);
      long long nSimChains = 0, nPileupChains = 0, nNoneChains = 0;
      for (long long c = 0; c < nChains; ++c) {
        const int s = chainSimIdx(chains, static_cast<int>(c), t3sims, nAccepted);
        if (s < 0)
          ++nNoneChains;
        else if (s >= nAccepted)
          ++nPileupChains;  // pileup sim: counted in totals, no kinematics -> not in table
        else {
          ++nSimChains;
          if (chains.nLayers[c] > maxLay[s])
            maxLay[s] = chains.nLayers[c];
        }
      }

      // Ceiling accumulation over accepted sims in the kinematic window.
      for (int s = 0; s < nAccepted; ++s) {
        if (!hasTrueEdge[s])
          continue;
        if (!(ev.sim_pt[s] > 0.9f) || !(std::fabs(ev.sim_eta[s]) < 4.5f))
          continue;
        const float vxy = std::sqrt(ev.sim_vx[s] * ev.sim_vx[s] + ev.sim_vy[s] * ev.sim_vy[s]);
        const bool cat[3] = {vxy < 1.0f, vxy >= 1.0f, vxy >= 5.0f};
        const int tcIdx = s < static_cast<int>(ev.sim_tcIdx.size()) ? ev.sim_tcIdx[s] : -999;
        const bool anyTC = tcIdx >= 0 && tcIdx < static_cast<int>(ev.tc_type.size());
        const bool t5t4TC = anyTC && (ev.tc_type[tcIdx] == 4 || ev.tc_type[tcIdx] == 9);
        for (int k = 0; k < 3; ++k) {
          if (!cat[k])
            continue;
          ++formable[k];
          if (maxLay[s] >= 0) {
            ++chained[k];
            ++layHist[k][layerBin8(maxLay[s])];
          }
          if (anyTC)
            ++baseAny[k];
          if (t5t4TC)
            ++baseT5T4[k];
        }
      }

      const long long nT3 = static_cast<long long>(ev.t3_lsIdx0.size());
      const long long nEdges = static_cast<long long>(g.edges.size());
      std::printf(
          "evt %lld (run %u lumi %u event %llu): nT3=%lld edges=%lld true=%lld chains=%lld"
          " (sim=%lld pileup=%lld none=%lld) | weld=%.3f ms\n",
          i, ev.run, ev.lumi, ev.evt, nT3, nEdges, nTrue, nChains, nSimChains, nPileupChains, nNoneChains, weldMs);

      totT3 += nT3;
      totEdges += nEdges;
      totTrue += nTrue;
      totChains += nChains;
      totChainsSim += nSimChains;
      totChainsPileup += nPileupChains;
      totChainsNone += nNoneChains;
      totWeldMs += weldMs;
    }

    const double nEvD = nRun > 0 ? static_cast<double>(nRun) : 1.0;
    std::printf("oracle summary: %lld events (lambdaLen=%.3f)\n", nRun, lambdaLen);
    std::printf("  nT3 total=%lld mean=%.1f | edges total=%lld mean=%.1f | true edges total=%lld mean=%.1f\n", totT3,
                totT3 / nEvD, totEdges, totEdges / nEvD, totTrue, totTrue / nEvD);
    std::printf("  chains total=%lld mean=%.1f : accepted-sim=%lld pileup-sim=%lld unmatched=%lld\n", totChains,
                totChains / nEvD, totChainsSim, totChainsPileup, totChainsNone);
    std::printf("  weld time total=%.3f ms mean=%.3f ms\n", totWeldMs, totWeldMs / nEvD);

    // The ceiling table. Percentages are relative to the formable row of each column.
    auto pct = [](long long n, long long d) { return d > 0 ? 100.0 * static_cast<double>(n) / d : 0.0; };
    std::printf("\nORACLE CEILING (accepted sims, sim_pt>0.9, |sim_eta|<4.5;");
    std::printf(" columns overlap: disp includes vDisp)\n");
    std::printf("  %-26s %16s %16s %16s\n", "", "prompt(vxy<1)", "disp(vxy>=1)", "vDisp(vxy>=5)");
    std::printf("  %-26s %16lld %16lld %16lld\n", "formable (>=1 true edge)", formable[0], formable[1], formable[2]);
    std::printf("  %-26s %8lld (%4.1f%%) %8lld (%4.1f%%) %8lld (%4.1f%%)\n", "oracle chained (>=2 T3)", chained[0],
                pct(chained[0], formable[0]), chained[1], pct(chained[1], formable[1]), chained[2],
                pct(chained[2], formable[2]));
    static const char* layRow[5] = {"  max nLayers <= 4", "  max nLayers = 5", "  max nLayers = 6",
                                    "  max nLayers = 7", "  max nLayers >= 8"};
    for (int b = 0; b < 5; ++b)
      std::printf("  %-26s %16lld %16lld %16lld\n", layRow[b], layHist[0][b], layHist[1][b], layHist[2][b]);
    std::printf("  %-26s %8lld (%4.1f%%) %8lld (%4.1f%%) %8lld (%4.1f%%)\n", "baseline: any TC", baseAny[0],
                pct(baseAny[0], formable[0]), baseAny[1], pct(baseAny[1], formable[1]), baseAny[2],
                pct(baseAny[2], formable[2]));
    std::printf("  %-26s %8lld (%4.1f%%) %8lld (%4.1f%%) %8lld (%4.1f%%)\n", "baseline: T5/T4-type TC", baseT5T4[0],
                pct(baseT5T4[0], formable[0]), baseT5T4[1], pct(baseT5T4[1], formable[1]), baseT5T4[2],
                pct(baseT5T4[2], formable[2]));
    return 0;
  }

  // graph mode
  long long totT3 = 0;
  long long totE1Exact = 0, totE1Emitted = 0;
  long long totE2Exact = 0, totE2Emitted = 0;
  long long totE2Suppressed = 0;
  long long totEdges = 0;
  long long minEdges = -1, maxEdges = -1;
  double totK1Ms = 0.0, totK2Ms = 0.0;

  for (long long i = 0; i < nRun; ++i) {
    if (!reader.loadEntry(i, ev, trk)) {
      std::fprintf(stderr, "Error: no aligned tracking event for LST entry %lld.\n", i);
      return 1;
    }

    ChainGraph g;
    const auto t0 = std::chrono::steady_clock::now();
    k1BuildIncidence(ev, g);
    const auto t1 = std::chrono::steady_clock::now();
    k2BuildEdges(ev, g);
    const auto t2 = std::chrono::steady_clock::now();
    const double k1Ms = msBetween(t0, t1);
    const double k2Ms = msBetween(t1, t2);

    long long e1Emitted = 0, e2Emitted = 0;
    for (const auto& e : g.edges) {
      if (e.type == 1)
        ++e1Emitted;
      else if (e.type == 2)
        ++e2Emitted;
    }
    // K2 drops E2 edges that duplicate an E1 edge, so the shortfall vs the
    // degree-arithmetic exact count is exactly the suppressed-as-E1 count.
    const long long e2Suppressed = g.e2CountExact - e2Emitted;
    const long long nT3 = static_cast<long long>(ev.t3_lsIdx0.size());
    const long long nEdges = static_cast<long long>(g.edges.size());

    std::printf(
        "evt %lld (run %u lumi %u event %llu): nT3=%lld"
        " E1 exact=%lld emitted=%lld | E2 exact=%lld emitted=%lld suppressedAsE1=%lld"
        " | edges=%lld | k1=%.3f ms k2=%.3f ms\n",
        i, ev.run, ev.lumi, ev.evt, nT3, g.e1CountExact, e1Emitted, g.e2CountExact, e2Emitted, e2Suppressed, nEdges,
        k1Ms, k2Ms);
    if (e1Emitted != g.e1CountExact)
      std::fprintf(stderr, "WARNING: evt %lld E1 emitted (%lld) != exact (%lld)\n", i, e1Emitted, g.e1CountExact);

    totT3 += nT3;
    totE1Exact += g.e1CountExact;
    totE1Emitted += e1Emitted;
    totE2Exact += g.e2CountExact;
    totE2Emitted += e2Emitted;
    totE2Suppressed += e2Suppressed;
    totEdges += nEdges;
    if (minEdges < 0 || nEdges < minEdges)
      minEdges = nEdges;
    if (nEdges > maxEdges)
      maxEdges = nEdges;
    totK1Ms += k1Ms;
    totK2Ms += k2Ms;
  }

  const double nEvD = nRun > 0 ? static_cast<double>(nRun) : 1.0;
  std::printf("graph summary: %lld events\n", nRun);
  std::printf("  nT3 total=%lld\n", totT3);
  std::printf("  E1 exact=%lld emitted=%lld%s\n", totE1Exact, totE1Emitted,
              totE1Exact == totE1Emitted ? "" : "  [MISMATCH]");
  std::printf("  E2 exact=%lld emitted=%lld suppressedAsE1=%lld\n", totE2Exact, totE2Emitted, totE2Suppressed);
  std::printf("  edges/event min=%lld mean=%.1f max=%lld\n", nRun > 0 ? minEdges : 0, totEdges / nEvD,
              nRun > 0 ? maxEdges : 0);
  std::printf("  time k1 total=%.3f ms mean=%.3f ms | k2 total=%.3f ms mean=%.3f ms\n", totK1Ms, totK1Ms / nEvD,
              totK2Ms, totK2Ms / nEvD);
  return 0;
}
