# SYNTHESIS AGENT -- STATUS

Artifact dir: `standalone/synth_ref`   Code: `standalone/protoFINAL`

## M0 -- merge decision (2026-08-05 02:30)

protoFINAL = protoFIN + **A11 delta** (AttachDelivery.h/.cc + main.cc) + **A15 delta**
(PixelAttach.h/.cc + main.cc). 3-way `git merge-file` on main.cc, base = protoFIN:
**zero conflicts**, 5678 + 45 (A11) + 75 (A15) = 5798 lines exactly. Binary md5
`45c68733cf0255d938762a4521e5a25b`.

### Why these two and not the others

| agent | offer | decision | reason |
|---|---|---|---|
| A11 | `-T3F`, `-RPSA`, `-RPST` | **TAKE THE CODE** | code-auditor: A11's is the preferred form of BOTH duplicated mechanisms. Its `-T3F` is mask-level (cheaper, NaN-safe) vs A05's `-AT3F` in the scored loop; its `-RPSA/-RPST` patch **all three** `-RPS` copies (A05 patched 2 of 3) and resolve both sentinels in one place; parser order longest-first is correct (A13b's is not). |
| A15 | `-XC4` | **TAKE THE CODE** | zero new tuned constants; auditor's composition hazard is against pair-log *consumers* (-XCO / -RPS 3|4 / -DC / -XCH) -- **none of which I am taking**. Verified at source level that `-XC4` writes only `ga.pairLog` and never `plsBestChainLogit`, so A11's `-RPSA` is provably independent of it. |
| A04 / A10 / A12 | `-a`, `-F` | **flags only** | already in protoFIN; three agents converged on the same lever. |
| A06 | `-EXR`, `-EXW`, `-L` | **flags only** | already in protoFIN. |
| A03 | `-ATS` | **REJECT** | code-auditor HIGH: `protoA03/main.cc` post-dates every binary it reported from by ~25 min -- the source is not provably the code that produced the numbers. Also a 3-way merge conflict with A05/A14 on the same line, and A14 measured the per-region delivery calibration as **already correct** on the assembled baseline. |
| A05 | `-AT3F` | **REJECT (superseded)** | same mechanism as A11's `-T3F`, inferior form, and its own headline "both copies now read one threshold" is a 2-of-3 fix. |
| A02 | `-XCH` head | **REJECT** | trained component (simplicity cost); auditor JET SAFETY: f[1]=\|eta_seed\| and f[14]=\|eta_TC\| let the MLP synthesize an eta-proximity term, defeating the no-proximity rule's purpose; silent-degradation hazard (missing header disables the whole arm); port cost (duplicate hash map). Its gains overlap `-T3F` + `-a` + `-RPSA`. |
| A01 | `-XCO` | **REJECT (overlaps)** | pair-log consumer (composition hazard with `-XC4`); its barrel-eff claim inverts on the 977; it retires the same bare type-8 rows as `-RPSA` (barrel dup deltas -.01352 vs -.01353 -- verifier-1 flags these as almost certainly non-additive). `-RPSA` is the cheaper form (per-seed scalar, no pair log). |
| A09 | `-RPS 3 -AT3 7` | **REJECT** | its own do-not-ship rule: dominated below dup .0670; we land far below that. |
| A13 | `-RPSA` | merged into A11's | identical implementation, A11's placement is correct. |
| A07/A08/A14 | nothing | -- | diagnosis + screening rules, carried into the verdict. |

## M1 -- gates and reproduction (in progress)

### M1 -- MERGE FIDELITY PROVEN (02:55)

`RA11` = protoFINAL with A11's own recommended line (`-T3F 0.10 -RPSA 5.0 -RPST 6 -a 6.0`)
reproduces `a11_ref/r_C2_A60R50` **exactly** on all eight headline metrics AND on nTC
(613601 = 613601). A11's code is faithful in the merged tree.
`FGATE` / `RA15` / `RA10` still running (they lack `-T3F`, which cuts the attach stage
~4x, so they are ~3x slower).

### M2 -- FIRST COMPOSITION (frozen 300)

```
tag                        eff     dup    fake     nhB     nhT     nhE     nTC
FINBASE (assembled)    0.80992 0.06230 0.05551 9.80254 9.87906 3.55956 618793
C1  T3F+XC4            0.81037 0.05722 0.04903 9.82958 9.93420 3.56597 613422
C2  +RPSA 5.5          0.81014 0.05340 0.04909 9.89369 9.93875 3.56858 611968
C3  +EXR 4.0           0.81010 0.05335 0.04921 9.91901 9.99017 3.57478 611967
C4  T3F+RPSA5.0+EXR    0.81006 0.05841 0.04913 9.92515 9.96229 3.55753 613768
LST (target)           0.80988 0.05179 0.0447610.1480410.01546 3.56248 608190
```

THE LEVERS ARE ESSENTIALLY ADDITIVE IN DUPLICATE RATE, which was not guaranteed:
standalone `-XC4` is -.00562, `-RPSA 5.5` is -.00377, `-T3F` is +.00068; naive sum from
.06230 is .05359 against the measured C2 .05340. So `-XC4` (enumeration fix, pair-log
side) and `-RPSA` (per-seed scalar bar) really are independent mechanisms, exactly as the
source-level reading said.

Displaced at C3: v15 +.0007, v510 -1 track (the known `-EXR` cost), v1030/d15/d510/d1030
BIT-IDENTICAL to the assembled baseline.

### M3 -- GATES AND REPRODUCTIONS ALL PASS (03:10)

* **NO-OP GATE**: `FGATE` (protoFINAL, assembled baseline, every new flag at its default)
  vs `fin_ref/r_FINBASE`: **33 PRE-EXISTING branches IDENTICAL, 0 DIFFER, 0 MISSING,
  0 ADDED** under `rebase_ref/cmp_branches.py`. Raw bytes, no tolerance.
* **A11 reproduced**: `RA11` == `a11_ref/r_C2_A60R50`, every metric + nTC 613601.
* **A15 reproduced**: `RA15` == `a15_ref/r_X4`, every metric + nTC 616531.
* **A10 reproduced**: `RA10` == `a10_ref/r_AJ_50_F10`, every metric + nTC 615551.
  (A10 changed no code; this proves the flag path and the frozen prefix are identical too.)

The merge is faithful and the defaults are a bit-exact no-op.

### M4 -- THE DESCENT (14 runs on the frozen 300) and the LEVER LEDGER

Every lever added one at a time from the assembled baseline. All five are load-bearing;
none is within noise on the quantity it is there for.

```
step                        eff      dup     fake      nhB      nhT   |  what it bought
FINBASE (assembled)     .80992   .06230   .05551   9.8025   9.8791   |  --
+ -XC4 1        (RA15)  .80979   .05668   .05562   9.8089   9.9079   |  dup  -.00562
+ -T3F 0.10     (C1)    .81037   .05722   .04903   9.8296   9.9342   |  fake -.00659, eff +.00058
+ -RPSA 5.5     (C2)    .81014   .05340   .04909   9.8937   9.9388   |  dup  -.00382
+ -EXR 4.0      (C3)    .81010   .05335   .04921   9.9190   9.9902   |  nhB +.025 nhT +.051 (rates flat)
+ -a 6.0        (D1)    .81054   .05285   .04893   9.9256   9.9926   |  eff +.00044 AND dup -.00050
LST (target)            .80988   .05179   .04476  10.1480  10.0155   |
```

FRONTIER PROBES (all from D1's five levers, moving ONE thing):
```
E4  -RPSA 4.5           .81006   .05167   .04893   9.9480   9.9996   dup BELOW LST
D5  -RPSA 5.0 (@C3)     .80992   .05267   .04922   9.9318   9.9919
E2  -XCT 4.25           .81072   .05488   .04888   9.9033   9.9705
E3  -XCT 4.25 -L 5.0    .81076   .05496   .04890   9.9115   9.9910
D3  -XCT 4.5            .81103   .05751   .04883   9.8761   9.9419
D6  -XCT 5.0            .81143   .06525   .04896   9.7984   9.8616
E1  -L 5.0              .81059   .05294   .04895   9.9337  10.0132   nhT at LST
D4  -L 5.0 -EXW 0.50    .81041   .05288   .04918   9.9547  10.1765   nh ABOVE LST overall
```

REJECTED FOR NOISE / COST:
* `-L 5.0` (E1): +.00005 eff, +.00009 dup -- inside noise on the rates. It buys +0.021 of
  nhT, but D1 is already at nhT -0.023 and nhE +0.010, so it fixes nothing that is missing,
  and it costs 5 more distinct displaced sims (v510 -1, v1030 -3, d15 -1 on the 300). It
  also reaches upstream into chain construction (K6 score), the one lever in the round with
  a real conflict surface. DROPPED on the standing "drop any lever within noise" rule.
* `-EXW 0.50` (D4): pushes overall length ABOVE LST (6.557 vs 6.519) but only reaches
  transition/endcap, which are already at/above LST at D1; barrel stays -0.19. Doubles the
  displaced spend. DROPPED -- but it is the config to take if the maintainer rules that
  aggregate length must beat LST.
* `-XCT` upward (D2/D3/D6, E2/E3): the exchange rate is 12-21 duplicates per unit of
  efficiency, four to twenty times worse than `-a`. Duplicate rate outranks fake rate and
  we have no duplicate budget, so this direction is not affordable. `-XCT` STAYS AT 4.

### M5 -- DISPLACED, IN DISTINCT SIMS (a08_ref/a08_distinct.py, frozen 300)

```
tier      nSim     LST   FINBASE      D1     D1's lead over LST
DISP1     4809    2375      2575    2571          +196  (baseline +200)
DISP5     3441    1319      1488    1483          +164  (baseline +169)
DISP10    2848     931      1053    1049          +118  (baseline +122)
DISP30    1599     101       127     126           +25  (baseline  +26)
```
D1 spends **4 distinct displaced sims out of a +200 lead (2%)**, all of it the `-a 6.0`
half, and every one of them a bare T5-class chain -- exactly the mechanism A04/A12
described and A08's screening rule predicts. d510 and d1030 are bit-identical to the
assembled baseline in every run of this synthesis.

### M6 -- FULL 977, THE NUMBERS OF RECORD

```
tag                        eff     dup    fake     nhB     nhT     nhE      nh      nTC
W_X4 (assembled base)  0.80905 0.06184 0.05607 9.80064 9.88373 3.55719 6.44243 2033868
W_D1 (PRIMARY)         0.80978 0.05246 0.04939 9.92631 9.99670 3.56988 6.49987 2010825
W_C2_A60R50 (A11 best) 0.80990 0.05743 0.04919 9.90572 9.91899 3.54733 6.46252 2016597
W_XC4 (A15 best)       0.80884 0.05629 0.05618 9.80687 9.91260 3.57512 6.46420 2026593
LST (target)           0.80987 0.05138 0.0453810.1498410.00937 3.55665 6.51862 1998494
```
vs LST: eff -.00009 | dup +.00108 | fake +.00401 | nTC +0.6% | nh -0.019
vs the assembled baseline: eff +.00073 | dup -.00938 | fake -.00668 | nh +0.057
The duplicate gap against LST is cut **90%** (+.01047 -> +.00108) and the fake gap **62%**
(+.01069 -> +.00401), while efficiency IMPROVES from -.00083 to -.00009.

The 300 -> 977 shift for D1 is -.00076 in efficiency, in line with the round's -.0006 to
-.0010 for every configuration, so the frozen 300 was a faithful iteration set here too.

### M7 -- DISPLACED ON THE FULL 977, IN DISTINCT SIMS

```
tier      nSim     LST   assembled base    W_D1     W_D1's lead over LST
DISP1    16369    7782             8512    8498          +716   (base +730)
DISP5    11809    4238             4859    4845          +607   (base +621)
DISP10    9819    2956             3423    3411          +455   (base +467)
DISP30    5721     392              494     493          +101   (base +102)
```
W_D1 spends **14 distinct displaced sims out of a +730 lead (1.9%)**, LOST 8 / GAINED 22
at DISP1 (so it is not even one-sided), and 12 of the 12 DISP10 losses are bare T5-class
chains -- the `-a 6.0` mechanism A04/A12 described and A08's screening rule predicts.
`dxy[5,10)` and `dxy[10,30)` are BIT-IDENTICAL to the assembled baseline in every run of
this synthesis, on both samples.

### M8 -- PLOTS
`standalone/performance/finishline_chainp-PU200_chainp-PU200/mtv/var/` (176 png + pdf),
LST vs ChainFinal on the full 977. Generated by `synth_ref/syn_plots.sh`.

### M9 -- THE ALTERNATE, AND THE VERDICT

```
FULL 977                  eff       dup      fake        nh       nTC
LST (target)          .80987    .05138    .04538    6.5186   1998494
CHAINFINAL   (D1)     .80978    .05246    .04939    6.4999   2010825
CHAINFINAL-D (E4)     .80938    .05133    .04938    6.5080   2007824
assembled baseline    .80905    .06184    .05607    6.4424   2033868
```
The alternate is ONE number different (`-RPSA 4.5` instead of 5.5) and it buys the
duplicate criterion outright (.05133 vs LST .05138) for .00040 of efficiency.

VERDICT AGAINST EACH FINISH-LINE CRITERION (primary, full 977):
1. FULL algorithm, nothing from the deletion set -- MET. Inherited from the lineage and
   re-verified for the one new upstream input: `t3_fakeScore` is a T3-LEVEL branch,
   computed at T3 build time, already consumed by ChainFeatures 20/21. Nothing else new
   reads anything but our own chains and our own attach logits.
2. General pLS-OT matching that produces the pT3-class object -- MET. 121.4 pT3-class rows
   delivered per event (LST's own pT3 count ~151.7/evt), through the general stage-B
   matching, with the OT-side contention and the pixel-side family dedup.
3. Matching-or-better PROMPT efficiency -- MISSED BY .00009 (~18 sims of 204388). Below the
   ~107-track 1-sigma floor A08 measured, and the baseline it started from was -.00083.
   Per region barrel +.00151 and transition +.00015 are ABOVE LST; the whole deficit is
   endcap (-.00178). Call it parity, not a win.
4. MUCH better DISPLACED -- MET decisively, and PROTECTED. +716 distinct displaced sims
   (8498 vs 7782), v510 +.0764, v1030 +.0864, d15 +.0711, v15 +.0239, d510 +.0162.
   ONE band is below LST: d1030 (-.0225), inherited from the baseline and moved by nothing
   this round. Cost of the whole synthesis: 14 distinct sims of a 730 lead (1.9%).
5. Same-or-better DUP -- MISSED BY +.00108 on the primary; MET on the alternate (-.00004).
   Endcap duplicate rate .07184 is BELOW LST's .08486 on both. The residual is barrel
   (.03056 vs .00971) and transition (.02793 vs .01308).
6. Same-or-better FAKE -- MISSED BY +.00401 (the baseline was +.01069, so 62% closed).
   Endcap fake .04310 is BELOW LST's .04630. The residual is barrel + transition.
7. Track length not regressed -- MIXED. Endcap +0.013 ABOVE LST, transition -0.013,
   barrel -0.224, aggregate -0.019 (baseline -0.076). On GOOD rows only (dilution divided
   out) transition is -0.002 and endcap +0.044; barrel is -0.158, and its f12 .612 against
   LST's .789 is the saturating inner-layer limit A06 identified, not something this
   synthesis can reach.
