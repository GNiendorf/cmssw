# A03 -- ENDCAP RECALL (attach prefilter |dTanLambda|) -- STATUS

Workspace: `standalone/protoA03` (copy of `protoFIN`, md5 519b0abc... before edits).
Artifacts: `standalone/a03_ref/`. Runner: `bash a03_ref/a03_run.sh <TAG> [overrides]`
(the ASSEMBLED BASELINE tail `-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2` is INSIDE the
script, so overrides append to the baseline).

## THE ANGLE
The attach prefilter's W1 window is a constant `|dTanLambda| < 0.6`. tanLambda = sinh(eta),
so its reach in ETA is 0.6/cosh(eta): 0.600 at eta 0, 0.13 at |eta| 2.2, 0.06 at |eta| 3.
Everything else works at a fixed reach in eta (the ported CrossCleanpLS pre-window
dR^2 < 0.02, i.e. dR < 0.141), so above |eta| ~ 2.2 the prefilter is the tighter of the two
-- and it feeds BOTH delivery and (through plsBestChainLogit) the seed dedup.

## THE CODE (M1, DONE)
ONE new flag `-PWE <w>`: the effective half-width becomes
`max(prefDTanL, w * cosh(eta_target))`, i.e. FLAT IN ETA at reach w. Default 0 = frozen.
* `PixelAttach.h`: `AttachParams::prefDEta` + `attachDTanLWindow()` -- the SINGLE window
  definition, called by both the analytic pair test and the binned index's scan range, so
  index-window == eval-window and the PixelAttachCand.h W1 superset proof is unchanged.
* `PixelAttach.cc`: 2 call sites (evalPair; candidatesForTarget).
* `main.cc`: flag, wiring to `gap.pref` / `apre` / `apar`, usage, one report line.
It can only WIDEN (max with the frozen constant), so any w > 0 is a strict superset.

## M2 -- FIRST MEASUREMENT (2 events, -PWE 0 / 0.15 / 0.30 / 0.60)
The prefilter DID widen, hard:
```
-PWE     |dTanL| reach at eta 0/1.5/2.2/3.0     pairs emitted/evt
 0       0.600 0.600 0.600 0.600                2.37e6
 0.15    0.600 0.600 0.685 1.510                2.40e6
 0.30    0.600 0.706 1.370 3.020                2.72e6
 0.60    0.600 1.411 2.741 6.041                4.02e6   (+69%)
```
and NOT ONE DECISION MOVED. Every downstream number is IDENTICAL across all four:
upgT5 944, delivT3 202, XC seeds retired 5204 [pixHit 4728 / pixDR 8 / chain 468],
carried type-8 killed 9, -ZP8 blocked 199, pixel-side revoked 1047, OT-side revoked 553,
DELIVERED 202, output TCs/evt 1706.0.

## M3 -- THE ATTRIBUTION (PROTO_ATTACH_CM, 30 events, -PWE 0 vs 0.6)
```
                                  -PWE 0     -PWE 0.6
pairs emitted / evt              5.88e6      9.39e6   (+60%)
TRUE pairs POSSIBLE / evt      46161.63    46161.63   (same universe)
TRUE pairs KEPT by the window  45863.63    46150.37
  -> PREFILTER (window) RECALL    0.9935      0.9998
same-sim attaches made            20749       20749   <-- IDENTICAL
true pairs missed BELOW THETA    0.4002      0.4039
true pairs missed to arbitration 0.5847      0.5811
```
THE PREFILTER IS NOT THE CONSTRAINT. It was already keeping 99.35% of the true pairs;
-PWE 0.6 takes that to 99.98% (286.7 more true pairs/evt) for 3.5e6 extra scored pairs/evt,
and the head scores EVERY ONE of the recovered pairs below its margin: not a single attach
decision changes. The head rejects 40.0% of true pairs -- 60x the prefilter's loss.

## M4 -- WHERE THE ENDCAP ACTUALLY LOSES (all from the Baseline agent's own runs)
```
tag                   -AT3   effB     effT     effE     dupE     fakE     eff(all)
GATE (stage OFF)        --  0.90805  0.87016  0.74873  0.09767  0.04016  0.80215
FINBASE                6.0  0.92660  0.88213  0.74496  0.08106  0.04525  0.80992
E_A8X4                 8.0  0.91442  0.87551  0.74784  0.08738  0.04049  0.80520
E_A9X4                 9.0  0.90827  0.87042  0.74828  0.09109  0.04011  0.80210
LST                     --  0.92557  0.88187  0.74596  0.08556  0.04603  0.80988
```
The pT3 stage is ENDCAP-NEGATIVE and barrel/transition-positive, and the -AT3 optimum is
REGION-SPLIT: barrel/transition want 6, the endcap wants 8-9. This is the same defect the
gen_c round measured directly on the logit ("eta calibration is broken: one global margin
gives barrel/trans/endcap 72.6/93.5/419.7 rows against LST's 322.4/139.0/127.3; per-region
thresholds differ by 4.6 logit units"). Mechanism: an over-delivered pT3-class row consumes
the seed, the -RPS predicate retires the seed's bare type-8 row, and a wrong-T3 delivery
matches no sim while the bare row did.

## M5 -- SECOND CODE CHANGE: `-ATS <slope>` (eta calibration of the stage-B margin)
    margin(eta) = -AT3 + slope * max(0, |eta| - 1.1)
ONE new constant, hinged at the barrel edge the code already uses, default 0 = bit-exact.
`gaMarginT3()` in AttachDelivery.h is the SINGLE definition and is used by the stage-B
accept AND by all three -RPS predicates -- otherwise a delivery the endcap margin refused
would still have its seed retired (the -CCR 1 failure mode).
Binary: protoA03/bin/chainproto_v2. Scan S10/S15/S20/S30 + V2GATE launched.
`-ATP <pivot>` added afterwards (binary chainproto_v3, default 1.1 == v2) so the hinge is
CHOSEN by measurement between the two region boundaries the harness reports at (1.1, 1.7)
rather than asserted.

## CODE STATE (complete delta from protoFIN: a03_ref/protoA03_vs_protoFIN.diff)
5 files, 28 hunks, no other file differs. Binaries in protoA03/bin:
  chainproto     = protoFIN + `-PWE` only          (used by GATE1 / W60)
  chainproto_cm  = + the ATTACHCM per-region split (used by CMR0 / CMR60; diagnostic only,
                   entirely inside `if (attachCM)`, i.e. behind PROTO_ATTACH_CM)
  chainproto_v2  = + `-ATS`                        (used by V2GATE / S10..S30)
  chainproto_v3  = + `-ATP`                        (used by P17*), and v3 with default flags
                   is 33/33 branches IDENTICAL to v2 (a03_ref/r_V2S6 vs r_V3S6, 6 evts).
Two flags, both default-0 no-ops:
  -PWE <w>  eta-flat prefilter W1 window: max(0.6, w*cosh(eta_target)). Superset by
            construction; PixelAttachCand.h's W1 proof carries over verbatim because
            attachDTanLWindow() is the SINGLE definition used by the pair test AND the
            binned index's scan range.
  -ATS <s>  eta calibration of the stage-B margin: -AT3 + s*max(0, |eta| - -ATP).
            gaMarginT3() is the SINGLE definition, used by the stage-B accept AND by all
            three -RPS predicates. -ATP defaults to 1.1 and only ever takes one of the two
            region boundaries the harness reports at.

## M6 -- PER-REGION PREFILTER RECALL (60 evts, -PWE 0), and the reframing
```
region    true pairs poss/evt  kept/evt   WINDOW RECALL  below_theta  arbitration  got_right
barrel           7845.17       7840.20       0.9994        0.6261       0.3353      0.0565
transition       5538.37       5514.52       0.9957        0.5892       0.3854      0.0546
endcap          30166.85      29918.53       0.9918        0.3130       0.6794      0.0889
```
The defect IS localised where the geometry said it would be (the endcap has the worst
window recall of the three) -- and it is 0.8%, i.e. irrelevant. The endcap's real situation
is the OPPOSITE of "recall-starved": it holds 30167 achievable true pairs/evt against the
barrel's 7845, its head-rejection rate is the LOWEST of the three (0.313 vs 0.626), and its
targets pick the right pLS MORE often (0.0889 vs 0.0565). The endcap is SATURATED with
alternatives, so 68% of its true pairs are lost to arbitration by construction, and the
binding problem is OVER-delivery at a global margin, not under-enumeration.

CONFIRMED at 60 events (CMR0 vs CMR60): window recall .9936 -> .9998 overall and
.9918 -> .9999 in the endcap, pairs emitted 5.42e6 -> 8.58e6 per event (+58%), and the
whole DECISIONS line is byte-identical --
  n=42217  same_sim=40213  cross_sim=202  true_tgt_fake_pls=597  fake_tgt_real_pls=652
  fake_both=553   at BOTH -PWE 0 and -PWE 0.6.

PRECISION ON "NOTHING CHANGED": the ATTACH decisions are identical, and the ONLY quantity
anywhere in the 60-evt ledger that moves is the ported crossclean's bare-chain arm, which
retires 20981 seeds instead of 20959 -- 22 seeds over 60 events, i.e. +0.37/evt against a
total of 3205/evt (+0.011%). So the widened window does reach the dedup, by one part in
ten thousand. Everything else -- upgT5 569.3, delivT3 134.3, pixel-side revoked 646.5,
OT-side revoked 325.7, -ZP8 additions 28.1, output TCs 2013.8/evt -- is identical.

And on the PHYSICS SCOREBOARD, not just the counters: r_agg_CMR0.txt and r_agg_CMR60.txt
(60 events each) agree to 4 decimals on EVERY line -- eff .8096, vxy bands .8376/.7676/
.7523/.7613, dxy bands .8320/.6096/.1538/.0244, per-region eff .9212/.8558/.7543, dup
.0626 (.0411/.0362/.0822), fake .0535 (.0647/.0659/.0437), nhitOT 9.830/9.892/3.550, and
n_TC 122516 on both.

NOTE ON THE 300-EVT GATE: tag GATE1 (v1 binary, -PWE 0) was SIGTERM'd by my own cleanup of
redundant runs at 22:19 (adjacent PID) and is not available. It is not needed: V2GATE is the
v2 binary with EVERY A03 flag at its default, and v2's defaults are v1's defaults are
protoFIN, so `V2GATE vs fin_ref/r_FINBASE_ND` is the no-op gate and `W60 vs V2GATE` is the
-PWE comparison. Both are run by monitor b9sclmwj4.

## M7 -- THE LOSS MECHANISM, QUANTIFIED (same 60-evt CM run)
`[ATTACHCM] SUPPRESSED type-8 rows: real_pLS=1537 (of which owner shares the sim=331)
fake_pLS=47` -- i.e. 25.6 REAL pLS rows per event are retired by the suppression and only
5.5 of them are retired by an owner that SHARES THEIR SIM. 78% of the real seeds this
mechanism kills are killed by a delivery belonging to a DIFFERENT track. That is the
channel -ATS attacks: it does not make the head better, it stops the head being trusted at
a margin it is not calibrated for.

AND IT IS NOT FREE. The two 60-evt runs were launched together (same machine load), so the
ratio is fair even though the absolute numbers are contaminated by 15 agents sharing the
box: attach stage 10452.7 -> 17861.6 ms/evt (+71%), whole job 674 -> 1133 s (+68%). The
30-evt pair gives the same picture (264 -> 482 s). Paying +70% of the dominant stage for
zero decisions is the reason NOT to carry -PWE.


## M8 -- THE 300-EVT SCOREBOARD (all runs complete)

NO-OP GATE PASSED EXACTLY. `V2GATE` (chainproto_v2, every A03 flag at default) is
IDENTICAL to `fin_ref/r_FINBASE` on every metric and on nTC (618793). v3 == v2 at
defaults (33/33 branches, 6 evts). So both new flags are proven inert at 0.

```
tag        change from ASSEMBLED BASELINE   eff      dup     fake     effE     dupE     fakE      nhE   delivT3/evt
V2GATE     (none -- the no-op gate)      .80992  .06230  .05551  .74496  .08106  .04525  3.55956    135.4
W60        -PWE 0.6                      .80992  .06229  .05552  .74496  .08105  .04526  3.55966    135.4
S10        -ATS 1.0                      .81076  .06414  .05344  .74695  .08433  .04233  3.53018    126.6
S15        -ATS 1.5                      .81094  .06523  .05279  .74751  .08626  .04143  3.51516    122.2
S20        -ATS 2.0                      .81103  .06654  .05234  .74751  .08861  .04086  3.49825    117.5
S30        -ATS 3.0                      .81116  .06857  .05186  .74839  .09221  .04037  3.46015    107.9
P17S15     -ATS 1.5 -ATP 1.7             .81037  .06347  .05482  .74607  .08321  .04397  3.54356    132.1
P17S25     -ATS 2.5 -ATP 1.7             .81050  .06434  .05447  .74640  .08481  .04332  3.53137    125.2
P17S40     -ATS 4.0 -ATP 1.7             .81067  .06620  .05409  .74684  .08823  .04263  3.50973    125.2
LST                                      .80988  .05179  .04476  .74596  .08556  .04603  3.56248
```
Barrel is UNTOUCHED by construction at every point (effB .92660, dupB .04344, fakB .06680,
nhB 9.8025 on ALL nine rows) -- the hinge is at |eta| 1.1 so the barrel margin is literally
the frozen 6.000. Displaced bands are also untouched: v510 .73356 and v1030 .74139 and
d15 .61650 on every -ATS row except S30 (v510 .73187, one track).

## M9 -- `-PWE` IS DEAD, CONFIRMED AT 300 EVENTS
W60 vs V2GATE over the full frozen 300: deliveries 40623 -> 40626 (+3 TOTAL, i.e. +0.01/evt),
seeds retired 3235.2 -> 3235.9/evt, nTC 618793 -> 618789, and every physics number agrees to
the 4th decimal. Widening the prefilter to a flat eta reach of 0.6 costs +70% of the
dominant stage's runtime and changes THREE decisions in three hundred events. The measured
endcap "recall defect" is REAL (window recall .9918 in the endcap vs .9994 in the barrel)
and it is IRRELEVANT: the head already rejects 31% of the true endcap pairs it is shown,
40x the window's loss. DO NOT CARRY -PWE. Recommend the finding be recorded as CLOSED so
no future round re-opens it.

## M10 -- WHAT THE ANGLE ACTUALLY FOUND: `-ATS`
Chasing the recall defect led to the real endcap defect, which is the OPPOSITE sign:
the stage-B margin is a single global constant and the endcap is OVER-delivered at it.
`-ATS <s>` tilts the margin, `margin(eta) = -AT3 + s*max(0,|eta| - 1.1)`.

MECHANISM, straight off the ledger (per event, -ATS 0 -> 1 -> 1.5 -> 2 -> 3):
```
DELIVERED pT3-class rows      135.4  126.6  122.2  117.5  107.9
carried pLS rows killed by -RPS 33.8   28.9   26.5   24.1    20.4
-ZP8 type-8 additions           29.4   32.5   34.6   37.2    43.4
pixel-side revocations         661.3  596.4  556.1  507.3   383.9
```
Refusing an endcap delivery the head is not calibrated for RETURNS the seed's bare type-8
row to the output. That is where the efficiency comes from (+.0012 at S30) and where the
duplicates come from (+.0063) -- bare seeds are dup-prone, and the fake rate falls because
a mis-attached endcap pT3-class row matches no sim while the bare row does.

IT DOMINATES A KNOWN BASELINE FRONTIER POINT. Against the Baseline agent's own -XCT 4.5
bracket (eff .81037 / dup .06675 / fake .05539), S10/S15/S20 are better on ALL THREE:
  S10  +.00039 eff  -.00261 dup  -.00195 fake
  S15  +.00057 eff  -.00152 dup  -.00260 fake
  S20  +.00066 eff  -.00021 dup  -.00305 fake
This is a new direction, not a re-parameterisation of -AT3/-XCT: -AT3 buys fake with
EFFICIENCY, -ATS buys fake with DUPLICATES, and duplicates are what -XCT is for.

PIVOT CHOSEN BY MEASUREMENT, NOT ASSERTED. 1.1 dominates 1.7 pairwise:
S10 (.81076/.06414/.05344) beats P17S25 (.81050/.06434/.05447) on all three; S20 beats
P17S40 on eff and fake. -ATP is therefore FIXED AT ITS DEFAULT 1.1 and is not a knob.

## M11 -- BATCH 5 (running): does -ATS + a tighter -XCT dominate the BASELINE ITSELF?
The extra dup is bare seeds and -XCT is the bare-seed cleaner, so the two should compose.
Launched XS15C35 (-ATS 1.5 -XCT 3.5), XS20C35, XS20C30, XS30C30.

## M12 -- 977-EVT CONFIRMATION LAUNCHED IN PARALLEL
Tag `W977_S15C35` = `-ATS 1.5 -XCT 3.5` on rebase_ref/LSTNtuple_instr_977evt.root against
fin_ref/fin_base977_hists.root. The 977 comparison point is the Baseline agent's own
`fin_ref/r_W_X4` (= FINBASE on 977: eff .80905 / dup .06184 / fake .05607). Launched
concurrently with batch 5 on the prediction that XS15C35 is the strict-dominance point;
if the 300-evt batch picks a different winner the 977 still bounds the direction.

## M13 -- WHY THIS IS NOT JUST "ETA BINS HELP"
The port round ALREADY had eta bins on the seed-dedup threshold (-XCT2 transition,
-XCT3 endcap, both defaulting to -XCT) and MEASURED THEM AND REJECTED THEM: one global
dedup threshold was kept. -ATS is the opposite site -- the DELIVERY margin, not the dedup
threshold -- and there it pays. The asymmetry is the point, and it is what the truth
partition predicts: the dedup threshold acts on a seed population whose separating power
collapses uniformly below logit ~4 in every region, whereas the delivery margin acts on a
head whose CALIBRATION (not its separating power) is eta-dependent. So this finding does
not generalise to "add eta bins to thresholds"; it is specific to the miscalibrated head,
and it should be retired the moment the head is retrained with eta in its features.

## M14 -- STAGE-B PRECISION (from the 60-evt CM run, for the record)
`stage-B(T3) same=6935 bad=1124` -- the bare-T3 deliveries are 86.0% same-sim at -AT3 6.
Per region the head's behaviour on true pairs is: barrel attaches 3.86% of them, endcap
only 0.76%, and the endcap loses 67.9% of its true pairs to ARBITRATION (barrel 33.5%).
The endcap is alternative-saturated, not recall-starved; that is why widening the window
does nothing and why re-calibrating the accept margin does.

## M15 -- BATCH 5: `-ATS` + A TIGHTER `-XCT` DOMINATES THE ASSEMBLED BASELINE
The prediction held. The extra duplicates -ATS creates ARE bare seeds, and -XCT is the
bare-seed cleaner, so the two compose and the composite is better than the baseline on
ALL THREE headline metrics at once.

```
tag        flags added to the baseline    eff       dup      fake      effE     dupE     fakE
V2GATE     (none == FINBASE)           .80992   .06230   .05551    .74496   .08106   .04525
XS15C35    -ATS 1.5 -XCT 3.5           .81001   .06196   .05284    .74706   .08445   .04140
XS20C35    -ATS 2.0 -XCT 3.5           .81010   .06319   .05239    .74706   .08667   .04083
XS20C30    -ATS 2.0 -XCT 3.0           .80877   .06068   .05241    .74651   .08513   .04075
XS30C30    -ATS 3.0 -XCT 3.0           .80882   .06249   .05192    .74717   .08837   .04024
LST                                    .80988   .05179   .04476    .74596   .08556   .04603
```

**XS15C35 vs the ASSEMBLED BASELINE: eff +.00009, dup -.00034, fake -.00267.** Strictly
better on all three; the efficiency move is parity (9 tracks in 62555, noise) and the fake
move is real. Track length: nhB 9.8025 -> 9.8366 and nhT 9.8791 -> 9.9221 (both TOWARD
LST's 10.148/10.015), nhE 3.5596 -> 3.5202 (AWAY from LST's 3.5625). Aggregate mean nhitOT
6.4412 -> 6.4299, a -0.011 regression -- stated, not hidden. Displaced is untouched:
v510 .73356, v1030 .74139, d15 .61650, d510 .20161, d1030 .03073 identical to the baseline
on every batch-5 row; v15 .79459 -> .79313 and vxy01 .84282 -> .84296.

NO BASELINE (-AT3,-XCT) POINT REACHES IT. From the Baseline agent's own frontier, the only
points with dup <= .06196 are A6X35 (.80904/.05925/.05558), A65X35 (.80873/.06024/.05187)
and A7X35 (.80807/.06124/.04975) -- every one of them gives up .001 to .002 of EFFICIENCY.
At dup ~.062 the baseline's own point is FINBASE itself, with .0027 more fake. So this is
a genuinely new direction, not a re-parameterisation.

PER REGION it is a deliberate REBALANCE, and it is honest to say so: barrel and transition
efficiency give up .0015/.0010 (effB .92660 -> .92512 vs LST .92557; effT .88213 -> .88111
vs LST .88187, so both slip just BELOW LST where the baseline was just above), the endcap
gains .0021 (effE .74496 -> .74706, now .0011 ABOVE LST). Duplicates move the other way:
dupB .04344 -> .03860, dupT .03431 -> .02944, dupE .08106 -> .08445 (still below LST
.08556). Fake improves everywhere but the barrel: fakT .06903 -> .06520, fakE .04525 ->
.04140 (LST .04603), fakB .06680 -> .06699 flat.

CHOICE AMONG THE FOUR. XS20C35 has +.00009 more eff (noise) and -.00045 less fake but
+.00123 more dup; duplicates outrank fake in the standing priority, so XS15C35 is the pick.
The -XCT 3.0 rows lose .0012 of efficiency and are rejected on the dominant metric.

## M16 -- BATCH 6 (ridge probe, running): XS20C375, XS10C375
Round numbers are preferred for the recommendation; these two only test whether the ridge
between -XCT 3.5 and 4.0 is flat, i.e. whether the pick is sensitive to the third digit.

## M17 -- BATCH 6: THE RIDGE IS NOT FLAT, AND IT MOVED THE PICK
```
tag        flags added                eff       dup      fake     effB     effT     effE     v15
V2GATE     (none == FINBASE)       .80992   .06230   .05551  .92660  .88213  .74496  .79459
XS10C375   -ATS 1.0 -XCT 3.75      .81032   .06239   .05346  .92591  .88187  .74673  .79459
XS15C35    -ATS 1.5 -XCT 3.5       .81001   .06196   .05284  .92512  .88111  .74706  .79313
XS20C375   -ATS 2.0 -XCT 3.75      .81059   .06473   .05236  .92591  .88213  .74729  .79240
XS20C35    -ATS 2.0 -XCT 3.5       .81010   .06319   .05239  .92512  .88162  .74706  .79167
S10        -ATS 1.0 only           .81076   .06414   .05344  .92660  .88238  .74695  .79459
LST                                .80988   .05179   .04476  .92557  .88187  .74596  .77193
```
Deltas from the ASSEMBLED BASELINE, in the standing priority order (eff, dup, fake):
```
S10        eff +.00084   dup +.00184   fake -.00207    ONE new knob, -XCT untouched
XS10C375   eff +.00040   dup +.00009   fake -.00205    <-- THE PICK
XS15C35    eff +.00009   dup -.00034   fake -.00267    duplicate-leaning
XS20C375   eff +.00067   dup +.00243   fake -.00315    too much duplicate
XS20C35    eff +.00018   dup +.00089   fake -.00312
```

**XS10C375 IS THE RECOMMENDATION.** It is the only point that buys EFFICIENCY (the dominant
metric) while holding the duplicate rate at parity: +.00009 dup is 9e-5, far inside noise,
against -.00205 of fake and +.00040 of efficiency (25 sim tracks in 62555). It also has
properties the others do not:
* PER-REGION EFFICIENCY IS AT OR ABOVE LST IN ALL THREE REGIONS -- effB .92591 > .92557,
  effT .88187 == .88187, effE .74673 > .74596. The assembled baseline was BELOW LST in the
  endcap (.74496). No other row on this board is >= LST everywhere.
* v15 IS UNMOVED (.79459, exactly the baseline). Every other -ATS point costs .0015-.0029
  of the vxy [1,5) band. All the far displaced bands (v510 .73356, v1030 .74139, d15
  .61650, d510 .20161, d1030 .03073) are IDENTICAL on every row of this whole study.
* the least track-length damage of the winning family: nhE 3.5327 (baseline 3.5596, LST
  3.5625), nhB 9.8217 and nhT 9.9055 both moving TOWARD LST. Aggregate mean nhitOT
  6.4412 -> 6.4325, a -0.009 regression.

THE ONE-KNOB ALTERNATIVE IS S10 (`-ATS 1.0` alone, -XCT left at the blessed 4). It has the
BEST efficiency of the family (+.00084) and the same fake gain, but pays +.00184 of dup on
a baseline that is already +.0105 above LST on duplicates. If the synthesis wants strictly
one new constant and no retune of anything existing, S10 is the honest offer; if it can
afford to move -XCT (already a tuned constant, so the COUNT of constants does not go up),
XS10C375 is better on the second-priority metric by a factor of twenty.

## M18 -- 977-EVT CONFIRMATIONS
`W977_S15C35` (-ATS 1.5 -XCT 3.5) launched first, `W977_S10C375` (-ATS 1.0 -XCT 3.75, the
pick) launched after batch 6 moved the recommendation. Comparison point on 977 is the
Baseline agent's `fin_ref/r_W_X4` = eff .80905 / dup .06184 / fake .05607.

## M19 -- FULL 977: THE EFFECT TRANSFERS ALMOST EXACTLY
```
tag              eff    vxy01     v15    v510   v1030     d15    d510   d1030     dup    fake      nhB      nhT      nhE       nTC
W977_S15C35   .80914  .84154  .80132  .72261  .71474  .58320  .24521  .03151  .06140  .05331   9.8355   9.9271   3.5179   2027846
W_X4 (base)   .80905  .84144  .80110  .72161  .71474  .58320  .24521  .03151  .06184  .05607   9.8006   9.8837   3.5572   2033868
W_GATE        .80213  .83399  .79737  .72010  .71474  .58320  .24521  .03151  .07692  .04742   9.4870   9.7774   3.3860   2002505
LST           .80987  .84285  .77719  .64422  .62567  .51042  .22906  .05402  .05138  .04538  10.1498  10.0094   3.5567   1998494
```
DELTA vs the assembled baseline, 977 against 300:
```
              eff        dup        fake
300 evts   +.00009    -.00034    -.00267
977 evts   +.00009    -.00044    -.00276
```
Every one of the three reproduces to better than .0001. The frozen 300 is a faithful
iteration set for THIS lever, and the improvement is real, not a 300-event fluctuation.

Per region on 977: effB .92318 / effT .87897 / effE .74631 against the baseline's
.92454/.87981/.74436 and LST's .92430/.88004/.74658 -- the same rebalance as at 300, and on
the full sample the endcap gets to .74631, just .0003 short of LST rather than past it.
dupB .03809 dupT .02885 dupE .08390 (baseline .04294/.03375/.08069, LST .00971/.01308/
.08486) and fakB .06773 fakT .06553 fakE .04173 (baseline .06754/.06932/.04578, LST
.04365/.04542/.04630). Displaced is fine and slightly better: v15 .80132 vs .80110,
v510 .72261 vs .72161, v1030/d15/d510/d1030 all identical to the baseline.
Track length: nhB and nhT both move TOWARD LST (+0.035/+0.043); nhE moves away (-0.039);
the aggregate mean nhitOT goes 6.4424 -> 6.4318, a -0.011 regression.

STILL RUNNING: `W977_S10C375`, the 977 confirmation of the PICK (-ATS 1.0 -XCT 3.75).

## M20 -- FINAL: THE PICK ON THE FULL 977
```
tag              eff    vxy01     v15    v510   v1030     d15    d510   d1030     dup    fake      nhB      nhT      nhE       nTC
W977_S10C375  .80957  .84201  .80175  .72261  .71474  .58320  .24521  .03151  .06193  .05393   9.8196   9.9085   3.5299   2030300
W977_S15C35   .80914  .84154  .80132  .72261  .71474  .58320  .24521  .03151  .06140  .05331   9.8355   9.9271   3.5179   2027846
W_X4 (base)   .80905  .84144  .80110  .72161  .71474  .58320  .24521  .03151  .06184  .05607   9.8006   9.8837   3.5572   2033868
LST           .80987  .84285  .77719  .64422  .62567  .51042  .22906  .05402  .05138  .04538  10.1498  10.0094   3.5567   1998494
```
per region 977:
```
tag              effB     effT     effE     dupB     dupT     dupE     fakB     fakT     fakE
W977_S10C375   .92402   .87966   .74627   .04025   .03113   .08305   .06763   .06651   .04261
W977_S15C35    .92318   .87897   .74631   .03809   .02885   .08390   .06773   .06553   .04173
W_X4 (base)    .92454   .87981   .74436   .04294   .03375   .08069   .06754   .06932   .04578
LST            .92430   .88004   .74658   .00971   .01308   .08486   .04365   .04542   .04630
```

THE PICK, `-ATS 1.0 -XCT 3.75`, vs the ASSEMBLED BASELINE on the full sample:
  eff +.00052   dup +.00009   fake -.00214
against the 300-evt prediction of +.00040 / +.00009 / -.00205. The duplicate delta is
IDENTICAL to five decimals on both samples and the other two agree to .0001.

vs LST on the full 977 it moves the efficiency gap from -.00082 to -.00030, i.e. it closes
63% of the remaining efficiency deficit, and it does it WITHOUT giving back duplicates.
Per-region efficiency becomes as balanced as it has been all round: -.0003 / -.0004 /
-.0003 in barrel / transition / endcap, against the baseline's +.0002 / -.0002 / -.0022.
Endcap dup .08305 and endcap fake .04261 both stay BELOW LST's .08486 / .04630.
Displaced is untouched or slightly better on every band (v15 +.00065, v510 +.0010,
v1030 / d15 / d510 / d1030 all bit-identical to the baseline).

THE COST, STATED PLAINLY: mean nhitOT 6.4424 -> 6.4335 (-0.009). nhB and nhT move TOWARD
LST (+0.019 / +0.025) but nhE moves away (3.5572 -> 3.5299, -0.027) because refusing an
endcap delivery returns a 3-hit bare seed row to the output in its place. Delivered
pT3-class rows fall 135.4 -> ~128/evt, further from LST's own ~151.7 pT3/evt, which was one
of the Baseline agent's reasons for preferring -AT3 6.

## STATUS: COMPLETE. Nothing left running.
