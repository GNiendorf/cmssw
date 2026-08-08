# A4 -- ROUND 5: feature harvest from LST's own heads

Baseline for everything here is **shipped = 4f1846078d9 (E1-B2)**. Every patch below was verified
with `git apply --check` against that commit. Every physics number came from
`d3_ref/pu_judge.py` (35 fields) on a `bin/lst_cpu -n <N> -s <8|32> -p 0.8` run.

The reproduction gate was passed before any arm was measured: my build with every new knob unset is
**35/35 bit-identical to `e1_ref/pu/B2.judge`** on `event_1000` (`V0_base` and `W0_base`, two
independent builds).

---

## STATUS AFTER THE MAINTAINER'S REDIRECTION

The surrogate deliverable that this README originally led with is **WITHDRAWN AND REJECTED** (see
`WITHDRAWN_surrogate_delete_chain2mlp.patch`). The objection was structural: replacing a network with
a smaller fitted model plus its own generated weights header is not removing machinery. The mandate
became: **delete `Chain2NetworkWeights.h` outright and retrain its one consumer, the attach head,
with the three raw 3-class gate logits as inputs.**

### WHAT IS IMPLEMENTED (worktree g5, builds clean, 0 `error:` in the fresh make log)

`a4_delete_chain2mlp_3logits_WIP.patch` -- verified `git apply --check` clean on 4f1846078d9.
10 files, +471 / -849 lines:
* `Chain2NetworkWeights.h` **deleted** (453 lines), its K7b' evaluation block deleted, the dead
  `gateLogit2` SoA column removed. No surrogate.
* Attach head retrained to 22 inputs, slots 11/12/13 = `chains.zFake/zPrompt/zDisp` (already stored
  per chain for the gate kills and the K9 order key, so no new chain-side arithmetic).
* `src/alpaka/ChainConfigEnv.h` -- env knobs for all eight attach bars plus band offsets, so a
  working-point point costs a run instead of a rebuild. Inert with nothing set.

**It is NOT yet physics-neutral.** It is not shippable as it stands, and the reason is measured, not
guessed. See `FINDINGS_R5.md` entries [A4 21:10] through [A4 09:40] for the full record.

### THE HEAD IS BETTER ON DISPLACED, WORSE ON THE ATTACH DECISION

Retuning mattered: the 20-input baseline recipe (patience 8, lr 1e-3) undertrained 22 inputs
(early-stop epoch 13). An LR search at patience 20 / 200 epochs found lr 3e-3, epoch 70 ("T3C"):

| frozen TEST-60, chain targets | REF f11=gateLogit2 | T3C f11=3 logits |
|---|---|---|
| chain-target AUC | .99821 | .99806 |
| prompt vxy<1 | .99859 | **.99871** |
| displaced vxy[1,5) | .99061 | **.99377** (+.0032) |
| displaced vxy[5,10) | .99722 | .97456 |

The displaced gain is the mechanistically predicted one: `gateLogit2` scores displaced-vs-prompt at
**.336 -- inverted, worse than random** ([A4 01:15]), so the shipped head was being fed an
anti-displaced signal.

But under the **fixed-signal-efficiency calibration on LST's 2x10 (pT x eta) binning** -- the one
calibration designed to remove threshold confounds -- the verdict is unambiguous:

**fake-weighted FPR at matched per-bin signal efficiency: shipped .000387, T3C .000441, ratio 1.141.**
At the same signal efficiency the retrained head admits 14% more background. That is a property of the
head, not of the working point.

### DEPLOYED GATES, EVERY ARM RUN (PU200 tune / holdout; both cube samples clean or bit-identical for
every arm, so PU200 is the binding gate for an attach-head change)

| arm | eff_ovl | eff_bar | eff_trn | dup_bar | vxy[1,5) | conv_T5 |
|---|---|---|---|---|---|---|
| shipped | -- | -- | -- | -- | -- | .6871 |
| MX (1 input, rate-matched) | -.00227 | -.00291 | -.00366 | +.00782 | -.00599 | .6082 |
| MX3 (3 logits, rate-matched) | -.00139 | -.00027 | -.00314 | +.01103 | -.00192 | .6098 |
| MX3 + conversion-matched | -.00137 | **+.00048** | -.00299 | +.01586 | -.00342 | **.6899** |
| MX3 + per-band + xc offset | -.00154 | -.00071 | -.00560 | **+.00035** | -.01248 | .7410 |

**The rows that resist: `dup_barrel` and `eff_transition`.** Every calibration that fixes one breaks
the other; the delivery bars and the crossclean trade against each other and no setting found holds
both. `eff_barrel`, `fake_barrel` and `conv_T5` all reach neutral or better.

### THE CENTRAL MECHANISTIC FINDING (reusable for any attach-head retrain)

Three DIFFERENT quantities can be held fixed across a head swap and they are not interchangeable:
1. **pair acceptance rate** (`refit_bars.py`) -- mixes signal and background, per-pair not per-target.
2. **per-target conversion rate** (`conv.py`, `LSTCHAIN_attachDelta`) -- pins the number of
   attachments but lets their composition drift. Cost 7.8 points of conversion when ignored, and
   doubled `dup_barrel` when matched.
3. **per-bin signal efficiency** (`calib_fixed_eff.py`) -- the only one that isolates head quality.
Conversion rate `n_pT5/(n_pT5+n_T5)` = **.6871 shipped**; both first-generation arms lost ~7.8 points
of it, which alone explains every gate failure (a bare T5 matches worse than a pT5, and the chain plus
its would-be seed then appear as two objects, so eff falls and dup rises).

### NEXT LEVERS, IN THE ORDER I WOULD TRY THEM
(a) **wider first layer** -- 24 units is the 20-input width; 22 inputs may need more capacity. This is
the lever I did not reach and the most likely fix. (b) log-softmax inputs. (c) **joint 4-parameter
calibration** of the three delivery bars WITH the crossclean offset, since those are the two that
trade. (d) more data -- T3C used the 300-event primary dump (52.4M pairs); the shipped head had 114.5M.

### EDGE HEAD AND 3-CLASS GATE: NOT STARTED
Definitions for whoever continues, as required before fitting: a **true edge** = two T3s sharing a sim
track (`label` in `prototype/edges_300evt.root`); a **true chain** = a chain whose members share one
sim track (`label` in `chains_m12_*.root`). Both dumps exist and `calib_fixed_eff.py` retargets to
either. **The gate must hold prompt-class and displaced-class efficiency as TWO separate targets** --
a single lumped target would destroy the displaced lead, which is this project's headline advantage.

---

## ORIGINAL (WITHDRAWN) DELIVERABLE AND THE STILL-VALID SIDE RESULTS

## THE THREE DELIVERABLES

### 1. `a4_ship_delete_chain2mlp.patch` -- DELETE MLP #4 OF 4. **Recommended.**

The 2-class `chainmlp` (`Chain2NetworkWeights.h`, 453 lines, 25->32->32->1, evaluated for every
chain) has exactly ONE consumer in the tree: attach pair input 11 (`ChainAttach.h`). It reads the
SAME 25-float chain row as the 3-class gate head, which runs immediately before it and already
publishes its standardized inputs `x[]` and its three logits `z[]`. The network is replaced by an
OLS fit in those (`Chain2LinearWeights.h`, R^2 .94686, 28 multiply-adds) and deleted.

| sample | result |
|---|---|
| `event_1000` (tune) | eff_ovl +.000239, eff_bar +.000514, eff_trn +.000448, fake_bar -.000009, all dxy bands identical |
| `event_2000` (**held out**) | eff_ovl +.000245, eff_bar +.000623, eff_trn +.000469, fake_bar -.000010, dup_bar +.000102 |
| `cube50` 5000 evt | **35/35 bit-identical** |
| `cube50_highPt` 5000 evt | **35/35 bit-identical** |

Track length unchanged (nlayers 3.6771 -> 3.6770, nhits -.0003, nhitOT +.0001 over 2.05M TCs).
Two disjoint 1000-event samples agree to the 4th decimal on eff_barrel/transition/overall, but each
is individually inside A5's +-35-track band, so the claim is DIRECTION not size.

**Ship patch validated end to end.** The patch applied ALONE to a clean 4f1846078d9 tree compiles
(0 `error:` in the fresh make log) and its binary is **35/35 bit-identical to the measured arm on BOTH
`event_1000` and `event_2000`** (`SHIP_tune`, `SHIP_hold` vs `V1_lin`, `H1_lin`). The shipping form is
provably the thing that was measured, not a mechanical lookalike.

**Caveat, to be stated with the headline:** gateLogit2 becomes an approximation. Admissible only
because (a) its single consumer ranks **16 of 20** in the attach head's first-layer L1 reach
(sum|W1[11,:]| = 1.97 vs 19.11 for the top input) and (b) **no `ChainConfig` bar is expressed on its
scale**. If gateLogit2 ever becomes load-bearing elsewhere, re-price this.

### 2. ~~`a4_ship_t4emit_disp005.patch`~~ -- **FALSIFIED. DO NOT SHIP.** (now `FALSIFIED_t4emit_disp005.patch`)

`minT3DisplacedScore >= 0.05` on the 4-layer class at K10 row emission (`t4EmitMinDispT3`). On BOTH
PU200 files it looked excellent -- fake_barrel -.0009 (tune) / -.00076 (holdout, **2.7 sigma** on a
600k-TC barrel denominator) with every efficiency and dup field within 1e-4 and a single track of
cost in dxy[10,30).

**`cube50_highPt` killed it**, exactly as the brief predicted for anything tuned on PU200:

| metric | base | 0.05 | delta |
|---|---|---|---|
| eff_vxy_1_5 | .251101 | .246696 | -.004405 |
| eff_vxy_5_10 | .119904 | .115108 | -.004796 |
| eff_vxy_10_30 | .025293 | .020315 | -.004978 |
| eff_dxy_1_5 | .093161 | .083919 | -.009242 |
| eff_dxy_10_30 | .0048467 | .0013507 | **-.003496 (72% relative)** |
| dup_barrel | .034211 | .044369 | +.010158 |
| n_tc_t4cl | 157 | 70 | **-87 (55% of the class)** |

The same bar removes **0.7%** of the T4 class on PU200 and **55%** on cube50_highPt -- 80x more
aggressive from one number.

**The transferable lesson: the T3 DNN's displaced-class output does not transfer to cube50_highPt.**
It is PU200-trained and does not fire on far, high-pT displaced tracks, so a floor on it means
"require triplets that look displaced *to a PU200-trained network*" -- a sample-dependent cut in
physics clothing, i.e. scope rule 2. **Any hard bar on cf_20..cf_24 inherits this defect wherever it
is placed** (gate, weld, emission, attach admission) -- including the existing `t3FakeMax = 0.10`
stage-B admission, which nobody appears to have checked on cube50_highPt. Those five columns remain
fine as HEAD INPUTS (a head can learn to discount them); they are not safe as thresholds.

Footnote: the cube50 (not highPt) run of the same bar crashed in the ntuple writer
(`write_lst_ntuple.cc:1852`, garbage `n_accepted_simtrk`, 32 streams) while the same binary and bar
completed cube50_highPt and six PU200 runs. Unresolved: either a pre-existing writer race at -s 32 or
an invariant violated when an ACCEPTED chain emits no row. The two obvious candidates are clean --
both `chains.tcRow()` readers guard on `row < 0`, and a 4-layer chain can never hold a pLS
(`kAttachMinLayers = 5`). Budget for this if post-arbitration row suppression is revisited.

### 3. `a4_instrument_and_arms.patch` -- the env-knob instrument (share this).

`ChainConfigEnv.h` + a one-line hook in the `LSTEvent` **constructor**, plus all five T3-DNN bars and
both gateLogit2 code paths, everything inert by default. Adding a knob is two lines.
**The hook must be in the LSTEvent constructor, NOT in `LST::run`:** the standalone driver
(`standalone/bin/lst.cc:459`) constructs `LSTEvent` directly and never calls `LST::run`, so a hook
there is invisible to every standalone measurement. This cost me one build and one void measurement.
**Always `grep -c "\[A4 env\]" <arm>.log` and confirm the override printed.**

---

## THE PRICED MENU for `t4EmitMinDispT3` (event_1000, 1000 evt, vs `W0_base`)

| bar | f_barrel | f_ovl | d_barrel | eff ovl/bar/trn/end | vxy 1_5 / 5_10 | dxy 1_5 | dxy 5_10 | dxy 10_30 | d(nlayers) | dTC(type9) |
|---|---|---|---|---|---|---|---|---|---|---|
| 0.05  | **-.0009** | -.0003 | +.0000 | **all four +.0000** | both +.0000 | +.0000 | +.0000 | -.0006 | -.00001 | -438 |
| 0.075 | -.0019 | -.0006 | +.0000 | all four +.0000 | both +.0000 | +.0000 | -.0030 | -.0013 | -.0001 | -1022 |
| 0.10  | -.0030 | -.0009 | +.0000 | -.0000/-.0001/0/0 | both +.0000 | -.0010 | -.0060 | -.0032 | -.0004 | -2011 |
| 0.15  | -.0052 | -.0017 | -.0001 | -.0003/-.0003/-.0010/-.0000 | -.0002/-.0015 | -.0013 | -.0089 | -.0089 | -.0014 | -10492 |
| 0.40  | -.0125 | -.0047 | -.0006 | -.0016/-.0017/-.0040/-.0004 | -.0053/-.0118 | -.0055 | -.0228 | -.0228 | -.0027 | -49967 |

0.05 is the knee. Above 0.10 it is a genuine displaced trade; per TRAP 8 the escalation should be
re-priced on top of A2's upstream payments rather than dismissed.

---

## MEASURED FACTS OTHER AGENTS SHOULD NOT RE-DERIVE

**TRAP 1, priced in both directions.** The same floor as a GATE bar at 0.10 kills ~79,000 chains per
1000 events and removes **290** type-9 TCs; at EMISSION the same bar removes **2,011** and 6.9x more
fake_barrel. A gate kill is ~99.6% release: the chains a gate bar removes have already lost their
hit claim to K9.

**The `nlayers -.225` deficit is not the 4-layer class, and killing 4-layer TCs makes it worse.**
Per-`tc_type` composition, `event_1000`, all TCs:

| type | LST n | LST nlay | OURS n | OURS nlay | dN | attributable d(mean) |
|---|---|---|---|---|---|---|
| 4 T5 | 140,608 | 5.573 | 305,914 | 5.396 | +165,306 | +0.4225 |
| 5 pT3 | 152,745 | 5.000 | 89,380 | 5.000 | -63,365 | -0.1544 |
| 7 pT5 | 811,465 | 7.739 | 671,615 | 7.531 | -139,850 | **-0.5955** |
| 8 pLS | 902,295 | 0.000 | 887,711 | 0.000 | -14,584 | +0.0000 |
| 9 T4 | 39,097 | 4.000 | 97,501 | 4.000 | +58,404 | **+0.1138** |
| ALL | 2,046,210 | 3.9017 | 2,052,121 | 3.6770 | | -0.2247 |

43.3% of TC rows are bare pLS at `nlayers == 0`, which is what pins the mean near 3.7-3.9; the
4-layer class is ABOVE our mean, so removing it LOWERS the mean (measured: -2,011 type-9 rows took
3.6771 -> 3.6768). The deficit is the **pT5 <-> T5 swap** -- chains that should have picked up a pLS
are delivered seedless -- i.e. `nlayers` is an ATTACH CONVERSION metric. This falsifies the keystone
hypothesis's track-length leg (the barrel fake/dup legs are untested here).

---

## THE FEATURE-HARVEST REPORT (the assignment's core question)

Everything LST gives its T3/T4/T5 heads and selectors that our 13-float node row lacks, priced as an
addition to the frozen 40-input edge head on identical rows / seed / schedule (5.5M edges, 56
events, event-level 34/11/12 split, 30 epochs):

| arm | +inputs | test AUC | E1 | E2 | vxy[1,5) | vxy>=10 |
|---|---|---|---|---|---|---|
| H0 baseline 40 | 0 | .952221 | .960922 | .913801 | .827546 | .808771 |
| H1 T3 displacedScore | +2 | **+.002269** | +.002124 | **+.004116** | +.004810 | +.005900 |
| H2 non-anchor radius | +2 | +.000891 | +.002075 | -.001644 | +.000871 | -.002371 |
| H4 betaIn | +2 | +.001021 | +.002479 | -.001503 | +.000540 | -.002956 |
| H124 all three | +6 | +.002754 | +.002650 | +.004655 | +.005949 | -.002287 |

* **H2 FALSIFIED.** master's `nonAnchorRegressionRadius`/`nonAnchorChiSquared` are an entire second
  hit set (the other hit of every mini-doublet) that no chain feature reads -- worth +.0009 overall
  and NEGATIVE on the 4-layer family.
* **H4 FALSIFIED.** `betaIn` is the 14th input of LST's own T3 DNN and we never use it: +.0010, also
  negative on E2.
* **H1 is the only real one** and carries all of it (adding H2+H4 buys +.0005 for 4 more inputs).
  **Not recommended as an edge retrain**: +.0023 AUC cannot pay for moving the edge-logit scale,
  which invalidates `thetaEdge`/`thetaEdgeE1`/`thetaEdgeE2` and chain features 2/3/4/18, forcing a
  gate retrain and a re-fit of every gate bar. The same information is already in the gate row as
  cf_23/cf_24 and deliverable 2 spends it there for free.
* **Still unpriced:** resolution (sigma) weighting of our two chi2 features. master's `chiSquared`
  and `rzChiSquared` are per-module-sigma weighted, ours are unweighted cm^2, which mixes 2S-strip
  and PS-pixel resolutions. It REPLACES inputs rather than adding any, so this harness cannot price
  it; it needs a re-dump. This is the one open harvest candidate.
* NOT harvested on purpose: LST's absolute first-hit (eta1, |phi1|, z1, r1). Our node row is
  deliberately origin-free; a head keyed on absolute z trained on PU200's luminous region is exactly
  the cube50 collapse mechanism.

**Method worth reusing:** any per-T3 quantity in the LST ntuple can be joined onto the existing
`prototype/edges_300evt.root` dump WITHOUT re-dumping, keyed on the exact float32 bit pattern of
`t3_fakeScore` (the value DumpWriter stored in `ni_12`/`no_12`). 99.73% of rows resolve, 0.067% of
keys are ambiguous and are dropped rather than guessed. Also `t3_hit_{1,3,5}` are the NON-ANCHOR
hits (`AccessHelper.cc getHitsFromMD` returns `{anchor, outer}` per MD).

---

## FILES

| file | what |
|---|---|
| `a4_ship_delete_chain2mlp.patch` | deliverable 1 (checked clean) |
| `a4_ship_t4emit_disp005.patch` | deliverable 2 (checked clean) |
| `a4_ship_BOTH.patch` | 1+2 together (checked clean) |
| `a4_instrument_and_arms.patch` | the measurement build / env instrument (checked clean) |
| `arm_a_surrogate.py`, `.json` | the chainmlp surrogate fit + the attach L1-reach ranking |
| `arm_b_edge_disp.py`, `arm_b2_harvest.py`, `.json` | the harvest report |
| `arm_c_design.py` | where to cut the T3-DNN scores, per gate cell |
| `parse_hdr.py` | reads any generated weight header into numpy |
| `cmp.py`, `len.py` | 35-field judge diff; track-length means |
| `run.sh` | one arm = one run; records the env overrides it took into `<tag>.cmd` |
| `V0/W0_base`, `V1_lin`, `H0/H1`, `J0/J1`, `K0/K1`, `E0..E5`, `HE/HF/KE/JE` | runs + judges |
| `VOID_build1_*.judge.json` | the void measurement from the `LST::run` hook bug, kept as the record |

Timing (scope rule 6) was NOT measured: ARM A only removes work (1856 -> 28 multiply-adds per chain,
~1.4M chains per 1000 events) and ARM C only reduces downstream volume, but five agents were running
8- and 32-stream jobs throughout, and MEMORY.md forbids timing under contention. It needs an idle
box and can only help.
