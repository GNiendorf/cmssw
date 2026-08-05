# A07 -- FAKE RATE: decomposition, addressability, and what is actually free

All numbers on the frozen 300-event subset, at the ASSEMBLED BASELINE
(`-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2`), which scores
eff .80992 / dup .06230 / fake .05551 against LST .80988 / .05179 / .04476.
The headline fake rate is `compare_ab.fake_overall_incut`: denominator = every TC with
|eta| < 4.5 AND pt > 0.9 (480309 rows = 1601.0/evt), numerator = tc_isFake.

## 0. WHERE THE REMAINING GAPS ACTUALLY LIVE (the one-table version)

**THIS IS AN ORACLE ROW, NOT AN ACHIEVED RESULT** -- it uses sim truth to decide which
rows to delete. It is here because it localises the entire remaining scoreboard deficit
into two cells and shows that neither of them touches the displaced win. Full 977:

```
                                eff      dup     fake |   v15    v510   v1030    d15  | nhB    nhT    nhE
ASSEMBLED BASELINE (W_X4)   0.80905  0.06184  0.05607 |.80110 .72161 .71474 .58320 | 9.80  9.88  3.56
+ pT3 head at LST's own
  fake rate (3.81%)         0.80905  0.06235  0.04835 |.80110 .72161 .71474 .58320 | 9.85  9.93  3.55
+ a working dedup of the
  -ZP8 bare-seed rows       0.80903  0.04504  0.04879 |.80110 .72161 .71474 .58320 |10.02 10.03  3.56
LST                         0.80987  0.05138  0.04538 |.77719 .64422 .62567 .51042 |10.15 10.01  3.56
```

Two localised fixes -- one head retrain and one bare-seed dedup -- would take the line to
**dup BELOW LST (.045 vs .051), fake within +.0034, track length at parity, and every
displacement band completely untouched and still far ahead** (v510 +.079, v1030 +.089,
d15 +.073 over LST). The only remaining deficit would be the -.0008 of efficiency the
Baseline agent already flagged on the 977. Neither fix goes anywhere near the large-DCA
chain branches, which is where every knob-based fake gain in this report had to take its
cost from.

### The same statement without any oracle: the fake-or-duplicate index per class, 977
"f-or-d%" is the fraction of a class's in-cut rows that are either fake or a duplicate,
i.e. the fraction that contributes nothing. It needs no simulator and no truth-based
deletion -- it is just the two harness flags.
```
OURS            N/evt   fake%    dup%  f-or-d%  |  LST        N/evt   fake%    dup%  f-or-d%
attach pT5     537.03    0.74    0.65     1.39  |  pT5       713.32    1.47    0.97     2.44
carried pLS    605.95    4.80    9.21    14.02  |  pLS       630.11    4.89    9.94    14.83
bare chain     357.13   11.07    6.91    17.97  |  T5 + T4   144.85   18.35    6.51    25.16
attach pT3      93.97   17.76    1.60    19.36  |  pT3       104.88    3.81    2.75     6.56
zp8 pLS         21.54    6.00   66.90    72.89  |  (no counterpart)
```
On EVERY population LST also builds we are equal or better -- pixel-attached 5+ 1.39 vs
2.44, bare pixel seeds 14.02 vs 14.83, bare outer-tracker objects 17.97 vs 25.16. There
are exactly two exceptions: the pT3-class cell, 3x worse than LST's pT3, and one extra
cell we carry that LST does not (the -ZP8 rows) which is 73% waste. Those two cells are
the whole scoreboard deficit, and section 0 shows what fixing them is worth.

## 1. THE FAKE BUDGET, BY DELIVERY CLASS (this is the whole answer)

Ours (per event, in-cut) versus LST's own TC collection on the same events:

```
OUR CLASS              N/evt  fake/evt   rate    | LST CLASS         N/evt  fake/evt   rate
chain T5   (bare 5+)  328.42     30.26  .0921    | T5   (bare 5)    111.92      7.43  .0664
carried pLS           600.58     28.53  .0475    | pLS              624.66     30.16  .0483
attachT3 pT3          93.32      16.23  .1739    | pT3              104.48      3.84  .0368
chain T4   (bare 4)    26.00      8.62  .3314    | T4                31.53     18.63  .5908
attachT5 pT5          531.20      4.04  .0076    | pT5              706.58     10.62  .0150
zp8 pLS (-ZP8 rows)    21.52      1.21  .0564    | --
TOTAL                1601.02     88.88  .05551   | TOTAL           1579.17     70.68  .04476
```

EXCESS LEDGER (+18.20 fakes/evt = the +.01075 gap). Compare POPULATIONS, not rows: the
pixel attach moves an object between the bare and attached classes without creating or
destroying it, so "bare T5" on our side and "T5" on LST's side are both just the
unattached leftovers and comparing them directly is biased.

```
population (rows/evt, fakes/evt, rate)      OURS                 LST            excess
5+ layer OT object, +/- pixel seed   859.62  34.30  3.99%   818.50  18.05  2.21%  +16.25
3 layer + pixel seed (pT3-class)      93.32  16.23 17.39%   104.48   3.84  3.68%  +12.39
bare pixel seed (pLS rows)           622.10  29.74  4.78%   624.66  30.16  4.83%   -0.42
4 layer OT object (T4-class)          26.00   8.62 33.14%    31.53  18.63 59.08%  -10.01
                                                                                  ------
                                                                                  +18.20
```

Two populations carry it; two repay a third of it. NOT a matching artifact: on the two
populations where we and LST build the same kind of object our fake rate is HALF theirs
(pixel-attached 5+ .0076 vs .0150, T4 .3314 vs .5908), so the ported matcher is not
calling more fakes than the production writer does.

By region (in-cut, per event): barrel excess +12.25, transition +6.63, endcap -0.68 (we
are better in the endcap). Barrel: bare-5L +11.5, pT3-class +5.9, T4 -3.4, pT5 -1.8.
Transition: bare-5L +8.5, pT3-class +2.9, T4 -3.3, pT5 -1.4. Same shape in both.

**CONFIRMED ON THE FULL 977** (fin_ref/r_W_X4.root vs fin_ref/fin_base977.root, my
`a07_ref/decomp_977.txt`). Every per-class rate transfers to within .0005 and the ledger
reproduces: 5+ layer +16.65, pT3-class +12.69, bare pixel -0.40, T4 -10.65, total +18.29
fakes/evt against +18.20 on the 300. Per-class rates on 977: chain T5 .0931 (300: .0921),
attachT3 .1776 (.1739), chain T4 .3336 (.3314), attachT5 .0074 (.0076), carried pLS .0480
(.0475); LST T5 .0679, pT3 .0381, T4 .6040, pT5 .0147, pLS .0489. **The decomposition is
not a 300-event artifact.**

## 2. WHAT THE TWO CELLS ACTUALLY ARE

**(a) 5+ layer chains: 1.8x more fake-prone as a POPULATION.** 859.6 rows/evt at 3.99%
against LST's 818.5 at 2.21% -- similar volume, worse purity. Two facts pin down where:
the leftover-vs-leftover comparison is a wash (our bare chain rows at 10 OT hits are 13.98%
fake, LST's bare T5 rows at 10 OT hits are 13.90%), and we attach a pixel seed to only 62%
of the population where LST attaches to 86%. Note that raising attach recall would NOT
reduce the fake count -- attaching a pixel to a true object only moves it between classes,
it does not delete a fake -- so the recall gap is a duplicate/length story, not a fake one.
The fake story is in section 3: the extra objects our builder admits and LST's T5 pipeline
never builds are the large-DCA ones.

**(b) pT3-class deliveries: a HEAD-QUALITY statement, and the sharpest number in this
report.** Attributing each reconstructed in-cut sim to the class of its BEST-matching TC
(the harness's own `sim_tcIdx`) and pairing that with the fake budget:

"tracks" = in-cut sims in the efficiency denominator whose BEST-matching TC is in that
class (that is the harness's own per-type efficiency definition, so it is like-for-like
between the two ntuples; it is an attribution, not a claim that removing the class would
cost exactly that many tracks).
```
                    tracks attributed/evt   fakes paid/evt   TRACKS PER FAKE
  our pT3-class              2.86               16.23            0.18
  LST's own pT3              4.17                3.84            1.09     <-- 6x better
  our pT5-class             24.05                4.04            5.95
  LST's own pT5             39.47               10.62            3.72
  our bare 5+ chains        17.54               30.26            0.58
  LST's bare T5              2.03                7.43            0.27
  (both ntuples reconstruct exactly the same 61.1 of 75.4 in-cut sims per event)
```
Confirmed on the full 977: our pT3-class 2.93 tracks / 16.69 fakes = **0.176**; LST's pT3
4.34 / 4.00 = **1.085**. The same factor of six.
Our pT3-class rows buy FEWER tracks than LST's pT3 (2.86 vs 4.17/evt) while paying 4x the
fakes. Everywhere else we are at or better than LST on this ratio, so this is not a
sample-wide effect -- it is one cell. Same volume as LST's pT3 (93 vs 104 in-cut rows/evt)
at 4.7x the fake rate (.1739 vs .0368), in every region (B .132/.020, T .209/.038,
E .248/.079). This is the borrowed attach head, whose model selection used chain-pair
validation and whose bare-pair labels a sibling found partly wrong. A perfect fake filter
on this cell alone is worth **-.0097 of fake rate**, i.e. almost the entire +.0108 gap.

## 3. THE FINDING THAT MATTERS MOST: THE FAKE EXCESS AND THE DISPLACED WIN SHARE A CARRIER

The -G 6 admission branch is dumped per TC (`tc_dbgBr`: 0 = T4 IP, 1 = T4 exempt/large-dca,
2 = 5+ IP, 3 = 5+ exempt/large-dca). Splitting our chain-derived fakes by it:

```
branch                       N/evt   fake%   fake/evt
3  5+ exempt   (large dca)  161.93   15.38     24.90     <-- 28% of ALL our fakes
1  T4 exempt   (large dca)   17.79   48.03      8.54
2  5+ IP                    166.08    3.22      5.35
0  T4 IP                      8.21    0.85      0.07
   pT5-class exempt (br 3)   60.04    2.91      1.75
   pT5-class IP    (br 2)   471.16    0.49      2.31
```

82% of the chain-derived fake budget sits on the two EXEMPT (large-DCA) branches -- which
are exactly the branches that produce v510 +.079, v1030 +.077 and d15 +.062 over LST.
Deleting the 5+ exempt branch outright is worth **fake -.01106 -- nearly the whole gap on
its own** -- and costs **v1030 -.53243, d15 -.55853, v510 -.26981**: that one branch holds
72% of the vxy[10,30) efficiency and 91% of the dxy[1,5) efficiency. The T4 exempt branch
is the same story in miniature (fake -.00477 for d15 -.04794 at eff -.00040). This is not
a tuning accident; it is one mechanism seen through two metrics. **Our fake excess is
largely the price of the displaced win, and it cannot be given back without giving the
displaced win back.** On the full 977 the same ablation reads fake -.01109 for
v1030 -.49585 and d15 -.51671 -- identical conclusion, three times the statistics.

## 4. THE SCAN: NOTHING FREE OF MEANINGFUL SIZE EXISTS AT THIS OPERATING POINT

Tooling: `a07_sim.py`, a removal simulator validated to reproduce all 16 published FINBASE
numbers exactly with an empty removal set. It answers "delete these delivered TCs" for
eff / dup / fake / per region / all four vxy and dxy bands, so a candidate can be judged
without a 300-event run. Denominators, so deltas can be read as tracks:
eff 22633, barrel 8787, transition 3928, endcap 9026, vxy[1,5) 1368, vxy[5,10) 593,
vxy[10,30) 1249, dxy[1,5) 897, dxy[5,10) 248, fake/dup 480309.

**41 structural predicates** (a07_scan.py) and **27 settings of the five existing gate
constants** -M4 / -M4D / -MRI / -MR / -C25+-C25D (a07_gate.py, all reconstructed exactly
from the dumped mP, mD, branch, nLayers and nNodes). The cheapest fake gains found:

```
change                    d_fake    d_eff    d_dup   d_v510  d_v1030    d_d15   verdict
-MR -0.5                 -.00589  -.00110  -.00001  -4 trk  -22 trk   -18 trk  costs displaced
-C25 0 -C25D 0           -.00438  -.00040  +.00010  -1 trk  -20 trk   -19 trk  costs displaced
T4 br1 & nB==4 dropped   -.00396  +.00000  -.00002  -4 trk   -0 trk   -41 trk  costs displaced
-M4D 0                   -.00365  -.00022  -.00079  -4 trk  -14 trk   -15 trk  costs displaced
-C25 0 -C25D -1          -.00303  -.00013  +.00006  -1 trk   -6 trk    -4 trk  costs 3 prompt trk
-MR -1.5                 -.00142  -.00013  +.00001  -2 trk   -1 trk    -2 trk  costs 3 prompt trk
-C25 1 -C25D -2          -.00113  -.00049  -.00029  -1 trk   -1 trk    -0 trk  costs 11 prompt trk
-M4D -1                  -.00087  +.00000  -.00016   0 trk   -4 trk    -3 trk  costs 7 displ trk
```

Every fake gain above .001 costs displaced efficiency. `-M4` (the T4 IP branch) is the one
gate constant that moves in the other direction: `-M4 6` gives **dup -.00405** for eff
-.00199 and fake +.00025 -- a duplicate-rate lever, not mine, but worth handing on.

**The one exactly-free move -- AND ITS DEATH ON THE FULL SAMPLE.** A fine sweep of the
three cheapest constants with ALL EIGHT displacement bands printed found exactly one
setting on the frozen 300 that is zero on efficiency, zero on
vxy[0,1)/[1,5)/[5,10)/[10,30) and zero on dxy[0,1)/[1,5)/[5,10)/[10,30) while gaining fake
rate: **`-MR -1.75`** (from the frozen -1.800), worth -0.00021.

I re-ran the identical sweep on the full 977 (`a07_ref/fine977.txt`). It is **not free
there**: d_eff +0.00000, d_fake -0.00024, but **d_v1030 -0.00073 (3 tracks) and
d_d01 -0.00003**. The freeness was a 300-event artifact, exactly as suspected. Every other
candidate in the sweep degrades the same way. **NOT ADOPTED, and now with a measurement
rather than a hunch.** This is also the cleanest available evidence that the negative
result in this section is real and not a failure to search hard enough: the single point
that survived a 41-predicate + 27-constant + exhaustive-cell search on the iteration set
did not survive the confirmation sample.

**Oracle check.** Marking every TC that is the sole protected cover of a counted sim
(overall denominator or ANY displacement band) shows only 2.3-7.3% of rows per cell are
essential -- so 93-98% is deletable at zero cost IF you had truth. (977: 7.18 / 2.30 /
3.07 / 2.54 / 4.55 / 2.52 % against the 300's 7.28 / 2.34 / 3.02 / 2.46 / 4.54 / 2.42 --
the oracle transfers as well, and the truth-perfect pT3 drop reads fake .04847 at
efficiency EXACTLY unchanged on the full sample.) Searching all
7-dimensional structural cells (deliv x type x branch x nPS x innermost layer x pt bin x
eta bin) with N >= 150 for cells with ZERO essential rows returns exactly one usable cell:
`chain T4, branch 1, nPS 2, inLay 2, pt<1.5, barrel` -- 0.89 rows/evt, 76% fake, worth
**-.0004 of fake**. It is a seven-way cut fitted on 300 events for four ten-thousandths;
proposing it would be exactly the transfer-fragile cleverness the synthesis is told to
reject. **I am not proposing it** -- and re-running the identical search on the full 977
proves the point: that cell now contains essential rows, and the ONLY zero-essential cell
left on 977 is 6.4% fake, i.e. dirtier than the 5.6% average, so deleting it would make
the fake rate WORSE. The 300-event cell was pure overfitting.

## 5. VERDICT

There is no free fake win at the assembled operating point, and the reason is structural
rather than a failure to look: the fake excess lives in (a) the exempt/large-DCA chain
branches, which are the displaced engine, and (b) the pT3-class delivery head, whose only
existing knob (-AT3) moves efficiency and duplicate rate at the same time. Fake rate is
therefore not a tuning problem on this line -- it is downstream of two decisions already
taken (accept large-DCA chains; deliver pT3-class rows on a borrowed head).

The one lever that would pay without touching efficiency, duplicates or displaced
performance is the permitted **attach-head retrain**: the pT3-class cell alone is worth
-.0097 of the +.0108 gap, it carries ZERO displaced efficiency (dropping the entire cell
moves d15 and v1030 by exactly 0.00000 on BOTH the 300 and the full 977, and v510 by 2
tracks), and it buys 0.18 tracks per fake where LST's own pT3 buys 1.09. That is the
recommendation.

### The retrain target, measured on the FULL 977 (not extrapolated)
Deleting exactly the FAKE pT3-class rows and nothing else -- what a perfect head would do
-- run through the simulator on the full 977:

```
                       eff       dup      fake  |  v15    v510   v1030    d15  | nhB/nhT/nhE
BASELINE (W_X4)     0.80905   0.06184   0.05607 | .80110 .72161 .71474 .58320 | 9.801/9.884/3.557
PERFECT pT3 filter  0.80905   0.06249   0.04622 | .80110 .72161 .71474 .58320 | 9.859/9.940/3.541
LST                 0.80987   0.05138   0.04538 | .77719 .64422 .62567 .51042 |10.150/10.009/3.557
delta               +0.00000  +0.00065  -0.00985 |  0      0      0      0    | +.06/+.06/-.02
```

**A perfect pT3 fake filter alone takes the fake gap from +.0107 to +.0008 -- parity with
LST -- with efficiency and ALL FOUR vxy and ALL FOUR dxy bands EXACTLY unchanged, and
track length slightly UP.** The duplicate rate moves +.00065, purely from the shrinking
denominator. That is the ceiling. A realistic retrain merely reaching LST's own pT3 fake
RATE of 3.81% at the same 93.97 rows/evt would carry 3498 fake rows instead of 16308 over
the 977, landing at fake **75698/1565647 = .04835, i.e. +.0030 against LST instead of
+.0107** -- again with efficiency and every displacement band untouched, because the rows
it removes are fakes.

This is the entire realistic fake programme for this line, and it is a training job, not a
tuning job. Everything else on offer is a trade.

### Why the pT3 cell and not the bigger one -- the "is it attainable" test
A perfect (truth) fake filter costs nothing anywhere by construction, so its value per cell
is the honest ceiling. On the full 977:

```
perfect filter on...        d_fake     LST's own comparable class   attainable?
bare 5+ chain rows        -0.01834     LST bare T5 is 6.79% fake    NO -- ours is 9.31%,
                                                                    LST cannot do this either
pT3-class rows            -0.00985     LST pT3   is 3.81% fake      YES -- ours is 17.76%,
                                                                    LST DEMONSTRATES 4.7x better
T4-class rows             -0.00512     LST T4    is 60.4% fake      NO -- ours is 33.4%,
                                                                    we are already twice as good
```

The largest ceiling is the bare 5+ chain cell, but nobody -- including LST -- knows how to
classify bare 5-layer outer-tracker objects better than ~7% fake, so that ceiling is not
reachable. The pT3-class cell is the ONLY one where a production algorithm on this very
sample already demonstrates the quality we are missing. That is what makes it the target,
not its size.

## 5b. HOW TO REPRODUCE ANY NUMBER IN THIS REPORT
```
bash a07_ref/setup_ws.sh                          # protoA07 = copy of protoFIN
bash a07_ref/a07_run.sh <TAG> [overrides]         # the assembled baseline is built in
bash a07_ref/pyrun.sh a07_ref/a07_extract.py <run.root> <out.pkl>
bash a07_ref/pyrun.sh a07_ref/a07_decomp.py   <run.root>      # the fake budget
bash a07_ref/pyrun.sh a07_ref/a07_bestclass.py <run.root>     # efficiency attribution
bash a07_ref/pyrun.sh a07_ref/a07_sim.py      <pkl>           # simulator self-validation
bash a07_ref/pyrun.sh a07_ref/a07_classes.py  <pkl>           # class ablations
bash a07_ref/pyrun.sh a07_ref/a07_gate.py     <pkl> [filter]  # gate-constant sweep
bash a07_ref/pyrun.sh a07_ref/a07_fine.py     <pkl>           # the exactly-free hunt
bash a07_ref/pyrun.sh a07_ref/a07_oracle.py   <pkl>           # zero-cost ceiling per cell
bash a07_ref/pyrun.sh a07_ref/a07_freecells.py <pkl> <minN>   # zero-essential cell search
bash a07_ref/pyrun.sh a07_ref/a07_denoms.py   <pkl>           # denominators
```
Pickles already built: `a07_ref/finbase.pkl` (frozen 300), `a07_ref/w_x4.pkl` (full 977).
Every 977 number above came from files fin_ref had already produced -- no new physics runs
were needed for the confirmation.

## 6. TWO SIDE-FINDINGS FOR OTHER ANGLES (measured here, not mine to take)

* **`-M4 6` is a duplicate-rate lever with ZERO displaced cost, confirmed on 977.** The T4
  IP branch (`tc_dbgBr == 0`) is 8.2 in-cut rows/evt at 0.85% fake and 42% duplicate.
  Raising -M4 from 4.0 to 6.0 gives **dup -.00405** for eff -.00199 and fake +.00025
  (977: dup -.00405, eff -.00202, fake +.00024 -- the same to five decimals), with
  d_v1030 and d_d15 exactly 0.00000 on BOTH samples. It is an existing constant; nothing
  new is added. It saturates at -M4 6 (-M4 8 is identical).
* **THE -ZP8 BARE-pLS ROWS ARE THE WHOLE DUPLICATE GAP, AND THEY ARE ALMOST FREE TO
  REMOVE.** 21.5 in-cut rows/evt, 5.6% fake, **67.2% duplicate** (72.9% fake-or-duplicate,
  by far the worst cell in the output). On the full 977, deleting exactly the -ZP8 rows
  that ARE duplicates -- 17.95 rows/evt -- measures:

      d_eff -0.00001   d_dup -0.01717   d_fake +0.00050
      d_v1030 +0.00000   d_d15 +0.00000

  That is **dup .06184 -> .04467, i.e. below LST's .05138**, for one hundred-thousandth of
  efficiency and zero displaced cost. It is an oracle (it uses truth to pick which rows),
  but it says the entire +.0105 duplicate gap lives in one cell of 21.5 rows/evt, and that
  cell is bare pixel seeds -- exactly what the ported CrossCleanpLS exists to dedup.
  Whoever owns the duplicate angle should start here and nowhere else.
  (Blunt deletion of the whole cell is dup -.01697 at eff -.00717; the selectivity is what
  makes it nearly free.)
  **AND THE MECHANISM IS PINNED**: of the 17.95 duplicate -ZP8 rows per event, **17.35
  (97%) duplicate a BARE CHAIN TC** (delivery class 1), 0.39 duplicate a pT5-class attach
  delivery, 0.09 a carried pixel row and 0.05 a pT3-class delivery. This is not
  seed-vs-seed dedup and it is not the pixel arms of CrossCleanpLS -- it is exactly the
  seed-vs-chain dedup in the sub-threshold band that the port round and the Baseline agent
  both named as the open target, and it is worth the whole duplicate gap.
  A second, smaller instance of the same shape: the T4 IP branch's duplicate rows,
  4.49/evt, are worth dup -.00413 at eff +0.00000 and zero displaced cost.
