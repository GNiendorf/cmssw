# A05 -- THE pT3 CLASS ON THE FIXED BASELINE

Agent: A05 (one of fifteen explorers). Workspace `standalone/protoA05` (copy of protoFIN),
artifacts `standalone/a05_ref/`. Runner `a05_ref/a05_run.sh <TAG> [overrides]` --
identical frozen prefix to fin_ref/fin_run.sh, artifacts named `a_<TAG>.*`.
Tables: `a05_tab.py` (overall + displaced bands), `a05_tab2.py` (per region); both fall
back to fin_ref / xc_ref / rebase_ref so baseline tags can be quoted in the same table.
Class ledger: `a05_class.py <tag> [--lst]`.

ASSEMBLED BASELINE (start point, every explorer):
    -XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2
    eff .80992 / dup .06230 / fake .05551 / nhitOT 9.80/9.88/3.56 / nTC 618793
    LST: eff .80988 / dup .05179 / fake .04476 / 10.15/10.02/3.56 / 608190

## M0 SETUP (done)
protoA05 = byte copy of protoFIN. Rebuild before any edit: md5 519b0abc... == protoFIN's.
Base binary pinned to `a05_ref/chainproto_base` so the source tree can be rebuilt while
the reference run is in flight.

## M1 NO-OP GATES
tag GATE  -- the assembled baseline on the PINNED PRE-EDIT binary (must equal FINBASE).
tag GATE2 -- the same line on the POST-EDIT binary (must equal GATE bit-identically).

## THE THREE LEVERS (all new code flag-gated, defaults = bit-exact no-op)
1. `-AT3F <v>` / `-AT3D <v>` -- pT3-class TARGET PRE-SELECTION on the upstream t3dnn
   3-class scores. THE GAP THIS FILLS: the attach head's 19 pair features carry the pLS,
   the target's GEOMETRY and the pair residuals, and NOTHING about whether the target T3
   is a real object (checked: PixelAttach.h feature list; t3_fakeScore appears only in the
   CHAIN node features, Features.cc f[12]). So a junk T3 that happens to lie on a helix
   compatible with some pixel seed is a pT3-class fake the pair head cannot rank away.
   Scalar cut on ONE object: no pair term, no proximity, no candidate loop.
   Measured on the sample (300 evts, 310950 T3s): base T3 purity (t3_pMatched > .75)
   0.216; fakeScore <= 0.5 keeps 50.6% of T3s at 98.8% true recall, <= 0.3 keeps 39.0% at
   97.3%, <= 0.1 keeps 27.1% at 91.7%, <= 0.05 keeps 22.8% at 85.8%.
2. `-AT3R <0|1>` -- evidence policy for a gated-out target (0 = out of the universe, seed
   keeps its bare row; 1 = evidence retained, -RPS still retires the seed).
3. `-RP3 <v>` -- DECOUPLE -AT3 from seed retirement. -AT3 is two knobs welded together:
   the bare-T3 delivery margin AND (via plsBestT3Logit, main.cc m16RefreshSupp) the
   threshold at which -RPS retires a seed's carried type-8 row. That is exactly why the
   baseline frontier shows -AT3 7 buying .0060 of fake while paying .0103 of dup.
4. `-AT3T` / `-AT3Z` -- per-region delivery margin on |t3_eta| (1.1 / 1.7 bins, the
   scoreboard's own regions, and the eta the delivered type-5 TC carries).

## M1 RESULT -- BOTH GATES PASSED EXACTLY
GATE (pinned pre-edit binary) == FINBASE on all 14 metrics, on nTC (618793) and on 33/33
branches under rebase_ref/cmp_branches.py. GATE2 (post-edit binary, no A05 flag) == GATE
on all 14 metrics and nTC. The A05 code is a proven no-op at its defaults.

## M2 -- THE CLASS LEDGER: WHAT WE DELIVER vs WHAT LST DELIVERS (300 evts, a05_class.py)
```
class                         rows/evt    fake     dup   uniqSim/evt  covSim/evt
OURS  attachT3 (baseline)       135.41  0.1387  0.0142      3.79        3.86
LST   pT3                       151.48  0.0348  0.0238      5.36        5.45
-- context --
OURS  attachT5                  575.93  0.0079  0.0065     25.07       25.22
OURS  chain                     461.11  0.1027  0.0675     26.09       30.33
OURS  carried type-8            860.77  0.0452  0.0838     19.25       19.54
LST   pT5                       803.48  0.0155  0.0099     41.85       42.12
LST   pLS                       894.54  0.0459  0.0890     20.01       20.61
```
uniqSim = accepted sims matched ONLY by a row of that slice (efficiency it alone carries).
THE DIAGNOSIS: our pT3 class is FOUR TIMES DIRTIER than the class it replaces (13.9% fake
vs 3.5%) and delivers 30% FEWER unique sims (3.79 vs 5.36). In absolute terms it produces
18.8 fake rows/evt against LST's 5.3 -- a 13.5 row/evt excess on a 2062.6 row/evt output,
i.e. ROUGHLY 60% OF THE ENTIRE FAKE-RATE GAP AGAINST LST comes from this one class.
Its duplicate rate is already BETTER than LST's (.0142 vs .0238), so the class is a fake
problem and an efficiency problem, not a duplicate problem.

## M3 RESULT -- `-AT3F` (t3dnn TARGET PRE-SELECTION) DOMINATES `-AT3`
```
tag    -AT3F      eff      dup     fake   deliv/evt   effB     effT     effE    fakB     fakT     fakE
GATE     off  0.80992  0.06230  0.05551     135.4  0.92660  0.88213  0.74496  .06680  .06903  .04525
F90      0.9  0.81014  0.06233  0.05511     134.8  0.92717  0.88213  0.74496  .06547  .06903  .04525
F70      0.7  0.81023  0.06249  0.05344     131.9  0.92739  0.88187  0.74507  .06191  .06677  .04482
F50      0.5  0.81054  0.06263  0.05219     129.6  0.92751  0.88264  0.74540  .05959  .06517  .04427
F30      0.3  0.81059  0.06274  0.05086     126.8  0.92751  0.88289  0.74540  .05732  .06331  .04361
F10      0.1  0.81050  0.06297  0.04894     121.2  0.92682  0.88315  0.74573  .05480  .06023  .04239
LST                    0.80988  0.05179  0.04476     151.5  0.92557  0.88187  0.74596  .04249  .04454  .04603
```
EFFICIENCY GOES UP AND FAKE GOES DOWN TOGETHER; duplicate rate moves +.0007 over the whole
range. Compare the only knob that moved fake before: `-AT3 7` costs .00092 of efficiency
and .00222 of duplicate rate to buy .00581 of fake. `-AT3F 0.1` BUYS .00657 of fake and
GAINS .00058 of efficiency for .00067 of duplicate rate. It strictly dominates.
WHY EFFICIENCY RISES: a gated-out junk T3 stops owning the pLS AND (at the default
-AT3R 0) stops feeding plsBestT3Logit, so the seed's carried bare-pLS row survives and
matches its sim. Confirmed by the ablation: F50R (`-AT3F 0.5 -AT3R 1`, evidence retained)
gives eff .80904 at the SAME fake .05218 -- the evidence policy alone is worth +.0015 of
efficiency, and the default (pre-selection semantics) is the right one.
All three regions improve on fake; every displaced band is untouched to 5 decimals.

## M4 -- `-RP3` WAS AN EXACT NO-OP, AND WHY (a real finding, now fixed)
`RP3A7` (`-AT3 7 -RP3 6`) reproduced fin_ref's `E_A7X4` (`-AT3 7`, no -RP3) on all 14
metrics AND on nTC (613782). Cause: the -RPS predicate exists in TWO places -- the
m16RefreshSupp copy that retires a CARRIED type-8 row (main.cc ~3476) and the `-ZP8`
copy that decides whether the post-deletion seed universe RE-EMITS that seed as a bare
row (~5048). Only the first read the new threshold, so every row -RP3 retired was
immediately re-emitted by the second under the old one; the only change was the row's
provenance mark. Both copies now read rpsT3Theta. With no -RP3 the two are identical to
thetaAttachT3, so this is still a bit-exact no-op by default (re-gated as GATE3).
DIRECT CONFIRMATION of the mechanism (old binary, one-sided): `RP3_5` (`-RP3 5`, retires
MORE carried rows) reproduced GATE bit-for-bit -- every retired row came straight back
through the -ZP8 channel. `RP3_7` (retires FEWER; the -ZP8 copy then blocks nothing extra)
DID move: eff .81041 dup .06281 fake .05553 nTC 619447 (+654 rows). One-sided response is
exactly what the shared-predicate bug predicts.

## M4b -- THE CLASS LEDGER AFTER THE GATE (the number this angle is judged on)
```
config                rows/evt    fake     dup   uniqSim/evt   fake rows/evt
OURS baseline           135.41  0.1387  0.0142     3.79            18.79
OURS -AT3F 0.3          126.76  0.0811  0.0150     3.75            10.28
OURS -AT3F 0.1          121.24  0.0525  0.0151     3.66             6.37
LST  pT3                151.48  0.0348  0.0238     5.36             5.27
```
The gate removes 62% of the class's fake content for 3.4% of its unique-sim content, and
the class's fake EXCESS over LST falls from 13.5 rows/evt to 1.1. The class's duplicate
rate was already better than LST's and stays there. What remains is a RECALL gap
(3.66 unique sims/evt against LST's 5.36) -- the class is now clean and still small.

## M5 RESULT -- PER-REGION `-AT3` IS NOT WARRANTED (both directions measured, both lose)
```
tag       change                 eff      dup     fake    effT     effE    fakT     fakE   deliv/evt
GATE      -                  0.80992  0.06230  0.05551  .88213  .74496  .06903  .04525     135.4
AT3Z5     -AT3Z 5 (endcap    0.80948  0.06205  0.05964  .88187  .74396  .06884  .05299     146.7
                    looser)
AT3T7     -AT3T 7 (transi-   0.80798  0.06202  0.05222  .87322  .74418  .06118  .04149     120.2
                    tion tighter)
```
Loosening the endcap buys .0004 of dup for .0041 of FAKE and LOSES endcap efficiency --
the extra endcap deliveries are overwhelmingly fake. Tightening the transition buys .0033
of fake for .0019 of overall efficiency and .0089 of TRANSITION efficiency, five times
worse than the global t3dnn gate at the same fake. Keep ONE global margin; the region
structure of the fake rate is not a region structure of the delivery margin.

## M6 -- THE `-AT3F` FRONTIER COMPLETED (300 evts; every displaced band unmoved to 5dp)
```
tag    -AT3F      eff      dup     fake     nTC    deliv/evt
GATE     off  0.80992  0.06230  0.05551  618793      135.4
F90      0.9  0.81014  0.06233  0.05511  618615      134.8
F70      0.7  0.81023  0.06249  0.05344  617862      131.9
F50      0.5  0.81054  0.06263  0.05219  617341      129.6
F30      0.3  0.81059  0.06274  0.05086  616716      126.8
F10      0.1  0.81050  0.06297  0.04894  615718      121.2   <== CHOSEN
F05     0.05  0.81019  0.06320  0.04805  615157
F02     0.02  0.80961  0.06342  0.04723  614236
D00   (-AT3D 0.0, margin form)
              0.81054  0.06265  0.05198  617239
LST           0.80988  0.05179  0.04476  608190      151.5
```
Efficiency is FLAT-TO-UP over 0.9 -> 0.1 and turns over only below 0.05; fake falls
monotonically; duplicate rate moves +.00067 across the WHOLE range. `-AT3F 0.1` is the
knee: it is the last point that still gains efficiency (+.00058) and it has taken 59% of
the fake excess over the baseline. `-AT3D 0.0` (margin form) reproduces `-AT3F 0.5` to
.00002 eff / .00002 dup / .0002 fake -- the two parameterizations are the same statement,
so ship the ONE-score form and drop -AT3D.

PER REGION at F10: fakB .06680 -> .05480 (LST .04249), fakT .06903 -> .06023 (LST .04454),
fakE .04525 -> .04239 (LST .04603 -- endcap fake now BELOW LST by .0036). effT .88315 is
the best transition efficiency measured all round (LST .88187). 42% of the barrel fake gap
and 40% of the transition fake gap against LST are closed by this one scalar cut.

## M7 -- COMBINATIONS WITH THE EXISTING KNOBS (300 evts)
```
tag        line (on top of the assembled baseline)     eff      dup     fake
GATE       -                                       0.80992  0.06230  0.05551
F10        -AT3F 0.1                               0.81050  0.06297  0.04894
F10X35     -AT3F 0.1 -XCT 3.5                      0.80957  0.05985  0.04900
F10X3      -AT3F 0.1 -XCT 3                        0.80829  0.05746  0.04901
F10RP7     -AT3F 0.1 -RP3 7                        0.81120  0.06465  0.04901
F10A5      -AT3F 0.1 -AT3 5 (looser delivery)      0.80939  0.06163  0.05257
F50R       -AT3F 0.5 -AT3R 1 (evidence retained)   0.80904  0.06224  0.05218
LST                                                0.80988  0.05179  0.04476
```
`-AT3F` and `-XCT` are ORTHOGONAL: -XCT 4 -> 3.5 costs .00093 eff and buys .00312 dup at
F10 exactly as it does at F off (.00088 / .00305), and does not move fake at either.
LOOSENING the delivery margin under the gate does NOT recover the recall gap: -AT3 5 loses
.0011 of efficiency and .0036 of fake rate -- the extra deliveries are junk the t3dnn gate
had already declined to call real. The recall gap is not a margin problem.

## M8 -- NO-OP RE-GATE AFTER THE -RP3 FIX
GATE3 (assembled baseline, post-fix binary) == GATE2 == GATE: all 14 metrics, nTC 618793,
and 33/33 branches IDENTICAL under rebase_ref/cmp_branches.py. Every A05 flag is a proven
bit-exact no-op at its default.

## M9 -- THE "RECALL GAP" IS 79% BOOKKEEPING (a05_recall.py, 300 evts, per event)
The M2/M4b ledger said our class carries 3.66 unique sims/evt against LST pT3's 5.36.
That comparison is between two DIFFERENT partitions of two different outputs. The
end-to-end question is: of the sims ONLY LST's pT3 covers, how many does our whole output
miss?
```
                                          GATE (no gate)   F10 (-AT3F 0.1)
LST pT3 covers                                 5.45            5.45
... of which ONLY an LST pT3 row covers        5.36            5.36
    -> we cover with OUR pT3 class             3.21            3.12
    -> we cover with a DIFFERENT slice         1.72            1.81
    -> WE MISS ENTIRELY                        0.43            0.43   <== the real loss
OUR pT3 class uniquely carries                 3.79            3.66
... of which LST covers NOWHERE                0.17            0.15   <== the class ADDS
```
THE REAL END-TO-END RESIDUAL ATTRIBUTABLE TO THE CLASS IS 0.43 SIMS/EVT, not 1.70, and
`-AT3F 0.1` COSTS EXACTLY ZERO OF IT (0.43 both sides, to 2dp). The apparent recall loss
under the gate is entirely re-absorbed by the chain / attachT5 / carried slices. On an
efficiency denominator of 208.5 in-cut accepted sims/evt, 0.43 is an upper bound of
~.002 of absolute efficiency -- and our class returns 0.15/evt that LST covers nowhere.
This is why the gate can take 59% of the class's fake content and still GAIN efficiency.

## M10 -- DELIVERY LEDGER UNDER THE GATE (per event, 300 evts)
```
tag     -AT3F   cand/evt   pixRev   otRev   DELIVERED   pairs blocked by the gate
GATE      off     1127.2    661.3   330.5      135.4      0
F70       0.7     1115.3    657.8   325.6      131.9      2.41 M/evt
F30       0.3     1088.3    645.4   316.2      126.8      3.92 M/evt
F10       0.1     1051.3    626.8   303.3      121.2      4.58 M/evt
F02      0.02      945.7    573.3   263.8      108.6      5.03 M/evt
LST pT3                                        151.5
```
The gate acts on the ENUMERATED pair list, before the margin test, so it also removes
~4.6 M prefiltered pair evaluations per event at F10 (no timing claim is made from these
contended runs, but the arithmetic is a real reduction in attach-stage work). Downstream,
the contention it feeds shrinks proportionally: 76 fewer candidates, 34 fewer pixel-side
and 27 fewer OT-side revocations per event for 14 fewer deliveries.

## M11 -- `-RP3` IS REDUNDANT WITH `-XCT` (batch3; the reason NOT to ship it)
With the shared-predicate bug fixed, -RP3 now responds in BOTH directions. At -AT3F 0.1:
```
knob move                              eff      dup     fake
-RP3 5  (retire MORE seeds)        0.80727  0.06051  0.04876
-RP3 6  (= -AT3, the default)      0.81050  0.06297  0.04894
-RP3 7  (retire FEWER seeds)       0.81120  0.06465  0.04901
-XCT 3  (crossclean tighter)       0.80829  0.05746  0.04901
-XCT 3.5                           0.80957  0.05985  0.04900
-XCT 4  (the baseline)             0.81050  0.06297  0.04894
```
-RP3 and -XCT trace the SAME eff/dup frontier because both are seed-retirement knobs, and
-XCT DOMINATES on the tightening side: -XCT 3.5 is better than -RP3 5 on efficiency
(+.0023) AND on duplicate rate (-.0007). So -RP3's only unique contribution is the
LOOSENING direction (+.0007 eff for +.0017 dup), which is a worse trade than -XCT 4.5
already offers. DO NOT SHIP -RP3 as a tuned knob; ship it only as the BUG FIX (both copies
of the -RPS predicate must read the same threshold), which is a no-op at the default.
`-AT3 5.5` under the gate: eff .81028 dup .06233 fake .05039 -- still worse on fake than
plain F10 at equal efficiency. Loosening the margin remains the wrong lever.

## M12 -- THE FINAL FRONTIER (batch4; 300 evts, all displaced bands unmoved to 5dp)
```
tag         line added to the assembled baseline    eff      dup     fake      nTC
GATE        (baseline)                          0.80992  0.06230  0.05551   618793
F10         -AT3F 0.1                           0.81050  0.06297  0.04894   615718
F10X375     -AT3F 0.1 -XCT 3.75                 0.81006  0.06126  0.04896   614930
F10X35      -AT3F 0.1 -XCT 3.5                  0.80957  0.05985  0.04900   614282
F10RP7      -AT3F 0.1 -RP3 7                    0.81120  0.06465  0.04901   616829
F10R7X375   -AT3F 0.1 -RP3 7 -XCT 3.75          0.81076  0.06281  0.04903   615991
F10R7X35    -AT3F 0.1 -RP3 7 -XCT 3.5           0.81028  0.06135  0.04906   615315
LST                                             0.80988  0.05179  0.04476   608190
```
TWO POINTS TO CARRY FORWARD.
 (1) `-AT3F 0.1` ALONE -- ONE new knob, nothing else retuned:
     +.00058 eff, +.00067 dup, -.00657 fake. This is the one the 977 confirms.
 (2) `-AT3F 0.1 -XCT 3.75` -- one new knob plus a RETUNE of the constant the baseline
     already carries (no new constant): +.00014 eff, -.00104 dup, -.00655 fake, i.e. the
     ONLY point measured this round that beats the assembled baseline on ALL THREE
     headline metrics at once. Per region it is better than the baseline everywhere
     except a .0006 barrel efficiency dip: effB .92603 effT .88264 effE .74562,
     dupB .04178 dupT .03145 dupE .08073, fakB .05487 fakT .06034 fakE .04237.
 The three-knob points (-RP3 7 added) buy at most +.0007 more efficiency for +.0015 more
 duplicate rate; not worth a third constant.

## M13 -- `-CCK 3` (t3dnn-QUALITY CONTENTION ORDERING) IS A NULL RESULT
Workspace protoA05b (protoA05 plus a one-line widening of the -CCK argument validator, so
-CCK 3 could be reached at all; the A05 -CCK 3 sort branch itself was already in protoA05
and passed GATE3). GATE4 on the new binary == GATE2 on all 14 metrics, nTC 618793, and
33/33 branches -- the widening is a proven no-op.
```
tag       line                            eff      dup     fake      nTC
GATE4     baseline                    0.80992  0.06230  0.05551   618793
CCK3      -CCK 3                      0.80984  0.06230  0.05553   618811
F10       -AT3F 0.1                   0.81050  0.06297  0.04894   615718
F10CCK3   -AT3F 0.1 -CCK 3            0.81037  0.06297  0.04899   615732
```
Reordering the OT-side contention sweep by the TARGET's t3dnn margin instead of the pair
logit moves nothing: -.00008 eff, .00000 dup, +.00002 fake, +18 rows out of 618793, and
the same null with the quality gate already on. The contention's outcome is set by WHICH
MDs are claimed, not by the order in which equally-eligible deliveries claim them. The
target-quality statement is worth having as a THRESHOLD (-AT3F) and worthless as an
ORDERING. Do not re-measure. -CCK stays at its default 0.

## M14 -- FULL 977 CONFIRMATION OF `-AT3F 0.1` (the number to quote)
```
tag                     eff    vxy01      v15     v510    v1030      d15     d510    d1030      dup     fake      nhB      nhT      nhE      nTC
W_F10 (-AT3F 0.1)   0.80987  0.84230  0.80154  0.72111  0.71474  0.58320  0.24521  0.03151  0.06252  0.04940  9.82275  9.90927  3.54483  2023552
W_X4 (baseline)     0.80905  0.84144  0.80110  0.72161  0.71474  0.58320  0.24521  0.03151  0.06184  0.05607  9.80064  9.88373  3.55719  2033868
LST                 0.80987  0.84285  0.77719  0.64422  0.62567  0.51042  0.22906  0.05402  0.05138  0.04538 10.14984 10.00937  3.55665  1998494

per region, 977      effB     effT     effE     dupB     dupT     dupE     fakB     fakT     fakE
W_F10             0.92539  0.88103  0.74508  0.04393  0.03413  0.08112  0.05542  0.06032  0.04287
W_X4              0.92454  0.87981  0.74436  0.04294  0.03375  0.08069  0.06754  0.06932  0.04578
LST               0.92430  0.88004  0.74658  0.00971  0.01308  0.08486  0.04365  0.04542  0.04630
```
THE HEADLINE: on the FULL 977 the gate closes the assembled baseline's residual efficiency
deficit exactly -- .80987 against LST's .80987, where the baseline sat at .80905 (-.0008).
Fake .05607 -> .04940 (the gap to LST falls from +.0107 to +.0040, a 63% reduction);
duplicate rate +.00068; nTC -10316 (-0.5%). Displaced UNTOUCHED: v510 +.0769, v1030
+.0891, v15 +.0244, d15 +.0728 over LST, and every band is within .0005 of the baseline
(v510 -.0005 is one track). Track length nhE 3.5448 vs LST 3.5567 (-0.012).
PER REGION on 977, barrel AND transition efficiency now sit ABOVE LST (+.0011 / +.0010,
against the baseline's +.0002 / -.0002), endcap fake .04287 is BELOW LST's .04630, and the
whole efficiency residual is now a single endcap band (-.0015).
The 300-evt run predicted eff +.00058 / fake -.00657 and the 977 delivered +.00082 /
-.00667 -- the frozen 300 is faithful for this knob.

## FINAL RECOMMENDATION
SHIP: the assembled baseline PLUS `-AT3F 0.1`. One new knob, one new scalar comparison on
a score the pipeline already computes at T3 build time (so it survives the deletion set),
no pair term, no proximity, no candidate loop, no training.
```
chainproto ... -XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2 -AT3F 0.1
```
OPTIONAL FREE RETUNE (adds NO constant, only moves one the baseline already carries):
`-XCT 3.75` on top gives, at 300 evts, the only point that beats the assembled baseline on
all three headline metrics simultaneously (+.00014 eff, -.00104 dup, -.00655 fake).
DO NOT SHIP: -AT3D (same statement as -AT3F, second parameterization), -AT3R (default is
right), -AT3T/-AT3Z (per-region margins lose), -CCK 3 (null), -RP3 as a tuned knob (its
frontier is dominated by -XCT) -- but DO carry the -RP3 BUG FIX, i.e. both copies of the
-RPS bare-T3 predicate reading one threshold, which is a bit-exact no-op by default.
