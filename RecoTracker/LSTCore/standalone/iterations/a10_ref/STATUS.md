# A10 -- JOINT OPERATING POINT -- STATUS

Agent role: A10 (joint optimum). Angle: the assembled config has ~4 coupled thresholds
(-a delivery, -AT3 pT3 delivery, -XCT dedup, plus the claim/braid family). The Baseline
agent only touched -AT3 x -XCT. Explore the joint space and report the PARETO SET.

Workspace: `standalone/protoA10` (copy of protoFIN, READ-ONLY sibling).
Artifacts: `standalone/a10_ref/` only.
Runner: `a10_ref/a10_run.sh <TAG> [overrides]` (clone of fin_run.sh, BIN=protoA10).

ASSEMBLED BASELINE (start point): `-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2`
  300 evts: eff .80992 dup .06230 fake .05551 nhitOT 9.80/9.88/3.56 nTC 618793
  LST:      eff .80988 dup .05179 fake .04476 nhitOT 10.15/10.02/3.56 nTC 608190

## M0 -- SETUP (DONE 21:36)
protoA10 = `cp -a protoFIN protoA10`. Binary md5 519b0abc34a28cd1e803d6b9407ef224 ==
protoFIN's, no rebuild, NO SOURCE EDITS PLANNED. Runner a10_ref/a10_run.sh appends the
assembled-baseline flags `-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2` before "$@", so a tag
with NO overrides must reproduce fin_ref FINBASE_ND exactly. Tables a10_tab.py /
a10_tab2.py (fall back to fin_ref/xc_ref/rebase_ref so old tags can be quoted).
a10_ledger.sh pulls the per-event delivery ledger out of the logs.

Baseline ledger (from fin_ref/r_FINBASE.log): chain-attached 575.9/evt, T3 deliveries
135.4/evt, carried bare type-8 rows 860.8/evt, chain TCs 1037.0/evt, total 2033.2/evt,
-RD pixel revocations 662.3, -CC OT revocations 330.5.

KNOB MAP established by reading protoA10/main.cc (nothing here is new code):
  -a 6.875   thetaAttach, CHAIN targets (stage A, pre-K9). Attached chains are emitted
             UNCONDITIONALLY as type-7 and never walk K9. Also half of the -RPS predicate.
  -AT3 6.0   bare-T3 target margin (stage B, post-K9); other half of -RPS.
  -XCT 4.0   ported-CrossCleanpLS bare-chain seed-dedup threshold.
  claim/braid: -F 0.20 (frac) -FC 1 (abs MD budget, = 2 hits at -H 1) -H 1 (hit-level
             claim) -W 0.50 (PROVABLY INERT per the M19 comment: a candidate passing
             -F 0.20/-FC 1 can never cover half an owner -> THE BARREL CURRENTLY HAS NO
             BRAID TEST AT ALL) -WE 0.20 + -WZ 1.5 (band-aware braid, |eta| >= 1.5 only)
             -FBC 0 (band claim budget removed) -B 10 (fake-aware order) -DD off.

## MECHANISM READ OF `-a` (from protoA10/main.cc + the FINBASE log), before any run
`-a` is NOT a delivery-volume knob on the chain side. The log line
`M16 delivery type-7 in-place upgrades of accepted chains=575.9 (of 1037.0 accepted chain
TCs)` says every attached chain was already an ACCEPTED chain TC, so moving `-a` does not
change how many chain TCs exist. What it changes is:
  (1) how many accepted chain TCs are UPGRADED to type-7 (gain the 4 pixel hits + pLS pt);
  (2) `m16RefreshSupp`: a carried type-8 row is dropped when its pLS is owned OR
      `plsBestChainLogit[p] >= thetaAttach || plsBestT3Logit[p] >= thetaAttachT3`
      -- so `-a` is literally one of the two constants in the -RPS seed-retirement test;
  (3) how many pLS stage B (bare-T3) still has available.
So LOWER -a = more upgrades + more seeds retired = fewer bare type-8 rows = less duplicate,
paid for with dilution risk (a wrong pLS glued onto a good chain drops its match fraction:
the measured M7b "upgrade dilution"). That is the joint coupling this angle is testing.
Seed accounting at the baseline: universe 1569.2/evt, carried type-8 delivered 860.8,
-ZP8 additions 29.4, total 890.2 bare-pLS TCs of 2033.2 output TCs.
Also noted: `-PU 1` reports `owners=0 pre-claimed slots=0` -- with -RT5 1 -RT3 1 every
carried row that owns OT hits is gone, so -PU is INERT on this line.

## B1 LAUNCHED 21:36 -- the -a axis nobody moved (9 runs, 300 evts)
GATE (no overrides) + -a 2 / 3 / 4.5 / 5.5 / 6.25 / 7.5 / 8.5 / 10
## B2 LAUNCHED 21:39 -- claim/braid family at baseline -a (9 runs)
-W 0.20 / -W 0.30 / -WZ 0 / -DD 5 / -DD 4 / -DD 3 / -F 0.15 / -F 0.25 / -FC 0
## M1 -- NO-OP GATE: PASSED EXACTLY (23:21, after one OOM-killed attempt)
tag GATE = a10_run.sh with NO overrides (i.e. the assembled baseline flags alone) against
fin_ref/r_FINBASE_ND: every metric identical to 5 decimals, nTC 618793 == 618793, and
`rebase_ref/cmp_branches.py` reports **33 PRE-EXISTING branches IDENTICAL, 0 DIFFER,
0 MISSING, 0 ADDED**. protoA10 is byte-identical to protoFIN and NO SOURCE WAS EDITED all
round, so every number below is a pure flag move on the maintainer-blessed binary.

## B1 RESULT (22:23) -- THE HEADLINE: `-a` DOMINATES THE ASSEMBLED BASELINE
300 evts, everything else at FINBASE. FINBASE = eff .80992 dup .06230 fake .05551.
```
-a      eff      dup     fake      nhB      nhT      nhE      nTC     v510    v1030      d15
2.0  0.80069  0.04600  0.06025  10.0809  10.0762  3.6057   605279  0.68803  0.68215  0.55853
3.0  0.80661  0.04981  0.05747   9.9979   9.9860  3.5803   610630  0.70489  0.71737  0.59420
4.5  0.81014  0.05622  0.05538   9.9063   9.8986  3.5620   615617  0.72513  0.73419  0.60758
5.5  0.81037  0.05783  0.05506   9.8787   9.8882  3.5596   616959  0.73356  0.73739  0.61315
6.25 0.81032  0.05989  0.05516   9.8432   9.8837  3.5584   617906  0.73356  0.73899  0.61427
6.875 (FINBASE) .80992  .06230  .05551   9.8025   9.8791  3.5596   618793  0.73356  0.74139  0.61650
7.5  0.80948  0.06496  0.05593   9.7585   9.8756  3.5624   619655  0.73356  0.74219  0.61650
8.5  0.80904  0.06872  0.05648   9.7012   9.8703  3.5649   620753  0.73356  0.74219  0.61650
10.0 0.80847  0.07052  0.05699   9.6767   9.8661  3.5669   621202  0.73356  0.74219  0.61650
LST  0.80988  0.05179  0.04476  10.1480  10.0155  3.5625   608190  0.65430  0.66453  0.55407
```
`-a` 6.875 -> 5.5 is a STRICT PARETO IMPROVEMENT of the assembled baseline: eff +.00045,
dup -.00447, fake -.00045, track length +.076/+.009/0.000, displaced v510 UNCHANGED
(.73356) and v1030/d15 -.004/-.003. -a 6.25 and -a 4.5 dominate it too. The baseline sits
on the WRONG SIDE of a knob it never moved, and `-a` is already in the frozen flag line, so
this costs ZERO new constants. Mechanism (see above): every attached chain was already an
accepted chain TC, so lowering -a adds no TCs -- it converts bare type-8 rows into pixel
hits on chains that were being delivered anyway (nTC falls 618793 -> 616959) and widens the
-RPS retirement. Below ~4.5 the dilution term takes over: eff and the displaced bands fall
while dup keeps improving (-a 3 already beats LST's dup at .04981).
FAILURE: the GATE run was killed mid-event-192 (machine hit 150 concurrent chainproto from
15 agents; memory down to 14 GB free). Relaunched 22:24.

## B3 LAUNCHED 21:43 -- joint -a x -XCT insurance grid (8 runs, launched blind so the
## trade curve at each candidate -a exists whichever way B1 points)
-a 5.5 x XCT 4.5/5 | -a 4.5 x XCT 4.5/5 | -a 8.5 x XCT 3/3.5 | -a 10 x XCT 3/3.5

## B2 RESULT (22:30) -- the claim/braid family, at the BASELINE -a
```
tag     change            eff      dup     fake     v510    v1030      d15      nTC
FINBASE (none)        0.80992  0.06230  0.05551  0.73356  0.74139  0.61650   618793
W_30    -W 0.30       0.80992  0.06230  0.05551  0.73356  0.74139  0.61650   618793  <- BIT-IDENTICAL
W_20    -W 0.20       0.81001  0.06109  0.05244  0.73187  0.72778  0.60647   616306
WZ_00   -WZ 0         0.81001  0.06106  0.05152  0.73187  0.72618  0.60424   615751
DD_5    -DD 5         0.80992  0.06230  0.05551  0.73356  0.74139  0.61650   618793  <- BIT-IDENTICAL
DD_4    -DD 4         0.80992  0.06230  0.05551  0.73356  0.74139  0.61650   618793  <- BIT-IDENTICAL
DD_3    -DD 3         0.80992  0.06230  0.05551  0.73356  0.74139  0.61650   618793  <- BIT-IDENTICAL
F_15    -F 0.15       0.81006  0.06136  0.05501  0.73356  0.74139  0.61650   618157
F_25    -F 0.25       0.81001  0.06290  0.05578  0.73356  0.74139  0.61538   619161
FC_0    -FC 0         0.80992  0.06226  0.05388  0.73356  0.73739  0.61204   617778
```
READINGS
* `-DD` (post-arbitration structural chain dedup) is COMPLETELY INERT at 3, 4 and 5 shared
  OT hit rows -- bit-identical output. The K9 claim already leaves no accepted chain pair
  sharing 3+ hit rows, so the chain-vs-chain duplicate cell is EMPTY. Nobody should spend
  another run on -DD on this line. The residual barrel duplicate is NOT chain-vs-chain.
* `-W` is inert down to 0.30 (bit-identical), confirming the M19 comment, and BITES at
  0.20: fake -.0031, dup -.0012, eff +.0001. But it is a DISPLACED tax: v1030 -.0136,
  d15 -.0100, d510 -.0040, d1030 -.0024. `-WZ 0` (put the whole detector in the -WE 0.20
  band and remove its claim budget) is the same trade a notch further.
* `-F 0.15` is a small FREE win: eff +.00014, dup -.00094, fake -.00050 with EVERY
  displaced band bit-identical. `-F 0.25` goes the wrong way on dup and fake.
* `-FC 0` buys fake -.0016 for a displaced nick.

## B3 RESULT (22:50) -- -a AND -XCT ARE THE SAME KIND OF KNOB, AND -a IS THE CHEAPER ONE
(2 of 8 runs, AX_45_50 / AX_55_50, were killed when the harness stopped the background
wrapper; AX_55_50 relaunched in B5.)
```
-a    -XCT      eff      dup     fake
4.5   4.0   0.81014  0.05622  0.05538
4.5   4.5   0.81050  0.05999  0.05526
5.5   4.0   0.81037  0.05783  0.05506
5.5   4.5   0.81072  0.06211  0.05493
8.5   3.0   0.80657  0.06325  0.05655
8.5   3.5   0.80794  0.06564  0.05652
8.5   4.0   0.80904  0.06872  0.05648
10    3.0   0.80590  0.06496  0.05701
10    3.5   0.80732  0.06739  0.05702
10    4.0   0.80847  0.07052  0.05699
```
LOCAL EXCHANGE RATES (duplicate rate paid per unit of efficiency bought):
  -XCT 4.0 -> 4.5  at -a 5.5 : +.00035 eff for +.00428 dup  = 12.2 : 1
  -a 4.5 -> 5.5    at XCT 4  : +.00023 eff for +.00161 dup  =  7.0 : 1
  -a 3.0 -> 4.5    at XCT 4  : +.00353 eff for +.00641 dup  =  1.8 : 1
So on the efficiency/duplicate frontier `-a` is a STRICTLY BETTER lever than `-XCT` in the
whole band 4.5 <= -a <= 6.875, and -a ~4.5 is the kink where it turns worse than -XCT.
The high--a family is simply off the frontier: at matched duplicate rate ~.063, -a 8.5
delivers .80657 where the -a 5.5 family delivers ~.8105 (+.004).
`-XCT` still does NOT move fake (flat to .0002 along it); fake is set by -a and -AT3.

## B4 LAUNCHED 22:31 -- joint refinement at -a 5.5 (9 runs)
-a 5.0 | -a 6.0 | -a 5.5 x XCT 3.5/3.0 | -a 5.5 +W 0.20 | +F 0.15 | +AT3 6.5 | +AT3 7 |
kitchen sink (-a 5.5 -XCT 3.5 -W 0.20 -F 0.15)

## B4 RESULT (23:28)
```
tag          flags added to FINBASE          eff      dup     fake     v510    v1030      d15      nTC
FINBASE      (none)                      0.80992  0.06230  0.05551  0.73356  0.74139  0.61650   618793
A_45         -a 4.5                      0.81014  0.05622  0.05538  0.72513  0.73419  0.60758   615617
AJ_50        -a 5.0                      0.81037  0.05696  0.05515  0.73187  0.73579  0.61204   616319
A_55         -a 5.5                      0.81037  0.05783  0.05506  0.73356  0.73739  0.61315   616959
AJ_60        -a 6.0                      0.81032  0.05909  0.05508  0.73356  0.73899  0.61427   617584
A_625        -a 6.25                     0.81032  0.05989  0.05516  0.73356  0.73899  0.61427   617906
AJ_55_X35    -a 5.5 -XCT 3.5             0.80957  0.05489  0.05513  0.73356  0.73739  0.61315   615653
AJ_55_X30    -a 5.5 -XCT 3.0             0.80851  0.05260  0.05517  0.73356  0.73739  0.61315   614550
AX_55_45     -a 5.5 -XCT 4.5             0.81072  0.06211  0.05493  0.73356  0.73739  0.61315   618743
AJ_55_F15    -a 5.5 -F 0.15              0.81045  0.05687  0.05454  0.73187  0.73739  0.61315   616318
AJ_55_W20    -a 5.5 -W 0.20              0.81041  0.05658  0.05200  0.73187  0.72378  0.60312   614464
AJ_55_AT65   -a 5.5 -AT3 6.5             0.80997  0.05877  0.05142  0.73356  0.73739  0.61315   614219
AJ_55_AT7    -a 5.5 -AT3 7               0.80930  0.05975  0.04939  0.73356  0.73739  0.61315   611916
AJ_ALL       -a 5.5 -XCT 3.5 -W .2 -F .15 0.80970  0.05270  0.05154  0.73019  0.72378  0.60312   612539
LST                                      0.80988  0.05179  0.04476  0.65430  0.66453  0.55407   608190
```
* `-a` has a broad flat efficiency optimum 5.0-6.25 at .81032-.81037 and duplicate rate
  falls monotonically as -a falls, so 5.0-5.5 is the sweet spot; -a 5.5 is the point that
  leaves v510 EXACTLY at the baseline .73356.
* `-F 0.15` composes with -a additively and for free: it takes A_55 to .81045/.05687/.05454
  -- better than A_55 on all three -- for a .0017 v510 nick and nothing else.
* `-W 0.20` composes too and is the only cheap FAKE lever (-.0031 at +.00004 eff), but it
  taxes displaced: v1030 -.0136, d15 -.0100 against our own baseline.
* `-AT3` still trades fake against BOTH eff and dup, exactly as the Baseline agent found.

## PLAN
B0  no-op gate: reproduce FINBASE bit-identically on protoA10.
B1  -a scan (the axis nobody moved): 5.0 / 5.75 / 6.375 / 6.875(base) / 7.5 / 8.5
    at fixed -AT3 6 -XCT 4.
B2  joint -a x -XCT, and -a x -AT3 around whatever B1 finds.
B3  claim/braid family at the best -a: -F, -W/-WE, -B, -DD, -FC.
B4  Pareto refinement + 977 confirmation of the 2-3 shortlisted points.

## RESUME 23:44 -- session was interrupted; B5/B6 recovered
The prior shell died with 6 300-evt runs and both 977 runs killed at events 6 / 84-99, and
4 finished runs stuck without their json (root + hists were on disk). Recovered by
re-running createPerfNumDenHists/compare_ab on the finished ones (AJ_40, AJ_55_FC0,
AJ_55_W20_X45, AX_55_50, AX_45_50) and RELAUNCHING the 8 killed runs at 23:44:
  977: W_A55 (-a 5.5), W_A55F15 (-a 5.5 -F 0.15)  [BASEHISTS=fin_ref/fin_base977_hists.root]
  300: AJ_55_F15_X35, AJ_55_F15_AT65, AJ_55_F10, AJ_50_F15, AJ_55_X375, AJ_55_F15_W20

## B5 RESULT (recovered 23:44) -- THE JOINT TIP AND THE THREE-WAY DOMINATORS
```
tag              flags added to FINBASE          eff      dup     fake     v510    v1030      d15      nTC
GATE/FINBASE     (none)                      0.80992  0.06230  0.05551  0.73356  0.74139  0.61650   618793
AJ_35            -a 3.5                      0.80833  0.05225  0.05649  0.70826  0.72538  0.59978   612683
AJ_40            -a 4.0                      0.80944  0.05547  0.05586  0.71164  0.73018  0.60312   614769
AJ_50_X35        -a 5.0 -XCT 3.5             0.80961  0.05408  0.05522  0.73187  0.73579  0.61204   615053
AX_55_45         -a 5.5 -XCT 4.5             0.81072  0.06211  0.05493  0.73356  0.73739  0.61315   618743
AX_55_50         -a 5.5 -XCT 5.0             0.81143  0.06852  0.05476  0.73356  0.73739  0.61315   621262
AJ_55_W20_X45    -a 5.5 -W 0.20 -XCT 4.5     0.81076  0.06085  0.05188  0.73187  0.72378  0.60312   616229
AJ_55_FC0        -a 5.5 -FC 0                0.81037  0.05777  0.05342  0.73356  0.73339  0.60870   615944
LST                                          0.80988  0.05179  0.04476  0.65430  0.66453  0.55407   608190
```
* AX_55_50 is the HIGHEST EFFICIENCY point measured all round (.81143, LST +.00155) and it
  keeps v510/v1030/d15 at the -a 5.5 values -- but at dup .06852 it is off the useful part
  of the frontier. It exists to show the -a 5.5 ridge has headroom, not as a candidate.
* AJ_55_W20_X45 beats FINBASE on ALL THREE headline metrics at once and by a lot
  (+.00084 eff, -.00145 dup, -.00363 fake) -- but -W 0.20 taxes displaced (v1030 -.0176,
  d15 -.0134) so it is the aggressive option, not the recommendation.

## PER-REGION READ OF THE -a MOVE (the mechanism, 300 evts)
```
tag              dupB     dupT     dupE     fakB     fakT     effB     effT     effE
FINBASE       0.04344  0.03431  0.08106  0.06680  0.06903  0.92660  0.88213  0.74496
A_45          0.02741  0.03249  0.07934  0.06542  0.06738  0.92739  0.88289  0.74441
A_55          0.03080  0.03313  0.08021  0.06551  0.06725  0.92773  0.88238  0.74485
AJ_55_F15     0.03080  0.03214  0.07875  0.06551  0.06608  0.92773  0.88264  0.74496
AJ_55_W20     0.02851  0.02891  0.08021  0.05883  0.06086  0.92785  0.88238  0.74485
AJ_ALL        0.02372  0.02266  0.07750  0.05899  0.05986  0.92648  0.88162  0.74474
LST           0.00989  0.01264  0.08556  0.04249  0.04454  0.92557  0.88187  0.74596
```
`-a` is a BARREL-DUPLICATE knob: dupB .04344 -> .03080 (-29%) at -a 5.5 while barrel and
transition EFFICIENCY go UP and past LST (.92773 vs LST .92557, .88238 vs .88187). This is
exactly where the Baseline agent said the whole residual gap lives, and it is a knob that
was already in the frozen line at the wrong value.

## B6 (relaunched 23:44 after the OOM kill; 977 pair relaunched 00:07 staggered)

## B6 RESULT (00:18) -- `-F` COMPOSES WITH `-a` AND IS FREE ALL THE WAY DOWN
```
tag              flags added to FINBASE          eff      dup     fake     v510    v1030      d15      nTC
A_55             -a 5.5                      0.81037  0.05783  0.05506  0.73356  0.73739  0.61315   616959
AJ_55_F15        -a 5.5 -F 0.15              0.81045  0.05687  0.05454  0.73187  0.73739  0.61315   616318
AJ_55_F10        -a 5.5 -F 0.10              0.81045  0.05668  0.05448  0.73187  0.73739  0.61315   616192
AJ_50_F15        -a 5.0 -F 0.15              0.81045  0.05601  0.05463  0.73187  0.73579  0.61204   615677
AJ_55_F15_X35    -a 5.5 -F 0.15 -XCT 3.5     0.80966  0.05394  0.05461  0.73187  0.73739  0.61315   615016
AJ_55_F15_AT65   -a 5.5 -F 0.15 -AT3 6.5     0.81006  0.05780  0.05087  0.73187  0.73739  0.61315   613562
AJ_55_F15_W20    -a 5.5 -F 0.15 -W 0.20      0.81050  0.05562  0.05148  0.73019  0.72378  0.60312   613823
AJ_55_X375       -a 5.5 -XCT 3.75            0.81001  0.05623  0.05509  0.73356  0.73739  0.61315   616243
```
`-F` (the claim fraction) 0.20 -> 0.15 -> 0.10 keeps buying duplicate rate and fake for
~zero efficiency and ONLY a v510 nick of .0017 (a single track on a 285 denominator = the
named noise floor). `-a 5.0 -F 0.15` is the best three-metric point that leaves every
displaced band at the -a value: .81045 / .05601 / .05463.
B8 launched 00:19 to find where -F bottoms out and to redo the -XCT / -AT3 / -W frontier
on top of the new (-a 5.0, -F 0.10) corner.

## B8 RESULT (00:36) -- THE RECOMMENDED CORNER IS (-a 5.0, -F 0.10)
```
tag              flags added to FINBASE          eff      dup     fake     v510    v1030      d15      nTC
GATE/FINBASE     (none)                      0.80992  0.06230  0.05551  0.73356  0.74139  0.61650   618793
AJ_55_F05        -a 5.5 -F 0.05              0.81014  0.05649  0.05429  0.73019  0.73739  0.61315   615872
AJ_50_F10        -a 5.0 -F 0.10              0.81045  0.05582  0.05457  0.73187  0.73579  0.61204   615551
AJ_50_F10_X45    -a 5.0 -F 0.10 -XCT 4.5     0.81076  0.05996  0.05444  0.73187  0.73579  0.61204   617268
AJ_50_F10_X35    -a 5.0 -F 0.10 -XCT 3.5     0.80970  0.05294  0.05464  0.73187  0.73579  0.61204   614288
AJ_50_F10_AT65   -a 5.0 -F 0.10 -AT3 6.5     0.81006  0.05670  0.05093  0.73187  0.73579  0.61204   612793
AJ_50_F10_W20    -a 5.0 -F 0.10 -W 0.20      0.81045  0.05455  0.05149  0.73019  0.72218  0.60201   613056
LST                                          0.80988  0.05179  0.04476  0.65430  0.66453  0.55407   608190
```
`-F` bottoms out at 0.10: 0.05 gives back efficiency (.81014) for nothing. `-a 5.0 -F 0.10`
is the RECOMMENDATION -- it beats the assembled baseline on ALL THREE headline metrics
(+.00053 eff, -.00648 dup, -.00094 fake) and both flags are ALREADY IN THE FROZEN LINE, so
it adds ZERO new constants. Per region: dupB .04344 -> .02870, dupT .03431 -> .03186,
dupE .08106 -> .07807, with effB/effT/effE all >= baseline and effB/effT above LST.
B9 launched 00:38: the fake-facing corner (-AT3 6.5 x -W 0.20), -AT3 7, -XCT 4.25,
-a 4.5 -F 0.10, -WZ 0.

## B9 RESULT (00:53) -- THE FAKE-FACING CORNER, AND THE DISPLACED PRICE OF IT
```
tag                  flags added to FINBASE              eff      dup     fake     v510    v1030      d15
AJ_50_F10            -a 5.0 -F 0.10                  0.81045  0.05582  0.05457  0.73187  0.73579  0.61204
AJ_45_F10            -a 4.5 -F 0.10                  0.81023  0.05507  0.05480  0.72513  0.73419  0.60758
AJ_50_F10_X425       ... -XCT 4.25                   0.81063  0.05765  0.05451  0.73187  0.73579  0.61204
AJ_50_F10_AT7        ... -AT3 7                      0.80939  0.05764  0.04891  0.73187  0.73579  0.61204
AJ_50_F10_AT65_W20   ... -AT3 6.5 -W 0.20            0.80988  0.05544  0.04781  0.73019  0.72218  0.60201
AJ_50_F10_WZ0        ... -WZ 0                       0.81023  0.05378  0.04782  0.72850  0.71417  0.59309
LST                                                  0.80988  0.05179  0.04476  0.65430  0.66453  0.55407
```
`-WZ 0` (put the whole detector inside the -WE 0.20 band-aware braid instead of only
|eta| >= 1.5) is the single best fake lever found: eff .81023 (still ABOVE LST), dup .05378,
fake .04782 -- i.e. it closes ~70% of the remaining fake gap AND ~30% of the duplicate gap
in one pre-existing flag. Its price is displaced: v1030 -.0216, d15 -.0190, d510 -.0161,
d1030 -.0047 against our own baseline. It stays FAR above LST on the vxy bands
(v510 +.074, v1030 +.050, d15 +.039) but it moves the numbers the maintainer said to
protect, so it is offered as the FAKE-FACING ALTERNATIVE, not the recommendation.
977 confirmations launched 00:54: W_A50F10 (recommendation) and W_A50F10_WZ0.

## 977-EVENT CONFIRMATION, PAIR 1 (01:05) -- IT TRANSFERS
```
tag                     eff    vxy01      v15     v510    v1030      d15      dup     fake      nhB      nTC
W_X4 (= FINBASE)    0.80905  0.84144  0.80110  0.72161  0.71474  0.58320  0.06184  0.05607  9.80064  2033868
W_A55  (-a 5.5)     0.80933  0.84174  0.80044  0.72111  0.70986  0.58121  0.05733  0.05555  9.87686  2027663
W_A55F15 (+ -F .15) 0.80924  0.84166  0.80044  0.72010  0.70961  0.58121  0.05634  0.05506  9.87686  2025491
LST                 0.80987  0.84285  0.77719  0.64422  0.62567  0.51042  0.05138  0.04538 10.14984  1998494
```
per region 977: W_A55 effB .92560 (LST .92430) effT .87989 (.88004) effE .74402 (.74658);
dupB .03024 (baseline .04294, LST .00971); fakB .06617 (baseline .06754).
The direction and the barrel-duplicate mechanism transfer exactly; the SIZE is ~70% of the
300-evt move (dup -.0045 on 977 vs -.0065 on 300 for -a 5.5 alone). Displaced nicks are
also smaller than on the 300 (v510 -.0005, v1030 -.0049, d15 -.0020).
NOTE: the detached runs lose the json step (compare_ab has no ROOT env in a setsid shell);
regenerate with prototype/compare_ab.py --base fin_ref/fin_base977_hists.root by hand.

## THE DISPLACED BANDS IN TRACKS, NOT EFFICIENCY POINTS (a10_ref/a10_disp.py)
Every band value measured this round is an integer multiple of 1/denominator, so the
denominators fall straight out of the spectrum of observed values:
  v15 1370   v510 593   v1030 1250   d15 898   d510 248   d1030 423   (frozen 300)
Displaced cost of each candidate, in TRACKS vs the assembled baseline:
```
tag                      v15  v510 v1030   d15  d510 d1030 | worst
A_55                       0     0    -5    -3     0     0 |   -5
AJ_50_F10                  0    -1    -7    -4     0     0 |   -7
AJ_50_F10_X45              1    -1    -7    -4     0     0 |   -7
AJ_50_F10_AT65            -5    -1    -7    -4     0     0 |   -7
AJ_50_F10_W20              0    -2   -24   -13    -1    -1 |  -24
AJ_50_F10_AT65_W20        -6    -2   -24   -13    -1    -1 |  -24
AJ_50_F10_WZ0             -2    -3   -34   -21    -4    -2 |  -34
AJ_45_F10                 -3    -5    -9    -8     0     0 |   -9
```
THIS IS THE LINE BETWEEN THE TWO FAMILIES. The `-a`/`-F`/`-XCT`/`-AT3` family costs at most
7 displaced tracks out of 1250 (0.6%) and nothing at all in d510/d1030. The `-W`/`-WZ`
braid family costs 24-34 in v1030 and 13-21 in d15 -- 3-5x more, and it is the only family
that touches d510/d1030 at all. Both stay far above LST, but only the first is free.

## FINAL 977-EVENT CONFIRMATION (01:43) -- BOTH CANDIDATES CONFIRMED
```
tag                     eff    vxy01      v15     v510    v1030      d15     d510    d1030      dup     fake      nhB      nhT      nhE      nTC
W_X4 (= FINBASE)    0.80905  0.84144  0.80110  0.72161  0.71474  0.58320  0.24521  0.03151  0.06184  0.05607  9.80064  9.88373  3.55719  2033868
W_A55  (-a 5.5)     0.80933  0.84174  0.80044  0.72111  0.70986  0.58121  0.24521  0.03151  0.05733  0.05555  9.87686  9.89324  3.55693  2027663
W_A50F10 (RECOMMEND)0.80932  0.84176  0.79978  0.71910  0.70791  0.57923  0.24521  0.03151  0.05524  0.05507  9.89241  9.89455  3.54390  2023011
W_A50F10_WZ0 (alt)  0.80903  0.84153  0.79759  0.71357  0.68497  0.55739  0.23915  0.02958  0.05313  0.04831  9.88309  9.87420  3.54390  2006416
W_GATE (port best)  0.80213  0.83399  0.79737  0.72010  0.71474  0.58320  0.24521  0.03151  0.07692  0.04742  9.48700  9.77738  3.38597  2002505
LST                 0.80987  0.84285  0.77719  0.64422  0.62567  0.51042  0.22906  0.05402  0.05138  0.04538 10.14984 10.00937  3.55665  1998494
```
per region 977:
```
tag                  effB     effT     effE     dupB     dupT     dupE     fakB     fakT     fakE
W_X4              0.92454  0.87981  0.74436  0.04294  0.03375  0.08069  0.06754  0.06932  0.04578
W_A50F10          0.92553  0.88034  0.74385  0.02822  0.03112  0.07752  0.06596  0.06617  0.04567
W_A50F10_WZ0      0.92542  0.87897  0.74385  0.02357  0.02508  0.07752  0.04887  0.05621  0.04567
LST               0.92430  0.88004  0.74658  0.00971  0.01308  0.08486  0.04365  0.04542  0.04630
```
RECOMMENDATION `-a 5.0 -F 0.10` on 977: eff +.00027 / dup -.00660 / fake -.00100 vs the
assembled baseline. Against LST the three gaps go
  eff  -.00082 -> -.00055 (33% closed)
  dup  +.01046 -> +.00386 (63% closed)
  fake +.01069 -> +.00969 ( 9% closed)
with barrel duplicate .04294 -> .02822, barrel efficiency still ABOVE LST (.92553 vs
.92430), transition efficiency now ALSO above LST (.88034 vs .88004), track length UP
(nhB 9.801 -> 9.892) and displaced intact (v510 -.0025, v1030 -.0068, d15 -.0040 against
our own baseline; still +.0749 / +.0822 / +.0688 against LST, and d510 .24521 > LST .22906).
ALTERNATIVE `-a 5.0 -F 0.10 -WZ 0`: dup +.00175 and fake +.00293 from LST -- it nearly
closes BOTH remaining gaps at eff -.00084 -- but it spends a THIRD of the displaced margin
(v1030 margin over LST +.0891 -> +.0593, d15 +.0728 -> +.0470). Offered, not recommended.

## DONE. No source edits all round; every number is a flag move on binary
## 519b0abc34a28cd1e803d6b9407ef224 (== protoFIN == protoXC).
