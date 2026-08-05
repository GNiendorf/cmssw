# P2.4b-1 -- feasibility + physics recon for the pT3-class replacement

MEASURE ONLY. No class flip, no deletion, no default change. Everything below is
flag-gated and default OFF.

## M0 -- recon complete (docs + code + training artifacts read)

### Eligibility definition (from the M16 design, prototype/PixelAttach.h:107-119)
A BARE T3 is a T3 row that is NOT a member of ANY K9-ACCEPTED chain.
 - the mask is built AFTER arbitration, against the ACCEPTED set, not the welded set:
   a T3 welded into a chain that K9 REJECTED is still bare and still attachable. That is
   precisely the pT3-class population LST recovers with its superbin machinery.
 - NO other filter. In particular t3_partOfPT3 / t3_partOfPT5 T3s STAY in the universe,
   because those are exactly the tracks the general attach is meant to deliver itself.
 - Stage B bids only for pLS that stage A (chain targets) left UNOWNED; one pLS one
   owner holds globally across both stages. Per-class margin: -AT3 (frozen 6.0) instead
   of -a (frozen 6.875).
 - Production universe == the dense chain-node set (ChainNodesSoA), which K0 builds by
   compacting EVERY triplet of EVERY lower module (src/alpaka/ChainGraph.h:99-123), so
   it is the same universe as the ntuple's t3_* loop.

### Head fitness -- ANSWERED FROM THE ARTIFACTS (deliverable 5, first half)
The SHIPPING head is r2, generated from
standalone/fanout5/attachretrain/attach_mlp_r2.pt (see the provenance header of
src/alpaka/AttachNetworkWeights.h). NOTE: P2_PORT_MAP.md line 480 still names
attach_mlp_g1.pt -- that is STALE; production ships r2.

r2's training dump (fanout5/attachretrain/ar_dump.sh) was taken with `-PDT 2`
= BOTH target kinds, at the M19 STACK operating point, 798 events, 75.5M pairs,
bare-T3 fakes downsampled 1-in-64 with wgt=64 (unbiased), chain pairs kept whole.

r2 held-out test AUC (attach_r2_testauc.json):
  ALL                     0.99854   (3.15M true / 6.58M sampled fake)
  CHAIN targets (tt0)     0.99410   (5976 true / 1.26M fake)
  BARE-T3 targets (tt1)   0.99855   (3,140,303 true / 5.31M sampled fake)
    tt1 prompt vxy<1      0.99680   (153,824 true)
    tt1 disp vxy [1,5)    0.99658   (6,536 true)
    tt1 disp vxy [5,10)   0.99741   (635 true)
    tt1 disp vxy >=10     n/a       (ZERO true pairs -- the M18 training gap)
=> bare-T3 targets ARE in the training distribution, heavily, and separably.
   Caveat to carry: r2's MODEL SELECTION used `--val-metric chain` (best epoch by
   CHAIN-pair AUC only, best_epoch=7). Calibration of the logit SCALE on tt1 is
   therefore not something the training guarantees; it has to be measured. That is
   task 5's second half and is measured below.

## M1 -- stage-B measurement build LANDED (flag-gated, default OFF)
New: src/alpaka/ChainAttachT3.h (all stage-B kernels; ChainAttach.h NOT modified),
LSTEvent::attachBareT3Probe (runs LAST inside attachPixels, after every delivery decision of
the event is taken and consumed; reads plsOwned into a scratch copy, never writes the live one),
and write_lst_ntuple.cc setBareT3AttachBranches (bt3_* branches, sim-matched with the identical
matcher and the identical > 0.75 rule that produces tc_simIdx).
Env knobs (all default OFF): LST_CHAIN_T3ATTACH, LST_CHAIN_T3_THETA, LST_CHAIN_T3_AUDIT,
LST_CHAIN_T3_HIST, LST_CHAIN_T3_DTANL, LST_CHAIN_T3_DPHI.

## M2/M3 -- FIRST NUMBERS (PU200RelVal, CPU, -s 1, frozen config, evts 0-2)

| evt | nodes(all T3) | bare T3 | pLS | grid cand | scored (== window pairs) | >= -AT3 6.0 | picks | attached |
|-----|---------------|---------|-----|-----------|--------------------------|-------------|-------|----------|
| 0   | 35 420        | 32 693  |20931| 12 717 920| 4 390 517                | 22 337      | 3 515 | 463      |
| 1   | 37 151        | 34 505  |20718| 11 851 734| 4 223 537                | 21 621      | 3 232 | 447      |
| 2   | 38 604        | 36 011  |21669| 13 281 736| 4 714 400                | 21 157      | 3 191 | 398      |

GRID SUPERSET AUDIT, bare-T3 targets (LST_CHAIN_T3_AUDIT=1):
```
[CHAIN K8B AUDIT] evt=0 bareT3=32693 pLS=20931 exactPairs=4390517 gridCand=12717920 gridPass=5760360 MISSING=0 supersetHolds=YES
[CHAIN K8B AUDIT] evt=1 bareT3=34505 pLS=20718 exactPairs=4223537 gridCand=11851734 gridPass=5422485 MISSING=0 supersetHolds=YES
[CHAIN K8B AUDIT] evt=2 bareT3=36011 pLS=21669 exactPairs=4714400 gridCand=13281736 gridPass=6059762 MISSING=0 supersetHolds=YES
```
MISSING = 0 on every event AND `scored` == `exactPairs` exactly on every event: the grid plus the
multi-cell duplicate suppression returns the analytic-window set EXACTLY, not merely a superset.

STAGE-B CPU COST (frozen windows, evt 0): pre 2.1 ms | grid 10.4 ms | score 407.8 ms |
contend 1.0 ms == ~421 ms/evt. Against pT3's measured 47.2 ms/evt (P2.6d sweeps.out, hybrid s=1)
and the 161.1 ms/evt it would cost unsubsidised, this is 2.6x OVER the number it has to beat.
The cost is NOT the grid (12.7M probes instead of 32693 x 20931 = 684M, a 54x cut, 10 ms); it is
the 4.39M analytic-window survivors that each need a 19->24->24->1 head evaluation.

## M4/M5 -- PHYSICS DUMP, 60 events, probe mode, frozen -AT3 6.0
```
would-be pT3-class deliveries :    471.8 /evt   real 416.4 /evt   purity 0.8825
LST pT3-class TC rows         :    150.0 /evt   real 145.3 /evt   purity 0.9688
(a) LST pT3 rows with a matched sim : 145.3 /evt
    ... same sim delivered by us    : 131.3 /evt (0.9035)
    ... and on the SAME T3          :  64.7 /evt (0.4450)
(b) our sims already delivered by some LST TC : 386.72 /evt
    our sims delivered by NO LST TC           :  12.42 /evt
(c) band        denom   LSTany   LSTpT3  pT3only     ours  keepOnly  gainNew
    vxy [0,1)    6786   0.5519   0.0393   0.0391   0.1779       241       46
    vxy [1,5)     628   0.3806   0.0223   0.0175   0.1035         7        2
    vxy [5,10)    318   0.3836   0.0126   0.0126   0.0220         2        0
    vxy [10,30)   935   0.3733   0.0000   0.0000   0.0000         0        0
```
=> 90.4% of LST's real pT3 rows are reproduced by sim; 91% of the pT3-ONLY sims are kept;
   +12.4 sims/evt that no LST TC delivers at all. BUT 82% of our deliveries are for sims some
   other LST TC already delivers -- the structural duplicate source.

## M5b -- HEAD CALIBRATION on bare-T3 pairs (LST_CHAIN_T3_HIST, 60 events)
5.56M scored pairs/evt, 39.9k bare-T3 targets/evt. Cumulative pair yield:
logit >= 0: 206k/evt | >= 6.0: 25.9k | >= 6.875: 18.7k | >= 8: 11.2k | >= 10: 1.27k | >= 12: 13
Per-target BEST logit percentiles: p50 = 1.97, p75 = 3.98, p95 = 8.06, p99 = 9.80.
Targets with best >= 6.0: 5075/evt; >= 8: 2079; >= 10: 310.
=> the head DOES separate on bare-T3 pairs (r2 test AUC 0.99855 on tt1), but the margin that is
   right for chain targets is FAR too loose for a 3-layer target: 5075 of 39.9k targets clear it.

## M6 -- TASK 6 (headline): the REPLACEMENT config, and the DUP FINDING
Built LST_CHAIN_T3REPLACE (default OFF): replacePT3 = true (ChainCompactCarriedTCs retires every
carried LST type-5 row, mirroring -RT5 1), stage B moved BEFORE the carried-row retirement and
publishing its ownership, and the owners emitted as type-5 rows after the chain rows.

5-event smoke, frozen -AT3 6.0, frozen (unfiltered) target universe:
  eff (pt>0.9)  0.8105 -> 0.8130   (+0.0025)
  fake (pt>0.9) 0.0377 -> 0.0531   (+0.0154)
  DUP (pt>0.9)  0.0530 -> 0.2862   (+0.2332)   <-- BLOCKING
The chains already deliver most of those sims; the M16 stage-B rule has NO hit-level contention
with the delivered chain TCs, so every bare T3 lying along a track a chain already delivered
delivers it a second time.

NEW LEVER, production-native and free: K9's hit-owner map is alive at attach time. Requiring at
most C of the target T3's six hits to be already CLAIMED by an accepted chain is the exact
analogue of the chain claim tolerance. Measured (evt 0, -AT3 6.0):
  C = 6 (frozen) : 32595 targets  4.38M scored  447 delivered  410 ms
  C = 3          : 23740 targets  3.22M scored  250 delivered  299 ms
  C = 1          : 15243 targets  2.10M scored  154 delivered  191 ms
  C = 0          : 13856 targets  1.91M scored  147 delivered  173 ms
LST's own pT3 count is 150/evt, so C = 0..1 reproduces the class SIZE exactly, and the cost lands
at the 161.1 ms/evt that an unsubsidised LST pT3 stage would cost.
300-event scoreboard in flight: b0 r60 r60c3 r60c1 r60c0 r80c1 r80c0 r100.

NO-OP GATE -- PASSED. p24b_ref/run_noop.sh, 30 PU200RelVal events, --use_chain_tracking, every
new env variable unset, against p26d_ref/bin_prod_pristine (the PRISTINE-tree binary P2.6d pinned
from the same HEAD this session started on):
```
branches compared : 33
values compared   : 1198892
mismatches        : 0
RESULT: BIT-IDENTICAL
```

## M7 -- 300-event scoreboard, FIRST TWO LEGS (b0 baseline, r60 frozen config)
`p24b_ref/bd_*.json` via createPerfNumDenHists + prototype/compare_ab.py (the P2.4 gate-b harness).

| metric | LST flag-OFF | b0 (current) | r60 (replace, -AT3 6.0, unfiltered) | delta vs b0 |
|---|---|---|---|---|
| eff (pt>0.9)   | 0.8136 | 0.8130 | 0.8140 | **+0.0010** |
| eff vxy [0,1)  | 0.8473 | 0.8461 | 0.8472 | +0.0011 |
| eff vxy [1,5)  | 0.7731 | 0.8028 | 0.8007 | -0.0021 |
| eff vxy [5,10) | 0.6530 | 0.7271 | 0.7303 | +0.0032 |
| eff vxy [10,30)| 0.6291 | 0.7173 | 0.7181 | +0.0008 |
| eff dxy [0,1)  | 0.8342 | 0.8404 | 0.8414 | +0.0010 |
| eff dxy [1,5)  | 0.4989 | 0.5601 | 0.5590 | -0.0011 |
| eff dxy [5,10) | 0.2281 | 0.2491 | 0.2491 | 0.0000 |
| eff dxy [10,30)| 0.0470 | 0.0256 | 0.0256 | 0.0000 |
| eff barrel     | 0.9242 | 0.9247 | 0.9226 | -0.0021 |
| eff transition | 0.8826 | 0.8814 | 0.8899 | +0.0085 |
| eff endcap     | 0.7556 | 0.7543 | 0.7551 | +0.0008 |
| **dup (pt>0.9)**  | 0.0513 | 0.0519 | **0.2829** | **+0.2310** |
| dup barrel     | 0.0097 | 0.0137 | 0.2910 | +0.2773 |
| dup transition | 0.0130 | 0.0186 | 0.3625 | +0.3439 |
| dup endcap     | 0.0846 | 0.0821 | 0.2514 | +0.1693 |
| **fake (pt>0.9)** | 0.0455 | 0.0464 | **0.0667** | **+0.0203** |
| fake barrel    | 0.0437 | 0.0515 | 0.0824 | +0.0309 |
| fake transition| 0.0459 | 0.0541 | 0.0848 | +0.0307 |
| fake endcap    | 0.0463 | 0.0413 | 0.0514 | +0.0101 |
| mean nhitOT    | 6.514  | 6.380  | 6.442  | +0.062 |
| mean nhitOT barrel | 10.151 | 9.898 | 9.319 | -0.579 |
| mean nhitOT trans  | 10.010 | 9.888 | 9.151 | -0.737 |
| mean nhitOT endcap |  3.559 | 3.468 | 3.845 | +0.377 |
| n TC           | 610581 | 614277 | 712788 | +98511 (+16%) |

VERDICT ON THE FROZEN CONFIG: efficiency is a WASH (+0.0010 overall, every band within
+-0.009), while dup rate goes 0.052 -> 0.283 (5.5x) and fake 0.046 -> 0.067 (+44%). The
replacement at the frozen -AT3 with the frozen (unfiltered) target universe is a clear NO.

## M7b -- WHY, and why the obvious fix does not work
The 60-event probe table says it: 82% of our 472 deliveries/evt are for sims some OTHER LST TC
already delivers. A bare T3 lying along a track a chain already delivered gets a pLS attached and
delivers that track a SECOND time. The M16 stage-B rule has no hit-level contention with the
delivered chain TCs -- LST's own pT3 gets that from CrossCleanpT3.

The obvious production-native fix (require the target T3's six hits to be UNCLAIMED by any
accepted chain -- K9's owner map, free, alive at attach time) FAILS ON PHYSICS, and the reason is
structural: a bare T3 that duplicates a chain-delivered track IS a real track, so its hits ARE
claimed; a bare T3 whose hits are unclaimed is preferentially JUNK. Measured at -AT3 6.0
(60-event probe, C = 1): deliveries 472 -> 57/evt, purity 0.88 -> 0.63, reproduction of LST's
real pT3 rows 90% -> 6%. The filter removes the duplicates AND the signal, and makes the class
DIRTIER. It is a cost lever (410 -> 191 ms/evt), not a physics lever.

Measured properly (48-event probe, C = 1, and the 300-event board leg r60c1):
  deliveries 472 -> 63.3/evt | purity 0.8825 -> 0.5839 | reproduction of LST's real pT3 90.4% ->
  5.8% | pT3-only sims recovered 241/265 -> 15/218
  300-event board: eff (pt>0.9) 0.8130 -> 0.8086 (-0.0044), dup 0.0519 -> 0.0640 (+0.0121),
  fake 0.0464 -> 0.0607 (+0.0143). WORSE THAN b0 ON EVERY AXIS. The claim filter is rejected.

## M8 -- STAGE-B COST, CPU and GPU (measured, per event)
CPU (AMD EPYC 9654, serial accelerator, -s 1, evt 0):
| maxClaimed | targets | scored pairs | delivered | pre | grid | score | contend | TOTAL |
|---|---|---|---|---|---|---|---|---|
| 6 (frozen) | 32595 | 4.38M | 447 | 2.1 | 10.2 | 409.6 | 0.9 | **422.8 ms** |
| 3          | 23740 | 3.22M | 250 | 2.5 | 10.2 | 298.6 | 0.3 | **311.6 ms** |
| 1          | 15243 | 2.10M | 154 | 2.1 | 10.1 | 191.2 | 0.2 | **203.6 ms** |
| 0          | 13856 | 1.91M | 147 | 2.0 | 10.0 | 173.2 | 0.2 | **185.4 ms** |

GPU (NVIDIA L40, -s 1, mean of 10 events):
| maxClaimed | pre | grid | score | contend | TOTAL |
|---|---|---|---|---|---|
| 6 (frozen) | 0.13 | 0.15 | 4.3 | 34.3 | 38.9 ms |
| 1          | 0.13 | 0.14 | 2.0 | 2.4  |  4.7 ms |
The GPU `contend` figure is a MEASUREMENT-SCAFFOLD ARTIFACT, not a property of the design: I wrote
stage-B contention as one serial kernel (an O(nOwners^2) selection sort), which is the form P2.6a
already replaced for stage A with the parallel rank kernel ChainAttachRDRank. Ported the same way
it would be O(0.1 ms). The REAL GPU cost of the bare-T3 attach is pre+grid+score = **4.6 ms/evt**
at the frozen universe. GPU affordability is a non-issue.

Like-for-like CPU comparison the maintainer asked for: LST's pT3 stage costs 161.1 ms/evt
UNSUBSIDISED (P2.6d sweeps.out, `preview` s=1 -- the 47.2 ms hybrid figure is only reachable while
pT5 pre-claims). So the swap is +262 ms/evt at the frozen universe, +43 ms at maxClaimed=1, and
+24 ms at maxClaimed=0. On GPU the swap is a clear WIN once contention is parallelised.

## M9 -- THE CONTROL THAT REFRAMES EVERYTHING: r9990 (drop LST's pT3, deliver NOTHING)
300 events, replacePT3 = true with the class margin set unreachable (-AT3 999), so stage B scores
but never delivers. This isolates what LST's pT3 class is worth INSIDE the chain-tracking config.

| metric | b0 (pT3 carried) | r9990 (pT3 dropped, nothing delivered) | delta |
|---|---|---|---|
| eff (pt>0.9)   | 0.8130 | 0.7743 | **-0.0387** |
| eff vxy [0,1)  | 0.8461 | 0.8056 | -0.0405 |
| eff barrel     | 0.9247 | 0.8552 | -0.0695 |
| eff transition | 0.8814 | 0.8416 | -0.0398 |
| eff endcap     | 0.7543 | 0.7426 | -0.0117 |
| dup (pt>0.9)   | 0.0519 | 0.0525 | +0.0006 |
| fake (pt>0.9)  | 0.0464 | 0.0490 | +0.0026 |
| n TC           | 614277 | 579968 | -34309 |

**THE pT3 CLASS IS LOAD-BEARING: 3.9 ABSOLUTE POINTS of efficiency, 7 points in the barrel.**
It is not redundant with the chain pipeline and cannot simply be deleted. This also cross-checks
the 60-event probe table exactly: pT3-only sims were 3.91% of vxy[0,1) there, and the measured
eff loss here is 3.87 points.

Consequence for the framing: our general attach RECOVERS all of it (r60 eff +0.0010 over b0) --
the candidate finder, the head and the class-margin machinery all WORK. The single blocking defect
is that it delivers 472 rows/evt where LST delivers 150, and 82% of those are for sims already
delivered by another TC. The blocker is TC-LEVEL DUPLICATE CLEANING of the stage-B output, which
stage B currently has NONE of. LST gets it from CrossCleanpT3 + the pt3dnn score.

## M10 -- THE THRESHOLD SCAN, and why -AT3 is NOT a usable knob for this trade
300-event board, all deltas vs b0 (the current state):

| leg | -AT3 | maxClaimed | eff (pt>0.9) | dup (pt>0.9) | fake (pt>0.9) | n TC |
|---|---|---|---|---|---|---|
| b0    | -     | -  | 0.8130 (BASE) | 0.0519 (BASE) | 0.0464 (BASE) | 614277 |
| r60   | 6.0   | 6  | **+0.0010**   | **+0.2310**   | **+0.0204**   | +98511 |
| r60c3 | 6.0   | 3  | -0.0011       | +0.0846       | +0.0232       | +40271 |
| r60c1 | 6.0   | 1  | -0.0044       | +0.0121       | +0.0144       |  +9132 |
| r60c0 | 6.0   | 0  | -0.0052       | +0.0082       | +0.0127       |  +6590 |
| r110  | 11.0  | 6  | **-0.0375**   | +0.0118       | +0.0023       | -31149 |
| r9990 | inf   | 6  | -0.0387       | +0.0007       | +0.0027       | -34309 |

THE CURVE IS THE ANSWER. r110 (margin 11) recovers essentially NOTHING of the 3.87 points the
pT3 class is worth (-0.0375 of -0.0387), while r60 (margin 6) recovers ALL of it and costs
+0.231 dup. There is no intermediate point that buys efficiency cheaply, because the efficiency
comes from the SAME low-logit region that produces the duplicates: **on bare-T3 targets the r2
head's logit does not rank "delivers a sim nobody else delivers" above "delivers a duplicate".**
It ranks pair compatibility, which the duplicates also have (a duplicate of a real track IS a
compatible pair). The class margin is therefore the wrong instrument, and the same is true of the
claim filter (M7b). What is missing is TC-LEVEL DUPLICATE CLEANING, which stage B has none of.

## M11 -- FULL SCOREBOARD (p24b_ref/BOARD_TABLE.txt, 300 PU200RelVal events, CPU -s 32)
The complete table is in BOARD_TABLE.txt. The decisive rows, all deltas vs b0:

| leg | -AT3 | maxClaimed | eff (pt>0.9) | dup (pt>0.9) | fake (pt>0.9) | n TC | mean nhitOT |
|---|---|---|---|---|---|---|---|
| b0    | -    | - | 0.8130 BASE | 0.0519 BASE | 0.0464 BASE | 614277 | 6.380 |
| r60   | 6.0  | 6 | **+0.0010** | **+0.2310** | **+0.0204** | +98511 | +0.062 |
| r60c3 | 6.0  | 3 | -0.0011 | +0.0846 | +0.0232 | +40271 | +0.089 |
| r60c1 | 6.0  | 1 | -0.0044 | +0.0121 | +0.0144 |  +9132 | +0.103 |
| r60c0 | 6.0  | 0 | -0.0052 | +0.0082 | +0.0127 |  +6590 | +0.104 |
| r80   | 8.0  | 6 | -0.0142 | +0.1472 | (see table) | | +0.100 |
| r80c1 | 8.0  | 1 | -0.0182 | +0.0073 | +0.0025 | -16689 | +0.100 |
| r110  | 11.0 | 6 | -0.0375 | +0.0118 | +0.0024 | -31149 | +0.099 |
| r120  | 12.0 | 6 | -0.0385 | +0.0019 | +0.0027 | -33963 | +0.102 |
| r9990 | inf  | 6 | -0.0387 | +0.0007 | +0.0027 | -34309 | +0.102 |

THE FRONTIER, absolute values (eff / dup / fake at pt>0.9, unfiltered universe):
```
b0    (current)   0.8130 / 0.0519 / 0.0464     n TC 614277
-AT3  6.0         0.8140 / 0.2829 / 0.0667     n TC 712788
-AT3  8.0         0.7988 / 0.1991 / 0.0474     n TC 641909
-AT3 11.0         0.7755 / 0.0637 / 0.0487     n TC 583128
-AT3 12.0         0.7745 / 0.0538 / 0.0490     n TC 580314
deliver nothing   0.7743 / 0.0525 / 0.0490     n TC 579968
```
The dup rate does not return to baseline until the margin is high enough that the class delivers
essentially nothing. There is no knee.

Every displaced band is unmoved by the swap (eff vxy [5,10) +0.0032 at best, vxy [10,30) +0.0008
in EVERY leg including the deliver-nothing control -- the pT3 class contributes nothing there,
which is expected: a bare T3 plus a pixel seed is an IP-ish object). The whole trade lives in
prompt barrel/transition efficiency versus dup rate.

## M12 -- VERDICT: NO-GO on the flip as it stands
There is NO operating point of (-AT3, claim filter) at which the replacement beats the current
state. The frontier is: full efficiency recovery costs +0.231 dup, and any setting that keeps dup
near baseline throws away essentially all of the efficiency.

The reason is now MEASURED and specific, and it is NOT the grid, NOT the head's discrimination,
NOT the candidate volume and NOT the cost:
  - the grid is exact (MISSING = 0, `scored` == the analytic-window pair count on every event),
  - the r2 head IS trained on bare-T3 pairs and separates them well (test AUC 0.99855 on tt1),
  - the cost is 4.6 ms/evt on GPU and 185-423 ms/evt on CPU (vs the 161.1 ms LST's pT3 stage
    costs unsubsidised) -- affordable on GPU, at worst a wash on CPU at a tight target universe,
  - the efficiency is fully recoverable (r60 is +0.0010 over b0).
The single blocking defect is that STAGE B HAS NO TC-LEVEL DUPLICATE CLEANING. It delivers 472
rows/evt where LST delivers 150, and 82% of them are for sims another TC already delivered. The
class margin cannot fix it (the head ranks pair COMPATIBILITY, and a duplicate of a real track is
a perfectly compatible pair), and neither can the K9 claim filter (it removes the real
duplicates and the signal together, and makes the class dirtier: purity 0.88 -> 0.58).

## Milestones
- [x] M0 recon
- [x] M1 stage-B measurement build (flag-gated, default OFF)
- [x] M2 target inventory
- [x] M3 grid superset audit for bare-T3 -- PASSES, MISSING=0
- [x] M4 volume + timing (CPU + GPU)
- [x] M5 physics dump vs LST pT3 rows
- [x] M6 replacement config + no-op gate (BIT-IDENTICAL)
- [x] M7-M11 300-event A/B scoreboard + threshold/claim scans
- [x] M12 verdict: NO-GO, with the blocking defect isolated to stage-B dup cleaning

## M13 -- WHAT THE FLIP WOULD NEED (the conditions, if the maintainer wants a fan-out)
1. A stage-B OUTPUT DUP CLEAN. The missing piece is the analogue of CrossCleanpT3: a bare-T3
   delivery must lose to a DELIVERED chain TC (and to another bare-T3 delivery) on shared
   outer-tracker hits. Note this is NOT the K9 claim map (measured, rejected above): the claim
   map is a pre-delivery reservation over accepted chains, whereas what is needed is a
   post-assembly hit-overlap contention against the rows actually emitted, which is exactly the
   K6 mutual-best / -RD shape already ported twice in this codebase. Target: bring 472 rows/evt
   down to ~150 while keeping the 241/265 pT3-only sims.
2. Only if (1) lands: re-run this scoreboard. The gate is eff (pt>0.9) >= b0's 0.8130 with dup
   <= 0.052 and fake <= 0.047.
3. A head retrain is NOT indicated by anything measured here (see M0/M5b) and should not be the
   first move. If one is wanted later, the right objective is not pair compatibility but
   "delivers a sim no other TC delivers", which needs a NEW label, not a new architecture --
   size: one pair-dump pass at the current operating point (ar_dump.sh shape, ~1 h) plus a
   ~15 min train, but the label is the research, not the compute.
4. The GPU contention kernel of stage B needs the P2.6a parallel-rank treatment before any
   timing claim is made from it (34 ms of the measured 39 ms is that one serial sort).

## NOTE, not mine
`standalone/prototype/PixelAttach.{cc,h}` show as MODIFIED in git (an "M20 binned candidate
index" refactor, phiDirAtRadius extraction + an enforceWindows flag), timestamped 11:38/11:39
during this session. I did NOT touch prototype/ -- it is read-only for this task. Another agent
is writing to the same tree. Flagged so nobody attributes it here.

## Artifacts
- code: src/alpaka/ChainAttachT3.h (new), LSTEvent.{h,dev.cc} (probe + replacement wiring),
  standalone/code/core/write_lst_ntuple.{h,cc} (bt3_* branches). ALL default OFF.
- env knobs: LST_CHAIN_T3ATTACH, LST_CHAIN_T3REPLACE, LST_CHAIN_T3_THETA,
  LST_CHAIN_T3_MAXCLAIMED, LST_CHAIN_T3_MAXFAKE, LST_CHAIN_T3_DTANL, LST_CHAIN_T3_DPHI,
  LST_CHAIN_T3_AUDIT, LST_CHAIN_T3_HIST
- scripts: run_board.sh (scoreboard), run_noop.sh (no-op gate), run_phys.sh / run_phys2.sh
  (probe dumps), p24b_board.py, p24b_phys.py, p24b_hist.py
- pinned builds: bin_repl/ (CPU), bin_repl_cuda/ (CUDA), md5_bin_repl*.txt
- results: BOARD_TABLE.txt, bd_*.json, bd_*_cmp.txt, audit.log, phys*.root
