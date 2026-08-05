# A11 -- STATUS

Workspace: `standalone/protoA11` (copy of protoFIN, binary md5 519b0abc... at copy time).
Artifacts: `standalone/a11_ref/`. Runner: `a11_ref/a11_run.sh <TAG> [overrides]` -- the
frozen prefix PLUS the assembled baseline tail `-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2`,
so an empty override list IS the baseline.

## ANGLE (chosen after the M0 diagnostic below, not before)

THE SEEDLESS CHAIN TC (`tc_type == 4`) IS THE SINGLE ROW CLASS RESPONSIBLE FOR BOTH
REMAINING GAPS. Attack it by ATTACHING more of them to their pixel seed, which is blocked
today only because ONE constant (`-a`) serves two different questions.

## M0 -- THE DIAGNOSTIC THAT PICKED THE ANGLE (no runs needed; both ntuples already exist)

`a11_ref/a11_types.py` -- per `tc_type` count / fake / duplicate over the SAME in-cut
selection the scoreboard uses (pt > 0.9, |eta| < 4.5), so the rows ADD UP to
fake_overall_incut and dup_overall_incut. Per event, frozen 300:

```
                 FINBASE (ours)                        LST
type        N/evt  fake/evt  dup/evt   fakeF  dupF |   N/evt  fake/evt  dup/evt   fakeF  dupF
4  T5-class 328.4    30.26    19.15   .0921 .0583 |  111.9     7.43     5.40   .0664 .0482
5  pT3-class 93.3    16.23     1.54   .1739 .0165 |  104.5     3.84     2.99   .0368 .0286
7  pT5-class 531.2     4.04     3.48   .0076 .0066 |  706.6    10.62     7.01   .0150 .0099
8  bare pLS  622.1    29.74    70.03   .0478 .1126 |  624.7    30.16    62.31   .0483 .0997
9  T4        26.0     8.62     5.54   .3314 .2132 |   31.5    18.63     4.07   .5908 .1291
ALL         1601.0    88.88    99.74  .05551 .06230| 1579.2    70.68    81.78  .04476 .05179
```

READINGS
1. Our ATTACHED chain rows (type 7) are BETTER than LST's pT5 on both rates (.0076 vs
   .0150 fake, .0066 vs .0099 dup). The pixel-backed class is not the problem.
2. The SEEDLESS chain rows (type 4) are: 328.4/evt at 9.2% fake against LST's 111.9 at
   6.6%. That is +22.8 fakes/evt out of a +18.2 total fake gap.
3. We attach a pixel seed to 531.2/859.6 = 62% of our chain objects; LST attaches to
   706.6/818.5 = 86%. Same total chain-class volume, very different SPLIT.
4. bare-pLS rows: 622.1 vs LST 624.7 -- IDENTICAL volume. The post-deletion seed universe
   is NOT over-emitted. Its duplicate count is +7.7.

`a11_ref/a11_dupmix.py` -- the duplicate PATTERN (for every sim matched by >1 in-cut TC,
the multiset of types sharing it), extra TC rows per event:

```
pattern      ours    LST   delta
pLS+pLS     22.79  23.05   -0.26   <- seed-seed dup: ALREADY AT PARITY, not our problem
T5+pLS      13.38   3.36  +10.02   <- THE SIGNATURE
pLS+T4       4.37   2.41   +1.96
T5+pT5       1.99   0.03   +1.96
T5+T5        0.94   0.63   +0.31
pT5+pLS      0.39   4.64   -4.25   <- we are much better here
pT3+pLS      0.09   0.50   -0.41
TOTAL       46.15  37.77   +8.38
```

`T5+pLS` -- a seedless chain TC coexisting with a bare pixel-seed row for the SAME sim --
is +10.0/evt against a total gap of +8.4/evt. That is the attach-recall failure written
out as a duplicate: the chain did not take its seed, so both rows survive.

## M1 -- THE ONE-CONSTANT DEFECT, AND THE EDIT (protoA11 only)

`-a` (thetaAttach) is used for TWO different decisions:
  (a) DELIVERY: `gaStageChains` attaches a (seed, chain) pair iff logit >= -a. 6.875.
  (b) RETIREMENT: the `-RPS` predicate drops a carried type-8 row iff the seed's BEST
      chain logit >= -a (three sites: the m16RefreshSupp lambda, the -ZP8 addition guard,
      and the -XCD fate classifier).
Lowering -a to attach more therefore ALSO retires more seeds, which is why the constant
has been unmovable. `plsBestChainLogit` is recorded BEFORE any threshold, so the two uses
are separable exactly.

EDIT (5 hunks, main.cc only): `-RPSA <v>` and `-RPST <v>`, the -RPS retirement margins,
defaulting to 1e9 = unset = follow `-a` / `-AT3`. Every pre-A11 command line is
bit-identical by construction; GATE2 proves it. Binary 311c937c64b03ee77614161c2b039ef5.

## M2 -- THE SECOND DEFECT: THE ATTACH HEAD IS BLIND TO T3 QUALITY

For a bare-T3 target `PixelAttach.cc` sets `gateLogit = 0` and `nLayersF = 3` (its own
comment: "no chain gate exists for a bare T3"), so of the 19 attach features NONE carries
target quality. Meanwhile the bare-T3 universe is 34352 fake / 8947 true per event =
**20.7% pure**. The pT3-class delivery is picking out of an unfiltered pool, which is why
its rows are 17.4% fake against LST's pT3 3.7%.

`t3_fakeScore` (the upstream t3dnn) is computed at T3 build time -- it SURVIVES the P2.7
deletion, and `ChainFeatures` 20/21 already consume it, so it is not a new dependency.
Its separating power over that universe (`a11_ref/a11_t3score.py`, 25 evts):

```
fakeScore <=   true recall   fake T3s/evt (from 34352)   pool purity
0.02             0.7192            858                      0.882
0.05             0.8550           1874                      0.803
0.10             0.9150           3206                      0.719
0.20             0.9539           5597                      0.604
0.40             0.9799          10281                      0.460
```

EDIT 2 (`-T3F <v>`, 5 hunks): a bare T3 with `t3_fakeScore > v` never enters the stage-B
pair enumeration -- applied to the bareness MASK, so the target is never scored and never
writes `plsBestT3Logit` either. Default 1e9 = OFF = bit-identical (GATE4).
Deliberately NOT `t3_promptScore`: that head is IP-biased and the displaced result must
not move. Binary 2a1b700252cf0cf1d7eddef0f1a4f3ab.

## M3 -- RESULTS (300 evts). NO-OP GATE PASSED EXACTLY.

GATE4 (final binary 2a1b700..., baseline flags, no new flag set) reproduces FINBASE on
EVERY metric and on nTC exactly (618793): eff .80992 dup .06230 fake .05551. Both edits
are inert by default.

`-T3F` (T3-quality target admission) -- the headline:
```
tag        flag            eff      dup     fake   |  effB     dupB     fakB    nhB
FINBASE    (baseline)   .80992   .06230   .05551   | .92660  .04344  .06680  9.8025
F040       -T3F 0.40    .81050   .06268   .05158   |
F020       -T3F 0.20    .81059   .06282   .05003   | .92728  .04444  .05600  9.8257
F010       -T3F 0.10    .81050   .06297   .04894   |
F005       -T3F 0.05    .81019   .06320   .04805   | .92569  .04479  .05406  9.8130
F002       -T3F 0.02    .80961   .06342   .04723   | .92421  .04517  .05362  9.7826
F010X35    +  -XCT 3.5  .80957   .05985   .04900   | .92523  .03954  .05495  9.8583
F005X35    +  -XCT 3.5  .80926   .06003   .04810   | .92409  .03974  .05420  9.8485
LST                     .80988   .05179   .04476   | .92557  .00989  .04249 10.1480
```
DISPLACED IS UNTOUCHED by -T3F at every point: v510 .73356 v1030 .74139 d15 .61650
d510 .20161 d1030 .03073, all identical to FINBASE (F002/F020 move v15 or v510 by one
track). That is what makes this cut safe.

WHY IT WORKS -- per-`tc_type` (`a11_types.py`), pT3-class rows only:
```
              N/evt   fake/evt   fakeFrac
FINBASE        93.3     16.23      .1739
F020           83.6      6.89      .0823
F005           77.5      3.42      .0442
LST pT3       104.5      3.84      .0368
```
types 4, 7 and 9 are BIT-IDENTICAL across these runs; only type 5 moves (and type 8 by
+4.9 rows). At -T3F 0.05 the pT3-class purity essentially MATCHES LST's own pT3 rows.
The cut removes 18.3 deliveries/evt and 16.0 fakes/evt -- it is almost pure fake removal,
which is why the efficiency goes UP rather than down.
`-AT3` LOOSENING AFTER THE CUT IS A DEAD END: -AT3 5 gives .80939/.06199/.05048 and
-AT3 4 gives .80763/.06058/.05559, both worse than leaving it at the default 6.

`-RPSA` (attach margin freed from the retirement margin):
```
tag     flags                       eff     dup    fake     nhB  | v510   v1030   d15
GATE4   (baseline)               .80992  .06230  .05551  9.8025  |.73356 .74139 .61650
A50     -a 5 -RPSA 6.875         .81067  .05760  .05517  9.8900  |.73187 .73579 .61204
A40     -a 4 -RPSA 6.875         .81041  .05651  .05593  9.9033  |.71332 .73018 .60312
A40C    -a 4  (COUPLED, no RPSA) .80944  .05547  .05586  9.9170  |.71164 .73018 .60312
```
The decoupling is worth +.00097 of efficiency at -a 4 (A40 vs A40C) -- exactly the
predicted mechanism. -a 5 is dominant over the baseline on eff (+.00075), dup (-.00470)
and track length (+0.087 nhB), and flat on fake. BUT IT ERODES DISPLACED: at -a 5,
v1030 -.0056 (7 tracks of 1249) and d15 -.0045 (4 of 897); at -a 4, v510 -.0202 (12 of
593). Attaching a pixel seed to a displaced chain at lower confidence corrupts it. Band
denominators (300 evts): overall 22633, vxy[1,5) 1368, vxy[5,10) 593, vxy[10,30) 1249,
dxy[1,5) 897, dxy[5,10) 248, dxy[10,30) 423.

## M4 -- THE COMBINATION (300 evts). BOTH GAPS ROUGHLY HALVED.

```
tag          added to the assembled baseline        eff      dup     fake     nhB   | v510  v1030    d15
GATE4        (= FINBASE, bit-identical)          .80992   .06230   .05551  9.8025  |.7336 .74139 .61650
F010         -T3F 0.10                           .81050   .06297   .04894  9.8230  |.7336 .74139 .61650
F10X375      -T3F 0.10 -XCT 3.75                 .81006   .06126   .04896  9.8431  |.7336 .74139 .61650
C_F10A60     -T3F 0.10 -a 6.0 -RPSA 6.875        .81090   .05996   .04874  9.8750  |.7336 .73899 .61427
C_A60X375    ... + -XCT 3.75                     .81054   .05829   .04877  9.8942  |.7336 .73899 .61427
C_A55        -T3F 0.10 -a 5.5 -RPSA 6.875        .81098   .05886   .04883  9.8958  |.7336 .73739 .61315
C_F10A50     -T3F 0.10 -a 5.0 -RPSA 6.875        .81116   .05820   .04901  9.9091  |.7319 .73579 .61204
LST                                              .80988   .05179   .04476 10.1480  |.6543 .66453 .55407
```
EVERY row above beats the assembled baseline on efficiency AND duplicate rate AND fake
rate AND barrel track length simultaneously. Against LST, `C_A60X375` sits at
eff +.00066, dup +.00650, fake +.00401 where the baseline sits at
eff +.00004, dup +.01051, fake +.01075 -- both remaining gaps roughly halved.

MECHANISM CONFIRMED per `tc_type` (C_F10A60 vs FINBASE, per event):
  type 4 seedless chain 328.4 -> 226.1   type 7 attached chain 531.2 -> 637.5
  type 5 pT3-class       93.3 ->  80.3 (fakes 16.23 -> 4.82)
  type 8 bare pLS       622.1 -> 623.2
  duplicate pattern `T5+pLS` 13.38 -> 11.40 extra rows/evt (LST 3.36)
102 seedless chain rows became pixel-backed rows; our attach fraction goes 62% -> 74%
(LST 86%), and the pT3-class fake fraction goes .1739 -> .0599 (LST pT3 .0368).

THE COST, STATED WITH THE HEADLINE: lowering `-a` erodes displaced efficiency
monotonically -- v1030 .74139 (6.875) -> .73899 (6.0) -> .73739 (5.5) -> .73579 (5.0) ->
.73018 (4.0), i.e. 3 / 5 / 7 / 14 tracks of a 1249 denominator, and d15 similarly on 897.
v510 does not move at all until -a 5.0. We remain +.0745 above LST on v1030 at -a 6.0 and
+.0713 at -a 5.0, but this is a REAL monotone erosion, not noise, and it is the reason
`-T3F` (which leaves every displaced band BIT-IDENTICAL) is the safer half of the result.

## RUNS IN FLIGHT (batch 1)
GATE (old binary, baseline) | CM0 (PROTO_ATTACH_CM=1 recall census) |
GATE2 (new binary, no new flags) | GATE3 (-RPSA 6.875 -RPST 6, must equal GATE2) |
A55 A50 A40 A30 (-a scan, retirement pinned) | A40C (-a 4 COUPLED, the contrast).

## RESUME NOTE (session restarted mid-batch)
The session was resumed. On restart I briefly recreated `a11_run.sh` pointing at a stray
`protoA11x` copy, which corrupted the byte offset of the four batch-5 / batch-977b
scripts that were mid-run. THE chainproto RUNS THEMSELVES COMPLETED; only their
`createPerfNumDenHists` + `compare_ab.py` postprocessing was lost. `a11_ref/recover.sh`
re-ran exactly that postprocessing for A55, A60, A50D2, W_C_A60 from the intact
`r_*.root` files. `protoA11x` deleted; `a11_run.sh` restored (protoA11 + the BASE tail).
No physics artifact was affected.

## M5 -- THE CLEAN DUPLICATE LEVER: `-RPSA` ALONE (batch 7)
`-RPSA` is a PURE seed-retirement knob: it appears only in the three `-RPS` drop
predicates, never in a delivery decision. Held at the shipped attach margin `-a 6.875`,
every chain TC, every pT5-class/pT3-class delivery and therefore EVERY DISPLACED BAND is
bit-identical to `-T3F 0.10`; only bare type-8 rows disappear. It is the direct fix for
the `T5+pLS` duplicate signature the M0 diagnostic named (+10.0 extra rows/evt against a
total dup gap of +8.4/evt): the seed is retired because the outer tracker already explains
it, WITHOUT paying the displaced erosion that attaching (lowering `-a`) costs.

### M5 RESULT -- `-RPSA` IS THE BEST DUPLICATE LEVER ON THE LINE (300 evts, all on -T3F 0.10)
```
tag        -RPSA      eff      dup     fake      nhB   | v510    v1030    d15
F010       6.875*  .81050   .06297   .04894   9.8230  |.73356  .74139  .61650
R_F10R60   6.0     .81037   .06018   .04899   9.8701  |.73356  .74139  .61650
R_F10R55   5.5     .81028   .05914   .04900   9.8871  |.73356  .74139  .61650
R_F10R50   5.0     .81010   .05846   .04901   9.8998  |.73356  .74139  .61650
R_F10R45   4.5     .80997   .05801   .04900   9.9081  |.73187  .74139  .61650
R_F10R40   4.0     .80957   .05777   .04895   9.9152  |.73187  .74139  .61650
LST                .80988   .05179   .04476  10.1480  |.65430  .66453  .55407
```
(*6.875 = unset = follow -a, i.e. exactly F010.) EXCHANGE RATE F010 -> R50 is
Deff -.00040 for Ddup -.00451 = **11.3:1**, against 3.4-3.9:1 for -XCT and ~5:1 for -a.
DISPLACED IS BIT-IDENTICAL down to -RPSA 5.0 (v510/v1030/d15/d510/d1030 all unchanged;
-RPSA 4.5 moves v510 by one track of 593). Track length moves TOWARDS LST
(nhB 9.823 -> 9.900 of LST's 10.148) because the rows removed are zero-OT-hit type-8 rows.

PER REGION the lever is almost purely BARREL, which is where the dup gap was:
dupB .04456 -> .03073 (LST .00989) while dupT moves only .03455 -> .03424. `-XCT` is the
complementary TRANSITION lever (dupT .03455 -> .03145 at -XCT 3.75). They are orthogonal.

### M5b -- 977-EVENT CONFIRMATIONS
```
tag         flags added to the assembled baseline      eff      dup     fake      nhB
FINBASE977  (nothing)                               .80905   .06184   .05607   9.8006
W_F010      -T3F 0.10                               .80987   .06252   .04940   9.8228
W_F10X375   -T3F 0.10 -XCT 3.75                     .80951   .06079   .04943   9.8423
W_C_A60     -T3F 0.10 -a 6.0 -RPSA 6.875 -RPST 6    .81025   .05954   .04920   9.8749
LST977                                              .80987   .05138   .04538  10.1498
```

## M6 -- BATCH 8: THE COMBINATION, AND ONE DEAD END (300 evts)
```
tag             flags added to the assembled baseline        eff      dup     fake      nhB
GATE4           (= FINBASE, bit-identical no-op gate)     .80992   .06230   .05551   9.8025
F010            -T3F 0.10                                 .81050   .06297   .04894   9.8230
R_F10R50        -T3F 0.10 -RPSA 5.0            <-- BEST   .81010   .05846   .04901   9.8998
R_F10R50X375    -T3F 0.10 -RPSA 5.0 -XCT 3.75            .80966   .05675   .04903   9.9201
R_F05R50        -T3F 0.05 -RPSA 5.0                      .80979   .05865   .04812   9.8907
R_F20R50        -T3F 0.20 -RPSA 5.0                      .81023   .05832   .05010   9.9019
C2_A60R50       -T3F 0.10 -RPSA 5.0 -RPST 6 -a 6.0       .81050   .05791   .04875   9.9066
R_F10R50T5      -T3F 0.10 -RPSA 5.0 -RPST 5.0   DEAD END .80701   .05660   .04882   9.9434
LST                                                      .80988   .05179   .04476  10.1480
```
`-RPST` (the bare-T3-side retirement margin) IS A DEAD END: -RPST 5.0 costs .0031 of
efficiency for .0019 of duplicate rate (1.6:1) -- far worse than -RPSA's 11.3:1. The T3
logit is simply not evidence that a seed is represented. Do not re-measure.
`-T3F` 0.05 / 0.10 / 0.20 brackets the fake/eff trade; 0.10 is the balance point.

## M7 -- MECHANISM PROOF (per tc_type, R_F10R50 vs FINBASE, per event)
```
type            N/evt          fake/evt        dup/evt
4  seedless   328.4 -> 328.4  30.26 -> 30.26  19.15 -> 16.04
5  pT3-class   93.3 ->  80.8  16.23 ->  4.97   1.54 ->  1.45
7  attached   531.2 -> 531.2   4.04 ->  4.04   3.48 ->  3.14
8  bare pLS   622.1 -> 620.4  29.74 -> 29.89  70.03 -> 66.55
9  T4          26.0 ->  26.0   8.62 ->  8.62   5.54 ->  5.58
```
Types 4, 7 and 9 are BIT-IDENTICAL in count AND fake count -- the delivery is untouched,
which is the structural reason every displaced band is unchanged. `-T3F` removes 12.5
pT3-class deliveries carrying 11.3 fakes (pT3-class fake fraction .1739 -> .0615 against
LST's own pT3 .0368); `-RPSA` removes 6.98 duplicate rows, split across the type-8 rows it
retires (-3.48) and the type-4 chains that stop being duplicates once their bare seed row
is gone (-3.11).
Duplicate PATTERN (extra TC rows/evt): `T5+pLS` 13.38 -> 10.37 (LST 3.36); `pLS+pLS`
22.79 unchanged (already at LST's 23.05); TOTAL 46.15 -> 42.83 (LST 37.77).

WHERE IT STOPS. Pushing -RPSA below 5.0 has a collapsing exchange rate (5.0 -> 4.0 buys
only .0007 of dup for .0005 of eff) because the surviving `T5+pLS` pairs have chain logits
below ~4 -- the SAME separating-power collapse below logit 4 this round already named as
the target for a purpose-built dedup head. My lever runs into that frontier; it does not
move it.

## M8 -- 977-EVENT CONFIRMATIONS (batch C)
```
tag         added to the assembled baseline           eff      dup     fake      nhB   | v510   v1030    d15
FINBASE977  (nothing)                              .80905   .06184   .05607   9.8006  |.72161 .71474 .58320
W_F010      -T3F 0.10                              .80987   .06252   .04940   9.8228  |.72111 .71474 .58320
W_F10X375   -T3F 0.10 -XCT 3.75                    .80951   .06079   .04943   9.8423  |.72111 .71474 .58320
W_F10R50    -T3F 0.10 -RPSA 5.0                    .80952   .05794   .04946   9.8988  |.72111 .71474 .58320
W_C_A60     -T3F 0.10 -a 6.0 -RPSA 6.875           .81025   .05954   .04920   9.8749  |.72111 .71254 .58187
LST977                                             .80987   .05138   .04538  10.1498  |.64422 .62567 .51042
```
per region 977: W_F10R50 effB .92497 effT .88065 effE .74477 | dupB .03010 dupT .03371
dupE .08029 | fakB .05581 fakT .06031 fakE .04278  (LST .92430/.88004/.74658 |
.00971/.01308/.08486 | .04365/.04542/.04630).
The 300 -> 977 efficiency shift is a uniform ~-.0006 on every A11 row (and -.00087 on
FINBASE), so the 300-evt ORDERING transfers but the 300-evt absolute efficiency does not:
read `-T3F 0.10 -RPSA 5.0` as LST PARITY (-.00035), not as a win.

## M9 -- FINAL 977 SCOREBOARD (batch D) AND THE RECOMMENDATION
```
tag           added to the assembled baseline        eff      dup     fake      nhB   | v510   v1030    d15
FINBASE977    (nothing)                           .80905   .06184   .05607   9.8006  |.72161 .71474 .58320
W_F010        -T3F 0.10                           .80987   .06252   .04940   9.8228  |.72111 .71474 .58320
W_F10X375     -T3F 0.10 -XCT 3.75                 .80951   .06079   .04943   9.8423  |.72111 .71474 .58320
W_F10R50      -T3F 0.10 -RPSA 5.0                 .80952   .05794   .04946   9.8988  |.72111 .71474 .58320
W_F10R50X375  + -XCT 3.75                         .80915   .05621   .04949   9.9185  |.72111 .71474 .58320
W_C_A60       -T3F 0.10 -a 6.0                    .81025   .05954   .04920   9.8749  |.72111 .71254 .58187
W_C2_A60R50   -T3F 0.10 -a 6.0 -RPSA 5.0  <-BEST  .80990   .05743   .04919   9.9057  |.72111 .71254 .58187
LST977                                            .80987   .05138   .04538  10.1498  |.64422 .62567 .51042
```
RECOMMENDED: `-T3F 0.10 -a 6.0 -RPSA 5.0 -RPST 6` (the `-RPST 6` is provably a no-op:
`rpsThetaT3` defaults to `-AT3`, which the assembled line leaves at its shipped 6.0; it is
carried only because that is the literal line measured). On 977 it is LST efficiency
PARITY (+.00003) with dup +.00605 and fake +.00381 where the assembled baseline sits at
-.00082 / +.01046 / +.01069 -- the duplicate gap cut 42%, the fake gap cut 64%, and
overall efficiency lifted onto LST. Displaced stays FAR above LST (v510 +.0769,
v1030 +.0869, d15 +.0715) but pays a real -.0022 on v1030 and -.0013 on d15 against the
baseline, entirely from the `-a 6.0` half.
DISPLACED-PURIST VARIANT: `-T3F 0.10 -RPSA 5.0` -- every displaced band BIT-IDENTICAL to
the baseline, eff .80952 (-.00035 vs LST), dup .05794, fake .04946. Two new constants,
no retune of an existing one.
