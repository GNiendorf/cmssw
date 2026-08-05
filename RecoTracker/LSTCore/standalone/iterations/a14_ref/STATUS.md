# A14 -- PER-REGION CALIBRATION -- STATUS

Angle: per-region calibration across the whole delivery+dedup surface, starting from the
ASSEMBLED BASELINE (`-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2`).

Workspace: `standalone/protoA14/` (copy of protoFIN). Artifacts: `standalone/a14_ref/`.
Runner: `bash a14_ref/a14_run.sh <TAG> [overrides]` -- identical frozen prefix to
fin_run.sh, only BIN and the artifact dir differ.
Tables: `a14_tab.py` (headline+bands), `a14_tab2.py` (per-region); both fall back to
fin_ref -> xc_ref -> rebase_ref so baseline tags can be quoted in the same table.

## M0 -- SETUP (DONE)
protoA14 = byte copy of protoFIN, binary md5 519b0abc34a28cd1e803d6b9407ef224 confirmed.

## M1 -- CODE (DONE, all default-inert)
1. `-AT32 <v>` / `-AT33 <v>` -- per-region bare-T3 attach margin, transition and endcap
   bins, on the SEED's |eta| with the scoreboard's boundaries (1.1 / 1.7), i.e. exactly
   the bins `-XCT2`/`-XCT3` already use. Applied in BOTH places `-AT3` acts: the delivery
   margin in `gaStageT3` and the `-RPS` seed-retirement predicate in main.cc (3 sites) +
   the attach CM diagnostic. Sentinel 1e9 -> follow `-AT3`, so a single `-AT3` remains the
   global margin and every pre-A14 line is bit-identical.
   Files: AttachDelivery.h (`GeneralAttachParams::t3MarginAt`, free `gaT3MarginAt`),
   AttachDelivery.cc, main.cc.
2. `-RGD 1` -- per-region ledger, diagnostic only: TC composition (ours vs LST's carried
   ntuple) by band and type; the pT3-class ledger (pixel-revoked / bidding / cc-revoked /
   delivered) by target-T3 band; the `-XCD` seed partition by seed band.
Binary v1 md5 1f9a3f13f9dfb6aef3c1c51dbb638836 (snapshot: protoA14/bin/chainproto_v1).

## M2 -- LST's OWN PER-REGION TC COMPOSITION (the calibration reference)
`a14_ref/lst_comp.py` on the frozen 300 (rows per event, LST's carried ntuple):
```
band             t4 T5    t5 pT3    t7 pT5    t8 pLS     t9 T4     total
barrel            58.0      85.2     363.8      41.8      16.3     565.1
transition        44.0      34.0     198.4      22.5       6.5     305.4
endcap            37.4      32.3     241.2     830.3      15.6    1156.8
ALL              139.4     151.5     803.5     894.5      38.4    2027.3
```
LST's pT3 class splits 56.2% / 22.5% / 21.3% barrel/transition/endcap. Note the ENDCAP is
830 bare-pLS rows per event -- 72% of all endcap TCs -- so endcap rates are heavily
diluted and small absolute endcap moves barely register on the rate.

## M2b -- NO-OP GATE: PASSED EXACTLY
tag AGATE = the assembled baseline line on protoA14 reproduces fin_ref's FINBASE on EVERY
metric to 5 decimals, on every per-region metric, and on nTC exactly (618793 = 618793).
`rebase_ref/cmp_branches.py fin_ref/r_FINBASE.root a14_ref/r_AGATE.root` reports
**33 IDENTICAL, 0 DIFFER, 0 MISSING**.

## OPERATIONAL NOTE (cost an hour)
Runs launched as children of a `run_in_background` wrapper shell are killed when that
wrapper is stopped -- batch 1 died at 285/300 events. Relaunch every batch with
`setsid nohup ... &` (a14_ref/batch1b.sh, batch2.sh) so the runs sit in their own session.
The box is shared with 14 sibling agents (load 118-135, ~130 concurrent chainproto), so a
300-evt run with the pT3 stage on takes 60-90 min wall, not the ~13 min of a quiet box.

## M3 -- BATCH 1 (9 points, 300 evts, launched 21:43, RELAUNCHED 23:12)
A0 (baseline + -RGD 1 -XCD 2, doubles as the new-binary no-op gate), the -XCT region
scan (XE45/XE5 loosen the endcap; XB35/XB3 tighten the barrel; XBT3 tightens barrel+
transition; XMIX tightens B+T and loosens E) and two -AT3 region probes (AE5 loosens the
endcap delivery margin, ABT7 tightens barrel+transition).

## M3b -- BATCH 2 (6 more points, launched 23:13 alongside batch 1)
XE6 (endcap -XCT3 6), XB25 (barrel 2.5), XSL1/XSL2 (the ONE-CONSTANT SLOPE family:
barrel 4-s / transition 4 / endcap 4+s for s = 1 and 2 -- the simplicity hedge, since a
config passing with one knob beats one passing with three), AB7E5 (-AT3 7 -AT32 7
-AT33 5), COMBO (XSL1 slope + AB7E5 margin together).

## M4 -- THE FIND, AND IT REVERSES THE OBVIOUS READING  (`a14_ref/bandcounts.py`)
The per-region RATE table says every dup/fake excess vs LST is barrel+transition and the
endcap is already BETTER than LST, so "tighten the barrel". THE RAW COUNTS SAY THE
OPPOSITE. The band denominators are wildly unequal -- efficiency splits
barrel/transition/endcap 8787 / 3928 / 9026 sims (40/18/42%) but the TC denominator splits
143359 / 77392 / 259558 (30/16/54%) -- so a rate delta in one band is NOT comparable to a
rate delta in another. In NUMERATOR COUNTS, over the global `-XCT 4.5 -> 3.0` move:
```
band        d(eff numer)   d(dup numer)   dup rows per sim match   global dup-pts per eff-pt
barrel            -36          -2294              63.7                     2.88
transition        -15          -1335              89.0                     4.03
endcap             -8          -1318             164.8                     7.46
```
(global factor = 21741 sims / 480309 TCs = .04526; efficiency and duplicate rate each have
ONE global denominator, so this is the honest end-to-end exchange rate.)
**The endcap buys 2.6x more global duplicate rate per unit of global efficiency than the
barrel.** The reason is structural, not a fit: the endcap holds 830 bare-pLS rows per event
(72% of all endcap TCs, `lst_comp.py`), so endcap seed retirements are nearly free in
efficiency and enormous in TC count, while barrel seeds are far more often the only cover
for their sim. The correct calibration is TIGHTEN THE ENDCAP -- the direction batches 1/2
did not scan. Batch 3 (below) scans it.

## M5 -- BATCH 3 (6 points, launched 23:47): the reversed direction
  RE3 RE25 RE2 RE0   `-XCT 4 -XCT3 <3|2.5|2|0>` -- the ONE-EXTRA-CONSTANT form: barrel and
                     transition stay at the shipped 4, only the endcap bin moves.
  RB5E3              `-XCT 5 -XCT2 5 -XCT3 3`   -- spend the recovered budget on a looser
  RB45E2             `-XCT 4.5 -XCT2 4.5 -XCT3 2`  barrel+transition (two constants).
The bar these must clear is NOT the baseline point, it is the GLOBAL -XCT frontier at
matched duplicate rate (`a14_ref/frontier.py`): -XCT 4.5 / 4 / 3.5 / 3 =
.81037/.06675, .80992/.06230, .80904/.05925, .80776/.05689.

## M6 -- THE `-RGD` LEDGER AT THE BASELINE (tag A0, 300 evts, per event)
```
composition          t4(T5)  t5(pT3)  t7(pT5)  t8(pLS)   t9(T4) | LST t4  t5    t7     t8    t9
barrel  |eta|<1.1     263.4     74.5    242.4     51.1     28.3 |  58.0  85.2  363.8  41.8  16.3
transit 1.1-1.7       183.4     27.2    121.5     25.4     10.3 |  44.0  34.0  198.4  22.5   6.5
endcap  |eta|>1.7     119.5     33.7    212.0    813.7     33.9 |  37.4  32.3  241.2 830.3  15.6

pT3 ledger          pixRevok  bidding  ccRevok  deliver
barrel                  84.6    200.1    125.6     74.5
transit                 91.7    110.8     83.6     27.2
endcap                 485.0    155.0    121.3     33.7

seed fate (-XCD 2)   class      N  consumed  RPSblock  XCretire  survive
barrel                   A  295.5     204.1      12.9      67.2     11.4
barrel                   B   44.4       2.8       0.5       2.7     38.4
barrel                   C    3.0       0.5       0.4       0.7      1.3
transit                  A  116.4      57.6      27.0      27.5      4.4
transit                  B   22.7       1.2       1.2       1.4     18.9
transit                  C    3.6       0.4       0.5       0.6      2.1
endcap                   A  264.0      40.0     194.2      19.2     10.6
endcap                   B  774.2       0.9       5.5       1.8    766.1
endcap                   C   45.2       0.8       4.5       2.9     37.0
```
READINGS
1. **THE pT3-CLASS DELIVERY CALIBRATION IS ALREADY LST's, AND THE OLD DEFECT IS GONE.**
   Our 135.4 delivered rows split 55.0 / 20.1 / 24.9 % barrel/transition/endcap against
   LST's own pT3 split of 56.2 / 22.5 / 21.3 %. The earlier round's pathology (a global
   margin delivering 73 / 94 / 420 where LST delivers 322 / 139 / 127) IS FIXED -- not by
   any regional constant but by the assembly itself (`-CC` OT contention plus the pixel-side
   `-RD` family dedup, which revoke 485 of the endcap's 640 candidates). There is nothing
   left to fix on the delivery-volume calibration. This kills the premise the angle was
   assigned on, and it is the single most useful thing this agent measured.
2. **OUR OVER-PRODUCTION IS T5/T4, NOT pT3-CLASS, AND IT IS BARREL+TRANSITION.** We emit
   +95 / +62 / +56 rows per event over LST by band, and the excess is entirely `t4` chain
   TCs (263 vs 58 barrel) standing where LST puts `t7` pT5 rows (242 vs 364). That is the
   chain-vs-pT5 split, not a per-region threshold -- out of scope for this angle and NOT
   fixable by any -XCT/-AT3 setting.
3. **THE A:B RETIREMENT SELECTIVITY IS WORST IN THE ENDCAP (10.7:1 vs barrel 24.9:1) AND
   THAT IS MISLEADING.** Per SEED the endcap is far more conservative -- it retires 0.23%
   of its class-B pool against the barrel's 6.1% -- because the endcap class-B pool is
   774 rows per event against an in-cut efficiency denominator of ~209 sims per event. Most
   endcap class-B seeds are out-of-acceptance pileup, so retiring them costs no MEASURED
   efficiency. This is exactly why the truth partition and the raw scoreboard counts
   disagree about the endcap, and why the counts are the ones to believe.

## M7 -- BATCH 1+2 RESULT: PER-REGION `-XCT` DOES NOT BEAT ONE GLOBAL `-XCT`
`judge.py` interpolates the measured global -XCT frontier in duplicate rate and reports the
efficiency excess. EVERY per-region point lands at or BELOW it. Nothing here earns a
second constant.
```
tag     setting (B/T/E)        eff      dup     fake   EFFGAIN vs global one-knob
A0      4 / 4 / 4          .80992  .06230  .05551   +.00000   (gate: == FINBASE)
XE45    4 / 4 / 4.5        .80997  .06344  .05550   -.00007
XE5     4 / 4 / 5          .81014  .06511  .05548   -.00006
XE6     4 / 4 / 6          .81045  .07017  .05538   -.00026
XB35    3.5 / 4 / 4        .80935  .06090  .05556   -.00017
XB3     3 / 4 / 4          .80851  .05986  .05559   -.00071
XB25    2.5 / 4 / 4        .80785  .05900  .05562   -.00106
XBT3    3 / 3 / 4          .80807  .05838  .05564   -.00050
XSL1    3 / 4 / 5 (slope)  .80873  .06268  .05556   -.00123
XSL2    2 / 4 / 6 (slope)  .80741  .06611  .05552   -.00290
XMIX    3 / 3 / 5          .80829  .06120  .05560   -.00132
```
The per-band MARGINAL EXCHANGE RATE (efficiency points paid per duplicate point bought,
moving one band alone off the baseline) is what drives it:
```
barrel  4 -> 3.5 / 3 / 2.5 : .41 / .58 / .63
transit 4 -> 3   (XBT3-XB3): .30
endcap  4 -> 4.5 / 5 / 6   : .04 / .08 / .07
```
**The endcap is ~8x cheaper than the barrel per unit of duplicate rate**, confirming the
count analysis from the other side. That is why every "tighten the barrel" point loses and
every "loosen the endcap" point merely slides along the existing curve: the global knob
already spends most of its effect in the endcap, so the endcap-only moves ARE the frontier
and the barrel-only moves are strictly worse than it. Batch 3 scans the one combination
this implies and batches 1/2 did not contain -- endcap TIGHTER, barrel LOOSER.

## M8 -- BATCH 1+2 RESULT: THE `-AT3` REGIONAL SIGNAL IS REAL AND IS THE OPPOSITE SIGN
The delivery margin, unlike the dedup threshold, genuinely wants opposite settings at the
two ends of the detector. TWO independent measurements of the ENDCAP margin alone:
```
AE5   -AT3 6 -AT33 5  vs A0  (endcap margin 5 -> 6): eff +.00115  fake -.00369  dup +.00125
ABT7  -AT3 7 -AT33 6  vs E_A7X4 (endcap 6 -> 7)    : eff +.00080  fake -.00169  dup +.00153
```
Raising the ENDCAP delivery margin buys efficiency AND fake rate and pays only duplicate
rate. Mechanism: in the endcap the pT3-class delivery is a WORSE cover for its sim than the
bare pLS row the -RPS predicate retires in its place, so suppressing the delivery hands the
sim back to a seed that covers it. And duplicate rate is exactly the currency the endcap
-XCT3 sells at .04-.08 -- the cheapest rate in the detector. So `-AT33` high paired with
`-XCT3` low is a trade with a positive expected value on BOTH dominant metrics. Batch 4
(launched 23:53) measures the pair.
```
predicted from the two slopes above, -AT33 8 with -XCT3 ~2.5-3, vs the baseline:
  eff +.0012   dup +.0000   fake -.0028
```
Everything about that prediction is a linear extrapolation of two small measured moves and
must be confirmed by the runs, then by the full 977.

## M9 -- THE BINS ARE CLEANLY REGIONAL (bandcounts.py A0 XE6 XB25 AE5 ABT7)
XB25 moves ONLY barrel counts, XE6 ONLY endcap counts (plus a small transition-dup
leakage: a seed at |eta| 1.75 can make a TC at |eta| 1.65, and the thresholds bin on the
SEED while the histograms bin on the TC -- 63 rows out of 3870, ignorable). The isolation
is real, so the per-band derivatives below are honest.
```
endcap -XCT3 4 -> 6 (XE6 - A0)  : eff +12 sim matches, dup +3870 rows  ->  322 : 1
barrel -XCT  4 -> 2.5 (XB25-A0) : eff -47,               dup -1649     ->   35 : 1
endcap -AT33 6 -> 5  (AE5 - A0) : eff -26, fake +1816, dup -545
```
In global units (x 22633/480309) the endcap dedup lever is 15.2 duplicate points per
efficiency point against the barrel's 1.65 -- a factor of NINE. The endcap efficiency
deltas are only 12-26 counts though, so every endcap claim here is extrapolation-grade
until the full 977 confirms it.

## TOOLING IN a14_ref/ (all read-only analysis)
  a14_run.sh    the frozen-line runner (BIN/NEV/LSTN/BASEHISTS overridable)
  a14_tab.py    headline + displaced bands       a14_tab2.py  per region
  final_tab.py  both blocks in one shot, LST row appended -- use this for the deliverable
  bandcounts.py per-band NUMERATORS and DENOMINATORS (the honest global weighting)
  lst_comp.py   LST's own per-band TC composition from the input ntuple
  judge.py      excess over the 1-D global -XCT frontier at matched duplicate rate
  pareto.py     is a point DOMINATED by any measured global (-AT3, -XCT) grid point
  rg.sh         print the -RGD ledger blocks out of run logs
  batch1.sh batch1b.sh batch2.sh batch3.sh batch4.sh
NOTE on the histograms: the eta efficiency denominator carries 892 sims per 300 events in
under/overflow (|eta| > 4.5) with ZERO matches in every configuration. The headline eff
divides by 22633 while the three bands sum to 21741, so band numerators must be divided by
22633 -- not by the band sum -- to convert into headline points. bandcounts.py reports raw
counts precisely so this cannot be got wrong by accident.

## THE CALIBRATION HYPOTHESIS THE BASELINE NUMBERS DICTATE (SUPERSEDED BY M4)
Per-region FINBASE vs LST: eff +.0010 / +.0003 / -.0010, dup +.0336 / +.0217 / -.0045,
fake +.0243 / +.0245 / -.0008. EVERY dup and fake excess is barrel + transition; the
ENDCAP is already BETTER than LST on both. So the calibration move -- if it exists -- is
to spend barrel/transition selectivity (tighten -XCT there) and buy it back in the endcap
(loosen -XCT3 / -AT33). Whether that is a real regional effect or just re-labelled global
movement is exactly what the ledger + the frontier comparison must decide: a per-region
setting only earns its extra constants if it beats the GLOBAL -XCT frontier at matched
duplicate rate, not merely the single baseline point.

