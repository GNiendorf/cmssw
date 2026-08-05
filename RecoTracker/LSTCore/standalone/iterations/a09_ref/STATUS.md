# A09 -- SEED-RETIREMENT UNIFICATION -- STATUS

Agent role: A09 (one of fifteen explorers). Angle: map the overlapping seed-retirement
mechanisms on the ASSEMBLED BASELINE, then SIMPLIFY (fewer mechanisms, same-or-better
scoreboard). Simplicity is a first-class deliverable.

Workspace: `standalone/protoA09` (copy of protoFIN, md5 519b0abc... before my edits).
Artifacts: `standalone/a09_ref/`. Runner: `a09_ref/a09_run.sh <TAG> [overrides]`
(carries the frozen prefix AND the assembled baseline `-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1
-CCR 2` internally; overrides come after and win).

## M0 -- CODE MAP (read from protoFIN/main.cc)

Bare-seed universe = `isQuad && pLS_isDupAlgPass2 == 0`, realised in TWO channels that both
consult the SAME verdicts:
  (a) CARRIED type-8 rows (LST's admitted pLS TCs) -- suppressed via `m16RowSuppressed`
      by `m16RefreshSupp()` (main.cc ~3387);
  (b) `-ZP8 6` ADDITIONS (the seeds LST's deleted CrossCleanpLS used to kill), re-added as
      synthetic type-8 rows (main.cc ~4944).

FIVE mechanisms act on that universe, in this FIXED pipeline order:
  I.   `plsDelivered[p]`      the seed already carries a delivery row (channel (b) only)
  II.  `pass1Removable[p]`    CheckHitspLS pass 1+2 -- at `-ZP8 6` this IS LST's
                              `pLS_isDupAlgPass2` branch, i.e. it DEFINES the universe
  III. `ga.plsOwned[p] != 0`  ATTACH CONSUMPTION. Unconditional, not flag-gated.
  IV.  `-RPS` logit predicate `plsBestChainLogit >= -a || plsBestT3Logit >= -AT3`.
                              NO geometry, NO delivered-TC requirement -- a pure score test.
  V.   `xcRetired[p]`         the ported CrossCleanpLS (`-XC`): pixel-anchored arm
                              (shared pixel hit row with a CONSUMED seed, or dR^2 < 1e-6)
                              and bare-chain arm (dR^2 < 0.02 of a SEEDLESS chain TC AND
                              that pair's attach logit >= -XCT).

STRUCTURAL FACTS ESTABLISHED BY READING (later confirmed by measurement):
* `-RPS` is PURELY OUTPUT-SIDE. Its half of `m16RefreshSupp` is gated on `ty == 8`, and
  nothing downstream reads type-8 suppression (the pre-claim list, the Extend claimed-hit
  map and the `-CC` pre-claim all use type 7 / type 5 OT hits only). So `-RPS` changes the
  emitted type-8 rows and NOTHING else -- a perfectly isolated A/B.
* `-RPS` is the ONLY mechanism with no structural anchor: it fires on a score alone. Every
  arm of `-XC` requires a delivered object.
* `-XC`'s bare-chain arm anchors ONLY on SEEDLESS chain TCs (`type != 7`, `outTCChain >= 0`).
  A seed whose track was delivered by a DIFFERENT seed as a type-7 or type-5 row can be
  retired only by the pixel-anchored arm, i.e. only on a shared pixel hit or dR^2 < 1e-6.
  That is the structural hole `-RPS` is currently filling.
* `-CCR 2` (worth +.0044 eff) exists ONLY to undo the T3 half of `-RPS`.

## M1 -- CODE CHANGES IN protoA09 (all flag-gated, defaults bit-identical)
1. `rpsRetires(p)`: the -RPS predicate collapsed from THREE copies into one, plus new modes
   - `-RPS 2` CHAIN HALF ONLY (drops the `plsBestT3Logit >= -AT3` term);
   - `-RPS 3` STRUCTURAL both halves: retire only if a target the seed scored above its
     class margin was actually DELIVERED and to a DIFFERENT seed. One pass over the pair
     log the delivery already wrote plus one ownership-array read per pair -- no pairwise
     candidate loop and no proximity criterion, so it satisfies the NEW-mechanism rule.
     It also makes `-CCR` structurally unnecessary: a revoked pT3-class delivery has
     `t3Pls == -1`, so its seed is released by construction rather than by hand.
   - `-RPS 4` structural bare-T3 half only; chain half keeps the shipped score test.
2. `-UM <0|1>` SEED-RETIREMENT UNIFICATION MAP (diagnostic only): per seed in the universe,
   the SET of mechanisms that WOULD fire, order-independent, histogrammed by truth class.
   Requires `-XCD 2`. Bits 1 owned / 2 rpsChain / 4 rpsT3 / 8 xcPixHit / 16 xcPixDR /
   32 xcChain / 64 strT3 / 128 strChain. Adds an `xcArm[]` bitmask alongside `xcRetired[]`;
   under `-UM` the arms stop early-skipping each other so overlap is visible, with the
   counters and `xcRetired` provably unchanged.
3. `-RPS=%d` now prints the VALUE, not a boolean (reporting only).

## RUN RECIPE
300 evts: `bash a09_ref/a09_run.sh <TAG> <overrides>`
977 evts: `NEV=-1 LSTN=$S/rebase_ref/LSTNtuple_instr_977evt.root \
           BASEHISTS=$S/fin_ref/fin_base977_hists.root bash a09_ref/a09_run.sh <TAG> <ov>`
Tables: `a09_ref/a09_tab.py` (overall + displaced bands), `a09_tab2.py` (per region),
`a09_led.py` (the retirement ledger lines out of the logs). All fall back to fin_ref /
xc_ref / rebase_ref so sibling tags can be quoted in the same table.
NOTE the box is shared with ~14 sibling explorers: load average ran 120-130 all round, so
6-7 parallel 300-evt runs is the sensible batch size and no timing claim is possible.

## BATCH 1 (300 evts, launched on the pre-edit binary md5 519b0abc...)
  A_NOOP   (baseline) -XCD 2                 <- no-op gate, must == FINBASE
  A_R0     -RPS 0 -XCD 2
  A_XCP    -XC 1 -XCD 2                      (pixel-anchored arms only)
  A_XCB    -XC 2 -XCD 2                      (bare-chain arm only)
  A_R0XC0  -RPS 0 -XC 0 -XCD 2               (floor: consumption + universe only)
  A_R0G1   -RPS 0 -XCG 1 -XCD 2              (unification candidate: XC on the RPS score)
  A_R0CCR1 -RPS 0 -CCR 1 -XCD 2              (prediction: == A_R0 bit-identically)

## M7 -- BATCH 2 (300 evts, NEW binary). FOUR BIT-IDENTITY GATES, ALL 33/33.
```
gate                                          meaning
A_NOOP == G2      33/33 IDENTICAL   the post-edit binary reproduces the pre-edit one
G2     == A_UM    33/33 IDENTICAL   the -UM map is inert (it also matches on all 14 metrics)
B_R3   == B_R3C1  33/33 IDENTICAL   -CCR is INERT at -RPS 3
B_R3   == B_R4    33/33 IDENTICAL   making the CHAIN half structural changes NOTHING
```
```
tag       change                      eff      dup     fake     survA  Bretired      nTC
G2        the baseline            0.80992  0.06230  0.05551    26.3    13.0      618793
B_R2      -RPS 2 (delete T3 term) 0.81169  0.07132  0.05542    36.1    10.2      622720
B_R3      -RPS 3 (structural)     0.81151  0.07070  0.05542    35.3    10.3      622416
B_R4      -RPS 4                  0.81151  0.07070  0.05542    35.3    10.3      622416
B_R3X3    -RPS 3 -XCT 3           0.80913  0.06411  0.05551    28.0    13.1      619284
B_R3A7    -RPS 3 -AT3 7           0.81019  0.07137  0.04960    35.6     9.6      616333
LST                               0.80988  0.05179  0.04476     --      --       608190
```

### THE ONE PLACE THIS BEATS THE BASELINE: the FAKE-LEANING operating point
The Baseline agent's fake-leaning alternative is `-AT3 7 -XCT 4.75`
(fin_ref F_A7X475) = eff .80992 / dup .07259 / fake .04952.
`-RPS 3 -AT3 7` at the SHIPPED `-XCT 4` gives eff .81019 / dup .07137 / fake .04960:
BETTER efficiency (+.00027), BETTER duplicate rate (-.00122), the same fake rate (+.00008),
and it needs ONE FEWER TUNED CONSTANT (`-XCT` stays at 4, and `-CCR` disappears).
Mechanism, measured: at `-RPS 1` moving `-AT3` 6 -> 7 costs +.00222 of duplicate rate; at
`-RPS 3` the same move costs +.00067. The fake knob got 3.3x cheaper in duplicates because
it stopped being a seed-dedup knob.

### WHERE IT DOES NOT BEAT THE BASELINE: the duplicate-leaning operating point
At the baseline's own point (dup .0623) the structural modes are BEHIND. `B_R3X3` reaches
survA 28.0 with 13.1 class-B retirements where the baseline reaches survA 26.3 with 13.0 --
i.e. the `-RPS 3` curve is STEEPER, better at high duplicate rate and worse at low. The
crossing is around survA ~31 (dup ~.066). Batch 3 pins the baseline curve above `-XCT 4.5`
so this is read off measured points rather than an extrapolation.

## M2 -- FIRST MAP (3-event smoke test of the new binary; 300-evt version = tag A_UM)
Binary `protoA09/bin/chainproto_a09` md5 fc636849f3bffa4f159e426af3d79408 (built under a
SEPARATE name so `protoA09/bin/chainproto` stays the untouched protoFIN binary 519b0abc...).
`-RPS 3` and `-UM 1` both run. Per event over 3 events, class A (the seeds whose sim already
has a covering TC, i.e. where a survivor is a duplicate by construction):

```
mechanism   fires(A)  onlyA        <- "only" = this mechanism is the SOLE retirer
owned        266.0      0.0
rpsChain     363.0      0.0   <-- the -RPS chain half NEVER fires alone
rpsT3        443.7      7.0
xcPixHit     175.0      4.0
xcPixDR       50.7      0.0   <-- the dR^2<1e-6 pixel branch never fires alone either
xcChain      115.0     93.7   <-- the bare-chain arm is the big UNIQUE retirer
strT3         17.7      0.0   (structural form of rpsT3: 25x rarer)
strChain     148.0      0.0
overlap (non-consumed seeds only):  A: RPSonly 14.7  XConly 98.0  BOTH 188.0  neither 23.7
```
READINGS (to be confirmed at 300 evts):
1. 92.8% of what `-RPS` retires among non-consumed seeds is ALSO retired by an `-XC` arm.
   Deleting `-RPS` outright releases only ~14.7 class-A seeds/evt -- but at the measured
   exchange rate (~.00087 of duplicate rate per class-A survivor per event, from the -XCT
   bracket) that is still ~+.013 of duplicate rate, so it is not free.
2. The `-RPS` CHAIN HALF is completely redundant: mask == 2 alone never occurs.
3. `xcChain` is the load-bearing arm (93.7 unique class-A/evt). `-XCT` cannot be deleted.
4. `xcPixDR` (the dR^2 < 1e-6 branch of the blessed port) never uniquely retires anything --
   measured dead weight, reported but NOT touched (the port is verbatim by mandate).
5. `strT3` fires on 17.7 class-A/evt against `rpsT3`'s 443.7. Almost everything the score
   test retires it retires WITHOUT the object it scored on having been delivered.

## M3 -- 6-EVENT -RPS MODE LADDER (early read; 300-evt versions are batch 2)
Truth partition, per event over 6 events. The ONLY thing that moves is the split between
RPSblock / XCretire / survive -- `consumed` is identical in every mode, which re-confirms
that `-RPS` is purely output-side.
```
mode   RPSblock(A)  XCretire(A)  surviveA   surviveB   surviveC   addedType8/evt
-RPS 1     213.5        111.3      23.3      789.3       32.5        25.5
-RPS 2     154.0        164.3      29.8      791.5       33.5        32.2
-RPS 3     171.5        147.5      29.2      791.3       33.5        31.5
-RPS 4     171.8        147.2      29.2      791.3       33.5        31.5
-RPS 0       0.0        311.5      36.7      791.8       34.0        38.2
```
* Deleting only the bare-T3 term (`-RPS 2`) releases 6.5 class-A seeds/evt; making the
  predicate structural (`-RPS 3`) releases 5.9. Deleting the whole predicate releases 13.4.
  So the T3 term carries HALF of what `-RPS` does and the chain term the other half, and
  the chain term is the one `-XC` cannot substitute for.
* `-RPS 3` and `-RPS 4` are the same to 0.3 seeds/evt: making the CHAIN half structural
  changes essentially nothing, consistent with "rpsChain never fires alone".
* Every mode releases ~2.0-2.5 class-B seeds/evt, i.e. the efficiency side is nearly flat
  between them; the modes differ almost entirely in duplicate cost.
* At the measured exchange rate (.000856 of duplicate rate per class-A survivor per event,
  from the FINBASE -XCT bracket) the predicted duplicate cost is +.0056 (-RPS 2),
  +.0050 (-RPS 3), +.0115 (-RPS 0) before any -XCT retune.

## M4 -- THE DECOUPLING, MEASURED (6 evts). This is the point of the whole angle.
Move `-AT3` 6 -> 7 and watch the SEED side:
```
                RPSblock(A)   surviveA   delivered pT3-class/evt
-RPS 1 -AT3 6      213.5        23.3            120.0
-RPS 1 -AT3 7      205.5        24.5             90.7
-RPS 2 -AT3 6      154.0        29.8            120.0
-RPS 2 -AT3 7      154.0        29.8             90.7      <- IDENTICAL seed side
```
Under the shipped predicate, tightening the DELIVERY margin also releases seeds, so `-AT3`
buys fake rate while paying duplicate rate. With the bare-T3 term deleted the seed side does
not move AT ALL (154.0 -> 154.0, 29.8 -> 29.8) while the delivery volume still falls 120.0
-> 90.7. `-AT3` becomes a pure delivery knob, which is exactly what the Baseline agent's
open item 1 said was needed to attack the fake rate.

## M5 -- BATCH 1, FIRST TWO ROWS (300 evts)
```
tag        change                        eff      dup     fake      nTC   surviveA(class A)
A_R0XC0    -RPS 0 -XC 0 (the FLOOR)  0.81730  0.35541  0.05062   729962      374.3
A_XCP      -XC 1 (pixel arms only)   0.81302  0.15634  0.05295   653007      134.4
FINBASE    the assembled baseline    0.80992  0.06230  0.05551   618793       26.3
LST                                  0.80988  0.05179  0.04476   608190        --
```
TWO CALIBRATIONS COME OUT OF THIS AND ARE USED THROUGHOUT:
  * duplicate rate is LINEAR in class-A survivors: .00087 of dup per surviving class-A seed
    per event ((.15634-.06230)/(134.4-26.3)); the floor row confirms the same slope over a
    ten-times-longer lever arm (.00083).
  * efficiency costs .00058 per class-B seed RETIRED ((.81302-.80992)/(5.8-0.7)).
### BATCH 1 COMPLETE (300 evts). TWO BIT-IDENTITY PROOFS AND THE FULL DECOMPOSITION.
```
tag        change                      eff      dup     fake      nTC   surviveA   R
A_NOOP     the assembled baseline  0.80992  0.06230  0.05551   618793     26.3    --
A_R0       -RPS 0                  0.81182  0.07661  0.05536   624438     41.2   7.53
A_R0CCR1   -RPS 0 -CCR 1           0.81182  0.07661  0.05536   624438     41.2   7.53
A_XCB      -XC 2 (chain arm only)  0.81023  0.06745  0.05695   621663     31.9  16.61
A_XCP      -XC 1 (pixel arms only) 0.81302  0.15634  0.05295   653007    134.4  30.34
A_R0G1     -RPS 0 -XCG 1           0.81103  0.07193  0.05541   622445     36.2   8.67
A_R0XC0    -RPS 0 -XC 0 (FLOOR)    0.81730  0.35541  0.05062   729962    374.3  39.72
LST                                0.80988  0.05179  0.04476   608190      --    --
```
R = (dup gained)/(efficiency gained) against the baseline. LOWER IS BETTER: it is the price
in duplicate rate of buying efficiency by deleting that mechanism, to be compared against
the price of buying the same efficiency by LOOSENING `-XCT` (which is ~9.9 immediately above
`-XCT 4` and flattens to ~30 far above it).

PROOF 1 -- NO-OP GATE: `A_NOOP` reproduces `FINBASE` on all 14 metrics AND
`rebase_ref/cmp_branches.py` reports 33/33 branches IDENTICAL, 0 DIFFER.
PROOF 2 -- `-CCR` IS INERT WITHOUT THE T3 TERM: `A_R0` vs `A_R0CCR1` is 33/33 IDENTICAL.
The +.0044 of efficiency `-CCR 2` is worth exists ONLY because the `-RPS` bare-T3 term fires
on the seed of a revoked delivery. Remove that term and the knob evaporates.

PER REGION -- WHERE `-RPS` ACTUALLY WORKS (300 evts)
```
tag        effB     effT     effE     dupB     dupT     dupE     fakB     fakT     fakE
A_NOOP   0.92660  0.88213  0.74496  0.04344  0.03431  0.08106  0.06680  0.06903  0.04525
A_R0     0.92739  0.88366  0.74828  0.04489  0.03939  0.10485  0.06677  0.06888  0.04516
A_R0G1   0.92637  0.88340  0.74740  0.03601  0.03874  0.10124  0.06707  0.06885  0.04511
A_XCB    0.92660  0.88238  0.74562  0.04362  0.03575  0.08993  0.06754  0.07023  0.04720
LST      0.92557  0.88187  0.74596  0.00989  0.01264  0.08556  0.04249  0.04454  0.04603
```
The duplicate rate `-RPS` uniquely buys is ALMOST ENTIRELY ENDCAP: switching it off moves
dupE .08106 -> .10485 (+.0238) against dupB +.0015 and dupT +.0051. That matters beyond
bookkeeping, because the endcap is the one region where the assembled line is currently
BETTER than LST on duplicates (.0811 vs .0856) -- deleting `-RPS` gives that lead away.
Separately, `-XCG 1` is a BARREL improvement specifically (dupB .04489 -> .03601 at fixed
`-RPS 0`) and does nothing for the endcap.

READINGS
* `-RPS` CANNOT BE DELETED FOR FREE, but it is CHEAP: R = 7.53, against 9.9 for loosening
  `-XCT` by half a unit. Deleting it and re-tightening `-XCT` is therefore, on these two
  numbers, a slightly BETTER way to sit at any given duplicate rate than the baseline's own
  `-XCT` -- which is what batch 3 measures directly instead of extrapolating.
* THE PIXEL-ANCHORED ARMS ARE THE CHEAPEST MECHANISM ON THE BOARD (R = 16.6 to delete, i.e.
  very expensive to give up). They are nearly redundant in COUNT (5.8 unique class-A/evt)
  but what they catch is almost pure duplicate: removing them costs .00515 of duplicate rate
  for .00031 of efficiency. They stay.
* `-XCG 1` (bare-chain arm keyed on the per-seed best chain logit rather than the pair's own)
  is a REAL improvement once `-RPS` is off: A_R0 -> A_R0G1 buys .00468 of duplicate rate for
  .00079 of efficiency (5.9:1), better than `-XCT` can at that point.
* Fake rate barely moves across the whole board (.0506 - .0570). Seed retirement is a
  duplicate-rate mechanism, not a fake-rate mechanism. Only `-AT3` moves fake.

### M6 -- THE TWO LAWS. The whole seed-retirement problem is TWO NUMBERS.
Fitting every point measured this round and every `-XCT` point the Baseline agent measured
(12 configurations spanning an 18x range in survivor count):

    dup = 0.04071 + 0.000844 * (class-A seeds SURVIVING per event)      resid <= .0021
    eff = 0.81730 - 0.000568 * (class-B seeds RETIRED   per event)      resid <= .0002

```
tag        survA   dup    fit-dup  |  Bretired   eff     fit-eff
F_A6X3      20.4  .05689  .05793   |    13.4   .80776   .80969*
E_A6X35     22.9  .05925  .06004   |    13.2   .80904   .80980*
FINBASE     26.3  .06230  .06291   |    13.0   .80992   .80992
F_A6X45     31.5  .06675  .06731   |    11.8   .81037   .81060
A_XCB       31.9  .06745  .06764   |    12.3   .81023   .81032
A_R0G1      36.2  .07193  .07127   |    11.1   .81103   .81100
A_R0        41.2  .07661  .07550   |     9.7   .81182   .81179
A_XCP      134.4  .15634  .15420   |     7.9   .81302   .81281
G_NOOP     140.2  .16098  .15909   |     7.9   .81333   .81281
A_R0XC0    374.3  .35541  .35677   |     0.0   .81730   .81730
```
(*the two tightest -XCT points are the only ones where the efficiency law is off by more
than .0003; their extra loss is real and is the "selectivity collapse below logit 4" the
port round named.)

CONSEQUENCE, AND IT IS THE MAIN INTELLECTUAL RESULT OF THIS ANGLE:
**at a given class-A survivor count the duplicate rate is the same NO MATTER WHICH MECHANISM
did the retiring.** Mechanisms are interchangeable in duplicate terms. They differ ONLY in
how much class B they take with them. So the entire seed-dedup design collapses to ONE
figure of merit --

    SELECTIVITY  =  class-A retired per class-B retired, at the required volume

-- MARGINAL selectivity, not total. Totals are all excellent and all misleading; what
decides the frontier is what the LAST retirements cost. Measured marginals (300 evts,
class-A retired per class-B retired):
```
  -XCT 4.0 -> 3.5 on the baseline          2.8 : 1
  -XCT 3.5 -> 3.0 on the baseline          1.8 : 1
  adding the -RPS predicate to -XC alone   4.5 : 1   (A_R0 -> FINBASE: 14.9 A per 3.3 B)
```
So `-RPS`'s residue is roughly TWICE as selective as anything `-XCT` can buy at the same
volume. That is the quantitative reason the mechanism is not redundant even though 93% of
what it fires on is also caught by `-XC`.

-- and any configuration's (eff, dup) can be predicted from its truth partition alone,
without running the scoreboard at all. Measured selectivity of the whole retirement set:
baseline 649.7 A per 13.0 B = 50:1; `-RPS 0` 634.8 A per 9.7 B = 65:1 at lower volume. The
question batch 3 answers is what the MARGINAL selectivity is when `-XCT` is tightened to
bring `-RPS 0` back to the baseline's volume.

BIG STRUCTURAL FINDING: at `-XC 1` the pixel-anchored arms retire only 5.8 class-A seeds per
event out of the universe, against 113.9 for both arms together. THE PIXEL-ANCHORED ARMS OF
THE PORTED CrossCleanpLS ARE ALMOST ENTIRELY REDUNDANT with `-RPS` + attach consumption; the
bare-chain arm does ~95% of the work. (They retire 2875.8 seeds/evt in absolute terms, but
almost all of those are seeds something else had already retired or that are not in the
post-deletion universe at all.)

## RESUME MARKER (session continued 2026-08-04 23:45)
Batch 3 (8 x 300 evts) was in flight at resume, launched 23:19 on binary
`protoA09/bin/chainproto_a09` md5 fc636849f3bffa4f159e426af3d79408; the box is saturated
(load ~100, ~50 sibling chainproto processes), so wall times are meaningless and the runs
are slow. Also in flight: `W_R2A7` = the 977-evt confirmation of `-RPS 2 -AT3 7`.
Nothing else needs launching before batch 3 lands.

## M8 -- BATCH 3 (frontier at matched duplicate rate) AND BATCH 4 (-XCG + combinations)
Full tables in `a09_scoreboard.txt`. Headline:
* the simplified predicates (-RPS 2 / -RPS 3) buy a STEEPER eff-vs-dup curve. Crossover
  against the baseline curve is at dup ~.0670. Below it the baseline wins (dup .0650:
  .81019 vs .80939); above it the simplification wins (dup .0700: .81132 vs .81079).
  The assembled baseline sits at .0623 -- on the wrong side. `-RPS 0` is DOMINATED
  everywhere and is not a candidate.
* THE ONE PLACE THIS ANGLE WINS: the fake-leaning operating point. `-RPS 3 -AT3 7` at the
  SHIPPED `-XCT 4` = .81019/.07137/.04960 against the baseline agent's `-AT3 7 -XCT 4.75`
  = .80992/.07259/.04952. Better eff, better dup, same fake, ONE FEWER tuned constant, and
  `-CCR` disappears (proven bit-identical inert at -RPS 3).
* 977 CONFIRMS IT: W_R2A7 .80972/.07146/.04995 vs W_A7X475 .80951/.07226/.04987 --
  +.00021 eff, -.00080 dup, +.00008 fake, same signs as the 300.
* `-XCG 1` is FRONTIER-NEUTRAL (within +/-.00025 at matched dup over the whole -XCT range,
  its own 4-point curve measured). Its value is structural: the bare-chain arm stops
  needing the per-pair log.
* `-RPS 2 -XCG 1` together need NO pair log anywhere in seed retirement -- the whole stack
  reduces to two per-seed scalars (plsOwned, plsBestChainLogit) plus the blessed XC
  geometry -- and it measures +.00043 ABOVE the baseline -XCT curve at its own duplicate
  rate (.81098 at dup .06811).

## FINAL STATE
No processes left running. 30 runs this round (28 x 300 evts, 2 x 977). Binary
`protoA09/bin/chainproto_a09` md5 fc636849f3bffa4f159e426af3d79408; `protoA09/bin/chainproto`
is still the untouched protoFIN binary 519b0abc34a28cd1e803d6b9407ef224.
Four bit-identity gates all 33/33 (A_NOOP==FINBASE, A_NOOP==G2==A_UM, B_R3==B_R3C1,
A_R0==A_R0CCR1).
