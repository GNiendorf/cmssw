# ASSEMBLED BASELINE (-T3E 1) -- STATUS

Agent role: BASELINE. Fifteen explorers start from the flag line this file names.
Workspace: `standalone/fin_ref/`. Code: `standalone/protoFIN/` (byte-identical copy of
`protoXC`; protoXC / protoBASE / prototype UNTOUCHED, no source edits planned).

Runner: `fin_ref/fin_run.sh <TAG> [overrides]` -- identical frozen line to
`xc_ref/xc_run.sh` except BIN=protoFIN and artifacts land in fin_ref. `-T3E` moved OUT of
the hardcoded POSTDELP2 string into the overrides so the gate (-T3E 0) and the assembled
line (-T3E 1) come through the same script.
Tables: `fin_tab.py` (overall+bands), `fin_tab2.py` (per-region). Both fall back to
xc_ref/ then rebase_ref/ so old tags can be quoted in the same table.

## M0 -- SETUP (DONE)
protoFIN copied from protoXC; `make -j 12` -> "Nothing to be done" (objects copied);
binary md5 e8380a9f788cb345284ab83c5e4f531d == protoXC's. No rebuild needed, no edits.

## M1 -- NO-OP GATE: PASSED EXACTLY
tag GATE = `-XC 3 -XCT 4.5 -T3E 0` reproduces xc_ref's B_p45 on EVERY metric to 5 decimals
and on nTC exactly (609229 = 609229): eff .80215 dup .07727 fake .04704. Wall 101.8 s.

## M1 -- BATCH 1 LAUNCHED (6 points, 300 evts, parallel)
  GATE     -XC 3 -XCT 4.5 -T3E 0                 <- no-op gate, must give .80215/.07727/.04704
  A6_CC0   -XC 3 -XCT 4.5 -T3E 1 -AT3 6
  A6_CC1   -XC 3 -XCT 4.5 -T3E 1 -AT3 6  -CC 1
  A6_CCN1  -XC 3 -XCT 4.5 -T3E 1 -AT3 6  -CC 1 -CCN 1
  A8_CC1   -XC 3 -XCT 4.5 -T3E 1 -AT3 8  -CC 1
  A10_CC0  -XC 3 -XCT 4.5 -T3E 1 -AT3 10

## M2 -- BATCH 1 + 2 RESULT: -CC 1 IS MANDATORY WITH THE pT3 STAGE ON
The stage is worth REAL efficiency (-AT3 6, no OT contention: eff .81231, ABOVE LST's
.80988) but is unusable raw: dup .28682, fake .06550, 465.9 deliveries/evt. With the
pre-existing M20 hit-overlap contention (`-CC 1`, MD granularity) the class becomes
affordable. `-CCN 1` (any shared MD kills) beats `-CCN 2` on BOTH dup and fake at equal
-AT3, for ~.003 of efficiency. `-CCG 0` (hit granularity) is a wash vs `-CCN 1` -- not
worth the knob. -AT3 is a SECOND seed-dedup knob as well as the delivery knob: it feeds
the pre-existing -RPS predicate through plsBestT3Logit.

## M3 -- SOURCE EDIT: -XCD 2 (diagnostic only)
`-XCD 1` defines class A as "sim already has a 5+ layer CHAIN TC". With the pT3 stage on
that misfiles a seed whose sim is covered only by one of OUR pT3-class deliveries as
class B ("efficiency we killed") when it is a duplicate by construction. `-XCD 2` adds the
delivered pT3-class rows (ga.t3Pls[t] >= 0, i.e. post-revocation) to the covered set.
Guarded by `xcDiag >= 1.5f`; writes nothing but its own counters. Binary 3d6c659f...
(was e8380a9f...). Re-gated as GATE2 in batch 3.

## M4 -- BATCH 3 LAUNCHED: the -AT3 x -XCT 2D grid at -CCN 1 (+ 2 -CCN 2 contrast points)

## M4 RESULT -- BATCH 3 (2D grid) : (-AT3 7, -CCN 1) IS THE DOMINANT RIDGE
Rebuild no-op re-gated: GATE2 (new binary, -XCD default 0) == GATE on all 14 metrics and
33/33 branches IDENTICAL under rebase_ref/cmp_branches.py. The -XCD 2 edit is inert.

At matched duplicate rate, -AT3 7 -CCN 1 dominates -AT3 6, -AT3 8 and -CCN 2:
  dup ~.060 : A7N1 eff ~.8057   vs A6N1 (XCT 6) .80471
  dup ~.072 : A7N1 eff ~.8063   vs A8N1 (XCT 5) .80436
  dup ~.075 : A7N2 eff .80679 / fake .05253  vs A7N1 (XCT 6) .80630 / fake .04959
-XCT is a MUCH cheaper knob than it was at -T3E 0: 6.0 -> 3.5 costs .00203 of efficiency
and buys .01936 of duplicate rate (~9.5:1), and fake is FLAT along it (.04959 -> .04991).
Fake is set almost entirely by -AT3 (7 -> 8 : .0499 -> .0476).

## M5 -- BATCH 4 LAUNCHED (12 pts): -AT3 6.5/7/7.5 x -XCT, all with -XCD 2, plus the
   -XC 0 reference row for the truth partition and a -CCR 2 release probe.

## M5/M6 RESULT -- THE BIG ONE: `-CCR 2` (the release that actually releases)
When the OT contention revokes a pT3-class delivery, `-CCR 1` (the shipped default) leaves
the seed's `plsBestT3Logit` in place, so the pre-existing `-RPS` predicate keeps the seed's
carried type-8 row retired: the revocation DESTROYS the seed outright. `-CCR 2` erases that
evidence, the carried bare-pLS row survives, and the ported CrossCleanpLS (-XC) then cleans
it like any other bare seed. Pre-existing flag, no new code.
  -AT3 7 -CCN 1 -XCT 4.5 :  -CCR 1  eff .80506 dup .05827   ->  -CCR 2  eff .80944 dup .06927
That is +.0044 of efficiency, and -XCT buys the duplicate rate back at a better rate than
the efficiency costs. With it the line REACHES AND PASSES LST's overall efficiency:
  -AT3 7 -CCR 2 -XCT 5.0 : eff .81045 vs LST .80988.

CCR-2 frontier (-AT3 7 -CCN 1), 300 evts:
  XCT 5.00  .81045 / .07643 / .04944
  XCT 4.50  .80944 / .06927 / .04959
  XCT 4.00  .80900 / .06452 / .04970
  XCT 3.50  .80807 / .06124 / .04975
  XCT 3.00  .80674 / .05875 / .04977
  XCT 2.50  .80537 / .05670 / .04978
  XCT 2.00  .80312 / .05485 / .04979
Fake is FLAT in -XCT and is set by -AT3 alone (6 -> .0556, 7 -> .0497, 8 -> .0476,
9 -> .0472, LST .04476).

## M7 -- ABLATIONS ON THE ASSEMBLED LINE (300 evts, -AT3 6 -CCN 1 -CCR 2)
  G_NOCC  -CC 0            eff .81209 dup .28578 fake .06554   (OT contention OFF)
  G_NOOP  -XC 0            eff .81333 dup .16098 fake .05436   (ported crossclean OFF)
  F_A6X4  -XC 3 -XCT 4     eff .80992 dup .06230 fake .05551   (both ON = the baseline)
Both mechanisms are load-bearing and they are NOT redundant: -CC removes pT3-class rows
that duplicate an already-delivered TC, -XC removes bare SEEDS that duplicate one. The XC
port costs .0034 of efficiency and buys .0987 of duplicate rate on this line.

## M8 -- 977-EVENT CHECK: the frozen 300 is a faithful subset
LST identity on 977 evts (fin_base977_hists.root, NEW this round) = eff .80987 dup .05138
fake .04538 vs the 300's .80988 / .05179 / .04476.
W_GATE (port-round best, 977) = eff .80213 dup .07692 fake .04742 vs the 300's
.80215 / .07727 / .04704. Deltas <= .0004 on every headline. Iterate on the 300.

## M9 -- FINAL GATES, ALL BIT-IDENTICAL (binary 519b0abc34a28cd1e803d6b9407ef224)
Second source edit this round: the `M20 pT3 dedup` line printed `-CCR %d` from
`ccRelPls >= 0.5f ? 1 : 0`, i.e. it reported "1" whether -CCR 1 or -CCR 2 was in force.
Now prints the value. Reporting only.
  GATE3      (new binary, -T3E 0)         == GATE       33/33 branches IDENTICAL
  FINBASE    (new binary, assembled line) == F_A6X4     33/33 branches IDENTICAL
  FINBASE_ND (-AT3 and -XCD omitted)      == FINBASE    33/33 branches IDENTICAL
The last one proves two things at once: -AT3 6 IS the shipped default (so the assembled
line carries one fewer tuned constant than it looks), and -XCD 2 is inert on physics.

## ===================== THE ASSEMBLED BASELINE =====================
Every explorer starts from EXACTLY this. Runner: `bash fin_ref/fin_run.sh <TAG> <below>`
(the frozen ANCHOR/CTL/FLAGSHIP/M19/CFF + POSTDELP2 prefix is inside the script).

    -XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2

Full explicit command line (what fin_run.sh expands to):

  chainproto -m hybrid -i rebase_ref/LSTNtuple_instr_300evt.root \
    -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
    -n -1 -o <out.root> \
    -e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 \
    -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 \
    -C25D -2.0 -A 4 -a 999 -D 5 -RT5 1 -BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 \
    -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5 -TT 1.2 -a 8 \
    -RPS 1 -RD 1 -a 6.875 -WE 0.20 -WZ 1.5 -FBC 0 -EX 1 -EXW 0.25 -EXR 2.0 -EXS 1 \
    -L 3.0 -CF 1 -CFC 1 -ZPF 3 -ZP5 1 -RT3 1 -ZP8 6 \
    -XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2

Canonical artifacts: fin_ref/r_FINBASE.{root,json,log,cmd} (300 evts, adds -XCD 2 for the
truth table; bit-identical to r_FINBASE_ND without it) and fin_ref/r_W_X4.* (977 evts).
Scoreboard: fin_ref/fin_scoreboard.txt.

### Why THIS point (300 evts; all candidates are on the measured frontier)
The dominant criterion is efficiency, then duplicate rate, then fake rate close behind.
Among the points reaching LST's overall efficiency, this is the MINIMUM-DUPLICATE one, and
its two remaining gaps against LST are balanced (dup +.0105, fake +.0107) rather than
lopsided. `-AT3` sits at its shipped DEFAULT 6.0, so only ONE constant is tuned here
(-XCT 4); the pT3-class row count that default produces, 135.4/evt, is also the closest to
LST's own pT3 count of 151.7/evt -- a structural check, not a tuned one.

BRACKET (move -XCT only; everything else fixed):
  -XCT 4.5  eff .81037  dup .06675  fake .05539   efficiency-leaning
  -XCT 4.0  eff .80992  dup .06230  fake .05551   THE BASELINE
  -XCT 3.5  eff .80904  dup .05925  fake .05558   duplicate-leaning
FAKE-LEANING ALTERNATIVE (a second constant moves): `-AT3 7 -XCT 4.75`
  eff .80992  dup .07259  fake .04952 -- same efficiency, +.0103 dup, -.0060 fake.
`-AT3` is the ONLY knob that moves the fake rate (6 -> .0555, 6.5 -> .0517, 7 -> .0495,
8 -> .0476, 9 -> .0472); -XCT does not move it at all.

### THE SCOREBOARD (frozen 300; 977 in fin_scoreboard.txt section C)
```
tag                     eff    vxy01      v15     v510    v1030      d15     d510    d1030      dup     fake      nhB      nhT      nhE      nTC
FINBASE             0.80992  0.84282  0.79459  0.73356  0.74139  0.61650  0.20161  0.03073  0.06230  0.05551  9.80254  9.87906  3.55956   618793
GATE (port best)    0.80215  0.83455  0.78801  0.73187  0.74139  0.61650  0.20161  0.03073  0.07727  0.04704  9.48612  9.78026  3.38764   609229
POSTDELP2           0.80626  0.83887  0.79167  0.73187  0.74139  0.61650  0.20161  0.03073  0.20553  0.04630  8.46885  8.74966  3.23588   655866
LST(base)           0.80988  0.84296  0.77193  0.65430  0.66453  0.55407  0.20968  0.05674  0.05179  0.04476 10.14804 10.01546  3.56248   608190
```
vs LST      : eff +.00004 | dup +.01051 | fake +.01075 | nTC +1.7% | nhitOT -0.35/-0.14/-0.003
              DISPLACED  v510 +.0793  v1030 +.0769  v15 +.0227  d15 +.0624
                         d510 -.0081  d1030 -.0260  (both small-denominator, unmoved all round)
vs GATE     : eff +.00777 | dup -.01497 | fake +.00847 | length +0.32/+0.10/+0.17
vs POSTDELP2: eff +.00366 | dup -.14323 | fake +.00921 | length +1.34/+1.13/+0.32

PER REGION (300 evts)
```
tag                    effB     effT     effE     dupB     dupT     dupE     fakB     fakT     fakE      nhB      nhT      nhE
FINBASE             0.92660  0.88213  0.74496  0.04344  0.03431  0.08106  0.06680  0.06903  0.04525  9.80254  9.87906  3.55956
GATE                0.90805  0.87016  0.74873  0.05390  0.04920  0.09767  0.05456  0.05716  0.04016  9.48612  9.78026  3.38764
LST(base)           0.92557  0.88187  0.74596  0.00989  0.01264  0.08556  0.04249  0.04454  0.04603 10.14804 10.01546  3.56248
```
Barrel and transition efficiency reach LST: 300 evts effB +.00103 effT +.00026 effE -.00100;
977 evts effB +.00024 effT -.00023 effE -.00222. Call barrel and transition PARITY and the
endcap -.002 (the port round was -.0175 / -.0117 / +.0028, so barrel and transition are
where the pT3 stage paid). Endcap duplicate rate and endcap fake rate are BETTER than LST
on both samples (.0811 vs .0856 and .0453 vs .0460 at 300; .0807 vs .0849 and .0458 vs
.0463 at 977). Every residual gap -- dup and fake alike -- is barrel + transition.

FULL 977 (LST reference regenerated this round: fin_base977_hists.root)
```
tag                     eff    vxy01      v15     v510    v1030      d15     d510    d1030      dup     fake      nTC
W_X4 (=FINBASE)     0.80905  0.84144  0.80110  0.72161  0.71474  0.58320  0.24521  0.03151  0.06184  0.05607  2033868
W_GATE              0.80213  0.83399  0.79737  0.72010  0.71474  0.58320  0.24521  0.03151  0.07692  0.04742  2002505
LST(base)           0.80987  0.84285  0.77719  0.64422  0.62567  0.51042  0.22906  0.05402  0.05138  0.04538  1998494
```
On 977 the efficiency lands -.00082 below LST rather than +.00004: read the 300-evt
efficiency parity as PARITY, not as a win. Everything else transfers to within .0006.

### TRUTH PARTITION (-XCD 2, per event, universe isQuad && isDupAlgPass2==0 = 1569.2/evt)
```
class                           N   consumed  RPSblock  XCretire   survive
-- assembled line, -XC 0 (no seed crossclean) --
A true, sim has cover TC    676.0      301.7     234.1       0.0     140.2
B true, sim has NO cover    841.3        4.9       7.2       0.0     829.2
C no true match              51.8        1.7       5.4       0.0      44.7
-- THE BASELINE, -XC 3 -XCT 4 --
A true, sim has cover TC    676.0      301.7     234.1     113.9      26.3
B true, sim has NO cover    841.3        4.9       7.2       5.8     823.4
C no true match              51.8        1.7       5.4       4.2      40.5
-- -XCT 4.5 --
A                           676.0      301.7     234.1     108.8      31.5
B                           841.3        4.9       7.2       4.6     824.6
C                            51.8        1.7       5.4       4.1      40.6
-- -XCT 3.5 --
A                           676.0      301.7     234.1     117.3      22.9
B                           841.3        4.9       7.2       7.0      822.2
C                            51.8        1.7       5.4       4.3      40.3
```
Read against the port round (which ran at -T3E 0, and whose "cover" meant chain TC only):
class A grew 597.3 -> 676.0 because the delivered pT3-class rows now cover sims of their
own, and CLASS-A SURVIVORS FELL 41.9 -> 26.3. 96.1% of class A is now retired, and the
XC arm's selectivity is 113.9 A per 5.8 B = 19.6:1.
Per-event on 977: A 682.8 / 305.6 / 236.8 / 114.0 / 26.3, B 848.5 / 5.0 / 7.3 / 6.1 /
830.1, C 52.6 / 1.7 / 5.4 / 4.2 / 41.3 -- the same partition.
CAVEAT: with `-XCD 1` (chain-only cover) these same runs would file the pT3-covered seeds
under class B. Use `-XCD 2` for anything with the stage on; the numbers above all do.

## ===================== WHAT IS OPEN, FOR THE FIFTEEN =====================
Everything below is MEASURED on this baseline, not speculation.

1. **FAKE RATE IS THE NEW HEADLINE DEFICIT, and only one knob touches it.** +.0108 vs LST,
   entirely barrel + transition (fakB .0668 vs .0425, fakT .0690 vs .0445; endcap is
   already better than LST). -XCT does not move it at all; only -AT3 does, and -AT3 buys
   fake by giving up efficiency AND duplicate rate at the same time (-AT3 7 -XCT 4.75 =
   same eff, +.0103 dup, -.0060 fake). That means the fake rate is a DELIVERY-HEAD QUALITY
   statement about bare-T3 targets, i.e. exactly what the permitted retrain attacks: the
   attach head's model selection used chain-pair validation, and a sibling found 758k
   harness-true bare pairs mislabelled as fakes (fixed labels exist in the gen_c_ref
   lineage). A head that separates real pT3-class deliveries from fake ones at -AT3 6
   volume would move fake without paying eff or dup.

2. **DUPLICATE RATE: +.0105 vs LST, and the structural floor moved.** Class-A survivors are
   26.3/evt (was 41.9 in the port round). Of the class-A total 676.0, the fates are
   consumed 301.7 / RPS-blocked 234.1 / XC-retired 113.9. The -XCT frontier still runs out
   of separating power in the same band the port round measured: 4.5 -> 3.5 buys .0075 of
   dup for .0013 of eff (5.7:1), and below 3 it collapses. A purpose-built seed-vs-chain
   dedup head for the sub-4-logit band is the named target.

3. **-CCR 2 IS DOING A LOT OF WORK AND IS UNDER-EXPLORED.** It is worth +.0044 of
   efficiency at fixed everything else. Nobody has measured the intermediate policies (e.g.
   release only when the revoking claimant is itself a pT3-class row, or release only the
   OT-side revocations and not the pixel-side ones). 330.5 OT revocations/evt and 661.3
   pixel revocations/evt pass through this decision.

4. **THE ENDCAP ATTACH-HEAD RECALL DEFECT IS STILL UNFIXED** (port round item 1b): the
   prefilter |dTanLambda| < 0.6 is TIGHTER than the dR^2 < 0.02 dedup window above
   |eta| ~ 2.2, and it now feeds THREE things -- chain delivery, pT3-class delivery and
   seed dedup. Endcap efficiency is the one region still below LST (-.0022 on 977).

5. **-CCN / -CCG / -CCK / -CCP were measured, do not re-measure them.** -CCP 1 is the
   feature not a variant (contention against ALREADY-DELIVERED TCs is the whole mechanism);
   -CCK is a wash; -CCG 0 is a wash against -CCN 1; -CCN 1 beats -CCN 2 on both dup and
   fake at equal -AT3. -CCN 3 (identical-triple only) is far too loose (dup .239).

6. **Eta-binned -XCT was measured and rejected in the port round; that still holds.**

7. **What must NOT move**: the ported CrossCleanpLS dR windows (-XCR2 1e-6, -XCW2 0.02) are
   maintainer-blessed verbatim; displaced performance (v510 +.079, v1030 +.077, d15 +.062
   vs LST) is UNTOUCHED by every knob in this round and must stay that way; d510 and d1030
   have not moved a single track all round and their denominators are ~285 -- do not tune
   on them.

## CODE STATE OF protoFIN (copy this workspace, do not copy protoXC)
`fin_ref/protoFIN_vs_protoXC.diff` is the COMPLETE source delta from protoXC: 3 hunks in
main.cc, all diagnostic or reporting, all proven inert (GATE3 and FINBASE_ND above).
  1. `-XCD 2` truth-partition mode (guarded by xcDiag >= 1.5f)
  2. its usage() text and the partition header line
  3. `M20 pT3 dedup` now prints the -CCR VALUE instead of a boolean
No physics code was touched this round. No new mechanism was written: every flag in the
assembled baseline already existed in protoXC.
Binary: protoFIN/bin/chainproto md5 519b0abc34a28cd1e803d6b9407ef224.
Build: `cd <yourcopy> && make -j 12` after the standard setup.sh/cmsenv/setup.sh prefix.
