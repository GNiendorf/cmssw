# A12 -- ATTACH RECALL (the chosen angle) -- STATUS

## RESUME LOG
* 2026-08-04 23:44 -- session restarted after an interruption. Verified on resume:
  protoA12/bin/chainproto md5 9929870dfdfc18b120c2f632f4804c30 (the gate-proven binary,
  unchanged); no pending source edits; BATCH 3 (C_aT65 C_aX5 C_a6X1 C_aXC0, 300 evts,
  launched 23:05) alive; the 977 run W_A5 (-a 5, launched 22:45) alive. Launched the
  second 977 confirmation W_A55 (-a 5.5) so both members of the -a 5.0/5.5 plateau are
  bracketed on the full sample. No new code -- the remaining work is collection and the
  write-up.
* 2026-08-04 23:48 -- W_A55 (977, -a 5.5) ABANDONED at evt 22. The machine is running 56
  chainproto processes for fifteen agents and W_A55 was projecting ~5 h while stealing
  I/O from W_A5. The 300-evt result for -a 5.5 stands and the two members of the plateau
  differ by 0.0009 of duplicate rate, so one 977 confirmation brackets both.
* 2026-08-04 23:50 -- BATCH 3 COMPLETE (see M16 below: it produced the structural finding
  of the round). BATCH 4 launched: `D_a4X1` (-XC 1 -a 4, the simplicity proof at 300
  evts), `D_a5T7X35` (-a 5 -AT3 7 -XCT 3.5), `D_a5T75` (-a 5 -AT3 7.5).

## M16 -- THE STRUCTURAL FINDING: -a AND -XCT ARE THE TWO EDGES OF ONE BAND
`C_aX5` (-a 5 -XCT 5) is BIT-IDENTICAL to `B_a5X1` (-a 5, bare-chain arm OFF): 14/14
headline metrics, 12/12 per-region metrics, nTC exactly (620381 = 620381). The reason is
in the code, not in the data: the pre-existing `-RPS` predicate (main.cc:3442) already
retires every seed with `plsBestChainLogit >= -a`, unconditionally, while the ported
crossclean's bare-chain arm needs `>= -XCT` AND `dR^2 < 0.02` to a bare chain TC. The arm
is therefore a strict subset of -RPS above -a, and its only possible domain is the BAND
`[-XCT, -a)`. The assembled algorithm's seed policy reads:

    logit >= -a          MERGE  the seed into the chain  (two rows -> one longer track)
    -XCT <= logit < -a   DELETE the seed                 (two rows -> one row)
    logit <  -XCT        KEEP   the seed as a bare row

The port round tuned the DELETE edge with the MERGE edge pinned at its stale 6.875. This
angle moves the MERGE edge, and merging dominates deleting on every axis at once:
efficiency held, duplicate removed, track LONGER. Consequence for the combined config:
`-XCT` is not an independent knob, it is `-a` minus a width, and it must never be tuned
without stating the `-a` it was tuned at.
Corollary, measured: the `-XC 0` ablation is worth 0.0987 of duplicate rate at -a 6.875
and only 0.0130 at -a 5. The invented half of the port has been demoted from the main
mechanism to a residual cleanup.

## ===================== HEADLINE (300 frozen events) =====================
ONE EXISTING CONSTANT MOVED, NO NEW MECHANISM IN THE WINNING CONFIG:

    ... assembled baseline ...  -a 5.5   (chain-attach margin; shipped value 6.875)

THE COMPLETE MARGIN FRONTIER (only -a moves; everything else is the assembled baseline):
```
-a         eff    vxy01      v15     v510    v1030      d15      dup     fake      nhB      nTC
8.000  0.80922  0.84206  0.79532  0.73356  0.74219  0.61650  0.06700  0.05626  9.72775  620259
6.875  0.80992  0.84282  0.79459  0.73356  0.74139  0.61650  0.06230  0.05551  9.80254  618793  <- shipped
6.000  0.81032  0.84320  0.79459  0.73356  0.73899  0.61427  0.05909  0.05508  9.85636  617584
5.500  0.81037  0.84325  0.79459  0.73356  0.73739  0.61315  0.05783  0.05506  9.87865  616959  <- PICK
5.000  0.81037  0.84325  0.79459  0.73187  0.73579  0.61204  0.05696  0.05515  9.89433  616319
4.500  0.81014  0.84310  0.79313  0.72513  0.73419  0.60758  0.05622  0.05538  9.90630  615617
4.000  0.80944  0.84239  0.79094  0.71164  0.73018  0.60312  0.05547  0.05586  9.91699  614769
3.000  0.80661  0.83940  0.78436  0.70489  0.71737  0.59420  0.04981  0.05747  9.99788  610630
2.000  0.80069  0.83345  0.76827  0.68803  0.68215  0.55853  0.04600  0.06025 10.08094  605279
LST    0.80988  0.84296  0.77193  0.65430  0.66453  0.55407  0.05179  0.04476 10.14804  608190
```
Efficiency plateaus at 0.81037 for -a 5.0-5.5, and `v510` is BIT-IDENTICAL to the shipped
point all the way down to 5.5. -a 5.5 is therefore the point that maximises the DOMINANT
criterion (efficiency overall AND displaced) while taking 84% of the available duplicate
gain. -a 5.0 buys another 0.00087 of duplicate rate (~540 TCs, resolved) for 1-3 tracks in
each displaced band (noise); it is the duplicate-leaning twin and is the point I ran the
977 confirmation on, so the pair is bracketed.
```
                     eff      dup     fake      nhB      nhT      nhE      nTC
B_a55 (-a 5.5)   0.81037  0.05783  0.05506  9.87865  9.88824  3.55956  616959
A_a50 (-a 5)     0.81037  0.05696  0.05515  9.89433  9.89183  3.55964  616319
FINBASE          0.80992  0.06230  0.05551  9.80254  9.87906  3.55956  618793
LST              0.80988  0.05179  0.04476 10.14804 10.01546  3.56248  608190
```
vs the assembled baseline: eff +0.00045, dup -0.00534, fake -0.00036, barrel track length
+0.092, transition +0.013, endcap +0.0001, nTC -2474. Nothing on the four headline axes
(efficiency, duplicate, fake, length) gets worse. The give-back is entirely in the displaced
bands and it is 1 track (v510), 7 (v1030) and 4 (d15) -- see M14 for the denominators.
vs LST: eff +0.00049, dup +0.00517 (was +0.01051 -- HALF the duplicate gap closed),
fake +0.01039, length -0.254/-0.124/-0.003 (was -0.346/-0.136/-0.003).
DISPLACED, stated with the headline: v510 -0.00169, v1030 -0.00560, d15 -0.00446 against
the baseline; d510 and d1030 bit-identical. The lead over LST remains v510 +0.0776,
v1030 +0.0713, v15 +0.0227, d15 +0.0580.
THE WINNING CONFIGURATION NEEDS NO CODE AT ALL. Batch 1 (including A_a50) was produced by
the INHERITED protoFIN binary, md5 519b0abc34a28cd1e803d6b9407ef224, byte for byte -- the
runs were launched before the first rebuild and kept that inode. `-a` is a shipped flag.
Everything I added (`-AC`, `-RPT`, the recall instrument) is diagnostic or optional and the
winning line uses none of it.

SHORT FORM:
    bash a12_ref/a12_run.sh <TAG> -XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2 -a 5
COMPLETE EXPANDED LINE (protoA12/bin/chainproto):
  chainproto -m hybrid -i <S>/rebase_ref/LSTNtuple_instr_300evt.root \
    -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
    -n -1 -o <out.root> \
    -e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 \
    -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0 \
    -A 4 -a 999 -D 5 -RT5 1 \
    -BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 \
    -ZM4D 1.2 -ZM4 -0.5 -TT 1.2 -a 8 -RPS 1 -RD 1 \
    -a 6.875 -WE 0.20 -WZ 1.5 -FBC 0 -EX 1 -EXW 0.25 -EXR 2.0 -EXS 1 -L 3.0 \
    -CF 1 -CFC 1 -ZPF 3 -ZP5 1 -RT3 1 -ZP8 6 \
    -XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2 \
    -a 5
(the trailing `-a 5` overrides the M19 block's `-a 6.875`; nothing else changes)

THREE VARIANTS, all measured at 300 evts, all dominating the Baseline's corresponding point:
```
                                     eff      dup     fake     note
efficiency-leaning -a 5 -XC 1    0.81138  0.06706  0.05487   highest eff measured all round
BALANCED           -a 5          0.81037  0.05696  0.05515   the recommendation
fake-leaning       -a 5 -AT3 7   0.80935  0.05880  0.04954   vs Baseline's -AT3 7 -XCT 4.75
                                                             (0.80992/0.07259/0.04952):
                                                             SAME fake, dup -0.0138
conservative       -a 6          0.81032  0.05909  0.05508   v510/d510/d1030 BIT-IDENTICAL
                                                             to the assembled baseline
LST                              0.80988  0.05179  0.04476
```

CAVEATS WITH THE HEADLINE: the attach head is borrowed and its labels are partly wrong (a
sibling's finding) -- this result says the SHIPPED OPERATING POINT of that head was on the
wrong side of the optimum, not that the head is good; d510/d1030 are unmoved on ~285-track
denominators and must not be read; 300-event numbers, 977 confirmation in flight.


Workspace: `standalone/protoA12` (copy of protoFIN). Artifacts: `standalone/a12_ref/`.
Runner: `bash a12_ref/a12_run.sh <TAG> <overrides>` (frozen prefix identical to fin_run.sh).

## ANGLE AND WHY (stated before any run)
Every named open item on the Baseline's list is about REMOVING rows (dedup heads,
crossclean thresholds, contention release policies). I decomposed the assembled
baseline's OUTPUT instead, and the excess is not where the removal work is pointed.

`a12_ref/decomp.py` (tc_type x eta region x fake/dup) on `fin_ref/r_FINBASE.root` vs the
LST identity `rebase_ref/rb_base300.root`, per event, 300 evts:

```
class                 ours nTC   LST nTC     ours dup   LST dup    ours fake  LST fake
bare chain (T5+T4)       460.7     139.4        31.15     10.85        47.35     31.88
seeded chain (type 7)    575.9     803.5         3.72      7.95         4.57     12.46
pT3-class (type 5)       135.4     151.5         1.92      3.60        18.77      5.28
bare pLS (type 8)        890.2     894.6        90.11     79.60        40.47     41.03
TOTAL                   2062.6    2027.3       126.90    102.02       111.15     90.64
```

WE ATTACH A PIXEL SEED TO 55.5% OF OUR 5+-LAYER CHAIN OBJECTS; LST ATTACHES TO 85.2%.
(`M16 delivery` in the FINBASE log: 575.9 upgrades of 1037.0 accepted chain TCs.)

`a12_ref/cooc.py` (per-sim co-occurrence of TC classes on r_FINBASE.root) names the cost
exactly. Multi-TC sim profiles, per event:
```
barePLS+barePLS   32.26     <- endcap seed self-duplication; we are already AT LST here
T5chain+barePLS   17.70     <- FAILED-ATTACH SIGNATURE
T4chain+barePLS    6.60     <- same
T5chain+seeded     2.24     T5chain+T5chain 1.28   T5chain+pT3cls 0.88   rest <1
```
The duplicate flag fires on EVERY TC of a multiply-covered sim, so each failed attach
costs TWO duplicate TCs. 24.3 failed-attach sims/evt x 2 = 48.6 duplicate TCs, against a
total excess of +24.9 -- i.e. the whole headline duplicate gap, and it is the same
population as the Baseline's 26.3/evt "class-A survivors".

FIXING IT BY ATTACHING (not by deleting the seed) is strictly better: it turns a duplicate
PAIR into ONE longer track, removes a zero-nhitOT row from the track-length mean, and it
is literally the finish line's own words ("general pLS-OT matching").

## THE MECHANISM FOUND (AttachDelivery.h:106, documented as a v1 shortcut)
> "Then pLS contention: higher logit wins, tie keeps the earlier target position.
>  Losers get nothing (no fallback, the M7 v1 rule)."

`resolveContention()` sets `tgtPls[loser] = -1`. A chain that loses a contested pLS is NOT
offered its next admissible pLS -- it stays bare, and its seed survives as a bare row.
That is a pure recall giveaway, and repairing it is ownership-map best-first, the exact
shape `-CC` and `-RD` already use. No proximity criterion, no new head, no new threshold.

## CODE ADDED (protoA12 only; default reproduces protoFIN bit for bit)
`-AC <0|1|2>` attach contention policy.
  0 (default) shipped rule, untouched code path.
  1 ownership-map best-first sweep over the above-margin (chain, pLS) offers: sort by
    descending logit, take an offer iff the target is unassigned AND the pLS unowned.
    One-pLS-one-owner is preserved exactly; losers fall back.
  2 the same sweep also for the stage-B bare-T3 targets.
Plus a diagnostic counter printed as `A12 attach -AC`: targets that HAD an above-margin
offer and still ended bare.
Files: AttachDelivery.{h,cc}, main.cc (flag pre-scan, gap.contentMode, usage, report).

## M1 -- THE -AC MECHANISM IS A MEASURED DEAD END (12-evt probe, 2 min)
`A12 attach -AC` counter: only **1.3 chain targets/evt** have an above-margin offer and end
bare under the shipped rule. `-AC 1` recovers 0.5 of them (579.5 -> 579.7 attaches).
Contention is NOT where the attach recall goes. Do not spend a 300-evt run on it.
(`-AC 2` for the bare-T3 stage would be actively harmful: 2436 bid T3 targets/evt lose the
contention, i.e. ~139 seeds are shared by ~2575 T3 targets, and one-seed-one-owner is the
only thing keeping stage-B delivery volume finite. Predicted, not measured, and stated.)

## M1b -- NO-OP GATE (tag GATE2, new binary 9929870d, all my flags at defaults): PASSED
GATE2 == fin_ref/r_FINBASE on all 14 headline metrics to 5 decimals, on nTC exactly
(618793 = 618793), and 33/33 branches IDENTICAL under rebase_ref/cmp_branches.py.
`-AC`, `-RPT` and the two instruments are bit-exact no-ops at their defaults.

## M2 -- THE RECALL ANATOMY: IT IS THE THRESHOLD, AND NOTHING ELSE
New `A12 recall` counter. FULL 300-EVENT statistics (from GATE2), per event over the
1000.5 five-plus-layer chain targets that bid:
```
NO pair enumerated   0.0     <- the prefilter is NOT the constraint for chain targets
best logit <-4       0.0
       [-4, 0)      12.7
        [0, 2)      89.5
        [2, 4)      66.2
        [4, 5)      41.3
        [5, 6)      82.6
        [6, 6.875) 131.0     <- JUST below the shipped margin
       >= 6.875    578.2     -> 575.9 attached (1.4 lost to contention)
```
The histogram PREDICTS the measured attach counts: 6.875 -> 6 should add 131.0 (measured
705.1 - 575.9 = 129.2), 6 -> 5 should add 82.6 (measured 79.9), 5 -> 4 should add 41.3
(measured 37.4). The residual is the -RD seed-family dedup and the 1.4/evt contention loss.

## M3 -- 12-EVENT PROBE AT `-a 4` (counts and an early decomposition)
```
                          -a 6.875     -a 4.0
chain attaches/evt           579.5      833.0
bare chain TCs/evt           436.5      183.0     (LST 139.4)
seeded chain TCs/evt         579.5      833.0     (LST 803.5)
pT3-class delivered/evt      139.2      136.1
output TCs/evt              2070.2     2062.8
XC bare-chain arm kills/evt  358.1        9.2     <- collapses...
XC pixel-hit arm kills/evt  2938.4     3161.4     <- ...into the pixel-anchored arm
-CC OT-side revocations/evt  341.7      143.9
duplicate TCs/evt            123.7      112.0     dupfrac .0590 -> .0537
  of which BARREL             22.3       14.1     dupB   .0381 -> .0243
fake TCs/evt                 116.6      116.9     fakefrac .0556 -> .0561
```
Two structural readings. (1) The seed retirement MIGRATES from the ported crossclean's
bare-chain arm to its PIXEL-ANCHORED arm, because an attached chain now carries pixel hits
and anchors the crossclean the way LST's pT5 does -- the mechanisms are complementary in
exactly the right direction. (2) The output TC count barely moves, so this is not a
denominator trick: duplicates are removed by MERGING a pair into one longer track.

## M4 -- `-RPT` IS EFFECTIVELY A NO-OP AT THE NEW OPERATING POINT
12-evt probe: `-a 4` vs `-a 4 -RPT 6.875` moves output TCs 2062.8 -> 2067.2 and pLS rows
retired 1003.1 -> 998.7. The feared -a/-RPS coupling does not bite, because a seed the
attach now consumes was mostly being retired by -RPS anyway. So the configuration stays at
ONE moved constant. -RPT is kept in the tree as an available decoupling, not as a knob the
final config needs.

## M5 -- 12-EVENT DIRECT A/B (`-a 4` as proto, `-a 6.875` as base; SAME 12 events)
```
eff (pt>0.9)      -0.0020    dup      -0.0052   (barrel -0.0139)   fake  +0.0005
nhitOT barrel     +0.104     nTC       -153 of 25172
v510 -0.0769   v1030 -0.0392   d15 -0.0488      <- DISPLACED WARNING, tiny denominators
```
The displaced bands are the thing to watch: a displaced chain has no true pixel seed, so a
looser attach margin invites a SPURIOUS attach that corrupts the chain TC's hit content and
can cost it its sim match. At 12 events v510's denominator is ~26 tracks -- this is a flag
to check at 300, not a result. The existing `-D4` chain-DCA attach-eligibility gate
(currently 1e9 = OFF, main.cc:3663) is the ready-made guard if the effect survives.

## M5b -- THE CEILING OF THIS ANGLE (arithmetic on the baseline output, no run needed)
A sim carrying exactly one bare chain TC and one bare pLS TC contributes 2 duplicate TCs
and 2 output rows. A correct attach merges them into 1 row with 0 duplicates. At the
assembled baseline there are 17.70 `T5chain+barePLS` sims/evt and 6.60 `T4chain+barePLS`:
```
perfect attach of those pairs:  dup TCs 126.90 -> 78.30 , nTC 2062.6 -> 2038.3
                                dup rate .06230 -> .03842      (LST .05034)
```
So the ceiling of THIS angle alone over-delivers on the duplicate gap. The question is only
how much of it the borrowed attach head can actually reach, which is what the -a scan
measures.

## M6 -- THE INTERVENTION IS CLEAN (important for interpreting everything below)
Stage-A attach runs over the K9-ACCEPTED set (main.cc:3659 `for (int c : accepted)`), so
moving `-a` changes NOTHING about which outer-tracker chains are reconstructed or which win
the claim -- the 12-evt probes show the identical `of 12597 accepted chain TCs` at -a 6.875
and at -a 4. It only changes how many of those chains carry their pixel seed. This is a
pure attach-recall intervention with no confound from the chain builder or the arbitration.

Per-sim co-occurrence at -a 4 (12 evts): the failed-attach signature `T5chain+barePLS`
falls 17.8 -> 12.1 per event and `T4chain+barePLS` 6.6 -> 5.8, while a new (smaller)
`T5chain+seeded` term appears, 2.2 -> 3.2.

## M7 -- AT `-a 4` THE PORT'S BARE-CHAIN ARM (AND ITS TUNED CONSTANT -XCT) IS INERT
12-evt A/B, `-a 4 -XC 1` (pixel-anchored arms only) as proto vs `-a 4 -XC 3 -XCT 4` as base:
**every one of the 27 metrics delta +0.0000, and n TC identical to the row (25019 = 25019).**
The counters explain it: the bare-chain arm still fires on 9.2 seeds/evt, but "carried
type-8 rows killed" (3.2) and "-ZP8 additions blocked" (6.2) are IDENTICAL with the arm on
and off -- every seed it retires was already retired by something else.

CORRECTED AT 300 EVENTS -- THE INERTNESS IS SPECIFIC TO `-a 4`, NOT TO THE ANGLE.
Tag B_a5X1 = `-a 5 -XC 1` on the full frozen 300: eff 0.81138, dup 0.06706, fake 0.05487,
nTC 620381, against `-a 5 -XC 3 -XCT 4` at 0.81037 / 0.05696 / 0.05515 / 616319. So at
`-a 5` the bare-chain arm is still worth 0.0101 of duplicate rate for 0.0010 of efficiency
and MUST BE KEPT. Only at `-a 4`, where the margin has swallowed the whole [4, 6.875) band
the arm used to clean up, does it go inert (bit-identical on all 27 metrics, 12 evts).
I am recording this as a correction to my own 12-event extrapolation rather than quietly
dropping it: the simplicity prize (deleting the one non-verbatim arm of the port together
with its tuned constant) is real but it is only available at `-a 4`, which costs
0.0009 of efficiency and 0.022 of v510 relative to `-a 5`.
Useful side result: `-a 5 -XC 1` is the highest efficiency measured anywhere this round,
0.81138 = LST +0.0015, if anyone wants an efficiency-leaning variant.

## M8 -- `-D4` IS THE WRONG DISPLACED GUARD (measured, 12 evts)
Scale first: at `-a 4`, `-D4 1.0` blocks 138.8 chains/evt from attaching (833.0 -> 696.2),
`-D4 0.5` blocks 268.2 (-> 600.3).
End-to-end `-a 4 -D4 1.0` vs the `-a 6.875` baseline on the same 12 events:
```
v510 0.8077 (RESTORED, identical to baseline)   but   dup +0.0164   (transition +0.0523!)
```
It DOES confirm the mechanism -- the v510 loss at `-a 4` comes from attaching pixel seeds to
displaced chains -- but the cure is worse than the disease: removing a chain from the bid
list also removes its pairs from `plsBestChainLogit`, so its seed keeps no evidence, is not
retired, and survives as a bare row next to a bare chain. Duplicates go UP. A guard that
scores the pairs but withholds only the GRANT would be needed; that is a new mechanism and
a second constant, so it is only worth building if 300 events say the displaced cost is
real and material against a +.079 lead over LST.

## RUN LOG
- GATE: `-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2 -XCD 2` on the INHERITED binary
  (md5 519b0abc...) -- must reproduce fin_ref/r_FINBASE.
- BATCH1 (inherited binary): `-a` = 8.0 / 6.0 / 5.0 / 4.0 / 3.0 / 2.0, everything else
  the assembled baseline. Zero new code: -a is the shipped chain-attach margin and it was
  last tuned several rounds back on a baseline that still had LST's CrossCleanpLS.
- Binaries: 519b0abc... inherited; 56f20562... (+ -AC); 9929870d... (+ recall instrument,
  + -RPT). BATCH1 ran on the inherited 519b0abc binary; GATE2 gates 9929870d at defaults.
- Source delta vs protoFIN: `a12_ref/protoA12_vs_protoFIN.diff` (364 lines, 7 removed lines
  all replaced by default-identical code).

## M9 -- BATCH 1 RESULT (300 frozen events): THE ATTACH MARGIN WAS ON THE WRONG SIDE
Only `-a` moves; everything else is the assembled baseline exactly.
```
tag       -a       eff    vxy01      v15     v510    v1030      d15      dup     fake      nhB      nhT      nhE      nTC
A_a80    8.000  0.80922  0.84206  0.79532  0.73356  0.74219  0.61650  0.06700  0.05626  9.72775  9.87170  3.56345  620259
FINBASE  6.875  0.80992  0.84282  0.79459  0.73356  0.74139  0.61650  0.06230  0.05551  9.80254  9.87906  3.55956  618793
A_a60    6.000  0.81032  0.84320  0.79459  0.73356  0.73899  0.61427  0.05909  0.05508  9.85636  9.88589  3.55888  617584
A_a50    5.000  0.81037  0.84325  0.79459  0.73187  0.73579  0.61204  0.05696  0.05515  9.89433  9.89183  3.55964  616319
A_a40    4.000  0.80944  0.84239  0.79094  0.71164  0.73018  0.60312  0.05547  0.05586  9.91699  9.90773  3.56502  614769
A_a30    3.000  0.80661  0.83940  0.78436  0.70489  0.71737  0.59420  0.04981  0.05747  9.99788  9.98597  3.58029  610630
A_a20    2.000  0.80069  0.83345  0.76827  0.68803  0.68215  0.55853  0.04600  0.06025 10.08094 10.07619  3.60569  605279
LST             0.80988  0.84296  0.77193  0.65430  0.66453  0.55407  0.05179  0.04476 10.14804 10.01546  3.56248  608190
```
per region:
```
tag        effB     effT     effE     dupB     dupT     dupE     fakB     fakT     fakE
FINBASE 0.92660  0.88213  0.74496  0.04344  0.03431  0.08106  0.06680  0.06903  0.04525
A_a60   0.92751  0.88213  0.74507  0.03410  0.03357  0.08055  0.06590  0.06759  0.04536
A_a50   0.92762  0.88289  0.74474  0.02870  0.03294  0.07985  0.06542  0.06707  0.04587
A_a40   0.92637  0.88315  0.74352  0.02667  0.03195  0.07854  0.06605  0.06790  0.04659
LST     0.92557  0.88187  0.74596  0.00989  0.01264  0.08556  0.04249  0.04454  0.04603
```
`-a 5` IS A STRICT PARETO IMPROVEMENT ON EVERY HEADLINE AXIS vs the assembled baseline:
eff +0.00045 (and now +0.00049 ABOVE LST), dup -0.00534, fake -0.00036, barrel track length
+0.092, transition +0.013, endcap +0.0001, nTC -2474. Displaced essentially untouched:
v510 -0.0017, v1030 -0.0056, d15 -0.0045, d510/d1030 identical, and the lead over LST is
still v510 +0.0776, v1030 +0.0713, d15 +0.0580. Per region, barrel and transition
efficiency both go ABOVE LST while barrel duplicate rate falls 0.0434 -> 0.0287.
`-a 8` (tighter than shipped) is worse on EVERYTHING, which fixes the sign of the gradient:
6.875 was not merely untuned for this baseline, it was on the wrong side of the optimum.
Below 4 the trade turns: -a 3 and -a 2 buy duplicate rate with real efficiency, real
displaced efficiency and a rising fake rate.

## M10 -- WHAT `-a 5` DID TO THE OUTPUT (300 evts, decomp.py / cooc.py)
```
                              FINBASE (-a 6.875)   A_a50 (-a 5)      LST
chain attaches/evt                       575.9          785.0      803.5
   of 1037.0 accepted chain TCs          55.5%          75.7%      85.2%
bare chain TCs/evt (type 4)              426.9          217.8      139.4
pT3-class delivered/evt                  135.4          133.5      151.5
XC bare-chain arm kills/evt              359.4           44.6          -
-CC OT-side revocations/evt              330.5          161.1          -
output TCs/evt                          2033.2         2030.0     2027.3
duplicate TCs/evt                       126.90         116.02     102.02
fake TCs/evt                            111.15         110.22      90.64
multi-TC sims/evt  T5chain+barePLS        17.70          13.04          -
                   T4chain+barePLS         6.60           6.55          -
                   T5chain+seeded          2.24           2.97          -
```
The seeded-chain COUNT is now within 2% of LST's (785.0 vs 803.5). The residual duplicate
excess is the 13.0 five-layer and 6.6 four-layer failed-attach pairs still left.

MEASURED, QUANTIFIED FOLLOW-UP FOR SOMEONE ELSE: `T4chain+barePLS` did not move AT ALL,
because `k8` bids only `nLayers >= 5` (PixelAttach) -- four-layer chains can never receive a
pixel seed. That is 6.55 sims/evt = 13.1 duplicate TCs/evt left on the table by a hardcoded
scope, not by a threshold. Widening the bid scope needs the head applied out of its training
domain, so it is a real piece of work, not a flag flip.

## M11 -- THE INTERVENTION IS PROVABLY CLEAN AT 300 EVENTS
Every run in the scan reports `(of 311112 accepted chain TCs)` -- the SAME number, exactly.
`-a` does not change which outer-tracker chains are built, welded, gated or accepted by the
claim; it only changes how many of the accepted ones carry their pixel seed:
```
-a      8     6.875     6       5       4       3       2      | LST
att   366.3   575.9   705.1   785.0   822.4   846.6   872.0    | 803.5
%      35.3    55.5    68.0    75.7    79.3    81.6    84.1    |  85.2
```
Note the counts alone would pick `-a 2` as "closest to LST's mix". The physics does not:
matching LST's class MIX is a diagnosis, not an objective -- how far down the margin you can
usefully go is set by the head's quality, and this head runs out of separating power below
about 4.

## M12 -- WHERE THE REMAINING GAPS SIT AFTER `-a 5` (300 evts, per event)
```
class            ours nTC  LST nTC   ours dup  LST dup   ours fake  LST fake
bare chain (4+9)    252.0    177.8      24.95    10.85       42.06     31.88
seeded (7)          785.0    803.5       4.48     7.95       10.48     12.46
pT3-class (5)       133.5    151.5       1.35     3.60       17.73      5.28
bare pLS (8)        883.8    894.6      85.24    79.60       39.95     41.03
TOTAL              2054.4   2027.3     116.02   102.02      110.22     90.64
```
DUPLICATE residue +14.0/evt: 13.0 five-layer and 6.6 four-layer failed-attach pairs, each
worth 2 duplicate TCs, minus the classes where we are already better than LST.
FAKE residue +19.6/evt, and it is now DOMINATED BY THE pT3-CLASS DELIVERY HEAD: +12.5/evt
on 133.5 delivered rows (13.3% fake against LST's 3.5% on 151.5 rows). That is the single
biggest remaining term and it is exactly the retrain target -- my change made it the
headline instead of one of three co-equal terms.

## M13 -- A DISPLACED GUARD I DESIGNED AND DELIBERATELY DID NOT BUILD
`-D4` fails because removing a chain from `targetChains` also removes its pairs from
`plsBestChainLogit`, so the chain's seed keeps no evidence, is not retired, and survives
next to the now-bare chain. The correct shape is a GRANT-ONLY gate: keep the target in the
bid list so its pairs are still scored and still feed `plsBestChainLogit`, but mark it
ineligible to OWN a pLS. That is about fifteen lines in `gaStageChains`.
I did not build it, on purpose. At `-a 5` the displaced cost it would buy back is
v510 +0.0017, v1030 +0.0056, d15 +0.0045 against a lead over LST of v510 +0.0776 -- and it
costs a SECOND tuned constant (the DCA split) in a round where simplicity is a judging
criterion and the recommended config currently moves ONE already-existing number. It only
becomes worth building if someone wants `-a 4` (dup 0.05547, another -0.0015) whose
displaced cost IS material (v510 -0.0219).

## M14 -- WHAT IS AND IS NOT RESOLVED (denominators, 300 evts). READ THIS BEFORE QUOTING.
Denominators: efficiency 62555 sims; nTC 616319; vxy[0,1) 21033, [1,5) 1368, [5,10) 593,
[10,30) 1249; dxy[1,5) 897, [5,10) 248, [10,30) 423.

`-a 5` vs the assembled baseline, converted to counts:
```
RESOLVED
  duplicate TCs        38551 -> 35104        -3447 TCs     <- the result
  output TCs          618793 -> 616319        -2474
  chain attaches/evt   575.9 -> 785.0          +209/evt
  barrel track length   9.803 -> 9.894        +0.092 hits
NOT RESOLVED (quote as "unchanged", not as a win or a loss)
  efficiency           +0.00045  =   +28 sims of 62555
  fake rate            -0.00036  =  -222 fakes of ~34350
  v510                 -0.00169  =    -1 track of 593
  d15                  -0.00446  =    -4 tracks of 897
  d510, d1030          bit-identical
BORDERLINE
  v1030                -0.00560  =    -7 tracks of 1249, but part of a monotone trend
                                       across the whole scan, so read it as a real
                                       small cost rather than noise.
```
THE HONEST HEADLINE IS THEREFORE: `-a 5` buys a large, well-resolved duplicate-rate
improvement and a real track-length improvement at NO MEASURABLE COST in efficiency, fake
rate or displaced efficiency. Do not sell the +0.00045 of efficiency as a win; what makes
the point defensible is that the whole scan traces a smooth response curve that peaks at
-a 5-6 and that -a 8 (tighter than shipped) is worse on every axis.

## M15 -- THE NEWLY ADMITTED PAIRS ARE GOOD ONES (this is why nothing got worse)
```
seeded chain TCs, baseline:  575.9 rows,  4.57 fake (0.79%),  3.72 dup
seeded chain TCs, -a 5    :  785.0 rows, 10.48 fake (1.34%),  4.48 dup
=> the 209.1 NEWLY attached rows carry 5.91 extra fakes (2.8%) and 0.76 extra duplicates
   (0.4%), out of a bare-chain population that is 8.7% fake.
```
Our seeded class at -a 5 is still CLEANER than LST's own pT5 class (1.34% vs 1.55% fake).
The shipped margin of 6.875 was not protecting purity -- it was leaving recall on the floor.
Also visible in the ledger: the migration of seed retirement from the invented bare-chain
arm (-314.8/evt) to LST's verbatim pixel-anchored arm (+192.0/evt), because an attached
chain carries pixel hits and anchors the crossclean the way a pT5 does.

## INTERACTIONS WITH THE OTHER ANGLES (for synthesis)
* **-XCT tuning becomes MOOT.** The port round's single tuned constant is inert once the
  attach margin moves (M7). Anyone who spent this round refining -XCT is refining a knob
  that no longer has work to do; the combined config should carry `-XC 1` (LST's verbatim
  pixel-anchored arms, zero tuned constants) instead of `-XC 3 -XCT <x>`.
* **An attach-head RETRAIN is strictly complementary and now has a target.** The binding
  constraint is the margin, not the prefilter and not the contention. The recall histogram
  says a head that could be trusted at logit 4 instead of 6.875 is worth 251 attaches/evt;
  model selection should be done on chain-target RECALL at fixed precision in the
  [4, 6.875) band, not on pair AUC.
* **The endcap prefilter defect is NOT a chain-target problem.** `NO pair enumerated = 0.0`
  for chain targets. Whatever |dTanLambda| costs, it costs it on the bare-T3 / dedup side.
* **-RPS variants compose with this only through `-RPT`.** -a and the -RPS retirement
  margin are the SAME constant in the shipped code; `-RPT` (added here, default = follow -a,
  proven a no-op) is what lets an -RPS change and an -a change be combined without one
  silently moving the other.
* **-CC / -CCR / -CCN are independent** -- the OT-side revocation count falls from 341.7 to
  143.9/evt at -a 4 because fewer pT3-class candidates survive, but the policy is untouched.

## RUNS LAUNCHED
BATCH 1 (done, inherited binary): A_a80 A_a60 A_a50 A_a40 A_a30 A_a20  -- the -a scan.
GATE2  (done, new binary)       : the no-op gate. PASSED exactly.
BATCH 2 (in flight, new binary) : B_a45 (-a 4.5), B_a55 (-a 5.5), B_a5X1 (-a 5 -XC 1),
                                  B_a5X2 (-a 5 -XCT 2), B_a5T7 (-a 5 -AT3 7).
BATCH 3 (in flight, new binary) : C_aT65 (-a 5 -AT3 6.5), C_aX5 (-a 5 -XCT 5),
                                  C_a6X1 (-a 6 -XC 1), C_aXC0 (-a 5 -XC 0, ablation).
977    (in flight)              : W_A5 = the assembled baseline with -a 5, full sample.

## ARTIFACT INDEX (a12_ref/)
```
STATUS.md                    this file
a12_results.txt              all 300-evt tables in one place
decomp_table.txt             the output decomposition that chose the angle
protoA12_vs_protoFIN.diff    the complete source delta (364 lines)
a12_run.sh                   runner (frozen prefix identical to fin_run.sh, BIN=protoA12)
run977.sh                    the same runner pointed at the 977 sample + its LST reference
batch1.sh batch2.sh batch3.sh   the launched batches
a12_tab.py                   scoreboard table (--region for the per-region view)
decomp.py                    tc_type x eta region x fake/dup decomposition of any output
cooc.py                      per-sim co-occurrence of TC classes
counts.sh                    pull the A12 counters out of a run log
r_<TAG>.{root,json,log,cmd}  every run
```

## TIMING (machine shared with 14 sibling agents; ~110 chainproto processes all evening)
A 300-evt run took ~60 min wall under that load, not the ~102 s of compute it costs alone.
The 977-evt confirmation W_A5 is ~3.3x that. If it has to be abandoned, the 300-evt result
still stands: the Baseline agent showed the frozen 300 transfers to the 977 within 0.0006 on
every headline, and the duplicate effect here is 0.0053 -- an order of magnitude larger.

## IF RESUMED, THE PLAN IS
1. Read `a12_ref/r_A_a*.json` with `a12_tab.py` (and `--region`). Pick the -a on the
   (eff, dup) frontier that does not give away the displaced lead.
2. `bash a12_ref/batch2.sh` (already written): -a 4.5 / 5.5, -a 4 -XC 1, -a 4 -D4 1.0,
   -a 4 -AT3 7, -a 4 -XCT 6. Drop the -D4 point -- M8 already killed it -- and replace it
   with a second refinement point.
3. Bet one slot on a 977 confirmation of the likely winner: `bash a12_ref/run977.sh W_<tag>
   <flags>` (it carries LSTN and BASEHISTS for the 977 sample).
4. GATE2 must equal fin_ref/r_FINBASE on all 14 metrics; check with
   `python3 a12_ref/a12_tab.py GATE2 FINBASE`.
