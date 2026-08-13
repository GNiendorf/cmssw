# TRIM-NN: can the chain head replace the chi2 terminal trim?

Baseline `394fbab372c` (physics identical to `2840340b6f1`). Work area `gpu_wt/tn1`
(worktree at the baseline + the coordinator's `LST_TRIM_ON` / `LST_TRIM_ABS` env probe,
`trim_ref/trim_env_probe.patch` md5 `8cfe06a8a72ef7818c563e48264ba9c6`, KEPT).
Artifacts under `standalone/trimnn_ref/`.

## [TRIMNN 16:05] The K7a/K7b refactor that makes variant scoring possible is BIT-IDENTICAL

`ChainFeaturesKernel`'s body is now the free function `chainBuildFeatures(... off, nNodes,
mdList, nMD, nLayers, f[25], dca)` and the head is `chainGateLogits(feat, dca, z[3])`, so a
candidate terminal VARIANT of a chain is scored by exactly the deployed feature builder and
exactly the deployed frozen head -- nothing is reimplemented anywhere in this round.

Proof of inertness: 500 jet events, `LST_CHAIN_CHAIN_DUMP` (score, dcaXY, all three logits, all
three margins, flags, the 25-float row and the hit list of every chain), refactored binary vs the
coordinator's r2 binary --

    ON_jets.bin     md5 22082f2b96c87200694252ffbe42b427   (662,244,652 B, r2 06f1c1a1...)
    TN1PAR_jets.bin md5 22082f2b96c87200694252ffbe42b427   (refactored e622c369f0...)

Build clean: 0 `error:`, 0 `failed to compile` on the fresh `.make.log`.

## [TRIMNN 16:10] MECHANISM, and it moves the round's target: the trim's PU200 win is NOT a gate win

`trimnn_ref/py/onoff.py` pairs the trim-ON and trim-OFF chain dumps ROW FOR ROW (chain
construction K6a-K6e is entirely upstream of the trim, so chain index c is the same welded chain
in both arms; the pre-trim node run is asserted equal on every row). Deployed dumps, one binary,
`LST_TRIM_ON` apart.

| | jets 500 evt | PU200 `event_1000` 1000 evt |
|---|---:|---:|
| chains | 3,195,139 | 6,439,634 |
| trim edits | 30,224 (0.95%) | 211,167 (3.28%) |
| inner / outer drops | 7,718 / 22,506 | 28,295 / 182,872 |
| gate-killed fraction | .9090 on / .9093 off | .5922 on / .5920 off |
| edits RESCUED at the gate (killed off, alive on) | 2,053 | 6,520 |
| edits LOST at the gate (alive off, killed on) | 1,378 | **8,052** |
| edits alive in BOTH arms: d(score) worse in | **100.0%** (median -5.38) | **100.0%** (median -7.26) |
| edits alive in BOTH arms: d(mX) HIGHER in | 70.3% | 81.1% |

Two things follow, and the second is the one that matters.

1. **On PU200 the trim LOSES more chains at the gate than it rescues** (8,052 vs 6,520) and yet
   PU200 efficiency goes UP by +.00126. So on the protected sample the trim's gain cannot be a
   gate-purity effect. It is an ARBITRATION effect: every edited chain's `score` falls (100% of
   them, median -7.26), the score is the base term of the K9 claim order key, and the edited
   chain's hit universe shrinks -- so the edit changes WHO WINS WHICH MDs, not primarily who
   passes the gate. On jets the gate ledger is positive (2,053 vs 1,378) but still small next to
   the 30,224 edits.
2. **The frozen head is NOT hostile to the trim's choices on the population the trim edits**:
   the trimmed variant has the HIGHER mX in 70% (jets) / 81% (PU200) of the edits that survive
   the gate in both arms. That is agreement on a subset the CHI2 RULE selected, not evidence the
   head can select it -- which is exactly what the variant probe measures next.

CAVEAT riding with this: `d(score)` falling on 100% of edits is arithmetic, not a finding
(`score = edgeSum + lambdaLen * nLayersAfter` and a drop always removes an edge and usually a
layer). The point is that the deployed rule pays a uniform ranking penalty and still wins, which
means the win is downstream of the gate.

## [TRIMNN 16:20] PHASE 0, THE ROUND-DECIDER: the FROZEN head already ranks variants far better than the chi2 rule, and it wants to trim 4x more often

Method: a new env-gated kernel (`ChainVariantProbe`, `LST_CHAIN_VARIANT_DUMP`) runs PRE-TRIM and,
for every chain with nNodes >= 3, builds all three terminal variants, scores each with the
DEPLOYED `chainBuildFeatures` and the DEPLOYED frozen `chain3mlp`, and writes one `P22C` record per
variant carrying that variant's own hit list. `trimnn_ref/py/truthv.py` (= `p3_ref/truth3.py`, the
`drop` column widened to int32 because it carries the chain index) then gives EACH VARIANT its own
`labelChainsHarness` >= 75% sim-match label. Nothing is reimplemented; the chi2 rule is
reconstructed offline from the dumped per-variant chi2 and layer counts and reproduces the
deployed edit count exactly (30,224 jets / 211,167 PU200, both matching the ON/OFF dumps).

| | jets tune 500 evt | PU200 `event_1000` 1000 evt |
|---|---:|---:|
| chains with nNodes >= 3 (Q3, the cost bound) | 309,703 = **9.7%** of all chains | 1,448,661 = **22.5%** |
| of those, chi2Full > 1.0 (K6f's guard admits) | 49.2% | 44.0% |
| K6f edits | 30,224 (9.8% of eligible) | 211,167 (14.6%) |
| frozen head argmax over mX: full / inner / outer | 59.3% / 18.2% / 22.5% | 55.0% / 15.2% / 29.8% |
| head vs K6f agreement, eligible chains | .611 | .628 |
| **Q2 AUC("this variant is real") from mX** | **.9816** | **.9727** |
| Q2 AUC from the chi2 IMPROVEMENT RATIO | **.4525** | **.4676** |
| Q2 WITHIN-CHAIN pairwise AUC, d(mX) | **.9719** | **.9769** |
| Q2 within-chain pairwise AUC, log chi2 ratio | .4872 | .6013 |
| **Q1b decidable chains** (variants disagree on truth) | 16,307 | 205,895 |
| **on those, the PICKED variant is TRUE: head** | **.8603** | **.9206** |
| the picked variant is true: K6f | .5441 | .7695 |
| the picked variant is true: always-full (trim off) | .4513 | .6761 |
| oracle (best available) | 1.0 by construction | 1.0 |
| truth rate of the selection over ALL eligible chains: head / K6f / full / oracle | .1060 / .0946 / .0847 / .1120 | .6738 / .6501 / .6199 / .6874 |

**The frozen head captures 78% (jets) and 80% (PU200) of the oracle's available gain over
never-trimming. The chi2 rule captures 36% and 45%.** The chi2 improvement ratio is BELOW CHANCE
as a realness score (.45 / .47): it is not a weak signal, it is an anti-correlated one, which is
exactly what "computing a proxy exactly is not answering the question" predicts.

Second finding of Phase 0, and it is the one that explains the deployed result below: **the
trim's own truth ledger is nearly empty.** Of K6f's 30,224 jet edits, 1,516 turn a fake chain into
a true one, 2 do the reverse, and **28,706 (95%) change nothing about realness at all**. On PU200:
19,484 rescues, 247 kills, 191,436 (91%) neutral. The shipped rule's Pareto win is therefore
almost entirely a downstream ARBITRATION effect, not a purity effect -- consistent with the
[16:10] gate ledger.

CAVEAT riding with the headline: an AUC of .98 for realness is NOT a licence. The variant label
(>75% purity over the variant's OWN hits) is mechanically easier to satisfy for a SHORTER
variant, so a rule that maximises it will trim more, and trimming costs layers, `score` and
therefore claim rank. Phase 0 is a licence to build, never a result. Deployment is next.

Also verified rather than assumed (constraint 2): `ChainEdgeInference` (K5) takes
modules / mds / segments / triplets / nodes / incidences / edges and **no `Chains` view at all** --
chains do not exist yet at K5 -- so nothing the trim does can reach the edge head's inputs. The
edge head does not need retraining in this round.

Also measured, and it removes a constant for free: over 928k jet variants and 4.3M PU200
variants the MINIMUM layer count of any dropped variant is exactly **4**, the emission floor.
`trimMinLayersAfter = 5` therefore never protects the floor; all it does is FORBID the 5 -> 4
layer move.

## [TRIMNN 16:25] PHASE 1 ON JETS: deleting EVERY guard and letting the FROZEN head choose beats the trim by 5x its own value -- eff, fake AND dup all better

`ChainTrimLearned` (`src/alpaka/ChainTrimLearn.h`) builds the same three variants K6f builds,
applies the same O(1) endpoint move, and differs ONLY in the comparison: argmax of
`mX = max(zPrompt, zDisp) - zFake` from the FROZEN `chain3mlp` over the three variants, ties to
the full chain. No retraining anywhere. One binary, arms selected by `LST_TRIM_MODE`:

    0  the shipped K6f chi2 ratio rule                                    (reference)
    1  head argmax, EVERY guard deleted
    2  head argmax, but a drop must still keep trimMinLayersAfter layers
    3  head argmax, but only on chains the chi2 concentrating guard admits

jets TUNE half, 500 events, `p4_ref/jetgate.py --split tune`, paired McNemar / clustered
bootstrap, all deltas against arm L0 = the shipped rule:

| cell | L0 ship | **L1 no guards** | L2 keep >=5 layers | L3 keep chi2 guard |
|---|---:|---:|---:|---:|
| core-all | .8120 | **.8233 (+.0112, p 4.9e-22)** | .8148 (+.0027) | .8150 (+.0030) |
| core dR<.005 | .5028 | **.5245 (+.0218, p 6.4e-04)** | .5084 (+.0056) | .5059 (+.0031) |
| core dR<.02 | .6128 | **.6353 (+.0225, p 1.2e-12)** | .6165 (+.0038) | .6187 (+.0059) |
| fake all | .1237 | **.1141 (-.0096, p 2.1e-35)** | .1178 (-.0059) | .1210 (-.0028) |
| fake dR<.02 | .3770 | .3645 (-.0125) | .3721 (-.0049) | .3742 (-.0028) |
| dup all | .0249 | **.0214 (-.0035, p 4.3e-11)** | .0243 (-.0006) | .0230 (-.0019) |
| dup dR<.02 | .0476 | .0383 (-.0093) | .0456 (-.0020) | .0419 (-.0057) |
| dup dR<.005 (the trim's only adverse jet cell) | .0985 | .0987 (+.0003, p .98) | .0963 | .0984 |

**For scale: the entire terminal trim is worth +.0021 core over not trimming. Swapping its
decision for the frozen head's is worth +.0112 on top of the trim -- five times the value of the
rule it replaces -- while taking fake down 7.8% relative and dup down 14% relative.** The trim's
one adverse jet cell (deep-core dup, dR<.005) is left exactly where it was.

**The ordering of the arms is the round's real lesson: the more of the hand-set rule you delete,
the better it gets.** L2 (keeps the layer floor) and L3 (keeps the chi2 concentrating guard) each
throw away ~three quarters of L1's gain. Each guard was suppressing a decision the head gets
right.

CAVEAT riding with this headline: this is the JETS TUNE half only. PU200 is the protected sample
and is running; both cubes are not yet measured; nothing here is a ship recommendation until
those land.

## [TRIMNN 16:30] Both cubes: NEUTRAL for L1 (no regression anywhere; largest move is -.0002)

`d3_ref/pu_judge.py`, 5000 events each, both at `-s 4` (no writer segfault in either arm).

| cell | cube50 L0 -> L1 | cube50_highPt L0 -> L1 |
|---|---|---|
| every vxy band | identical to 4 dp | `[10,30)` .0416 -> **.0414**, others identical |
| every dxy band | identical | `[5,10)` .0649 -> **.0646**, others identical |
| every eta region | identical | identical |
| fake / dup | identical | dup_barrel .0243 -> .0242, rest identical |
| n_tc | 1046 -> 1041 | 956 -> 956 |
| n_tc_t4cl | 460 -> 463 | 430 -> 437 |

The displaced gun samples barely see this change at all, which is expected: their chains are short
and 4-layer-dominated, so few of them have nNodes >= 3 to trim.

## [TRIMNN 16:32] WHY the guards cost so much: `trimMinLayersAfter = 5` made the shipped trim structurally blind to 67% of its own eligible population

Head-argmax edit rate vs K6f edit rate, split by the FULL chain's layer count (probe rows):

| full nLayers | jets: chains / head edits / K6f edits | PU200: chains / head edits / K6f edits |
|---|---|---|
| 5 | 208,293 / **.339** / **.000** | 600,317 / **.249** / **.000** |
| 6 | 100,699 / .545 / .296 | 797,517 / .575 / .228 |
| 7 | 711 / .851 / .605 | 50,827 / .858 / .585 |

A 5-layer chain cannot be trimmed under `trimMinLayersAfter = 5` because every drop lands on 4
layers. That is 67% of the eligible jets population and 41% of PU200's, and the shipped rule
never touched any of it. The head wants to edit a third of it, and 70,588 (jets) / 149,621
(PU200) of those edits DEMOTE a 5-layer chain into the 4-LAYER (T4) gate class.

Caveat riding with this, and it is the one for the coordinator to weigh: the 4-layer class policy
is exactly what jet round 3 tuned, so L1's jet gain is partly a re-population of a recently tuned
cell. That is legitimate -- the head is choosing, and the T4 branch is a real gate -- but it means
the sealed halves and the T4-class TC counts are the check that matters, not the tune halves.

## [TRIMNN 16:45] PU200: L1 beats the trim on EVERY efficiency and EVERY fake cell -- and gives back half the trim's duplicate benefit. It is NOT a strict Pareto win.

Setup check first, because it validates everything above: **arm L0 (`LST_TRIM_MODE=0`) reproduces
the coordinator's shipped `TRIMON` PU200 judge on every cell to 5 decimals and on `n_tc` exactly
(1,583,156)**, from a different binary in a different work area. The arm plumbing is sound.

PU200 `event_1000`, 1000 events, `d3_ref/pu_judge.py`; efficiency significance from
`p4_ref/paired_rle.py` (McNemar, rows sorted on run/lumi/evt):

| cell | TRIMOFF | L0 = ship | **L1 head** | L1 - ship | paired p |
|---|---:|---:|---:|---:|---:|
| eff_overall | .80785 | .80911 | **.81004** | **+.00093** | 3.4e-05 |
| eff barrel / transition / endcap | | | | +.00144 / +.00097 / +.00046 | 7.7e-05 / .23 / .091 |
| vxy [1,5) / [5,10) / [10,30) | | | | +.00000 / +.00148 / **+.00527** | 1 / .74 / .015 |
| dxy [0,1) | .83294 | .83438 | .83532 | +.00094 | 1.9e-04 |
| **dxy [1,5)** | .56433 | .56725 | **.57700** | **+.00975** | .0011 |
| **dxy [5,10)** | .25720 | .25819 | **.28004** | **+.02185** | 1.1e-05 |
| **dxy [10,30)** | .05446 | .05446 | **.05953** | **+.00507** | .022 |
| fake overall | .04597 | .04478 | **.04356** | **-.00122** | |
| fake barrel / transition / endcap | | | | -.00232 / -.00276 / -.00018 | |
| **dup overall** | .04051 | .04008 | .04029 | **+.00021** | |
| dup barrel / transition / endcap | | | | **-.00090** / +.00025 / +.00076 | |
| n_tc | 1,583,935 | 1,583,156 | 1,580,591 | -2,565 | |
| **n_tc_t4cl** | 73,827 | 72,947 | **101,239** | **+28,292 (+39%)** | |

**HEADLINE WITH ITS CAVEAT ATTACHED: L1 beats the shipped trim on every efficiency cell and every
fake cell of BOTH samples and on both cubes, but it is NOT the strict Pareto win the trim is --
PU200 duplicates go the wrong way by +.00021 overall (+.00076 endcap, +.00025 transition, while
barrel improves by -.00090).** Measured against the no-trim baseline instead, L1 still has FEWER
duplicates than not trimming at all (-.00022 vs TRIMOFF), i.e. it keeps about half of the trim's
duplicate benefit and doubles its efficiency and fake benefits:

| vs TRIMOFF | the shipped trim | L1 |
|---|---:|---:|
| eff_overall | +.00126 | **+.00219** |
| dxy [10,30) | +.00000 | **+.00507** |
| fake_overall | -.00119 | **-.00241** |
| dup_overall | **-.00043** | -.00022 |

The duplicate cost is mechanically the `n_tc_t4cl` explosion: L1 demotes ~150k PU200 5-layer
chains into the 4-layer class per 1000 events and the T4 class carries a higher duplicate rate.

Neither guarded arm rescues the duplicate cell: L2 (keep the layer floor) is +.00027 on dup with
only +.00057 eff; L3 (keep the chi2 guard) is dup-neutral (+.00004) but only +.00027 eff and it
LOSES vxy[5,10) by -.00098. **No arm in this round is a strict Pareto improvement on the trim.**

## [TRIMNN 16:55] THE REFEREE CONTROL SETTLES IT: the same unguarded argmax decided by the CHI2 PROXY loses .0092 core efficiency. It is the DECIDER, not the aggressiveness.

The obvious attack on L1 is "it just trims more". `LST_TRIM_MODE=4` is the control that separates
those: the identical kernel, the identical three variants, the identical unguarded 3-way argmax,
decided by the combined-fit chi2 instead of by the head. It is in fact MORE aggressive than L1
(93.6% of eligible chains edited vs L1's 40.7%).

jets tune, 500 events, all vs L0 = ship:

| cell | L0 ship | **L1 head argmax** | **L4 chi2 argmin (control)** |
|---|---:|---:|---:|
| core-all | .8120 | **+.0112** (p 4.9e-22) | **-.0092** (p 8.2e-09) |
| core dR<.005 | .5028 | +.0218 | -.0019 |
| core dR<.02 | .6128 | +.0225 | -.0072 |
| fake all | .1237 | **-.0096** | **+.0073** (p 6.0e-12) |
| fake dR<.02 | .3770 | -.0125 | +.0183 |
| dup all | .0249 | -.0035 | -.0025 |

Trimming harder with the proxy makes the physics WORSE in both directions that matter. Trimming
less hard with the head makes it better. The proxy is not a weak decider, it is the wrong
question, exactly as Phase 0's below-chance realness AUC (.45 / .47) says.

Also recorded here: **arm V0 (`LST_TRIM_MODE=0` on the binary that also contains modes 4 and 5) is
BIT-IDENTICAL to L0 on every jet cell -- 0 discordant sims, 0.0000 on every fake and dup band.**
The added modes are inert at the default, proved rather than asserted.

## [TRIMNN 17:00] SIMPLICITY LEDGER: line-neutral, four constants GONE, one numerical method GONE, three networks unchanged, zero retraining

| | shipped | L1 ship-form |
|---|---|---|
| networks / weight headers | 3 | **3, byte-identical** (no retraining anywhere in this round) |
| kernels in the chain block | K6f is one kernel | **one kernel**, `ChainTrimLearned` replaces `ChainTrimTerminals` |
| hand-set constants | `trimFactor`, `trimAbsChi2`, `trimMinLayersAfter`, `trimPasses` | **all four deleted** (`terminalTrim` on/off kept) |
| numerical methods | `chainFitChi2Combined` (103 lines of double-precision circle + rz fit) exists only to serve the trim | **deleted from the pipeline entirely** |
| lines | -- | `ChainWeld.h` **-232**; `ChainTrimLearn.h` **+196**; `ChainGate.h` **+55** (the K7a/K7b extraction, whitespace-insensitive); `LSTEvent.dev.cc` ~+5. **Net ~+24 lines.** |

The extraction that makes this possible (`chainBuildFeatures`, `chainGateLogits`) is not overhead
attributable to the round: it is what lets the trim, the gate and any future consumer share ONE
feature builder and ONE head instead of copies, and it is proved bit-identical.

No layer floor constant is needed, because a nNodes >= 3 chain's dropped variant spans >= 4 layers
by construction of the weld (minimum over 5.2M measured variants: exactly 4 = the emission floor).

## [TRIMNN 17:00] COST LEDGER (measured counts, not a timing gate)

Per event, single stream, counted off the probe:

| | jets | PU200 |
|---|---:|---:|
| chains / event | 6,390 | 6,440 |
| chains with nNodes >= 3 (the only ones touched) | 619 (9.7%) | 1,449 (22.5%) |
| **extra `ChainFeatures` evaluations added / event** | **+1,858** | **+4,346** |
| extra chain-head (25->32->32->3) evaluations / event | +1,858 | +4,346 |
| combined-fit chi2 evaluations REMOVED / event | -1,229 | -2,725 |
| added feature builds as a fraction of one K7a pass | **+29%** | **+68%** |

So the design adds three `ChainFeatures` builds and three head evaluations per eligible chain and
removes one to three chi2 combined fits per eligible chain. A `ChainFeatures` build is strictly
more work than a `chainFitChi2Combined` (it contains the same two fits plus the T3 aggregates, the
layer walks, the two order statistics and the bridge fits), so this is a real net cost and it is
biggest on PU200. It has NOT been timed; the coordinator should price it.

## [TRIMNN 17:05] Track-length shift on jets: L1 moves core tracks out of T5 and bare-pLS and into a BETTER T4 class

`jetgate.py` TC mix, jets tune, per event and jet-core counts (500 events):

| class | L0 ship | L1 head |
|---|---|---|
| T5 | 45.0/evt, non-fake .860, core 6,927 | 43.2/evt, **.880**, core 6,755 |
| pT5 | 37.7/evt, .997, core 6,844 | 37.7/evt, .997, core **6,923** |
| bare pLS | 16.9/evt, .946, core 1,807 | 16.4/evt, .947, core **1,568** |
| **T4** | 14.2/evt, non-fake **.507**, core 1,849 | **16.4/evt, non-fake .573, core 2,435** |
| eff all-sim (not jet-restricted) | .8305 | **.8362** |
| TCs/evt | 125.1 | 125.0 |

The T4 class grows by 2.2 TCs/event AND gets cleaner (non-fake .507 -> .573), and it takes 586 more
jet-core tracks while bare pixel seeds give up 239. This is the trim's own published mechanism --
"trimming a parasitic terminal RESCUES chains that would otherwise fail the gate or lose the
claim, and they take core tracks a bare pixel seed would otherwise win" -- running four times
harder because the head is willing to demote a 5-layer chain that the chi2 rule was forbidden to
touch.

## [TRIMNN 17:10] The offline proxy PREDICTED the deployed control correctly, which is rare here

Offline, over the eligible chains, the truth rate of the selected variant:

| | edit rate | selected variant is TRUE |
|---|---:|---:|
| jets: head argmax | .407 | **.1537** |
| jets: chi2 argmin (control) | .936 | .1358 |
| jets: never trim | .000 | .1322 |
| PU200: head argmax | .450 | **.7842** |
| PU200: chi2 argmin (control) | .934 | **.7372 -- BELOW never trimming** |
| PU200: never trim | .000 | .7494 |

The offline statement "unguarded chi2-argmin destroys realness relative to not trimming at all"
predicted the deployed result (-.0092 core efficiency, +.0073 fake) before the arm was run. Given
this project's record of offline proxies mispredicting deployment 4+ times, the agreement is worth
recording -- but it is agreement on a CONTROL that failed, not a licence to trust the proxy for
selecting a candidate.

## [TRIMNN 17:15] No trivial length rule reproduces the head's choice

Truth rate of the selected variant over all eligible chains, offline, against every degenerate
rule a referee would propose:

| rule | jets edit rate / selected-true | PU200 edit rate / selected-true |
|---|---|---|
| **head argmax over mX** | .407 / **.1537** | .450 / **.7842** |
| always drop the inner node | 1.000 / .1362 | 1.000 / .7105 |
| always drop the outer node | 1.000 / .1336 | 1.000 / .7335 |
| keep the most layers (= never trim) | .000 / .1322 | .000 / .7494 |
| unguarded chi2 argmin | .936 / .1358 | .934 / .7372 |
| ORACLE | -- / .1611 | -- / .7955 |

The head is the only rule that beats never-trimming on BOTH samples, and it does so while editing
less than half as often as any always-drop rule.

## [TRIMNN 17:20] COUPLING #1 MEASURED, not assumed: L1 disturbs the CLAIM ORDER KEY on 4% of all chains and 70% of its edits change gate branch

`trimnn_ref/py/onoff.py` on the two chain dumps (`LST_TRIM_MODE=0` vs `=1`, one binary, jets 500
events, paired by chain index with the pre-trim node run asserted equal on every row):

| | ship (K6f) vs no trim | **L1 vs ship** |
|---|---:|---:|
| chains edited | 30,224 (0.95% of all chains) | **126,123 (3.95%)** |
| inner / outer drops | 7,718 / 22,506 | 56,446 / 69,677 |
| nLayers fell | 30,224 | 106,551 (84% of edits) |
| **gate BRANCH changed** | 6,311 | **88,187 (70% of edits)** |
| rescued at the gate (killed before, alive after) | 2,053 | **8,960** |
| lost at the gate | 1,378 | 3,582 |
| gate-killed fraction | .9093 -> .9090 | .9090 -> **.9071** |
| edits alive in both arms: `score` (= the K9 order-key base) is WORSE in | 100.0% | 73.0% (median -4.47) |
| edits alive in both arms: head margin mX is HIGHER in | 70.3% | 74.1% |

So the coupling is real and four times larger than the shipped rule's: L1 changes the gate branch
of 88k chains per 500 jet events and pushes most edited chains DOWN the claim order key. The
efficiency gain survives that, and the gate ledger is now strongly positive (+5,378 net chains
alive against the trim's +675), which is where the core-efficiency gain comes from.

Also proved here: the chain dump of `LST_TRIM_MODE=0` on the mode-4/5 binary is md5-identical
(`22082f2b96c87200694252ffbe42b427`, 661,999,964 B) to the coordinator's r2 baseline binary. Three
independent binaries in this round produce the identical baseline dump.

## [TRIMNN 17:25] The control on PU200 is unambiguous: the proxy-driven argmax destroys exactly the displaced cells the head-driven one rescues

Same kernel, same variants, same unguarded 3-way argmax, PU200 `event_1000`, 1000 events:

| cell | L0 ship | **L1 head argmax** | **L4 chi2 argmin (control)** |
|---|---:|---:|---:|
| eff_overall | .80911 | **+.00093** | **-.00248** |
| eff barrel | .92349 | +.00144 | -.00456 |
| vxy [1,5) | .79773 | +.00000 | **-.02160** |
| vxy [5,10) | .71379 | +.00148 | **-.05468** |
| vxy [10,30) | .70416 | +.00527 | **-.05098** |
| dxy [1,5) | .56725 | **+.00975** | **-.03346** |
| dxy [5,10) | .25819 | **+.02185** | -.01589 |
| dxy [10,30) | .05446 | **+.00507** | +.00000 |
| fake overall | .04478 | -.00122 | -.00082 |
| dup overall | .04008 | +.00021 | **+.00103** |
| n_tc_t4cl | 72,947 | 101,239 | **221,168 (3.0x)** |

The two arms differ ONLY in which function ranks the variants, and they move the protected
displaced bands in opposite directions by 5 percentage points. This is the round's central
evidence for the maintainer's premise: the trim's problem was never how often it fired, it was
that it was asking the wrong question.

## [TRIMNN 17:40] TRACK LENGTH PRICED (maintainer directive): L1 costs 0.18 OT hits per REAL track on PU200 -- but a margin gap of 1.0 REVERSES the loss on jets and is now the better candidate

Definition (`trimnn_ref/py/length.py`, matched to `prototype/compare_ab.py`'s `mean_nhitOT` and to
`d3_ref/pu_judge.py`'s TC denominator): mean `tc_nhitOT` over TCs with `tc_pt > 0.9`.
Reference point: at the 2026-08-06 master baseline the chain pipeline sat at **6.427 vs LST
master's 6.519** -- already 0.09 short.

**PU200 `event_1000`, 1000 events**

| | TRIMOFF | L0 = ship | L3 chi2-guard | L2 layer-floor | **L1 no guards** |
|---|---:|---:|---:|---:|---:|
| **mean nhitOT** | 6.4233 | **6.3207** | 6.2825 | 6.2008 | **6.1393** |
| mean nhitOT, MATCHED TCs | 6.4737 | 6.3748 | 6.3388 | 6.2533 | **6.1961** |
| mean nhitOT, FAKE TCs | 5.3775 | 5.1682 | 5.0735 | 5.0636 | 4.8912 |
| mean nlayers, matched | 4.0706 | 4.0346 | 4.0054 | 3.9788 | 3.9335 |
| len T5-class | 10.9715 | 10.7297 | 10.7332 | 10.5545 | 10.5529 |
| **len pT5-class** | 11.0975 | **10.9083** | 10.9052 | 10.6569 | **10.6466** |
| n T5 / n T4 | 259,671 / 73,827 | 249,820 / 72,947 | 239,498 / 90,743 | 246,518 / 71,864 | 228,655 / 101,239 |

**L1 gives up 0.181 OT hits per TC and 0.179 per MATCHED TC against the shipped rule (2.8%), and
0.284 against TRIMOFF.** Against the historical reference that is a move from -0.09 to roughly
-0.38 versus LST master on a metric where we were already behind, and it does NOT show up
anywhere in the efficiency / fake / duplicate tables.

The breakdown the directive asked for says BOTH mechanisms are present and names the dominant one:

* **class re-mix**: 21k PU200 T5-class TCs (~10.73 OT hits) become T4-class (exactly 8.0). Worth
  about -0.036 of the -0.181.
* **within-class truncation, and this is the bigger term**: the **pT5 class shortens from 10.9083
  to 10.6466 at essentially constant count (572k)**, worth about **-0.095** on its own. These are
  matched, pixel-seeded, high-quality tracks losing a real outer-tracker hit. That is the cost
  the maintainer is worried about, and it is the majority of the effect.
* the fake TCs shorten twice as fast (5.168 -> 4.891) as the matched ones, which is the benign
  half and is where the fake-rate gain comes from.

**jets tune, 500 events -- and here the picture INVERTS with a margin gap**

| | TRIMOFF | L0 = ship | **L1 gap 0** | **G10 gap 1.0** | G20 gap 2.0 |
|---|---:|---:|---:|---:|---:|
| mean nhitOT | 8.9337 | 8.8695 | 8.7829 (**-.087**) | **8.9008 (+.031)** | 8.9339 (+.064) |
| mean nhitOT, matched | 8.9642 | 8.9025 | 8.8275 | **8.9488 (+.046)** | 8.9766 |
| **mean nhitOT, JET-CORE TCs** | 9.3346 | 9.3122 | 9.3286 | **9.4363 (+.124)** | 9.4257 |
| jet-core TCs | 8,581 | 8,592 | 8,859 | **8,767** | 8,709 |
| core-all efficiency vs ship | -.0021 | -- | **+.0112** | **+.0077** | +.0054 |
| jet fake vs ship | +.0022 | -- | -.0096 | **-.0063** | -.0044 |
| jet dup vs ship | +.0024 | -- | -.0035 | **-.0040** | -.0025 |

**`LST_TRIM_MODE=5` with `trimMarginGap = 1.0` (G10) -- the head must beat the full chain by one
logit unit, ONE constant replacing the shipped rule's four -- keeps 69% of L1's jet core-efficiency
gain (+.0077 of +.0112), takes duplicates DOWN FURTHER than L1 (-.0040 vs -.0035), and makes
tracks LONGER than the shipped rule on jets (+.031 overall, +.124 on jet-core TCs).** Its PU200
run is still in flight; that number decides between G10 and L1 and I will not pre-judge it.

Mechanism of the reversal, from the offline gap scan: raising the gap from 0 to 1.0 halves the
edit rate (.407 -> .221 jets, .450 -> .240 PU200) and halves the 5 -> 4 layer demotions
(70,588 -> 32,258 jets), while the offline truth rate of the selected variant falls by only
.0047 (.1537 -> .1490). The marginal trims -- the ones the head prefers by a hair -- are the ones
that shorten tracks without buying realness.

## [TRIMNN 17:45] The length/realness exchange rate, and why a margin gap improves it

Offline, over every eligible chain, MDs removed by the selection (each MD is 2 OT hits) against the
realness bought, as a function of the decisiveness gap:

| gap | jets: edits / OT hits removed / selected-true | PU200: edits / OT hits removed / selected-true |
|---:|---|---|
| 0.0 (L1) | 126,123 / **275,902** / .1537 | 651,435 / **1,476,878** / .7842 |
| 0.5 | 94,374 / 207,568 / .1516 | 484,014 / 1,102,146 / .7814 |
| **1.0 (G10)** | 68,339 / **150,718** / .1490 | 348,308 / **794,594** / .7773 |
| 2.0 (G20) | 33,218 / 73,084 / .1435 | 173,464 / 392,718 / .7673 |
| never trim | 0 / 0 / .1322 | 0 / 0 / .7494 |

Read as an exchange rate against never-trimming, PU200: L1 spends 1.48M outer-tracker hits to buy
+.0348 of realness; **G10 spends 794k (54%) to buy +.0279 (80%)**. The marginal trims are the
expensive ones. Same story on jets: G10 buys 79% of the realness for 55% of the hits.

## [TRIMNN 17:50] The incumbent already pays this tax and nobody priced it: the SHIPPED trim costs 0.103 OT hits per TC on PU200

`TRIMOFF -> L0 = the shipped rule` is **6.4233 -> 6.3207 mean nhitOT, a loss of 0.103 per TC**
(matched TCs 6.4737 -> 6.3748, -0.099), bought for +.00126 overall efficiency. So the length cost
is not something the learned rule introduces; it is intrinsic to trimming at all, and the shipped
Pareto win already carries an unpriced 0.10-hit charge on the same metric where the pipeline was
already 0.09 behind LST master.

The right question is therefore the EXCHANGE RATE, not the level. PU200, per 0.001 of overall
efficiency bought against TRIMOFF:

| arm | d eff vs TRIMOFF | d nhitOT vs TRIMOFF | **OT hits per 0.001 eff** |
|---|---:|---:|---:|
| L0 = shipped trim | +.00126 | -0.103 | **82** |
| L1 no guards | +.00219 | -0.284 | **130** |
| G10 gap 1.0 | (PU200 in flight) | (in flight) | (in flight) |

L1 buys 1.7x the efficiency for 2.8x the hits, so on this axis alone it is a WORSE trade than the
incumbent even though it is a better rule on every physics cell. That is exactly the cost the
gate set is blind to, and it is why the G10 working point matters: on jets G10 already buys 69%
of L1's efficiency gain while making tracks LONGER than the incumbent.

## [TRIMNN 18:05] NEW LEAD CANDIDATE **G10**: the head plus ONE decisiveness constant beats the trim on eff, fake AND dup on both samples, makes jet tracks LONGER, and costs 0.020 OT hits on PU200

`ChainTrimLearned` with `trimMode = 5, trimMarginGap = 1.0`: the head's preferred variant must beat
the full chain by one logit unit. **One constant, replacing the shipped rule's four.**

**PU200 `event_1000`, 1000 events, vs L0 = ship** (paired McNemar for efficiency):

| cell | ship | **G10** | delta | p |
|---|---:|---:|---:|---:|
| eff_overall | .80911 | .80966 | **+.00054** | .0027 |
| barrel / transition / endcap | | | +.00075 / +.00060 / +.00033 | .013 / .31 / .19 |
| vxy [1,5) | .79773 | .79752 | **-.00021** | **1.00 (16 vs 15 discordant sims -- one track, pure noise)** |
| vxy [5,10) / [10,30) | | | +.00246 / **+.00455** | .38 / .015 |
| dxy [1,5) | .56725 | .57667 | **+.00942** | 2.6e-04 |
| dxy [5,10) | .25819 | .27507 | **+.01688** | 2.2e-04 |
| dxy [10,30) | .05446 | .05636 | +.00190 | .38 |
| **fake overall** | .04478 | .04425 | **-.00053** | |
| fake barrel / transition | | | -.00113 / -.00098 | |
| **dup overall** | .04008 | .03991 | **-.00017** | |
| dup barrel / transition / endcap | | | -.00078 / -.00005 / **+.00013** | |
| n_tc_t4cl | 72,947 | 84,300 | +11,353 (L1 was +28,292) | |
| **mean nhitOT** | 6.3207 | **6.3006** | **-0.020** (L1 was -0.181) | |
| mean nhitOT matched | 6.3748 | 6.3577 | -0.017 | |
| len pT5-class | 10.9083 | 10.8934 | -0.015 (L1 was -0.262) | |

**jets tune, 500 events, vs ship**: core-all **+.0077** (p 3.9e-13), dR<.005 **+.0155** (p .0088),
dR<.02 **+.0160** (p 4.9e-08), fake **-.0063** (p 1.1e-21), dup **-.0040** (p 5.4e-18),
dup dR<.02 -.0105, dup dR<.005 +.0022 (p .80). **mean nhitOT 8.8695 -> 8.9008 (+0.031, LONGER than
the shipped rule) and jet-core TC length 9.3122 -> 9.4363 (+0.124)**, with 175 more jet-core TCs.

**The only two cells anywhere that are worse than the shipped rule are PU200 `vxy[1,5)` at -.00021
(McNemar p = 1.00, sixteen discordant sims against fifteen -- one track) and PU200 `dup_endcap` at
+.00013.** Everything else on both tune halves improves. Cubes for G10 are running and are the
remaining gate.

Length exchange rate against TRIMOFF, the metric the directive asked for:

| arm | d eff | d nhitOT | **OT hits per 0.001 eff** |
|---|---:|---:|---:|
| L0 = shipped trim | +.00126 | -0.103 | 82 |
| **G10 gap 1.0** | **+.00180** | **-0.123** | **68 (BETTER than the incumbent)** |
| L1 no guards | +.00219 | -0.284 | 130 |
| G20 gap 2.0 | +.00125 | **-0.051** | 41 |

G20 (gap 2.0) is the length-first option: it is **+0.051 LONGER than the shipped rule on PU200**
(6.3721 vs 6.3207, i.e. it gives back HALF of the incumbent trim's unpriced length charge) at
ship-level overall efficiency, and it still carries dxy[1,5) +.0046, dxy[5,10) +.0070 and jets
core +.0054 / fake -.0044 / dup -.0025. If the maintainer weights length above the last .0005 of
PU200 overall efficiency, G20 is the arm to take.

## [TRIMNN 18:15] The full gap ladder on jets, so the coordinator can pick a working point

jets tune, 500 events, everything against L0 = the shipped rule:

| arm | constants | core-all | dR<.005 | dR<.02 | fake | dup | dup dR<.005 | mean nhitOT | jet-core len |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| TRIMOFF | -- | -.0021 | -.0056 | -.0031 | +.0022 | +.0024 | -.0031 | 8.9337 (+.064) | 9.3346 (+.022) |
| **L0 ship** | 4 | -- | -- | -- | -- | -- | -- | 8.8695 | 9.3122 |
| G20 gap 2.0 | 1 | +.0054 | +.0124 | +.0113 | -.0044 | -.0025 | +.0073 | 8.9339 (**+.064**) | 9.4257 (+.114) |
| **G10 gap 1.0** | **1** | **+.0077** | **+.0155** | **+.0160** | **-.0063** | **-.0040** | +.0022 | 8.9008 (**+.031**) | **9.4363 (+.124)** |
| G05 gap 0.5 | 1 | +.0100 | +.0199 | +.0205 | -.0084 | -.0038 | +.0009 | 8.8596 (-.010) | 9.4132 (+.101) |
| L1 gap 0 | **0** | +.0112 | +.0218 | +.0225 | -.0096 | -.0035 | +.0003 | 8.7829 (-.087) | 9.3286 (+.017) |

The ladder is monotone in efficiency and monotone in length, and they run in opposite directions.
Every rung beats the shipped rule on efficiency, fake AND duplicates. The choice is purely how
much outer-tracker track length the maintainer is willing to spend, and on jets even the most
aggressive rung is only 0.087 hits below the incumbent while G10 and G20 are ABOVE it.

## [TRIMNN 18:25] G10 passes both cubes: identical to the shipped rule except three one-track cells

`d3_ref/pu_judge.py`, 5000 events each, `cube50_highPt` ran clean at `-s 4` (no writer segfault).

* **cube50**: every efficiency, fake and duplicate cell **identical to the shipped rule to 4 dp**.
  n_tc 1046 -> 1045, n_tc_t4cl 460 -> 461.
* **cube50_highPt**: identical everywhere except `vxy[10,30)` .0416 -> .0414 (309 -> 308 tracks of
  7,433), `dxy[5,10)` .0649 -> .0646 (216 -> 215 of 3,327) and `dup_barrel` .0243 -> .0242. n_tc
  956 -> 956, n_tc_t4cl 430 -> 436.

Those are the same three one-track moves L1 makes; the displaced gun samples are essentially blind
to this change because their chains are short and 4-layer-dominated, so few have nNodes >= 3.

## [TRIMNN 18:30] VERDICT

**The maintainer's premise is confirmed by deployment, not by an offline proxy.** The chi2 terminal
trim can be deleted and replaced by the FROZEN chain head ranking the same three variants, at the
same network count, with no retraining anywhere, and the result is better physics on every gate
the round could measure.

**What was actually established, in order of how load-bearing it is:**

1. **The chi2 improvement ratio is anti-informative about realness** (AUC .45 / .47, below chance;
   within-chain pairwise .49 / .60). The frozen head's margin is at .98 / .97. Nothing was ever
   trained to compete with the trim, and when something is allowed to, the proxy loses badly.
2. **The DECIDER is what matters, not the aggressiveness.** The identical unguarded argmax decided
   by chi2 (mode 4) is MORE aggressive and loses .0092 jet core efficiency and .0025 PU200 overall
   efficiency, destroying vxy[5,10) by .055. No trivial length rule reproduces the head's choice
   either.
3. **The guards, not the rule, were the binding constraint.** `trimMinLayersAfter = 5` made the
   shipped trim structurally unable to touch 67% (jets) / 41% (PU200) of its own eligible
   population. Deleting guards monotonically improves physics and monotonically shortens tracks.
4. **The trim's Pareto win was never a purity effect**: 95% (jets) / 91% (PU200) of its edits do
   not change any variant's truth label. It is an arbitration effect, and so is the replacement's.

**LEAD CANDIDATE: `G10` = `ChainTrimLearned`, head argmax over mX with a decisiveness gap of 1.0
logit units. ONE constant replaces the shipped rule's four; `chainFitChi2Combined` (103 lines of
double-precision fit) leaves the pipeline entirely; three networks unchanged and byte-identical.**

| gate | result |
|---|---|
| jets tune core-all | **+.0077** (p 3.9e-13); dR<.005 +.0155; dR<.02 +.0160 |
| jets fake / dup | **-.0063 / -.0040**; only adverse jet cell dup dR<.005 +.0022 (p .80) |
| PU200 eff overall | **+.00054** (p .0027); dxy[1,5) **+.0094**, dxy[5,10) **+.0169**, vxy[10,30) **+.0046** |
| PU200 fake / dup | **-.00053 / -.00017** (both better than the shipped rule) |
| PU200 adverse cells | vxy[1,5) -.00021 (**McNemar p = 1.00, one track**) and dup_endcap +.00013 |
| both cubes | clean; three one-track moves, identical elsewhere |
| **track length** | **jets +0.031 LONGER than the shipped rule (+0.124 on jet-core TCs); PU200 -0.020** |
| simplicity | 4 constants -> 1; one numerical method deleted; net ~+13 lines; kernel and network count unchanged |
| cost | +3 `ChainFeatures` builds and +3 head evals per nNodes>=3 chain, -1..3 chi2 fits; **+29% (jets) / +68% (PU200) of one K7a pass**; NOT timed |

**Alternatives on the same one-constant kernel, if the coordinator weights differently:**
* `G20` (gap 2.0) -- **length-first**: PU200 nhitOT 6.3721, i.e. **+0.051 LONGER than the shipped
  rule**, giving back half the incumbent trim's own unpriced 0.103-hit charge, at ship-level PU200
  overall efficiency but still dxy[1,5) +.0046 / dxy[5,10) +.0070 and jets core +.0054 / fake
  -.0044 / dup -.0025.
* `L1` (gap 0, **zero constants**) -- **maximum physics**: jets core +.0112, PU200 dxy[5,10)
  +.0219, dxy[10,30) +.0051; priced at PU200 dup +.00021 and **-0.181 OT hits per TC** (-0.179 per
  matched TC), which on the maintainer's directive is the arm to refuse.

**CAVEATS, riding with the verdict:**
* Everything above is the TUNE halves (jets 0-499, PU200 `event_1000`) plus both cubes. The sealed
  halves are untouched and the coordinator judges there.
* **Nothing was retrained.** Phase 0 showed the frozen head already ranks variants far better than
  the rule, so Phase 2 was never entered -- correctly, and that is why the attach head did not need
  an on-policy pass either (`chains.zFake/zPrompt/zDisp` are unchanged functions of an unchanged
  head; only WHICH chain object they are evaluated on moves, which is exactly what the trim already
  did). If the coordinator wants the attach head re-fitted on-policy against the new trim, that is
  a follow-up round, and its absence is a known gap in this one.
* The T4-class population grows 15% on PU200 under G10 (72,947 -> 84,300), so this change
  re-populates the cell jet round 3 tuned. That is legitimate but it is where a sealed-half
  surprise would come from.
* No timing was taken. The added K7a work is real and biggest on PU200.
* The claim-order key is disturbed on 4% of all chains at gap 0 (measured, [17:20]); the gap-1.0
  working point roughly halves that.

## [TRIMNN 18:35] Every arm quoted against the historical master reference, as the directive asks

The 2026-08-06 master baseline: chain pipeline **6.427**, LST master **6.519**, gap **-0.092**.
Today's shipped head is already at 6.3207 on PU200 `event_1000` (the pipeline has drifted 0.106
shorter since that baseline, before this round touches anything), i.e. a gap to master of -0.198.

| arm | PU200 mean nhitOT | gap to LST master 6.519 | change in that gap vs the shipped rule |
|---|---:|---:|---:|
| TRIMOFF (no trim at all) | 6.4233 | -0.096 | -0.103 (i.e. the trim itself widened it) |
| **L0 = shipped rule** | **6.3207** | **-0.198** | -- |
| **G20 gap 2.0** | **6.3721** | **-0.147** | **NARROWS by 0.051** |
| **G10 gap 1.0** | **6.3006** | **-0.218** | widens by 0.020 |
| L1 gap 0 | 6.1393 | -0.380 | widens by 0.181 |
| L2 keep layer floor | 6.2008 | -0.318 | widens by 0.120 |
| L3 keep chi2 guard | 6.2825 | -0.256 | widens by 0.038 |
| L4 chi2 argmin (control) | -- | -- | (arm rejected on physics) |

On jets the same arms are 8.8695 (ship), **8.9008 (G10, +0.031)**, **8.9339 (G20, +0.064)**,
8.7829 (L1, -0.087).

So the honest one-line price: **G10 costs 0.020 OT hits per PU200 TC and BUYS 0.031 per jets TC;
G20 gives back half of the incumbent trim's own unpriced 0.103-hit charge on PU200 and buys 0.064
on jets.** Only L1 is a genuine length regression, and it is the arm the directive tells us to
refuse.

## [TRIMNN 18:50] The complete PU200 ladder, with length -- G10 remains the pick

PU200 `event_1000`, 1000 events. Every arm is `ChainTrimLearned` with ONE constant (the gap);
L1 is the same kernel with zero constants.

| cell | L0 ship | G20 gap2 | **G10 gap1** | G05 gap0.5 | L1 gap0 |
|---|---:|---:|---:|---:|---:|
| eff_overall | .80911 | .80910 | **.80966** | .80991 | .81004 |
| eff barrel | .92349 | .92380 | .92425 | .92469 | .92493 |
| vxy [1,5) | .79773 | .79752 | **.79752 (-.00021, p 1.00)** | .79666 (**-.00107**) | .79773 |
| vxy [5,10) | .71379 | .71527 | .71626 | .71429 | .71527 |
| vxy [10,30) | .70416 | .70584 | .70871 | .70895 | .70943 |
| dxy [1,5) | .56725 | .57180 | **.57667** | .57602 | .57700 |
| dxy [5,10) | .25819 | .26514 | **.27507** | .27607 | .28004 |
| dxy [10,30) | .05446 | .05573 | .05636 | **.05953** | **.05953** |
| fake overall | .04478 | .04489 (**+.00011**) | **.04425** | .04386 | .04356 |
| **dup overall** | .04008 | .03992 | **.03991** | .04001 | .04029 (**worse**) |
| dup endcap | .06754 | .06754 | .06767 | .06788 | .06831 |
| n_tc_t4cl | 72,947 | 77,074 | 84,300 | 90,650 | 101,239 |
| **mean nhitOT** | **6.3207** | **6.3721 (+.051)** | **6.3006 (-.020)** | 6.2342 (-.086) | 6.1393 (-.181) |
| mean nhitOT matched | 6.3748 | 6.4272 | 6.3577 | 6.2918 | 6.1961 |
| mean nlayers matched | 4.0346 | 4.0561 | 4.0223 | 3.9874 | 3.9335 |

G05 buys a little more efficiency than G10 (+.00080 vs +.00054, and it reaches L1's dxy[10,30)
+.00507) but pays 4x the length (-.086 vs -.020) and turns the vxy[1,5) cell from noise
(-.00021) into a real 5-track loss (-.00107). **G10 is the working point where every PU200
efficiency cell, every fake cell and every duplicate aggregate is at least as good as the shipped
rule, the only exceptions being one track in vxy[1,5) and +.00013 in dup_endcap.**
