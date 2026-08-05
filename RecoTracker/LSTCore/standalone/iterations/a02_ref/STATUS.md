# A02 -- PURPOSE-BUILT SEED-vs-CHAIN DEDUP HEAD -- STATUS

Agent role: A02-dedup-head, one of fifteen explorers on the assembled baseline.
Workspace code: `standalone/protoA02` (copy of protoFIN). Artifacts: `standalone/a02_ref/`.
Runner: `a02_ref/a02_run.sh <TAG> [overrides]` (frozen prefix inside, BIN=protoA02).

ASSEMBLED BASELINE (the start point, must reproduce):
    -XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2
    eff .80992 / dup .06230 / fake .05551 / nhitOT 9.80/9.88/3.56 / nTC 618793

## THE ANGLE
The `-XCT` frontier collapses below logit ~4 (selectivity 19.6:1 at the baseline,
2.8:1 going to -XCT 3.5). That is a HEAD-QUALITY limit: the attach head ranks
"is this (seed, chain) pair geometrically compatible", not "is this seed's track
already delivered". Train a head for the second question and use it as the -XCT
criterion in the bare-chain arm ONLY (delivery head untouched).

Label from harness truth, exactly the -XCD 2 partition:
  class A = seed sim-matched AND its sim already has a covering TC  -> SAFE to retire
  class B = seed sim-matched AND its sim has NO covering TC         -> retiring costs eff
  class C = no sim match                                            -> junk

## M0 -- SETUP (DONE)
protoA02 = byte copy of protoFIN; binary md5 519b0abc34a28cd1e803d6b9407ef224 before
any edit. GATE0 launched with the UNMODIFIED binary through a02_run.sh to prove the
runner reproduces FINBASE.

## M1 -- SOURCE EDITS (three, all flag-gated, defaults inert)
  1. `-XCP <path>`  dedup-head training pair dump. Enumerates every
     (bare-chain TC, post-deletion bare seed) pair inside the SAME blessed
     dR^2 < 0.02 window the ported arm uses, with no logit prefilter and no
     threshold, writes features + truth class. Writes nothing else.
  2. `-XCH <0|1>`   bare-chain dedup criterion source: 0 (default) the attach logit
     (the original code path, untouched), 1 the trained dedup head. At -XCH 0 the
     original block runs verbatim.
  3. `DedupInference.{h,cc}` + generated `dedup_mlp_weights.h`, the established
     sentinel/`__has_include` pattern. Sentinel returns -1e9 (never retires).

Binary after the edits: protoA02/bin/chainproto md5 98a8751d18aae1ea669160840809f972
(was 519b0abc34a28cd1e803d6b9407ef224 = protoFIN's, before any edit).

## M2 -- THE HEADROOM, MEASURED (5-event smoke, -XCP)
The arm's candidate pool per event (post-deletion universe, not consumed, not
-RPS-blocked, not retired by the pixel arms), seed-level:
    A = 118.4   B = 60.0   C = 4.8    seeds/evt
and what the CURRENT criterion (attach logit >= -XCT) takes out of it:
    XCT 5.0 : A= 91.4 B=2.4   XCT 4.5: A= 98.6 B=2.8   XCT 4.0: A=103.6 B=4.0
    XCT 3.5 : A=106.6 B=6.0   XCT 3.0: A=109.6 B=7.6   XCT 2.0: A=112.8 B=11.4
So (a) the chain arm, not the pixel arms, does nearly all the class-A retirement in this
universe, and (b) 14.8 class-A seeds/evt sit BELOW the deployed threshold, mixed in with
56 class-B seeds. That 14.8 is the entire prize, and the baseline's own -XCT bracket
prices it: d(dup)/dA = -0.00088 and d(eff)/dB = -0.00055 per seed/evt. Retiring all 14.8
for free would be dup .0490 at unchanged efficiency -- BELOW LST's .05179. Realistically
a fraction of it.

## M3 -- OFFLINE PRICING (a02_ref/predict_frontier.py)
Because both slopes are calibrated by the baseline's own bracket, a candidate operating
point can be priced from the dump alone and only two or three points need a harness run.
Every predicted number is labelled as such; the scoreboard rows are measured.

## M4 -- A TRAP THAT COST A RESTART (recorded so nobody repeats it)
`rebase_ref/carve300.py` says it plainly: the frozen 300 ARE entries 0..299 of the
977-event ntuple. A dump run over the 977 therefore produces NOTHING BUT TEST-SET events
for its first 300 entries. Fixed with a fourth flag, `-XCF <n>` = first LST entry of the
hybrid loop (default 0, so every existing command line is bit-exact), which also lets the
dump run as four disjoint chunks (entries 300/470/640/810, 170 each). Events are
processed independently, so a chunked dump and a single dump are the same data.

Final binary: protoA02/bin/chainproto md5 37266f5bd73a290bea4e7c3b3f8b5db9.

## M5 -- THE HEAD SEPARATES WHERE THE ATTACH LOGIT CANNOT (validation, held-out events)
Pilot on 138 train / 24 val events (entries 300+ of the 977; the frozen 300 never
touched). Seed-level max-pooled, per event:

```
criterion       operating points (A retired / B retired per event)
attach logit    5.0: 95.25/3.67   4.5: 103.50/4.38  4.0: 109.67/5.42  3.5: 113.42/6.33
                3.0: 115.79/8.00  2.0: 119.54/12.00 1.0: 122.29/18.79
dedup head      2.41: 103.00/3.33 1.71: 111.42/4.54 0.78: 118.12/7.00 -0.29: 122.58/11.79
pool ceiling    A=127.33  B=64.92  C=9.54
```
The head DOMINATES the attach logit at every point of the curve, and at the deployed
class-B budget (5.42/evt on these events) it retires 115.38 class A against 109.67 --
+5.7 duplicate seeds per event for exactly zero efficiency cost. At the baseline's own
operating point it is better on BOTH axes (111.42 A at 4.54 B vs 109.67 A at 5.42 B).

Priced with the calibrated slopes that is dup .06230 -> ~.0573 at unchanged efficiency,
i.e. roughly half the remaining duplicate gap to LST closed by one substituted criterion.
PREDICTION -- the measured rows are below.

## M6 -- THE HEAD, ON THE FROZEN 300 (TEST ONLY, NEVER TRAINED OR SELECTED ON)
Model: 22 -> 32 -> 32 -> 1, BCE, trained on 138 events / validated on 24 (entries 300+ of
the 977), model selection on the OPERATING POINT (class-A retired at the deployed class-B
budget), not on AUC. Export parity vs torch: max |dlogit| = 3.8e-6 over 20000 pairs.

FIRST, THE PRICING MODEL IS VALIDATED. Re-pricing the ATTACH LOGIT's own bracket on the
test dump reproduces the measured scoreboard:
```
             predicted            measured (fin_ref)
-XCT 4.5     eff .81065 dup .06694    eff .81037 dup .06675
-XCT 4.0     eff .80992 dup .06230    eff .80992 dup .06230   (the reference, by construction)
-XCT 3.5     eff .80926 dup .05918    eff .80904 dup .05925
```
so predictions below are good to about +-.0003 in efficiency and +-.0001 in duplicate rate.

THE FRONTIER, 103 held-out frozen-300 events, seeds/evt (pool: A=126.10 B=64.72 C=8.57):
```
criterion / thr      A/evt   B/evt    sel    predEff   predDup
attach  -XCT 4.5    103.07    3.79   27.2    .81065    .06694
attach  -XCT 4.0    108.35    5.11   21.2    .80992    .06230   <- THE BASELINE
attach  -XCT 3.5    111.89    6.31   17.7    .80926    .05918
attach  -XCT 3.0    114.65    7.92   14.5    .80838    .05675
head    -XCT 1.6    110.75    4.16   26.7    .81041    .06013
head    -XCT 1.2    113.64    4.98   22.8    .80995    .05758
head    -XCT 0.45   118.50    7.54   15.7    .80854    .05331
head    -XCT 0.0    120.18    9.54   12.6    .80744    .05183
```
The head DOMINATES the attach logit at every matched class-B cost: at B ~ 5.0 it retires
113.6 class-A seeds against 108.4, at B ~ 6.1 it retires 116.2 against 111.9 (B=6.31),
at B ~ 7.5 it retires 118.5 against 114.7 (B=7.92). The selectivity collapse the port
round named IS a head-quality limit, and a head trained on the dedup question rather than
the attach question does not collapse in the same place.

## M7 -- HARNESS CONFIRMATION BATCH (300 evts, launched)
GATE2  -XCH 0                 no-op gate on the FINAL binary (head compiled in)
H16    -XCH 1 -XCT 1.6        better than the baseline on BOTH axes
H12    -XCH 1 -XCT 1.2        iso-efficiency, maximum duplicate gain
H045   -XCH 1 -XCT 0.45       duplicate-leaning
H00    -XCH 1 -XCT 0.0        the point predicted to sit at LST's own duplicate rate

## M8 -- WHAT THE HEAD IS ACTUALLY USING, AND THE TRAINING-FREE CONTROLS
Permutation importance at the deployed operating point (drop in class-A retired per event
at fixed class-B cost), frozen-300 test dump:
```
  +66.1  seedBestChainLogit     +2.2  attachLogit          +1.2  famSize
   +6.0  chainScore             +2.0  seedBestT3Logit      +1.1  absTcEta
   +5.5  log10SeedPt            +1.7  seedPtErrRel         +1.0  chainNNodes
   +2.8  seedScore              +1.6  chainNLayers         <=0.6  the remaining 8
   +2.2  seedEtaErr             +1.4  famRpsBlocked
   +2.2  absSeedEta
```
The single most load-bearing input is `plsBestChainLogit` -- "the best attach compatibility
this seed reached against ANY chain" -- which is exactly the `-XCG 1` quantity the port
round measured and rejected as "looser". THAT DOES NOT MEAN -XCG 1 WOULD DO THIS. The
training-free controls, on the same test events, at matched class-B cost:
```
criterion                              A/evt at B<=3.79   5.11    6.31    7.92
attach logit (-XCG 0, THE BASELINE)          103.13     108.14  111.56  114.42
plsBestChainLogit alone (= -XCG 1)           101.33     107.52  110.83  113.98
max(attach, bestChain)                       101.33     107.52  110.83  113.98
attach + bestChain                           103.18     108.04  111.48  114.19
0.5*attach + bestChain                       102.97     107.96  111.51  114.77
THE TRAINED HEAD                                 --     113.69      --      --
```
Every hand-built combination lands on top of the baseline; the head is +5.6 above all of
them at the same efficiency cost. The win is the JOINT use of the two logits with seed
quality and chain quality, which is what training buys and what a threshold on either
logit alone cannot express.

## M9 -- HEAD PATH SANITY (5 events, tag SANE, -XCD 2 truth partition)
Same 5 events, same everything, only the criterion swapped:
```
                        XCretire A   XCretire B   XCretire C   class-A survivors
-XCH 0 -XCT 4 (base)         108.4          5.0          2.8               23.0
-XCH 1 -XCT 1.2 (head)       112.6          4.6          4.8               18.8
```
+4.2 class A retired AND -0.4 class B, i.e. strictly better on both axes, exactly as the
offline frontier predicted. `head available=1` confirmed in the report line.

## M10 -- WHERE THE GAIN LANDS (frozen-300 test dump, 125 events, seeds/evt)
```
region                 poolA  poolB | attach@4  A     B | head@1.2  A     B |    dA     dB
barrel |eta|<1.1       75.52  25.03 |          66.90  2.71 |         70.82  2.44 | +3.93  -0.27
transition 1.1-1.7     31.66  13.09 |          27.46  1.23 |         28.98  1.42 | +1.51  +0.19
endcap |eta|>=1.7      18.63  26.11 |          13.73  1.11 |         13.70  1.19 | -0.03  +0.08
```
The whole gain is BARREL and TRANSITION, and the endcap is untouched. That is exactly
where the assembled baseline's residual duplicate and fake gaps against LST live (endcap
dup and fake are already better than LST). The head is not trading one region for another.

## M11 -- AND WHY (seed pT dependence, same test dump, seeds/evt)
```
band        poolA |  attach@4 A     B |  head@1.2 A     B |    dA     dB
pt 0.8-1.0  49.54 |        43.66  1.50 |       45.16  1.57 | +1.50  +0.07
pt 1-2      51.89 |        45.54  2.57 |       45.75  2.32 | +0.22  -0.25
pt 2-5      15.80 |        14.21  0.87 |       14.39  0.89 | +0.18  +0.02
pt 5-20      7.04 |         4.29  0.06 |        6.72  0.18 | +2.43  +0.12
pt >20       1.13 |         0.02  0.00 |        1.11  0.05 | +1.09  +0.05
```
The single biggest deficit of the attach logit as a dedup criterion is HIGH pT: it retires
4.3 of the 7.0 available class-A seeds at pt 5-20 and 0.02 of 1.13 above 20 GeV, while the
head retires 6.7 and 1.11. That is a mechanism, not a fitted quirk: a high-pt seed is
nearly straight, so the curvature-difference features the ATTACH head leans on carry
almost no information there, and it scores those pairs low whether or not they are the
same track. Asking "is this already delivered" instead of "is this pair compatible" is
precisely what recovers them. The rest of the gain is at the pt threshold (0.8-1.0), where
seed resolution is worst.

## M12 -- SIMPLICITY LADDER (is the MLP earning its keep?)
Class-A seeds retired per event at the baseline's class-B budget (5.11/evt), frozen-300
test dump, every model trained on the SAME 238 training events:
```
attach logit alone (the baseline)                       108.14
logistic on (attachLogit, bestChainLogit)               107.80
logistic on (attach, bestChain, log10 pt)               109.73
logistic on (attach, bestChain, pt, chainScore, |eta|)  109.39
logistic on all 22 inputs                               111.68
the deployed 22 -> 32 -> 32 -> 1 MLP                    113.69
```
Both dimensions of the model earn their place: the full input set is worth +1.9 over a
three-input linear model, and the nonlinearity is worth another +2.0 over a linear model
on the same 22 inputs. There is no two-feature hand rule hiding in here.

## M13 -- THE MEASURED SCOREBOARD (300 evts). NO-OP GATE PASSED; THE HEAD DOES NOT WIN.
GATE2 (-XCH 0 on the FINAL header-compiled binary) reproduces FINBASE on EVERY metric and
on nTC exactly (618793 = 618793). The substitution is clean and the no-op is proven.

```
tag     -XCT      eff     dup     fake      nhB      nhT      nhE      nTC   chainKill/evt
GATE2  (base) .80992  .06230  .05551  9.80254  9.87906  3.55956  618793      359.4
H16      1.6  .80758  .05969  .05371  9.83933  9.92316  3.56639  617292      403.7
H12      1.2  .80639  .05685  .05371  9.87368  9.95197  3.57056  615951      429.3
H045     0.45 .80467  .05281  .05361  9.92473  9.99464  3.57913  613777      472.5
H00      0.0  .80312  .05107  .05356  9.95059 10.02045  3.58400  612569      497.9
LST           .80988  .05179  .04476 10.14804 10.01546  3.56248  608190
```
DISPLACED IS UNTOUCHED: v1030 .74139, d15 .61650, d510 .20161, d1030 .03073 are IDENTICAL
in all five runs; v510 moves .73356 -> .73187 (one track) and stays far above LST's .65430.

THE DUPLICATE PREDICTION WAS RIGHT AND THE EFFICIENCY PREDICTION WAS WRONG.
```
tag    predicted eff / dup      measured eff / dup       eff error
H16       .81041 / .06013         .80758 / .05969        -.0028
H12       .80995 / .05758         .80639 / .05685        -.0036
H045      .80854 / .05331         .80467 / .05281        -.0039
H00       .80744 / .05183         .80312 / .05107        -.0043
```
Duplicate rate lands within .0008 every time. Efficiency is 3-4x more expensive than the
class-B count says it should be -- AND THE HEAD RETIRES FEWER CLASS-B SEEDS THAN THE
BASELINE DOES at H12 (4.98 vs 5.11 per event on the same events).

THE HONEST COMPARISON, at matched efficiency, against the baseline's OWN -XCT frontier:
```
                       eff      dup     fake
baseline -XCT 3.0   .80776   .05689   .05562
head     -XCT 1.6   .80758   .05969   .05371
```
+.0028 duplicate for -.0019 fake. Duplicate rate outranks fake rate, so THE BASELINE WINS
and the head, deployed this way, is a REGRESSION on the primary axes. Reported as such.

WHAT THE HEAD DID BUY, and it is the one thing -XCT could not: the FAKE RATE. -XCT moves
fake by essentially nothing (.05551 -> .05562 across the whole baseline bracket); the head
moves it -.0018 and then saturates (.05371 / .05371 / .05361 / .05356 across four
thresholds). The exchange rate, 1.4 duplicate per 1.0 fake, is slightly BETTER than -AT3's
1.7 (from the Baseline agent's -AT3 7 -XCT 4.75 point), and it does not require touching
the delivery margin. The mechanism is class-C (junk) seeds: the head retires ~2 more per
event than the attach logit does, and each one is a fake type-8 row.

## M14 -- WHY (the finding that matters most to the campaign)
The class A / class B truth partition IS NOT A FAITHFUL PROXY FOR (duplicate, efficiency).
The head was trained to maximise class-A retirement at fixed class-B retirement and it DID
(+5.6 A/evt at the same B on held-out frozen-300 events, and it dominates the attach logit
at every point of that curve). The scoreboard says the opposite. The two statements are
only compatible if the class-B seeds are heterogeneous -- and they are:
  * "-XCD 2 covered" means the seed's sim appears in the sim set of some delivered chain
    TC or pT3-class row. That is a LOOSER condition than the harness's own >75% hit match
    on the assembled track candidate. A seed can be class A and still be the only row that
    actually MATCHES its sim, so retiring it costs efficiency that the partition scores as
    free;
  * consistently, the baseline's own efficiency cost per class-B seed is not constant
    either: -.00037, -.00073, -.00100 per seed as -XCT walks 4.5 -> 4.0 -> 3.5 -> 3.0.
The fix is a sharper label -- "does retiring this seed cost an IN-CUT harness match",
which needs the sim's in-cut flag and the covering TC's match fraction in the dump, not
the T3-sim-set intersection. That is one flag and one re-dump away and is the single
highest-value follow-up from this round. Diagnostic runs H12D and BX3D (the head and the
matched-efficiency baseline point, both with -XCD 2) were launched to confirm the
mechanism in situ.

## M15 -- NO-OP PROOF, EXACT
`rebase_ref/cmp_branches.py a02_ref/r_GATE2.root fin_ref/r_FINBASE_ND.root`
-> 33 branches IDENTICAL, 0 DIFFER, 0 MISSING, 300/300 entries. The final binary
(md5 9a1df11803aa595e20396d4ce36a5b2c, dedup head compiled in) at `-XCH 0` is BIT-IDENTICAL
to the Baseline agent's canonical assembled-baseline artifact. All four new flags
(-XCH, -XCP, -XCF, plus DedupInference) are inert at their defaults.

## M16 -- PER REGION: THE HEAD AIMS AT THE RIGHT PLACE AND OVERSHOOTS
```
tag       effB     effT     effE     dupB     dupT     dupE     fakB     fakT     fakE
GATE2  0.92660  0.88213  0.74496  0.04344  0.03431  0.08106  0.06680  0.06903  0.04525
H16    0.92284  0.87831  0.74441  0.03337  0.02985  0.08307  0.06701  0.06702  0.04243
H12    0.92147  0.87576  0.74385  0.02844  0.02625  0.08157  0.06720  0.06706  0.04232
H045   0.91886  0.87322  0.74319  0.02185  0.02180  0.07898  0.06748  0.06710  0.04201
H00    0.91658  0.87195  0.74208  0.01927  0.01927  0.07791  0.06761  0.06717  0.04184
LST    0.92557  0.88187  0.74596  0.00989  0.01264  0.08556  0.04249  0.04454  0.04603
```
Barrel and transition duplicate rate -- the ONLY places the assembled baseline is behind
LST on duplicates -- fall by more than half (dupB .0434 -> .0193, dupT .0343 -> .0193,
against LST's .0099 / .0126), and the endcap is left alone (as designed, and as the
offline per-region table predicted). The fake gain is transition + endcap
(fakT .0690 -> .0670, fakE .0453 -> .0418, both now BELOW LST). The efficiency it spends
is also barrel + transition, which is why the trade nets out negative.

## M17 -- THE PROXY FAILURE, NAILED (frozen-300 test dump, 125 events, seeds/evt)
At the MATCHED-EFFICIENCY operating point (head -XCT 1.6 vs the assembled baseline):
```
                     class-B retired    class-A retired    measured eff
attach logit @ 4.0        5.06              108.09            .80992
dedup head   @ 1.6        4.12              110.14            .80758
```
THE HEAD RETIRES 0.94 FEWER CLASS-B SEEDS PER EVENT AND LOSES .0023 OF EFFICIENCY. There
is no way to reconcile that with "class B is the efficiency cost". The partition is simply
not the quantity the scoreboard measures.

The composition says where the mislabelling lives:
```
class-B seeds retired only by the HEAD    n=100  median pt 1.37  36% above 2 GeV
class-B seeds retired only by the ATTACH  n=217  median pt 1.28  15% above 2 GeV
class-A seeds retired only by the HEAD    n=871  median pt 4.02  54% above 2 GeV
class-A seeds retired only by the ATTACH  n=615  median pt 1.16  11% above 2 GeV
```
The head's whole gain is HIGH-pT seeds labelled class A. "Class A" only means the seed's
sim appears in the sim set of some delivered TC; it does NOT mean that TC achieves the
harness's >75% hit match. At high pt our chain TCs are exactly where that is least
guaranteed, so a large share of the head's "free" retirements are in fact the only row
that was matching the track. The head did what it was asked; the label was wrong.

See the running log below for measured results.

## M18 -- THE SHARP LABEL (the M14/M17 follow-up, in progress)
New flag `-XCL <0|1>` (default 0, DUMP-ONLY, reads nothing, decides nothing). With
`-XCP`, it appends to every dumped pair the exact counterfactual for the seed's bare row
in the HARNESS's own definitions (OutputWriter steps 3/4), computed with the ported
production matcher on the delivered TCs' own hit lists:
```
yFree nSoleCut nSoleAcc nAccSims nFullSims isDup isFake nPartner
```
  isDup     some sim this row matches (FULL sim space, >75%) has another matching row
            -> retiring removes one duplicate row
  isFake    the row matches no sim at all -> retiring removes one fake row
  nSoleCut  ACCEPTED, in-denominator sims (pt>0.9, |eta|<4.5, vperp<2.5, |vz|<30) whose
            ONLY >75% match is this row -> retiring costs exactly this much efficiency
  nPartner  matched sims left with exactly ONE other row -> that row stops being a
            duplicate too (the second-order duplicate gain)
Cover maps are built from (a) every delivered chain/attach TC, (b) every surviving
carried pT5/pT3 row, (c) every bare-seed row the arm may deliver, all taken from the
state BEFORE the bare-chain arm runs -- so the label does not depend on -XCT.

FIRST MEASUREMENT (5 events, 1287 candidate seeds = 257/evt):
```
                          seeds   share
retiring removes a DUP      775   60.2%
retiring removes a FAKE      62    4.8%
neither, and sole match of
  an ACCEPTED in-cut sim     13    1.0%   <- THE ENTIRE EFFICIENCY COST
  a PILEUP sim only         437   34.0%   <- neutral rows: no dup gain, no eff cost
old -XCD 2 class B          441   34.3%   <- what the M5-M13 head was trained to protect
```
THE OLD LABEL OVERSTATED THE EFFICIENCY COST BY 34x AND MISSED THE REAL PENALTY
ENTIRELY. Class B is 441 seeds; only 13 of them cost efficiency. The other 428 are
sole matches of PILEUP sims: retiring them costs nothing but also GAINS nothing, and
because nTC is the denominator of both rate metrics, retiring them RAISES dup and fake
rates. That is the mechanism behind M13's "the head retires fewer class B and loses
more efficiency": class B was neither the cost nor the objective.

## M18b -- THE PRICING MODEL BECOMES EXACT (a02_ref/price18.py)
With the sharp columns, a criterion's whole scoreboard follows from the dump by simple
row arithmetic, because every harness quantity is per row:
    nTC -= 1 | nFake -= isFake | nDup -= isDup (+ partner) | nMatched -= nSoleCut
No calibrated slopes, no regression. First check on 5 events, `--partner 0`:
    -XCT 4.5  PRED dup .06652   measured .06675
    -XCT 4.0  PRED dup .06230   measured .06230   (the reference, by construction)
    -XCT 3.5  PRED dup .06034   measured .05925
    -XCT 3.0  PRED dup .05911   measured .05689
Direction and scale right; the low end drifts because the second-order term (a retired
duplicate un-duplicates its partner) matters more the more you retire. Calibrated
properly on the full frozen-300 dump.

THE ORACLE (5 events, PRED): retire every row that is (isDup or isFake) and costs no
in-cut match -- 167.4 seeds/evt against the deployed criterion's 140.6 --
    eff .81088   dup .03974   fake .05064
i.e. ABOVE the baseline's efficiency, and duplicate rate well BELOW LST's .05179. The
headroom the round was looking for is real; the question is how much of it a head can
reach from features that carry no truth.

## M18c -- IN-SITU CONFIRMATION OF M14/M17 (H12D vs BX3D, -XCD 2, 300 evts)
The two diagnostic runs launched at the end of the previous session finished:
```
                                XCretire A   XCretire B   XCretire C
head  -XCT 1.2  (H12D)              119.4          6.0          8.0    measured eff .80639
attach -XCT 3.0 (BX3D)              119.9          8.4          4.5    measured eff .80776
```
The head retires FEWER class A *and* FEWER class B than the attach criterion it is
compared against, and still loses .0014 of efficiency. The old partition cannot even
order the two criteria correctly. (It does explain the fake gain: class C retirement
8.0 vs 4.5.) This is exactly what M18's sharp label was built to replace.

## M18d -- THE SHARP-LABEL HEAD, TRAINING RECIPE
`protoA02/train_dedup18.py`. Same 22 inputs and same 22->32->32->1 architecture as the
M6 head (so the deployed C++ path, the export and the parity check are unchanged); only
the LABEL and the MODEL SELECTION move:
  label       y = 1 iff (isDup or isFake) and nSoleCut == 0
  weighting   the 2% of rows that DO cost efficiency carry --cost-weight (swept)
  selection   PREDICTED duplicate RATE at MATCHED predicted efficiency, from the same
              row arithmetic as price18.py -- never AUC, never class counts
Sweep on the partial dumps (262 events, 42 val), val PRED dup at matched efficiency
(baseline .06230): cost-weight 5 -> .05998, 20 -> .05853, 50 -> .05751, 100 -> .05788,
200 -> .05801; hidden 48 at cw 50 -> .05732. Chose cw 50, hidden 32 (the extra width is
inside the noise of a 42-event val set and costs simplicity).

## M18e -- WHAT THE HEADROOM ACTUALLY IS (partial dump, 64 events, illustrative)
Composition of the arm's candidate pool and of what each criterion takes out of it,
seeds per event:
```
                        retired    dup rows   fake rows   pileup-sole   eff cost
POOL (everything)         197.2       124.0        8.47         62.4       2.38
attach logit >= 4.0       113.5       106.4        0.95          6.0       0.25
dedup head (sharp) -1.0   140.8       119.5        6.00         14.7       0.52
ORACLE                    132.5       124.0        8.47          0.0       0.00
```
READINGS
* THE DEPLOYED CRITERION IS ALREADY 94% PRECISE ON DUPLICATES (106.4 of 113.5) and
  spends only 0.25 seeds/evt of real efficiency. The port round's "selectivity collapse"
  was measured in the WRONG currency; in the harness's currency the attach logit is a
  good duplicate criterion and a BAD fake criterion.
* THE HEADROOM IS 17.6 duplicate rows + 7.5 FAKE rows per event. The attach logit
  retires 0.95 of the 8.47 fake rows because a fake seed matches nothing, so it is not
  "compatible" with any chain and the ATTACH question scores it low. Asking the DEDUP
  question instead recovers them: that is the mechanism behind M13's one real gain
  (fake -.0018 and then saturating), now explained and no longer accidental.
* THE THIRD POPULATION, 62.4 seeds/evt that are the sole match of a PILEUP sim, is the
  trap. Retiring one costs no efficiency and gains nothing, and it shrinks nTC, the
  denominator of both rate metrics. The attach logit takes 6.0/evt of them; the head at
  a comparable point takes 14.7. Any criterion pushed hard enough drowns in these.

## M18f -- RUN INVENTORY FOR THIS PHASE (so a resume knows what exists)
Binary with the sharp label: protoA02/bin/chainproto md5 23e987d5574bc10d3fb7b64a96c243fd
(the M13 deployed binary was 9a1df11803aa595e20396d4ce36a5b2c; the only source change is
-XCL plus its dump block, all inert at the default).
  a02_ref/pairs_L0.txt   frozen 300, TEST ONLY  (-XCF 0   -n 300)
  a02_ref/pairs_L1..L4   977 entries 300/470/640/810, 170/170/170/167 -- TRAINING
  a02_ref/r_GATE3.*      no-op gate of the M18 binary at -XCH 0 (must equal FINBASE)
  a02_ref/price18.py     exact row-arithmetic pricing + calibration against fin_ref
  a02_ref/m18_regions.py per-region / per-pT decomposition in the sharp currency
  protoA02/train_dedup18.py, protoA02/score_dedup18.py

## M18g -- THE PRICING MODEL IS EXACT (frozen-300 test dump, 59318 seeds, 300 events)
Calibrating the one free coefficient (the second-order partner credit) against the four
-XCT points fin_ref measured on these same events:
```
partner    rms dup    rms eff   rms fake   max dev dup
0.00       0.00196    0.00076    0.00001       0.00282
0.50       0.00094    0.00076    0.00001       0.00141
1.00       0.00010    0.00076    0.00001       0.00019   <- full credit is correct
```
At partner 1.0 the residuals are
  -XCT 4.50 eff -.00029 dup +.00019 | 4.00 (reference) | 3.50 eff +.00056 dup -.00007
  -XCT 3.00 eff +.00138 dup -.00001
DUPLICATE AND FAKE RATE ARE PREDICTED TO 2e-4 AND 1e-5 FROM THE DUMP ALONE. Efficiency
is predicted to about 1e-3, over-predicted at aggressive retirement (a retired row's
sim can be picked up by a row that the same sweep also retires). Physically: retiring a
duplicate row also un-duplicates its partner, so each retirement is worth TWO duplicate
rows, not one -- which is why the -XCT knob buys duplicates so cheaply.

## M18h -- THE SHARP-LABEL HEAD, PREDICTED FRONTIER ON THE TEST SET (never trained on)
```
criterion        PREDeff  PREDdup PREDfake     ret/ev
attach  -XCT 4.5 0.81040  0.06574  0.05538      98.6      (measured .81037/.06675/.05539)
attach  -XCT 4.0 0.80992  0.06230  0.05551     114.0      THE ASSEMBLED BASELINE
attach  -XCT 3.0 0.80914  0.05688  0.05562     122.8      (measured .80776/.05689/.05562)
head    +0.5     0.81029  0.06073  0.05399     118.4
head     0.0     0.81005  0.05707  0.05397     124.2      <- matched efficiency
head    -0.5     0.80966  0.05411  0.05396     129.6
head    -1.0     0.80915  0.05175  0.05393     135.4      <- LST's own duplicate rate
head    -1.5     0.80837  0.04989  0.05389     141.9
ORACLE           0.81104  0.04553  0.05239     133.7
```
At MATCHED efficiency the head is predicted better than the assembled baseline on BOTH
remaining axes (dup -.0052, fake -.0015) and better than the attach criterion's own
frontier at every point of it. Harness runs GATE4 / T05 / T00 / TM05 / TM10 launched.

## M18i -- WHERE THE GAIN COMES FROM (test dump, head thr 0.0 vs attach 4.0, seeds/evt)
```
                  pool | attach ret  dup  fake  neutral  cost | head ret  dup  fake  neu  cost
TOTAL            197.7 |      114.0 107.5  0.87     5.4  0.23 |    124.2 113.3 4.59  6.1  0.21
barrel <1.1      102.2 |       70.0  66.7                0.13 |     73.6  70.4            0.12
transition        45.6 |       28.6  27.0                0.05 |     30.5  27.9            0.05
endcap >=1.7      49.9 |       15.4  13.9                0.05 |     20.0  15.0            0.04
pt 0.8-1          85.8 |       46.8  44.6                0.02 |     52.0  47.9            0.02
pt 5-20            8.4 |        4.2   4.1                0.03 |      5.8   5.3            0.04
pt >20             1.4 |        0.1   0.1                0.00 |      0.7   0.5            0.01
```
+5.8 duplicate rows AND +3.7 fake rows per event, at a LOWER efficiency cost (0.21 vs
0.23). The fake half is the biggest single difference and it is structural: a fake seed
matches no sim, so it is compatible with no chain, so the ATTACH question scores it low.
The duplicate half is spread over all three regions and concentrated at the pT threshold
and above 5 GeV -- the two places seed resolution and curvature information are worst.

## M18j -- NO-OP GATE PASSED ON THE M18 BINARY
`cmp_branches.py a02_ref/r_GATE3.root fin_ref/r_FINBASE_ND.root`
-> 33 branches IDENTICAL, 0 DIFFER, 0 MISSING, 300/300 entries. The sharp-label binary
at `-XCH 0` is BIT-IDENTICAL to the Baseline agent's canonical assembled-baseline
artifact, so -XCL, -XCP and -XCF are all inert at their defaults. GATE4 repeats the gate
on the FINAL binary (new weights header compiled in).

## M19 -- MEASURED. THE SHARP-LABEL HEAD WINS ON ALL THREE AXES.
Harness, frozen 300 events, assembled-baseline line with the ONE substitution -XCH 1:
```
tag     -XCT      eff      dup     fake      nhB      nhT      nhE      nTC
GATE4  (-XCH 0) 0.80992  0.06230  0.05551  9.80254  9.87906  3.55956  618793  == FINBASE
T05      0.50   0.81085  0.06212  0.05378  9.80938  9.87612  3.56746  617489
T00      0.00   0.81019  0.05819  0.05377  9.85160  9.91892  3.57362  615752
TM05    -0.50   0.80908  0.05486  0.05376  9.88960  9.95676  3.57994  614114
TM10    -1.00   0.80767  0.05218  0.05373  9.92312  9.99177  3.58712  612387
LST             0.80988  0.05179  0.04476 10.14804 10.01546  3.56248  608190
```
T00 vs the assembled baseline: eff +.00027, dup -.00411, fake -.00174, track length
+0.05/+0.04/+0.014. STRICTLY BETTER ON EVERY AXIS INCLUDING TRACK LENGTH, with ONE knob
moved and no new mechanism. T05 is the efficiency-leaning point (+.00093 eff, and still
-.00018 dup and -.00173 fake). Efficiency at T00 and T05 is ABOVE LST's .80988.

DISPLACED UNTOUCHED: v510 .73356, v1030 .74139, d15 .61650, d510 .20161, d1030 .03073
are IDENTICAL to FINBASE at T05, T00 and TM05 (TM10 moves v510 to .73187, one track).

GATE4 (-XCH 0 on the FINAL binary, new weights header compiled in) is BIT-IDENTICAL to
fin_ref/r_FINBASE_ND.root: 33/33 branches, 300/300 entries.

THE PREDICTION WAS RIGHT THIS TIME, INCLUDING EFFICIENCY:
```
tag     PRED eff/dup/fake            measured                 eff error
T05     .81029 .06073 .05399   .81085 .06212 .05378            -.00056
T00     .81005 .05707 .05397   .81019 .05819 .05377            -.00014
TM05    .80966 .05411 .05396   .80908 .05486 .05376            +.00058
TM10    .80915 .05175 .05393   .80767 .05218 .05373            +.00148
```
Compare M13, where the OLD label mispredicted efficiency by -.0028 to -.0043 with the
same machinery. The label was the whole problem.

## M19b -- THE REFINED FRONTIER (300 evts, one knob, -XCH 1 -XCT <t>)
```
tag     -XCT      eff      dup     fake      nhB      nhT      nhE      nTC
GATE4  (-XCH 0) 0.80992  0.06230  0.05551  9.80254  9.87906  3.55956  618793
T05      0.50   0.81085  0.06212  0.05378  9.80938  9.87612  3.56746  617489
T025     0.25   0.81059  0.06010  0.05376  9.83024  9.89824  3.57066  616590
T00      0.00   0.81019  0.05819  0.05377  9.85160  9.91892  3.57362  615752
TM025   -0.25   0.80953  0.05645  0.05376  9.87188  9.93735  3.57674  614929
TM05    -0.50   0.80908  0.05486  0.05376  9.88960  9.95676  3.57994  614114
TM10    -1.00   0.80767  0.05218  0.05373  9.92312  9.99177  3.58712  612387
LST             0.80988  0.05179  0.04476 10.14804 10.01546  3.56248  608190
```
THE PICK IS -XCT 0.0: the lowest duplicate rate among the points whose efficiency is
at or above LST's .80988. Efficiency .81019, duplicate .05819 (gap to LST .0064, was
.0105), fake .05377 (gap .0090, was .0108), and track length moves TOWARDS LST in all
three regions. Below -XCT 0 the exchange rate stays good (about 7 duplicate per 1
efficiency in units of 1e-4) but efficiency drops under LST, which the finish line
forbids.

vs THE OLD-LABEL HEAD at comparable duplicate rate the new one is worth about +.003 of
efficiency everywhere: old H16 .80758/.05969, new TM025 .80953/.05645; old H045
.80467/.05281, new TM10 .80767/.05218.

## M19c -- WHAT THE SHARP-LABEL HEAD LEANS ON (permutation importance, TEST set)
Rise in PREDICTED duplicate rate at matched efficiency when one input is scrambled
(deployed head .05686 vs the attach criterion .06230, so .00544 = the whole gain lost):
```
 +0.00544 seedEtaErr        +0.00544 seedBestChainLogit  +0.00544 chainScore
 +0.00506 seedScore         +0.00372 isFamBestPt         +0.00264 log10SeedPt
 +0.00154 attachLogit       +0.00151 famLiveCand         +0.00146 absSeedEta
 +0.00139 chainNNodes       +0.00131 nCandTcForSeed      <=0.0013 the remaining 10
```
The three at the top are saturated (scrambling any one of them costs the ENTIRE gain).
Note what changed from the old-label head: there the single dominant input was
`seedBestChainLogit` and everything else was decoration. Here the head leans on SEED
QUALITY (etaErr, pLS score, pt, is-this-the-family's-best-pt) together with CHAIN
QUALITY (chainScore), and the ATTACH LOGIT itself is only the seventh input. That is the
signature of a head answering "is this seed a worse copy of a track already delivered"
rather than "is this pair geometrically compatible".

## M19d -- THE TWO-KNOB FAKE-LEANING ALTERNATIVE (-AT3 7 with the head)
```
tag       flags                       eff      dup     fake      nhE      nTC
T00       -XCT 0.0                 0.81019  0.05819  0.05377  3.57362  615752
A7T00     -AT3 7 -XCT 0.0          0.80935  0.06116  0.04786  3.54313  611003
A7TM05    -AT3 7 -XCT -0.5         0.80820  0.05752  0.04783  3.55029  609224
LST                                0.80988  0.05179  0.04476  3.56248  608190
```
-AT3 7 buys about .0059 of fake for .0008 of efficiency and .003 of duplicate, exactly
as the Baseline agent measured without the head; the head does not change that trade.
It is NOT recommended: it drops efficiency below LST, it costs a second tuned constant,
and it shortens tracks (nhE 3.543 vs 3.574). Recorded so the synthesis has the option if
fake rate is judged more important than the priority order says.

## M20 -- FULL 977-EVENT CONFIRMATION
```
tag                     eff      v510    v1030      d15     d510      dup     fake      nhB      nhT      nhE       nTC
FINBASE (W_X4)      0.80905   0.72161  0.71474  0.58320  0.24521  0.06184  0.05607  9.80064  9.88373  3.55719  2033868
W_T05  (-XCT 0.5)   0.81002   0.72161  0.71474  0.58320  0.24521  0.06165  0.05428  9.80779  9.88272  3.56596  2029398
W_T00  (-XCT 0.0)   0.80934   0.72161  0.71474  0.58320  0.24521  0.05769  0.05428  9.85008  9.92577  3.57181  2023722
LST                 0.80987   0.64422  0.62567  0.51042  0.22906  0.05138  0.04538 10.14984 10.00937  3.55665  1998494
```
W_T00 vs the assembled baseline on the FULL sample: eff +.00029, dup -.00415,
fake -.00179, track length +0.049/+0.042/+0.015. BETTER ON EVERY AXIS, and its
efficiency deficit against LST (-.00053) is SMALLER than the assembled baseline's
(-.00082). W_T05 is above LST efficiency (.81002 vs .80987) but keeps almost all of the
duplicate gap.
DISPLACED IS BIT-FOR-BIT UNCHANGED on the 977: v510 .72161, v1030 .71474, d15 .58320,
d510 .24521, d1030 .03151 in FINBASE, W_T05 and W_T00 alike.
Per region, 977, W_T00: effB .92490 effT .88034 effE .74453 | dupB .03203 dupT .03127
dupE .07976 | fakB .06781 fakT .06710 fakE .04297. (LST .92430/.88004/.74658 |
.00971/.01308/.08486 | .04365/.04542/.04630.) Barrel and transition efficiency ABOVE
LST; endcap duplicate AND endcap fake below LST; every residual gap is barrel+transition.

## THE RECOMMENDATION
    -XC 3 -XCT 0 -T3E 1 -CC 1 -CCN 1 -CCR 2 -XCH 1
i.e. the assembled baseline with the bare-chain dedup criterion switched from the
borrowed attach logit to the purpose-built head, and the ONE tuned constant re-tuned on
the new scale. Still exactly one tuned constant. -XCT 0.5 is the efficiency-leaning
alternative (eff above LST on both samples, most of the duplicate gain given up).

## M20b -- SIMPLICITY LADDER IN THE NEW CURRENCY (PRED dup / fake at matched efficiency,
## frozen-300 TEST set, every model trained on the same data with the same recipe)
```
attach logit -XCT 4 (the baseline)          0.06230  0.05551
logistic on attachLogit alone               0.06186  0.05552
logistic on (attach, bestChain)             0.06230  0.05551
logistic on (attach, bestChain, log10 pt)   0.06078  0.05552
logistic on all 22 inputs                   0.06025  0.05419
MLP 22->32->32->1 (half the training data)  0.05707  0.05404
THE DEPLOYED HEAD (all of it)               0.05686  0.05397
```
No hand rule on one or two logits reaches it: the nonlinearity alone is worth .0032 of
duplicate rate over a linear model on the SAME 22 inputs, and the full input set is
worth .0016 over a three-input linear model. There is no simpler criterion hiding here.

## PROCESS NOTE
The stalled first attempt at this ladder (scratchpad/ladder.py, no output after 70
minutes of CPU) was killed; the table above is the fast rerun on two of the four
training chunks plus the deployed head. All A02 processes are finished.
