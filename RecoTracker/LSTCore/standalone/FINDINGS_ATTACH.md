# FINDINGS_ATTACH.md -- on-policy ATTACH retrain behind the round-2 chain gate (`04c6e122e68`)

Agent AT. Baseline for EVERY number in this file is `04c6e122e68` (the newly shipped candidate),
never the older head. Jets tune = events 0-499 of `jet_ref/trackingNtuple_jets_1000.root`; the
500-999 holdout and PU200 `event_2000` are SEALED and are never run here.

--------------------------------------------------------------------------------------------------

## [AT 23:10] DESIGN ON RECORD, before any build: three arms, and the one that decides whether this round ships is the CONTROL

**What is broken.** `04c6e122e68` retrained the chain gate. The attach head reads that gate's three
RAW logits as its inputs 11/12/13 (`ChainAttach.h:622-624` <- `ChainGate.h:535-537`), standardized
with constants `kFeatMean[11..13] = -1.8029 / 1.1455 / -1.0031` fitted on the round-3 (pre-jet-round)
dump. Those constants, the trained weights behind them, and the eight `ChainConfig` bars that sit on
the head's output scale were all fitted against a gate that no longer exists. Known cost 4 of the
ship commit.

**Machinery reused, not rebuilt** (`nnloop_ref/s3_work/`, the S3 stage of `PLAN_NN_LOOP.md`):
`ea_ref/ea_measure.patch` (pair dump, `git apply` clean on `04c6e122e68`, its `LST_EA_TARGET_ALL`
expansion never enabled) + the `LST_CHAIN_JOIN_DUMP` sidecar reconstructed from
`nnloop_ref/instrument_joindump.txt`; `read_pairs.py`, `joinio.py`, `s3label.py` (label = the
shipped M19 definition, replicated not invented), `train_s3.py`, `barfit_s3.py`, `setbars_s3.py`,
`b1_ref/port_hdr.py`. Gates: `d3_ref/pu_judge.py` (35 fields), `m3_ref/jetphys.py`,
`p4_ref/jetgate.py` (fine core bins), `p4_ref/paired_rle.py` (McNemar, cube-capable).

**Arms.**

| arm | attach WEIGHTS | slots 11-13 standardization | eight bars | what it isolates |
|---|---|---|---|---|
| `ATCAL` | shipped, byte-identical | REFIT on the new dump | refit | the moved logit scale ALONE -- calibration with zero retraining |
| `ATCTL` | retrained, EXACT shipped recipe (`A2_cos3e3` args verbatim) | refit | refit | the mandated re-dump control: on-policy hygiene, no recipe change |
| `ATJ` | retrained, jet-core rows added to the mix | refit | refit | the only recipe change, reported ONLY as (ATJ - ATCTL) |

`ATCTL` is both the mandated control AND, if nothing beats it, the ship candidate: it is exactly the
hygiene fix the NN loop exists to perform. `ATCAL` exists because the brief asks whether the value is
calibration or retraining, and under `barfit_s3.py`'s protocol that question is NOT answerable by
"refit the bars on the shipped head" -- the protocol pins the new head's per-cell TRUE-pair acceptance
to the shipped head's own acceptance measured on the same rows, so for an unchanged head it returns
the shipped bars BY CONSTRUCTION and a bars-only arm is a provable no-op. Re-standardizing slots
11-13 is the one calibration change that is real without touching a weight, so `ATCAL` is the
decomposition: (`ATCAL` - shipped) = calibration, (`ATCTL` - `ATCAL`) = the retrain.

**What I expect, stated in advance so it can be wrong.** EA measured pair-logit AUC .996 (jets) /
.9996 (PU200) and no ranking headroom, and the barfit protocol pins signal acceptance -- so the
efficiency cells should be NEUTRAL by construction and the free quantity is the false-positive rate
at matched acceptance. The one cell with a mechanism pointing at a real gain is the round's largest
known cost: jet-core dup .0232. The duplicate is (delivered chain TC, un-retired bare pLS) and
retirement runs on `xcTheta/T/E` + `rpsThetaChain`, i.e. on this head's logit; EA measured deep-core
TRUE (chain, own-pLS) pairs sitting a median 1.2 logit units BELOW the frozen margin. A head that
ranks deep-core true pairs higher retires more of those seeds. That is the mechanism `ATJ` targets,
and it is a DUP mechanism, not the efficiency one EA priced at +.018.

**Provenance, per PLAN_NN_LOOP.md.** Instrumented CPU binary `bf1372242664371211b7e097b3c4af82`,
lib `627e0163fbaadfb7f1ec2276a8ed51f0`, 0 `error:` in `.make.log.1786589394`, worktree
`gpu_wt/at1` detached at `04c6e122e68`. Pristine ship binary frozen at `at_ref/basebin/`
(`5293c48f2561a6968efecf4fd4b1270f`, lib `e7a11d8115ca59a7d53a26db5ea8c7d5`). Weight-header hashes
in the build: attach `ab7497aae202c8e6c9b0c917fdeec343`, gate `8d1e194ae35003324591dacaaa02a872`,
edge `05b7f3f379aa5fa285538eba5b8b4d8a`, `ChainConfig.h` `8685d4b2d7a0e09a6fcff21e5085c713`.
Every dump records these. Inertness of the instrument is PROVEN below, not asserted.

**Guards.** The E1-B2 far-dca cell is untouched: no kernel, no cell, no conditioning changes -- only
`AttachNetworkWeights.h` (a generated file) and the eight numeric bar initialisers in
`ChainConfig.h`. `dupXcDelta`/`dupXcPtMax` (D's region-conditioned retirement, shipped this round)
are NOT refitted and NOT touched; they are a delta ON a bar, so they ride the new `xcTheta` scale
unchanged -- a caveat I will measure rather than assume.

--------------------------------------------------------------------------------------------------

## [AT 23:20] INSTRUMENT INERT, PROVEN, and the ONE thing about the dump that changed since S3 and invalidates train_s3.py's splice: the pair record is now 22 floats wide, not 20

**Inertness.** `at_ref/basebin` (the pristine ship binary `5293c48f2561a6968efecf4fd4b1270f`, lib
`e7a11d8115ca59a7d53a26db5ea8c7d5`) vs `at_ref/instrbin` (`bf1372242664371211b7e097b3c4af82`, lib
`627e0163fbaadfb7f1ec2276a8ed51f0`), PU200 `event_1000`, 1000 events, `-s 8`, `d3_ref/pu_judge.py`:
**0 of 35 judge fields differ.** No env variable set. `at_ref/runs/BASE_pu.judge` vs `INERT_pu.judge`.

**The record width.** `ChainAttachPairRow::x` is `float x[kAttachFeatures]` and `kAttachFeatures`
is **22** in this tree, so the dump now carries the FULL standardized 22-vector the head consumed --
including slots 11-13, the gate logits standardized with the DEPLOYED header's constants.
`PAIRDUMP_FORMAT.md` still documents `nFeat = 20` and `train_s3.py`'s
`concatenate([a[:, :11], b, a[:, 12:]])` splice assumes it; both are stale and silently WRONG on a
22-wide dump (the splice would drop slot 11 and shift eight columns). `at_ref/s3label_at.py` takes
the width from the header, `at_ref/train_at.py` and `at_ref/barfit_at.py` OVERWRITE columns 11:14
with the re-standardized raw z3 and keep every other column verbatim. **Anyone reusing the S3 scripts
after this round must make the same fix.**

**Corpus validation, four checks, one of them new and truth-free** (`at_ref/labverify.py`, run on
the jet smoke corpus; the same script gates the full corpora below):
 * AUC of the DEPLOYED head's own dumped logit against my replicated label: stage0 .994, stage1
   .982, stage2 .944 (jets, 3 evt) -- a broken join reads .5.
 * MISALIGN control (labels rolled): stage0 .591 on 3 events.
 * **NEW, uses no truth at all:** for chain-kind rows the dumped standardized slots 11-13 must equal
   `(z3 - kFeatMean[11..13]) / kFeatStd[11..13]` with the DUMPING BINARY's own header constants.
   Max |difference| = **5.8e-07** (float32 round-off). That proves the (pair row -> chain row) join
   through `chains.bin` is exact WITHOUT a label, which the S3 round could not check.
 * Bare-T3 rows carry the raw-z3 0 sentinel EXACTLY (max |z3| = 0) and its standardized image
   exactly, i.e. the `attachStdz<11>(0.f)` convention survives the join.

**Corpus sizes.** PU200 `event_1000`, 1000 evt, single stream: ~19 GB `pairs.bin`. Jets rows 0-499:
15,446 pair rows/evt, 0.91% true -- i.e. jets are **LESS** true-rich than PU200 (2.97%), so the
`--flat` machinery `g2_ref` needed for 99.8%-true gun rows is not needed and is not provided.

**BASE (= `04c6e122e68`) tune-half references, my own runs, for every arm below to be judged against**
(`at_ref/runs/BASE_*`): PU200 `eff_overall_incut` .8097, `eff_dxy_1_5` .5621, `eff_dxy_5_10` .2502,
`eff_dxy_10_30` .0538, `dup_overall_incut` .04276, `fake_overall_incut` .04535, `n_tc` 1,583,791.
Jets 0-499: eff jet-core **.7659** (reproduces D's [D 23:40] shipped .7659 exactly), eff dR<.02
.5074, pooled fake .1317, pooled dup .0246.

--------------------------------------------------------------------------------------------------

## [AT 23:55] THE OFF-POLICY SHIFT, MEASURED: the gate's three logits moved ~0.16 header-sigma in LOCATION and up to +26% in SCALE -- and the deployed head absorbed it. Re-standardizing alone (ATCAL) is worth 1% of background at pinned signal, offline.

### 1. How far the inputs actually moved (`at_ref/z3shift.py`, 8M chain-kind pair rows each side)
OLD = `nnloop_ref/s3_work/lab` (dumped behind the PRE-jet-round gate); NEW = my on-policy dump.
Same sample, same 1000 events, same instrument family.

| slot | OLD mean/std | NEW mean/std | d(mean) in units of the SHIPPED `kFeatStd` | std ratio |
|---|---|---|---:|---:|
| `zFake` | -2.686 / 1.243 | -2.943 / 1.240 | **-0.158** | 0.998 |
| `zPrompt` | +1.706 / 1.141 | +1.514 / 1.436 | **-0.156** | **1.259** |
| `zDisp` | -1.491 / 0.913 | -1.681 / 1.028 | **-0.186** | **1.126** |

So the disease is real and it is BOTH a location shift (~0.16 sd, all three in the same direction --
the new gate is globally more negative) and a SCALE inflation on the two true-class logits
(`zPrompt` +26%, `zDisp` +13%). The scale half is the part a bar refit cannot absorb, because
`zPrompt`/`zDisp` enter the head as inputs, not as its output.

**But the head absorbed it.** The DEPLOYED head's own dumped logit ranks my replicated label at
stage-0 AUC **.99954** on the new dataflow, against **.99933** on the old S3 dump -- it is not
degraded at all, it is (marginally) better. That is the first hard evidence that this round's value
is not in the ranking. It corroborates EA's .996/.9996 from an independent direction.

### 2. ATCAL (shipped weights, slots 11-13 re-standardized) -- the bar refit, `at_ref/bars/ATCAL.json`
Protocol: `at_ref/barfit_at.py` = `barfit_s3.py` with `SHIPPED` set to the bars DEPLOYED at
`04c6e122e68` and the 22-wide X fix. Per-bar per-cell TRUE-pair acceptance is PINNED to the deployed
head's own acceptance on the same rows, so only the false-positive rate is free.

| bar | deployed | ATCAL | signal acc (judge split) | FPR ratio |
|---|---:|---:|---:|---:|
| `attachTheta` | 6.626049 | 6.732559 | .70547 -> .70670 | 0.982 |
| `attachThetaT` | 5.753662 | 5.968921 | .67405 -> .67452 | 0.945 |
| `attachThetaE` | 5.907102 | 5.940714 | .84798 -> .84868 | 0.999 |
| `attachThetaT3` | 5.515511 | 5.476098 | .71941 -> .71948 | 1.007 |
| `rpsThetaChain` | 5.480793 | 5.562626 | .87869 -> .87913 | 0.979 |
| `xcTheta` | 3.430463 | 3.414935 | .97957 -> .97956 | 1.016 |
| `xcThetaT` | 2.828650 | 2.887746 | .97840 -> .97799 | 0.984 |
| `xcThetaE` | 3.641699 | 3.693486 | .97707 -> .97693 | 0.987 |

FPR at matched per-CELL signal efficiency: universe A **0.989**, B **1.006**, C **0.988**.
**Read: 1.1% less background in the chain universes, 0.6% MORE in the bare-T3 one.** For scale, the
S3 round's retrain moved the same three numbers to 0.727 / 0.814 / 0.730 -- a 27% background
reduction. This round's calibration lever is ~25x smaller than the previous round's retrain was.
Offline. Deployment is the only referee and ATCAL's gates are running.

Caveat WITH those numbers: the bars move by up to +0.215 (`attachThetaT`), which is small on a
logit scale whose delivery margin is 5.7-6.7 but is NOT zero, so ATCAL is a real physics change and
is judged deployed like any other arm.

### 3. Arms now in flight
`ATCTL` (200 epochs, shipped recipe, PU200 only) on GPU 0; `ATJ25` (the same recipe with the jet
corpus at 25% of the train loss weight -- the gate round's own share) on GPU 1. Both fit the slot
11-13 standardization AND `pos_weight` on the PU200 REFERENCE train split alone, so `ATJ25` is a
genuine nested case of `ATCTL` and the only thing the share changes is the loss.
Jet corpus: 500 events (rows 0-499 ONLY), 6.51M pair rows, 1.75% true overall, 4.45% true on
stage 0 -- i.e. jets are true-RICHER than PU200 at stage 0 (3.09%) and true-POORER overall.

--------------------------------------------------------------------------------------------------

## [AT 00:10] **THE JET-CORE DUP COST IS NOT REACHABLE BY THIS ROUND, AND THE NUMBER THAT CLOSES IT IS THE RETIREMENT RECALL ITSELF: on the jet corpus the deployed head ALREADY retires .953/.976/.985 of true (chain, pLS) pairs at `xcTheta/T/E`.** So the un-retired bare pLS is not a head or a bar failure on the scored population -- it is a pair that was never scored.

`at_ref/evalbars.py`, jet corpus (rows 0-499, 6.51M pair rows, labels verified), each head at the
bars it would DEPLOY with:

| bar | deployed head @ deployed bars | ATCAL @ ATCAL bars | n_true |
|---|---:|---:|---:|
| `attachTheta` (delivery, barrel) | .58026 (bkg 1.07e-3) | .58156 (bkg 9.88e-4) | 30,824 |
| `attachThetaT` | .67945 (2.57e-3) | .66217 (2.21e-3) | 7,927 |
| `attachThetaE` | .84471 (2.92e-3) | .84401 (2.93e-3) | 37,072 |
| `attachThetaT3` (stage B) | .59118 (1.44e-3) | .58718 (1.39e-3) | 30,192 |
| **`rpsThetaChain` (retirement)** | **.85820** (4.59e-3) | .85752 (4.36e-3) | 75,823 |
| **`xcTheta` (retirement, barrel)** | **.95303** (1.99e-2) | .95000 (1.91e-2) | 32,979 |
| **`xcThetaT`** | **.97571** (2.50e-2) | .97618 (2.26e-2) | 8,397 |
| **`xcThetaE`** | **.98511** (1.65e-2) | .98503 (1.57e-2) | 41,962 |

**Read the three `xc*` rows.** Of the (chain, pLS) pairs that ARE scored and ARE the same track, the
deployed head already puts 95.3-98.5% above the retirement bar on JETS. There is no recall left to
recover there, so no retrain and no refit of those bars can remove the round's
(delivered chain TC, un-retired bare pLS) duplicate: the missing pairs are the ones the prefilter and
the K9-accepted target list never present to the head at all -- EA's tier T3 = .337 overall, .221 in
the deep core. **This is the same conclusion EA and D reached from two other directions, now measured
a third way, on the head's own rows.** ATCAL's numbers move these cells by at most .003.

Caveat WITH the headline: these are pair-level rates on the SCORED population, and they say nothing
about a structural change to the target list (EA priced that separately at +.018 core on this base and
measured the naive form of it at -.029). They only close the CALIBRATION route to the dup cell.

Honest consequence for this round: the ranking has no headroom (deployed stage-0 AUC .99954 on the
new dataflow), the calibration is worth ~1% of background at pinned signal, and the dup cell is
structurally out of reach. I expect the deployed arms to be NEUTRAL, and I will report that rather
than dress it up. What the round still delivers is the NN-loop property itself -- the head and its
eight bars back on-policy behind the shipped gate -- plus the three measurements above, which
retire the calibration hypothesis with numbers instead of leaving it as a named open cost.

--------------------------------------------------------------------------------------------------

## [AT 00:55] ATCAL DEPLOYED (calibration only, weights byte-identical): **PU200 efficiency neutral in all 12 paired cells, PU200 dup -0.43% rel, BOTH CUBES PRESERVED (cube50 BIT-IDENTICAL, cube50_highPt dxy[1,5) unchanged at .1209), jets fake <.05 -1.1% rel (p 3e-4) -- but jets dup <.05 +.0033 = +10.5% rel (p 5.5e-4), which FAILS the dup gate.** The mechanism is named and a parameter-free fix is already built and deployed as ATCALM.

Plus a TRAP anyone deploying an attach arm in this tree must know, verified by byte pattern, not assumed.

### 1. The trap: the eight bars compile into `bin/lst_cpu`, the attach weights into `liblst_cpu.so`
`LSTEvent`'s constructor takes `ChainConfig const& chain_config = ChainConfig{}` (`LSTEvent.h:141`),
and the standalone driver default-constructs it, so the DEFAULT MEMBER INITIALISERS -- i.e. all eight
bars -- are instantiated in the CALLER's translation unit and land in **`bin/lst_cpu`**, while
`AttachNetworkWeights.h` lands in **`LST/liblst_cpu.so`**. Verified: the float32 byte pattern of
`xcTheta` appears exactly once in each arm's BINARY and never in any library
(BASE 3.430463 / ATCAL 3.414935 / ATCALM 3.323724, each in its own `bin/lst_cpu` only), and
ATCAL's and ATCALM's libraries are byte-IDENTICAL because they carry the same head.
**Consequence: checking only the resolved `liblst_cpu.so` md5 does NOT pin an arm's bars, and two
arms that differ only in bars have the same library hash.** `at_ref/gates.sh` snapshots and prints
BOTH, and asserts the resolved library against the snapshot.

### 2. ATCAL deployed, all gates, vs BASE = `04c6e122e68`
PU200 `event_1000`, 1000 evt, paired McNemar (`p4_ref/paired_rle.py`):

| cell | BASE | ATCAL | delta | b / c | p |
|---|---:|---:|---:|---|---:|
| overall | .8097 | .8097 | +.0000 | 32 / 33 | 1.0 |
| barrel / transition / endcap | .9242 / .8796 / .6795 | .9241 / .8795 / .6797 | -.0002 / -.0001 / +.0002 | | .36 / 1.0 / .092 |
| **dxy** [0,1) / [1,5) / [5,10) / [10,30) | .8345 / .5621 / .2502 / .0538 | .8345 / .5624 / .2502 / .0538 | +.0000 / **+.0003** / .0000 / .0000 | 0/1, 0/0, 0/0 | .81 / 1.0 / 1.0 / 1.0 |
| **vxy** [1,5) / [5,10) / [10,30) | .7954 / .7128 / .6963 | .7956 / .7133 / .6967 | +.0002 / +.0005 / +.0005 | 0/1, 0/1, 0/2 | 1.0 / 1.0 / 0.5 |

35-field: `dup_overall_incut` .042761 -> .042578 (**-0.43% rel**), `dup_barrel` -2.55% rel,
`dup_transition` -2.43% rel, `fake_overall_incut` +0.09% rel (+4.3e-05), `n_tc` +329 (+0.02%),
`n_tc_t4cl` +0. **All four displaced dxy bands and all three vxy bands neutral-or-better; nothing is
resolved against us anywhere on PU200.**

**Cubes -- the round's headline is intact.** `cube50` (10k evt, full): **every one of the 35 fields
BIT-IDENTICAL.** `cube50_highPt` entries 0-4999: every efficiency cell BIT-IDENTICAL
(`eff_dxy_1_5` = **.120887**), `dup` -.0010, `fake` +1.1e-06, `n_tc` -1.
*Aside worth having on record: the round's headline `cube50_highPt dxy[1,5) = .1209` is the
entries 0-4999 (PRIMARY half) number. The FULL 10k-event sample reads **.1120** on the same shipped
binary, because entries 5000-9999 are a different population (G's "secondary rows"). Both are
reported here; only the 0-4999 slice is comparable to the committed .1209.*

**Jets, tune rows 0-499, paired (`p4_ref/jetgate.py`):** eff jet-core .7659 -> .7669 (+.0010,
p .111, not resolved), eff dR<.02 .5074 -> .5113, eff <.005 .4108 -> .4133.
fake <.05 .3766 -> **.3724** (-.0042, p 2.8e-04), fake <.02 .3997 -> .3924 (p 8.2e-04).
**dup <.05 .0313 -> .0346 (+.0033, p 5.5e-04); dup pooled .0246 -> .0257 (p 2.5e-05). THIS IS A FAIL.**

### 3. Why, and the parameter-free fix (`ATCALM`, deployed, gates running)
The PU200-only bar refit RAISES the retirement bars (`rpsThetaChain` +0.082, `xcThetaT` +0.059,
`xcThetaE` +0.052). On PU200 that is acceptance-neutral BY CONSTRUCTION; on JETS it is not, and
`at_ref/evalbars.py` measures the loss directly: `xcTheta` retirement recall .95303 -> .95000. Fewer
retired seeds is exactly the (delivered chain, un-retired bare pLS) duplicate. **The bar refit is a
SAMPLE-TRANSFER failure, not a head failure.**
Fix, and it introduces no tuned parameter: `barfit_at.py --lab2 <jet lab>` takes each bar as the
**MINIMUM** of its acceptance-matching quantile on PU200 and on jets, so a bar cannot lose TRUE-pair
acceptance relative to the deployed head on EITHER sample. min() is forced by the direction of a
`>=` comparison; the quantity protected is the same per-cell signal acceptance the one-sample
protocol already pins. It moves `xcTheta` 3.4149 -> 3.3237, `xcThetaE` 3.6935 -> 3.6748,
`attachThetaT3` 5.4761 -> 5.4524, `attachThetaE`/`attachThetaT`/`rpsThetaChain` slightly down, and
leaves `xcThetaT` and `attachTheta` PU-driven.

--------------------------------------------------------------------------------------------------

## [AT 01:20] **THE min() BAR PROTOCOL REMOVES THE JETS DUP REGRESSION (`ATCALM`: dup <.05 +.0033 p 5.5e-4 -> +.0008 p .334) AND BUYS MORE PU200 dup (-0.98% rel) -- and it pays for that with a RESOLVED -.0004 on PU200 `eff_barrel` (18 lost / 5 gained, p .0106).** So the bar refit has a narrow trade surface with a small cost at BOTH ends, and the cost swaps sample when the protocol swaps.

| gate cell (vs BASE = `04c6e122e68`) | `ATCAL` (PU-only bars) | `ATCALM` (min over PU200 and jets) |
|---|---|---|
| PU200 `eff_overall_incut` | +.0000 (p 1.0) | -.0001 (p .45) |
| PU200 `eff_barrel` | -.0002 (p .36) | **-.0004 (p .0106, 18/5) <- resolved** |
| PU200 all four dxy bands | +.0000 / +.0003 / .0000 / .0000, all p >= .81 | -.0001 / +.0003 / .0000 / .0000, all p >= .625 |
| PU200 all three vxy bands | +.0002 / +.0005 / +.0005 | +.0002 / +.0005 / +.0005 |
| PU200 `dup_overall_incut` | -0.43% rel | **-0.98% rel** |
| PU200 `fake_overall_incut` | +0.09% rel | +0.17% rel |
| jets eff jet-core | +.0010 (p .111) | -.0001 (p .879) |
| jets fake <.05 | **-.0042 (p 2.8e-04)** | -.0026 (p .019) |
| **jets dup <.05** | **+.0033 (p 5.5e-04) FAIL** | **+.0008 (p .334) PASS** |
| jets dup pooled | +.0011 (p 2.5e-05) | -.0001 (p .821) |
| cube50 (10k, full) | **all 35 fields BIT-IDENTICAL** | **all 35 fields BIT-IDENTICAL** |
| cube50_highPt 0-4999 | every eff cell BIT-IDENTICAL, dup -.0010 | every eff cell BIT-IDENTICAL (0 discordant sims in all 11), `dup` -.0010, `dup_barrel` -.0018, `fake` +1.1e-06 |
| cube50_highPt full 10k | every eff cell BIT-IDENTICAL, dup -.0005 | every eff cell BIT-IDENTICAL, `dup` -.0005, `fake` +8.5e-07 |

Mechanism of the `ATCALM` cost, stated so it is not mistaken for noise: `min()` lowers `xcTheta`
(3.4149 -> 3.3237), i.e. it retires MORE bare pixel seeds. A retired seed that was its sim's ONLY
match is a lost track, and that is D's measured trade (`[D 21:45]`: the retirement-bar family's best
exchange rate is 0.9 duplicate TCs per core sim LOST). Honest multiplicity note WITH the number:
12 paired cells are tested, so a single cell at p .0106 is ~what multiplicity produces
(Bonferroni-corrected p ~ .13) and `eff_overall_incut` itself is UNRESOLVED at p .45; I am reporting
it as a resolved sub-cell rather than deciding it either way.

**These two arms are the CALIBRATION half of the decomposition (weights byte-identical to shipped).
Neither is the round's candidate.** `ATCTL` (the mandated re-dump control -- the retrained head on the
shipped recipe) and `ATJ25` (jets at 25% of the train loss) are at epoch ~75/200 and ~65/200.

--------------------------------------------------------------------------------------------------

## [AT 02:35] **THE MANDATED CONTROL IS NOT A NULL AND IT IS NOT A WIN: `ATCTL` (on-policy retrain, shipped recipe verbatim) buys +.0054 RESOLVED jet-core efficiency (p 3.8e-04) and -.0183 jet fake <.05 (p 8e-11), and pays -.0008 RESOLVED PU200 overall efficiency (p .0017) and jets dup <.05 .0313 -> .0921, a THREEFOLD regression (p 5.5e-36).** The dup mechanism is measured, not guessed, and it is the one the memory rule already names: per-cell acceptance calibration does not protect an ORDERING consumer.

### 1. The head itself: equal in ranking, better in background, on PU200
`best_val_auc` **0.99957843** vs the shipped head's **0.99957727** -- equal to the sixth decimal, at
best epoch 46 vs 117. Frozen-test AUC per stage is identical to five decimals (stage0 .99952 vs
.99953, stage1 .99904 vs .99908, aux4L .99965 vs .99961). **The retrain has no ranking headroom, as
EA said.** What it DOES buy is background at pinned per-cell signal efficiency:
FPR ratio **A 0.908 / B 1.082 / C 0.900** (ATCAL: .989 / 1.006 / .988). So the retrain is worth ~9%
of PU200 chain-universe background and COSTS 8% in the bare-T3 universe.

### 2. Deployed, vs BASE = `04c6e122e68`

| gate | BASE | ATCTL | delta | p |
|---|---:|---:|---:|---:|
| **PU200 eff_overall_incut (paired)** | .8097 | .8088 | **-.0008** | **.0017 (234/170) RESOLVED DOWN** |
| PU200 dxy[0,1) | .8345 | .8336 | -.0009 | .0012 RESOLVED DOWN |
| PU200 dxy[1,5) | .5621 | .5630 | +.0010 | .375 |
| PU200 dxy[5,10) / [10,30) | .2502 / .0538 | .2502 / .0538 | +.0000 / +.0000 | 1.0 / 1.0 |
| PU200 vxy[1,5) / [5,10) / [10,30) | .7954 / .7128 / .6963 | .7941 / .7128 / .6967 | -.0013 / .0000 / +.0005 | .36 / 1.0 / .82 |
| PU200 dup_overall / transition | .04276 / .01019 | .04246 / .00902 | -0.71% / **-11.5%** rel | |
| PU200 fake_overall | .04535 | .04551 | +0.34% rel | |
| **jets eff jet-core** | .7659 | **.7712** | **+.0054** | **3.8e-04 RESOLVED UP** |
| jets eff dR<.02 / <.05 | .5074 / .6091 | .5187 / .6179 | +.0113 / +.0089 | .0047 / .0023 |
| jets fake <.05 / <.02 | .3766 / .3997 | .3582 / .3808 | **-.0183 / -.0190** | 8e-11 / .0018 |
| **jets dup <.05** | .0313 | **.0921** | **+.0608 (2.9x)** | **5.5e-36 CATASTROPHIC** |
| jets dup pooled | .0246 | .0557 | +.0311 (2.3x) | 7.9e-107 |

**The dup mechanism, measured on the head's own rows** (`at_ref/evalbars.py`, jet corpus): ATCTL's
PU200-fitted bars lose JET retirement recall -- `xcTheta` .95303 -> **.89751**, `rpsThetaChain`
.85820 -> **.81247**. Deployed, bare pLS TCs go 15.5 -> 17.9 per event and the jet-core sims won by a
bare pLS go 1340 -> 2292: the seeds that should have been retired onto their chain survive as separate
TCs. **This is the memory rule `class weighting is a threshold instrument, not an argmax one` applied
to the attach head: the eight bars are pinned at fixed per-cell PU200 signal acceptance, and that
pinning says nothing about (a) how the same logit scale transfers to a denser sample or (b) the pLS
CONTENTION atomicMax, which is an ordering consumer of this exact logit.** A retrain that is
AUC-identical and background-better on PU200 still reorders contention and still moves the jet
transfer, and neither is visible in any offline number the protocol pins.

Caveats WITH the headline: the +.0054 jet-core gain is on the TUNE half (holdout sealed, coordinator's
to judge) and is REAL by paired McNemar; the -.0008 PU200 loss is in the PROMPT core (dxy[0,1),
vxy[0,1)) and every DISPLACED band is neutral-or-better, so the crown jewels are not touched; the
jets dup regression is 20x larger than ATCAL's and is the reason `ATCTL` as fitted is not shippable.

### 3. Arms still in flight
`ATCTLM` (ATCTL head + the min() two-corpus bars: `xcTheta` 3.398 -> **2.274**, `rpsThetaChain`
5.421 -> **4.954**) -- the only variant that can restore the jet retirement recall, deployed, gates
running. `ATJ25` (jets at 25% of train loss) -- gates running; its offline signature is the mirror
image of ATCTL's: PU200 FPR ratio 0.988/1.131/0.982 (worse than ATCTL) but the JET background at its
own bars is **8x lower** than ATCTL's (`xcTheta` bkg 2.0e-3 vs 1.7e-2), i.e. the jet rows bought jet
calibration and spent PU200 background.

--------------------------------------------------------------------------------------------------

## [AT 03:05] **THE JET ROWS BELONG IN THE ATTACH MIX, AND IT IS NOT CLOSE: `ATJ25` (jets at 25% of the train loss) delivers jet-core efficiency .7659 -> .8114, +.0455 paired (p 3e-171), deep core dR<.02 .5074 -> .6031 (+.0957), jet fake <.05 -.0372 -- AND PU200 efficiency goes UP, resolved: overall +.0008 (p .0037), barrel +.0014 (p .0024), vxy[1,5) +.0038 (p .0079), dxy[0,1) +.0010 (p 5.9e-04).** Caveats WITH the headline, and one of them is a hard-gate fail: **jets dup <.05 .0313 -> .0826 (2.6x, p 9e-42)**, PU200 `dup_overall` +2.19% rel, PU200 `fake_overall` +0.99% rel, and the jet numbers are the TUNE half (the holdout is sealed and is the coordinator's to judge).

This is the answer to the brief's question "do jet-core rows belong in the attach mix at all", and it
is the opposite of what EA's structural ceiling predicted -- so it is worth saying exactly why the
ceiling did not bind. EA priced the STRUCTURAL lever (widen the K9-accepted target list) at +.018 core
with a FROZEN head, and priced the head's RANKING headroom at zero (AUC .996). Both stand.
`ATJ25` moves neither: it changes **which pixel seeds get retired**, and the efficiency it recovers is
seeds that the PU200-trained head was retiring WRONGLY in the deep core.
The deployed decomposition says so directly: bare-pLS TCs go 15.5 -> 19.6 per event and jet-core sims
whose match is a bare pLS go **1340 -> 2831**, while pT5-won core sims fall 7365 -> 6836. Net +998
jet-core sims that had NO match before. **The gain is a wrong-retirement repair, not a new object.**

| gate (vs BASE `04c6e122e68`) | ATCTL (control) | **ATJ25** | ATCTLM (min bars) |
|---|---|---|---|
| PU200 eff_overall paired | **-.0008 (p .0017)** | **+.0008 (p .0037)** | -.0024 (p 7e-18) |
| PU200 eff_barrel | -.0008 (p .103) | +.0014 (p .0024) | -.0041 (p 3.7e-17) |
| PU200 dxy[0,1) | -.0009 (p .0012) | +.0010 (p 5.9e-04) | -.0024 (p 6.2e-18) |
| PU200 dxy[1,5) / [5,10) / [10,30) | +.0010 / .0000 / .0000 | +.0003 / .0000 / .0000 | +.0003 / .0000 / .0000 |
| PU200 vxy[1,5) / [5,10) / [10,30) | -.0013 / .0000 / +.0005 | **+.0038 (p .0079)** / +.0005 / +.0017 | -.0028 (p .041) / -.0005 / +.0002 |
| PU200 dup_overall | -0.71% | **+2.19%** | -6.71% |
| PU200 fake_overall | +0.34% | **+0.99%** | +0.33% |
| **jets eff jet-core** | +.0054 (p 3.8e-04) | **+.0455 (p 3e-171)** | -.0100 (p 4.9e-11) |
| jets eff dR<.02 | +.0113 | **+.0957** | -.0072 |
| jets eff dR[.02,.05) | +.0056 | +.0622 | -.0198 |
| jets fake <.05 | -.0183 | -.0372 | +.0012 |
| **jets dup <.05** | **+.0608** | **+.0513** | +.0082 |
| jets TCs/evt | -- | 123.1 -> 127.1 | 123.2 |
| cube50 (10k full) | eff cells BIT-IDENTICAL, dup -.0010, fake +1.4e-06 | pending | pending |
| cube50_highPt 0-4999 | eff cells BIT-IDENTICAL (**dxy[1,5) .1209 held**), dup -.0010 | pending | pending |

**`ATCTLM` is REJECTED by measurement** -- the min() two-corpus bar protocol works when the bars move
by ~0.05 (ATCAL) and destroys the arm when they move by ~1.1 (`xcTheta` 3.398 -> 2.274): PU200 overall
-.0024 AND jets core -.0100 AND jets dup still up. On record so nobody re-derives it.

`ATJ25M` (ATJ25 head + min() bars, `xcTheta` 3.484 -> 2.114) is in flight as the one remaining attempt
to buy the jets dup back; given what ATCTLM did to ATCTL I expect it to fail the same way, and I am
recording that prediction before its numbers land.

--------------------------------------------------------------------------------------------------

## [AT 03:35] **CLOSING: the control-corrected decomposition, and it says the ATTACH RETRAIN ITSELF IS WORTH NEGATIVE, THE CALIBRATION IS WORTH NOTHING, AND THE ONLY THING WORTH ANYTHING IS PUTTING JET ROWS IN THE MIX.** Ship candidate `ATJ25R`: jet-core efficiency +.0312 paired (deep core dR<.02 +.0682, dR<.005 +.0572), PU200 dup -5.24% rel, jets fake -.0216, jets dup NOT RESOLVED (p .148), both cubes' efficiency cells BIT-IDENTICAL -- paid for with **PU200 prompt efficiency -.0009 (p 9.9e-04, entirely inside dxy[0,1)/vxy[0,1)) and PU200 fake +1.12% rel**, and the jet numbers are the TUNE half.

### 1. THE DECOMPOSITION THE BRIEF ASKED FOR, each stage against the stage below it

| stage | what it is | jets core eff | jets dup <.05 | PU200 eff | PU200 dup | PU200 fake |
|---|---|---:|---:|---:|---:|---:|
| `ATCAL` - BASE | **CALIBRATION alone** (slots 11-13 re-standardized, weights byte-identical, bars refit) | +.0010 (ns) | **+.0033** | +.0000 | -0.43% | +0.09% |
| `ATCTL` - `ATCAL` | **THE RETRAIN ITSELF** (the mandated re-dump control, shipped recipe verbatim) | +.0044 | **+.0575** | **-.0008** | -0.28% | +0.25% |
| `ATJ25R` - `ATCTL` | **THE JET ROWS + the role-split bars** | **+.0258** | **-.0564** | -.0001 | **-4.53%** | +0.78% |

**Answer, in the brief's own words: the calibration refit is worth approximately nothing (1.1% of
background at pinned signal, and a jets dup regression that eats it), the retrain itself is worth
NEGATIVE (it costs .0008 of resolved PU200 efficiency and TRIPLES the jet-core duplicate for +.0044
of jet-core efficiency), and the whole of the round's value is the one recipe change EA's structural
ceiling predicted would not pay: jet-core rows in the attach mix.**
Why the ceiling did not bind is stated at [AT 03:05] and is the round's real finding: EA priced the
target-list route and the ranking route, and `ATJ25` moves neither. It changes **which pixel seeds get
retired**, and its efficiency is a WRONG-RETIREMENT REPAIR (bare-pLS TCs 15.5 -> 17.5/evt, jet-core
sims won by a bare pLS 1340 -> 2077, +684 core sims that previously had NO match at all).

### 2. FULL TABLE, every gate, every arm, vs BASE = `04c6e122e68` (my own runs, `at_ref/runs/`)

| gate | ATCAL | ATCALM | **ATCTL (control)** | ATCTLM | ATJ25 | **ATJ25R (candidate)** |
|---|---|---|---|---|---|---|
| PU200 eff_overall | +.0000 (1.0) | -.0001 (.45) | **-.0008 (.0017)** | -.0024 (7e-18) | **+.0008 (.0037)** | **-.0009 (9.9e-04)** |
| PU200 eff_barrel | -.0002 (.36) | -.0004 (.011) | -.0008 (.10) | -.0041 (3.7e-17) | +.0014 (.0024) | -.0017 (5.9e-04) |
| PU200 eff_transition | -.0001 | +.0000 | -.0010 (.23) | -.0006 (.51) | +.0015 (.13) | -.0013 (.19) |
| PU200 eff_endcap | +.0002 | +.0002 | -.0009 (.013) | -.0016 (2.8e-06) | +.0000 | -.0002 (.70) |
| PU200 dxy[0,1) | +.0000 (.81) | -.0001 (.63) | -.0009 (.0012) | -.0024 (6e-18) | +.0010 (5.9e-04) | -.0007 (.0099) |
| **PU200 dxy[1,5)** | +.0003 | +.0003 | +.0010 | +.0003 | +.0003 | **+.0003** |
| **PU200 dxy[5,10)** | .0000 | .0000 | .0000 | .0000 | .0000 | **.0000** |
| **PU200 dxy[10,30)** | .0000 | .0000 | .0000 | .0000 | .0000 | **.0000** |
| **PU200 vxy[1,5)** | +.0002 | +.0002 | -.0013 (.36) | -.0028 (.041) | +.0038 (.0079) | **+.0015 (.34)** |
| **PU200 vxy[5,10)** | +.0005 | +.0005 | .0000 | -.0005 | +.0005 | **+.0005** |
| **PU200 vxy[10,30)** | +.0005 | +.0005 | +.0005 | +.0002 | +.0017 | **+.0017** |
| PU200 dup_overall | -0.43% | -0.98% | -0.71% | -6.71% | **+2.19%** | **-5.24%** |
| PU200 fake_overall | +0.09% | +0.17% | +0.34% | +0.33% | +0.99% | **+1.12%** |
| PU200 n_tc | +0.02% | +0.01% | -0.03% | -0.20% | +0.35% | +0.07% |
| **jets eff jet-core** | +.0010 (.11) | -.0001 (.88) | +.0054 (3.8e-04) | -.0100 (4.9e-11) | **+.0455** | **+.0312 (1.2e-94)** |
| jets eff dR<.005 | +.0025 | -- | +.0112 | -- | -- | **+.0572 (1.9e-19)** |
| jets eff dR<.02 | +.0039 | +.0016 | +.0113 | -.0072 | +.0957 | **+.0682 (3.9e-74)** |
| jets eff dR[.02,.05) | +.0022 | +.0000 | +.0056 | -.0198 | +.0622 | **+.0465 (3.3e-30)** |
| jets fake <.05 | -.0042 | -.0026 | -.0183 | +.0012 | -.0372 | **-.0216 (8.9e-16)** |
| **jets dup <.05** | +.0033 (5.5e-04) | +.0008 (.33) | **+.0608 (5.5e-36)** | +.0082 (.013) | **+.0513 (9.2e-42)** | **+.0044 (.148 NOT RESOLVED)** |
| jets dup <.02 | +.0060 | -- | -- | -- | +.0626 | **-.0019 (.72)** |
| jets dup pooled | +.0011 | -.0001 | +.0311 | +.0041 | +.0203 | +.0018 (.013) |
| **cube50, 10k full** | 0 eff cells moved | 0 | 0 | 0 | 0 | **0** |
| **cube50_highPt 0-4999** | 0 eff cells moved, `dxy[1,5)` .1209 | 0 | 0 | 0 | 0 | **0, all 11 cells 0 discordant sims** |
| **cube50_highPt 10k full** | 0 eff cells moved | 0 | 0 | 0 | 0 | **0** |
| cube dup / fake | -.0010 / +1e-06 | -.0010 / +1e-06 | -.0010 / +2e-06 | -.0010 / +2e-06 | **+.0032 / -3e-06** | **-.0005 / +8e-07** |

**Every single arm leaves BOTH cube guns' efficiency BIT-IDENTICAL, in all 35 fields' worth of eff
cells, on all three cube slices. The round's `cube50_highPt dxy[1,5) = .1209` headline is untouched
by construction, not by luck: the cube guns have no chain/pLS pair the attach head can re-decide.**

### 3. WHAT `ATJ25R` IS, exactly
Head `at_ref/models/ATJ25.pt`: `at_ref/train_at.py`, the SHIPPED `A2_cos3e3` recipe verbatim
(`--arm scalar --epochs 200 --patience 200 --hidden 24 --batch-size 16384 --lr 3e-3 --sched cos
--gamma-neg 2 --disp-mid 8 --disp-hi 16 --class-weight m19 --val-cap 6e6 --seed 42`), with
`--lab pu=at_ref/lab/pu --lab jet=at_ref/lab/jet --share jet=0.25`. Slot 11-13 standardization AND
`pos_weight` fitted on the PU200 REFERENCE train split alone, so the mix is a nested case of the
control and only the loss share changes. Jets rows are events 0-499 ONLY.
Bars: the ROLE SPLIT. Delivery bars (`attachTheta/T/E`, `attachThetaT3`) at matched acceptance on
PU200; retirement bars (`rpsThetaChain`, `xcTheta/T/E`) at `min(PU200, jets)`. Both halves come from
the same parameter-free protocol; the split is by the bar's role and was decided from the measured
mechanism (a retirement bar that loses TRUE-pair recall on the denser sample MAKES duplicates;
a delivery bar's failure mode is fake, and fake is judged on PU200). `ATJ25M` (min() on all eight)
is the same head with PU200 fake +2.88% instead of +1.12% and is strictly dominated.
Patch `at_ref/patch/ATJ25R.patch`, md5 **`0a27d3441210ba6b11c508f4ee6e3fc6`**, 586 lines,
**2 files**, `git apply --check` CLEAN against `04c6e122e68`: `AttachNetworkWeights.h` (generated,
byte-verified exporter) and exactly EIGHT numeric initialisers in `ChainConfig.h`. No kernel, no new
constant, no new file, no knob, no env hook. `dupXcDelta`/`dupXcPtMax` untouched; the E1-B2 far-dca
cell untouched.

### 4. THE HONEST FAILS, with the headline and not after it
1. **PU200 prompt efficiency -.0009, RESOLVED (p 9.9e-04, 262 lost / 191 gained), all of it inside
   `dxy[0,1)` and `vxy[0,1)`.** Under `feedback_tuning_priority` efficiency is dominant, so this is
   the gate that matters most and `ATJ25R` does not pass it. Two things that do NOT excuse it but do
   bound it: every DISPLACED band (four dxy, three vxy) is neutral-or-better, and the control
   `ATCTL` loses the SAME -.0008, i.e. **the prompt loss is owned by the on-policy retrain, not by
   the jet enrichment** (`ATJ25R` - `ATCTL` = -.0001).
2. **PU200 `fake_overall` +1.12% rel.** Ranked last, but it is on top of the shipped round's own
   +3.4%.
3. **jets dup <.05 +.0044 is unresolved (p .148) but it is not zero**, and the jet numbers are the
   TUNE half. The 500-999 holdout is sealed and I never ran it.
4. **NOT TIMED.** Nothing in this round was timed; the head has the same 22->24->24->1 shape and the
   same eight bars, so no new arithmetic is added, but that is an argument, not a measurement.
5. `ATJ25`'s jet-core +.0455 is the LARGER efficiency number and it is available if the maintainer
   prefers dup for efficiency: it also takes PU200 efficiency UP (+.0008 resolved) and PU200
   `vxy[1,5)` up +.0038 (p .0079). Its price is jets dup <.05 .0313 -> .0826 and PU200 dup +2.19%.
   Patch `at_ref/patch/ATJ25.patch` md5 `c2635e66709ca83599179a8f57d44da7`, apply-check clean.
   **I am not making that call; both patches are banked.**

### 5. The mandated control as a standalone artifact
`at_ref/patch/ATCTL.patch` md5 `8eb1f1cbbf67300934147268278b36a8`, apply-check clean. It is the pure
NN-loop hygiene fix -- head and bars back on-policy behind the shipped gate, no recipe change -- and
by measurement it is **not shippable on its own**: -.0008 PU200 efficiency and a 2.9x jet-core
duplicate. Recording that is the point of the control: a re-dump retrain with no recipe change is not
a null here either, it is a LOSS, and any future round that re-derives the attach head must beat this
number rather than assume hygiene is free.

### 6. Provenance and artifacts (`at_ref/`, nothing committed, ~50 GB)
Dumps `dump/pu1000` (219.0M pair rows, 1000 evt) and `dump/jet500` (6.51M rows, evt 0-499), each with
`PROVENANCE.txt` carrying the binary md5, the library md5 and the md5 of all four weight headers +
`ChainConfig.h`; corpora `lab/pu`, `lab/jet` with `labverify.py` output. Instrumented binary
`bf1372242664371211b7e097b3c4af82` / lib `627e0163fbaadfb7f1ec2276a8ed51f0`, PROVEN 35/35-field
identical to the pristine ship binary. Arms `arms/<TAG>/` each carry their own `bin/lst_cpu`,
`LST/liblst_cpu.so`, `AttachNetworkWeights.h`, `ChainConfig.h` and `PROVENANCE.txt`. Scripts:
`export_hdr.py` (byte-verified against the shipped header), `s3label_at.py`, `labverify.py`,
`train_at.py`, `barfit_at.py`, `evalbars.py`, `z3shift.py`, `mkcal.py`, `setbars_at.py`,
`deploy.sh`, `gates.sh`, `mkpatch.sh`. **`analysis/DNN/README.md`'s attach row must be updated in the
ship commit** (the standing rule); the replacement recipe line is
`standalone/at_ref/train_at.py --lab pu=... --lab jet=... --share jet=0.25`, bars
`at_ref/barfit_at.py` role-split, and the "Known pending" section can be deleted.
-- AT

### 7. SHIP-VERIFICATION of the candidate patch, the strongest form available
`at_ref/patch/ATJ25R.patch` applied to a PRISTINE `04c6e122e68` worktree and rebuilt CPU-only
(`.make.log.1786606283`, **0 `error:`**) reproduces the MEASURED arm **byte-identically**:
`bin/lst_cpu` md5 `3a9154ee9b4269432aa92ec9bc447466` and `LST/liblst_cpu.so` md5
`e14eab0149d73d6603ee343f1caab4af`, the same two hashes as `at_ref/arms/ATJ25R/`, which is the
snapshot every number in the table above was produced from. So the patch IS the arm, not a
reconstruction of it. (Worktree `gpu_wt/at2` is left holding the candidate; `gpu_wt/at1` holds the
instrumented dump build.)

### 8. Disk housekeeping, so nobody looks for files I pruned
After labelling, `dump/pu1000/{pairs,join,chains}.bin` (24 GB) were deleted; their sha1s, the binary
md5, the library md5 and all five header md5s stay in `dump/pu1000/PROVENANCE.txt`, and the dump is
regenerable in ~16 min with `at_ref/dumprun.sh` from the FROZEN `at_ref/instrbin`. The labelled
corpora `lab/pu` and `lab/jet` -- the things a retrain actually needs -- are kept in full, as is
`dump/jet500`. The cube ROOT files under `runs/` were also pruned (their `.json`/`.judge`, i.e. all
35 fields per arm per slice, are kept); regenerate any of them with
`at_ref/gates.sh at_ref/arms/<TAG> <TAG> cubes` from the frozen per-arm snapshot.
