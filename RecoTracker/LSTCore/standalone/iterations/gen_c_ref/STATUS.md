# GEN-C STATUS (durable milestone log)

Workspace: standalone/protoC (my build), artifacts standalone/gen_c_ref/.
Runner: gen_c_ref/gc_run.sh  (identical flag line to t3attach_ref/ta_run.sh, my binary/dirs).
Scoreboards: gen_c_ref/ta_tab.py, gen_c_ref/ta_board.py (repointed to gen_c_ref).

## C0 -- SETUP + NO-OP GATE (PASS)

Build trap found and recorded: protoC MUST be built with the FULL env sequence
`source setup.sh; cmsenv; source setup.sh` (that is CMSSW_17_0_0_pre2 / ROOT 6.36).
Building with `source setup.sh` alone links CMSSW_14_2_0_pre4 / ROOT 6.30 and the binary
dies in TFile::Open with a Cling/libCore version mix.

CODE ADDED (harness-invisible instrument, all sentinels for every non-stage-B row):
  OutputWriter.h  OutTC::attachLogit / dbgT3Row / dbgPlsRow
  OutputWriter.cc branches tc_attLogit, tc_t3Row, tc_plsRow
  main.cc         fills them on the type-5 delivery only
NO-OP GATE, 300 evts, frozen P25BASE flag line:
  eff .81303 vxy01 .84610 v15 .80283 v510 .72713 v1030 .71732 d15 .56009 d510 .24912
  dup .05188 fake .04636 nTC 614277   == P25BASE EXACTLY. Instrument is free.

## C1 -- THE NUMBER THAT REFRAMES THE PROBLEM

`r_off` = `-RT3 1 -CF 1 -CFC 1 -T3E 0` (LST's pT3 rows retired, our stage B OFF, i.e. NO
pT3 class in the pipeline at all), 300 evts:
  eff .77432 | vxy01 .80555 | v15 .77173 | v510 .72240 | v1030 .71814
  d15 .55901 | d510 .24912 | dup .05254 | fake .04905 | nTC 579968
=> THE pT3 CLASS IS WORTH +0.03871 OF EFFICIENCY in the P25BASE pipeline. The replacement
has to recover 3.87 points, not the ~0.2 points the -AT3 6 A/B was showing. The best
dedup point measured so far (d_hit, eff .80385) recovers 2.95 points, i.e. it is still
0.92 points (~590 sims / 300 evts) short of what LST's pT3 rows deliver.

## C2 -- LST'S PRE-CLEANING pT3 SET IS 588.7 ROWS/EVT, NOT 151.7

Straight from the input ntuple's pT3_* branches (300 evts):
  176611 rows total = 588.7/evt | isFake 8419 = 0.0477 | simIdxAll empty == isFake exactly.
The campaign's "LST retires 151.7 pT3 rows/evt" is LST's POST-cleaning count (the
tc_type==5 rows that survive its dup flags + CrossCleanpT3). Dedup-free matching therefore
has to be compared at 588.7, and "matched count" has TWO meaningful reference points.

## C3 -- TASK 1 VERDICT (300 evts, all numbers from gen_c_ref/task1*_report.txt)

Method: one low-margin run (`c1_curve` = flagship + `-RT3 1 -CF 1 -CFC 1 -AT3 -8 -RDT 0`,
no dedup of any kind) instrumented with the per-delivery stage-B logit, re-thresholded
offline. The re-thresholding is EXACT (stage B is monotone in the margin) and was
VALIDATED against an independent real run: at T = 6 it reproduces 1132.7 rows/evt, the
`d_none` ledger value, to the decimal.

MATCHING (dedup ignored on both sides):
  at LST's PRE-CLEAN count (588.7 rows/evt)  ours fake .0167 vs LST .0477  (3x PURER)
                                             ours covers 15.6 in-cut sims/evt vs LST 4.4
  at LST's DELIVERED count (151.7 rows/evt)  ours fake .0044 vs LST .0348  (8x PURER)
  PER REGION, row count matched INSIDE each region (at LST's pre-clean count):
     barrel      ours fake .1555 vs LST .0388   <== ONLY REGIME WHERE WE ARE WORSE
     transition  ours fake .0402 vs LST .0444
     endcap      ours fake .0036 vs LST .0738   (20x purer)
  PER pT BAND (same treatment) ours is purer in EVERY band (.0181/.0056/.0039/.0566 vs
     LST .0500/.0479/.0402/.1392).
  ETA CALIBRATION IS BROKEN: one global margin gives barrel/trans/endcap = 72.6/93.5/419.7
  rows against LST's 322.4/139.0/127.3. Per-region thresholds for LST's own mix differ by
  4.6 logit units (barrel 5.44, transition 7.32, endcap 10.08).

MARGINAL VALUE (the number the gate depends on) -- in-cut sims/evt this class covers that
our pipeline WITHOUT it (r_off) does not:
  LST's pT3 rows are worth +2.94 to US (r_off .77432 -> P25BASE .81303).
  ours: T=6 -> 3.24 (1132.7 rows) | 6.5 -> 3.09 | 7 -> 2.82 | 8 -> 1.87 | 9 -> 0.68
  saturates at 3.46 (T=2, 2523 rows). So the CEILING clears the requirement, but only at
  ~1000 rows/evt, 6.6x LST's delivered count.

THE REAL GAP IS THE DEDUP, AND IT IS MEASURED:
  set                    rows/ev  distinct-sims  rows/dist  dupFrac  fakeFrac
  LST pre-clean pT3        588.7        147.1       4.00     0.9041   0.0477
  LST DELIVERED pT3        151.7        146.0       1.04     0.0232   0.0348
  ours @ T=6              1132.7        628.4       1.80     0.8255   0.0802
  ours @ T=10              159.7        108.9       1.47     0.9587   0.0046
LST's dedup goes 588.7 -> 151.7 rows, dupFrac .904 -> .023, KEEPING 146.0 of 147.1
distinct sims (99.3%). That is the thing we do not have.
DUPLICATE SOURCE DECOMPOSITION (per evt, T=6): 1132.7 rows = 884.4 duplicating a sim a
CHAIN TC already covers + 50.6 duplicating each other + 90.8 fake + 106.9 unique.
=> 78% of the class duplicates OUR OWN CHAINS. The bare-T3 mask only excludes T3s that
are MEMBERS of an accepted chain; every OTHER T3 of an already-reconstructed track (an
alternative hit on one layer, a triplet the weld did not use) stays "bare" and is
delivered.

VERDICT: our pLS->OT MATCHING is BETTER than LST's pT3 matching on purity-at-matched-count
(everywhere except the barrel) and on raw sim coverage (2-5x), and its marginal-value
ceiling (3.46) clears what LST gives us (2.94). It is WORSE on marginal value PER
DELIVERED ROW by ~6.6x, and that deficit is a DEDUP deficit, not a matching deficit.
Secondary real matching defects: (a) barrel purity, (b) eta calibration of the logit.

## C4 -- WHAT I BUILT (all flag-gated, all default 0 = pre-M21 bit-exact)

CODE (protoC only; the CMSSW tree is untouched):
 1. PixelAttach.h/.cc  kAttachFeat 19 -> 25. New slots, defined for BOTH target kinds:
      19 tgtFakeScore  20 tgtPromptScore  21 tgtDisplacedScore   (a bare T3's OWN t3-DNN
         scores; for a chain target the worst-node aggregate over its member T3s)
      22 dKappaRel = (|k_pls|-|k_tgt|)/(|k_pls|+|k_tgt|)  -- the scale-free radius
         agreement LST's radius criterion tests; slot 13 is an ABSOLUTE difference and is
         numerically blind at high pT, and its 1e9 degenerate-fit sentinel cannot reach
         this bounded form.
      23 tgtRtInner  24 tgtZInner -- the geometric scale slots 15/17 are measured at.
    Slots 0-18 unchanged, so the resident 19-input r2 head scores bit-identically.
 2. AttachInference.{h,cc}  attachLogitT3() + attach_t3_mlp_weights.h (namespace
    attacht3mlp). Absent header -> attachLogitT3 IS attachLogit.
 3. AttachDelivery  -HT3 selects the dedicated head for STAGE B ONLY. Chain targets always
    keep the general head -> the frozen pT5-class line cannot move.
 4. DumpWriter/main pairdump  + tgtRow, plsRow, labelHit, hitFrac.
    labelHit = THE HARNESS RULE on the object the pair would deliver (>75% of the merged
    pLS+T3 hit list from one sim), computed exactly and cheaply from per-object
    sim->hit-count tables (the matcher's permutation max over sims IS the count of
    distinct hits whose sim list contains s). Measured on the 300-evt dump: the legacy
    proxy label is a STRICT SUBSET of it -- 758,249 pairs (+4.8%) are harness-true and
    proxy-fake, and every one of them used to be downsampled as a fake.
 5. main.cc -CCG 2 = MODULE granularity (md_detId), -CCO 1 = OWNER-RESOLVED counting
    (the map stores WHO holds each unit; the veto needs -CCN units from ONE owner).
 6. main.cc -RDP -- pixel-side pre-claim of the seed-family map from surviving carried
    rows with OT content. MEASURED NO-OP in the flagship line (at -RT5 1 -RT3 1 every
    such row is already retired), kept default 0 and documented as such.
 7. gen_c_ref/gc_train_t3.py + gc_export_t3_weights.py -- bare-T3-only training on
    labelHit with model selection at the OPERATING POINT (weighted TPR at weighted
    FPR 3e-5), not on a chain-pair AUC. Test-60 held out and asserted.
    RESULT v1: weighted test AUC .99837, TPR@3e-5 .338, per-region AUC
    barrel-like .99781 / transition .99821 / endcap-like .99693.

NO-OP GATE for all of it: P25BASE reproduced EXACTLY (eff .81303 ... nTC 614277).

## C5 -- THE NEW HEAD IS BETTER, MEASURED ON THE DELIVERY (300 evts, c2_curve)

Re-thresholding the -HT3 1 low-margin run the same way as c1_curve:
  fakeFrac   old-head rows / MARG      new-head rows / MARG
  ~0.020      642.8 / 1.87              814.3 / 2.48    (+33% marginal value)
  ~0.009      373.3 / 0.68              465.6 / 1.28    (+88%)
  barrel purity at ~310 barrel rows: old .1067 -> new .0945 ; at ~320 rows old .1555 -> .0945
The gain is exactly where it was aimed (the high-specificity tail; the head is selected on
weighted TPR at weighted FPR 3e-5). Low-purity end is unchanged or marginally worse, which
is the expected cost of moving the selection metric off global AUC.
STILL BROKEN: the eta calibration. At T=6 the mix is barrel/trans/endcap 301/253/716
against LST's 322/139/127; the endcap spends 716 rows/evt for MARG 0.48.

## C6 -- THE DEDUP GRANULARITY QUESTION IS NOW ANSWERED, AND THE ANSWER IS NEGATIVE

Batch 1 (300 evts, old head, -AT3 6 unless noted; eff / dup / fake / deliv):
  d_hit  MD-free hit N=1   .80385 .04612 .05546 130.6   <- the M20 frontier
  d_n1   MD  N=1           .80456 .04682 .05649 135.5
  d_both MD  N=2           .80815 .07151 .06396 181.5
  w_n2o  MD  N=2 +owner    .80851 .07266 .06636 188.0   (eff +.0004, dup and fake WORSE)
  w_m2o  module N=2 +owner .79819 .10800 .05962 187.3
  w_m3   module N=3        .79587 .11298 .06022 189.5
  w_m3o  module N=3 +owner .81171 .23095 .06510 386.9
  r_n3   MD  N=3           .81320 .23512 .06651 398.3   <- ONLY point that holds eff
  w_m3_a7 module N=3, AT3 7 .79565 .10652 .05139 140.7
READINGS
 * MODULE granularity (my -CCG 2) is WORSE than MD at every matched row count. Hypothesis
   was that the module is the hit-choice-invariant unit for a same-track duplicate; the
   measurement says modules are shared by too many DIFFERENT tracks in dense events, so
   the veto lands on signal. Recorded as a negative result, flag kept, default off.
 * OWNER RESOLUTION (-CCO) buys +.0004 of eff and COSTS dup and fake. The reason is
   visible in the fake ledger: a FAKE T3 is built from several tracks' hits, so "the
   crowd holds my units" is the fake's own signature -- resolving the crowd away
   protects fakes. -CCO 2 restores the all-units-claimed fallback for that reason.
 * The whole (granularity x N x margin) surface is a single frontier. Nothing on it holds
   eff >= .81303 with dup <= .052.

## C7 -- THE NEW HEAD MOVES THE END-TO-END FRONTIER (batch 2, 300 evts)

tag           head  -AT3  dedup      eff     dup     fake   deliv
d_hit          r2   6    hit N=1  .80385  .04612  .05546  130.6   (the M20 frontier point)
x_a6_n1        M21  6    MD  N=1  .80293  .04569  .05829  151.0
x_a75_n1       M21  7.5  MD  N=1  .80394  .04762  .05002  117.4  <== +0 eff, -.0054 FAKE
x_a75_hit1     M21  7.5  hit N=1  .80298  .04688  .04969  113.8
x_a9_n1        M21  9    MD  N=1  .79960  .04948  .04836   84.5
x_a9_hit1      M21  9    hit N=1  .79881  .04885  .04828   82.1
x_a11_n1       M21  11   MD  N=1  .78678  .05242  .04846   34.0
x_a75_m2o2     M21  7.5  mod N=2o .79222  .07243  .05049  108.8
x_a75_m3o2     M21  7.5  mod N=3o .79587  .11114  .05064  162.7
r_off (no pT3 class at all)       .77432  .05254  .04905      0
DUPLICATE RATE IS NOW SOLVED (.0457-.0495 <= .052 everywhere on the MD/hit N=1 family) and
FAKE is within .0014-.0033 of the gate. EFFICIENCY is the remaining miss: .8039 vs .81303.

TWO MECHANISMS FOUND WHILE READING THESE NUMBERS (both now implemented, flag-gated):
 (i) -RPS DESTROYS SEEDS THAT NEVER GOT A DELIVERY. The predicate fires on
     plsBestT3Logit >= AT3, recorded for EVERY SCORED PAIR, so a seed that merely HAD a
     compatible bare T3 loses its carried bare-pLS row even when it lost the
     one-pLS-one-owner contention or its delivery was revoked by the dedup. That track
     ends with NO row at all. It is also why eff goes DOWN when the margin is LOOSENED
     from 7.5 to 6 (.80394 -> .80293) while the row count goes UP: more seeds are
     destroyed than tracks are added. `-RPO 1` makes that half fire on OWNERSHIP only.
(ii) THE LOST K9 PRE-CLAIM. From the fake ledger, -RT3 1 (retiring LST's pT3 rows) lets
     29.7 extra chain rows/evt through K9 carrying 4.5 extra FAKES/evt, purely because
     those rows stopped pre-claiming their 6 OT hits. Our own deliveries would restore it,
     but they are decided AFTER K9 and cannot pre-claim. `-PT3 1` measures the upper bound
     on what a second claim pass would buy (diagnostic only -- it consults LST's pT3s).

## C8 -- THE SECOND-CLAIM-PASS IDEA IS A DEAD END, MEASURED (-PT3)

y_pt3_off = LST's pT3 rows RETIRED FROM THE OUTPUT but still PRE-CLAIMING into K9:
  eff .75843 | dup .05301 | fake .04716 | nTC 569522   against r_off .77432/.05254/.04905
So the pre-claim buys -.0019 of fake and COSTS .0159 of EFFICIENCY: giving 3-layer objects
claim priority over the 5-7-layer chains prices the chains out of the greedy walk. Adding
our own deliveries on top (y_a9_n1_pt3) reaches fake .04612 -- BELOW the gate and below
P25BASE -- but at eff .79701 and with v510 collapsing .72713 -> .69716.
=> Making the pT3-class deliveries first-class claimants in K9 (the "second claim pass")
would buy the fake gap and lose 1.6 points of efficiency doing it. The M16 staging (chains
claim, deliveries contend afterwards) is the RIGHT architecture. Recorded as a negative
result so the next agent does not spend a day on it.
NOTE -PT3 is diagnostic only in any case: it consults LST's pT3 rows.

## C9 -- FINAL DEDUP IDEA: LAYER CONTAINMENT (-CCL)

Rationale from C7: the -CCN 1 veto ("any shared unit kills") is what gets dup under the
gate and it costs 0.74 in-cut sims/evt of UNIQUE coverage -- T3s that merely brush an
already-delivered object. "Shares a unit" is not "is the same track". "Is the same track"
is "everything I would add is already there", so -CCL 1 vetoes only when ONE owner of a
shared unit already covers EVERY LAYER the T3 occupies. Per owner: a 16-bit layer mask
built while it pre-claims; per candidate: at most 3 lookups and 3 mask tests. Ownership
map, O(1), no candidate-vs-candidate comparison, no proximity term.

## C10 -- BATCHES 4 + 5 (-RPO, -CCL), 300 evts

tag           -AT3  dedup            eff     dup     fake   deliv
x_a75_n1       7.5  MD N=1        .80394  .04762  .05002  117.4   <-- best dup-passing
x_a75_hit1     7.5  hit N=1       .80298  .04688  .04969  113.8
q_a75_L        7.5  layer only    .80508  .06605  .05068  139.4
q_a75_L_n2     7.5  layer+MD N=2  .80486  .05232  .05064  124.7
q_a6_L         6    layer only    .80416  .06802  .06126  183.1
z_a75_n1_rpo   7.5  MD N=1 +RPO   .80561  .05724  .04993  117.4
z_a6_n1_rpo    6    MD N=1 +RPO   .80741  .05726  .05825  151.0
z_a6_n2_rpo    6    MD N=2 +RPO   .81013  .08406  .06699  205.2
READINGS
 * -CCL (layer containment) IS a real refinement: at 7.5 it buys +.0011 of eff over
   MD N=1 -- but it is LOOSER, so it also buys +.018 of dup. Combined with MD N=2 it
   lands at dup .05232, .0003 OVER the gate, for +.0009 of eff. Genuine but tiny.
 * -RPO (stop destroying seeds that never got a delivery) buys +.0017 eff and costs
   +.0096 dup: the restored carried bare-pLS rows ARE mostly duplicates. Confirms the
   mechanism, does not help the gate.
 * The whole (head x margin x granularity x N x layer x RPO) surface is ONE flat frontier
   around eff .804 +- .002 for dup .047-.057. Nothing reaches eff .81303.

## C11 -- DOES LAYER CONTAINMENT ESCAPE THE N-SHARED-UNIT FRONTIER?  PARTLY, AND IT
##        BUYS A REGION THE N-RULES CANNOT REACH -- BUT NOT ENOUGH  (gen_c_ref/frontier.txt)

N-shared-unit family (M21 head, MD granularity, -RDT 1, no -CCL, no -RPO), 300 evts:
  N=1: dup .04569 eff .80293 (@6) | .04707/.80385 (@7) | .04762/.80394 (@7.5)
       | .04890/.80131 (@8.5) | .04948/.79960 (@9)          <- N=1 TOPS OUT at .80394
  N=2: dup .07001 eff .80267 (@9) | .07268/.80750 (@7.5)     <- N=2 TOPS OUT at .80750
  THE N-RULE FAMILY HAS NO POINT AT ALL BETWEEN dup .0495 AND dup .0700.
Layer containment fills exactly that hole:
  CCL+N=2 @7    dup .05215 eff .80478 fake .05252
  CCL+N=2 @7.5  dup .05232 eff .80486 fake .05064
  CCL+N=2 @8    dup .05252 eff .80403 fake .04945
  CCL only @7.5 dup .06605 eff .80508 fake .05068
  CCL+N=1 @7.5  dup .04762 eff .80394 (IDENTICAL to N=1: N=1 already subsumes the rule)
Residual against the interpolated N-rule envelope: +.0048 at dup .052, +.0030 at .066,
0 at .0476, -.0019 at dup .067/margin 5.
VERDICT ON -CCL: it is a REAL, qualitatively different ownership rule and it does deliver
operating points the N-rule spectrum cannot -- +0.0009 of efficiency over the best
dup-passing N=1 point, at dup .05232. But .05232 is .00032 OVER the dup gate, and its
efficiency is still .0082 short of .81303. It does NOT break sibling A's ceiling; it
shifts the same wall by about one part in a thousand.

## C12 -- FINAL CONFIGURATION AND GATE VERDICT

BEST FULL CONFIGURATION (everything else = the frozen flagship line):
  -RT3 1 -CF 1 -CFC 1 -HT3 1 -AT3 7.5 -RDT 1 -CC 1 -CCG 1 -CCN 1
  (adding -CCL 1 gives a bit-identical result at this point: -CCN 1 already subsumes it)
  tag x_a75_n1 == f_a75_Ln1, 300 evts:
    eff .80394 | vxy01 .83683 | v15 .78869 | v510 .72555 | v1030 .71814
    d15 .55901 | d510 .24912 | d1030 .02564 | dup .04762 | fake .05002 | nTC 608790
    deliveries 117.4/evt, of which (pt>0.9) 77.7/evt with fake fraction .0662, dup .0255
  vs P25BASE  eff -.00909 | dup -.00426 (PASS) | fake +.00366 (FAIL) | eff FAIL
  per region  eff barrel .90764 (P25BASE .92474) transition .87384 (.88135)
              endcap .75147 (.75425); dup better everywhere; fake worse in barrel and
              transition, equal in the endcap.
GATE: FAIL on efficiency (-.0091) and fake (+.0037). PASS on duplicate rate and on the
displaced bands (d15 .55901 vs .56009 = ONE track in a 924 denominator; d510 and d1030
bit-identical; v510/v1030 within +-.0016).

## C13 -- ARTIFACT INDEX (gen_c_ref/)

  STATUS.md                this file
  gc_run.sh gc_par.sh gc_pd.sh      runners (protoC binary, gen_c_ref outputs)
  ta_tab.py ta_board.py             scoreboards, repointed to gen_c_ref
  gc_extract.py gc_task1.py gc_task1b.py gc_task1d.py   TASK 1 machinery
  gc_dupdiag.py gc_fakeledger.py gc_frontier.py         diagnostics
  gc_train_t3.py gc_export_t3_weights.py                the M21 bare-T3 head
  attach_t3_mlp.pt attach_t3_norm.json attach_t3_mlp_weights.h   the trained head
  pd_bareT3.root (779 MB)  the 300-evt bare-T3 pair dump, 21.17M rows, 25 features,
                           tgtRow/plsRow/labelHit/hitFrac
  task1_report.txt task1b_report.txt task1c_other.txt task1d_report.txt task1d_c2.txt
  dupdiag_c1.txt dupdiag_c2.txt dupdiag_lst.txt dupsrc_c1.txt ledger_base.txt ledger_x.txt
  board_b2.txt board_b3.txt board_b45.txt board_b6.txt frontier.txt final_tab.txt
  spec_b*.txt              the exact flag lines of every 300-evt point
  t_<tag>.{root,log,json,cmd}, t_agg_<tag>.txt   every run

REPRODUCE THE FINAL POINT:
  cd standalone/protoC && (source ../setup.sh; cmsenv; source ../setup.sh) && make -j 12
  bash gen_c_ref/gc_run.sh FINAL -RT3 1 -CF 1 -CFC 1 -HT3 1 -AT3 7.5 -RDT 1 -CC 1 -CCG 1 -CCN 1
All new flags default to the pre-M21 value, and the frozen P25BASE line reproduces EXACTLY.
