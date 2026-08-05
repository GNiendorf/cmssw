# A06 -- TRACK LENGTH -- STATUS

Angle: recover the barrel / transition mean-nhitOT deficit vs LST without paying duplicate
rate. Workspace `standalone/protoA06` (byte copy of protoFIN, binary md5
519b0abc34a28cd1e803d6b9407ef224). Artifacts `standalone/a06_ref/`.
Runner: `bash a06_ref/a06_run.sh <TAG> [overrides]` -- frozen prefix + the ASSEMBLED
BASELINE tail `-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2` are inside the script, so a bare
`a06_run.sh GATE` IS the baseline.

## M0 SETUP (DONE)
protoFIN copied to protoA06, binary md5 identical, no rebuild, no source edits planned.

## M1 GATE: PASSED EXACTLY
`a06_run.sh GATE` (no overrides) reproduces FINBASE on all 14 headline metrics, all 12
per-region metrics and nTC exactly: eff .80992 dup .06230 fake .05551,
nhitOT 9.80254 / 9.87906 / 3.55956, nTC 618793 = 618793, f12 .600/.445/.480.
No source edits were made in protoA06 at any point this round, so the binary is still
md5 519b0abc34a28cd1e803d6b9407ef224 and every result below is a pure FLAG delta.

## M2 ATTRIBUTION (DONE, offline, from the already-written r_FINBASE.root and rb_base300.root)
Tools: `a06_decomp.py` (class decomposition of the harness metric), `a06_hist.py`
(nhitOT distribution per class). Both reproduce the published means to 5 decimals, so the
decomposition is exact, not a model.

THE METRIC. `mean_nhitOT` = sum(tc_nhitOT)/nTC over EVERY TC row with pt > 0.9 -- fakes and
duplicates included. It is a MIXTURE mean over classes with FIXED per-class values:
bare pLS rows 0, pT3-class rows exactly 6, T4-class 8, and the long objects quantised at
10 / 12 / 14 (5 / 6 / 7 MDs).

BARREL (ours 9.80254, LST 10.14804, gap -0.3455). Per event, pt > 0.9:
```
                 A06 baseline                    LST
  pT5cls   226.89 @ 11.266   (10:36.7% 12:63.3%)  317.65 @ 11.627 (10:18.7% 12:81.3%)
  T5       155.39 @ 11.102   (10:44.9% 12:55.1%)   48.25 @ 11.250 (10:37.5% 12:62.5%)
  T4        10.11 @  8.000                         12.60 @  8.000
  pT3cls    53.68 @  6.000                         60.09 @  6.000
  pLS       31.80 @  0.000                         24.30 @  0.000
  TOTAL    477.86 @  9.80254                      462.89 @ 10.14804
  [fake]    31.92 @  8.612                         19.67 @  8.511
  [dup ]    20.76 @  6.051                          4.58 @  7.245
  [good]   425.18 @ 10.075                        438.65 @ 10.252
```
TWO CAUSES, ROUGHLY HALF EACH (Shapley-style single-substitution):
 * give A06 LST's fake/dup SHARES, keep its own class means -> 9.973 (+0.171, 49%)
 * give A06 LST's good-row MEAN, keep its own shares          -> 9.960 (+0.158, 46%)
The first half is the duplicate + fake excess, i.e. NOT this angle's lane -- it is the same
+.0105 dup / +.0108 fake already on the scoreboard, restated in hits. The second half IS
this angle: our long objects are 0.38 hits shorter in barrel, 0.24 in transition.

THE MECHANISM OF THE SECOND HALF IS EXACTLY ONE LAYER. The distribution is discrete and
the ONLY thing that moves is the 10-vs-12 split: 63.3% of our barrel pT5-class rows have
6 MDs against LST's 81.3%, and 55.1% of our bare-chain T5 rows against LST's 62.5%. Nobody
is losing two layers. Converting the excess 10-hit barrel rows to 12 (about 52 rows/evt)
would be worth +0.218 of the barrel mean -- 63% of the whole gap and 137% of the half this
angle owns. This is precisely the "short by exactly one layer, outermost terminal" signature
Extend.h was written for.

ENDCAP IS AT PARITY (3.55956 vs 3.56248) ONLY BY CANCELLATION: 65.6% of endcap rows are
0-hit bare seeds (LST 67.7%), and having FEWER of them exactly offsets our shorter long
objects there. Do not read endcap parity as "the endcap is fine".

TWO PRE-EXISTING MACHINES ACT ON THE 10-vs-12 SPLIT, both already in the frozen line:
 * TERMINAL TRIM `-TR 1 -TT 1.2 -TL 5 -TA 1.0`: examined 1566.8/evt, TRIMMED 241.7/evt
   (outer 214.3, inner 27.7). Every trim drops exactly one terminal MD = 2 OT hits.
 * CHAIN EXTENSION `-EX 1 -EXW 0.25 -EXR 2.0 -EXS 1`: 1037.0 chains/evt examined,
   2451.6 candidate MDs/evt, but only 60.1/evt extended (5.79%), all outer (mode 1),
   chi2-rejected only 6.8/evt and uniq/fit-rejected ZERO. The extension is NOT being
   limited by its quality guards -- it is being limited by its SEARCH (outer end only,
   1 MD per end, LS-linked only, |dlayer| <= 1, 0.25 cm xy window).
That asymmetry (a trim that fires 241.7 times and an extension that fires 60.1 times, with
the extension's quality guards idle) is the concrete, measured opening.

CROSS-CHECK FROM THE LST SIDE (source, not a fit): `LSTCore/interface/Common.h:98` declares
the T5 object as `kLayers = 7, kHits = 14;  // 5 base + max 2 extensions`, and pT5 as
`kLayers = 9  // 2 pixel + 7 OT (= T5::kLayers after extension)`. So LST's own delivered
long object is a 5-MD core PLUS up to two extension layers -- exactly the 10 / 12 / 14
quantisation both files show. The comparison is therefore like-for-like: same object shape,
same mechanism class, and the measured difference is how OFTEN each side lands the extra
layer (LST 81.3% of barrel long rows at 12+, us 63.3%).

THE TWO HALVES SEPARATED CLEANLY (a06_len.py, `good` = not fake and not duplicate):
```
                nhB      nhT      nhE  | goodB    goodT    goodE  | f12B  f12T  f12E | n0B
FINBASE      9.8025   9.8791   3.5596  |10.0751  10.1114   3.7887 | .600  .445  .480 | 31.8
LST         10.1480  10.0155   3.5625  |10.2517  10.1520   3.7708 | .789  .592  .776 | 24.3
GATE(port)   9.4861   9.7803   3.3876  | 9.7183  10.0299   3.6052 | .600  .445  .480 | 67.5
```
ON GOOD ROWS ONLY the gap is  barrel -0.177,  transition -0.041,  endcap +0.018.
So the TRANSITION deficit is essentially ALL dup/fake dilution and the ENDCAP good rows are
already LONGER than LST's. **The only real length deficit this angle owns is the BARREL
-0.177 on good rows**, and its mechanism is the single number f12B.

f12 (fraction of LONG rows -- type 4 and 7 -- carrying 12 or more OT hits) is the clean
target: it is IDENTICAL between FINBASE and the port-round GATE (.600/.445/.480 both), i.e.
it is untouched by every dedup / pT3 / crossclean knob moved this round and by every knob
the other fourteen explorers are moving. It is a pure chain-construction quantity.
CEILING ARITHMETIC: +1 barrel extension per event = +2 hits / 477.86 barrel rows =
+0.00419 of nhB. Reaching LST's f12B .789 means +72.3 twelve-hit barrel rows/evt =
+0.303 of nhB, which alone would close 88% of the whole barrel gap.

SAME PICTURE ON THE FULL 977 (a06_len.py on fin_ref/fin_base977.root and fin_ref/r_W_X4.root):
```
                nhB      nhT      nhE  | goodB    goodT    goodE  | f12B  f12T  f12E | n0B
FINBASE 977  9.8006   9.8837   3.5572  |10.0724  10.1177   3.7861 | .597  .443  .482 | 32.1
LST     977 10.1498  10.0094   3.5567  |10.2556  10.1529   3.7684 | .786  .590  .776 | 24.4
```
Every length quantity transfers from the frozen 300 to within .003. Iterate on the 300.

WHERE THE SHORT ROWS SIT (a06_f12.py; f12 of type-4/7 rows only, per event):
```
                A06 baseline                               LST
barrel  pt .9-1.5 .578 / 1.5-3 .627 / 3-10 .661   |  .759 / .828 / .857
        good .621 (354.2/evt)  dup .510  fake .210|  good .800 (355.9/evt) dup .680 fake .296
trans   good .462 (204.4/evt)                     |  good .598 (202.7/evt)
endcap  good .485 (244.1/evt)                     |  good .780 (229.4/evt)
```
Three readings that matter. (1) The GOOD long-row population is the SAME SIZE on both sides
in barrel (354.2 vs 355.9) and transition (204.4 vs 202.7) -- we find the same tracks, we
just build them one layer shorter. (2) The deficit is FLAT IN pt (-0.18 to -0.20 in f12 in
every pt band), so it is not a low-momentum extrapolation problem. (3) The ENDCAP long
objects are the WORST (-0.295 in f12) even though the endcap mixture mean reads at parity --
endcap parity is bought entirely by our having fewer 0-hit bare-seed rows than LST, and it
will stop being parity the moment a sibling's dedup removes more of them.

## M2b -- f12 IS ORTHOGONAL TO EVERY KNOB THE OTHER ROUNDS MOVED (proved, not argued)
`a06_len.py` run over ALL ~50 output files the Baseline agent produced this round
(a06_ref/len_finref_sweep.txt) returns **f12 = 0.600 / 0.445 / 0.480 and nLongB = 382.3 in
every single one** -- every -AT3 from 6 to 9, every -XCT from 2 to 5.25, -CC on and off,
-XC on and off, -CCN 1 and 2, -CCR 1 and 2. The same holds on 977 (0.597/0.443/0.482 for
W_X4, W_X45, W_A7X475 and W_GATE alike) and for the port round's xc_ref points.
What DOES move along that whole frontier is n0B, the barrel 0-hit row count (28.7 at
-XCT 3 up to 59.7 at -AT3 9), and nhB tracks it exactly: 9.93 down to 9.60.
So the barrel length metric decomposes into two strictly separate terms --
  * n0B  : owned by the duplicate/crossclean work (the other explorers), and
  * f12  : owned by chain construction, i.e. by this angle and nobody else.
Everything below moves f12. Nothing below touches n0B.
(Coverage: 50 fin_ref outputs + 30 xc_ref outputs = 80 distinct configurations spanning two
whole rounds of tuning; `awk` over a06_ref/len_xcref_sweep.txt returns exactly one distinct
f12 triple, count 30. This is as close to a proof of orthogonality as a measurement gets.)

## M3 BATCH 1 (7 single-flag deltas, 300 evts, launched)
  E_E3   -EX 3               inner end as well as outer
  E_S0   -EXS 0              drop the LS-link requirement (spatial index instead)
  E_J2   -EXJ 2              allow a 2-layer jump
  E_N2   -EXN 2              2 MDs per end
  E_W50  -EXW 0.50 -EXR 4.0  wider residual windows
  T_TR0  -TR 0               terminal trim OFF (upper bound reference, expected fake cost)
  T_TA3  -TA 3.0             concentrate the trim on genuinely mis-fitting chains

## M4 -- BATCH 1 RESULT: THE EXTENSION IS ALMOST FREE, THE TRIM IS NOT
```
tag     change                   eff      dup     fake     nhB      nhT      nhE  | f12B  f12T  f12E | ext/evt
FINBASE (baseline)           .80992   .06230   .05551  9.80254  9.87906  3.55956 | .600  .445  .480 |  60.1
E_E3    -EX 3                .80944   .06211   .05623  9.91409  9.92226  3.56748 | .666  .468  .485 | 100.7
E_W50   -EXW .5 -EXR 4       .80975   .06218   .05585  9.84872 10.09403  3.59047 | .628  .526  .507 | 120.9
E_S0    -EXS 0               .80930   .06211   .05630  9.81648  9.92546  3.56545 | .602  .461  .488 |  74.4
E_J2    -EXJ 2               .80992   .06230   .05552  9.80676  9.87999  3.55956 | .602  .445  .480 |  61.4
E_N2    -EXN 2               .80992   .06230   .05551  9.80254  9.88094  3.55992 | .600  .445  .480 |  60.1
T_TA3   -TA 3.0              .80908   .06263   .05602  9.86737  9.97928  3.63717 | .647  .500  .568 |  56.8
T_TR0   -TR 0 (trim off)     .80820   .06345   .05687  9.86707 10.00400  3.71103 | .652  .518  .657 |  54.3
LST                          .80988   .05179   .04476 10.14804 10.01546  3.56248 | .789  .592  .776 |   --
```
READINGS
1. **-EX 3 (turn on the INNER end) is the single best barrel lever.** 60.1 -> 100.7
   extensions/evt (inner contributes 41.5), f12B .600 -> .666, nhB +0.112, and it makes the
   DUPLICATE RATE SLIGHTLY BETTER (-.00019), because an extension cannot change the TC
   count -- it only rewrites a hit list. Cost: eff -.00048, fake +.00072.
2. **The residual windows are the transition lever.** -EXW 0.25 -> 0.50 with -EXR 2 -> 4
   doubles the extension volume (120.9/evt) and moves nhT +0.215, PAST LST (10.094 vs
   10.015), for eff -.00017 / fake +.00034 / dup -.00012. Note 0.50 is the SHIPPED DEFAULT;
   the frozen line had tightened it.
3. **-EXJ 2, -EXN 2 and -EXS 0 are dead ends.** -EXJ/-EXN move nothing (the barrel has only
   6 layers, so a chain missing one layer can gain exactly one and never two). -EXS 0 (drop
   the LineSegment-link requirement) buys +14/evt extensions for 6x the runtime
   (10.97 ms/evt vs 1.73) and the WORST efficiency of the batch: the detector's own segment
   adjacency is a better candidate filter than a free-form spatial window. Do not re-measure.
4. **The trim levers work but bill efficiency AND duplicate rate.** -TR 0 gives the biggest
   endcap length move of anything measured (f12E .480 -> .657, nhE +0.151) but costs
   -.00172 eff, +.00115 dup, +.00136 fake. -TA 3.0 is the better half of it (57% fewer
   trims, -.00084 eff, +.00033 dup). Both are DOMINATED by the extension levers on
   length-per-unit-efficiency, so neither goes in the recommendation.
5. n0B is 31.8-32.6 in every row: none of these levers touches the duplicate term of the
   metric, exactly as designed.

## M5 -- BATCH 2 (combination + the extension's three IDLE quality guards)
  C_A   -EX 3 -EXW .5 -EXR 4                   the combination
  C_AU  ... -EXU 0.10    ambiguity guard (never fired at any setting so far)
  C_AC  ... -EXC 1.0     own-fit-quality guard (never fired)
  C_AF  ... -EXF 1.2     tighter refit chi2 factor (the one guard that DOES fire)
  C_B   -EX 3 -EXW 1.0 -EXR 6                  aggressive windows
## M4b -- THE THREE EXTRA PROBES
```
tag     change                   eff      dup     fake     nhB      nhT      nhE  | f12B  f12T  f12E | ext/evt
GATE    (no-op)              .80992   .06230   .05551  9.80254  9.87906  3.55956 | .600  .445  .480 |  60.1
E_WW    -EXW 1.0 -EXR 6.0    .80864   .06198   .05675  9.93616 10.44341  3.67222 | .682  .664  .616 | 239.7
T_TL6   -TL 6                .80811   .06342   .05685  9.86707 10.00397  3.70305 | .652  .518  .657 |  54.5
L_L5    -L 5.0               .81006   .06231   .05550  9.81074  9.90244  3.57154 | .605  .455  .494 |  58.4
```
* THE -EXW KNEE IS AT 0.50. Extension volume 60.1 -> 120.9 -> 239.7 for -EXW .25/.50/1.0,
  but the efficiency bill is convex: .25->.50 costs .00017, .50->1.0 costs .00111 (6.5x for
  the same doubling). 0.50 is also the SHIPPED DEFAULT.
* -TL 6 all but abolishes the trim (241.7 -> 11.3 trims/evt) and lands on the same point as
  -TR 0: it is the trim-off bound under another name, and it is priced the same.
* -L 5.0 (stronger chain-length prior in the K6 score / K9 order) is the only thing measured
  all round that moves length and efficiency in the SAME direction (+.00014 eff, +0.008 nhB,
  +0.023 nhT, +0.012 nhE, dup/fake flat to 1e-5). It is real but small; it buys a fifth of
  what -EX 3 does for one more tuned constant, so it is REPORTED, NOT RECOMMENDED.

## M5 RESULT -- BATCH 2 (combination + the three idle guards). RESUMED SESSION 23:45.
On resume I found two duplicate 977 processes writing the SAME output file (r_W_C_A.root,
launched twice) -- both killed, that file is INVALID and was not used. The batch-2 300-evt
runs all completed; C_AC/C_AU needed a06_post.sh.
```
tag   change                        eff     dup    fake      nhB      nhT      nhE | outer inner /evt
GATE  (baseline)               .80992  .06230  .05551   9.8025   9.8791  3.5596 |  60.1    0
C_A   EX3 EXW.5 EXR4           .80900  .06175  .05723   9.9788  10.1670  3.6073 | 120.9  56.7
C_AF  ... -EXF 1.2             .80939  .06190  .05659   9.9218  10.0464  3.5894 |  88.9  46.1
C_AC  ... -EXC 1.0             .80953  .06207  .05629   9.9218  10.0226  3.5811 |  89.2  38.1
C_AU  ... -EXU 0.10            .80939  .06195  .05653   9.9241  10.1202  3.5974 | 110.8  39.0
C_B   EX3 EXW1.0 EXR6          .80771  .06134  .05883  10.0869  10.5313  3.6946 | 239.7  69.6
LST                            .80988  .05179  .04476  10.1480  10.0155  3.5625 |
```

## M6 -- THE DECISIVE ATTRIBUTION: THE DISPLACED COST IS **ENTIRELY** THE INNER ARM
E_W50 (`-EXW .50 -EXR 4.0`, -EX 1) and C_A (`-EX 3 -EXW .50 -EXR 4.0`) have the SAME outer
extension count to the unit (36277 = 36277); C_A merely adds 17021 inner ones. So their
difference IS the inner arm, cleanly:
```
                          eff     v510    v1030      d15     dup    fake      nhB
E_W50 (outer only)     .80975  .73356  .74139   .61650  .06218  .05585   9.8487
C_A   (+ inner)        .80900  .72850  .72858   .58974  .06175  .05723   9.9788
GATE  (baseline)       .80992  .73356  .74139   .61650  .06230  .05551   9.8025
```
E_W50's displaced bands are BIT-IDENTICAL to the baseline (v510 .73356, v1030 .74139,
d15 .61650, d510 .20161, d1030 .03073 -- every digit). Adding the inner arm costs
-.0268 of d15 (= -11.5 tracks on a 430 denominator) and -.0128 of v1030 (= -16 tracks on
1249) for +0.130 of nhB. Denominators measured, not assumed: vxy 1-5 = 1368, 5-10 = 593,
10-30 = 1249; dxy 1-5 = 430, 5-10 = 142, 10-30 = 199 (300 evts).
PHYSICAL READING: a track born at vxy 10-30 cm has NO hit on the inner layers the inner
arm extrapolates into, so the only thing there to find is a pileup MD that happens to sit
on the extrapolated circle. The chi2 and ambiguity guards reduce it (C_AF/C_AC/C_AU) but
never remove it. **The inner arm is therefore REJECTED on the standing priority order
(displaced efficiency is protected), regardless of how much barrel length it buys.**
E_WW (`-EX 1 -EXW 1.0 -EXR 6.0`, outer only, 239.7 ext/evt) leaves displaced essentially
untouched too (v1030 -.0008, d15 -.0011, d510/d1030 exactly 0) -- confirming from the
other side that the OUTER arm is displaced-safe even 4x wider than the frozen window.

## M7 -- BATCH 3 + BATCH 4 LAUNCHED
batch3: D_F10 D_F08 D_WF12 D_W50 D_R4 D_E3F  (guard scan + splitting -EXW from -EXR)
batch4: G_W75 G_WWU G_WWF G_WWUF G_L8        (outer-only knee + the idle guards on it)

## M8 -- BATCH 3 + 4 RESULT: THE rz HALF OF THE WINDOW IS THE FREE HALF
```
tag      change (all -EX 1, outer only)     eff      dup     fake      nhB      nhT      nhE | ext/evt
GATE     baseline (-EXW .25 -EXR 2)      .80992  .06230  .05551   9.8025   9.8791  3.5596 |  60.1
D_R4     -EXR 4.0                        .80992  .06225  .05562   9.8274   9.9299  3.5657 |  77.0
D_W50    -EXW 0.50                       .80975  .06226  .05563   9.8166   9.9989  3.5746 |  90.3
D_WF12   -EXW .5 -EXR 4 -EXF 1.2         .80984  .06222  .05566   9.8076   9.9933  3.5782 |  88.9
E_W50    -EXW .5 -EXR 4                  .80975  .06218  .05585   9.8487  10.0940  3.5905 | 120.9
G_WWUF   -EXW 1 -EXR 6 -EXU .25 -EXF 1.2 .80939  .06215  .05587   9.8657  10.2244  3.6074 | 155.5
G_W75    -EXW .75 -EXR 5                 .80926  .06210  .05615   9.8835  10.2715  3.6226 | 173.9
G_WWF    -EXW 1 -EXR 6 -EXF 1.2          .80922  .06207  .05616   9.8816  10.2883  3.6236 | 178.5
G_WWU    -EXW 1 -EXR 6 -EXU .25          .80895  .06208  .05629   9.9121  10.3560  3.6469 | 206.6
E_WW     -EXW 1 -EXR 6                   .80864  .06198  .05675   9.9362  10.4434  3.6722 | 239.7
inner-arm rows (rejected, see M6):
D_F08    -EX 3 ... -EXF 0.8              .80997  .06227  .05552   9.7742   9.7984  3.5563 |  46.9
D_F10    -EX 3 ... -EXF 1.0              .80957  .06203  .05628   9.8938   9.9714  3.5790 | 108.9
C_E2     -EX 2 (inner INSTEAD of outer)  .80944  .06212  .05611   9.8344   9.7729  3.5379 |  41.6
LST                                      .80988  .05179  .04476  10.1480  10.0155  3.5625 |
```
1. `-EXR 4.0` ALONE IS FREE: efficiency .80992 = the baseline to five decimals, duplicate
   rate .06225 (BETTER by 1.4e-4 -- an extension cannot change nTC, it only rewrites a hit
   list, so a longer hit list can only tighten the harness duplicate test), fake +1.1e-4,
   and every displaced band except v510 (-1 track) bit-identical. Extensions 60.1 -> 77.0.
   Extend.h predicts exactly this: the two residuals have different intrinsic scales (a 2S
   strip is ~5 cm long in z but ~100 um in r-phi), so the frozen -EXR 2.0 was throttling
   the extension on the axis that carries no information.
2. EVERY OUTER-ONLY ROW LEAVES DISPLACED ALONE OR IMPROVES IT. G_WWUF is the extreme case:
   v510 .73524 (+.0017), v1030 .74219 (+.0008), d15 .61761 (+.0011) -- all three BETTER
   than the baseline, d510/d1030 identical. Compare the inner-arm rows, which lose 5-12
   displaced tracks apiece. The outer/inner asymmetry is the whole story of this angle.
3. DUPLICATE RATE FALLS MONOTONICALLY WITH EXTENSION VOLUME (.06230 -> .06198 at 239.7
   ext/evt). Length and duplicate rate are ALIGNED here, not traded.
4. THE ONLY BILL IS EFFICIENCY AND FAKE, and it is convex in extension volume:
   60->77 ext costs 0.0 eff, 77->121 costs .00017, 121->156 costs .00036, 156->240
   costs .00075. -EXF 1.2 and -EXU 0.25 both buy efficiency back: at -EXW 1 -EXR 6 the
   raw point is .80864 and the guarded point (both guards) is .80939 for 65% of the length.
5. -EXF 0.8 OVERSHOOTS: it kills more extensions than the frozen -EXF 2.0 allowed and the
   length goes BELOW baseline (nhB 9.7742). The guard has a floor.

## M9 -- BATCH 5 LAUNCHED: H_R6 H_R10 H_W50R6 H_W50U H_W50L5 H_WWU5F

## M10 -- BATCH 5 RESULT + THE FRONTIER, and WHERE THE BILL LANDS
```
tag        change (all outer-only)         eff      dup     fake     nhB      nhT     nhE | ext/evt
GATE       baseline                     .80992  .06230  .05551  9.8025   9.8791  3.5596 |  60.1
D_R4       -EXR 4.0                     .80992  .06225  .05562  9.8274   9.9299  3.5657 |  77.0
H_R6       -EXR 6.0                     .80979  .06224  .05567  9.8259   9.9382  3.5675 |  78.7
H_R10      -EXR 10.0                    .80970  .06223  .05572  9.8250   9.9387  3.5685 |  79.0
D_WF12     -EXW .5 -EXR 4 -EXF 1.2      .80984  .06222  .05566  9.8076   9.9933  3.5782 |  88.9
H_W50U     -EXW .5 -EXR 4 -EXU .25      .80975  .06223  .05569  9.8379  10.0588  3.5819 | 107.5
E_W50      -EXW .5 -EXR 4               .80975  .06218  .05585  9.8487  10.0940  3.5905 | 120.9
H_W50L5    -EXW .5 -EXR 4 -L 5.0        .80988  .06219  .05584  9.8555  10.1153  3.6015 | ~121
H_WWU5F    -EXW 1 -EXR 6 -EXU .5 -EXF 1.2 .80953 .06215  .05580  9.8630  10.1962  3.6022 | 148.0
LST                                     .80988  .05179  .04476 10.1480  10.0155  3.5625 |
```
* THE rz WINDOW SATURATES AT 4 cm: -EXR 4 -> 6 -> 10 adds 2.0 extensions/evt in total and
  starts costing efficiency (.80992 / .80979 / .80970). 4.0 is the right value and it is
  FREE. Do not go past it.
* -EXU 0.50 beats -EXU 0.25 (H_WWU5F .80953 dominates G_WWUF .80939 on efficiency AND fake
  at the same length). The ambiguity guard is the best of the three.
* THE EFFICIENCY BILL IS ENTIRELY IN THE TRANSITION REGION. Per-region for E_W50:
  effB .92660 (unchanged, LST .92557), effE .74496 (unchanged), effT .88213 -> .88111
  (LST .88187). Barrel and endcap efficiency do not move at all along the whole outer-arm
  frontier; only the transition pays. That is also where the length gain is biggest
  (nhT +0.215), so the trade is local and legible, not diffuse.
* `-L 5.0` IS A FREE RIDER AND STACKS. Alone it is better than the baseline in ALL THREE
  regions (effB +.00011, effT +.00025 which is ABOVE LST, effE +.00011) with dup/fake flat
  and length +0.008/+0.023/+0.012. Added on top of E_W50 it recovers .00013 of the .00017
  and adds another +0.007/+0.021/+0.011 of length. It is the only knob measured all round
  that moves efficiency and length in the same direction.

## M11 -- 977 CONFIRMATION LAUNCHED (W_EW50, W_L5, W_R4) + BATCH 6 (J_N2 J_N2J2 J_L5N2)

## M12 -- BATCH 6 RESULT: -EXN 2 / -EXJ 2 ARE FREE BUT TOO SMALL TO EARN A KNOB
```
tag       change on top of E_W50        eff      dup     fake     nhB      nhT      nhE | f12B
E_W50     -EXW .5 -EXR 4             .80975  .06218  .05585  9.8487  10.0940  3.5905 | .628
J_N2      + -EXN 2                   .80975  .06218  .05585  9.8487  10.1043  3.5926 | .628
J_N2J2    + -EXN 2 -EXJ 2            .80975  .06218  .05587  9.8634  10.1072  3.5926 | .638
J_L5N2    H_W50L5 + -EXN 2           .80988  .06219  .05584  9.8555  10.1247  3.6032 | .633
```
-EXN 2 -EXJ 2 is genuinely free (efficiency, duplicate rate and EVERY displaced band
bit-identical to E_W50; fake +2e-5) and buys +0.015 of nhB by letting a chain walk two
layers outward one MD at a time. It is rejected only on the simplicity criterion: two more
tuned constants for 1/3 of what the single -EXR 4.0 delivers. Recorded for the synthesis
round in case barrel length turns out to be the binding criterion.
-EXN 2 WITHOUT -EXJ 2 does nothing in barrel, which identifies the true limit: the barrel
chains that are short are short at a layer the -EXJ 1 jump cannot reach.

## M13 -- FULL 977 CONFIRMATION (LST reference fin_ref/fin_base977_hists.root)
```
tag                    eff    vxy01     v15    v510   v1030     d15    d510   d1030     dup    fake     nhB      nhT     nhE      nTC
W_X4 (= FINBASE)   .80905  .84144  .80110  .72161  .71474  .58320  .24521  .03151  .06184  .05607  9.8006   9.8837  3.5572  2033868
W_R4  -EXR 4       .80896  .84137  .80066  .72111  .71425  .58286  .24521  .03151  .06181  .05619  9.8261   9.9347  3.5631  2033844
W_EW50 -EXW.5 -EXR4.80873  .84116  .79912  .72111  .71279  .58154  .24521  .03151  .06177  .05641  9.8473  10.1030  3.5885  2033758
W_L5  + -L 5.0     .80882  .84122  .79934  .72111  .71181  .58088  .24521  .03151  .06170  .05644  9.8543  10.1256  3.5994  2033710
LST(base)          .80987  .84285  .77719  .64422  .62567  .51042  .22906  .05402  .05138  .04538 10.1498  10.0094  3.5567  1998494
per region 977   effB     effT     effE  | dupB    dupT    dupE  | fakB    fakT    fakE
W_X4           .92454   .87981   .74436  |.04294  .03375  .08069 |.06754  .06932  .04578
W_R4           .92447   .87935   .74443  |.04291  .03366  .08067 |.06758  .06997  .04578
W_EW50         .92430   .87875   .74429  |.04289  .03350  .08065 |.06780  .07085  .04581
W_L5           .92430   .87905   .74436  |.04303  .03278  .08066 |.06783  .07091  .04582
LST            .92430   .88004   .74658  |.00971  .01308  .08486 |.04365  .04542  .04630
```
THE LENGTH GAIN TRANSFERS EXACTLY (nhB/nhT/nhE within .002 of the 300-evt deltas). THE
EFFICIENCY COST IS ABOUT DOUBLE THE 300-evt READING (-EXR 4: .00000 -> .00009;
-EXW.5 -EXR4: .00017 -> .00032; + -L 5: .00004 -> .00023). Quote the 977 number.
W_L5 DOMINATES W_EW50 on the full sample as well: better efficiency (.80882 vs .80873),
better duplicate rate (.06170 vs .06177) and MORE length in all three regions, for +3e-5
of fake. -L 5.0 is confirmed as a net-positive rider, not a trade.
On 977 the recommendation puts nhT 10.1256 ABOVE LST's 10.0094 and nhE 3.5994 ABOVE LST's
3.5567, leaves barrel efficiency exactly at LST (.92430 = .92430), and costs .00023 of
overall efficiency and .00037 of fake. Duplicate rate IMPROVES.

## M14 -- FINAL RECOMMENDATION
`-EXW 0.50 -EXR 4.0 -L 5.0` appended to the assembled baseline. Three constants, one of
which (-EXW 0.50) is the SHIPPED DEFAULT the frozen line had tightened. Fallbacks in
increasing conservatism: drop -L 5.0 (pure extension-pass, composes with anything);
drop to `-EXR 4.0` alone (one constant, 977 cost .00009).
REJECTED AND WHY: the inner extension arm (-EX 2/3) at any guard setting -- it is the only
thing measured this round that costs DISPLACED efficiency (-11.5 d15 tracks, -16 v1030
tracks per 300 evts) and displaced is protected. The trim levers (-TR 0, -TL 6, -TA 3.0) --
dominated by the extension on length-per-efficiency and they cost duplicate rate too.
-EXS 0, -EXJ 2 alone, -EXN 2 alone, -EXF 0.8, -EXR > 4 -- measured dead ends.

## M15 -- DONE. Full scoreboard: a06_ref/a06_final_scoreboard.txt (35 configurations,
   300-evt headline + displaced bands, 300-evt per-region, 977-evt both).
Binary md5 519b0abc34a28cd1e803d6b9407ef224 == protoFIN's; `diff -rq protoA06 protoFIN`
shows no differing source file. EVERY result this round is a pure FLAG delta, zero code.
Reproduce with: bash a06_ref/a06_run.sh <TAG> <overrides>   (the assembled baseline tail is
inside the script, so a bare `a06_run.sh GATE` IS the baseline).
RECOMMENDED LINE = assembled baseline + `-EXW 0.50 -EXR 4.0 -L 5.0`  (tag H_W50L5 / W_L5).
No background process of mine is left running.
