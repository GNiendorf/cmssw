# GEN-B STATUS (durable milestone log)

Workspace: standalone/gen_b_ref/    Code tree: standalone/protoB/  (copy of prototype/)
Gate P25BASE: eff .81303 | vxy01 .84610 | v15 .80283 | v510 .72713 | v1030 .71732 |
d15 .56009 | d510 .24912 | dup .05188 | fake .04636 | nTC 614277.
PASS: eff >= .81303, dup <= .052, fake <= .047, displaced bands not degraded.

## B0 -- ORIENTATION (done)

Read STATUS.md + STATUS2.md. Key facts inherited (not re-derived):
 * -CC family real surface = -CCG (1 MD / 0 hit) -CCN <n> -CCP -CCK -CCR; -CCT is dead text.
 * Round-1 dedup matrix result: dup is solvable, eff and fake are not, at -AT3 6.
 * LST pT3 rows retired by -RT3 1 = 151.7/evt (POST-cleaning; those are the TC rows).
 * `-CF 1 -CFC 1` is output-identical and 19% faster -> campaign default.

NEW FACTS I ESTABLISHED IN B0 (from the input ntuple directly):
 * LST's PRE-cleaning pT3 candidate count is 516.8/evt (300-evt file, 30-evt sample),
   fake fraction 0.0550 by LST's own ntuple truth. The post-clean TC count is 151.7/evt.
   So LST's own crossclean throws away 71% of its candidates.
 * pT3_isDuplicate is 1 for EVERY row in this ntuple -> useless, do not use it.
 * pT3_plsIdx / pT3_t3Idx are in the SAME index spaces as our pLS rows and t3 rows, so
   LST's pT3 set and our bare-T3 delivery set are directly comparable PAIR BY PAIR.

## B1 -- MONOTONICITY LEMMA (proved from AttachDelivery.cc; drives the whole measurement)

The stage-B owner set at margin theta is EXACTLY {owners at theta = -inf with logit >= theta}.
Proof: per target the pLS pick is argmax over pLS of the pair logit, independent of theta
(theta only gates whether the target bids). In resolveContention a loser gets NOTHING (no
fallback). Lowering theta can only add bidders whose per-target-best logit is BELOW theta,
i.e. below every incumbent bidder, so no incumbent can lose a pLS to a newcomer.
=> ONE run at a very low -AT3 with a per-delivery logit dump gives the ENTIRE threshold
curve offline. No 13-min job per threshold point.

## B2 -- TASK 1 VERDICT (300 evts, COMPLETE)   [gen_b_ref/task1_full.txt, ceiling_full.txt]

Instrument: protoB `-PT3C <path>` dumps EVERY stage-B delivery at -AT3 -6 (whole universe)
and EVERY LST pT3 candidate row, both matched with the SAME ported >0.75 matcher.
`gb_task1.py` (count/purity/coverage curves) and `gb_ceiling.py` (unique coverage against a
FIXED background R = the non-pT3 rows of P25BASE) do the offline scan.
MONOTONICITY VERIFIED NUMERICALLY: dump logit >= 6 gives 2262 deliveries in evts 0-2,
identical to d_none's 608+809+845. One run = the whole threshold curve.

THE THREE NUMBERS THAT MATTER, all against the SAME background R:
  LST pre-clean candidate set  588.7 rows/evt  fake .0477  cov 4.60  UNIQ 4.49
  LST post-clean (its real WP) 151.7 rows/evt  fake .0345  cov 4.55  UNIQ 4.45
  OURS at matched count 588.7  588.7 rows/evt  fake .0167  cov 15.9  UNIQ 2.43(@th 8.0)
  OUR CEILING (any theta)     3804 rows/evt   fake .6651  cov 33.1  UNIQ 4.77

VERDICT (global): our MATCHING is NOT worse -- it is better on every axis a matcher owns:
  * PURITY at matched count: .0167 vs LST .0477  (2.9x cleaner)
  * COVERAGE at matched count: 15.9 vs 4.60 in-cut sims/evt (3.5x)
  * CEILING: 4.77 unique sims/evt reachable vs LST's 4.49 pre-clean / 4.45 delivered
  * RECALL of the specific sims LST uniquely delivers: 90.1%
What is worse is RANKING FOR UNIQUENESS. Our logit scores pair COMPATIBILITY; a duplicate
of an already-delivered track is a perfectly compatible pair, so thresholding cannot
separate "adds a track" from "repeats one". At LST's row count we realise 2.43 of the 4.77
we can reach (51%); to realise 89% we must sit at theta 6 = 1133 rows/evt.
=> THE DEFICIT IS DEDUP, NOT MATCHING. LST's CrossCleanpT3 converts 588.7 candidates into
151.7 rows that are 98% unique (4.55 cov -> 4.45 unique). We have no equivalent.

PER REGION (recall of LST-unique by our universe / LST-unique per evt):
  barrel  90.6% (2.82)   transition 85.7% (0.89)   endcap 93.7% (0.74)
PER pT: 0.9-1.5 93.6% (1.98) | 1.5-3 87.9% (1.41) | 3-10 87.2% (0.86) | 10+ 84.1% (0.21)
Purity at matched count is better than LST in EVERY region and EVERY pT band
(barrel .0293 vs .0387, transition .0224 vs .0442, endcap .0132 vs .0739;
 pt bands .0189/.0143/.0107/.0540 vs .0500/.0479/.0402/.1392).
NO region or pT regime where our matching is worse. The only sub-100% recall is the
transition band (85.7%), worth 0.13 sims/evt = 0.0007 eff -- not the blocker.

THE ACTUAL BUDGET: the pT3 class is worth 4.45 unique sims/evt = .0243 of efficiency by
this measure, and r_off (-T3E 0, class deleted) measures .81303 -> .77432 = .0387.
Target for any full solution: recover ~4.45 unique sims/evt with ~150-250 rows/evt.

THRESHOLD CURVE (rows/evt, fakefrac, UNIQ/evt, % of ceiling):
  th 4  1617 .2501 4.67 98% | th 6 1133 .0802 4.27 89% | th 7  912 .0403 3.63 76%
  th 8   643 .0193 2.43 51% | th 9  373 .0093 0.99 21% | th 10 160 .0046 0.30  6%

## B3 -- TASK 2 ANSWER: THE HEAD IS NOT THE PROBLEM (offline, 300 evts, event-split)

`gb_combo.py`: the attach head's ONLY object-quality slot (feature 11 chainGateLogit) is
hard-wired to 0 for a bare T3 (PixelAttach.cc makeT3Pre line "no chain gate exists for a
bare T3"), so the head genuinely has NO input describing whether the 3-layer object is
real. The T3's own DNN scores are that input.
  AUC(real vs fake) on the delivery population: attach logit .9698 | t3_promptScore .9048
  | t3_fakeScore .0883 (= .9117 inverted) | t3_displacedScore .4470 (useless)
  Logistic combination on a HELD-OUT event half: AUC .9698 -> .9767, and at FIXED row
  count it roughly HALVES the fake fraction (e.g. 600 rows/evt .0167 -> .0081).
BUT -- the finding that changes the plan -- ranking better for "is it real" makes UNIQUE
coverage WORSE at fixed row count (600 rows: UNIQ 1.727 -> 1.393; 900 rows: 2.913 ->
2.727). PURITY AND UNIQUENESS ARE ANTI-CORRELATED here: the cleanest 3-layer objects are
exactly the ones the chains already reconstructed; the rescues are the marginal ones.
=> RETRAINING THE HEAD FOR PURITY CANNOT BUY EFFICIENCY. It buys fake rate only.
   Adopted instead: use the T3's own quality as a TARGET-SELECTION GATE (-QP / -QF) --
   the missing object-quality signal supplied where it helps (fakes) and nowhere else.
   Measured offline at theta 6: promptScore >= 0.2 cuts fakes 90.8 -> 36.7 per event
   (-60%) for 0.165/evt of unique coverage (-5%). promptScore >= 0.1 cuts to 46.1 for
   -0.075 unique.

## B4 -- CONSTRAINT ARITHMETIC (calibrated, gb_opt.py)
Background r_off (class deleted): eff .77432 nTC 1933.2 fake .04905 dup .05254.
  eff  = .77432 + UNIQ/79.3     (D fitted on P25BASE; checks to +-.003 on 8 runs,
                                 systematically +.0023 optimistic because our deliveries
                                 also retire carried bare-pLS rows -- the -RPT fix below)
  fake = (94.8 + fakes/evt) / (1933.2 + rows/evt)
  dup  = NOT linear in rows (one duplicate row flags TWO rows); rows/evt is the proxy.
TARGET: UNIQ >= 3.07/evt (what LST's carried pT3 rows deliver) at rows/evt low enough for
dup <= .052 -- empirically rows <= ~150 (d_n1 135.5 -> .0468, d_otn1 146.2 -> .0524,
d_both 181.5 -> .0715).
BEST EXISTING RULES AT THAT ROW COUNT REACH UNIQ 2.61-2.64. THE GAP IS ~0.45 UNIQ/evt.

## B5 -- THREE NEW MECHANISMS BUILT (protoB, all flag-gated, NO-OP GATE PASS)
NO-OP: 30 evts, frozen flag line, per-event counter lines BYTE-IDENTICAL to the frozen
P25 binary (t3attach_ref/bin/chainproto_p25).
  -RPT <0|1>  the bare-T3 term of the -RPS carried-pLS drop predicate. 0 = legacy ("the
              seed had ANY scored pair above the class margin"), 1 = OWNERSHIP ("a
              delivery actually owns it"). The legacy form was written for a TIGHT margin;
              at a loose one it retires essentially every seed with any bare-T3 candidate
              and destroys the bare-pLS class. This is the measured ~.0023 eff tax.
  -QP <c> / -QF <c>  target-quality gate on bare-T3 targets (t3_promptScore >= c /
              t3_fakeScore <= c), applied at TARGET SELECTION so a junk T3 never competes
              for a pixel seed a good target could use. One per-object comparison, no pair
              loop, no proximity term.
  -RDC <0|1>  PIXEL CLAIM-UNIVERSE UNIFICATION. The stage-B seed-family map holds only the
              seeds the ATTACH kept, so a delivery may sit on a seed family that a
              SURVIVING CARRIED pixel row (type 7/8) already delivers. -RDC 1 pre-loads the
              map with those seeds. Identical ownership-map operation, identical >= 2
              shared pixel hits rule -- the pixel-side analogue of the M15 OT claim-universe
              unification, which is exactly the missing half.
  -CCNP <n>   splits the OT ownership count: -CCNP against the PRE-CLAIM map (already
              delivered TCs), -CCN against EARLIER DELIVERIES. Motivated by SIZE ASYMMETRY:
              a 3-MD object is redundant against a 5-7-layer chain only when the chain
              holds ALL THREE of its MDs; two 3-MD objects are the same track at TWO.

## B6 -- BATCH 1 (300 evts each): THE NEW MECHANISMS WORK   [gb_board.py]
All on `-RT3 1 -CF 1 -CFC 1 -RPT 1 -RDT 1 -RDC 1`.
tag  extra flags                         eff     dup    fake   deliv
B1   -AT3 6 -CC 1 -CCN 2 -CCNP 3 -QP .1  .81096  .07744 .05293 148.7
B3   -AT3 5 -CC 1 -CCN 2 -CCNP 3 -QP .2  .81246  .07819 .05792 167.5
B4   -AT3 6 (no OT dedup) -QP .1         .81685  .28529 .05264 416.5
B5   -AT3 6 -CC 1 -CCN 1 -CCNP 3 -QP .1  .80776  .05376 .05016 113.7
B6   -AT3 6 -CC 1 -CCN 2 -CCNP 2 -QP .1  .81096  .07744 .05293 148.7  (== B1: CCNP 2 vs 3
                                                                       never binds at MD)
PRIOR BEST AT COMPARABLE ROW COUNT: d_both 181.5 rows eff .80815 dup .07151 fake .06396.
B1 delivers +.0028 eff and -.011 fake at 33 FEWER rows. The mechanism stack is real.

DECOMPOSITION OF WHAT IS LEFT (duplicate FLAGS per event, r_off base = 101.6):
  LST's 151.5 pT3 rows add   +4.6 flags -> ~2 duplicate rows of 151.5  = 1.5% duplicates
  B5's   113.7 rows add      +8.4 flags -> ~4 duplicate rows of 113.7  = 3.7%
  B1's   148.7 rows add     +59.6 flags -> ~30 duplicate rows of 148.7 = 20%
So the last 35 rows between B5 and B1 are ~85% duplicates, and they carry +.0032 eff.
THE REMAINING PROBLEM IS EXACTLY THIS: the rows that still carry unique tracks are mixed
1:5 with rows that duplicate an already-delivered track, and no per-object or
ownership-count quantity we have separates them (measured, see B7).

## B7 -- UNIQUENESS-RANKING HEADROOM (offline, held-out event half)
Trained a model whose TARGET is "this delivery covers an in-cut sim the pT3-less pipeline
misses", on inference-time features only (logit, T3 DNN scores, |eta|, pt, and the two
OWNERSHIP-MAP counts nPre/nCur). Ranking by it instead of by the attach logit:
  150 rows/evt: UNIQ 1.233 -> 2.960   200 rows/evt: 1.753 -> 3.140
i.e. the information IS there and the ownership count is the 2nd strongest feature
(logistic coefficients: logit +2.14, nPre -1.26, lnPrompt +0.89, lgPlsPt +0.75,
|eta| -0.68). BUT the fake cost is prohibitive: at 150 rows the uniqueness ranker takes
13.4 fakes/evt against LST's 5.2. Under a purity floor (logit >= theta AND
promptScore >= q) that keeps the fake budget payable, the frontier tops out at
UNIQ 2.813 / 260 rows / 6.2 fakes -> predicted eff .8098.
=> A LEARNED UNIQUENESS HEAD DOES NOT CLOSE THE GAP EITHER. Purity and uniqueness are
   anti-correlated all the way down.

## B8 -- DUPLICATE ANATOMY: THE STRICT MD OWNERSHIP RULE IS THE ANSWER  [gb_dupanat.py]
For each pT3-class row with pt > 0.9, which TC type is the partner that makes it a
duplicate (rows/evt, duplicate rows/evt, and the partner breakdown):
  LST carried pT3   104.0 rows   2.14 dup (2.1%)  T5 .69 pLS .56 pT3 .53 pT5 .20 T4 .17
  B4 no OT dedup    331.2 rows 232.79 dup (70.3%) T5 157.5 pT5 63.1 pT3 21.5 T4 6.3
  B1 -CCN 2         106.4 rows  23.63 dup (22.2%) T5 12.2 pT5 8.2 pLS 2.0
  B5 -CCN 1          77.0 rows   3.03 dup (3.9%)  pLS 1.8 T5 .65 pT5 .49
=> -CCN 1 at MD granularity (ANY already-claimed MD kills the delivery) brings our
   duplicate fraction to 3.9% against LST's 2.1%, and it does it by removing exactly the
   T5/pT5 partners -- i.e. the ownership map IS the correct and sufficient mechanism for
   the chain-overlap half. The residual is 1.8/evt bare-pLS partners: carried type-8 rows
   of a DIFFERENT seed family that happen to reconstruct the same track. That is the one
   place LST uses its dR crossclean, and it is worth ~.0017 of dup.
   With -CCN 1 the remaining deficits are EFFICIENCY (-.0053) and FAKE (+.0032), not dup.

## B9 -- BATCH 2 (300 evts): -CCN 1 CAPS dup, theta AND -RPT ARE THE REMAINING DIALS
All on `-RT3 1 -CF 1 -CFC 1 -RDT 1 -RDC 1 -CC 1 -CCN 1`.
tag  extra                          eff     dup    fake   deliv
B5   -RPT 1 -AT3 6 -QP .1           .80776  .05376 .05016 113.7
C1   -RPT 1 -AT3 5 -QP .2           .80908  .05365 .05303 126.3
C2   -RPT 1 -AT3 4 -QP .35          .80851  .05334 .05719 136.4
C6   -RPT 1 -AT3 3 -QP .5           .80666  .05306 .06171 144.4
C5   -RPT 1 -AT3 5 -QP .2 -CCG 0    .80829  .05294 .05243 122.1
C7   -RPT 0 -AT3 5 -QP .2           .80284  .04604 .05297 126.3
C3/C4 (-CCNP 2 / 1) are BIT-IDENTICAL to C1: at -CCN 1 the nCur >= 1 test subsumes any
nPre test, so -CCNP only has meaning at -CCN >= 2. Recorded so nobody re-runs it.
READINGS
 * dup is SOLVED and stable at ~.0530-.0537 across the whole theta range -- the ownership
   map, not the threshold, is what controls it.
 * -RPT is a hard eff/dup trade: 1 -> 0 costs .0062 of eff and buys .0076 of dup.
 * Lowering theta below 5 buys nothing (+.0007 eff from 6->4) and costs .007 of fake.
 * BEST SO FAR C1: eff -.00395, dup +.00165, fake +.00603 vs the gate.

## B10 -- THE -RT3 CLAIM TAX (found by arithmetic, fixed by -RT3P)
r_off (class deleted) has nTC 1933.2 and 94.83 fakes/evt; P25BASE's NON-pT3 rows are
1896.1 with 89.7 fakes. So merely RETIRING the carried pT3 rows adds 37.1 chain rows/evt
carrying 5.1 FAKES/evt -- because a retired row stops pre-claiming its outer-tracker hits,
so the K9 greedy walk gets looser and admits chains the baseline priced out. That tax is
~.0025 of fake rate and is nothing to do with the pT3 class.
FIX `-RT3P <0|1>`: a type-5 row retired by the WHOLESALE -RT3 rule stays in the PRE-CLAIM
(it is still removed from the output). Justification: the class is being REPLACED, not
deleted -- the hits stay owned by the pT3 class, only the provider changes. The chains then
face exactly the baseline claim.
Also added `-RPT 2`: retire a carried type-8 row whose seed is in the same SEED FAMILY
(>= 2 shared pixel hits) as a delivery owner -- the measured residual duplicate partner
(1.8/evt) is precisely a sibling seed of a seed we just delivered. One seed family, one
row, the rule the algorithm already uses, applied to retirement. Ownership-map shaped
(pixel hit -> owner, then one pass over seeds); never a pLS x pLS loop.
NO-OP GATE re-verified after both: 30/30 per-event lines byte-identical to the frozen p25.

## B11 -- BATCH 3 (300 evts): -RT3P AND -RPT 2 CONFIRMED, DISPLACED BANDS RESTORED
E7 = `-RT3 1 -RT3P 1 -T3E 0` (the class deleted, but the retired rows still pre-claim):
  nTC 1898.4/evt (vs r_off 1933.2 -- the 37 extra chain rows ARE gone, exactly as the
  arithmetic predicted) and fake .04716 vs r_off .04905. The claim tax is real and -RT3P
  removes it. It also costs .0159 of baseline efficiency, i.e. those 37 rows carried 1.26
  unique sims/evt -- but in P25BASE the SAME chains are priced out and LST's pT3 rows
  cover those tracks instead, so the comparison that matters is end to end.
All on `-RT3 1 -RT3P 1 -CF 1 -CFC 1 -RDT 1 -RDC 1 -CC 1 -CCN 1`:
tag  extra                    eff     vxy01   v15     v510    d15     dup    fake   deliv
E1   -RPT 1 -AT3 5 -QP .2     .80973  .84312  .78657  .71609  .56009  .05384 .05094 159.1
E2   -RPT 2 -AT3 5 -QP .2     .80973  .84312  .78657  .71609  .56009  .05324 .05089 159.1
E3   -RPT 2 -AT3 5 -QP .35    .80780  .84132  .78233  .71293  .56009  .05315 .04902 153.7
E4   -RPT 2 -AT3 4 -QP .35    .80842  .84194  .78304  .71451  .56009  .05291 .05514 169.8
E5   -RPT 2 -CCN 2 -CCNP 1    .80973  .84312  .78657  .71609  .56009  .05431 .05113 160.9
E6   -RPT 2 -AT3 6 -QP .1     .80780  .84108  .78587  .71136  .56009  .05334 .04798 144.9
READINGS
 * -RT3P 1 RESTORES THE DISPLACED BANDS EXACTLY: d15 .56009 and d510 .24912 and v1030
   .71732 == P25BASE, where every earlier configuration sat at d15 .55901. That is a real
   physics win and it comes from the chains facing the baseline claim again.
 * -RPT 2 (seed-family retirement) buys .0006 of dup over -RPT 1 for zero efficiency.
 * -CCNP 1 at -CCN 2 (E5) reproduces -CCN 1 exactly: the pre-claim map is what binds.
 * -RT3P 1 COSTS the vxy[1,5) and vxy[5,10) bands (.78657/.71609 vs P25BASE
   .80283/.72713). Those chains it prices out were carrying mid-displacement tracks.
 * BEST FAKE E6 .04798 (+.001 over gate); BEST EFF E2 .80973 (-.0033).

## B12 -- BATCH 4/5 (300 evts): -AFB IS A DEAD LEVER, -RDC IS ~NEUTRAL
tag  config                                                     eff     dup    fake   deliv
E2   RT3P1 RDC1 CCN1 RPT2 AT3 5 QP .2                          .80973  .05324 .05089 159.1
F1   E2 + -AFB 3                                               .80973  .05401 .05108 160.5
F2   E6 + -AFB 3                                               .80780  .05407 .04804 145.9
F4   no -RT3P, CCN1 RPT2 AT3 5 QP .2 AFB 3                     .80908  .05372 .05315 127.6
F5   RT3P1 RDC1 CCN1 RPT2 AT3 6 QP .2 QF .15 AFB 4             .80684  .05424 .04650 141.6
F6   E2 + -AFB 3, -RDC 0                                       .80899  .05265 .05180 173.2
F7   no -RT3P, CCN1 RPT2 AT3 6 QP .1 AFB 3                     .80776  .05380 .05016 114.6
READINGS
 * -AFB (contention fallback) IS WORTH NOTHING: F1 vs E2 and F2 vs E6 are identical in
   eff to 5 decimals and cost .0008 of dup. A bare-T3 target that loses its best seed
   has no second seed that is both free and above the margin. Measured, not assumed --
   record it so nobody rebuilds it.
 * -RDC (pixel claim-universe unification) is ~NEUTRAL and slightly dup-NEGATIVE:
   F6 (-RDC 0) has dup .05265 vs F1 .05401 and eff .80899 vs .80973 at 13 more rows.
   The seed families it blocks were already blocked by the OT ownership map.
 * F5 IS THE FIRST POINT TO PASS THE FAKE GATE: fake .04650 <= .047. It fails on
   eff (-.0062) and dup (+.0022).
 * The duplicate fraction of our rows is now 3.5% against LST's 2.1%, and the whole
   residual is 2.1/evt carried BARE-pLS partners -- a different seed family reconstructing
   the same track, the one thing that has no shared-structure signature at all.

## B13 -- BATCH G/H (300 evts): THE FRONTIER, AND IT DOES NOT REACH THE GATE
All `-RT3 1 -RT3P 1 -CF 1 -CFC 1 -RDT 1 -CC 1 -CCN 1 -RPT 2` + the listed knobs.
tag  RDC  AT3   QP    QF     eff     vxy01   v15     v510    dup     fake    deliv
G2    1   5     -     .2    .80991  .84317  .79223  .71767  .05407  .05356  166.2
E2    1   5    .2     -     .80973  .84312  .78657  .71609  .05324  .05089  159.1
H1    0   5    .2    .2     .80881  .84208  .78869  .71609  .05236  .05012  168.6
G3    1   5.5  .25    -     .80837  .84184  .78375  .71451  .05397  .04824  150.8
H3    0   5.5  .25   .2     .80789  .84123  .78587  .71451  .05241  .04787  159.2
E6    1   6    .1     -     .80780  .84108  .78587  .71136  .05334  .04798  144.9
G1    1   6    .15    -     .80763  .84090  .78587  .71136  .05407  .04757  144.7
F5    1   6    .2    .15    .80684  .84019  .78445  .71136  .05424  .04650  141.6
H2    0   6    .2    .15    .80675  .84009  .78445  .71136  .05250  .04670  150.3
H4 (-CCK 1, pLS-pt sweep order) == E2 in every band: the keep-best key is not where the
physics is, confirming the earlier -CCK finding at the new operating point.
THE FRONTIER IS LINEAR AND SHALLOW: d(eff)/d(fake) ~= 0.46 over the whole family
(.04670/.80675 -> .05356/.80991). Extrapolating to eff .81303 needs fake .0604.
THE GATE IS NOT REACHABLE BY THIS FAMILY. Best simultaneous point H2:
  fake .04670 PASSES, dup .05250 misses by .0005, eff .80675 misses by .00628.

## B14 -- FINAL FRONTIER (300 evts). NOMINATED CONFIGURATION: H3
`-RT3 1 -RT3P 1 -CF 1 -CFC 1 -RDT 1 -RDC 0 -CC 1 -CCN 1 -RPT 2 -AT3 5.5 -QP 0.25 -QF 0.2`
              eff     vxy01   v15     v510    v1030   d15     d510    dup     fake    nTC
P25BASE      .81303  .84610  .80283  .72713  .71732  .56009  .24912  .05188  .04636  614277
H3           .80789  .84123  .78587  .71451  .71732  .56009  .24912  .05241  .04787  613361
delta        -.00514 -.00487 -.01696 -.01262  .00000  .00000  .00000 +.00053 +.00151  -916
 deliveries 159.2/evt (LST retires 151.7) | dup fraction of our rows 3.5% (LST 2.1%)
ALTERNATIVES ON THE SAME FRONTIER
  H2 (-AT3 6 -QP .2 -QF .15)  eff .80675 dup .05250 fake .04670  <- FAKE GATE PASSES
  I4 (-RDC 0 -AT3 5.25)       eff .80855 dup .05239 fake .04904
  I2 (no -RT3P, -AT3 5.5)     eff .80785 dup .05233 fake .05005, and it HOLDS the vxy
                              bands (v15 .79152 v510 .72713 v1030 .71814) at the cost of
                              d15 (.55901). -RT3P trades vxy[1,10) for dxy[1,5).
VERDICT: NO CONFIGURATION PASSES. dup and fake are at or within .001-.002 of the gate;
EFFICIENCY IS SHORT BY .005. Every knob on this frontier trades eff against fake at a
slope of 0.46, so the remaining .005 of eff costs .011 of fake -- it cannot be bought
here. The gap is structural and is stated in the deliverable.

## B15 -- WHERE THE REMAINING DEFICIT LIVES (H3 vs P25BASE, per region)
  eff barrel     .92474 -> .91405  (-.01069)   <== the whole deficit is here
  eff transition .88135 -> .87860  (-.00275)
  eff endcap     .75425 -> .75303  (-.00122)
  dup barrel/transition IMPROVE (-.0007/-.0006); dup endcap +.0014
  fake barrel +.0018, transition +.0041, endcap +.0006
That matches Task 1 exactly: 2.82 of LST's 4.45 unique sims/evt are BARREL, our universe
reaches 2.87 of them (90.6% recall), and the barrel is where 3-layer objects sit inside
the densest already-claimed MD population -- so the -CCN 1 ownership veto that fixes dup
is hardest exactly where the efficiency is.

## B16 -- MECHANISM LEDGER (what I added, all flag-gated, defaults = frozen)
KEPT (in the nominated configuration)
  -RT3P 1  retired carried pT3 rows keep pre-claiming in K9   [+.0019 fake, restores
           d15/d510/v1030 to baseline exactly, costs vxy[1,10)]
  -CC 1 -CCN 1   MD-granularity ownership veto, ANY already-claimed MD kills   [the dup
           mechanism: our duplicate row fraction 70% -> 3.5%, LST's is 2.1%]
  -RPT 2   -RPS predicate = ownership, plus seed-family retirement of carried type-8 rows
           [+.0062 eff over -RPT 0, and -.0006 dup over -RPT 1]
  -QP/-QF  T3 object-quality target gate    [-60% of our fakes for -5% of our unique]
  -RDC 0   pixel claim-universe unification MEASURED NEUTRAL-TO-NEGATIVE: leave it OFF
DROPPED (measured worthless, recorded so nobody rebuilds them)
  -AFB k   contention fallback: 0.00000 eff change, +.0008 dup
  -CCNP n  meaningless at -CCN 1 (nCur >= nPre); only meaningful at -CCN >= 2, where
           -CCN 2 -CCNP 1 reproduces -CCN 1 exactly
  -CCK 1   sweep order by pLS pt: identical in every band
  head retraining for purity: improves AUC .9698 -> .9767 and halves fakes at fixed row
           count, but LOWERS unique coverage -- purity and uniqueness are anti-correlated
DIAGNOSTIC ONLY (no physics path)
  -PT3C    per-delivery + per-LST-pT3-candidate truth dump (Task 1)
  -PT3D    per-delivery ownership-map counts (offline dedup-rule ranking)
