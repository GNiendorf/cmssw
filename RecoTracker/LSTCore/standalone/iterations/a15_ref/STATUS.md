# A15 -- FULL DUP DECOMPOSITION on the assembled baseline, then attack the top cell

Workspace: `standalone/protoA15` (copy of protoFIN). Artifacts: `standalone/a15_ref/`.
Runner: `a15_ref/a15_run.sh <TAG> [overrides]` -- identical frozen prefix to
fin_ref/fin_run.sh; adds `DUMPHITS=1` to switch on the pre-existing env-gated `tc_hitOT`
diagnostic branch (`PROTO_DUMP_TCHITS`). Batch: `a15_ref/a15_scan.sh`.

## M0 -- METRIC FIDELITY (established before any measurement)
The scoreboard `dup` / `fake` are the harness `Root__TC_{dr,fr}_*_eta` ratios, i.e. they
count ONLY TCs with **pt > 0.9** (`ana.pt_cut`). On the LST 300-evt file that is 473752 of
608190 TCs: dup **.05179** (headline) vs .05032 unrestricted. `tc_isDuplicate` itself is
computed over ALL TCs. `a15_dup.py` computes flags on everything and reports on the
pt>0.9 universe; it reproduces the file's own `tc_isDuplicate` branch with **0 mismatches
over 608190 TCs** and the headline .05179 exactly.

## M1 -- NO-OP GATE: PASSED EXACTLY
`A15GATE` (assembled baseline on protoA15, binary 519b0abc...) = FINBASE on every metric
(eff .8099 dup .06230 fake .05551 nTC 618793) and **33/33 branches IDENTICAL** to
fin_ref/r_FINBASE_ND.root under rebase_ref/cmp_branches.py.
`A15DUMP` (same + `-XCD 2` + DUMPHITS=1) is identical on all 14 metrics, so the
`tc_hitOT` dump is inert.
No source edit was needed for the decomposition: `tc_hitOT`, `tc_simIdxAll` and
`tc_isChain` are pre-existing writer branches. `tc_isChain` carries the OutDeliv
provenance code, so the delivery classes separate exactly:
  cPLS = carried LST type-8 row | zPLS = **-ZP8 synthetic bare seed** (deliv 4)
  chT5 / chT4 = bare chain TC   | aPT5 / aPT3 = our attach deliveries
(There are no cPT5 / cPT3 rows on this line: `-ZP5 1` and `-RT3 1` drop them wholesale.)

## M2 -- THE DECOMPOSITION (a15_ref/dec_PROTO300.txt, dec_LST300.txt, dec_CMP.txt)

### per class, ours (metric universe, 300 evts)
```
class      nTC    %TC     nDup   %dup  dupRate | dupRate B/T/E        | fakeRate
cPLS    180173  37.5%    16670  55.7%  0.09252 | .0504 / .0496 / .0950 | 0.04750
aPT5    159360  33.2%     1044   3.5%  0.00655 | .0029 / .0070 / .0106 | 0.00760
chT5     98525  20.5%     5745  19.2%  0.05831 | .0664 / .0361 / .0763 | 0.09214
aPT3     27995   5.8%      461   1.5%  0.01647 | .0061 / .0128 / .0448 | 0.17389
chT4      7801   1.6%     1663   5.6%  0.21318 | .0333 / .2794 / .3372 | 0.33137
zPLS      6455   1.3%     4340  14.5%  0.67235 | .7121 / .6783 / .5985 | 0.05639
```
**zPLS is 1.3% of the TCs and 14.5% of the duplicates** -- a duplicate rate of 0.672,
7.3x the carried seeds' 0.093. These are exactly the seeds LST's (deleted)
CrossCleanpLS used to kill and that `-ZP8 6` re-admits as the post-deletion universe.

### the cells, ours vs LST, per event (dec_CMP.txt)
total dup pairs 54.13/evt vs LST 44.57 (+9.56). Sorted by |delta|:
```
cell                proto/evt   LST/evt     delta
PLS+T5   barrel          9.06      1.39     +7.67   <== THE cell (= zPLS+chT5)
PLS+PT5  endcap          0.42      4.94     -4.52   (we are BETTER)
PLS+T5   transition      2.61      0.70     +1.90
PLS+T4   endcap          4.77      3.06     +1.71
PLS+T5   endcap          3.59      1.99     +1.60
PT5+T5   B+T+E           2.15      0.00     +2.15   (aPT5+chT5 braids; LST has 1 pair)
PLS+PLS  endcap         27.34     27.61     -0.27   (inherited, identical to LST)
```
Structure mix: one-noOT 21.60 vs 13.07 (+8.54), partial 3.95 vs 2.95 (+1.00),
identical/subset 0.00 vs 0.72 (LST has same-hit-set T5 pairs; we have none).

### ORACLE yields (perfect resolution of one cell; simsLost = 0 for every row)
```
cPLS+cPLS  -.03070   (LST's own PLS+PLS oracle is -.03176 -- the SAME floor, inherited)
chT5+zPLS  -.01376   <== the top addressable cell, and LST does not have it at all
cPLS+chT5  -.00369   chT4+zPLS -.00316   cPLS+chT4 -.00300   aPT5+chT5 -.00252
chT5+chT5  -.00121   aPT3+chT5 -.00082   aPT3+aPT5 -.00069
structure: one-noOT -.02452  both-noOT -.03082  partial -.00457  disjoint -.00140
```

## M3 -- THE HIT-OVERLAP AXIS IS EXHAUSTED (negative result, do not re-measure)
`a15_shadow.py` replays ownership-map sweeps (the -CC / K9 shape) over the delivered TCs:
```
rule (victims / kill threshold)              dup     d(dup)    fake    d(fake)  simsLost
all OT classes, >=1 shared OT hit          .05783  -.00447  .04561  -.00990      5832
all OT classes, >=2 shared OT hits         .05898  -.00332  .04802  -.00750      2402
all OT classes, >=3 shared OT hits         .06226  -.00004  .05541  -.00010        14
all OT classes, shared/own >= 0.30         .06210  -.00020  .05535  -.00016        20
all OT classes, shared/own >= 0.40         .06227  -.00003  .05546  -.00006         7
all OT classes, shared/own >= 0.50/0.60    .06230  +.00000  .05551  +.00000       0/2
```
A count-based rule (>=1 or >=2 shared hits) does buy dup AND fake, but only by dropping
6-11k TCs and orphaning 2.4-5.8k sims -- it is hitting the K9 claim's legitimate
`-F 0.20` tolerance, not duplicates. A FRACTION-based rule -- the only shape that could
separate a braid from a neighbour -- yields **nothing**: at shared/own >= 0.5 it drops 17
TCs in 300 events. Reason: the K9 claim already enforces that budget upstream, so every
surviving OT-sharing duplicate pair shares 1-2 hits out of 10-14. Consistent with the
structure table: our OT-sharing dup pairs are 100% "partial", ZERO "identical" or
"subset". **No hit-overlap contention can reach the -.00457 "partial" oracle.**

The fraction rule has a CLIFF exactly at the K9 budget, which is the proof it is just
`-F` in disguise (frac / dup / fake / simsLost):
  0.25 -.00025 / -.00192 /   72     0.20 -.00272 / -.00700 / 1541
  0.15 -.00375 / -.00836 / 2890     0.10 -.00438 / -.00986 / 4603
Anything that reaches the braids reaches the legitimate claim tolerance in the same step.

## M3b -- CELL DRILL-DOWN (a15_ref/drill.txt): the blessed dR^2 window is the real ceiling
```
cell        pairs/evt  dR q10/q50/q90        inside dR^2<0.02   |eta| median  shared OT
chT5+zPLS      11.85   .0160 / .0699 / .1769      66.2%             0.60        0 always
cPLS+chT4       2.87   .0191 / .0583 / .1177      94.8%             2.38        0 always
chT4+zPLS       2.86   .0149 / .0424 / .0879      98.6%             2.25        0 always
aPT5+chT5       2.16   .0024 / .0156 / .0657     100.0%             1.35        2 in 79%
```
Two decisive readings.
1. **A THIRD of the top cell (chT5+zPLS) lies OUTSIDE the ported dR^2 < 0.02 window.** The
   window is maintainer-blessed verbatim, so no threshold and no retrained head can ever
   reach those 4.0 pairs/evt. The reachable part of that cell is 66%, not 100%.
2. **The chT4 cells are 95-99% INSIDE the window** -- they are blocked by the enumeration,
   not by the geometry. That is what makes them the right target.
3. aPT5+chT5 shares exactly 2 OT hits in 79% of pairs: precisely the K9 `-F 0.20`
   tolerance for a 10-hit chain. Confirms M3.

## M4 -- THE ATTACK: `-XC4`, the pair-log ENUMERATION HOLE
Found by reading the port: the bare-chain arm of the ported CrossCleanpLS is driven from
`ga.pairLog`, and the pair enumeration (`PixelAttach.cc`,
`k8EnumeratePrefilteredPairsGeneral`) has `if (chains.nLayers[c] < 5) continue;  // v1
scope: attach only to pT5-class chains`. So **a delivered 4-layer chain TC is invisible to
the arm** -- `chainPairs.find(c)` misses and the seed in front of it survives at ANY -XCT.
The two cells this blinds are cPLS+chT4 (861 pairs) and chT4+zPLS (857 pairs) = 5.7
pairs/evt, oracle **-.0062**, and none of it is reachable today.

`-XC4 1` closes it with a SCORE-ONLY pass over the accepted 4-layer chains: same head,
same prefilter windows, same dR^2 < 0.02 window, same -XCT threshold, and it writes
NOTHING but the pair log -- not chainPls, not plsOwned, not plsBestChainLogit (so the
-RPS predicate is untouched) and no delivery. `-XC4T` is an optional separate threshold
for the 4-layer arm (default = follow -XCT), because a chT4 anchor is weaker evidence
(fake rate .331 vs chT5's .092). Default `-XC4 0` is bit-identical.
Source delta: `AttachParams::minChainLayers` (default 5), one guard in PixelAttach.cc,
the score-only pass + flag + ledger line in main.cc. Binary bababf39b51bca0436c693bd82e8a0dc.

## M5 -- BATCH 1 RESULT: `-XC4 1` WORKS. NO-OP GATE ON THE NEW BINARY PASSED EXACTLY.
`A15G2` (new binary, `-XC4` at its default 0) == `A15GATE` on all 14 metrics AND
**33/33 branches IDENTICAL**. `X4D` (`-XC4 1 -XCD 2`) == `X4` 33/33, so `-XCD` stays inert.
```
tag                       eff      dup     fake      nhB      nhT      nhE      nTC
A15GATE / A15G2       0.80992  0.06230  0.05551  9.80254  9.87906  3.55956   618793
X4T6   -XC4 1 -XC4T 6 0.80988  0.05745  0.05562  9.80555  9.90478  3.57424   616984
X4T5   -XC4 1 -XC4T 5 0.80979  0.05696  0.05563  9.80739  9.90632  3.57622   616746
X4     -XC4 1         0.80979  0.05668  0.05562  9.80890  9.90786  3.57795   616531
X4T3   -XC4 1 -XC4T 3 0.80957  0.05648  0.05560  9.81061  9.90876  3.57985   616317
LST(base)             0.80988  0.05179  0.04476 10.14804 10.01546  3.56248   608190
per region      effB     effT     effE     dupB     dupT     dupE     fakB     fakT     fakE
A15GATE      0.92660  0.88213  0.74496  0.04344  0.03431  0.08106  0.06680  0.06903  0.04525
X4T6         0.92660  0.88187  0.74496  0.04313  0.02945  0.07375  0.06681  0.06917  0.04537
X4           0.92660  0.88136  0.74496  0.04286  0.02921  0.07256  0.06682  0.06916  0.04536
LST(base)    0.92557  0.88187  0.74596  0.00989  0.01264  0.08556  0.04249  0.04454  0.04603
```
**`-XC4 1` buys -.00562 of duplicate rate for -.00013 of efficiency and +.00011 of fake,
and it makes tracks LONGER (+0.006 / +0.029 / +0.018).** It closes 53% of the duplicate
gap against LST (+.01051 -> +.00489). Displaced is UNTOUCHED at every point except -XC4T 3
(v510 -.0017 = one track, noise). Endcap duplicate rate .08106 -> .07256, now well below
LST's .08556; transition .03431 -> .02921. Ledger: 5573 (seed, 4-layer chain) pairs logged
per event, 242.4 seeds/evt retired BY a 4-layer target (the chain arm goes 359.4 ->
606.1/evt), and `upgT5` / `delivT3` per event are byte-identical -- the pass really is
score-only.
`-XC4T 6` is the efficiency-leaning variant (eff .80988 = LST's own .80988 exactly, dup
-.00485) but it costs a SECOND tuned constant. `-XC4 1` alone adds ZERO tuned constants
(it follows -XCT), which is why it is the recommendation.

## M6 -- RESUME (23:43): batch 2 had been double-launched; killed and relaunched as b3.sh
Two copies of `b2.sh` were running concurrently against the same output paths. Killed both
sets and relaunched the four surviving points cleanly as `a15_ref/b3.sh`:
  W_XC4  (977 evts, -XC4 1)   <- final confirmation vs fin_ref/r_W_X4.json (= FINBASE @977)
  X4DUMP (300, DUMPHITS=1 -XCD 2)  <- post-attack decomposition
  X4_T45 (300, -XC4 1 -XCT 4.5)    X4_T35 (300, -XC4 1 -XCT 3.5)
The 977 BASELINE is NOT re-run: fin_ref/r_W_X4.json already carries it (that agent's tag
"W_X4" is FINBASE at 977; my "W_XC4" is FINBASE + -XC4 1 -- do not confuse the two names).
INTEGRITY CHECK on the batch-1 outputs after the double launch: all seven ROOT files have
exactly 300 entries, kRecovered=0, 20 MB. Independent corroboration: X4D == X4 on 33/33
branches and A15G2 == fin_ref/r_FINBASE_ND (written by another agent) on 33/33 -- a
truncated file could not match branch-for-branch. Batch-1 numbers stand.

## M7 -- POST-ATTACK DECOMPOSITION (a15_ref/dec_X4_300.txt): the fix is SURGICAL
Same decomposition rerun on `r_X4.root` (baseline + `-XC4 1`). The two target cells, and
nothing else, moved:
```
cell            pairs BEFORE   pairs AFTER    change
cPLS+chT4              861          179       -79%
chT4+zPLS              857           35       -96%
-- everything else --
cPLS+cPLS             8159         8124        -0.4%   (inherited from LST, untouchable)
chT5+zPLS             3556         3515        -1.2%
cPLS+chT5             1022         1019        -0.3%
aPT5+chT5              648          648         0
ALL                  16239        14627        -9.9%
```
The chT4 CLASS duplicate rate goes 0.21318 -> 0.04282 at IDENTICAL nTC (7801 both) --
proof the pass is score-only: it removed the SEED in front of the 4-layer chain, not the
chain. zPLS rows 6455 -> 5548 (907 retired), zPLS dup rate .67235 -> .63536.
Residual oracle table (dup after solving that cell perfectly, simsLost = 0 everywhere):
```
cPLS+cPLS  -.03082   INHERITED. LST's own PLS+PLS oracle is -.03176. Not addressable.
chT5+zPLS  -.01372   THE remaining cell. 66% inside the blessed dR^2 window (M3b).
cPLS+chT5  -.00370   aPT5+chT5 -.00253  chT5+chT5 -.00122  cPLS+chT4 -.00062 (was -.00300)
```
Take the two floors out and our addressable residue against LST is small: our dup .05668
vs LST .05179, and .0308 of ours is the same PLS+PLS floor LST carries.

## M8 -- IN FLIGHT (launched 23:43-23:47)
  W_XC4  977 evts, baseline + -XC4 1  (compare to fin_ref/r_W_X4.json = FINBASE @977)
  X4DUMP 300, DUMPHITS=1 -XCD 2       (structure axis of the post-attack decomposition)
  X4_T45 / X4_T35  300, -XC4 1 with -XCT 4.5 / 3.5  (does -XC4 move the -XCT optimum?)
  ANAT   300, protoA15b binary, -XC4 1 -XCD 3  (CLASS-A SURVIVOR ANATOMY, see below)

`protoA15b` = copy of protoA15 + a DIAGNOSTIC-ONLY `-XCD 3` (5 hunks, ~50 lines, all under
`xcDiag >= 2.5f`). Built in a separate dir so the running protoA15 binary is untouched.
It answers the one question that decides the next round: of the class-A survivors (seeds
whose sim is already covered and which we still emit -- duplicates by construction), how
many had NO enumerated in-window (seed, chain) pair at all (= another ENUMERATION hole,
fixable the -XC4 way) versus had one whose logit fell short (= HEAD QUALITY, needs the
purpose-built dedup head). Bit-exactness is proven by the run itself: ANAT's ROOT output
must be 33/33 identical to r_X4.root, which is the old binary at -XCD 0.

## M7b -- POST-ATTACK LIKE-FOR-LIKE vs LST (a15_ref/dec_CMP_X4.txt)
The PLS+T4 cells FLIP from a deficit to a surplus, i.e. we go from worse than LST to
better than LST on 4-layer duplicates:
```
cell            before -XC4   after -XC4    LST     verdict after
PLS+T4 | E        +1.71         -2.49       3.06    BETTER than LST
PLS+T4 | T        +0.64         (off list)  0.05    ~parity
T4 class dup rate  .21318        .04282     .12908  BETTER than LST at equal nTC
```
The residual duplicate gap against LST is now ONE shape: PLS+T5 (B +7.67, T +1.84,
E +1.52 = 11.0 pairs/evt) plus the PT5+T5 braid (+2.15/evt, hit-overlap-exhausted, M3).
Everything else in the table is at or better than LST.

## M9 -- BATCH 3 RESULT: `-XC4 1` SHIFTS THE WHOLE `-XCT` FRONTIER (300 evts)
The -XCT bracket rerun with -XC4 1. Compare column-for-column against the assembled
baseline's own bracket (fin_ref): at MATCHED efficiency the -XC4 1 curve sits ~.0056
lower in duplicate rate, uniformly, and fake does not move.
```
-XCT     -XC4 0 (assembled baseline)        -XC4 1 (this round)          d(eff)   d(dup)
         eff      dup      fake             eff      dup      fake
5.0      -        -        -                (batch 4)
4.5    .81037   .06675   .05539           .81023   .06131   .05550      -.00014  -.00544
4.0    .80992   .06230   .05551           .80979   .05668   .05562      -.00013  -.00562
3.5    .80904   .05925   .05558           .80886   .05351   .05568      -.00018  -.00574
```
X4DUMP (same point + DUMPHITS + -XCD 2) is identical to X4 on all 14 metrics: the dump
and the -XCD 2 diagnostic are inert on this binary too.

### THE STRICT PARETO POINT: `-XC4 1 -XCT 4.5`
Against the ASSEMBLED BASELINE (-XC4 0 -XCT 4 = .80992 / .06230 / .05551) it is better on
ALL THREE at once: eff +.00031, dup -.00099, fake -.00001. It also keeps overall
efficiency ABOVE LST's .80988 (.81023), which -XCT 4 does not (.80979, -.00009).
Per region: effB .92705 effT .88264 effE .74507 (LST .92557/.88187/.74596) --
barrel and transition ABOVE LST; dupB .04999 dupT .03719 dupE .07485 (LST
.00989/.01264/.08556) -- endcap still better than LST; fake .06649/.06888/.04537.
### THE DUPLICATE-LEANING POINT: `-XC4 1 -XCT 4`
eff .80979 / dup .05668 / fake .05562: -.00562 of duplicate rate for -.00013 of
efficiency, i.e. it closes 53% of the duplicate gap against LST (+.01051 -> +.00489).
DISPLACED IS UNTOUCHED at both points (v510 .73356, v1030 .74139, d15 .61650, d510 .20161,
d1030 .03073 -- all bit-equal to the baseline). Only -XCT 3.5 moves one v510 track.
TRACK LENGTH IMPROVES at -XCT 4 (nh 9.809/9.908/3.578 vs the baseline's 9.803/9.879/3.560)
and is roughly flat at -XCT 4.5 (9.761/9.858/3.572).

## M10 -- BATCH 4 LAUNCHED (00:17)
  W_XC4_T45  977 evts, -XC4 1 -XCT 4.5   (the Pareto point on the full sample)
  X4_T50     300, -XCT 5.0   X4_T425  300, -XCT 4.25   (frontier above 4.5)
W_XC4 (977, -XCT 4) still running from batch 3.

## M11 -- THE ANATOMY RESULT (protoA15b, `-XCD 3`): THE ENUMERATION AXIS IS NOW CLOSED
NO-OP GATE FIRST: r_ANAT.root == r_X4.root on **33/33 branches** (`rebase_ref/cmp_branches.py`,
300/300 entries). The protoA15b diagnostic build and `-XCD 3` are both provably inert.

Truth partition at `-XC4 1 -XCT 4` (per event, 300 evts), against the baseline's own:
```
class                          N   consumed  RPSblock  XCretire   survive
A true, sim has cover TC   676.0      301.7     234.1     120.5      19.7   (baseline 113.9 / 26.3)
B true, sim has NO cover   841.3        4.9       7.2       6.6     822.6   (baseline   5.8 / 823.4)
C no true match             51.8        1.7       5.4       4.4      40.3
```
Class-A survivors 26.3 -> **19.7/evt**; the 4-layer arm retires 6.6 more class A for 0.8
more class B = **8.2:1** selectivity (the 5-layer arm runs at 19.6:1), which is why the
efficiency cost is .00013 and not more.

### WHY THE 19.7 SURVIVE -- enumeration vs head quality
```
bin                                     /evt      %      reading
NO in-window enumerated pair            3.31   16.8%   geometry: outside the BLESSED dR^2
  ... of which no enumerated pair AT ALL 0.02    0.1%   <-- the enumeration hole is CLOSED
best in-window logit < 0                3.33   16.8%   head is CONFIDENT and WRONG
best in-window logit 0-2                3.12   15.8%   head quality
best in-window logit 2-3                3.83   19.4%   head quality
best in-window logit 3-thr(4)           6.16   31.2%   head quality, the NAMED sub-4 band
best in-window logit >= thr             0.00    0.0%   consistency check PASSES (must be 0)
```
**83.2% of the residue is HEAD QUALITY, 16.8% is the maintainer-blessed dR^2 window, and
0.1% is enumeration.** -XC4 1 was the last enumeration fix available: there is no third
one to find. Every remaining duplicate this mechanism could reach needs a BETTER SCORE on
a pair it already enumerates and already has inside the window -- i.e. exactly the
purpose-built seed-vs-chain dedup head the round scoped, and 31% of the target sits in the
logit 3-4 band that head has to separate. Only 0.83/evt of the residue is anchored on a
4-layer chain, so a separate -XC4T threshold buys nothing further.

## M12 -- THE FULL `-XC4 1` FRONTIER (300 evts) AND THE MATCHED-EFFICIENCY READING
```
                      eff      dup     fake      nhB      nhT      nhE      nTC
-XC4 0 (baseline family, fin_ref)
  -XCT 4.5         .81037   .06675   .05539
  -XCT 4.0         .80992   .06230   .05551   9.80254  9.87906  3.55956  618793
  -XCT 3.5         .80904   .05925   .05558
-XC4 1 (this round)
  -XCT 5.00        .81112   .06828   .05533   9.69294  9.78436  3.56474  621384
  -XCT 4.50        .81023   .06131   .05550   9.76067  9.85817  3.57200  618574
  -XCT 4.25        .80997   .05870   .05557   9.78731  9.88596  3.57521  617463
  -XCT 4.00        .80979   .05668   .05562   9.80890  9.90786  3.57795  616531
  -XCT 3.50        .80886   .05351   .05568   9.84353  9.94343  3.58304  615015
LST                .80988   .05179   .04476  10.14804 10.01546  3.56248  608190
```
THE MATCHED-EFFICIENCY STATEMENT (this is the result, not any single row):
```
                                       eff       dup      fake    vs the assembled baseline
assembled baseline  -XCT 4.0        .80992    .06230    .05551
-XC4 1  -XCT 4.25                   .80997    .05870    .05557   eff +.00005  dup -.00360
-XC4 1  -XCT 4.00                   .80979    .05668    .05562   eff -.00013  dup -.00562
-XC4 1  -XCT 4.50                   .81023    .06131    .05550   eff +.00031  dup -.00099  fake -.00001
```
The -XC4 1 curve dominates the -XC4 0 curve at EVERY efficiency in the measured range;
fake moves by at most .00011 anywhere on it. There is no efficiency price to pay for the
flag itself -- the only question is where synthesis wants to sit on a strictly better
frontier. `-XCT 4.25` is the "no efficiency cost" point (+.00005 eff, -.00360 dup),
`-XCT 4.0` is the "keep the existing constant" point (-.00013 eff, -.00562 dup) and is
what I recommend because it adds ZERO tuned constants: -XCT stays at the value the
assembled baseline already ships.
DISPLACED bands are bit-identical to the baseline at EVERY point on the frontier except
-XCT 3.5 (v510 -.0017, v15 -.0007 = one or two tracks). d15/d510/d1030 never move at all.
TRACK LENGTH rises monotonically as -XCT falls, and at -XCT 4 it is LONGER than the
baseline in all three regions (+0.006 / +0.029 / +0.018).

## M13 -- 977-EVENT CONFIRMATION: THE 300-EVENT RESULT TRANSFERS EXACTLY
```
tag                      eff    vxy01      v15     v510    v1030      d15     d510    d1030      dup     fake      nhB      nhT      nhE      nTC
W_X4 (= FINBASE)     0.80905  0.84144  0.80110  0.72161  0.71474  0.58320  0.24521  0.03151  0.06184  0.05607  9.80064  9.88373  3.55719  2033868
W_XC4 (-XC4 1)       0.80884  0.84122  0.80110  0.72161  0.71474  0.58320  0.24521  0.03151  0.05629  0.05618  9.80687  9.91260  3.57512  2026593
W_GATE (port best)   0.80213  0.83399  0.79737  0.72010  0.71474  0.58320  0.24521  0.03151  0.07692  0.04742  9.48700  9.77738  3.38597  2002505
LST(base)            0.80987  0.84285  0.77719  0.64422  0.62567  0.51042  0.22906  0.05402  0.05138  0.04538 10.14984 10.00937  3.55665  1998494
per region      effB     effT     effE     dupB     dupT     dupE     fakB     fakT     fakE
W_X4         0.92454  0.87981  0.74436  0.04294  0.03375  0.08069  0.06754  0.06932  0.04578
W_XC4        0.92430  0.87943  0.74426  0.04231  0.02856  0.07235  0.06757  0.06948  0.04587
LST(base)    0.92430  0.88004  0.74658  0.00971  0.01308  0.08486  0.04365  0.04542  0.04630
```
977 vs 300 for the same change: dup -.00555 (300 gave -.00562), eff -.00021 (300 gave
-.00013), fake +.00011 (300 gave +.00011). The frozen 300 is faithful to <= .0001 here.
* DISPLACED IS BIT-IDENTICAL on the full sample too: v15 .80110, v510 .72161, v1030 .71474,
  d15 .58320, d510 .24521, d1030 .03151 -- every one equal to the baseline to 5 decimals.
  The +.078 / +.089 displaced lead over LST is completely untouched.
* TRACK LENGTH is LONGER in all three regions (+0.006 / +0.029 / +0.018).
* ENDCAP duplicate rate .07235 vs LST's .08486, TRANSITION .02856 vs the baseline's .03375.
  Barrel efficiency .92430 lands exactly on LST's .92430.
* vs LST on 977: eff -.00103, dup +.00491 (was +.01046 -- 53% of the gap closed), fake
  +.01080 (unchanged; this flag does not touch fake).

## M14 -- 977 CONFIRMATION OF THE PARETO POINT, AND THE FINAL SCOREBOARD
```
tag                        eff    vxy01      v15     v510    v1030      d15     d510    d1030      dup     fake      nhB      nhT      nhE      nTC
W_X4  = FINBASE        0.80905  0.84144  0.80110  0.72161  0.71474  0.58320  0.24521  0.03151  0.06184  0.05607  9.80064  9.88373  3.55719  2033868
W_X45   -XCT 4.5       0.80964  0.84204  0.80197  0.72161  0.71474  0.58320  0.24521  0.03151  0.06636  0.05595  9.75198  9.83542  3.55231  2040256
W_XC4_T45 -XC4 1 -XCT 4.5  0.80947  0.84185  0.80197  0.72161  0.71474  0.58320  0.24521  0.03151  0.06098  0.05605  9.75745  9.86331  3.56934  2033322
W_XC4     -XC4 1 -XCT 4    0.80884  0.84122  0.80110  0.72161  0.71474  0.58320  0.24521  0.03151  0.05629  0.05618  9.80687  9.91260  3.57512  2026593
W_GATE (port best)     0.80213  0.83399  0.79737  0.72010  0.71474  0.58320  0.24521  0.03151  0.07692  0.04742  9.48700  9.77738  3.38597  2002505
LST(base)              0.80987  0.84285  0.77719  0.64422  0.62567  0.51042  0.22906  0.05402  0.05138  0.04538 10.14984 10.00937  3.55665  1998494
per region      effB     effT     effE     dupB     dupT     dupE     fakB     fakT     fakE
W_X4         0.92454  0.87981  0.74436  0.04294  0.03375  0.08069  0.06754  0.06932  0.04578
W_XC4_T45    0.92518  0.88050  0.74450  0.04961  0.03651  0.07466  0.06726  0.06920  0.04586
W_XC4        0.92430  0.87943  0.74426  0.04231  0.02856  0.07235  0.06757  0.06948  0.04587
LST(base)    0.92430  0.88004  0.74658  0.00971  0.01308  0.08486  0.04365  0.04542  0.04630
```
BOTH readings confirm on the full sample.
 * MATCHED -XCT (4.5 vs 4.5, 977): dup .06636 -> .06098 = **-.00538** for -.00017 of
   efficiency. Same shape as the 300's -.00544.
 * vs THE ASSEMBLED BASELINE (977): `-XC4 1 -XCT 4.5` is eff **+.00042**, dup **-.00086**,
   fake **-.00002** -- a strict improvement on all three; `-XC4 1 -XCT 4` is eff -.00021,
   dup **-.00555**, fake +.00011.
 * DISPLACED bit-identical to the baseline at both points on 977 (v15 .80110/.80197,
   v510 .72161, v1030 .71474, d15 .58320, d510 .24521, d1030 .03151).
 * ENDCAP duplicate rate .07235 / .07466 vs LST's .08486 and the baseline's .08069.

## RECOMMENDATION
Add ONE boolean, `-XC4 1`, to the assembled baseline. Zero new tuned constants (it follows
-XCT). -XCT itself stays where synthesis wants it on a frontier that is now strictly better
everywhere; my recommendation is to leave it at the shipped 4 and bank the -.0055 duplicate
rate. `bash fin_ref/fin_run.sh <TAG> -XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2 -XC4 1`
(runner a15_ref/a15_run.sh, binary protoA15/bin/chainproto).
ALL RUNS COMPLETE, no background processes left.
