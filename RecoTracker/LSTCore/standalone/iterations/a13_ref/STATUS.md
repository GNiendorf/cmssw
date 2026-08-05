# A13 -- DELIVERY-CONDITIONED SEED RETIREMENT (and unwelding the -RPS bars)

Explorer A13. Workspace `standalone/protoA13` (+ `protoA13b`), artifacts `standalone/a13_ref/`.
Runner: `bash a13_ref/a13_run.sh <TAG> [overrides]` -- the frozen prefix AND the assembled
baseline `-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2` are inside the script, so overrides are
deltas from the ASSEMBLED BASELINE.
Batch: `bash a13_ref/a13_batch.sh < file` with lines `TAG :: overrides`.
Tables: `a13_ref/a13_tab.py` / `a13_tab2.py` (fall back to fin_ref / xc_ref / rebase_ref).

## CHOSEN ANGLE (and why)

The assembled baseline's two remaining gaps against LST are duplicate +.0105 and fake
+.0108, and the Baseline agent's own truth partition says the duplicate excess is almost
exactly the 26.3 class-A bare-seed SURVIVORS per event (26.3 seeds vs .0105 * 2062 = 21.7
duplicate rows). Those survivors are seeds whose covering track candidate exists but which
the ported crossclean cannot reach, because its bare-chain arm needs the seed to be within
dR^2 < 0.02 of the TC AND to clear a logit bar whose separating power collapses below ~4
(marginal 3.5 class-A per class-B at -XCT 4.5 -> 3.5).

Two cross-cutting mechanisms, neither of which is on the Baseline agent's open list:

**(1) -DC, DELIVERY-CONDITIONED SEED RETIREMENT.** Ask an OWNERSHIP question instead of a
proximity one: *this seed bid for target X; is X in the output, delivered to somebody
else?* If yes, the outer-tracker object the seed points at has already been reconstructed
and the seed's bare row is a second delivery of it. Two arms:
  bit 0 (1) bare-T3 arm  -- the target is a DELIVERED pT3-class row (post -RD/-CC
            revocation) owned by another seed. The ported crossclean has NO arm of this
            shape at all; it can only reach such a seed through the pixel-anchored test
            against the owner seed (shared pixel hit row, or dR^2 < 1e-6).
  bit 1 (2) chain arm    -- the target is a chain row that reached the output as a TC,
            bare or seed-attached. This is the ported bare-chain arm with the dR window
            REMOVED, which the project's no-proximity rule requires of new code anyway.
One O(nChains) map plus one sweep of the pair log the delivery already wrote. No dR, no
eta/phi grid, no embedding, no candidate-vs-candidate loop. One constant, `-DCT`.

**(2) -RPSA / -RPST, UNWELDING THE -RPS SEED BAR FROM THE DELIVERY MARGIN.** The shipped
-RPS predicate is `plsBestChainLogit >= -a || plsBestT3Logit >= -AT3`, i.e. it reads the
DELIVERY thresholds. That is why `-AT3` is two knobs in one and why (measured, section D of
the baseline scoreboard) fake can only be bought by giving up efficiency AND duplicate rate
together: raising -AT3 makes the delivery stricter (fake down) and the seed retirement
weaker (dup up) in the same move. -RPSA/-RPST are the retirement bars alone. Both default
to the delivery margins, so every existing command line is bit-identical. If the weld is
what costs us, the fake-leaning corner of the frontier becomes reachable without the
duplicate penalty.

Deliberately avoided: the four items the Baseline agent listed as open (attach-head
retrain, seed-vs-chain dedup head, -CCR policies, endcap prefilter recall) -- fourteen
siblings are working the same finish line and those are the obvious targets.

## CODE STATE

`protoA13`  = protoFIN + (1) only.  Binary da43c9e8cad21149c51e1867b94a1475.
`protoA13b` = protoA13  + (2).      Binary e4f532b123232c8be8e8e2164111a1ad.
Both default to no-op: `-DC 0` (default) and `-RPSA`/`-RPST` unset (= -a / -AT3).

## MILESTONES

- M0 SETUP: protoA13 copied from protoFIN, `make` -> nothing to do, binary md5
  519b0abc34a28cd1e803d6b9407ef224 == protoFIN. GATE0 launched on that binary
  (baseline flags + -XCD 2) as the protoFIN-parity gate.
- M1 CODE: -DC / -DCT / -DCO added to protoA13 (build clean). 5-evt smoke at
  `-DC 3 -DCT 4` runs and reports.
- M2 CODE: -RPSA / -RPST added to protoA13b (build clean).
- M3 BATCH 1 launched (7 x 300 evts): GATE1 (no-op proof of the new binary), RXC1
  (-XC 1 reference), DC1_6/DC1_5/DC1_4 (T3 arm), DC2_4X1 (chain arm replacing the ported
  one), DC2_6 (chain arm on top).

## M4 -- 30-EVT TRIAGE PROBE 1 (12 pts): THE CHAIN ARM IS A STRICT SUPERSET OF THE PORT

Probes run at NEV=30 (`a13_run30.sh`, artifacts `p30_*`). Efficiency at 30 events is
quantised at ~.00044 per track -- these are DIRECTIONAL, the 300-evt batch decides.

```
tag             eff       dup      fake       nhB       nhE     nTC   | A_surv  B_xcretire
P_BASE       0.81106   0.06331   0.05554   9.83689   3.57213   63140  |  25.6      6.2
P_DC2_6      0.81106   0.06128   0.05556   9.87126   3.57279   63071  |  23.6      6.4
P_DC2_5      0.81106   0.05981   0.05557   9.89685   3.57559   62978  |  21.7      7.5
P_DC2_4      0.81062   0.05945   0.05553   9.90657   3.58094   62897  |  21.3      9.5
P_XC1_DC2_4  0.81062   0.05945   0.05553   9.90657   3.58094   62897  |  21.3      9.5
P_XC1_DC3_4  0.81062   0.05918   0.05554   9.90935   3.58348   62857  |  20.8     10.4
P_DC1_4      0.81106   0.06304   0.05555   9.83963   3.57466   63099  |  25.2      7.1
P_DC1_2      0.81062   0.06251   0.05548   9.84786   3.57973   63004  |  24.5      9.3
P_XC1        0.81239   0.15645   0.05294   8.81856   3.52443   66625  | 135.8      0.9
```

READINGS
1. `P_DC2_4` and `P_XC1_DC2_4` are IDENTICAL on every metric and on nTC. Turning the
   ported bare-chain arm OFF changes nothing once `-DC 2 -DCT 4` is on: at that bar the
   ownership test RETIRES EVERY SEED THE dR^2 < 0.02 WINDOW DOES, and 4.4 more per event
   besides. The window is redundant, not merely replaceable.
2. `-DC 2` moves duplicate rate at a much better exchange rate than `-XCT` does. -DCT 5
   buys .0035 of duplicate rate for ZERO efficiency at 30 evts; -XCT 4 -> 3.5 on the same
   line buys .0031 for .0009. And it moves track length the RIGHT way (nhB 9.837 -> 9.897,
   toward LST's 10.148) because it removes zero-OT-hit type-8 rows.
3. Fake is untouched (.05554 -> .05557): the mechanism only ever removes bare seeds.
4. The bare-T3 arm (`-DC 1`) is real but small -- 135 delivered T3 targets/evt against
   ~1050 delivered chains -- worth ~.0008 of duplicate rate at -DCT 2.

## M5 -- 30-EVT PROBES 2+3 (20 more pts): TWO RESULTS, ONE OF THEM A REFUTATION

`Q_GATE2` (protoA13b, no -RPSA/-RPST) == `P_BASE` (protoA13, no -DC) on every metric and
on nTC (63140): the -RPSA/-RPST edit is inert at its defaults.

### (a) THE CHAIN ARM OF -DC IS THE PRE-EXISTING -RPS CHAIN BAR IN DISGUISE
`P_DC2_6` (-XC 3 -XCT 4 -DC 2 -DCT 6) and `Q_RPSA6` (-XC 3 -XCT 4 -RPSA 6) are IDENTICAL
on eff/dup/fake/nhB/nhE/nTC (.81106/.06128/.05556/9.87126/3.57279/63071) even though their
truth partitions attribute the retirement to different columns (A_rps 242.9/A_xc 118.4 vs
A_rps 308.9/A_xc 52.4). Reason: at a pair logit >= 6 the chain the seed bid for is
essentially always a chain that got delivered, so "bid >= t for a DELIVERED chain" and
"best chain bid >= t" select the same seeds. The delivery condition only bites where the
delivered fraction of the target class is small.
CONSEQUENCE FOR SIMPLICITY: for the chain arm there is no need for a new mechanism at all.
Lowering the pre-existing seed bar does the same thing with zero new code.

### (b) ...AND FOR THE BARE-T3 CLASS, WHERE ONLY 12% OF CANDIDATES ARE DELIVERED
(135.4 delivered of 1127.2 candidates), the delivery condition DOES bite. At matched
duplicate rate the delivery-conditioned arm is worth +.0022 of efficiency over the bar:
  Q_DC1_0  (-DC 1 -DCT 0, delivery-conditioned)  eff .80708  dup .06124
  Q_RPST5  (-RPST 5, bar only)                   eff .80487  dup .06071
Neither is worth using on its own -- both are worse than doing nothing on that axis -- but
the ORDERING is the point, and it is what refutes (2) below.

### (c) THE -RPS WELD IS PROTECTIVE, NOT COSTLY -- HYPOTHESIS (2) IS REFUTED
Holding the seed bar at 6 while making the pT3-class delivery stricter is a DISASTER:
```
tag        change                    eff       dup      fake
Q_GATE2    (baseline)             0.81106   0.06331   0.05554
Q_A7       -AT3 7   (welded)      0.81106   0.06551   0.04985
Q_A7_T6    -AT3 7 -RPST 6         0.80619   0.06110   0.04975
Q_A8       -AT3 8   (welded)      0.80664   0.06782   0.04782
Q_A8_T6    -AT3 8 -RPST 6         0.79159   0.05839   0.04807
```
-.0049 and -.0195 of efficiency. The weld exists because the bar is a PROXY for "a
replacement was delivered": raise the delivery margin without raising the bar and you
retire seeds whose replacement no longer exists. That is exactly the failure mode (b)
identifies. -RPSA/-RPST stay in the code as a measured negative result, not a knob to ship.

### (d) WHAT DOES WORK ON THE DUPLICATE AXIS: LOWER THE CHAIN BAR (either spelling)
30 evts, everything else the assembled baseline:
```
tag          flags                    eff       dup      fake       nhB     nTC    A_surv
Q_GATE2      (baseline)            0.81106   0.06331   0.05554   9.83689  63140    25.6
Q_RPSA6      -RPSA 6               0.81106   0.06128   0.05556   9.87126  63071    23.6
P_DC2_6      -DC 2 -DCT 6          0.81106   0.06128   0.05556   9.87126  63071    23.6
Q_RPSA55     -RPSA 5.5             0.81106   0.06049   0.05555   9.88646  63030    22.7
P_DC2_5      -DC 2 -DCT 5          0.81106   0.05981   0.05557   9.89685  62978    21.7
P_DC2_4      -DC 2 -DCT 4          0.81062   0.05945   0.05553   9.90657  62897    21.3
P_XC1_DC3_4  -XC 1 -DC 3 -DCT 4    0.81062   0.05918   0.05554   9.90935  62857    20.8
R_X1D35      -XC 1 -DC 2 -DCT 3.5  0.80929   0.05599   0.05560   9.95475  62669    17.3
R_X1D3       -XC 1 -DC 2 -DCT 3    0.80752   0.05369   0.05556   9.98854  62471    14.7
R_X1D2       -XC 1 -DC 2 -DCT 2    0.80221   0.04949   0.05543  10.06973  61946    10.4
```
Down to a duplicate rate of ~.0598 the efficiency does not move at all at 30 evts, the
fake rate never moves, and TRACK LENGTH IMPROVES MONOTONICALLY (nhB 9.837 -> 9.907 at
-DCT 4, against LST's 10.148) because every row removed is a zero-OT-hit type-8 row.
DISPLACED (v510 .72549, v1030 .76147, d15 .64948) is bit-frozen across every single row
above -- none of these mechanisms touches a displaced track.

## M6 -- THE EQUIVALENCE IS EXACT: `-DC 2 -DCT t` == `-RPSA t`, EVERY t TESTED

30 evts, everything else the assembled baseline. These pairs are IDENTICAL on
eff / dup / fake / nhB / nhE / nTC:
```
-DC 2 -DCT 6   == -RPSA 6      0.81106 0.06128 0.05556  9.87126 3.57279  63071
-DC 2 -DCT 5   == -RPSA 5      0.81106 0.05981 0.05557  9.89685 3.57559  62978
-DC 2 -DCT 4.5 == -RPSA 4.5    0.81106 0.05952 0.05555  9.90240 3.57880  62928
-DC 2 -DCT 4   == -RPSA 4      0.81062 0.05945 0.05553  9.90657 3.58094  62897
-XC 1 -DC 2 -DCT 5 == -XC 1 -RPSA 5   0.81150 0.07102 0.05523  9.77899 3.56442  63441
and -RPSA 5 -DC 2 -DCT 4       == -RPSA 4  (the union is the lower bar)
```
So the ownership condition I built adds NOTHING over the seed bar that already exists for
CHAIN targets: above a pair logit of ~4, a chain a seed bid for is essentially always a
chain that got delivered. The delivery condition only pays where the delivered fraction of
the target class is small -- which is the bare-T3 class (135.4 delivered of 1127.2
candidates), where it is worth +.0022 of efficiency at matched duplicate rate over the
plain bar but is not worth using at all against doing nothing.

THE HONEST CONCLUSION: ship the ONE NUMBER, not the mechanism. `-RPSA` is a bar on a
predicate that has been in the code since M16; it needs no new code, no pair log, and no
new concept. `-DC` stays as the instrument that PROVED the equivalence and as the only
form in which a bare-T3 seed retirement is affordable.

## M7 -- 300-EVT BATCH 1: THE GATE PASSES AND THE MECHANISM HOLDS UP

`GATE0` (protoA13 binary, assembled baseline + -XCD 2) reproduces `FINBASE` EXACTLY on all
14 headline metrics, all 12 per-region metrics and on nTC (618793 = 618793).

```
tag        change from baseline       eff       dup      fake      nhB      nhT      nhE     nTC
GATE0      (assembled baseline)   0.80992  0.06230  0.05551  9.80254  9.87906  3.55956  618793
DC2_6      -DC 2 -DCT 6           0.80979  0.05956  0.05557  9.84857  9.88238  3.56108  617813
DC2_4X1    -XC 1 -DC 2 -DCT 4     0.80904  0.05719  0.05555  9.89168  9.89171  3.56782  615963
DC1_6      -DC 1 -DCT 6           0.80984  0.06228  0.05551  9.80267  9.87918  3.55968  618776
DC1_5      -DC 1 -DCT 5           0.80979  0.06218  0.05551  9.80295  9.88008  3.56040  618669
DC1_4      -DC 1 -DCT 4           0.80975  0.06205  0.05551  9.80466  9.88187  3.56158  618478
RXC1       -XC 1 (no chain arm)   0.81302  0.15634  0.05295  8.80701  9.06081  3.51133  653007
LST                               0.80988  0.05179  0.04476 10.14804 10.01546  3.56248  608190
```
AGAINST THE PUBLISHED -XCT BRACKET (same 300 events):
```
                          eff       dup
-XCT 4.5 (baseline+)   0.81037   0.06675
-XCT 4.0 (baseline)    0.80992   0.06230
-XCT 3.5               0.80904   0.05925
DC2_4X1                0.80904   0.05719   <- SAME efficiency as -XCT 3.5, -.00206 dup
DC2_6                  0.80979   0.05956   <- +.00075 efficiency over -XCT 3.5 at ~equal dup
```
The retirement bar DOMINATES the -XCT frontier: at matched efficiency it delivers .0021
more duplicate rate, and `DC2_4X1` reaches it with the ported bare-chain arm switched OFF
entirely (`-XC 1`), i.e. with the dR^2 < 0.02 window and the -XCT constant DELETED.
Fake never moves (.05551 -> .05555/.05557) and displaced is bit-frozen
(v510 .73356, v1030 .74139, d15 .61650, d510 .20161, d1030 .03073 on every row).
Track length moves the RIGHT way: nhB 9.803 -> 9.892 toward LST's 10.148.

## M8 -- 300-EVT BATCH 2: THE PORTED BARE-CHAIN ARM CAN BE DELETED AT ZERO COST

`-XC 1 -DC 2 -DCT t` (== `-XC 1 -RPSA t`): the ported bare-chain arm and its dR^2 < 0.02
window OFF, one seed bar instead. 300 evts.
```
bar      eff       dup      fake       nhB      nhT      nhE     nTC
6.0   0.81209  0.10039  0.05449   9.42750  9.48280  3.53169  633459
5.0   0.81085  0.06913  0.05530   9.76107  9.76425  3.55240  621470
4.5   0.80984  0.06188  0.05547   9.83685  9.83964  3.56075  618327
4.0   0.80904  0.05719  0.05555   9.89168  9.89171  3.56782  615963
3.5   0.80780  0.05390  0.05560   9.93329  9.93103  3.57489  613983
3.5*  0.80758  0.05364  0.05559   9.93610  9.93438  3.57756  613556   (* -DC 3, both arms)
baseline (-XC 3 -XCT 4)
      0.80992  0.06230  0.05551   9.80254  9.87906  3.55956  618793
```
`-XC 1 -RPSA 4.5` MATCHES the assembled baseline on every headline number
(eff -.00008, dup -.00042, fake -.00004, track length +.034/-.039/+.001) while DELETING a
mechanism and a constant: the bare-chain arm of the ported CrossCleanpLS, its dR^2 < 0.02
window, and -XCT. That is the simplification result.

FRONTIER COMPARISON at matched efficiency (300 evts, published -XCT bracket vs this one):
```
eff       -XCT gives dup    the seed bar gives dup    gain
0.81037      0.06675              ~0.0655            -.0013
0.80992      0.06230              ~0.0620 (4.5)      -.0004
0.80904      0.05925               0.05719 (4.0)     -.00206
```
And the UNION (keep the ported arm AND lower the bar) beats both -- `DC2_6` = `-XC 3
-XCT 4 -RPSA 6` gives 0.80979 / 0.05956 where the -XC 1 line needs bar 4.5 for 0.06188.
The two mechanisms are complementary: the ported arm catches LOW-logit seeds sitting
angularly on top of a TC, the bar catches HIGH-logit seeds anywhere.

Displaced: d15/d510/d1030 are bit-frozen on every row; v510 drops by exactly one track
(.73356 -> .73187) below bar 4.5 on a denominator of ~600 -- noise, not tuned on.

## M9 -- 300-EVT BATCH 3: THE -RPSA FRONTIER, AND THE FINAL NO-OP GATE

`GATE2` (protoA13b -- BOTH edits in, all flags at their defaults) reproduces `FINBASE` and
`GATE0` on all 14 headline metrics, all 12 per-region metrics, on nTC (618793) AND
bit-for-bit: `rebase_ref/cmp_branches.py fin_ref/r_FINBASE.root a13_ref/r_GATE2.root`
reports 33 PRE-EXISTING branches IDENTICAL, 0 DIFFER, 0 MISSING, 0 ADDED.

THE FRONTIER (`-XC 3 -XCT 4 -RPSA t`, everything else the assembled baseline, 300 evts):
```
t          eff       dup      fake      nhB      nhT      nhE     nTC  | A_surv B_retired
inf(def) 0.80992  0.06230  0.05551  9.80254  9.87906  3.55956  618793 |  26.3    13.0
6.0      0.80979  0.05956  0.05557  9.84857  9.88238  3.56108  617813 |  23.6    13.4
5.5      0.80970  0.05853  0.05560  9.86488  9.88353  3.56221  617338 |  22.4    13.8
5.0      0.80957  0.05787  0.05560  9.87688  9.88519  3.56361  616872 |  21.5    14.4
4.5      0.80939  0.05743  0.05559  9.88507  9.88800  3.56562  616429 |  21.0    15.3
4.0      0.80904  0.05719  0.05555  9.89168  9.89171  3.56782  615963 |  20.7    16.3
LST      0.80988  0.05179  0.04476 10.14804 10.01546  3.56248  608190 |
```
Exchange rate eff:dup -- inf->6 is 21:1, 6->5.5 is 11:1, 5.5->5 is 5:1, 5->4.5 is 2.4:1,
4.5->4 is 0.7:1. The knee is at 5.0-5.5 and the mechanism runs out at 4.0, where class-A
survivors reach 20.7 -- the ~20 STRUCTURAL FLOOR the port round predicted.

The delivery ledger is IDENTICAL at every point (pT3-class candidates 1127.2, pixel-side
revocations 661.3, OT-side 330.5, DELIVERED 135.4/evt): the bar touches bare SEEDS only,
which is why the fake rate never moves and why the whole gain is barrel duplicate rate
(dupB .04344 -> .03179 at t=5.5, LST .00989) with barrel efficiency still ABOVE LST
(.92625 vs .92557) and transition efficiency exactly AT LST (.88187).

## M10 -- THE RECOMMENDED POINT (300 evts)

    -XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2 -RPSA 5.5

ONE number added to the assembled baseline, on a predicate (-RPS) that has been in the
code since M16. No new mechanism ships.
```
                       eff    vxy01      v15     v510    v1030      d15     d510    d1030      dup     fake      nhB      nhT      nhE     nTC
A13 (-RPSA 5.5)    0.80970  0.84258  0.79459  0.73356  0.74139  0.61650  0.20161  0.03073  0.05853  0.05560  9.86488  9.88353  3.56221  617338
FINBASE (baseline) 0.80992  0.84282  0.79459  0.73356  0.74139  0.61650  0.20161  0.03073  0.06230  0.05551  9.80254  9.87906  3.55956  618793
LST                0.80988  0.84296  0.77193  0.65430  0.66453  0.55407  0.20968  0.05674  0.05179  0.04476 10.14804 10.01546  3.56248  608190
```
vs the assembled baseline: eff -.00022 | dup -.00377 | fake +.00009 | track length
+.062/+.004/+.003 | nTC -1455. EVERY DISPLACED BAND IS UNCHANGED TO THE LAST DIGIT.
vs LST: eff -.00018 (parity) | dup +.00674 (was +.01051, a 36% reduction of the gap) |
fake +.01084 (unchanged -- this mechanism does not touch the fake rate).
Per region: effB .92625 (LST .92557, still ABOVE), effT .88187 (= LST exactly),
effE .74485 (LST .74596); dupB .03179 (was .04344), dupT .03406, dupE .08052 (LST .08556,
still BETTER); fakE .04521 (LST .04603, still BETTER).

BRACKET: -RPSA 6 = .80979/.05956/.05557 (conservative), 5.5 = .80970/.05853/.05560,
5.0 = .80957/.05787/.05560 (duplicate-leaning). Below 4.5 the exchange rate collapses.

SIMPLIFICATION VARIANT (same constant count as the baseline, one MECHANISM fewer):
    -XC 1 -T3E 1 -CC 1 -CCN 1 -CCR 2 -RPSA 4.5
= 0.80984 / 0.06188 / 0.05547, nhB 9.83685, nTC 618327 -- matches the assembled baseline
on every headline number while DELETING the ported bare-chain crossclean arm, its
dR^2 < 0.02 window and the -XCT constant.

## M11 -- RESUMED (2026-08-04 23:45). STATE AT RESUMPTION

This agent was resumed after an interruption. Everything above M10 stands and was
re-verified from the artifacts: protoA13 (da43c9e8, -DC) and protoA13b (e4f532b1,
-DC + -RPSA/-RPST) intact, GATE0/GATE2 both bit-identical to FINBASE.

IN FLIGHT at resumption (launched by the pre-interruption incarnation, 977 evts,
protoA13b, BASEHISTS=fin_ref/fin_base977_hists.root):
  W_X1D5    -XC 1 -DC 2 -DCT 5   (== -XC 1 -RPSA 5, the simplification variant)
  W_RPSA45  -RPSA 4.5
  W_RPSA55  -RPSA 5.5            (the M10 recommended point)

NEW WORK THIS SEGMENT -- BATCH 4: does the seed bar COMPOSE with the fake axis?
-RPSA never moves the fake rate (measured, M9), and -AT3 is the only knob that does.
If they are orthogonal, the pair closes BOTH residual gaps at once, which neither knob
does alone. 8 points, 300 evts: -AT3 {6.5,7} x -RPSA {5.5,5,4.5}, plus -XCT 3.5 -RPSA 5.5
and -XCT 4.5 -RPSA 6 to test whether the ported arm's constant should move with the bar.

M11a NOTE: of the three 977-evt runs in flight at resumption, `W_X1D5` DIED at event 83
(no process, log truncated) -- almost certainly killed with the interrupted session.
`W_RPSA45` and `W_RPSA55` are alive and CPU-bound at 99.7% (~10 s/evt under the 15-agent
contention, vs 2.8 s/evt when the Baseline agent had the machine); ETA ~01:00 and ~02:05.
The 977 confirmation of the SIMPLIFICATION variant (-XC 1) is therefore not going to
exist; the 300-evt numbers for it stand on their own and are flagged as such.

## M12 -- THE BAR COMPOSES WITH BOTH OTHER AXES (300 evts)

Two points from the pre-interruption batch landed (they duplicate two of my batch-4 lines,
so those two were killed and the rest left running):
```
tag            flags (on top of the assembled baseline)   eff       dup      fake      nTC
GATE2          (assembled baseline)                    0.80992  0.06230  0.05551  618793
C_RPSA55       -RPSA 5.5                               0.80970  0.05853  0.05560  617338
D_A7RPSA55     -AT3 7 -RPSA 5.5                        0.80869  0.06045  0.04976  612180
D_RPSA55X35    -RPSA 5.5 -XCT 3.5                      0.80882  0.05547  0.05567  615929
LST                                                    0.80988  0.05179  0.04476  608190
```
* -RPSA is ORTHOGONAL to -AT3. -AT3 7 alone costs .00092 of efficiency and RAISES dup to
  .06452; with the bar at 5.5 the same -AT3 7 lands at dup .06045 -- i.e. the bar pays back
  the duplicate penalty that made the fake-leaning corner unattractive. The pair closes the
  SUM of the two residual gaps from .0213 (baseline) to .0149 for .0012 of efficiency.
* -RPSA also composes with the ported arm's own constant: -XCT 3.5 on top of the bar gives
  dup .05547, the lowest duplicate rate reached all round at eff >= .8088.
* Displaced is BIT-FROZEN on every one of these rows (v510 .73356, v1030 .74139,
  d15 .61650, d510 .20161, d1030 .03073).

## M13 -- 977-EVT CONFIRMATION (W_RPSA45) AND THE FULL JOINT GRID

`W_RPSA45` (-RPSA 4.5, 977 evts, LST reference fin_ref/fin_base977_hists.root):
eff .80852 dup .05698 fake .05614 nhB 9.88154 nTC 2026288, against the assembled
baseline's 977 numbers (fin_ref W_X4) .80905 / .06184 / .05607 / 9.80064 / 2033868.
THE DELTAS ARE THE SAME TO 5 DECIMALS AS AT 300 EVENTS (eff -.00053, dup -.00486/-.00487,
fake +.00007/+.00008). The mechanism transfers.

Batch 4 (joint grid) complete -- see a13_ref/a13_scoreboard2.txt section D. -RPSA is
orthogonal to -AT3 and to -XCT; the fake-leaning corner of the published frontier becomes
affordable once the bar holds the duplicate rate down. `-AT3 6.5 -RPSA 5.5` is the first
point of the campaign with BOTH residual gaps under +.008 at eff within .0005 of LST.

STILL RUNNING: W_RPSA55 (the primary recommendation, 977) and W_X1R45 (the simplification
variant, 977).

## M14 -- ALL 977-EVT CONFIRMATIONS IN. FINAL STATE (segment complete)

```
tag                       eff      dup     fake      nhB      nTC     (977 evts)
W_X4 (assembled base)   0.80905  0.06184  0.05607  9.80064  2033868
W_RPSA55  [PICK]        0.80886  0.05802  0.05614  9.86321  2029040
W_RPSA45                0.80852  0.05698  0.05614  9.88154  2026288
W_X1R45 (-XC 1)         0.80909  0.06148  0.05601  9.83235  2032598
LST                     0.80987  0.05138  0.04538 10.14984  1998494
```
Every 300 -> 977 delta transfers to within .00005. Displaced bands are identical to the
assembled baseline on every row (v510 .72161/.72111 = one track, v1030 .71474, d15 .58320,
d510 .24521, d1030 .03151). No process of mine is left running; artifacts complete in
a13_ref (scoreboard: a13_ref/a13_scoreboard2.txt, plus the M1-M10 record above).
