# F1 -- BARREL-DUP MECHANISM agent (continuation of B02 attach-conversion + B04 owner-credibility + new-cell design)

Artifact dir: standalone/f1_ref. Workspaces inherited: protoB02 (a-bands + -a4), protoB04 (-B4D/-XCQ).
Shared log: S/FINDINGS_F.md (F2 = thresholds agent on -XCT surface, check before composing).

## M0 -- INHERITANCE RECOVERED (done)
* Server shutdown killed only the FINAL compare_ab step of most B02 runs + three B04 runs.
  All B02 ROOT/hists survived; regenerated JSONs for A35/A45/A55/G2/Q40/Q60/Q80/T50/T40/E50.
  B4G1/B4Q1/B4X30 ROOT are truncated (killed mid-run) -> must re-run.
* GATES CONFIRMED (cmp_branches vs r_D1, 33/33 IDENTICAL, 0 ADDED):
  - r_B02G2 = protoB02 binary carrying a-band + -a4 code, CHAINFINAL line -> bit-exact no-op.
  - r_B4G0  = protoB04 binary carrying -B4D + -XCQ code, CHAINFINAL line + -B4D 2 -> bit-exact no-op.

## M1 -- B02 BARREL -a FRONTIER, COMPLETE (frozen 300, deltas vs B02G0=CHAINFINAL)
-a2 6.0 -a3 6.0 pinned; transition/endcap deltas EXACTLY zero at every barrel point.
| -a | d_eff | d_effB | d_dupB | d_fakB | d_nhB | v15 | v510 | v1030 | dxy15 |
|---|---|---|---|---|---|---|---|---|---|
| 5.5 | +.00013 | +.00034 | -.00025 | -.00021 | +.005 | 0 | 0 | -.0016(-2) | -.0011(-1) |
| 5.0 | +.00022 | +.00057 | -.00229 | -.00012 | +.018 | 0 | 0 | -.0032(-4) | -.0022(-2) |
| 4.5 | +.00027 | +.00068 | -.00357 | +.00001 | +.025 | 0 | -.0017(-1) | -.0048(-6) | -.0067(-6) |
| 4.0 | +.00004 | +.00011 | -.00424 | +.00071 | +.031 | +.0007 | -.0068(-4) | -.0088(-11) | -.0100(-9) |
| 3.5 | -.00031 | -.00080 | -.00950 | +.00149 | +.068 | +.0007 | -.0084(-5) | -.0136(-17) | -.0134(-12) |
| 3.0 | -.00106 | -.00273 | -.01320 | +.00287 | +.098 | -.0022 | -.0084 | -.0192(-24) | -.0190(-17) |
| 2.0 | -.00283 | -.00728 | -.01924 | +.00690 | +.148 | -.0066 | -.0135 | -.0488(-61) | -.0479(-43) |
(parenthesized = raw sims on 300-evt denominators v510=593 v1030=1249 dxy15=897; overlap not yet dedup'd -> a08_distinct pending for the chosen point)
FREE region ends at 4.5 (eff maximum, fakB at par). Below 4.0 the fakB price grows fast.
* -a2 5.0: eT +.00051 dupT -.00018 fakT -.00014 (small free transition win). -a2 4.0: fakT +.00086 (no).
* -a3 5.0: fakE +.00056 (endcap protected -> do not touch).
* -a4 4/6/8 (4-layer chain absorb): dud (best dupB -.00020 for fakB +.00029). DEAD END.

## PLAN
1. Joint -a x -RPSA scan (J-runs) + free-win composition (-a 4.5/5.0 + -a2 5.0).
2. Re-run B4X30/B4Q1; offline b04_frontier on r_B4G0.log B4P rows; XCQ vs flat bar at equal eff.
3. Read -CC machinery; design chain-vs-chain contention for the seedlessChain+SEEDEDchain cell.
4. Compose; one 977 confirmation of the best line.

## M2 -- BATCH 1 DONE (joint -a x -RPSA + XCQ real pair; tags F1C0/F1J60/F1J65/F1J45/F1Q15/F1QX)
* F1C0 (-a 4.5 -a2 5.0 -a3 6.0): eff +.00035 dupB -.00360 dupT -.00018 fakB -.00007 fakT
  -.00013 nhB +.025, displaced -1/-6/-6 (v510/v1030/dxy15). FREE WINS COMPOSE ADDITIVELY.
* Joint surface: at -a 4.5, -RPSA 4.5 buys only dupB -.00015 more for eff -.00044
  (conversion absorbs retirement targets); -RPSA 6.0/6.5 regains +.00005/+.00009 eff at
  +.00003/+.00008 dupB but dupE +.00020/+.00040. VERDICT: keep -RPSA 5.5.
* B04 -XCQ FALSIFIED by real runs: F1Q15 (-XCQ 1.5) eff -.00199 effB -.00512 for dupB
  -.00980 (offline b04_frontier.py predicted -.00049 -- price model 4x optimistic).
  F1QX (XCT 3.0 + XCQ 1.0) eff -.00336 for dupB -.01543. At matched dupB, conversion
  -a 3.5 (eff -.00031) strictly dominates. Owner-credibility retirement is DEAD; the
  branch census (cov0 vs cov2 identical owner-branch mix) explains why.

## M3 -- -CCS CHAIN-LOSER SUPPRESSION (new mechanism for the seedlessChain+SEEDEDchain cell)
Cell characterized on r_D1 (300): 268 chain-side + 272 seeded-side barrel members/300evt
= 1.8/evt (census said 1.899). ALL 272 seeded partners are isChain=2 = assembled
pixel-anchored chains (attach conversions), br2 254/br3 18; the bare loser is br2 175 /
br3 89. Both members are ACCEPTED chains -> they share sub-claim-budget MDs by
construction (K9 allows it), so hit-counting cannot separate them. But they SHARE THE
SEED: the loser scored a pair (in ga.pairLog) with the pLS the winner owns.
MECHANISM: suppress an accepted BARE chain whose best scored pair logit toward a pLS
owned by a DIFFERENT chain is >= band bar. Ownership-map, existing quantities only.
CODE (protoB02/main.cc, 4 edits): flag decls ccsTheta/T/E (1e9=OFF per band, no
follow-barrel -- endcap protected); pre-scan parse -CCS2/-CCS3/-CCS before -CC;
recordPairs |= ccsOn; build ccsLoserLogit once per event (invert chainAttachPls ->
plsOwnerChain, one pairLog pass) + suppression check in the emission loop before the
type-7 upgrade block (bare chains only, tcPos lockstep intact, plsBestChainLogit/-RPSA
untouched). Built clean.

## M4 -- -CCS MEASURED (batch 2 done); GATE PASSED (F1SG vs r_D1: 33/33, 0 ADDED)
Isolated on CHAINFINAL (300):
| point | d_eff | d_dupB | d_dupT | d_fakB | d_fakT | displ distinct DISP1 |
|---|---|---|---|---|---|---|
| -CCS 8.0 | -.00004 | -.00055 | 0 | -.00001 | 0 | (small) |
| -CCS 6.0 | -.00013 | -.00206 | 0 | -.00039 | 0 | -1 |
| -CCS 4.0 | -.00022 | -.00303 | -.00003 | -.00185 | 0 | -13 v1030 raw (over budget) |
| -CCS 2.0 | -.00133 | -.00433 | -.00003 | -.00659 | +.00002 | collapse |
| -CCS2 6.0 | -.00004 | -.00004 | -.00397 | 0 | -.00034 | -1 |
-CCS kills fake alongside dup (the losers include fake chains camped on real seeds).
DISPLACED BUDGET (a08_distinct, DISP1 distinct sims vs CHAINFINAL): -a 5.5 -4 | -a 5.0
-6 | -a 4.5 -11 (OVER the single-sims budget) | -a2 5.0 +0 | -CCS 6.0 -1 | -CCS2 6.0 -1.
All -a losses = bare chains converted with a WRONG seed. => -a 5.0 is the budget point.

## M5 -- COMPOSITIONS (batch 3+4 done). WINNER = F1B4
| tag (vs CHAINFINAL) | d_eff | d_dupB | d_dupT | d_fakB | d_fakT | DISP1 distinct |
|---|---|---|---|---|---|---|
| F1B1 (-a 5.0 -a2 5.0 -CCS 6 -CCS2 6) | +.00013 | -.00442 | -.00414 | -.00059 | -.00049 | -7 |
| F1B2 (-a 4.5 variant)                | +.00018 | -.00569 | -.00415 | -.00047 | -.00048 | (~-12, over) |
| F1B3 (-CCS 5 -CCS2 5, -a 5.0)        | +.00004 | -.00510 | -.00577 | -.00114 | -.00114 | -12 (over) |
| F1B4 (-a 5.0 -a2 5.0 -CCS 6 -CCS2 5) | +.00013 | -.00443 | -.00574 | -.00059 | -.00114 | -8 |
Deltas add linearly across the two mechanisms (verified against isolated points).
F1B4 dominates F1B1 (same eff/dupB, deeper dupT/fakT, +1 sim). Endcap flat everywhere
(dupE +.00001 fakE +.00003, from the -a2 spillover at the 1.7 boundary in T50 already).
Length: nh +.0035, nhB +.0161, nhT -.0077 -- the nhT dip is COMPOSITION (removing long
duplicate chain rows lowers the mean), no kept track is shortened.
F2 shared-log entries: -MRB -1.2 / -MRT -1.5 free fake bars (co-compose candidates);
flat -XCT digs the barePLS cell deeper at real eff cost (their B35: dupB -.00495 @
-.00053). Trap logged: -CCS-suppressed chains can no longer retire seeds via -XC ->
slight sub-additivity if F2 tightens -XCT on top.

## M6 -- 977 CONFIRMATION (F1W = F1B4 line, full 977, deltas vs W_D1 = CHAINFINAL 977)
RECOMMENDED OVERRIDE LINE (binary: protoB02/bin/chainproto):
  -T3F 0.10 -XC4 1 -RPSA 5.5 -EXR 4.0 -a 5.0 -a2 5.0 -a3 6.0 -CCS 6.0 -CCS2 5.0
| metric | CHAINFINAL | F1W | delta | LST |
|---|---|---|---|---|
| eff    | .80978 | .80983 | +.00005 | .80987 (now -.00004 below) |
| dupB   | .03056 | .02610 | -.00446 | .00971 |
| dupT   | .02793 | .02235 | -.00557 | .01308 |
| dupE   | .07184 | .07185 | +.00000 | .08486 (still below, protected) |
| fakB   | .05507 | .05437 | -.00070 | .04365 |
| fakT   | .05998 | .05874 | -.00123 | .04542 |
| fakE   | .04310 | .04312 | +.00002 | .04630 (still below) |
| nh/nhB/nhT | 6.49987/9.92631/9.99670 | +.0031/+.0152/-.0064 | | (nhT dip = composition: long dup rows removed) |
Displaced distinct (a08, 977): DISP1 lead +716 -> +682 (spent 34 net: 37 lost/3 gained,
T5 conversions with wrong seed); DISP5 -30, DISP10 -25, DISP30 -2. 95% of the win kept.
Dial-back levers if 34 is over budget: -CCS2 6.0 (saves ~2-6 sims, costs dupT .0016),
-a 5.5 (saves ~2/300evt, costs dupB .0020).

## CODE CHANGES (all in standalone/protoB02; all default-inert; gates all 33/33 vs r_D1)
A) -a2/-a3 attach-margin bands (inherited from B02 agent, gates B02G0/B02G1):
   main.cc:851-852 (decls), 1213-1216 (pre-scan parse, MUST precede getopt "a:"),
   1521-1524 (sentinel resolution post-getopt), 3111-3112 (gap assignment);
   AttachDelivery.h:67-68 (fields); AttachDelivery.cc:77-79 + 92-97 (band lookup at the
   ONE acceptance site).
B) -a4 4-layer absorb (inherited, gate B02G2): measured DUD -- recommend NOT porting.
C) -CCS/-CCS2/-CCS3 chain-loser suppression (F1, gate F1SG):
   main.cc:853-867 (decls+mechanism comment), 1264-1269 (pre-scan parse before -CC),
   3149 (recordPairs |= ccsOn), 4062-4087 (per-event ccsLoserLogit build: invert
   chainAttachPls -> plsOwnerChain, one ga.pairLog pass), 4127-4141 (suppression in the
   emission loop, bare chains only, tcPos lockstep intact, plsBestChainLogit untouched).
protoB04 -XCQ: FALSIFIED, recommend discarding (keep b04_ref for the record).

## COMPOSITION NOTES / KNOWN CONFLICTS
* -CCS-suppressed chains leave outTCs -> they can no longer retire seeds via the -XC
  bare-chain crossclean: slight sub-additivity with F2's -XCT tightening (same-side
  coupling; no double-deletion possible -- different objects).
* Keep -RPSA 5.5: post-conversion retirement is nearly dead (M2).
* F2's -MRB -1.2 / -MRT -1.5 free fake bars are upstream (admission): they will shrink
  -CCS/-a target populations slightly; re-verify the combined line before shipping.
* Endcap: leave -a3 6.0 and -CCS3 unset (both measured harmful/unneeded; dupE/fakE
  protected below LST).
