# A04 -- PROMPT EFFICIENCY -- STATUS

Workspace: `standalone/protoA04` (copy of protoFIN, binary md5 519b0abc... at copy time).
Artifacts: `standalone/a04_ref/`. Runner: `a04_ref/a04_run.sh <TAG> <overrides>` (same frozen
prefix as fin_run.sh; the assembled-baseline overrides are NOT baked in, pass them).
Frozen binary copy for parallel batches: `a04_ref/chainproto_base`.

## M0 -- SETUP (DONE)
protoA04 = `cp -a protoFIN protoA04`; binary md5 519b0abc34a28cd1e803d6b9407ef224 == protoFIN.

## M1 -- NO-OP GATE: PASSED (bit-identical, two links)
`a04_ref/cmp_prefix.py` (entry-prefix variant of rebase_ref/cmp_branches.py, needed because
the gate runs are 30 events and FINBASE is 300):
  * `r_G0_30` (protoA04's COPIED protoFIN binary, assembled baseline, 30 evts)
    vs the first 30 entries of `fin_ref/r_FINBASE_ND.root` : **33 IDENTICAL, 0 DIFFER**
  * `r_G2_30` (REBUILT binary carrying the A04 source edit, same flags)
    vs `r_G0_30` : **33 IDENTICAL, 0 DIFFER** (rebase_ref/cmp_branches.py)
So protoFIN -> protoA04(copy) -> protoA04(rebuilt) is bit-exact at defaults.
`GATE300` (300 evts, copied binary) is the full-length confirmation. NOTE: the first
attempt at a 300-evt gate was killed by the machine at evt 208 (all 15 explorers are
running; load average 146) -- relaunched, not a code problem.

## M2 -- THE INSTRUMENT: `a04_ref/a04_diff.py` (per-sim efficiency decomposition)
Compares two output files sim row by sim row (both writers copy sim_* verbatim, so rows
align). Reproduces the SCOREBOARD headline efficiency exactly -- that number is
compare_ab's `eff_overall_incut`, i.e. sum_all (incl. over/underflow) of the
`TC_base_0_0_ef_*_eta` set, whose denominator is

    sim q != 0 AND sim_pt > 0.9 AND |sim_vz| < 30 AND sqrt(vx^2+vy^2) < 2.5   (NO eta cut)

numerator sim_tcIdx >= 0. VERIFIED: 18331/22633 = .80993 against r_FINBASE_hists.root, and
the per-region rows reproduce .92660 / .88213 / .74496 exactly.

## M3 -- THE DECOMPOSITION OF THE BASELINE (FINBASE vs LST, frozen 300)   *** HEADLINE ***
```
denominator 22633 (75.4/evt)     eff ours .80992   eff LST .80988   delta +.00004
BOTH 18102 | LSTONLY (we lose) 228 | OURSONLY (we gain) 229 | NEITHER 4074
CHURN = 457 sims = 2.49% of our numerator
```
THE NET IS ZERO BUT THE CHURN IS NOT. We are not "at parity by delivering the same tracks";
we lose 228 tracks LST finds and find 229 LST misses. The prompt-efficiency headroom is the
228, not the +.00004.

WHICH LST OBJECT DELIVERS THE 228 WE LOSE (x eta region):
```
LST type    total   barrel  transi  endcap
pT3            88       50      27      11
pT5            65       20      31      14
pLS            63       16       9      38
T5             10        2       6       2
T4              2        0       1       1
------------------------------------------
total         228       88      74      66
```
WHICH OF OUR ROWS DELIVERS THE 229 WE GAIN:
```
our type    total   barrel  transi  endcap
T5 (chain)    105       41      47      17
pLS            63       25      16      22
pT3 (attach)   37       24       6       7
pT5 (attach)   22        7       5      10
T4              2        0       1       1
```
Everything is prompt: vxy[0,1) 217 of 228 losses, dxy[0,1) 228 of 228.
pt: the losses are flat-ish in pt; we are NET POSITIVE above 4 GeV (+12, +10) and NET
NEGATIVE below 4 (-8, -5, -8).

## M7 -- BATCH 1 RESULT: THE MECHANISM ATTRIBUTION OF THE 228   *** HEADLINE 2 ***
Each row = the assembled baseline with ONE mechanism relaxed; "recov" = how many of the
reference's 228 lost sims that variant covers, "givenback" = how many of the 229 gains it
loses. 300 evts.
```
variant  change            eff      dup     fake | recov givenback  net | recovered by LST type
D_CC0    -CC 0         0.81209  0.28578  0.06554 |   85      16    +69 | pT5:46 pT3:35 pLS:3
D_XC0    -XC 0         0.81333  0.16098  0.05436 |   44       0    +44 | pLS:15 pT5:15 pT3:14
D_RPS0   -RPS 0        0.81182  0.07661  0.05536 |   38       0    +38 | pLS:24 pT5:8 pT3:6
D_XCT6   -XCT 6        0.81222  0.10306  0.05443 |   27       0    +27 | pT3:10 pLS:9 pT5:8
D_AT34   -AT3 4        0.80352  0.05721  0.09853 |   21       9    +12 | pT3:15 pT5:3 pLS:3
D_MAX    all four off  0.80524  0.36942  0.22253 |  153      31   +122 | pT3:57 pT5:52 pLS:42
FINBASE  (reference)   0.80992  0.06230  0.05551 |    -       -      - |
LST                    0.80988  0.05179  0.04476 |
```
FOUR READINGS.
1. **THE OT CONTENTION IS THE BIGGEST PROMPT-EFFICIENCY DESTROYER, not the crossclean.**
   `-CC 1 -CCN 1` costs 85 of the 228 (37%), against 44 for the whole ported CrossCleanpLS
   and 38 for -RPS. It is concentrated in TRANSITION (35) and BARREL (28) -- exactly where
   the residual gap lives -- and what it kills is pixel-backed deliveries (pT5-class 46 +
   pT3-class 35). `-CCN 1` means "any ONE shared MD kills"; a 3-MD delivery sharing a
   single MD with a DIFFERENT track's TC is not a duplicate, it is a crossing.
2. **-AT3 IS ALREADY PAST ITS EFFICIENCY PEAK GOING DOWN.** -AT3 4 LOSES .0064 of
   efficiency and gains .043 of fake while dup FALLS to .0572. Looser pT3-class delivery
   destroys efficiency because a wrong attach consumes the pLS and -RPS then retires the
   seed's carried type-8 row: the delivery neither matches nor leaves the row that did.
   The pT3-class margin is bounded from BELOW by seed destruction, not only by fake.
   (The Baseline scanned 6..9; the peak is at or just below the shipped default 6.)
3. **A THIRD OF THE LOSS IS STRUCTURAL.** With every retirement off and -AT3 3, D_MAX still
   only recovers 153 of 228: 75 sims (33%) are not reachable by any relaxation of these
   mechanisms. That is consistent with the offline finding that 157 of the 228 have NO TC
   of ours within dR 0.02 of the LST TC that delivered them.
4. **-RPS 0 is the cheapest efficiency in the retirement set** (+.0019 eff for +.0143 dup,
   7.5 dup per eff) and it is an ENDCAP/pLS effect (24 of its 38 recoveries are LST-pLS
   rows, 27 of 38 are endcap). -XCT 6 is 17 dup per eff. -CC 0 is 71 dup per eff.

## M8 -- BATCH 4 [running]: -CCN 2 WITH -CCR 2 HAS NEVER BEEN MEASURED
The Baseline's "-CCN 1 beats -CCN 2 on both dup and fake" was measured in its batches 1-3,
i.e. BEFORE `-CCR 2` was discovered in M5/M6; every point on its published frontier is
-CCN 1 -CCR 2. Since the contention is the biggest efficiency destroyer and -CCR 2 changes
what a revocation costs, the combination is re-opened here:
  F_N2 (-CCN 2), F_N2X3 (-CCN 2 -XCT 3), F_N2X2 (-CCN 2 -XCT 2)   [dup payback]
  F_AT35 / F_AT355 (locate the -AT3 efficiency peak)
  F_MRIL (-MRI -1.5, the IP-5+ chain acceptance rescue = the prompt CHAIN knob)

## M9 *** THE RESULT: `-a` (THE CHAIN-CLASS ATTACH MARGIN) IS MIS-SET, AND LOWERING IT
##     IMPROVES EFFICIENCY, DUPLICATE RATE, FAKE RATE AND TRACK LENGTH AT ONCE ***
`-a` was frozen at 6.875 by the M19 round, BEFORE wholesale pT5 replacement (-RT5 1), the
pT3 stage, -CC and the ported -XC existed. Nobody re-scanned it. It is a THREE-way lever
exactly as -AT3 turned out to be: it sets (i) which accepted chains are upgraded to
pixel-backed type-7 TCs, (ii) which pLS become owned (and are therefore unavailable to
stage B), and (iii) the -RPS "lost a contention above -a" predicate that retires a seed's
carried type-8 row. 300 evts, ONLY -a moved:
```
-a        eff      dup     fake      nhB      nTC     effB     dupB     fakB
1e9   0.80833  0.07045  0.05751   9.6759   621083  0.92443  0.06786  0.06917   (no chain attach)
8     0.80922  0.06700  0.05626   9.7278   620259  0.92557  0.05773  0.06818
6.875 0.80992  0.06230  0.05551   9.8025   618793  0.92660  0.04344  0.06680   <- THE BASELINE
6     0.81032  0.05909  0.05508   9.8564   617584  0.92751  0.03410  0.06590
5     0.81037  0.05696  0.05515   9.8943   616319  0.92762  0.02870  0.06542
LST   0.80988  0.05179  0.04476  10.1480   608190  0.92557  0.00989  0.04249
```
MONOTONE in all four judged quantities down to ~6, saturating in efficiency between 6 and 5
(+.00005 = one sim). The mechanism is not subtle: a chain-class attach both DELIVERS a
pixel-backed row and RETIRES the seed's bare type-8 row, so every extra good attach removes
a duplicate row and lengthens a track. -a 8 / 1e9 show the other direction is strictly
worse on everything.

CHOSEN POINT `-a 6` (300 evts) vs the assembled baseline:
  eff  .81032 (+.00040)   dup .05909 (-.00321)   fake .05508 (-.00043)
  nhitOT 9.856/9.886/3.559 (+.054/+.007/-.001)   nTC 617584 (-1209)
  per region effB +.00091 effT 0 effE +.00011 | dupB -.00934 dupT -.00074 dupE -.00051
             fakB -.00090 fakT -.00144 fakE +.00011
  vs LST: eff +.00044 | dup +.00730 (was +.01051) | fake +.01032 (was +.01075)
DISPLACED COST, stated with the headline and in TRACKS because the denominators are small
(vxy[10,30) 1249, dxy[1,5) 897, vxy[5,10) 593 over 300 evts):
  -a 6 : v1030 -.0024 = -3 tracks, d15 -.0022 = -2 tracks, v510 and v15 UNCHANGED.
  -a 5 : v1030 -.0056 = -7 tracks, d15 -.0045 = -4 tracks, v510 -.0017 = -1 track.
We remain +.0740 (v1030) / +.0776 (v510) / +.0578 (d15) ahead of LST at -a 6, so this is
~3% of the displaced lead, but it is a real one-directional drift and -a 5 doubles it.
That, plus the efficiency saturation, is why the recommendation is 6 and not 5.

## M4 -- BATCH 1 (diagnostic ablations, 300 evts, binary copy) [done]
  D_MAX   -XC 0 -RPS 0 -CC 0 -AT3 3   (delivery ceiling: nothing retired, everything delivered)
  D_XC0   -XC 0
  D_CC0   -CC 0
  D_RPS0  -RPS 0
  D_AT34  -AT3 4
  D_XCT6  -XCT 6
Purpose: intersect each run's recovered set with the 228 to attribute each lost sim to a
mechanism (seed retirement / OT contention revoke / delivery margin).

## M5 -- SOURCE EDIT: the general-attach PREFILTER WINDOWS are now flags
`-PW <v>` absolute |dTanLambda| half-width (default 0.6 = frozen), `-PWR <f>` FRACTIONAL
tolerance -- effective half-width max(-PW, f*|targetTanLambda|), default 0 = frozen and
bit-exact -- and `-PWP <v>` |dPhiAtInnermost| (default 0.4 = frozen). Three files:
PixelAttach.h (one field), PixelAttach.cc (effDTanLWindow + the two use sites: evalPair and
the binned index QUERY range, so the finder stays a superset), main.cc (parse + wire into
gap.pref and the pairdump apre + usage). Binary 7bb9189dbbb1bba616d1458c17f43996.
Motivation: the Baseline's open item 4 -- the absolute 0.6 window is a SHRINKING angular
window as |eta| rises (tanLambda ~ sinh eta) and feeds pT5-class delivery, pT3-class
delivery AND the -XC bare-chain arm.

## M6 *** THE PREFILTER IS NOT BINDING ON DELIVERY -- OPEN ITEM 4 IS CLOSED ***
One event (run 1 lumi 46 evt 4560), assembled baseline, only the window changed:
```
flags        prefiltered pairs   chain-attached   T3-attached   seed-family revoked
(frozen)           2 258 995            471            110              538
-PWR 0.3           2 550 385            471            110              538
-PWP 0.8           4 409 731            471            110              538
-PW  2.0           6 558 465            471            110              538
```
Tripling the candidate volume -- including making the tanLambda window fractional, which
is exactly the endcap fix item 4 asks for -- changes ZERO attach decisions, and the whole
per-event delivery line (pixKept/chains/theta/pixdrop/claim/chainTC/upgT5/delivT3/supp) is
character-identical at 30 events too. At the shipped margins (-a 6.875, -AT3 6) the head
never selects a pair from the extra volume: the binding constraint is the HEAD, not the
candidate finder. The only channel left for the windows is -XC (it reads ga.pairLog at the
looser -XCT 4), i.e. duplicate rate, not efficiency.

## M10 -- BATCH 4 CLOSED: `-CCN 2` STAYS DOMINATED EVEN WITH `-CCR 2`, AND `-AT3 6` IS THE PEAK
The re-opened question from M8 is settled. 300 evts, only the named flag(s) moved off the
assembled baseline:
```
tag       change              eff      dup     fake      nTC
F_N2      -CCN 2          0.81037  0.08550  0.06286   631131
F_N2X3    -CCN 2 -XCT 3   0.80842  0.08065  0.06298   628688
F_N2X2    -CCN 2 -XCT 2   0.80555  0.07733  0.06304   626458
F_AT35    -AT3 5          0.80771  0.06015  0.06957   626508
F_AT355   -AT3 5.5        0.80930  0.06134  0.06113   621995
F_MRIL    -MRI -1.5       0.80984  0.06230  0.05601   619061
E_FS40    -FS 0.40        0.80970  0.08241  0.06300   630104
E_FS60    -FS 0.60        0.81001  0.09505  0.06977   638789
GATE300   (baseline)      0.80992  0.06230  0.05551   618793
E_A6      -a 6            0.81032  0.05909  0.05508   617584
```
* `-CCN 2` buys .00045 of efficiency for +.0232 dup and +.0074 fake, and -XCT cannot buy the
  dup back (at dup .0773 it is already .0044 BELOW the baseline in efficiency). The
  Baseline's `-CCN 1` verdict survives the `-CCR 2` discovery. CLOSED, do not re-measure.
* `-AT3` peaks AT the shipped default 6: 5.5 and 5 both lose efficiency AND gain fake.
* `-MRI -1.5` (chain IP acceptance) and `-FS` (relaxing the chain-side dedup) are dead ends:
  no efficiency, large dup/fake.
* Only `-a` moves efficiency, duplicate rate and fake rate the SAME way at once.

## M11 -- THE ATTRIBUTION OF THE `-a` GAIN (a04_attr.py against the reference 228)
```
variant   recov givenback net | by LST type                 | by region
-a 6         17       1   +16 | pT5:9  pT3:4  pLS:4         | barrel:9 transi:5 endcap:3
-a 5         29       2   +27 | pT5:19 pT3:6  pLS:4         | barrel:15 transi:12 endcap:2
-a 8          3       3    +0 | pLS:2  pT5:1                | endcap:2 barrel:1
-a 1e9        5       3    +2 | pLS:3  pT5:2                | endcap:5
-CC 0        85      16   +69 | pT5:46 pT3:35 pLS:3 T5:1    | transi:35 barrel:28 endcap:22
```
What `-a 6` recovers is PIXEL-BACKED barrel/transition tracks -- the same population and the
same regions as the residual gap. Per-sim recount at `-a 6`: LSTONLY 219 (was 228),
OURSONLY 229 (unchanged); within OURSONLY the chain-T5 rows fall 105 -> 97 and the
pT5-class attach rows rise 22 -> 30, i.e. the SAME sims are delivered by a longer,
pixel-backed row instead of a bare chain. That is why length and duplicate rate improve
together.

## M12 -- BATCH 5 [-a 4.5 / 5.5 / 6.25, and -a 6 -XCT 4.5] and BATCH 6 [-a 4,
##          -a 6 -XCT 3.5, -a 5 -XCT 3.5, -a 6 -AT3 7, -a 6 -AT3 6.5] LAUNCHED
Purpose: (i) close the -a frontier, (ii) test the DOMINANCE claim -- at matched duplicate
rate does moving -a beat moving -XCT? -- (iii) the -a x -AT3 interplay, since -AT3 is the
only knob that touches fake and fake is the largest remaining gap.
W_A6 (`-a 6` on the full 977) is running.

## M13 *** THE `-a` FRONTIER (300 evts, ONLY -a moved off the assembled baseline) ***
```
-a        eff      dup     fake      nhB      nhT      nhE      nTC   |  v1030    d15
1e9   0.80833  0.07045  0.05751   9.6759   9.8635   3.5692  621083  | 0.74219 0.61650
8     0.80922  0.06700  0.05626   9.7278   9.8717   3.5635  620259  | 0.74219 0.61650
6.875 0.80992  0.06230  0.05551   9.8025   9.8791   3.5596  618793  | 0.74139 0.61650  <- BASELINE
6.25  0.81032  0.05989  0.05516   9.8432   9.8837   3.5584  617906  | 0.73899 0.61427
6.00  0.81032  0.05909  0.05508   9.8564   9.8859   3.5589  617584  | 0.73899 0.61427  <- CHOSEN
5.50  0.81037  0.05783  0.05506   9.8787   9.8882   3.5596  616959  | 0.73739 0.61315
5.00  0.81037  0.05696  0.05515   9.8943   9.8918   3.5596  616319  | 0.73579 0.61204
4.50  0.81014  0.05622  0.05538   9.9063   9.8986   3.5620  615617  | 0.73419 0.60758
LST   0.80988  0.05179  0.04476  10.1480  10.0155   3.5625  608190  | 0.66453 0.55407
```
Efficiency rises monotonically to a plateau at 5.5-5.0 and turns over at 4.5; duplicate
rate is monotone DOWN over the whole range; fake bottoms at 5.5; barrel track length rises
monotonically toward LST. Displaced drifts DOWN monotonically and that is the only cost.

## M14 *** `-a` STRICTLY DOMINATES `-XCT` AS A DUPLICATE-RATE KNOB ***
Compare the Baseline's published -XCT frontier (at -a 6.875) with the -a frontier
(at -XCT 4), at MATCHED duplicate rate:
```
dup ~.0592 :  -XCT 3.5 -> eff .80904      vs   -a 6   -> eff .81032    +.00128
dup ~.0569 :  -XCT 3.0 -> eff .80776      vs   -a 5   -> eff .81037    +.00261
dup ~.0623 :  -XCT 4.0 -> eff .80992 (the baseline)   -a 6.25 .81032 at dup .05989
```
And the two compose: `-a 6 -XCT 4.5` = eff .81076 / dup .06343 / fake .05496 beats
`-a 6.875 -XCT 4.5` = .81037 / .06675 / .05539 on ALL THREE. There is no -XCT setting at
which -a 6.875 is preferable to -a 6.

## M15 -- THE MECHANISM, FROM THE DELIVERY LEDGER (per event, -a 6.875 -> 6.00 -> 5.00)
```
type-7 in-place upgrades of accepted chains   575.9 -> 705.1 -> 785.0
pT3-class rows delivered                      135.4 -> 134.4 -> 133.5
ADDED bare type-8 rows (-ZP8)                  29.4 ->  26.6 ->  24.4
XC seed retirements, BARE-CHAIN arm            359.4 -> 156.9 ->  44.6
XC -ZP8 additions blocked                     123.9 ->  58.5 ->  20.9
OT-side (-CC) revocations                     330.5 -> 220.7 -> 161.1
pT3-class candidates before any dedup        1127.2 ->1046.1 ->1001.9
```
Lowering `-a` converts ~129 bare chain TCs per event into pixel-backed type-7 rows. Each
conversion (i) lengthens the track by the pixel hits, (ii) CONSUMES the pLS so its bare
type-8 row is never added -- which is a duplicate removed at the source rather than by a
dedup threshold -- and (iii) shrinks the work the ported CrossCleanpLS and the OT contention
have to do (the bare-chain XC arm falls 359 -> 157/evt). That is why one knob moves
efficiency, duplicate rate, fake rate and track length in the same direction: it removes
duplicates STRUCTURALLY (by consumption) instead of by a threshold, and thresholds are what
cost efficiency.

## M16 -- WHERE THE RESIDUAL DEFICIT LIVES AT `-a 6` (per-sim, 300 evts)
```
denom 22633   eff ours .81032   eff LST .80988   delta +.00044
BOTH 18111 | LSTONLY(loss) 219 | OURSONLY(gain) 229 | NEITHER 4074   CHURN 448 = 2.44%
region:  barrel +17 (80 lost / 97 gained)   transi +1 (74/75)   endcap -8 (65/57)
pt:      [0.9,1.2) -6   [1.2,2) -3   [2,4) -3   [4,10) +12   [10,inf) +10
```
The assembled algorithm is now NET POSITIVE in barrel and transition and net negative only
in the ENDCAP and only BELOW 4 GeV. At the baseline the endcap losses were dominated by
sims LST delivers with a BARE pLS row (37 of 66) -- i.e. the residual prompt deficit is a
SEED-RETENTION question (-RPS / -XC / -ZP8), not a delivery-head question. That is the
handoff to whichever explorer owns seed retirement; `-a` has taken the barrel/transition
part of the gap to positive and cannot reach the endcap pLS part.

## M17 -- BATCH 7 LAUNCHED (joint frontier): J_A6X5 (-a 6 -XCT 5), J_A55X45 (-a 5.5 -XCT 4.5),
##          J_A625X4 (-a 6.5). Batch 6 still running. W_A6 (977) still running.
Source delta of this workspace vs protoFIN recorded in `a04_ref/protoA04_vs_protoFIN.diff`
(3 files, 73 added lines: the -PW / -PWR / -PWP prefilter-window flags, all defaulting to
the frozen values and proven bit-identical at defaults in M1). EVERY physics run in this
report used `a04_ref/chainproto_base` = the UNEDITED protoFIN binary
(md5 519b0abc34a28cd1e803d6b9407ef224); only the M6 prefilter probes used the rebuilt one
(7bb9189dbbb1bba616d1458c17f43996). THE RECOMMENDATION NEEDS NO CODE CHANGE.

## M18 -- PER-SIM AND ATTRIBUTION ALONG THE `-a` FRONTIER (300 evts)
```
point                eff    delta    loss  gain | recov givenback | effB delta  effT delta  effE delta
-a 6.875 (base)  0.80992  +0.00004   228   229  |   -       -     |  +0.00103    +0.00025    -0.00100
-a 6.25          0.81032  +0.00044   219   229  |  13       1     |  +0.00171    +0.00051    -0.00078
-a 6.00          0.81032  +0.00044   219   229  |  17       1     |  +0.00193    +0.00025    -0.00089
-a 5.50          0.81037  +0.00049   218   229  |  23       2     |  +0.00216    +0.00051    -0.00111
-a 5.00          0.81037  +0.00049    -     -   |  29       2     |  +0.00205    +0.00102    -0.00122
-a 4.50          0.81014  +0.00027   222   228  |  31       2     |  +0.00182    +0.00102    -0.00155
-a 6 -XCT 4.5    0.81076  +0.00088   214   234  |  22       1     |  +0.00239    +0.00153    -0.00078
```
Every point recovers PIXEL-BACKED (LST-pT5 and LST-pT3) sims in BARREL and TRANSITION; the
recoveries never reach the endcap (2-3 sims at every setting). Below -a 5 the recovery keeps
growing but NEW losses grow faster (at -a 4.5 the loss count goes back UP to 222) -- the
efficiency turnover is real, not a threshold artefact.

## M19 -- BATCHES 6 AND 7 STILL RUNNING; W_A6 (977 evts) STILL RUNNING
Machine load 100-110 (all fifteen explorers). If this session is resumed and the
batches have landed, the outstanding tables are:
  batch6 : I_A40 (-a 4), I_A6X35 (-a 6 -XCT 3.5), I_A5X35 (-a 5 -XCT 3.5),
           I_A6T7 (-a 6 -AT3 7), I_A6T65 (-a 6 -AT3 6.5)
  batch7 : J_A6X5 (-a 6 -XCT 5), J_A55X45 (-a 5.5 -XCT 4.5), J_A625X4 (-a 6.5)
  W_A6   : `-a 6` on the full 977 -- THE FINAL NUMBER
The RECOMMENDATION does not depend on them: `-a 6` is already established on the 300 as a
strict improvement over the assembled baseline on efficiency, duplicate rate, fake rate and
track length simultaneously, with a stated displaced cost of 3 + 2 tracks.

## M20 -- THE DISPLACED COST OF `-a`, IN TRACKS (300 evts; denominators vxy[10,30) 1249,
##          dxy[1,5) 897, vxy[5,10) 593, vxy[1,5) 1368)
```
-a       v1030      d15      v510      v15    total displaced tracks lost
6.875   ref        ref       ref       ref     0
6.25     -3         -2         0         0    -5
6.00     -3         -2         0         0    -5
5.50     -5         -3         0         0    -8
5.00     -7         -4        -1         0   -12
4.50     -9         -8        -5        -1   -23
6 + XCT 4.5  -3     -2         0        +1    -4
```
At `-a 6` we still sit v510 +.0776 / v1030 +.0740 / d15 +.0578 AHEAD of LST, so the cost is
~3% of the displaced lead. It is one-directional and it accelerates below 5.5, which is the
reason the recommendation is 6 and not the efficiency-plateau point 5.5/5.

## M21 -- COMPLETE ATTRIBUTION TABLE (all variants measured this round, vs the reference 228)
```
variant             recov givenback net | recovered by LST type        | by region
-CC 0                  85      16   +69 | pT5:46 pT3:35 pLS:3 T5:1    | transi:35 barrel:28 endcap:22
-XC 0                  44       0   +44 | pLS:15 pT5:15 pT3:14        | barrel:23 transi:11 endcap:10
-RPS 0                 38       0   +38 | pLS:24 pT5:8  pT3:6         | endcap:27 barrel:6 transi:5
-a 4.5                 31       2   +29 | pT5:20 pT3:9  pLS:2         | barrel:16 transi:13 endcap:2
-a 5                   29       2   +27 | pT5:19 pT3:6  pLS:4         | barrel:15 transi:12 endcap:2
-XCT 6                 27       0   +27 | pT3:10 pLS:9  pT5:8         | barrel:15 transi:8 endcap:4
-CCN 2                 24       3   +21 | pT3:16 pT5:7  pLS:1         | endcap:8 barrel:8 transi:8
-a 5.5                 23       2   +21 | pT5:12 pT3:6  pLS:5         | barrel:13 transi:7 endcap:3
-a 6 -XCT 4.5          22       1   +21 | pT5:10 pLS:6  pT3:6         | barrel:11 transi:8 endcap:3
-AT3 4                 21       9   +12 | pT3:15 pT5:3  pLS:3         | barrel:14 transi:5 endcap:2
-a 6                   17       1   +16 | pT5:9  pT3:4  pLS:4         | barrel:9 transi:5 endcap:3
-AT3 5                 16       4   +12 | pT3:13 pT5:2  pLS:1         | barrel:10 transi:4 endcap:2
-a 6.25                13       1   +12 | pT5:8  pLS:3  pT3:2         | barrel:7 transi:4 endcap:2
-AT3 5.5               11       2    +9 | pT3:9  pT5:1  pLS:1         | barrel:6 transi:3 endcap:2
-FS 0.60               10       2    +8 | pT5:7  pT3:2  T5:1          | transi:5 barrel:5
-MRI -1.5               1       0    +1 | pT5:1                       | barrel:1
ALL FOUR OFF + -AT3 3 153      31  +122 | pT3:57 pT5:52 pLS:42 T5:2   | endcap:58 barrel:52 transi:43
```
THE STRUCTURAL FLOOR IS 75 SIMS (228 - 153): no relaxation of any retirement or delivery
margin reaches them. The two mechanism families are cleanly separated by REGION -- the
retirement family (-RPS) is ENDCAP/pLS, the delivery-margin family (-a, -AT3, -CC) is
BARREL/TRANSITION and pixel-backed. `-a` is the only member of either family that recovers
its share without paying duplicate rate; every other row on this table costs .014 to .28 of
duplicate rate for its recoveries.

## M22 -- HALF-SAMPLE STABILITY OF THE `-a 6` GAIN (300 evts split 150/150)
Numerator tracks (denominator 11191 / 11442):
```
              first 150   second 150   full 300
-a 6.875 base      9061         9270      18331
-a 6.00            9066         9274      18340
gain                 +5           +4         +9
```
The gain is present in BOTH halves and in the same proportion. It is not a single-event or
single-band artefact. (`-a 5.5` gives +5 / +4 as well.) The absolute delta vs LST is not
half-stable -- LST itself is .81065 on the first half and .80915 on the second -- which is
exactly why the judged quantity here is the DELTA BETWEEN OUR TWO CONFIGURATIONS on the
same events, not the delta vs LST on a subsample.

## M23 -- `-a 6` MOVES THE TC-TYPE COMPOSITION TOWARD LST ON EVERY SLICE (300 evts, per event)
```
slice                  -a 6.875   -a 6.0      LST      direction
pT5-class (type 7)        531.2    637.5     706.6    toward LST
bare chain (type 4)       328.4    226.1     111.9    toward LST
pT3-class (type 5)         93.3     92.5     104.5    flat
bare pLS  (type 8)        622.1    619.6     624.7    flat
all                      1601.0   1601.7    1579.2    flat
mean nhitOT, all rows     6.441    6.462     6.519    toward LST
duplicate rate, all       0.0623   0.0591    0.0518   toward LST
fake rate, all            0.0555   0.0551    0.0448   toward LST
```
The frozen `-a 6.875` was leaving ~106 chains per event UNATTACHED that LST's own pT5/pT3
stages do attach. This is a strong TRANSFERABILITY argument: `-a 6` is not a PU200-specific
tune that trades one metric for another, it moves our object-type composition toward the
reference algorithm's on every slice at once. The pixel-slice subset invariant still holds
exactly at `-a 6` (compare_types verdict "OK -- kept pixel rows are an exact subset of
base"), so nothing structural broke.
Files: `a04_ref/types_GATE300.txt`, `types_E_A6.txt`, `types_H_A55.txt`.

## M24 -- THE COMPOSITION FRONTIER: `-a` IS THE KNOB THAT SETS OUR pT5-CLASS COUNT
Rows per event by tc_type, 300 evts:
```
-a        pT5(7)    T5(4)   pT3(5)   pLS(8)   nhitOT_all   DR_all
1e9          0.0    854.3     96.1    627.5      6.407     0.0704
8          345.1    511.3     94.4    625.6      6.419     0.0670
6.875      531.2    328.4     93.3    622.1      6.441     0.0623   <- BASELINE
6.25       612.1    250.4     92.7    620.2      6.456     0.0599
6.0        637.5    226.1     92.5    619.6      6.462     0.0591   <- CHOSEN
5.5        675.1    190.5     92.1    618.4      6.472     0.0578
5.0        698.9    167.6     91.8    617.3      6.479     0.0570
4.5        715.2    152.1     91.5    616.0      6.486     0.0562
LST        706.6    111.9    104.5    624.7      6.519     0.0518
```
`-a` trades bare chain rows for pixel-backed rows one for one. The frozen 6.875 leaves our
pixel-backed count at 531/evt against LST's 707 -- a 25% shortfall that nobody had noticed
because `-a` was frozen before the pT5 replacement existed. OUR COUNT MATCHES LST's AT
`-a` ~5.0. The pT3-class and bare-pLS counts barely move, so this is a clean single-axis
effect and not a rebalancing of everything.

## M25 -- THE `-a` RECOVERY IS A LOW-pT RECOVERY (net sims, ours minus LST, per pt bin)
```
pt bin        -a 6.875   -a 6.0   -a 5.0
[0.9,1.2)          -8       -6       -6
[1.2,2)            -5       -3       -2
[2,4)              -8       -3       -3
[4,10)            +12      +12      +12
[10,inf)          +10      +10      +10
```
The assembled algorithm was already comfortably ahead of LST above 4 GeV and behind below
it. `-a 6` closes about half of the sub-4-GeV deficit and touches nothing above 4 GeV. The
residual sub-2-GeV deficit (-9 net) plus the endcap pLS deficit are what is left of the
prompt gap.

## M26 -- THE 219 REMAINING LOSSES AT `-a 6`, BY LST DELIVERING TYPE x REGION
```
LST type   barrel  transi  endcap  total
pT3            47      28      11     86
pLS            14      10      37     61
pT5            17      28      14     59
T5              2       7       2     11
T4              0       1       1      2
total          80      74      65    219
```
BARREL is dominated by LST-pT3 (47 of 80): sims LST reaches with a pixel + 3-layer object.
ENDCAP is dominated by LST-pLS (37 of 65): sims LST reaches with a BARE PIXEL SEED that we
retire. Those are two different problems and only the first is a delivery-margin problem.
`-a` and `-AT3` can only ever touch the first; the endcap 37 belong to whoever owns seed
retention (-RPS / -XC / -ZP8). This is the single most useful handoff from this angle.

## M27 -- HOW MUCH OF EACH LOSS BLOCK IS EVEN REACHABLE BY SEED RETENTION
Losses by LST type x region under the three seed-retention ablations (300 evts):
```
                 baseline  -RPS 0  -XC 0  -XCT 6   -a 6
pLS  / endcap          37      22     30      35     37
pLS  / barrel          14      11     11      11     14
pT3  / barrel          47      49     41      44     47
pT3  / transi          28      26     23      24     28
pT5  / transi          28      30     26      27     28
pT5  / barrel          17      20     11      16     17
```
Turning the entire seed-retention family OFF removes only 15 of the 37 endcap bare-pLS
losses, and it does NOT touch the barrel pT3 block at all (47 -> 49, i.e. it gets slightly
WORSE because the extra surviving seeds win contentions). So:
  * the ENDCAP bare-pLS deficit has a hard floor around 22/evt-300 even with -RPS off;
  * the BARREL pT3 deficit (47) is NOT a retirement problem and NOT a margin problem -- it
    survives every relaxation measured this round and is part of the 75-sim structural
    floor. It is a CANDIDATE-EXISTENCE problem (the pair was never enumerated or the
    covering chain took a different seed).
Neither is reachable from this angle. The prompt-efficiency headroom that IS reachable was
the pixel-backed barrel/transition block, and `-a 6` takes it.

## M28 -- THE BARREL LST-pT3 BLOCK IS IMMOVABLE; THE CONTENTION IS WHAT PROTECTS BARE SEEDS
Same table, adding the contention ablations:
```
                 baseline  -a 6  -RPS 0  -XC 0  -XCT 6  -CCN 2  -CC 0
pT3  / barrel          47    47      49     41      44      46     44
pT5  / barrel          17    17      20     11      16      18      7
pT5  / transi          28    28      30     26      27      29     10
pT5  / endcap          14    14       7     13      14      11      5
pLS  / barrel          14    14      11     11      11      25     35
pLS  / endcap          37    37      22     30      35      42     44
```
TWO CLEAN READINGS.
1. The barrel LST-pT3 block sits at 41-49 under EVERY relaxation of every mechanism. It is
   not reachable by tuning; it is the largest single component of the structural floor.
2. `-CC 0` recovers the pixel-backed blocks (pT5/barrel 17->7, pT5/transi 28->10) but makes
   the bare-pLS blocks WORSE (pLS/barrel 14->35, pLS/endcap 37->44). The OT contention is
   simultaneously the biggest destroyer of pixel-backed efficiency AND the protector of
   bare-seed efficiency; that two-sidedness is why its net recovery (+69) is much smaller
   than its gross (85) and why loosening it is not the free lunch the gross number suggests.

## M29 -- WHY `-a` TURNS OVER, AND WHY IT COSTS DISPLACED: SEED CONSUMPTION
Losses by LST type x region along the -a frontier (300 evts):
```
                 -a 6.875   6.0    5.5    5.0
pT5  / transi          28    28     28     24
pT5  / barrel          17    17     17     15
pLS  / endcap          37    37     39     40
pLS  / barrel          14    14     13     16
pT3  / barrel          47    47     46     46
```
Lowering `-a` buys pixel-backed (pT5-block) recoveries and PAYS for them in bare-pLS
coverage, because an attached chain CONSUMES the seed and the seed's own bare row is then
never added. Below ~5.5 the bare-pLS endcap block starts growing faster than the pT5 block
shrinks -- that is the efficiency turnover at 4.5, and it is the SAME mechanism as the
displaced drift (displaced tracks in this algorithm are disproportionately carried by bare
pLS rows and by chains, not by pixel-backed rows). One mechanism explains both costs.

## M30 -- CORRECTION TO M28, FROM THE D_MAX CEILING RUN
With EVERYTHING off at once (-XC 0 -RPS 0 -CC 0 -AT3 3) the blocks go:
```
                 baseline   D_MAX
pT3  / barrel          47      29
pT5  / barrel          17       4
pT5  / transi          28       9
pT5  / endcap          14       3
pLS  / barrel          14     188
pLS  / transi          10      85
pLS  / endcap          37      31
```
So the barrel LST-pT3 block is NOT fully structural: 18 of its 47 ARE reachable, and the
pixel-backed blocks are almost entirely reachable (17->4, 28->9, 14->3). What makes them
unreachable IN PRACTICE is that the configuration which reaches them destroys bare-pLS
coverage (barrel 14 -> 188, transition 10 -> 85) and duplicate rate (.369) at the same
time. The single-mechanism ablations each keep the pT3/barrel block at 41-49 because each
one alone cannot both deliver the pT3-class row AND leave the competing rows alive.
THE HONEST STATEMENT OF THE STRUCTURAL FLOOR: 75 sims are unreachable by any SINGLE
relaxation; the joint relaxation reaches 153 of 228 but at a duplicate rate of .369 and a
fake rate of .223, i.e. it is not a config, it is a ceiling measurement.

## M31 *** BATCH 6: `-a 6` DOMINATES AT EVERY OTHER KNOB SETTING MEASURED ***
Each pair below is the SAME (-AT3, -XCT) with only -a moved from 6.875 to 6.0 (300 evts):
```
(-AT3, -XCT)      -a 6.875 eff/dup/fake        -a 6.0 eff/dup/fake        verdict
(6,   4.0)      .80992 / .06230 / .05551     .81032 / .05909 / .05508   better on ALL 3
(6,   4.5)      .81037 / .06675 / .05539     .81076 / .06343 / .05496   better on ALL 3
(6,   3.5)      .80904 / .05925 / .05558     .80953 / .05612 / .05515   better on ALL 3
(6.5, 4.0)      .80966 / .06341 / .05181     .80992 / .06007 / .05143   better on ALL 3
(7,   4.0)      .80900 / .06452 / .04970     .80926 / .06109 / .04937   better on ALL 3
```
FIVE FOR FIVE. `-a 6.875 -> 6.0` is a Pareto improvement on efficiency, duplicate rate AND
fake rate at every operating point of the other two knobs. This is not a re-balancing.

FULL BATCH 6 TABLE (300 evts)
```
tag        flags off the baseline        eff      dup     fake      effB     effT     effE
I_A40      -a 4                      0.80944  0.05547  0.05586  0.92637  0.88315  0.74352
I_A6X35    -a 6 -XCT 3.5             0.80953  0.05612  0.05515  0.92614  0.88111  0.74485
I_A5X35    -a 5 -XCT 3.5             0.80961  0.05408  0.05522  0.92625  0.88187  0.74463
I_A6T7     -a 6 -AT3 7               0.80926  0.06109  0.04937  0.92398  0.87984  0.74684
I_A6T65    -a 6 -AT3 6.5             0.80992  0.06007  0.05143  0.92625  0.88136  0.74562
E_A6       -a 6                      0.81032  0.05909  0.05508  0.92751  0.88213  0.74507
H_A6X45    -a 6 -XCT 4.5             0.81076  0.06343  0.05496  0.92796  0.88340  0.74518
GATE300    (assembled baseline)      0.80992  0.06230  0.05551  0.92660  0.88213  0.74496
LST                                  0.80988  0.05179  0.04476  0.92557  0.88187  0.74596
```
`-a 4` confirms the turnover: efficiency falls to .80944, BELOW `-a 4.5`.

## M32 -- THE FOUR USEFUL OPERATING POINTS, ALL BUILT ON `-a 6`
```
purpose                flags added to the baseline   eff      dup     fake   knobs moved
ONE-KNOB (recommend)   -a 6                       .81032  .05909  .05508      1
max efficiency         -a 6 -XCT 4.5              .81076  .06343  .05496      2
same eff, best fake    -a 6 -AT3 6.5              .80992  .06007  .05143      2
duplicate-leaning      -a 5 -XCT 3.5              .80961  .05408  .05522      2
assembled baseline     (none)                     .80992  .06230  .05551      0
LST                                               .80988  .05179  .04476
```
Every one of the four beats the assembled baseline on at least two of the three judged
quantities and none is worse on any of them except where explicitly traded. The
recommendation is the ONE-KNOB row: it is the only change that improves all three at once,
it removes a constant that was stale rather than adding one, and simplicity is a judging
criterion.

## M33 -- SEED CONSUMPTION IS THE MASTER VARIABLE BEHIND BOTH MARGINS
Loss blocks along both delivery margins (300 evts, all rows at -a 6 except the first):
```
config                     pT3/barrel   pLS/endcap   pT5/transi
baseline (-a 6.875, AT3 6)        47           37           28
-a 6                              47           37           28
-a 6 -XCT 3.5                     50           39           29
-a 5 -XCT 3.5                     49           41           25
-a 4                              48           48           23
-a 6 -AT3 6.5                     59           33           28
-a 6 -AT3 7                       77           22           30
```
Both delivery margins move the SAME two blocks in OPPOSITE directions: any margin that
delivers more pixel-backed rows consumes more seeds and loses bare-pLS coverage, and any
margin that delivers fewer leaves more seeds alive. `-AT3` is the violent version of this
trade (47 -> 77 pT3/barrel for 37 -> 22 pLS/endcap between 6 and 7) and `-a` is the gentle
one (flat on both between 6.875 and 6.0 while still recovering 17 sims elsewhere). That
asymmetry is exactly why `-a` is a free improvement and `-AT3` is not.

## M34 -- BATCH 7 (joint frontier closed, 300 evts)
```
tag        flags                 eff      dup     fake     v1030      d15   displ.tracks lost
GATE300    (baseline)        0.80992  0.06230  0.05551  0.74139  0.61650    0
J_A625X4   -a 6.5            0.81023  0.06077  0.05525  0.73979  0.61538   -3
E_A6       -a 6              0.81032  0.05909  0.05508  0.73899  0.61427   -5
H_A55      -a 5.5            0.81037  0.05783  0.05506  0.73739  0.61315   -8
J_A55X45   -a 5.5 -XCT 4.5   0.81072  0.06211  0.05493  0.73739  0.61315   -7
H_A6X45    -a 6 -XCT 4.5     0.81076  0.06343  0.05496  0.73899  0.61427   -4
J_A6X5     -a 6 -XCT 5       0.81151  0.07006  0.05479  0.73899  0.61427   -4
I_A5X35    -a 5 -XCT 3.5     0.80961  0.05408  0.05522  0.73579  0.61204  -11
I_A6T65    -a 6 -AT3 6.5     0.80992  0.06007  0.05143  0.73899  0.61427   -5
LST                          0.80988  0.05179  0.04476  0.66453  0.55407
```
`-a 6.5` is the minimum-risk version: +.00031 eff, -.00153 dup, -.00026 fake for 3 displaced
tracks. `-a 6` is the recommendation: it takes essentially the whole efficiency plateau
(+.00040 of the +.00045 available) and twice the duplicate-rate benefit, for 5 displaced
tracks. Everything past 5.5 buys duplicate rate at an accelerating displaced price.
Efficiency can be pushed to .81151 with `-XCT 5` but that costs +.0110 of duplicate rate,
which is the wrong trade when duplicate rate is already our second-largest gap vs LST.

## M35 -- THE THREE KNOBS ARE ORTHOGONAL BY ROW TYPE (rows/evt, 300 evts)
```
config              pT5(7)    T5(4)   pT3(5)   pLS(8)   nhitOT_all
baseline             531.2    328.4     93.3    622.1      6.441
-a 6.5               583.1    278.2     92.9    620.9      6.450
-a 6                 637.5    226.1     92.5    619.6      6.462
-a 5.5               675.1    190.5     92.1    618.4      6.472
-a 6 -AT3 6.5        637.5    226.1     82.4    623.3      6.449
-a 6 -XCT 3.5        637.5    226.1     92.5    616.2      6.475
-a 6 -XCT 5          637.5    226.1     92.5    630.7      6.417
LST                  706.6    111.9    104.5    624.7      6.519
```
`-a` sets the pixel-backed / bare-chain split and NOTHING else; `-AT3` sets the pT3-class
count and nothing else; `-XCT` sets the bare-seed count and nothing else. Each is exactly
one row-type dial. This is a useful property for the synthesis config: the three can be
chosen independently, and `-a` is the only one of the three whose dial position was never
re-derived after the algorithm changed underneath it.

## M36 *** THE 977-EVENT CONFIRMATION: THE `-a 6` GAIN TRANSFERS ***
`W_A6` = the assembled baseline with `-a 6` on the full 977, compared against the SAME LST
977 reference the Baseline agent generated (`fin_ref/fin_base977_hists.root`).
NOTE: a04_run.sh defaults BASEHISTS to the 300-evt LST hists, so `a04_ref/r_W_A6.json` has a
WRONG base column; the correct comparison is `a04_ref/r_W_A6_977.json` /
`r_agg_W_A6_977.txt`, regenerated by hand against fin_base977_hists.root. Only the base
column was ever wrong; the proto column is identical in both.
```
metric      W_A6 (-a 6)     FINBASE      LST(977)     delta vs FINBASE   delta vs LST
eff             0.80937     0.80905       0.80987        +0.00032          -0.00050
dup             0.05858     0.06184       0.05138        -0.00326          +0.00720
fake            0.05563     0.05607       0.04538        -0.00044          +0.01025
nhB             9.85495     9.80064      10.14984        +0.05431          -0.29489
nhT             9.88961     9.88373      10.00937        +0.00588          -0.11976
nhE             3.55665     3.55719       3.55665        -0.00054          +0.00000
nTC             2029808     2033868       1998494          -4060            +31314
effB            0.92539     0.92454       0.92430        +0.00085          +0.00109
effT            0.88012     0.87981       0.88004        +0.00031          +0.00008
effE            0.74422     0.74436       0.74658        -0.00014          -0.00236
dupB            0.03347     0.04294       0.00971        -0.00947          +0.02376
dupT            0.03308     0.03375       0.01308        -0.00067          +0.02000
dupE            0.08014     0.08069       0.08486        -0.00055          -0.00472
fakB            0.06657     0.06754       0.04365        -0.00097          +0.02292
fakT            0.06787     0.06932       0.04542        -0.00145          +0.02245
fakE            0.04591     0.04578       0.04630        +0.00013          -0.00039
displaced:  v15 0.80132 (+.00022)  v510 0.72161 (unchanged)  v1030 0.71254 (-.00220)
            d15 0.58187 (-.00133)  d510 0.24521 (unchanged)  d1030 0.03151 (unchanged)
```
THE 300 PREDICTED THE 977 ALMOST EXACTLY: predicted +.00040 / -.00321 / -.00043, delivered
+.00032 / -.00326 / -.00044. Every one of the three within .0001.
DISPLACED on 977: -9 tracks in vxy[10,30) (denominator ~4070) and -4 in dxy[1,5); v510,
d510 and d1030 did not move a single track. We remain v510 +.0774, v1030 +.0869, d15 +.0715
AHEAD of LST -- the displaced lead is untouched in substance.
BARREL EFFICIENCY IS NOW ABOVE LST ON THE FULL SAMPLE (+.00109, was +.00024), transition is
at parity (+.00008, was -.00023), and every region's duplicate rate improved.

## ===================== FINAL RECOMMENDATION (A04) =====================
ONE FLAG, NO CODE:  add `-a 6` to the assembled baseline.
  full line: -XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2 -a 6
It REPLACES the stale constant 6.875 rather than adding a knob, so the assembled algorithm
still carries the same number of tuned constants it did before.
Conservative alternative `-a 6.5` (half the benefit, 3 displaced tracks instead of 5 on the
300). Efficiency-leaning `-a 6 -XCT 4.5`. Fake-leaning `-a 6 -AT3 6.5`. All measured above.
