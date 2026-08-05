# A08 -- DISPLACED AUDIT + PROTECTION -- STATUS

Agent role: A08-displaced-audit (one of fifteen explorers off the ASSEMBLED BASELINE).
Code: `standalone/protoA08/` (byte-identical copy of protoFIN, NO source edits).
Artifacts: `standalone/a08_ref/`. Runner: `a08_ref/a08_run.sh <TAG> <overrides>`.

Baseline overrides: `-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2` (+ `-AT3 6 -XCD 2`, both
proven inert by the Baseline agent's FINBASE_ND gate).

## M0 -- SETUP (DONE)
protoA08 = `cp -a protoFIN protoA08`; binary md5 519b0abc34a28cd1e803d6b9407ef224 ==
protoFIN's. No rebuild, no source edits made or planned. Tools written (analysis only,
they touch no physics): `a08_bands.py` (band audit WITH denominators + binomial sigma),
`a08_simdiff.py` (track-level LOST/GAINED profiling against the LST ntuple).

## M1 -- NO-OP GATE: PASSED EXACTLY
Tag `A08GATE` = `-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2 -AT3 6 -XCD 2` on protoA08
reproduces fin_ref/r_FINBASE on EVERY metric (eff .80992 / dup .06230 / fake .05551 /
nTC 618793 / all displaced bands / all per-region cells) and **33/33 branches IDENTICAL**
under `rebase_ref/cmp_branches.py`. `a08_ref/gate_check.txt`.

## M2 -- THE AUDIT (done, needed no new runs)

### (a) Denominators, and which displaced bands are REAL
`a08_bands.py` on the round's own artifacts. 300 evts (bands_FINBASE_300.txt) and the
full 977 (bands_W_X4_977.txt). dTracks = numerator difference vs LST; the sims are the
same in both files so this is a PAIRED count, and sigComb (uncorrelated binomial) is a
conservative over-estimate of its noise.

```
band            300evt: rate  num/den   dTracks  nsig  |  977evt: rate  num/den   dTracks  nsig
vxy[1,5)         .79459  1087/1368       +31   +1.44  |   .80110  3653/4560      +109   +2.80
vxy[5,10)        .73356   435/593        +47   +2.97  |   .72161  1436/1990      +154   +5.26
vxy[10,30)       .74139   926/1249       +96   +4.22  |   .71474  2929/4098      +365   +8.61
dxy[0,1)         .83727 19686/23512     +154   +1.91  |   .83518 64162/76824     +432   +2.95
dxy[1,5)         .61650   553/897        +56   +2.69  |   .58320  1763/3023      +220   +5.70
dxy[5,10)        .20161    50/248         -2   -0.22  |   .24521   243/991        +16   +0.85
dxy[10,30)       .03073    13/423        -11   -1.85  |   .03151    49/1555       -35   -3.11
```

CORRECTION TO THE ROUND'S CAVEAT. The round groups d510 and d1030 together as
"small denominators, unmoved all round -- do not tune". Only half of that is right:
* **d510 IS noise.** It is -2 tracks on the 300 and **+16 tracks on the 977** -- it
  changes SIGN between samples. Never quote its 300-evt deficit again.
* **d1030 IS NOT noise.** -11 tracks on the 300 becomes **-35 tracks / -3.1 sigma on the
  977** with denominator 1555. It is a real, reproducible deficit (we find 49 where LST
  finds 84). It is also the ONLY displaced band where LST beats us on the full sample.

### (b) Every other displaced band is a multi-sigma WIN on the full sample
v510 +5.3 sigma, v1030 +8.6 sigma, d15 +5.7 sigma, v15 +2.8 sigma, and dxy[0,1) +3.0
sigma. The headline displaced advantage is not a small-denominator artifact.

### (c) DISPLACED INVARIANCE: the whole pixel/dedup config space moves ~0 displaced tracks
Harvested every 300-evt sweep point on disk from this round and the two before it:
66 in fin_ref (`sens_map_300.txt`) + 39 in xc_ref + 6 in rebase_ref
(`sens_map_prior.txt`) = **111 configurations**, spanning -AT3 5..11, -XCT -6..6.875,
-XC 0/1/2/3, -XCG, -XCT2/-XCT3 eta bins, -CC 0/1, -CCN 1/2/3, -CCG, -CCR 1/2, -T3E 0/1,
-ZP8 3/4/5/6 -- an overall-efficiency swing of 5.7 percentage points (.75328 .. .81333):
```
  vxy[10,30)  0.74139 in ALL 111 runs     (926/1249) -- one prior-lineage point, RBBASE
              (pre-deletion universe), gives 927. ONE track in 111 configurations.
  dxy[1,5)    0.61650 in ALL 111 runs     (553/897)  -- RBBASE 555. TWO tracks.
  dxy[5,10)   0.20161 in ALL 111 runs     (50/248)   -- ZERO variance, no exceptions.
  dxy[10,30)  0.03073 in ALL 111 runs     (13/423)   -- ZERO variance, no exceptions.
  vxy[5,10)   .72513/.72681/.73019/.73187/.73356/.73524 = 430..436 of 593, a 6-track span
  vxy[1,5)    0.74561 .. 0.80044          = 1020..1095 of 1368 -- the ONLY responsive band
```
So: the pT3 delivery stage, the OT hit-overlap contention and the ported CrossCleanpLS
are **displaced-inert**. The only displaced band with real sensitivity to the pixel layer
is vxy[1,5) -- which is inside the pixel volume and tracks overall efficiency. Confirmed
directly by the ablation table (bands_ablations_300.txt): FINBASE / GATE(-T3E 0) /
-XC 0 / -CC 0 give v1030 926/926/926/926, d15 553/553/553/553, d510 50/50/50/50,
d1030 13/13/13/13, v510 435/434/435/436.

WHY: the sims in those bands are matched by objects that carry NO PIXEL SEED (see (d)) --
bare chain TCs. The pixel/seed layer physically cannot reach them.
**Consequence for synthesis: nothing any explorer does in the pixel / delivery / dedup
layer can damage the displaced headline. Displaced lives entirely in the CHAIN layer.**

TRACK-LEVEL PROOF, not just equal rates (`simdiff_stage_neutrality_300.txt`, same sims,
LOST / GAINED between two configurations):
```
  pT3 stage OFF -> ON  (-T3E 0 -> 1):  vxy[10,30) 0/0 | vxy[5,10) 0/1 | vxy[30,60) 0/0 | dxy[1,30) 0/0
  crossclean OFF -> ON (-XC 0 -> 3):   vxy[10,30) 0/0 | vxy[5,10) 0/0 | vxy[30,60) 0/0 | dxy[1,30) 0/0
```
Not one displaced track changes identity when either mechanism is switched on. The equal
rates in the ablation table are not a coincidence of cancelling moves -- literally the
same sims are matched before and after.

### (d) What the wins are made of (protect THIS), track level
`a08_simdiff.py` reproduces the createPerfNumDenHists denominator EXACTLY (performance.cc
:1208: |eta|<4.5, pt>0.9, |vtx_z|<30, charge!=0; NO vxy or dxy cut on these two plots) --
verified: it returns 24/423 for LST and 13/423 for us in d1030, 830/1249 and 926/1249 in
v1030, matching the histograms track for track. 300 evts (`simdiff_exactsel_300.txt`):
* vxy[10,30): LOST 38 / GAINED 134, net **+96**. Our gains are **T5-class chains (84%,
  nhitOT 10/12) and T4-class (16%, nhitOT 8)** -- bare OT chains, 83% barrel. Zero of the
  134 is a pixel object. LST's own matches for the 38 we lose are also T5 (84%) / T4 (16%).
* vxy[5,10): net +47; vxy[1,5): net +31; dxy[1,5): net +56 -- same character.
The displaced advantage IS the bare large-DCA chain branch. The thresholds that govern it
are `-MR / -MRI / -U5 / -U6 / -C25 / -C25D / -M4 / -M4D / -ZM4D` (the -G 6 kill tree in
main.cc:2791-2814), NOT anything pixel-side.

### (e) What the d1030 deficit is made of
`simdiff_exactsel_300.txt`, exact selection: LOST 12 / GAINED 1 (977: 105/27 on the
looser cut). LST finds the 12 as **T4 with nhitOT 8 (75%)** and T5 with nhitOT 10 (25%) --
pure OT objects, no pixel. Profile: **92% BARREL** (median |eta| 0.34), pt median 1.12 GeV
(min 0.90), production radius **vxy 16.2 .. 52.3 cm, median 35.8**, i.e. outside the pixel
detector entirely, and 8 of 12 are PROTONS (nuclear-interaction secondaries in tracker
material). No pixel-side mechanism can ever recover them -- which is exactly why the band
is frozen at 13/423 in all 111 configurations. It is a chain-ADMISSION question in the
short (4-layer) large-DCA branch, governed by `-M4D` (`-ZM4D` in the transition).
CEILING: recovering ALL of them is worth +.0284 in d1030 (to .0591, above LST's .0567)
and +.00053 of OVERALL efficiency. Small in absolute terms; it is a headline-band item.
CONFIRMED ON THE FULL 977 with the exact selection (`a08_977pass.txt`): **LOST 43 /
GAINED 8, net -35.** LST matched-TC type T4=36 (84%, nhitOT 8) / T5=7 (nhitOT 10);
**42 of 43 are BARREL**; pt median 1.08 GeV (min 0.90, max 2.29); vxy 16.2 .. 52.9,
median 35.8; pdgId 19 protons / 16 pions / 7 electrons. One sentence: the deficit is
LOW-pt BARREL SECONDARIES BORN AT 16-53 cm THAT LST RECONSTRUCTS AS 4-LAYER, 8-HIT T4s.

### (f) BAND x REGION MATRIX, and a displaced win nobody has reported
`matrix_FINBASE_300.txt` (proto | LST | dTracks (denom), exact selection):
```
vxy band        barrel                    transition             endcap                 ALL
[0,1)     .9271|.9262  +8 (8428)   .8836|.8833  +1 (3779)  .7448|.7462 -12 (8826)  -3 (21033)
[1,5)     .8300|.8114 +12 (647)    .7850|.7557  +9 (307)   .7464|.7222 +10 (414)  +31 (1368)
[5,10)    .7656|.7289 +10 (273)    .6857|.5714 +20 (175)   .7310|.6138 +17 (145)  +47 (593)
[10,30)   .7418|.6588 +84 (1011)   .7634|.7054 +13 (224)   .3571|.4286  -1 (14)   +96 (1249)
[30,inf)  .0794|.0632 +26 (1599)            -                        -            +26 (1599)
dxy band
[0,1)     .9115|.9013 +100 (9776)  .8689|.8593 +42 (4370)  .7450|.7438 +12 (9366)+154 (23512)
[1,5)     .6102|.5472 +48 (762)    .6923|.6346  +6 (104)   .5161|.4516  +2 (31)   +56 (897)
[5,10)    .2017|.1933  +2 (238)    .2500|.7500  -4 (8)     .0000|.0000  +0 (2)     -2 (248)
[10,30)   .0310|.0548 -10 (420)    .0000|.3333  -1 (3)               -            -11 (423)
```
* **`vxy[30,inf)` is a real displaced band nobody in this round has reported** (it is the
  vxy histogram's OVERFLOW, so compare_ab.py and the round scoreboard silently drop it).
  Denominator 1599 on the frozen 300 -- larger than v1030's 1249 -- and we beat LST there
  **+26 tracks, .0794 vs .0632 (+26% relative)**. Add it to the displaced scoreboard.
* The whole d1030 deficit is BARREL (420 of its 423 sims are barrel). The 4 "lost" tracks
  in the dxy[5,10) TRANSITION cell sit on a denominator of **8**, and the d1030 transition
  cell on a denominator of **3** -- cell-level noise, exactly as the standing caveat says.
* Our one prompt regional deficit is vxy[0,1) ENDCAP (-12 tracks), consistent with the
  round's known endcap attach-head recall defect.

SAME MATRIX ON THE FULL 977 (`matrix_W_X4_977.txt`) -- these are the numbers to quote:
```
vxy band        barrel                     transition              endcap                ALL
[0,1)   .9252|.9257  -14 (27364)  .8812|.8825 -16 (12600) .7438|.7461 -67 (28615)  -97 (68579)
[1,5)   .8541|.8234  +63 (2056)   .7809|.7507 +33 (1091)  .7396|.7304 +13 (1413)  +109 (4560)
[5,10)  .7472|.6721  +73 (973)    .7022|.6227 +47 (591)   .6901|.6103 +34 (426)   +154 (1990)
[10,30) .7188|.6223 +324 (3360)   .7054|.6459 +42 (706)   .5000|.5312  -1 (32)    +365 (4098)
[30,inf).0864|.0685 +102 (5719)             -                       -            +102 (5721)
dxy band
[0,1)   .9081|.8968 +360 (31878)  .8667|.8599 +98 (14572) .7436|.7444 -26 (30374) +432 (76824)
[1,5)   .5845|.5047 +202 (2532)   .5995|.5654 +13 (382)   .4954|.4495  +5 (109)   +220 (3023)
[5,10)  .2437|.2228  +20 (956)    .3125|.4375 -4 (32)     .0000|.0000  +0 (3)      +16 (991)
[10,30) .0316|.0535  -34 (1552)   .0000|.3333 -1 (3)                -              -35 (1555)
```
* **vxy[30,inf) on the 977: +102 tracks, .0863 vs LST .0685, denominator 5721 -- the
  LARGEST displaced denominator of any band (v1030 is only 4098) and a +3.6 sigma win.**
  It is dropped by the round scoreboard purely because it is histogram overflow.
  BREAKDOWN of that tail on the FULL 977 (`a08_977pass.txt`): the win lives entirely in
  vxy [30,60), and BOTH algorithms reconstruct exactly ZERO sims above 60 cm (4176 of the
  band's 5721 sims), so the raw [30,inf) rate is diluted by an unreconstructable tail.
```
  vxy[30,40)  .46988 | .41807   +43 on 830    +2.13 sigma
  vxy[40,50)  .29114 | .11392   +28 on 158    +4.02 sigma
  vxy[50,60)  .10413 | .04847   +31 on 557    +3.52 sigma
  vxy[60,inf) .00000 | .00000    +0 on 4176    (neither algorithm reconstructs anything)
  vxy[30,60)  .31974 | .25372  +102 on 1545   +4.07 sigma   <- QUOTE THIS ONE
```
  On the frozen 300 the same slice is .3098 vs .2463, +26 on 410, +2.0 sigma.
* Every displaced win is barrel+transition+endcap consistent; the only negative displaced
  cells are d1030 (barrel, -34) and the 3- and 32-sim transition cells (noise).
* The prompt endcap deficit is -67 tracks in vxy[0,1) / -26 in dxy[0,1): that, not
  anything displaced, is where the endcap attach-head recall defect shows up.

### (g) THE BRIEF'S CLASS-B QUESTION, ANSWERED WITH TRACKS
"Are seeds of DISPLACED sims in class B being retired by the crossclean?" The XC arm
retires 5.8 class-B seeds/evt. Splitting the -XC 0 -> -XC 3 efficiency cost by band
(300 evts, numerator counts, `bands_ablations_300.txt`):
```
                overall  vxy[0,1)  vxy[1,5)  vxy[5,10)  vxy[10,30)  dxy[1,5)  dxy[5,10)  dxy[10,30)
-XC 0 (NOXC)      18408     17801      1091        435         926       553         50          13
-XC 3 (FINBASE)   18331     17727      1087        435         926       553         50          13
cost of the port    -77       -74        -4          0           0         0          0           0
```
**96% of the crossclean's efficiency cost is PROMPT (vxy < 1 cm); it costs 4 tracks in
vxy[1,5) and ZERO beyond 5 cm.** No displaced-aware exemption is warranted -- there is
nothing there to exempt. The same split for the pT3 stage (GATE -T3E 0 -> FINBASE, +176
tracks): +174 prompt, +9 in vxy[1,5), +1 in vxy[5,10), 0 everywhere else. The stage's
gain is ~99% prompt and it costs nothing displaced.

### (i) DUPLICATE RATE PER DISPLACEMENT BAND -- OUR DUP DEFICIT IS 100% PROMPT
`displaced_dup_300.txt`. Denominator = TCs matched (>0.75) to a sim, binned by that sim's
displacement (NOT the scoreboard's all-TC eta-binned convention, but identical on both
sides so the comparison is fair). 300 evts:
```
band          protoTC  protoDupRate |  lstTC  lstDupRate |  delta
vxy[0,1)        19123      0.14585  |  18137     0.04472 |  +0.10113
vxy[1,5)         1200      0.18833  |   1084     0.04982 |  +0.13852
vxy[5,10)         437      0.00915  |    391     0.01535 |  -0.00619
vxy[10,30)        927      0.00216  |    854     0.05621 |  -0.05405
vxy[30,60)        127      0.00000  |    105     0.07619 |  -0.07619
dxy[1,5)          553      0.00000  |    511     0.05479 |  -0.05479
dxy[5,10)          50      0.00000  |     54     0.07407 |  -0.07407
dxy[10,30)         13      0.00000  |     24     0.00000 |  +0.00000
```
**From vxy 5 cm outward our displaced tracks are essentially DUPLICATE-FREE (0.0 - 0.9%)
while LST duplicates them at 1.5 - 7.6%.** The round's +.0105 overall duplicate deficit is
entirely a PROMPT phenomenon (vxy < 5 cm), where the denominator lives. So the displaced
story is not only "much better efficiency" -- it is much better efficiency AT A LOWER
DUPLICATE RATE, on every displaced band with a usable denominator. Nobody has reported
this; it belongs in the headline alongside the efficiency numbers.

SAME ON THE FULL 977 (`displaced_dup_977.txt`) -- the numbers to quote:
```
band          protoDupRate | lstDupRate | delta      (proto matched TCs)
vxy[0,1)           0.14005 |    0.04228 | +0.09777   62058
vxy[1,5)           0.18028 |    0.04305 | +0.13723    4016
vxy[5,10)          0.00970 |    0.01701 | -0.00731    1443
vxy[10,30)         0.00884 |    0.05019 | -0.04135    2942
vxy[30,60)         0.00000 |    0.03015 | -0.03015     494
dxy[1,5)           0.00678 |    0.03814 | -0.03136    1769
dxy[5,10)          0.00000 |    0.05983 | -0.05983     243
```

AND IT IS INTRINSIC TO THE CHAIN LAYER, NOT TO THE DEDUP MECHANISMS
(`displaced_dup_ablations_300.txt`, dup rate among matched TCs per vxy band):
```
tag           vxy[0,1)      vxy[1,5)     vxy[5,10)    vxy[10,30)   vxy[30,60)
FINBASE     .14585 2789   .18833 226   .00915 4/437  .00216 2/927  .00000 0/127
NOXC  -XC 0 .31647 6695   .34446 454   .01822 8/439  .00216 2/927  .00000 0/127
NOCC  -CC 0 .45338 10470  .50681 744   .12446 58/466 .00431 4/928  .00000 0/127
GATE -T3E 0 .18588 3598   .23918 293   .02278 10/439 .00216 2/927  .00000 0/127
LST         .04472  811   .04982  54   .01535 6/391  .05621 48/854 .07619 8/105
```
With BOTH dedup mechanisms switched OFF our vxy[10,30) duplicate rate is still 0.4% and
vxy[30,60) is still 0.0%, against LST's 5.6% and 7.6%. -CC and -XC do all their work in
the prompt region (and in vxy[1,5)/[5,10), which are still inside the pixel volume).
This closes the protection argument on the SECOND priority axis as well: no pixel-layer
knob can damage displaced efficiency OR displaced duplicate rate.

### (h) TRACK LENGTH IN THE DISPLACED BANDS (the "length not regressed" requirement)
`displaced_length_300.txt` -- mean nhitOT of the MATCHED TC, matched sims only:
```
band          protoLen  lstLen    dLen   proto matched-TC types        LST matched-TC types
vxy[0,1)         7.916   8.260  -0.345   pT5 40 T5 28 pLS 27 pT3 5     pT5 65 pLS 25 pT3 7 T5 3
vxy[1,5)         9.323   9.636  -0.313   T5 64 pT5 17 pLS 15 pT3 3     pT5 50 T5 27 pLS 12 pT3 9
vxy[5,10)       10.892  11.258  -0.366   T5 92 pT5 4 T4 3 pLS 1        T5 81 pT5 13 pT3 5 T4 1
vxy[10,30)      10.739  11.275  -0.536   T5 96 T4 4                    T5 99 T4 1
vxy[30,inf)      9.465   9.743  -0.278   T5 68 T4 32                   T5 80 T4 20
dxy[1,5)        10.358  11.095  -0.737   T5 92 T4 8                    T5 98 T4 2
dxy[5,10)        9.880  10.192  -0.312   T5 80 T4 20                   T5 83 T4 17
dxy[10,30)       8.923   9.167  -0.244   T4 54 T5 46                   T5 58 T4 42
```
The displaced length deficit (-0.24 .. -0.74) is the SAME magnitude as the overall one
(-0.35 barrel / -0.14 transition / -0.003 endcap): there is no displaced-specific length
regression, and we win those bands on efficiency while carrying tracks of essentially the
same length. Composition confirms the mechanism: from vxy 5 cm outward BOTH algorithms
are >= 80% T5-class, i.e. pure OT objects -- which is the physical reason the pixel layer
cannot reach these bands.

### (j) WHY THE d1030 RECOVERY IS EXPENSIVE -- LST's T4 CLASS IS 60% FAKE
`fake_by_type_300.txt`, TC population and fakes by object type (300 evts; tc_isFake, so
the rate is over ALL TCs of that type, slightly different from the pt/eta-cut scoreboard
convention -- .05389 here vs .05551 there -- but identical on both sides):
```
type      FINBASE nTC   nFake  fakeRate  f/evt  |   LST nTC   nFake  fakeRate  f/evt
pLS           267058   12140   0.04546   40.5   |   268362   12308   0.04586   41.0
pT5           172779    1369   0.00792    4.6   |   241043    3741   0.01552   12.5
T5-class      128072   11197   0.08743   37.3   |    41826    2661   0.06362    8.9
pT3-class      40623    5633   0.13867   18.8   |    45445    1581   0.03479    5.3
T4-class       10261    3006   0.29295   10.0   |    11514    6902   0.59944   23.0
ALL           618793   33345   0.05389  111.2   |   608190   27193   0.04471   90.6
```
TWO THINGS FALL OUT, both new:
1. **LST's OWN T4 class is 60% fake** and costs it 23 fakes/evt; ours is 29% and costs 10.
   The d1030 tracks we lose are LST T4s (84% of them, nhitOT 8). Recovering them by
   loosening the 4-layer large-DCA admission means buying into a class LST itself runs at
   60% fake purity -- for a band worth 35 tracks on the whole 977. That is the honest
   price of the only real displaced deficit, and it is why I expect the -M4D probes to
   fail the priority test rather than pass it.
2. **Our fake deficit is NOT in the short/displaced class at all.** Per event vs LST:
   T5-class +28.4, pT3-class +13.5, T4-class **-13.0**, pT5 -7.9, pLS -0.5 (net +20.6).
   The round's headline fake deficit lives in the LONG chain class and the pT3-class rows.
   Anyone attacking fake should go there; tightening the SHORT displaced branch would cost
   displaced efficiency and buy almost nothing, because that branch is already cleaner
   than LST's.

CONFIRMED ON THE FULL 977 (`fake_by_type_977.txt`), fakes/evt by type:
```
type        W_X4 nTC   fakeRate  f/evt  |   LST nTC   fakeRate  f/evt  |  delta f/evt
pLS           877078    0.04598   41.3  |    881388    0.04631   41.8  |   -0.5
pT5           568839    0.00776    4.5  |    792588    0.01513   12.3  |   -7.8
T5-class      420846    0.08872   38.2  |    137314    0.06490    9.1  |  +29.1
pT3-class     133407    0.14206   19.4  |    149024    0.03540    5.4  |  +14.0
T4-class       33698    0.29482   10.2  |     38180    0.60956   23.8  |  -13.6
ALL          2033868    0.05456  113.6  |   1998494    0.04517   92.4  |  +21.2
```

LOCALIZED FURTHER (`chain_class_300.txt`), bare OT objects (type 4/9) only, per event:
```
FINBASE  barrel nhitOT=10  96.5 TC  15.8 fakes  .1633    LST  barrel nhitOT=8  16.3 TC 13.1 fakes .8075
         trans  nhitOT=10  86.5 TC  11.0 fakes  .1276         endcap nhitOT=8  15.6 TC  5.5 fakes .3517
         barrel nhitOT=8   12.1 TC   7.7 fakes  .6421         barrel nhitOT=10 23.5 TC  4.6 fakes .1961
         barrel nhitOT=12 108.9 TC   3.6 fakes  .0331         trans  nhitOT=8   6.5 TC  4.4 fakes .6740
```
The two algorithms make their bare-OT fakes in COMPLETELY DIFFERENT PLACES: LST's live in
the 4-layer (8-hit) class -- 81% fake in the barrel -- while ours live in the 5-layer
(10-hit) barrel and transition classes (26.8 fakes/evt combined at 13-16% purity cost).
Our 8-hit barrel class is 12.1 TC/evt against LST's 16.3. Any displaced-protection
constraint should therefore be traded against the 10-hit barrel/transition slice, never
against the 8-hit one -- the 8-hit slice is where the d1030 tracks live and where we are
already ahead of LST on cleanliness.

## M3 -- GAIN PROBES: RESULT (300 evts; `probe_batch1.txt`)
```
tag           eff      vxy01    v15      v510     v1030    d15      d510     d1030    dup      fake     nhB     nhE     nTC
A08GATE     0.80992  0.84282  0.79459  0.73356  0.74139  0.61650  0.20161  0.03073  0.06230  0.05551  9.803  3.5596  618793
P_M4D25     0.80961  0.84249  0.79825  0.74199  0.75500  0.63880  0.22177  0.03310  0.06359  0.06331  9.754  3.5725  626266
P_M4DOFF    0.80634  0.83901  0.79971  0.75211  0.76781  0.65552  0.24194  0.03310  0.06148  0.13828  9.425  3.6159  687404
P_ZM4D0     0.80992  0.84282  0.79459  0.73524  0.74139  0.61761  0.20161  0.03073  0.06281  0.05639  9.802  3.5595  619911
P_MR25      0.80979  0.84268  0.79386  0.73524  0.74219  0.62096  0.20161  0.03073  0.06223  0.05737  9.806  3.5618  619895
LST         0.80988  0.84296  0.77193  0.65430  0.66453  0.55407  0.20968  0.05674  0.05179  0.04476 10.148  3.5625  608190
```
per region: P_M4D25 effB .92614 effT .88213 effE .74463 | dupB .04370 dupT .03857 dupE
.08233 | fakB .08595 fakT .07505 fakE .04700. P_M4DOFF fakB .23063 -- rejected outright.

**`-M4D -2.5` IS A REAL DISPLACED LEVER -- THE FIRST ONE FOUND IN THIS ROUND.** In tracks
(300 evts, exact selection): v15 +5, v510 +5, **v1030 +17**, **d15 +20**, d510 +5,
d1030 +1 -- SIX of six displaced bands positive, ~52 displaced tracks. With it, EVERY
displaced band beats LST including d510 (.22177 vs .20968); only d1030 stays behind
(.03310 vs .05674).

WHAT IT COSTS: overall in-cut efficiency -.00031 (-7 sims: noise), duplicate rate +.00129,
and **fake rate +.00780** (.05551 -> .06331), which would take the round's worst deficit
from +.0108 to +.0186 against LST. NOTE the two numbers are not in tension: the headline
efficiency denominator requires |vtx_perp| < 2.5 cm (performance.cc:1186), so nearly all
of the displaced gain is INVISIBLE to it by construction.

THE GAIN IS STRICTLY ADDITIVE, NOT A RESHUFFLE (`simdiff_m4d25_vs_base_300.txt`,
baseline vs -M4D -2.5, same sims, exact selection):
```
  vxy[10,30):  LOST 0   GAINED 17   net +17   (T4-class 88%, 100% barrel, pt med 1.30)
  dxy[1,5)  :  LOST 0   GAINED 20   net +20   (T4-class 90%,  95% barrel, pt med 1.15)
```
ZERO tracks are lost in either band. That matters for credibility at 300 events: a
strictly one-sided move of 17 and 20 tracks is not a statistical reshuffle, and the two
bands agree on what the added objects are (short, low-pt, barrel, 4-layer).

MECHANISM, verified (`m4d_mechanism_300.txt`) -- the lever is surgical:
```
             T4-class TC/evt  fakes/evt  fakeRate   T5-class TC/evt  fakes/evt
A08GATE            34.2         10.0      0.2930        426.9         37.3
P_M4D25            61.6         28.3      0.4593        426.6         37.1
P_M4DOFF          279.1        230.5      0.8259        426.6         37.3
LST                38.4         23.0      0.5994        139.4          8.9
```
The T5-class population is untouched to 0.3 TC/evt, so the lever cannot disturb the
machinery the displaced wins actually come from. The marginal purity of the step to -2.5
is 27.4 new T4-class TCs/evt of which 18.3 are fakes (67% fake); the step from there to
"off" is 93% fake. Strongly convex -- the first step is by far the best, everything past
it is worthless. At -2.5 we would run MORE T4s than LST (61.6 vs 38.4) but CLEANER ones
(46% vs 60% fake).

FULL BAND x REGION PICTURE AT `-M4D -2.5` vs LST (`matrix_P_M4D25_300.txt`, 300 evts, in
tracks): v15 +36, v510 +52, v1030 +113, vxy[30,inf) +36, d15 +76, **d510 +3 (goes POSITIVE
against LST for the first time)**, d1030 -10. Prompt side: vxy[0,1) -10 (was -3), endcap
-15 (was -12). So the lever makes EVERY displaced band a win except d1030.

AND IT DOES NOT DAMAGE THE OTHER DISPLACED AXES (`m4d25_dup_length_300.txt`):
displaced duplicate rate vxy[10,30) .00212 (was .00216, LST .05621), vxy[30,60) still
0.00000 (LST .07619); displaced matched-track length v1030 10.689 (was 10.738, LST 11.283)
and vxy[30,60) 9.358 (was 9.465, LST 9.810). Only the FAKE rate pays.

THE OTHER THREE PROBES ARE NOT WORTH A KNOB:
* `-ZM4D 0` (drop the transition-band displaced-T4 tightening): +1 track in v510, +1 in
  d15, and fakT .06903 -> .07433. The transition 8-hit population is only 3.8 TC/evt, so
  there is nothing there. Leave -ZM4D at 1.2.
* `-MR -2.5` (loosen the exempt-5+ rescue): +1 v510, +1 v1030, +4 d15, for +.00186 fake.
  A worse exchange than -M4D in every band. It also confirms the PROTECTION direction:
  TIGHTENING -MR would cost displaced tracks -- do not let a fake-focused change go there.
* `-M4D -1e9`: fake .13828, fakB .23063. Dead.

BRACKET RUNNING: `-M4D -1.6` and `-M4D -2.0` to find whether a cheaper fraction of the
gain exists (the step is convex, so it may).

## M3b -- PROTECTION-PRICE PROBES: THE TRAP THIS ANGLE EXISTS TO FLAG
`probe_protection.txt`, 300 evts:
```
tag          eff      vxy01    v15      v510     v1030    d15      d510     d1030    dup      fake
A08GATE    0.80992  0.84282  0.79459  0.73356  0.74139  0.61650  0.20161  0.03073  0.06230  0.05551
T_MR10     0.81028  0.84329  0.79313  0.73019  0.73579  0.61315  0.19758  0.02837  0.06232  0.05193
T_MRI05    0.80961  0.84249  0.79386  0.73019  0.73899  0.61650  0.20161  0.03073  0.06209  0.05478
LST        0.80988  0.84296  0.77193  0.65430  0.66453  0.55407  0.20968  0.05674  0.05179  0.04476
```
**`-MR -1.0` LOOKS LIKE A PURE WIN AND IS NOT.** On the headline scoreboard it reads
+.00036 efficiency, **-.00358 fake** (fakB .0668 -> .0598, fakT .0690 -> .0617), duplicate
rate flat, and barrel/transition efficiency both UP. Anyone hunting the round's fake
deficit will find it and propose it. In tracks it quietly costs **SIX of six displaced
bands**: v15 -2, v510 -2, v1030 -7, d15 -3, d510 -1, d1030 -1 (about -16 displaced tracks
for +8 in-cut ones). That is the single most important protection statement I can hand
the synthesis: **-MR is a displaced knob wearing a fake knob's clothes, and the headline
metrics cannot see the damage because the efficiency denominator caps vtx_perp at 2.5 cm.**
`-MRI 0.5` is the milder version of the same thing: -.00073 fake for v510 -2, v1030 -1.

AND THE DAMAGE IS STRICTLY ONE-SIDED (`simdiff_mr10_vs_base_300.txt`, baseline vs
`-MR -1.0`, same sims): vxy[10,30) LOST 7 GAINED **0**; vxy[5,10) LOST 2 GAINED **0**;
dxy[1,30) LOST 5 GAINED **0**. **Every single lost track is a T5-class chain (100% in all
three bands)** -- i.e. exactly the long large-DCA objects section (d) identified as the
mechanism of the displaced advantage. There is no reshuffling and no compensating gain:
the knob simply deletes displaced tracks.

MECHANISM (`mr10_mechanism_300.txt`): `-MR -1.0` removes 9.5 T5-class chain TCs/evt, of
which 7.5 are fakes and 2.0 are TRUE (barrel nhitOT=10 goes 96.5 -> 91.9 TC/evt, fakes
15.8 -> 11.9; the T4-class, pT5 and pT3-class populations are untouched). Over 300 events
that is ~600 true chain TCs deleted -- and yet overall in-cut efficiency goes UP by 8
sims, because almost every deleted true chain was a duplicate of a pixel-seeded object.
The ONLY sims that lose coverage outright are the displaced ones.

### THE UNIFYING PRINCIPLE OF THIS WHOLE AUDIT
A prompt sim is covered by SEVERAL objects at once (pT5 + pT3-class + bare pLS + chain --
see the composition table in (h): at vxy<1 our matched types are pT5 40% / T5 28% /
pLS 27%). A displaced sim is covered by ONE (at vxy>5 both algorithms are >= 80% bare
T5-class, and beyond vxy 10 cm there is no pixel object at all). So:
* pixel-layer changes cannot touch displaced, because there is no pixel object there --
  hence the 111-configuration invariance in (c);
* chain-layer changes hit displaced FIRST and HARDEST, because for displaced sims the
  chain is the only cover, while for prompt sims deleting a chain merely deletes a
  duplicate -- hence -MR -1.0 reading as a headline win while deleting displaced tracks.
Any future proposal should be classified by this test before it is scored.

MEASURED, not asserted (`cover_multiplicity_300.txt`, 300 evts; a "cover" is any TC with a
>0.75 match to the sim):
```
band          FINBASE meanCovers  fracWithPixelCover  |  LST meanCovers  fracWithPixelCover
vxy[0,1)              1.079              0.7614       |      1.023            0.9735
vxy[1,5)              1.104              0.4177       |      1.027            0.7254
vxy[5,10)             1.005              0.0552       |      1.008            0.1830
vxy[10,30)            1.001              0.0000       |      1.029            0.0036
vxy[30,60)            1.000              0.0000       |      1.040            0.0000
```
Same on the FULL 977 (`cover_multiplicity_977.txt`): fracWithPixelCover W_X4 / LST =
.7607/.9735 (vxy<1), .4281/.7255 ([1,5)), .0522/.1685 ([5,10)), **.0014/.0039 ([10,30)),
.0000/.0000 ([30,60))**; meanCovers 1.004/1.026 and 1.000/1.015 in the two outer bands.

**Beyond vxy 10 cm, essentially ZERO matched sims have a pixel-seeded cover in EITHER
algorithm** (4 of 2929 for us, 10 of 2564 for LST on the 977), and the mean number of
covers is 1.00 -- a far-displaced
track is held by exactly one object and has no redundancy at all. That is simultaneously
(i) the mechanical proof of the 111-configuration invariance in (c) -- the pixel layer has
nothing there to move -- and (ii) the reason a chain-layer tightening deletes displaced
tracks one-for-one while merely deleting duplicates in the prompt region.

EXCHANGE RATES, both directions, per 0.001 of fake rate (300 evts):
```
  -M4D -1.2 -> -2.5   +52 displaced tracks for +.0078 fake   =  6.7 tracks per 0.001
  -MR -1.8 -> -2.5     +6 displaced tracks for +.0019 fake   =  3.2 tracks per 0.001
  -MR -1.8 -> -1.0    -16 displaced tracks for -.0036 fake   =  4.4 tracks per 0.001
```
`-M4D` is the most efficient displaced-per-fake lever in the chain layer by a factor ~2,
and it is also the only one that is surgical (it moves the 4-layer class only and leaves
the T5-class population that carries the displaced wins untouched to 0.3 TC/evt).

## M3c -- BRACKET RUNNING: `-M4D -1.6` and `-M4D -2.0`: 4 points, one chain-side knob each, 300 evts
  P_M4D25   -M4D -2.5    loosen the global exempt(large-DCA) T4-class kill (-1.2 today)
  P_M4DOFF  -M4D -1e9    that kill fully off -- the upper bracket
  P_ZM4D0   -ZM4D 0      drop the transition-band displaced-T4 tightening (+1.2 today)
  P_MR25    -MR -2.5     loosen the exempt-5+ OR-rescue floor (-1.800 today)
plus 2 PROTECTION-PRICE probes (what a fake-focused sibling would cost us in displaced):
  T_MR10    -MR -1.0      tighten the exempt-5+ rescue
  T_MRI05   -MRI 0.5      tighten the IP-5+ rescue back to the ANCHOR value

## M3c RESULT -- THE -M4D BRACKET IS STRONGLY CONVEX; -1.6 IS THE KNEE
`probe_batch_full.txt`, `bands_bracket_300.txt`, 300 evts. All four -M4D points plus the
two protection probes in one table:
```
tag           eff      vxy01    v15      v510     v1030    d15      d510     d1030    dup      fake     fakB
A08GATE     0.80992  0.84282  0.79459  0.73356  0.74139  0.61650  0.20161  0.03073  0.06230  0.05551  0.06680
P_M4D16     0.80975  0.84263  0.79678  0.73862  0.74540  0.62542  0.22177  0.03073  0.06273  0.05747  0.07205
P_M4D20     0.80970  0.84258  0.79751  0.74030  0.75020  0.62988  0.22177  0.03073  0.06312  0.05977  0.07786
P_M4D25     0.80961  0.84249  0.79825  0.74199  0.75500  0.63880  0.22177  0.03310  0.06359  0.06331  0.08595
P_M4DOFF    0.80634  0.83901  0.79971  0.75211  0.76781  0.65552  0.24194  0.03310  0.06148  0.13828  0.23063
T_MR10      0.81028  0.84329  0.79313  0.73019  0.73579  0.61315  0.19758  0.02837  0.06232  0.05193  0.05980
LST         0.80988  0.84296  0.77193  0.65430  0.66453  0.55407  0.20968  0.05674  0.05179  0.04476  0.04249
```

## M4 -- CORRECTION TO MY OWN UNIT: COUNT DISTINCT SIMS, NOT BAND ROWS
`a08_distinct.py` (new, analysis only), `distinct_displaced_300.txt` / `_977.txt`.
A sim born at vxy 20 cm with |dxy| 3 cm is counted once in vxy[10,30) AND once in
dxy[1,5). My M3 note summed the two projections ("~52 displaced tracks" for -M4D -2.5).
THAT DOUBLE COUNTS. Correct unit: distinct sims, displacement := max(vxy, |dxy|), same
createPerfNumDenHists selection (pt>0.9, |eta|<4.5, |vz|<30, q!=0).

THE DISPLACED HEADLINE, RESTATED IN DISTINCT TRACKS (this is the number to quote):
```
tier (displacement)   nSim   LST   FINBASE   net   |  977: nSim   LST   W_X4   net
>= 1 cm    (300)      4809  2375     2575   +200   |     16369  7782   8512  +730
>= 5 cm    (300)      3441  1319     1488   +169   |     11809  4238   4859  +621
>= 10 cm   (300)      2848   931     1053   +122   |      9819  2956   3423  +467
>= 30 cm   (300)      1599   101      127    +26   |      5721   392    494  +102
```
On the full 977 we reconstruct **8512 displaced tracks where LST reconstructs 7782: +730,
+9.4% relative** (lost 377 / gained 1107), and beyond 10 cm **3423 vs 2956, +467, +15.8%
relative**. At p~0.5 on N=16369 the combined binomial sigma is ~90 tracks, so +730 is ~8
sigma. This is the single cleanest statement of the headline advantage in the round and
it is free of the vxy/dxy double count.

### THE -M4D LEVER, RESTATED THE SAME WAY (300 evts, distinct sims, vs the baseline)
```
                 >=1cm            >=5cm           >=10cm           >=30cm     fake      eff
-M4D -1.6   LOST 1 GAIN 18   LOST 0 GAIN 14   LOST 0 GAIN 11   LOST 0 GAIN 6   +.00196  -.00017
-M4D -2.5   LOST 2 GAIN 39   LOST 1 GAIN 33   LOST 0 GAIN 27   LOST 0 GAIN 10  +.00780  -.00031
```
Every gained object is T4-class (18/18 and 36/39; the 3 T5 at -2.5 are collateral).
Marginal exchange rate, distinct displaced tracks per 0.001 of fake rate:
```
  baseline -> -1.6 :  +17 tracks for +.00196 fake  =  8.7 per 0.001   <- THE KNEE
  -1.6     -> -2.0 :   +9 tracks for +.00230 fake  =  3.9 per 0.001
  -2.0     -> -2.5 :  +11 tracks for +.00354 fake  =  3.1 per 0.001
  -2.5     -> off  : +~70 tracks for +.07497 fake  =  0.9 per 0.001   (dead)
```
The first step is 2.2x more efficient than the second and 9x more efficient than the
tail. If a displaced-leaning variant is wanted, `-M4D -1.6` is the ONLY defensible point:
one knob, +17 distinct displaced tracks, efficiency flat to 4 sims, dup +.0004, and it is
strictly one-sided in 6 of the 7 displaced bands (`simdiff_m4d16_vs_base_300.txt`:
gained/lost = 4/1, 3/0, 5/0, 6/0, 8/0, 5/0, 0/0).

MY RECOMMENDATION IS STILL "DO NOT SHIP IT BY DEFAULT". The round's fake rate is already
+.0108 against LST and fake is a finish-line criterion; +.0020 more buys 17 tracks in a
region where we are already +200. Ship the baseline; keep -M4D -1.6 documented as the
displaced-leaning variant with a measured price.

### THE PROTECTION STATEMENT, RESTATED THE SAME WAY
`-MR -1.0` costs **13 distinct displaced sims at >=1 cm, 9 at >=10 cm, and gains ZERO**
(all 13 are T5-class chains) while reading as +.00036 eff / -.00358 fake on the headline.
That asymmetry -- a pure headline win that silently deletes 13 displaced tracks -- is the
single most important thing this angle has to hand the synthesis.

## M5 -- CORRECTION TO MY OWN M2(e): THE d1030 DEFICIT IS **NOT** AN ADMISSION CEILING
M2(e) inferred that the 12 missing dxy[10,30) tracks (LST finds them as 8-hit T4s) are
governed by the 4-layer large-DCA kill `-M4D`, and quoted a "ceiling" of +.0284 in d1030
if all were recovered. THE BRACKET FALSIFIES THAT. With the kill **fully off**
(`-M4D -1e9`, fake .13828, fakB .23063 -- a catastrophic configuration nobody would ship):
```
  d1030   FINBASE 13/423    -M4D -1.6 13    -M4D -2.0 13    -M4D -2.5 14    -M4D off 14
```
`simdiff_d1030_m4doff_300.txt`: even with admission wide open we recover exactly **ONE**
of the missing tracks and still lose 12 to LST (LST's 12: T4=9 nhitOT 8, T5=3 nhitOT 10).
So the d1030 tracks are not being killed by the displaced-T4 admission cut at all -- they
are never FORMED. The block is upstream of the kill tree (T4/segment formation or the
module map), which is exactly what the standing memory note
`project_displaced_efficiency_research.md` says about the IP-tuned chain. Nothing at the
selection layer can recover them. Treat d1030 as OUT OF SCOPE for cut tuning, and do not
let anyone spend fake rate chasing it.

This is also the reason d1030 is the one band that behaves differently from every other
displaced band: the others respond to `-M4D` (v510 +3..+5, v1030 +5..+17, d15 +8..+20),
d1030 does not.

## M6 -- THE SCREENING TEST FOR SYNTHESIS (the transferable part of this angle)
The audit reduces to one mechanical rule, and synthesis can apply it in seconds without
re-running anything:

  **Does the proposed change alter the population of BARE OT chain TCs (tc_type 4 = T5
  class or 9 = T4 class)? If yes it is DISPLACED-CRITICAL and must be scored on distinct
  displaced sims before it is accepted. If it only touches pixel-seeded rows (types 5, 7,
  8) or the ownership/dedup maps, it is DISPLACED-INERT and needs no displaced check.**

Justification, measured, not asserted:
* beyond vxy 10 cm essentially ZERO matched sims have a pixel-seeded cover in EITHER
  algorithm (4/2929 for us, 10/2564 for LST on the 977) and meanCovers is 1.00
  (`cover_multiplicity_977.txt`) -- a far-displaced track is held by exactly one bare
  chain object with no redundancy;
* hence 111 configurations spanning the entire pixel/delivery/dedup space move at most
  ONE track in v1030 / d15 / d510 / d1030 (`sens_map_300.txt`, `sens_map_prior.txt`);
* hence a chain-layer tightening deletes displaced tracks ONE FOR ONE while in the prompt
  region it merely deletes a duplicate -- which is why `-MR -1.0` reads +.00036 eff /
  -.00358 fake on the headline while destroying 13 distinct displaced tracks and gaining
  none.

FLAGS ON THE FROZEN LINE, CLASSIFIED BY THAT TEST:
```
DISPLACED-INERT (measured, 111 configs)  -XC -XCT -T3E -CC -CCN -CCR -AT3 -RPS -RD
                                         -ZP8 -ZPF -ZP5 -RT3
DISPLACED-CRITICAL (measured directly)   -MR (-16 tracks at -1.0)  -MRI (-3 at 0.5)
                                         -M4D (+17 at -1.6, +37 at -2.5)  -ZM4D (+1 at 0)
DISPLACED-CRITICAL (same kill tree,      -M4 -M5 -M6 -MD -U4 -U5 -U6 -C25 -C25D -ZM4
  not individually measured -- SCREEN     -G -TR -TT -TA -BK -BT
  THEM, do not assume)
```
The first row is the good news for this round: **every mechanism the assembled baseline
added this round is in it.** The pT3 stage, the OT hit-overlap contention and the ported
CrossCleanpLS are all displaced-inert at track level, so the assembly did not spend any
of the headline advantage.

## M7 -- MECHANISM OF THE BRACKET (`m4d_bracket_mechanism_300.txt`, 300 evts, per event)
```
tag          T4/evt  T4fk/evt  T4fkRate |    T5/evt  T5fk/evt   marginal purity of the step
A08GATE        34.2      10.0    0.2930 |     426.9      37.3
P_M4D16        41.6      14.5    0.3491 |     426.8      37.2   +7.4 T4/evt, 61% of them fake
P_M4D20        49.7      19.9    0.4001 |     426.7      37.2   +8.1 T4/evt, 66% fake
P_M4D25        61.6      28.3    0.4593 |     426.6      37.1  +11.9 T4/evt, 71% fake
LST            38.4      23.0    0.5994 |     139.4       8.9
```
Two structural facts:
1. The T5-class population is untouched across the whole bracket (426.9 -> 426.6, i.e.
   0.3 TC/evt). The lever cannot disturb the machinery the displaced wins come from --
   it is genuinely surgical on the 4-layer class.
2. At `-M4D -1.6` our 4-layer class is 41.6 TC/evt against **LST's own 38.4** -- the same
   size -- while being nearly twice as clean (35% fake vs LST's 60%). That is the
   structural argument for the knee: it is not an exotic operating point, it is where our
   short-displaced class matches the size of the one LST already ships.

## M8 -- THE KNEE DOES NOT DAMAGE THE OTHER TWO DISPLACED AXES
`m4d16_dup_length_300.txt` (dup rate among TCs matched to a sim in the band, and the mean
nhitOT of those TCs; 300 evts):
```
band          FINBASE dup / len   -M4D -1.6 dup / len   -M4D -2.5 dup / len   LST dup / len
vxy[5,10)      .00915 / 10.888     .00909 / 10.868       .00905 / 10.855      .01535 / 11.238
vxy[10,30)     .00216 / 10.738     .00215 / 10.723       .00212 / 10.689      .05621 / 11.283
vxy[30,inf)    .00000 /  9.465     .00000 /  9.398       .00000 /  9.358      .07619 /  9.810
```
Displaced duplicate rate is unmoved (and stays 3-25x better than LST's); displaced matched
track length moves by -0.015 at the knee, which is inside rounding. Only the fake rate pays.

## M9 -- FULL 977 CONFIRMATION OF BOTH LEVERS (`tab977_final.txt`, `distinct_W_*.txt`)
Three 977-evt runs: `W_M4D16`, `W_M4D25`, `W_MR10`, against `fin_ref/r_W_X4` (= the
assembled baseline) and the 977 LST reference.
```
tag             eff    vxy01      v15     v510    v1030      d15     d510    d1030      dup     fake      nhB      nhE      nTC
W_X4        0.80905  0.84144  0.80110  0.72161  0.71474  0.58320  0.24521  0.03151  0.06184  0.05607  9.80064  3.55719  2033868
W_M4D16     0.80892  0.84131  0.80175  0.72412  0.71938  0.58915  0.26034  0.03408  0.06225  0.05804  9.78660  3.56091  2040577
W_M4D25     0.80873  0.84112  0.80285  0.72714  0.72450  0.59841  0.26942  0.03666  0.06318  0.06385  9.75280  3.56985  2058432
W_MR10      0.80933  0.84176  0.80022  0.71859  0.70620  0.57459  0.23713  0.03087  0.06188  0.05243  9.79315  3.55153  2025683
LST(base)   0.80987  0.84285  0.77719  0.64422  0.62567  0.51042  0.22906  0.05402  0.05138  0.04538 10.14984  3.55665  1998494
per region  effB/effT/effE   dupB/dupT/dupE   fakB/fakT/fakE
W_X4        .92454 .87981 .74436   .04294 .03375 .08069   .06754 .06932 .04578
W_M4D16     .92437 .87966 .74429   .04304 .03445 .08125   .07285 .07023 .04616
W_M4D25     .92409 .87966 .74409   .04307 .03821 .08205   .08687 .07497 .04750
W_MR10      .92483 .88050 .74450   .04288 .03369 .08066   .06046 .06190 .04521
LST         .92430 .88004 .74658   .00971 .01308 .08486   .04365 .04542 .04630
```
Everything transfers. The fake price of each knob reproduces to 4 decimals (300 -> 977:
-M4D -1.6 +.00196 -> +.00197; -M4D -2.5 +.00780 -> +.00778; -MR -1.0 -.00358 -> -.00364).

DISTINCT DISPLACED SIMS ON THE 977 -- THE DEFINITIVE NUMBERS
```
                            >=1 cm        >=5 cm       >=10 cm       >=30 cm
baseline vs LST         +730 (377/1107) +621        +467          +102
-M4D -1.6 over baseline  LOST 2 GAIN 47  L1 G43      L1 G38        L1 G19    [46/47 T4-class]
-M4D -2.5 over baseline  LOST 4 GAIN 101 L2 G91      L1 G79        L1 G39    [96/101 T4-class]
-MR -1.0  over baseline  LOST 59 GAIN 4  L52 G1      L46 G1        L10 G0    [ALL 59 T5-class]
```
* `-MR -1.0` DESTROYS **59 distinct displaced tracks and recovers 4** on the full sample,
  45 of them net beyond 10 cm, every one of them a bare T5-class chain -- while reading
  +.00028 efficiency and **-.00364 fake** on the headline scoreboard. On the frozen 300
  this was 13 tracks; the 977 shows the real size. THIS IS THE ROUND'S DISPLACED TRAP.
* `-M4D -1.6` is confirmed one-sided at scale: 47 gained, 2 lost, 46 of the 47 T4-class.
* d1030 does move a little on the 977 (.03151 -> .03408 -> .03666, i.e. +4 and +8 tracks
  on 1555) -- so the M5 statement should be read as "the admission cut recovers at most a
  handful of the 35-track deficit", not "exactly zero". It is still nowhere near LST's
  .05402 and the -M4DOFF test stands: the tracks are mostly never formed.

### EXCHANGE RATES, 977, distinct displaced tracks per 0.001 of fake rate
```
  baseline -> -M4D -1.6   +45 tracks for +.00197   = 22.8 per 0.001   <- best in the chain layer
  -M4D -1.6 -> -M4D -2.5  +52 tracks for +.00581   =  9.0 per 0.001
  -MR -1.8  -> -MR -1.0   -55 tracks for -.00364   = 15.2 per 0.001   (buying fake HERE is expensive)
  -AT3 6    -> -AT3 8      0 tracks for -.0079     = INFINITE          <- buy fake HERE
```
THE LAST ROW IS THE PUNCHLINE OF THIS ANGLE. `-AT3` moves the fake rate by more than
`-MR -1.0` does (.0555 -> .0476 vs .0555 -> .0519) and it is measured DISPLACED-INERT
across the 111-configuration map. **If synthesis needs fake rate, it should come from
-AT3 (or from the pT3-class / 10-hit T5-class slices this audit localised), NEVER from
-MR, -MRI or any other chain-layer threshold.** That single sentence is worth more to the
combined config than either -M4D point.

## FINAL POSITION OF THIS ANGLE
1. Displaced is SAFE in the assembled baseline: the pT3 stage, the OT contention and the
   ported CrossCleanpLS are displaced-inert at track level (0/0 LOST/GAINED).
2. The headline, restated without double counting: **+730 distinct displaced tracks over
   LST on the 977 (8512 vs 7782, +9.4%), +467 beyond 10 cm (+15.8%), at a displaced
   DUPLICATE rate 3-25x better than LST's, with track length within -0.02.**
3. RECOMMENDED SHIP: the assembled baseline UNCHANGED. `-M4D -1.6` is a documented,
   measured, one-knob displaced-leaning variant (+45 displaced tracks for +.0020 fake) --
   offered, not recommended, because fake is a finish-line criterion we already miss.
4. THE PROTECTION RULE, and the one thing that must survive into the combined config:
   screen every proposal by whether it changes the bare-OT-chain (tc_type 4/9) population.
   If it does, score it on distinct displaced sims BEFORE accepting it. `-MR -1.0` is the
   worked example of why: a headline win that silently deletes 59 displaced tracks.
