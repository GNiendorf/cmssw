# GEN-A STATUS (durable milestone log)

Workspace: standalone/gen_a_ref/   Code tree: standalone/protoA/  (copy of prototype @ M20)
Gate: P25BASE eff .81303 | dup .05188 | fake .04636 | nTC 614277 (300 evts PU200RelVal).

## A0 -- SETUP (DONE)
protoA built from prototype (source-identical start). Runner gen_a_ref/ga_run.sh,
pairdump runner ga_pd.sh. Binaries are SNAPSHOTTED (bin_qd2, bin_pd1) before long runs so
a rebuild cannot touch a running job.

## A1 -- TASK-1 INSTRUMENT (-QD) BUILT AND CROSS-VALIDATED
New flag `-QD <path>` (main.cc, hybrid path, right after gaStageT3, BEFORE any dedup).
Writes one text stream with, per event:
  S = accepted sims (pt, eta, vperp, vz)
  L = LST's COMPLETE PRE-CLEANING pT3 candidate set: pLS pixel hits ++ pT3_otHitIndices,
      matched with the prototype's OWN ported >0.75 matcher
  O = our bare-T3 delivery winners with their attach logit, matched the SAME way
Run at `-AT3 -1e9`: gaStageT3's per-target pick is argmax over enumerated pairs and its
pLS contention keeps the global argmax, so the winner set at margin theta is EXACTLY
{winners with logit >= theta}. ONE dump therefore carries the whole threshold curve
(proved in code; verified numerically: O-count per event == the log's delivT3).

CROSS-VALIDATION (independent, from the ntuple's own truth branches):
  my instrument : LST 588.7 rows/evt, fake .04942, covers 1310 in-cut sims
  ntuple branches: LST 588.7 rows/evt, isFake .0477, pT3_simIdx covers 1310 in-cut sims
  denominator   : 22784 over 300 evts == Root__TC_base_0_0_ef_denom_eta EXACTLY
    (harness rule = pt>0.9, |vz|<30, vperp<2.5 AND sim_q != 0 -- neutrals are dropped by
     performance.cc "if (effset.pdgid == 0 and q == 0) return", worth 35120 -> 22784)

## A2 -- TASK-1 VERDICT: OUR MATCHING IS BETTER. GLOBALLY AND IN EVERY REGION/pT BAND.

Dedup ignored on BOTH sides. Coverage = distinct in-cut sims matched >75% by the set.

  set                              cand/evt   cov   covFrac   fake
  LST pT3, complete pre-clean         588.7  1310   0.05750   .04942
  ours at MATCHED COUNT (th 8.19)     588.7  4704   0.20646   .01677
  ours at LST's post-clean count      151.7  1254   0.05504   .00440
      (th 10.05, = LST's 151.7 retired TC rows)

  => at equal candidate count we cover 3.6x more sims at 1/3 the fake fraction;
  => at 1/4 the candidate count we match LST's coverage (96%) at 1/11 the fake fraction.
  LST's single operating point lies FAR BELOW our curve everywhere.

PER REGION (cand/evt | cov | fake), LST vs ours at a count-comparable threshold:
  barrel  |eta|<1.1  LST 240.5 | 822 (.0925) | .0404      ours th8.0  86.3 | 1551 (.1745) | .0331
  trans   1.1-1.7    LST  93.3 | 258 (.0646) | .0462      ours th9.0  51.8 |  712 (.1782) | .0150
  endcap  >1.7       LST  96.0 | 230 (.0232) | .0752      ours th10.1 128.0 |  956 (.0966) | .0036
PER pT:
  0.9-1.5  LST 304.9 | 599 (.0658) | .0501   ours th9.0  190.9 | 973 (.1069) | .0107
  1.5-3    LST 105.7 | 412 (.0573) | .0479   ours th10.1  68.3 | 572 (.0796) | .0036
  3-10     LST  17.7 | 234 (.0500) | .0402   ours th10.1   9.3 | 242 (.0517) | .0025
  10+      LST   1.4 |  65 (.0360) | .1392   ours th9.0    0.5 |  86 (.0477) | .0252
NO regime where LST's matching wins. TASK 2's premise ("if worse") does not hold.

HONEST CAVEATS
 (a) our per-target argmax + one-pLS-one-owner contention is part of the ATTACH machine
     (candidate formation), not our dedup; LST's pre-clean set does reuse a pLS across
     rows. That helps us and is stated rather than hidden.
 (b) our ranking is not a SUPERSET of LST's: at matched count we share only 546 of LST's
     1310 sims (764 unique to LST) while gaining 4158 unique. Our full universe covers
     1183/1310 = 90.3% of LST's sims, so 127 are structurally unreachable (T3 not bare,
     or pair outside the prefilter).

## A3 -- THE REAL PROBLEM IS COMPLEMENTARITY, NOT MATCHING (the decisive number)
Marginal value of the class in OUR pipeline (from the prior agent's matrix):
  r_off  (class OFF entirely)  eff .77432 dup .05254 fake .04905 nTC 579968
  P25BASE(LST's pT3 rows)      eff .81303 -> LST's rows are worth +.03871
  d_none (ours, no dedup)      eff .81505 -> our rows are worth +.04073  (the CEILING)
  d_n1   (ours, best dedup yet)eff .80456 dup .04682 fake .05649
Our candidate set at theta 6 covers 37.5% of the denominator but only 4.07% of it is NEW:
~90% of what we cover is ALREADY delivered by the chains. LST's set covers 5.75% and 3.87%
of that is new (67% new). So LST's machinery is not a better MATCHER, it is a better
COMPLEMENT -- and complementarity is exactly what an ownership-map dedup is for.
=> the entire remaining job is TASK 3.

## A4 -- THE OFFLINE DEDUP LABORATORY (ga_lab.py / ga_lab2.py)
A 300-evt C++ A/B costs 13 min. The -QD stream carries the candidate set the delivery
actually sees, so the dedup rules are replayed in Python and only finalists are paid for
in C++. VALIDATION: the audit's A record reproduces r_off's effNum EXACTLY (17642/22784 =
0.77432), and replaying the 9 known configurations preserves their true efficiency ORDER
exactly, with real = est - 0.0027 (sd .0012) and real_fake = est + 0.0014 (sd .0013).

## A5 -- THE HARD STRUCTURAL CEILING (the decisive TASK-3 result)
`ORACLEONLY` = perfect truth-based selection (keep ONLY candidates that cover an in-cut
sim nothing else covers), no threshold. It bounds what ANY head or ranking could ever do:

  ownership rule                          deliv/e   new sims (lab)   -> max real eff
  no OT dedup                                 3.7             1061         ~.8162
  ccN3  drop when ALL 3 MDs owned             3.5             1023         ~.8147
  ccN2  drop when >= 2 MDs owned              3.2              929         ~.8110
  ccN1  drop when >= 1 MD owned               2.8              826         ~.8069
  ccN1 no pre-claim                           3.5             1060         ~.8162
GATE needs +882 real new sims (eff .81303).

MEASURED added-row DUPLICATE fraction of each rule (from the real runs, pt>0.9 rows):
  ccN1  -1516 dups on +22999 rows   (NEGATIVE: the rule removes more dups than it adds)
  ccN2 +11079 dups on +34694 rows   = 31.9%
  nocc +134536 dups on +106625 rows = 126%
The dup gate (<= .052, against r_off's ALREADY-FAILING .05254) admits at most ~4% added-
row duplicates. Only ccN1 achieves that.

=> ccN1 is REQUIRED by dup and CAPS efficiency at ~.8069 < .81303.
=> ccN3/nocc are REQUIRED by efficiency and blow dup to .072 / .283.
The two gates are mutually exclusive ON THIS CANDIDATE RANKING, and the ORACLE run shows
it is NOT a ranking problem: at ccN1, perfect truth-based ordering buys +0.00026 of
efficiency over the head's own logit. The 235 lab-sims between ccN1 (826) and no-dedup
(1061) live in candidates whose MDs are ALREADY OWNED, a population that is 91-99%
duplicates -- there is no per-object or shared-hit quantity in the event that separates
them, because "a duplicate of a real track" is structurally identical to "a distinct
track that happens to share an MD".

## A6 -- ALSO ESTABLISHED
 * r_off (class OFF) fake is .04905 and dup .05254 -- BOTH ALREADY OUTSIDE the gate. The
   pT3 class must therefore DILUTE them: LST's rows add 74.3 pt>0.9 rows/evt at ~0% fake
   and 3.9% dup, which is what pulls .04905 -> .04636 and .05254 -> .05188.
 * The per-object T3 quality gate (-T3Q, t3_fakeScore) STRICTLY DOMINATES raising the
   attach margin: at th6+fs<=0.1 the pool is 1027 cand/evt at fake .0408, against th7
   alone at 907 cand/evt and fake .0418. It is the single most useful new knob.

## A7 -- THE DEDICATED BARE-T3 HEAD IS A LARGE PURITY WIN (TASK-2 work, done anyway)
Built even though TASK 1 says our matching is not worse, because the shipping r2 head was
selected on a CHAIN-pair metric and carries no T3 quality and only single-point pair
residuals.
  NEW: AttachT3Extra.h/.cc (12 extra slots: t3 fake/prompt/displaced scores, log10 T3
       radius, pixel-vs-triplet radius asymmetry, and rphi/rz helix-residual RMS+max of
       the pLS helix against ALL THREE T3 anchors -- the rPhiChiSquared / rzChiSquared
       analogues LST leans on), AttachT3Inference.{h,cc}, attach_t3_mlp_weights.h,
       gen_a_ref/ga_train_t3.py (trains + exports in one step).
  The 19-slot kAttachFeat contract is UNTOUCHED, so the chain (pT5-class) head and the
  frozen pT5 line cannot be perturbed. Flag -T3H 1; default 0 = today's behaviour.
  Training: 300-evt bare-T3 pairdump with the extra block (-PDT 1 -PDS 400), 20.4M rows,
  EVENT-level split ASSERTED == the frozen test-60, selection on weighted TPR@FPR=1e-4
  (the regime the delivery actually operates in; global AUC is useless at 1e-5 acceptance).
  TEST(frozen 60): AUC .99942  TPR@1e-4 .3842  TPR@1e-3 .8524.
LAB A/B at matched efficiency, ccN1 ownership, 100 evts:
  r2 head th6.0 : 145.5 rows/evt  eff .80723  rowFake .1984  fake .05854
  T3 head th6.53: 131.5 rows/evt  eff .80684  rowFake .0604  fake .04967
  => 3.3x cleaner rows at the same efficiency. The head IS the right place for the T3
  quality information; the explicit -T3Q gate is the cheap version of the same idea.

## A8 -- TIER-2 IS UNSALVAGEABLE (the last escape route, closed)
Two-tier ownership ("a candidate with owned MDs may still be delivered if it clears a much
higher bar") was implemented and measured. Over 220 events:
  tier 1  nClMd == 0 : 1443.7 cand/evt, 87.1% fake,  2.9% dup, 793 new-sim candidates
  tier 2  nClMd >= 1 : 2396.5 cand/evt, 54.7% fake, 44.4% dup, 248 new-sim candidates
  best tier-2 selector (logit > 10.57): 71.9 rows/evt at 98.9% DUPLICATE, 14 new-sim
  candidates in 220 events. t3_fakeScore, the head logit and their conjunction all fail
  the same way -- every tier-2 selection is 83-99% duplicates.
=> there is no per-object or shared-structure quantity that separates "duplicate of a
track we already have" from "distinct track sharing one MD". The 235-sim gap is real.

## A9 -- NO-OP GATE (PASS)
All GEN-A additions (-QD, -T3Q, -T3PS, -CCW, -T3H, the extra feature block, the second
head) OFF by default: 30 evts against the frozen P25 binary, 30/30 per-event counter
lines IDENTICAL, scoreboard eff .80108 dup .05317 fake .03738 nTC 53716 == the prior
agent's NOOP2 numbers exactly.

## A10 -- C++ FINALIST MATRIX (300 evts each, all with -RT3 1 -CF 1 -CFC 1)
Common dedup: -RDT 1 (pixel seed-family) + -CC 1 -CCG 1 -CCN 1 -CCP 1 (MD-granularity
ownership map, pre-claimed from everything already delivered) + -CCR 2.

  tag  head  -AT3  -T3Q      eff     dup    fake     nTC  deliv/e   gate
  P25BASE (LST's pT3 rows)  .81303  .05188  .04636  614277    -      ref
  r_off   (class OFF)       .77432  .05254  .04905  579968    0      --
  g_a    r2   6.0   0.1     .80644  .04835  .04985  610954  121.0    .D.
  g_c    r2   6.0   0.02    .80214  .04850  .04824  607637  108.2    .D.
  g_d    r2   7.0   0.1     .80337  .04965  .04834  605091   95.4    .D.
  g_f    r2   6.0   0.1 +CCW.80640  .04829  .04984  610913  121.0    .D.   (weld: 4.9/evt, wash)
  g_g    r2   6.0   0.1 hitG.80636  .04817  .04983  610872  120.7    .D.   (wash)
  h_a    T3   6.5   --      .80868  .04840  .04923  611376  124.8    .D.   <== BEST
  h_e    T3   6.5   0.1     .80772  .04844  .04837  610216  119.8    .D.
  h_d    T3   8.0   --      .80416  .05026  .04768  603614   89.5    .D.
  h_b    T3   4.0   --      .80693  .04566  .07196  626884  194.4    .D.
  h_c    T3   2.0   --      .80219  .04218  .12197  655871  309.7    .D.
  h_f    T3   0.0   --      .79613  .03816  .19062  701047  484.1    .D.
  k_a    T3   3.0   0.05    .80513  .04598  .05708  615718  154.4    .D.
  k_b    T3   4.5   0.05    .80640  .04711  .05159  613353  138.9    .D.
  k_d    T3   5.0   0.1     .80776  .04754  .05152  614107  140.1    .D.
  k_c    T3   6.5   0.1 -RPS 0 .80811 .06309 .04818 615919  119.8    ...  (-RPS 0 blows dup)

READINGS
 * DUP IS SOLVED. Every ownership-dedup configuration lands at .042-.050, comfortably
   inside the .052 gate and BELOW P25BASE's .05188. The MD-granularity map with the
   pre-claim is the right rule and the right granularity.
 * EFFICIENCY IS NOT MONOTONE IN VOLUME. -AT3 6.5 -> 0 raises deliveries 124.8 -> 484.1
   and LOWERS efficiency .80868 -> .79613, because -RPS retires a carried bare-pLS row for
   EVERY seed with T3 evidence above the margin, not only for owners. -RPS 0 recovers a
   little efficiency but costs .015 of duplicate rate (k_c), so it is not a way out.
 * THE DEDICATED T3 HEAD IS WORTH +.0022 eff AND -.0006 fake at matched settings
   (h_a .80868/.04923 vs g_a .80644/.04985), and 3.3x cleaner rows in the lab A/B.
 * -CCW (weld group) and hit granularity are BOTH washes -> dropped for simplicity.
 * BEST = h_a. Gate: dup PASS, fake miss by .0022, eff miss by .0044.

## A11 -- FINAL FRONTIER AND RECOMMENDED CONFIGURATION

  tag   head -AT3 -T3Q      eff     dup    fake     nTC  deliv/e   miss(eff/dup/fake)
  h_a    T3   6.5   --    .80868  .04840  .04923  611376  124.8   -.0044 / PASS / +.0022
  m_a    T3   6.5  0.3    .80851  .04843  .04866  610898  122.5   -.0045 / PASS / +.0017
  h_e    T3   6.5  0.1    .80772  .04844  .04837  610216  119.8   -.0053 / PASS / +.0014
  h_d    T3   8.0   --    .80416  .05026  .04768  603614   89.5   -.0089 / PASS / +.0007
  m_b    T3   8.0  0.1    .80333  .05027  .04763  603266   88.2   -.0097 / PASS / +.0006
Efficiency and fake trade off SMOOTHLY and monotonically along the delivery margin; the
duplicate gate is satisfied everywhere. Driving fake down to the gate costs ~.005 of
efficiency and STILL does not reach .047, because r_off (class OFF) is already at .04905.

RECOMMENDED: m_a
  -RT3 1 -CF 1 -CFC 1 -T3H 1 -AT3 6.5 -T3Q 0.3 -RDT 1 -CC 1 -CCG 1 -CCN 1 -CCP 1 -CCR 2
  eff .80851 | vxy01 .84146 | v15 .79505 | v510 .72871 | v1030 .71814
  d15 .55901 (521/932) | d510 .24912 (71/285) | dup .04843 | fake .04866 | nTC 610898
  vs P25BASE: eff -.00452, dup -.00345 (BETTER), fake +.00230.
  DISPLACED NOT DEGRADED: d510 EXACTLY held at 71/285; d15 differs by ONE track (521 vs
  522 of 932) and that one track is lost by -RT3 1 itself (r_off is also 521/932), not by
  any GEN-A choice; v510 and v1030 are both ABOVE P25BASE.

GATE VERDICT: FAIL. dup PASSES (.04843 <= .052, and better than baseline). eff misses by
.0045 and fake by .0023.

## A12 -- THE FAKE GATE IS UNREACHABLE, AND THE CURVE PROVES IT
Sweeping the delivery margin with the T3 head traces the whole fake curve:
  -AT3    deliv/e     eff      fake
   0.0      484.1   .79613   .19062
   2.0      309.7   .80219   .12197
   4.0      194.4   .80693   .07196
   6.5      124.8   .80868   .04923
   8.0       89.5   .80416   .04768
   9.0       60.9   .79841   .04783
   (0 deliveries = r_off)         .04905
Fake has a MINIMUM of ~.0476 around 89 deliveries/evt and rises on BOTH sides: too few
rows cannot dilute r_off's .04905, too many bring their own fakes. .047 is never reached.
P25BASE gets .04636 because LST's rows are ~0% fake AND their presence removes 183
pre-existing fakes (frNum 22225 -> 22042 while adding 22302 rows), a second-order coupling
through the seed-family suppression that our replacement does not reproduce.
