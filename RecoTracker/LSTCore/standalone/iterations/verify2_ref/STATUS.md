# verifier-2 STATUS

Assigned claims: A02 (dedup head, -XCH 1 / -XCT 0), A07 (fake angle, negative result),
A12 (-a 5 chain-attach margin).

Tooling: verify2_ref/vtab.py -- independent re-derivation of the scoreboard straight from
the createPerfNumDenHists histogram sets (NOT compare_ab.py), so a doctored json or a bug
in the shared tool cannot propagate. verify2_ref/v2_run.sh -- runner with the frozen prefix
copied verbatim from fin_ref/fin_run.sh, outputs in verify2_ref only.

## M1 (02:05) independent re-derivation, frozen 300
LST rb_base300           eff .80988 dup .05179 fake .04476 len 10.148/10.016/3.5625  -> matches the brief
FINBASE (assembled)      eff .80992 dup .06230 fake .05551
A02 r_T00 (-XCH 1 -XCT 0) eff .81019 dup .05819 fake .05377   CONFIRMS A02 headline
A02 r_T05                eff .81085 dup .06212                CONFIRMS the eff-leaning alt
A12 r_A_a50 (-a 5)       eff .81037 dup .05696 fake .05515    CONFIRMS A12 headline
All displaced band counts re-derived; A02 v510/v1030/d15/d510/d1030 bit-identical to
FINBASE; A12 v510 -1, v1030 -7, d15 -4, d510/d1030 identical -- all as claimed.

## M2 (02:06) 977 re-derivation
LST fin_base977          eff .80987 dup .05138 fake .04538
FINBASE977 (r_W_X4)      eff .80905 dup .06184 fake .05607
A02 r_W_T00              eff .80934 dup .05769 fake .05428    CONFIRMS
A02 r_W_T05              eff .81002 dup .06165                CONFIRMS
A12 r_W_A5 (COMPLETED after A12 reported) eff .80941 dup .05640 fake .05562
  -> A12's 977 eff is .00046 BELOW LST; the "at/above LST parity" reading is 300-only.

## M3 (02:05) workspace / binary integrity
diff -r protoFIN protoA07 EMPTY; both md5 519b0abc34a28cd1e803d6b9407ef224 -- A07 CONFIRMED.
protoA12 md5 9929870dfdfc18b120c2f632f4804c30, protoA02 md5 bbaecc06a3077ed26d4567901f668a33
-- both match the claimed values.
A02 GATE3/GATE4 reproduce fin_ref/r_FINBASE_ND on all 28 re-derived quantities. CONFIRMED.
A07 r_GATEA07 reproduces fin_ref/r_FINBASE exactly. CONFIRMED (note: r_GATEA07.cmd records
an EMPTY override list because a07_run.sh was overwritten 1h34m AFTER the gate run; the log
header proves the run did carry -XC 3 -XCT 4 ... . Documentation nit, not a result defect.)

## M4 (02:06) own runs launched (background)
V_A02BEST, V_A12BEST, V_A07BEST, V_A02GATE, V_A12GATE -- 300 evts each, outputs in verify2_ref.

## M5 (02:40) A02 checks not requiring a run
* TRAIN/TEST SPLIT: pairs_L0 = exactly the 300 frozen keys; pairs_L1..L4 = the other 677
  events; |L0 INTERSECT train| = 0, |train INTERSECT frozen300| = 0. train_dedup18.py sets
  trainable = ~isfrozen, builds val from trainable only, computes clip/mean/std from the
  train split only, and the FOM used for model selection is evaluated on isval. CONFIRMED.
* EXPORT PARITY: dedup_parity.py on 20000 pairs of pairs_L0 -> max|dlogit| 3.8e-6
  (they reported 2.9e-6; same order, conclusion unchanged). Header arch [22,32,32,1] =
  1825 params as claimed.
* THE FINDING (verify2_ref/vprice.py, my own reimplementation of the row arithmetic):
  197.7 candidate seeds/evt, isDup 125.4, isFake 8.3, neutral 61.5, nSoleCut>0 2.49
  -- ALL FOUR EXACT.
* PRICING vs MY OWN re-derivation of fin_ref's measured -XCT points (AT3 6, CCR 2 line):
    XCT   measured eff/dup/fake      predicted (their nSim)     predicted (in-cut nSim)
    3.0   .80776 .05689 .05562       .80914 .05688 .05562       .80776
    3.5   .80904 .05925 .05558       .80960 .05918 .05558       .80904
    4.5   .81037 .06675 .05539       .81008 .06694 .05538       .81036
  dup to 2e-4 and fake to 1e-5 CONFIRMED. The efficiency channel is mis-normalised:
  price18.py/train_dedup18.py use nSim = 62555 (the PT-histogram denominator) with the
  IN-CUT rate .80992, while nSoleCut is gated by the in-cut denominator (22633). Factor
  2.764. With the right denominator the model is exact to ~1e-5. Model selection is
  unaffected (the FOM constraint e >= eff0 reduces to d_mat >= 0, scale-free).

## M6 (02:45) A07 checks
* a07_ref/sim_cut.py run BY ME on fin_ref/r_FINBASE.root: BASELINE row reproduces every
  one of my independently re-derived quantities exactly (eff .80992, v01 .84282,
  v15 .79459, v510 .73356, v1030 .74139, d15 .61650, d510 .20161, d1030 .03073,
  dup .06230, fake .05551, nh 9.80254/9.87906/3.55956, nTC 618793). CONFIRMED.
* The four-population fake ledger re-derived BY ME on the 977 (pt>0.9 rows, my own
  decomposition run): pT3-class 93.98 rows/16.69 fake = 17.76% vs LST 104.9/4.00 = 3.81%;
  bare pLS 627.5/30.40 = 4.85% vs 630.1/30.80 = 4.89%; T4 26.13/8.72 = 33.4% vs
  32.06/19.37 = 60.4%; 5+ OT 868.0/34.78 = 4.01% vs 826.1/18.14 = 2.20%. Sum of excesses
  +18.28 fakes/evt. Matches A07's ledger everywhere.
* PERFECT_pT3 on 977: fake .05607 -> .04622 at d_eff and all 8 bands +0.00000. I get the
  same from the raw counts: (88505-16308)/(1578457-16308) = .04622. CONFIRMED.
  Reaching LST's own 3.81% lands at .04834 (they said .04835). CONFIRMED.
* The trap (barrel 4-layer chain rows) reproduces: d_fake -.00393 at d_eff +0.00000 but
  d_v1030 -.02242 / d_d15 -.03902 on the 300 and -.02269 / -.04929 on the 977.

## M7 (02:45) A12 checks
* 311112 accepted chain TCs in EVERY -a setting (grepped from the logs). CONFIRMED.
* attach fraction 575.9/1037.0 = 55.5% at baseline, 785.0 at -a 5, i.e. +209.1 rows/evt.
* my own decomposition of r_A_a50.root vs r_FINBASE.root vs rb_base300.root:
  newly attached rows carry (10.48-4.56)/209.11 = 2.83% fake; bare-chain population
  (type 4) 8.74%; our seeded class 1.335% vs LST's pT5 1.552%; pT3-class at -a 5
  13.29% on 133.5 rows vs LST 3.48% on 151.5. ALL EXACT.
* GATE2 (new protoA12 binary, baseline flags) == fin_ref/r_FINBASE on all 28 quantities.
* -XC 0 ablation: at -a 6.875 (fin_ref/r_G_NOOP) dup .16098 vs .06230 = .0987;
  at -a 5 (r_C_aXC0) .06997 vs .05696 = .0130. CONFIRMED.
* -XCT 5 at -a 5 (r_C_aX5) == -XC 1 at -a 5 (r_B_a5X1) on all 28 quantities. CONFIRMED.
* NEW SINCE THEIR REPORT: batch 4 and W_A5 finished. r_D_a4X1 (-XC 1 -a 4) is identical
  to r_A_a40 (-XC 3 -XCT 4 -a 4) on every quantity -- the deletability proof they said
  was unfinished now holds. And W_A5 (977) = eff .80941 / dup .05640 / fake .05562.

## M8 (02:35) MY OWN RUNS -- all five reproduce, 28/28 quantities each
  V_A07BEST  protoA07 + assembled tail          == fin_ref/r_FINBASE_ND  EXACT
  V_A02GATE  protoA02 -XCH 0                    == fin_ref/r_FINBASE_ND  EXACT
  V_A12GATE  protoA12 -AC 0                     == fin_ref/r_FINBASE_ND  EXACT
  V_A02BEST  protoA02 -XCT 0 -XCH 1             == a02_ref/r_T00         EXACT (.81019/.05819/.05377)
  V_A12BEST  protoA12 -a 5 (CURRENT binary)     == a12_ref/r_A_a50       EXACT (.81037/.05696/.05515)
The V_A12BEST identity also settles A12's unverifiable "batch 1 ran on the inherited
protoFIN binary" provenance claim: the answer does not depend on which binary was used.

## VERDICT: ISSUES (all three headline physics results reproduce; the defects are in
## the 977 reading of A12, a normalisation bug in A02's pricing tool, and provenance
## bookkeeping). Details in the returned report.
