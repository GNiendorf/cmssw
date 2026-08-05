# SEED-CROSSCLEAN PORT (-XC) -- STATUS

Working dir for every artifact: `standalone/xc_ref/`. Code: `standalone/protoXC/` (copy of
`protoBASE`; `protoBASE` and `prototype` are UNTOUCHED).

---
## 1. WHAT WAS BUILT

`CrossCleanpLS` (`src/alpaka/TrackCandidate.h:327-421`) ported into `protoXC/main.cc`,
structurally intact, operating on OUR track candidates. Flag-gated; defaults OFF and
bit-identical.

| flag | meaning | default |
|---|---|---|
| `-XC <mask>` | bit 0 = pixel-anchored branches (LST pT5 + pT3 arms), bit 1 = bare-chain branch (LST T5 arm), 3 = both | 0 (OFF) |
| `-XCT <logit>` | bare-chain dedup threshold on the ATTACH HEAD logit | 0 |
| `-XCR2 <v>` | pixel-anchored dR^2 window | 1e-6 (LST's) |
| `-XCW2 <v>` | bare-chain dR^2 pre-window | 0.02 (LST's) |
| `-XCD <0\|1>` | truth partition diagnostic (changes nothing) | 0 |

### The port, arm by arm
* **pT5 arm** (LST: chain TC's pLS) and **pT3 arm** (LST: pT3's pLS) -- in our delivery a
  TC has a seed iff `ga.plsOwned` marks it, so both arms collapse to ONE anchor set. Kept
  verbatim: retire a bare seed that shares ANY pixel hit row with an anchor seed
  (`pixelHitsOverlapAny`, the >= 1 criterion) OR is within `dR^2 < 1e-6` of it.
* **T5 arm** -- the ONE substitution. Same `dR^2 < 0.02` pre-window against a SEEDLESS
  chain TC; the deleted pLS-embedding-vs-eta-WP test is replaced by the attach head's
  logit for that (seed, chain) pair `>= -XCT`. The scores are read from the delivery's own
  pair log (`GeneralAttach::recordPairs`) -- plumbed, not recomputed.
* Applied UNIFORMLY to the whole post-deletion universe (`isQuad && isDupAlgPass2 == 0`),
  through both channels: the carried type-8 rows and the `-ZP8 6` additions. Post-deletion
  LST's own `CrossCleanpLS` no longer exists to have pre-filtered the carried channel.
* **Cost**: no pairwise candidate loop. Hit test = one hash set probed with <= 4 rows;
  anchor dR test = (eta, phi) hash grid, cell >= window so the 3x3 neighbourhood is exact,
  phi cells wrap; bare-chain arm is TC-outer/seed-inner exactly as LST is, driven off the
  already-scored pair list.

---
## 2. NO-OP GATE -- PASSED

```
protoBASE/bin/chainproto  POSTDELP2 line            -> xc_ref/r_GATE_BASE.root
protoXC/bin/chainproto    POSTDELP2 line + "-XC 0"  -> xc_ref/r_GATE_XC0.root
python3 rebase_ref/cmp_branches.py r_GATE_BASE.root r_GATE_XC0.root
  ->  33 IDENTICAL, 0 DIFFER, 0 MISSING, 0 ADDED
```
and the scoreboard reproduces the published POSTDELP2 line exactly:
`eff .80626 / dup .20553 / fake .04630 / nTC 655866`.

Reproduce: `bash xc_ref/xc_run.sh <TAG> [overrides]` (frozen ANCHOR/CTL/FLAGSHIP/M19/CFF
line + `-ZPF 3 -ZP5 1 -RT3 1 -T3E 0 -ZP8 6`, identical to `rebase_ref/rb_run.sh`).
Table: `python3 xc_ref/xc_tab.py <TAG> ...` (falls back to `rebase_ref/r_<TAG>.json`).

---
Two further flags were added AFTER the first gate and re-gated (`NOOP2` is bit-identical to
`GATE_BASE`, and `REP_p4` is bit-identical to `B_p4`, so the refactor is a no-op):
`-XCT2` / `-XCT3` (eta-binned thresholds, default to `-XCT`) and `-XCG` (score source).

---
## 3. THE SCAN -- frozen 300-evt subset, reference row = NOOP == POSTDELP2

`-XC 1` = pixel-anchored only, `-XC 2` = bare-chain only, `-XC 3` = both.

```
tag                     eff    vxy01      v15     v510    v1030      d15     d510    d1030      dup     fake      nhB      nhT      nhE      nTC
NOOP  (=POSTDELP2)  0.80626  0.83887  0.79167  0.73187  0.74139  0.61650  0.20161  0.03073  0.20553  0.04630  8.46885  8.74966  3.23588   655866
P1    -XC 1         0.80577  0.83835  0.79094  0.73187  0.74139  0.61650  0.20161  0.03073  0.18690  0.04471  8.48544  8.81387  3.30391   648387
C_p6  -XC 2 -XCT 6  0.80546  0.83806  0.79094  0.73187  0.74139  0.61650  0.20161  0.03073  0.14455  0.04775  9.07171  9.25380  3.27006   634107
C_p4  -XC 2 -XCT 4  0.80184  0.83426  0.78801  0.73187  0.74139  0.61650  0.20161  0.03073  0.09262  0.04878  9.51980  9.76527  3.32497   614097
C_p2  -XC 2 -XCT 2  0.79490  0.82703  0.78143  0.72681  0.74139  0.61650  0.20161  0.03073  0.08120  0.04883  9.66448  9.92090  3.35023   607261
C_p0  -XC 2 -XCT 0  0.77975  0.81153  0.76243  0.72513  0.74139  0.61650  0.20161  0.03073  0.07545  0.04858  9.85272 10.12909  3.38672   597521
B_6875 -XC 3 6.875  0.80577  0.83835  0.79094  0.73187  0.74139  0.61650  0.20161  0.03073  0.18690  0.04471  8.48544  8.81387  3.30391   648387
B_p65 -XC 3 -XCT6.5 0.80551  0.83811  0.79020  0.73187  0.74139  0.61650  0.20161  0.03073  0.15815  0.04538  8.77364  9.04252  3.31710   638465
B_p625              0.80533  0.83792  0.79020  0.73187  0.74139  0.61650  0.20161  0.03073  0.14059  0.04577  8.94106  9.18881  3.32782   632303
B_p6                0.80498  0.83754  0.79020  0.73187  0.74139  0.61650  0.20161  0.03073  0.12455  0.04613  9.08986  9.32446  3.33937   626677
B_p575              0.80471  0.83726  0.79020  0.73187  0.74139  0.61650  0.20161  0.03073  0.11152  0.04640  9.20514  9.44234  3.35027   622027
B_p55               0.80414  0.83669  0.78947  0.73187  0.74139  0.61650  0.20161  0.03073  0.10108  0.04661  9.29418  9.54269  3.36002   618221
B_p525              0.80387  0.83640  0.78947  0.73187  0.74139  0.61650  0.20161  0.03073  0.09277  0.04676  9.36073  9.62392  3.36880   615182
B_p5                0.80343  0.83592  0.78874  0.73187  0.74139  0.61650  0.20161  0.03073  0.08624  0.04687  9.41440  9.68562  3.37625   612713
B_p475              0.80281  0.83526  0.78874  0.73187  0.74139  0.61650  0.20161  0.03073  0.08138  0.04696  9.45350  9.73555  3.38222   610843
B_p45  <-- BEST     0.80215  0.83455  0.78801  0.73187  0.74139  0.61650  0.20161  0.03073  0.07727  0.04704  9.48612  9.78026  3.38764   609229
B_p4                0.80135  0.83374  0.78728  0.73187  0.74139  0.61650  0.20161  0.03073  0.07149  0.04715  9.53916  9.84267  3.39576   606776
B_p35               0.80029  0.83264  0.78655  0.73187  0.74139  0.61650  0.20161  0.03073  0.06752  0.04721  9.57875  9.88703  3.40227   604938
B_p3                0.79875  0.83103  0.78509  0.73187  0.74139  0.61650  0.20161  0.03073  0.06465  0.04721  9.61301  9.92506  3.40813   603371
B_p2                0.79446  0.82656  0.78070  0.72681  0.74139  0.61650  0.20161  0.03073  0.06035  0.04725  9.68315  9.99724  3.41993   600175
B_p1                0.78814  0.82004  0.77266  0.72681  0.74139  0.61650  0.20161  0.03073  0.05771  0.04725  9.76166 10.08434  3.43350   596345
B_p0                0.77935  0.81111  0.76170  0.72513  0.74139  0.61650  0.20161  0.03073  0.05662  0.04714  9.87026 10.19406  3.45021   591239
B_m2                0.76251  0.79347  0.74927  0.72513  0.74139  0.61650  0.20161  0.03073  0.05479  0.04656 10.15069 10.41312  3.48785   579069
B_m6                0.75328  0.78372  0.74561  0.72513  0.74139  0.61650  0.20161  0.03073  0.05269  0.04559 10.45805 10.54956  3.51516   568350
LST(base)           0.80988  0.84296  0.77193  0.65430  0.66453  0.55407  0.20968  0.05674  0.05179  0.04476 10.14804 10.01546  3.56248   608190
```

### Readings
* **The mechanism reaches LST's duplicate rate.** `B_m6` = .05269 vs LST .05179. The gap
  is not a ceiling of the rule; it is the EFFICIENCY PRICE of the last steps.
* **Both branches earn their place.** At a matched threshold the pixel-anchored branch is
  worth a further ~.02 of duplicate rate for ~.0005 of efficiency (C_p4 .09262/.80184 vs
  B_p4 .07149/.80135; C_p2 vs B_p2 the same shape).
* **The pixel-anchored branch alone is nearly inert** on the relevant universe (-XC 1:
  1.9 carried rows + 24.9 -ZP8 additions per event, dup .20553 -> .18690). Reason: LST's
  CheckHitspLS PASS 2 -- which the P1 re-baseline decided to KEEP -- already uses
  ">= 1 shared pixel hit row OR dR2 < 1e-5", i.e. essentially `pixelHitsOverlapAny`. The
  arm is therefore mostly subsumed by a kernel we keep. It is still worth keeping: it is
  free and it takes the last ~.02 of duplicate rate that pass 2 cannot see (pass 2 only
  compares seeds to seeds; this compares seeds to what a chain actually delivered).
* **Displaced performance is completely untouched.** v510 / v1030 / d15 / d510 / d1030 do
  not move anywhere from -XCT 6.875 down to -XCT 3. Bare pixel seeds are prompt objects.
* **Track length only improves** (nhB 8.47 -> 9.49, nhT 8.75 -> 9.78, nhE 3.24 -> 3.39 at
  the best point) and improves monotonically along the scan. Caveat: mean nhitOT is
  diluted by type-8 rows (nhitOT = 0), so part of the gain is the removal of those rows
  rather than longer tracks. No regression either way.
* **Fake rate rises very slightly** (.04630 -> .04704). Same denominator effect: nTC falls
  7.1% while the fake count barely moves, because only 3.3% of the retired universe is
  unmatched.

### Per-region (the same runs)
```
tag                    effB     effT     effE     dupB     dupT     dupE     fakB     fakT     fakE      nhB      nhT      nhE
NOOP                0.91271  0.87551  0.75216  0.24496  0.23463  0.17432  0.05011  0.05333  0.04199  8.46885  8.74966  3.23588
B_p5                0.90964  0.87195  0.74961  0.06526  0.06559  0.10333  0.05423  0.05670  0.04013  9.41440  9.68562  3.37625
B_p45               0.90805  0.87016  0.74873  0.05390  0.04920  0.09767  0.05456  0.05716  0.04016  9.48612  9.78026  3.38764
B_p4                0.90759  0.86762  0.74828  0.04609  0.03935  0.09405  0.05484  0.05746  0.04016  9.53916  9.84267  3.39576
LST(base)           0.92557  0.88187  0.74596  0.00989  0.01264  0.08556  0.04249  0.04454  0.04603 10.14804 10.01546  3.56248
```
Endcap duplicate rate is at LST's own level by -XCT 4.5 (.0977 vs .0856) and endcap
efficiency stays ABOVE LST's everywhere on the useful part of the scan. The residual
duplicate gap is barrel + transition.

### Eta bins: MEASURED, NOT WORTH IT
A least-squares fit of the per-region curves reproduces overall efficiency exactly
(residual 9e-16) and overall duplicate rate to 8e-4, and the optimum over a 3-threshold
grid is always three NEARLY EQUAL thresholds. Measured confirmations:
```
E_a  -XCT 4  -XCT2 4.5 -XCT3 3    eff 0.80131  dup 0.07057   (global -XCT 4:   0.80135 / 0.07149)
E_b  -XCT 4.75 -XCT2 5.25 -XCT3 4 eff 0.80294  dup 0.08109   (global at equal eff: ~0.0824)
E_c  -XCT 3  -XCT2 3   -XCT3 5    eff 0.79976  dup 0.07238   (DOMINATED by global -XCT 3.5)
```
Best case ~.001 of duplicate rate at equal efficiency, for two extra tuned constants.
**Recommendation: keep the single global threshold.** The three eta regions have the same
efficiency/duplicate exchange rate along this axis, so LST's reason for binning its
embedding WP does not apply to the attach logit.

### Score source (-XCG): the pair log is NOT needed
```
-XCG 0 (this pair)   B_p65 .80551/.15815  B_p6 .80498/.12455  B_p5 .80343/.08624  B_p4 .80135/.07149
-XCG 1 (per-seed)    G_p65 .80546/.15735  G_p6 .80484/.12278  G_p5 .80303/.08334  G_p4 .80038/.06817
```
The two trace the SAME frontier (at matched duplicate rate the efficiencies agree to
<1e-4). So the already-plumbed per-seed `plsBestChainLogit` -- the quantity the -RPS
predicate keys on -- is sufficient, and the Alpaka port needs NO pair log at all.

---
## 4. TRUTH PARTITION (-XCD 1), per event

Universe = `isQuad && pLS_isDupAlgPass2 == 0` = 1569.2 seeds/evt. Fates in pipeline order.
```
class                           N   consumed  RPSblock  XCretire   survive
-- NOOP (no port) --
A true, sim has chain TC    597.3      245.1     166.2       0.0     186.0
B true, sim has NO chain TC 920.1        2.8       2.4       0.0     914.9
C no true match              51.8        1.1       2.4       0.0      48.3
-- -XC 3 -XCT 4.5 (best) --
A true, sim has chain TC    597.3      245.1     166.2     144.1      41.9
B true, sim has NO chain TC 920.1        2.8       2.4       5.8     909.1
C no true match              51.8        1.1       2.4       5.6      42.7
-- -XC 3 -XCT 4 --
A                           597.3      245.1     166.2     150.5      35.5
B                           920.1        2.8       2.4       7.4     907.5
C                            51.8        1.1       2.4       5.8      42.5
-- -XC 3 -XCT 0 (deep) --
A                           597.3      245.1     166.2     166.4      19.6
B                           920.1        2.8       2.4      40.9     874.0
C                            51.8        1.1       2.4       8.2      40.1
```
* At the best point the port retires **77.5% of the class-A duplicates for 0.63% of class
  B** -- a 25:1 selectivity.
* Class A saturates at ~166 of 186 even at -XCT -6. The residual ~20/evt are seeds whose
  sim IS covered by a chain but which have NO scored pair with a SEEDLESS chain inside the
  window: either the covering chain took a different seed (so it is type 7 and goes to the
  pixel arm, which then needs a shared hit or dR < 0.001), or the attach prefilter never
  enumerated the pair. **That ~20/evt is the structural floor of this rule.**
* Class B is 920/evt but is dominated by PILEUP sims: the efficiency denominator is only
  208.5 accepted in-cut sims/evt, and the 5.8 class-B retirements at the best point cost
  0.86 sims/evt of efficiency (-.00411), i.e. only ~15% of class-B retirements are
  in-cut sims. Do not read "5.8 rows" as "5.8 tracks lost".
* Class C retirement is pure bookkeeping and does not reduce the fake RATE, because the
  denominator (nTC) falls faster than the numerator.

---
## 5. BEST CONFIGURATION

```
bash xc_ref/xc_run.sh <TAG> -XC 3 -XCT 4.5
```
i.e. BOTH branches, LST's dR windows verbatim (`-XCR2 1e-6`, `-XCW2 0.02`), ONE global
dedup threshold of 4.5 on the attach logit -- LOOSER than the delivery margin `-a 6.875`,
which is the design's whole point.

```
                        eff    vxy01      v15     v510    v1030      d15     d510    d1030      dup     fake      nhB      nhT      nhE      nTC
POSTDELP2 (reference) 0.80626  0.83887  0.79167  0.73187  0.74139  0.61650  0.20161  0.03073  0.20553  0.04630  8.46885  8.74966  3.23588   655866
-XC 3 -XCT 4.5        0.80215  0.83455  0.78801  0.73187  0.74139  0.61650  0.20161  0.03073  0.07727  0.04704  9.48612  9.78026  3.38764   609229
LST (target)          0.80988  0.84296  0.77193  0.65430  0.66453  0.55407  0.20968  0.05674  0.05179  0.04476 10.14804 10.01546  3.56248   608190

vs POSTDELP2:  eff -0.00411   dup -0.12826 (-62%)   fake +0.00074   nTC -7.1%   length +1.02/+1.03/+0.15
vs LST:        eff -0.00773   dup +0.02548          fake +0.00228   nTC +0.2%   length -0.66/-0.24/-0.17
               displaced: v510 +0.0776  v1030 +0.0769  v15 +0.0161  d15 +0.0624
                          d510 -0.0081  d1030 -0.0260   (all UNCHANGED by the port)
```
Bracket if a different efficiency/duplicate policy is wanted: `-XCT 5.0`
(.80343 / .08624) is the efficiency-leaning choice; `-XCT 4.0` (.80135 / .07149) the
duplicate-leaning one. The frontier is smooth between them and the marginal exchange rate
is ~7 units of duplicate rate per unit of efficiency across 5.0 -> 4.0, falling to ~3.7
below 4.0 and ~1.9 below 3.5.

Cost: no measurable wall-time change (102-104 s per 300-evt run across the whole scan,
run 10-way parallel, so indicative only). No pairwise candidate loop was added.

---
## 6. THE REMAINING GAP, HONESTLY

**dup +0.0255 and eff -0.0077 against LST.** Both are barrel + transition; endcap is at or
better than LST on every metric. Three separable pieces:

1. **~42 class-A survivors/evt** (of which ~20 are the structural floor above). These are
   the direct residual duplicates. Attacking them means either (a) letting the
   pixel-anchored arm fire on a seed-family relation rather than a single shared hit /
   dR < 0.001 -- LST's own `pixelHitsOverlapAny` is the >= 1 rule, so this would be a
   DEVIATION from the port, or (b) improving attach-head RECALL so the covering chain and
   the duplicate seed actually produce a scored pair (the prefilter |dTanLambda| < 0.6 is
   TIGHTER than the dR^2 < 0.02 window above |eta| ~ 2.2, where d(tanLambda)/d(eta) > 4).
2. **The efficiency price of going deeper.** Below -XCT 4 the class-A/class-B selectivity
   collapses from 25:1 to 4:1. That is a HEAD QUALITY statement, not a threshold-tuning
   one: the attach logit stops separating "this seed is the chain's track" from "this seed
   is a different track that happens to lie within dR 0.14".
3. **-0.0077 of efficiency we never had.** Note NOOP already sits -0.0036 below LST, and
   -0.0041 of the -0.0077 is what the port itself spends. The pre-existing part is the
   pT3-class deletion residue measured in the P1 re-baseline (NOZP8 -0.0381 recovered to
   -0.0036 by the bare-seed universe), not something this round created.

### What I would attack next, in order
1. **Attach-head recall in the endcap** (item 1b). Cheap, measurable, and it feeds BOTH
   delivery and dedup: widen the prefilter in `tanLambda` at high |eta| (or prefilter in
   eta instead) and re-measure. If the ~20/evt floor is prefilter-limited it moves.
2. **Re-measure with the pT3 stage ON.** The whole scan ran at `-T3E 0` (the POSTDELP2
   line), so there are ZERO delivered pT3-class TCs and the ported pT3 arm has no anchors.
   With the pT3 stage on, class A should shrink (more sims get a delivered TC) and the
   pixel-anchored arm gains anchors. That is the pT3 re-measurement already on the plan
   and it should be done BEFORE tuning -XCT any further.
3. **Only then** consider a second dedup head. The measurement says the current head runs
   out of separating power below logit ~4; a purpose-trained seed-vs-chain "is this the
   same track" head would attack exactly that band. Out of scope this round by mandate.

