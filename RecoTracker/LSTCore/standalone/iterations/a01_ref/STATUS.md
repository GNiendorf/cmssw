# A01 -- CLASS-A STRUCTURAL SURVIVORS -- STATUS

Angle: the ~26.3/evt class-A survivors of the assembled baseline -- bare seeds whose sim
IS covered by a delivered TC but which the ported CrossCleanpLS cannot see, because the
covering TC took a DIFFERENT seed (type 7) or is a delivered pT3-class row, or because the
attach prefilter never enumerated the (seed, target) pair.

Workspace: `standalone/protoA01` (copy of protoFIN). Artifacts: `standalone/a01_ref/`.
Runner: `bash a01_ref/a01_run.sh <TAG> [overrides]`. Tables: `a01_tab.py`, `a01_tab2.py`.

## M0 -- SETUP (DONE)
protoA01 = byte copy of protoFIN, then edited. Binary b1a86c95296fe1530a2121ca9e2b388b.

## THE MECHANISM (-XCO): OWNERSHIP-MAP SEED DEDUP
"Did this seed bid for an outer-tracker object we ACTUALLY DELIVERED?"
Reads the delivery's own scored-pair log (`ga.pairLog`, already produced) plus the
delivered set. NO dR, NO proximity, NO embedding, no candidate-vs-candidate loop -- the
two relations are "target X is delivered" (an ownership lookup) and "seed p bid for X at
logit L" (a row the delivery already wrote). One linear pass.

Flags (all default to a bit-identical no-op):
  -XCO <logit>   threshold; 1e9 = OFF (default)
  -XCOM <0|1>    0 target row itself delivered (default) | 1 >= -XCON of the target's MDs
                 owned by any delivered TC (superset)
  -XCON <n>      MDs for -XCOM 1 (default 2)
  -XCOK <mask>   1 chain targets | 2 bare-T3 targets | 3 both (default)
  -XCD 3         diagnostic: SURVIVOR REACHABILITY table (no decision changes)

## M1 -- GATE + DIAG LAUNCHED

## M1 -- RUNS IN FLIGHT (launched 21:41 / 21:54, machine is saturated: 130 load, 130+
   sibling chainproto processes, ~5 evt/min per run instead of ~180)
  GATE   assembled baseline on protoA01, all A01 flags at defaults  -> no-op proof
  DIAG   GATE + -XCD 3 (reachability diagnostic)                    -> the ceiling
  O_6 O_5 O_45 O_4 O_35   baseline + -XCD 3 + -XCO {6,5,4.5,4,3.5}

## M2 -- THE REACHABILITY DIAGNOSTIC (-XCD 3, tag DIAG, 300 evts) -- THE KEY RESULT
Truth partition reproduced EXACTLY (A 676.0/301.7/234.1/113.9/26.3, B 841.3/4.9/7.2/5.8/
823.4, C 51.8/1.7/5.4/4.2/40.5), so -XCD 3 is inert and the baseline is reproduced.

SURVIVING seeds by best logit over a DELIVERED target (per event):
```
class                          noPair noDelivTgt      <2    [2,3)   [3,4)     >=4
A true, sim has cover TC         0.02       0.26    9.33     4.21    6.48    6.05
B true, sim has NO cover TC    621.16      36.56  142.13    12.87    6.78    3.89
C no true match                 19.50       1.59   16.91     1.24    0.71    0.52
of the >=4 column, [seedless chain / type-7 chain / pT3-class]:
A                                4.80       0.89    0.36
B                                0.37       2.91    0.61
C                                0.04       0.42    0.06
```
READINGS
1. THE PREFILTER IS NOT THE PROBLEM FOR CLASS A. "noPair" is 0.02/evt out of 26.3. The
   port round's hypothesis that the ~20/evt floor is partly seeds the attach prefilter
   never enumerated is REFUTED on the assembled baseline: essentially every class-A
   survivor HAS scored pairs. (For class B, 621 of 823 have no pair -- those are pileup
   seeds with no outer-tracker target at all, which is why class B is unreachable and safe.)
2. THE BINDING CONSTRAINT IS THE PORTED ARM'S dR WINDOW, NOT ITS THRESHOLD. 4.80/evt of the
   class-A survivors have a pair at logit >= 4 with a chain TC WE DELIVERED SEEDLESS --
   exactly the object the ported -XCT arm tests, at exactly the threshold it uses. They
   survive only because they sit outside dR^2 < 0.02 of that TC.
3. THE TYPE-7 TARGETS ARE THE TRAP. At logit >= 4 the type-7 (chain delivered with a
   DIFFERENT seed) column is 0.89 class A against 2.91 class B -- a 1:3 LOSING trade. The
   seedless column is 4.80 A against 0.37 B -- 13:1. So "the covering chain took a different
   seed" is real but it is NOT where the affordable duplicates are.

## M3 -- THE -XCO FRONTIER (all target shapes, 300 evts, on the assembled baseline)
`bash a01_ref/a01_run.sh <TAG> -XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2 -XCD 3 -XCO <v>`
```
tag       -XCO      eff      dup     fake     effB     dupB   A-surv  B-retire
DIAG      off   0.80992  0.06230  0.05551  0.92660  0.04344    26.3      5.8
O_6       6     0.80970  0.05954  0.05557  0.92625  0.03480    23.5      6.3
O_5       5     0.80944  0.05774  0.05560  0.92580  0.02990     --       --
O_45      4.5   0.80926  0.05726  0.05559  0.92557  0.02872    20.8      8.5
O_4       4     0.80886  0.05695  0.05555  0.92512  0.02809     --       --
O_35      3.5   0.80758  0.05364  0.05559  0.92330  0.02270    16.6     12.7
LST                     0.80988  0.05179  0.04476  0.92557  0.00989
```
* NO-OP PROVEN: DIAG (= the assembled line + the inert -XCD 3) reproduces FINBASE on all
  14 headline metrics AND on nTC exactly (618793).
* -XCO DOMINATES -XCT AS THE DUPLICATE KNOB. At matched duplicate rate:
    dup .0595 : -XCO 6 eff .80970  vs  -XCT 3.5 (dup .05925) eff .80904   -> +.00066
    dup .0569 : -XCO 4 eff .80886  vs  -XCT 3.0 (dup .05689) eff .80776   -> +.00110
  Marginal exchange: -XCO 6 buys .00276 of duplicate rate for .00022 of efficiency
  (12.5:1) where -XCT 4->3.5 pays .00088 for .00305 (3.5:1).
* FAKE IS COMPLETELY FLAT along -XCO (.05551 -> .05559). Same as -XCT.
* IT IS A BARREL DUPLICATE KILLER, which is where the residual gap is: dupB .04344 ->
  .02809 at -XCO 4 with effB still at LST's own .92557 at -XCO 4.5. Transition and endcap
  barely move.
* DISPLACED UNTOUCHED down to -XCO 3.5 (v510 .73187-.73356, v1030/d15/d510/d1030 fixed).

## M4 -- NO-OP GATE PASSED EXACTLY
`a01_ref/r_GATE` (protoA01, assembled line, every A01 flag at its default) vs
`fin_ref/r_FINBASE_ND`: `rebase_ref/cmp_branches.py` -> 33 IDENTICAL, 0 DIFFER, 0 MISSING,
0 ADDED, and all 14 headline metrics + nTC (618793) equal. `r_DIAG` (the same line plus
`-XCD 3`) is equal on every metric too, so the reachability diagnostic is inert.

## M5 -- 977-EVT CONFIRMATION LAUNCHED for the leading candidate (tag W_O5, -XCO 5).

## M6 -- TARGET-SHAPE ABLATION (-XCOK 1, delivered-SEEDLESS-chain targets only)
protoA01b re-gated: GATEB == FINBASE on all 14 metrics and nTC (618793).
```
tag       -XCO  -XCOK       eff      dup     fake  A-retire B-retire A-surv
DIAG      off    --     0.80992  0.06230  0.05551    113.9      5.8   26.3
S_45      4.5    1      0.80939  0.05743  0.05559    119.2      8.1   21.0
S_4       4      1      0.80904  0.05719  0.05555    119.6      9.1   20.7
S_3       3      1      0.80617  0.05142  0.05559    125.9     15.2   14.3
S_2       2      1      0.80082  0.04736  0.05551    130.1     26.9   10.2
O_45      4.5    7      0.80926  0.05726  0.05559    119.5      8.5   20.8
```
VERDICT ON THE KNOB: -XCOK IS NOT WORTH HAVING. At 4.5 the seedless-only and all-shapes
variants agree to 0.3 class A / 0.4 class B and to .0002 of duplicate rate; at low
thresholds ALL-SHAPES is very slightly better at matched duplicate rate (restricting the
target set forces a lower threshold to reach the same duplicate rate, and the head
separates worse there). Keep the default and do not tune it. The reach table's warning
about type-7 targets is real per-target but numerically negligible in aggregate.
S_2 shows the mechanism can go BELOW LST's duplicate rate (.04736 vs .05179) -- the gap is
an efficiency price, not a ceiling, exactly as the port round found for -XCT.

## M7 -- BATCHES 3 + 5 IN FLIGHT (launched 22:57 and 23:41; machine saturated, load ~100,
   ~90 sibling chainproto processes, so a 300-evt run is taking ~60-75 min not ~13)
The two questions left after M3/M6, both about whether -XCO can REPLACE tuned -XCT rather
than stack on it (simplicity is a judging criterion):
```
tag      line                                                     question
T45_O5   -XCT 4.5 -XCO 5      joint frontier: relax XCT, hold dup with XCO
T5_O5    -XCT 5   -XCO 5      relax XCT further
T45_O6   -XCT 4.5 -XCO 6      the gentle corner
O_7      -XCT 4   -XCO 7      the top of the XCO frontier
R_O5     -XC 1    -XCO 5      REPLACEMENT: pixel arms only, no bare-chain dR arm at all
R_O35    -XC 1    -XCO 3.5    same, pushed
M1_O5    -XCO 5 -XCOM 1       the superset MD-ownership target test
W_O5     977 evts, -XCT 4 -XCO 5    final-number confirmation
```

## M8 -- THE DIAGNOSTIC IS AN EXACT EX-ANTE MODEL OF THE ARM (the strongest result here)
The `-XCD 3` reach table predicts both the gain and the price of any `-XCO` setting
WITHOUT running it. At `-XCO 4` the ">= 4" bucket says 6.05 class A and 3.89 class B;
measured, class-A survivors fell 26.3 -> 20.3 (6.0) and class-B retirements rose
5.8 -> 9.7 (3.9). Agreement to 0.05/evt on both.

CONSEQUENCE -- THE "STRUCTURAL FLOOR" IS 0.28/EVT, NOT ~20. Of the 26.3 class-A survivors,
26.07 (99%) have a scored pair against a target we DELIVERED; only 0.28 are unreachable
(0.02 the prefilter never enumerated + 0.26 with no delivered target at all). So the ~20/evt
were never structurally invisible -- they are THRESHOLD-limited, and the thing that hid them
was the ported arm's dR window plus its blindness to type-7 and pT3-class targets, not a
missing relation. My angle's premise ("the attach prefilter never enumerated the pair") is
REFUTED at 0.02/evt; the other half ("the covering chain took a different seed") is real but
small (0.89/evt at logit >= 4, and it trades 1:3 against class B -- a losing trade per target).
The affordable population is the SEEDLESS-chain column: 4.80 class A against 0.37 class B, 13:1.

WHY THE WHOLE 26 CANNOT SIMPLY BE TAKEN: driving -XCO to -inf would retire all 26.07 but
cost 165.7 class-B retirements. The head's separating power, not the mechanism's reach, is
the binding constraint -- the same conclusion the port round reached for -XCT, now proven
for a mechanism with no geometry in it at all.

## M9 -- WHAT THE ARM ACTUALLY MOVES (per region, 300 evts)
```
tag              eff     dup    fake    effB    dupB    effT    dupT    effE    dupE     nhB
DIAG (base)  0.80992 0.06230 0.05551 0.92660 0.04344 0.88213 0.03431 0.74496 0.08106  9.8025
O_6 (-XCO 6) 0.80970 0.05954 0.05557 0.92625 0.03480 0.88187 0.03418 0.74485 0.08071  9.8487
O_5 (-XCO 5) 0.80944 0.05774 0.05560 0.92580 0.02990 0.88162 0.03398 0.74474 0.08011  9.8773
LST          0.80988 0.05179 0.04476 0.92557 0.00989 0.88187 0.01264 0.74596 0.08556 10.1480
```
* IT IS A PURE BARREL DUPLICATE KILLER. dupB .04344 -> .02990 at -XCO 5 (-31%), which is
  where the whole residual gap against LST lives. Transition moves .0343 -> .0340 and
  endcap .0811 -> .0801; both were already at or better than LST.
* IT DOES NOT COST BARREL EFFICIENCY. effB .92660 -> .92580 at -XCO 5, still ABOVE LST's
  own .92557. Transition and endcap efficiency move by < .0006.
* TRACK LENGTH IMPROVES, toward LST. nhB 9.8025 -> 9.8773 (LST 10.1480), nhE 3.5596 ->
  3.5645 (LST 3.5625, so endcap length matches LST at -XCO 5). Retiring a bare type-8 row
  removes a 3-hit-equivalent row from the mean, so the arm helps the length metric instead
  of costing it -- the opposite of what a delivery-side cut would do.
* FAKE AND DISPLACED ARE INERT. fake .05551 -> .05560 across the whole scan; v510, v1030,
  d15, d510, d1030 do not move a single track anywhere in the -XCO family down to 3.5.

## M10 -- JOINT (-XCT, -XCO) IS REDUNDANT, NOT COMPLEMENTARY
`T45_O5` = -XCT 4.5 -XCO 5 -> eff .80988 / dup .06221 / fake .05547, which lands ON TOP of
the -XCT 4 baseline (.80992 / .06230 / .05551). Relaxing the ported threshold and paying it
back with the ownership arm is a WASH. Both knobs are thresholds on the SAME head, so they
are substitutes; the ownership arm is simply the better-shaped one. Do not ship both tuned:
ship one.

## M11 -- THE HEADLINE: THE OWNERSHIP ARM STRICTLY SUBSUMES THE PORTED BARE-CHAIN ARM
Proven by BIT-IDENTICAL output, not by matching aggregates:
```
  -XC 3 -XCT 4 -XCO 3.5   ==   -XC 1 -XCO 3.5    33/33 branches IDENTICAL, nTC 613556
  -XC 3 -XCT 5 -XCO 5     ==   -XC 1 -XCO 5      33/33 branches IDENTICAL, nTC 621337
```
(`-XC 1` = pixel-anchored arms only; the whole bare-chain arm, its dR^2 < 0.02 window and
its -XCT constant are GONE.)

WHY IT IS TRUE BY CONSTRUCTION. The ported bare-chain arm retires a seed iff
  (i) it has a scored pair against a chain at logit >= -XCT, (ii) that chain was delivered
  SEEDLESS, and (iii) dR^2(seed, chain) < 0.02.
The ownership arm requires (i) and (ii) and NOT (iii). So it is a strict superset at equal
threshold, and the runs confirm the superset costs nothing: at matched threshold the two
produce the same event file byte for byte.

CONSEQUENCE: THE dR WINDOW NEVER ADDS SELECTIVITY, IT ONLY REMOVES REACH. It is a pure
restriction on an otherwise-identical decision. Everything the geometric window was doing
is already done by "is the thing you bid for on the output?", and the ~5/evt of class-A
duplicates it was blocking (M8: the 4.80/evt seedless column at logit >= 4, 13:1 against
class B) are recovered for free.

This is the one place this round where a NEW mechanism is simpler than what it replaces:
it removes the only geometric criterion in the new code path and one tuned constant, and
it is ownership-shaped end to end (a lookup into "what did we deliver", not a candidate
loop). The maintainer's "keep the ported dR windows verbatim" instruction is satisfied on
the PIXEL arms, which are untouched.

## M12 -- THE OWNERSHIP KNOB DOMINATES THE PORTED THRESHOLD KNOB EVERYWHERE
Efficiency at MATCHED duplicate rate, 300 evts (interpolated on each family's frontier;
-XCT moved alone = the Baseline agent's bracket, -XCO moved alone from the same baseline):
```
       dup     via -XCT     via -XCO   XCO gain
   0.06000      0.80926      0.80974   +0.00048
   0.05900      0.80890      0.80962   +0.00072
   0.05800      0.80836      0.80948   +0.00112
   0.05750      0.80809      0.80935   +0.00126
   0.05700      0.80782      0.80892   +0.00110
```
Marginal exchange from the baseline: -XCT 4 -> 3.5 pays .00088 eff for .00305 dup (3.5:1);
-XCO off -> 6 pays .00022 for .00276 (12.5:1); -XCO off -> 5 pays .00048 for .00456 (9.5:1).
The ownership arm is 3-4x more efficient as a duplicate knob, and the reason is M11: it
spends its threshold budget on seeds the geometric arm could not reach at ANY threshold.

## M13 -- THE TWO SHIPPABLE CONFIGURATIONS
```
name          flags added to the assembled baseline        eff      dup     fake   knobs
A. GENTLE     -XCO 6            (keeps -XCT 4)         0.80970  0.05954  0.05557    2
B. SIMPLE     -XC 1 -XCO 4      (DELETES -XCT and the  0.80886  0.05695  0.05555    1
                                 dR window entirely)
   baseline   --                                       0.80992  0.06230  0.05551    1
   LST                                                 0.80988  0.05179  0.04476
```
B is bit-identical to `-XC 3 -XCT 4 -XCO 4` (M11), i.e. it is the SAME algorithm with dead
code removed -- so it is constant-neutral against the baseline (one tuned constant either
way) while cutting the duplicate gap against LST from +.0105 to +.0052 for .0011 of
efficiency, and it removes the only geometric criterion in the new path.

## M14 -- ACCOUNTING NOTE FOR WHOEVER PORTS THIS
The arm reports 227.8 retirements/evt at -XCO 6, but only 3.4/evt are LOAD-BEARING
(class A 113.9 -> 116.7, B 5.8 -> 6.3, C 4.2 -> 4.3). The other ~224 land on seeds the
delivery already consumed or the pre-existing -RPS predicate already blocked; the fate
partition ranks consumed > RPSblock > XCretire, so they are invisible in the output and
harmless. If -RPS is ever retired or reshaped, this arm's measured cost/benefit will move,
because the two overlap heavily -- they ask nearly the same question, -RPS from the seed
side (best logit over ANY chain) and this arm from the delivery side (best logit over a
DELIVERED chain). THE DELIVERY-SIDE FORM IS THE RIGHT ONE and is what buys the extra reach.
The 3.4 load-bearing retirements produce .00276 of duplicate rate because 2.8 of them are
class A -- duplicates by definition.

## M15 -- TRANSFER RISK (a judging criterion) AND NOISE DISCIPLINE
TRANSFER: the arm contains exactly ONE tuned number, a threshold on the SAME attach-head
logit the pipeline already thresholds in three other places (-AT3, -XCT, -RPS). It has no
geometric constant, no eta/region binning (the port round measured and rejected eta bins
for -XCT and I did not reintroduce them), no occupancy or multiplicity term, and nothing
that reads a PU-dependent quantity. Its transfer risk is therefore exactly the transfer
risk of the head itself, which is a pre-existing, already-stated caveat -- the arm adds no
NEW sample-specific surface. The -XCOK and -XCOM/-XCON sub-knobs were measured (M6, and
M1_O5 below) and are recommended to be DELETED, not tuned.

NOISE: v510 shows .73356 -> .73187 at -XCO 4.5 and below. That is ONE track on a ~285
denominator at 300 events and it is the SAME single track that moves in the port round's
tables. It is noise; I did not tune on it and no reader should. v1030, d15, d510 and d1030
do not move anywhere in this round's scan. The displaced advantage over LST is untouched by
everything measured here (v510 +.078, v1030 +.077, d15 +.062).

## M16 -- KNOWN CONFLICTS FOR THE SYNTHESIS AGENT
1. -RPS / -RPSA (a sibling angle is actively scanning these; I saw -RPSA/-RPST runs on the
   machine). THIS IS THE ONE REAL CONFLICT. The -RPS predicate already blocks 234.1 class-A
   seeds/evt using the per-seed best logit; my arm blocks a further 3.4 load-bearing/evt
   using the per-DELIVERY best logit. They overlap heavily (M14: 224 of my arm's 228
   retirements/evt land on seeds -RPS already blocked). If -RPS is loosened, my arm's
   measured benefit GROWS; if -RPS is tightened, it shrinks toward zero. The two must be
   tuned together or the combined config will double-count. My recommendation is robust in
   direction but its magnitude is not additive with an -RPS change.
2. -AT3. It feeds plsBestT3Logit into -RPS, so it is also a seed-dedup knob (the Baseline
   agent's finding). Same coupling as (1), one level removed. My scan held -AT3 at its
   shipped default 6.0 throughout, so nothing here is entangled with an -AT3 retune except
   through -RPS.
3. -XCT. My arm SUBSUMES it (M11, bit-identical). Any sibling result expressed as an -XCT
   setting should be re-expressed as an -XCO setting before combining; shipping both tuned
   is measured to be a wash (M10) and costs a constant.
4. NO conflict with -CC/-CCN/-CCR (OT-side delivery contention): that arm revokes
   DELIVERIES, mine retires SEEDS, and -CCR 2 is what makes the revoked seed's bare row
   reappear for mine to judge. They compose; I ran with -CC 1 -CCN 1 -CCR 2 throughout.
5. NO conflict with an attach-head RETRAIN (a permitted activity this round). My arm reads
   the same logit everything else does, so a better-separated head moves my frontier the
   same way it moves -XCT's -- and M8 says head separation, not mechanism reach, is the
   binding constraint, so a retrain is the highest-leverage thing that could be combined
   with this.

## M17 -- HOW TO PORT IT WITHOUT KEEPING THE FULL PAIR LOG
My prototype reads `ga.pairLog` (every scored pair) because that log already existed. A
production port does NOT need it, and should not pay for it. The arm only ever asks, for a
DELIVERED target, "which seeds bid for you at logit >= -XCO?". So during scoring, append
the seed row to a PER-TARGET bucket only when logit >= -XCO; after delivery, sweep the
delivered targets and retire every seed in their buckets. That is:
  * an ownership map keyed by target, built with one atomic append per surviving pair --
    the same shape as the M20 -CC contention map that is already in the code;
  * bounded by the number of ABOVE-THRESHOLD pairs, not all pairs (at -XCO 6 that is
    ~228 entries/evt against the full log's ~1e6/evt, i.e. ~4 orders of magnitude smaller);
  * a single linear sweep at the end, no candidate-vs-candidate loop, no geometry.
The threshold must be applied at INSERT time for this to be cheap, which means -XCO becomes
a compile/config constant rather than a scan knob in production. That is fine -- it is one
number and it is the only one.

## M18 -- BATCH 5 RESULTS
```
tag       change from baseline        eff      dup     fake     effB     dupB      nhB   Asurv
DIAG      (assembled baseline)    0.80992  0.06230  0.05551  0.92660  0.04344  9.80254   26.3
O_7       -XCO 7                  0.80992  0.06229  0.05551  0.92660  0.04344  9.80254   26.3
O_6       -XCO 6                  0.80970  0.05954  0.05557  0.92625  0.03480  9.84870   23.5
O_5       -XCO 5                  0.80944  0.05774  0.05560  0.92580  0.02990  9.87729   21.3
O_45      -XCO 4.5                0.80926  0.05726  0.05559  0.92557  0.02872  9.88605   20.8
O_4       -XCO 4                  0.80886  0.05695  0.05555  0.92512  0.02809  9.89357   20.3
O_35      -XCO 3.5                0.80758  0.05364  0.05559  0.92330  0.02270  9.93610   16.6
M1_O5     -XCO 5 -XCOM 1          0.80184  0.04788  0.05563  0.91419  0.01189 10.03730   10.7
T45_O6    -XCT 4.5 -XCO 6         0.81014  0.06400  0.05545  0.92671  0.04195  9.80062   28.6
LST                               0.80988  0.05179  0.04476  0.92557  0.00989 10.14804
```
* -XCO 7 IS A NO-OP ON THE OUTPUT (dup .06229 vs .06230, Asurv unchanged at 26.3) even
  though the arm fires 183/evt. Everything above logit 7 was already retired by -XCT 4 or
  -RPS. The frontier only starts moving below ~6.5, which is where the head's own
  separating power starts to matter.
* -XCOM 1 (the MD-ownership SUPERSET) IS THE LIMIT DEMONSTRATION, AND IT IS NOT SHIPPABLE.
  It drives duplicate rate to .04788 -- BELOW LST's .05179 -- and barrel track length to
  9.79 -> 10.04 against LST's 10.15, i.e. it closes BOTH remaining gaps at once. It costs
  .0081 of efficiency (effB .92660 -> .91419), which is far too much under the standing
  priority. KEEP THE DEFAULT -XCOM 0. Its value is as evidence: the ownership FORM can
  reach LST on duplicate rate and track length simultaneously, and what stops it is head
  quality, exactly as M8 concluded.
* -XCT 4.5 + -XCO 6 is not on the frontier either (eff +.00022, dup +.00170 vs baseline).
  Confirms M10: do not ship both tuned.

## M19 -- CORRECTION TO M11: THE dR WINDOW *DOES* CARRY INFORMATION AT LOW LOGIT
R_O45 (`-XC 1 -XCO 4.5`, the single-knob family) completes the replacement frontier and
forces me to qualify M11. Both statements are true and they are not in conflict:
```
SINGLE KNOB  -XC 1 -XCO t        STACK  -XC 3 -XCT 4 -XCO t
  t=5    .81067 / .06900           off    .80992 / .06230
  t=4.5  .80970 / .06171           t=6    .80970 / .05954
  t=4    .80886 / .05695  (== O_4, bit-identical)
  t=3.5  .80758 / .05364  (== O_35, bit-identical)
```
* M11 STANDS AS STATED: at the SAME threshold the ownership arm reproduces the ported arm
  bit-for-bit, so the window adds NOTHING at matched threshold, and `-XC 1 -XCO t` is a
  legitimate one-knob replacement that deletes the geometry.
* BUT THE STACK IS ON A BETTER FRONTIER. At matched efficiency .80970, the stack gives
  dup .05954 and the single knob .06171 (-.0022 for the stack); at .80926 it is .05726
  vs ~.0592. The reason is that "retire if (logit >= 4 AND inside the dR window) OR
  (logit >= 6)" is a genuinely better-shaped classifier than "retire if logit >= 4.5".
  The window is a USEFUL SECONDARY DISCRIMINATOR in the 4-6 logit band -- precisely the
  band where M8 says the head's own separating power collapses.
* HONEST CONCLUSION: I cannot recommend deleting the maintainer-blessed dR window on the
  physics. It earns its place. What I CAN recommend is ADDING the ownership arm on top of
  it, which is where all the measured gain is. The one-knob variant remains on the table
  purely as a simplicity option that costs ~.0022 of duplicate rate.

## M20 -- 977-EVENT CONFIRMATION (final numbers) -- IT TRANSFERS TO .00004
```
tag                     eff      dup     fake     effB     dupB     dupT     dupE      nhB      nhE      nTC
W_X4 (baseline)     0.80905  0.06184  0.05607  0.92454  0.04294  0.03375  0.08069  9.80064  3.55719  2033868
W_O5 (-XCO 5)       0.80860  0.05724  0.05614  0.92405  0.02942  0.03331  0.07968  9.87519  3.56224  2027161
LST                 0.80987  0.05138  0.04538  0.92430  0.00971  0.01308  0.08486 10.14984 3.55665  1998494
```
* THE 300-EVT TRADE REPRODUCES ON THE FULL SAMPLE TO WITHIN .00004. 300 evts: -.00048 eff
  for -.00456 dup. 977 evts: -.00045 eff for -.00460 dup. The frozen 300 is a faithful
  iteration set for this mechanism.
* DUPLICATE GAP AGAINST LST NEARLY HALVED: +.01046 -> +.00586. Barrel duplicate rate
  .04294 -> .02942 (-31%) with barrel efficiency .92454 -> .92405, still within .0003 of
  LST's own .92430.
* DISPLACED IS BIT-FOR-BIT UNMOVED: v510 .72161, v1030 .71474, d15 .58320, d510 .24521,
  d1030 .03151 -- every one identical to the baseline. The displaced advantage over LST
  (v510 +.078, v1030 +.089, d15 +.073) is fully protected.
* TRACK LENGTH IMPROVES: nhB 9.8006 -> 9.8752 (LST 10.1498), nhE 3.5572 -> 3.5622 (LST
  3.5567, so endcap length now slightly EXCEEDS LST). Not a regression -- a gain.
* FAKE FLAT: .05607 -> .05614.

## M21 -- FINAL RECOMMENDATION
Add ONE flag to the assembled baseline, changing nothing else:
    -XCO 5     (977-confirmed above)   eff -.00045, dup -.00460, fake +.00007
  or -XCO 6     (300 only, gentler)     eff -.00022, dup -.00276, fake +.00006
Everything else (-XC 3, -XCT 4, -T3E 1, -CC 1, -CCN 1, -CCR 2, -AT3 default) UNCHANGED.
-XCOM / -XCON / -XCOK stay at their defaults and should be DELETED from any port.

## M22 -- ROUND CLOSED. FINAL SUMMARY
Runs: 22 on the frozen 300 + 1 on the full 977, all on protoA01/protoA01b (copies of
protoFIN; ONE file touched, main.cc; all four new flags default to a bit-identical no-op,
gate proven twice at 33/33 branches and nTC 618793).

WHAT THE ANGLE FOUND, IN ORDER OF IMPORTANCE
1. MY ASSIGNED PREMISE WAS WRONG AND THAT IS THE RESULT. The ~20-26/evt class-A survivors
   are NOT structurally invisible. 99% (26.07 of 26.3) have a scored pair against an
   object we DELIVERED; only 0.28/evt are unreachable and only 0.02/evt were never
   enumerated by the prefilter. The true structural floor is 0.28/evt.
2. A MECHANISM THAT SEES THEM, WITH NO GEOMETRY IN IT. -XCO: "did this seed bid, at logit
   >= t, for an outer-tracker object we actually delivered?" Ownership lookup + the pair
   log the delivery already writes. No dR, no proximity, no embedding, no candidate loop.
3. IT DOMINATES THE PORTED THRESHOLD KNOB by 3-4x on the (eff, dup) exchange rate, is a
   pure BARREL duplicate killer (where the whole residual gap is), costs no barrel
   efficiency, IMPROVES track length, and does not move fake or displaced at all.
4. 977-CONFIRMED at -XCO 5: -.00045 eff for -.00460 dup, transferring from the 300 to
   within .00004. Duplicate gap vs LST nearly halved (+.01046 -> +.00586).
5. HONEST NEGATIVE: the maintainer-blessed dR window EARNS ITS PLACE. It is redundant at
   matched threshold (proven bit-identical) but as a low-logit secondary discriminator the
   stack beats the ownership arm alone by ~.0022 of duplicate rate at matched efficiency.
   Do not delete it.
6. THE BINDING CONSTRAINT IS HEAD QUALITY, NOT MECHANISM REACH -- now proven for a
   mechanism with no geometry at all. -XCOM 1 reaches dup .0479 (BELOW LST) and barrel
   length 10.04 (LST 10.15) simultaneously, but costs .0081 efficiency. Anyone retraining
   the attach head should expect this arm's frontier to move with it.
