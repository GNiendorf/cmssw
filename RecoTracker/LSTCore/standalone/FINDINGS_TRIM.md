# THE TERMINAL TRIM, PRICED (coordinator, 2026-08-13)

Baseline `ce81e988ec1`. ONE binary in `gpu_wt/r2` (`bin/lst_cpu` md5 `06f1c1a1adc89b9e05b2723de303da53`),
arms env-toggled by a probe hook (`trim_ref/trim_env_probe.patch`, `LST_TRIM_ON` / `LST_TRIM_ABS`,
inert with nothing set). Build clean: 0 `error:`, 0 `failed to compile`.

## HEADLINE: `ChainTrimTerminals` is a STRICT PARETO WIN on every measured cell of both samples

**jets, 500 TUNE events (`p4_ref/jetgate.py --split tune`, paired McNemar)**

| cell | trim ON | trim OFF | the trim is worth |
|---|---:|---:|---:|
| core-all | .8120 | .8099 | **+.0021** (119 lost / 72 gained, p 8.3e-04) |
| dR<.005 | .5028 | .4972 | +.0056 (p .12) |
| dR<.02 | .6128 | .6097 | +.0031 (p .060) |
| dR>.05 | .9041 | .9021 | +.0020 (p .0041) |
| fake | .1237 | .1259 | **-.0022** (1.8% rel) |
| dup | .0249 | .0273 | **-.0024** (8.8% rel) |
| TCs/evt | 125.1 | 125.3 | -0.2 |

**PU200 `event_1000`, 1000 events**

| cell | trim ON | trim OFF | the trim is worth |
|---|---:|---:|---:|
| eff_overall | .80911 | .80785 | **+.00126** |
| barrel / transition / endcap | | | +.00103 / +.00247 / +.00097 |
| vxy[1,5) / [5,10) / [10,30) | | | **+.00299 / +.00246 / +.00239** |
| dxy[0,1) / [1,5) / [5,10) | | | +.00144 / **+.00292** / +.00099 |
| dxy[10,30) | .05446 | .05446 | +.00000 |
| fake_overall | .04478 | .04597 | **-.00119** |
| dup overall / bar / tr / end | | | -.00043 / -.00099 / -.00017 / -.00021 |
| n_tc | 1,583,156 | 1,583,935 | -779 |

**Every efficiency cell up, every fake and duplicate cell down, on both samples.** The only cells
that do not improve are PU200 `dxy[10,30)` (exactly zero) and the jets deep-core duplicate cell
dR<.005 (.0985 on vs .0954 off, i.e. the trim costs +.0031 there). Nothing else in this project
measures as a clean Pareto move.

**For scale: +.00126 PU200 overall efficiency is MORE than jet round 4's entire ship candidate
`UN4` (+.00081), and `UN4` pays for its gain in duplicates while the trim REDUCES them.**

## MECHANISM
TC mix, jets: T5 core wins 6,927 (on) vs 6,768 (off); bare-pLS core wins 1,807 (on) vs 1,906 (off).
Trimming a parasitic terminal does not merely shorten a chain -- it RESCUES chains that would
otherwise fail the gate or lose the claim, and they take core tracks a bare pixel seed would
otherwise win. Removing a node makes the chain MORE likely to be delivered.

## WHAT THE RULE ACTUALLY IS (`ChainWeld.h` K6f, ~100 lines, 4 constants)
Four independent guards, all conservative: `nNodes >= 3`; combined-fit chi2 > `trimAbsChi2` (1.0,
the "concentrating guard" -- a chain that already fits well is untouched); the surviving variant
must keep >= `trimMinLayersAfter` (5) layers, which is STRICTER than the emission floor of 4; and
the fit must improve by `trimFactor` (1.2), a ratio not an absolute. Larger improvement wins,
inner drops win exact ties. Applying is O(1): inner drop is `nodeOffset += 1`, outer drop is
`nNodes -= 1`, both inside the chain's original CSR allocation. The chain's `score` is then
RECOMPUTED (`edgeSum + lambdaLen * nLayersAfter`), so a trimmed chain faces the gate AND the claim
order key with a worse score. `trimPasses = 1`: a chain with two parasitic ends keeps one.

## THE MAINTAINER'S OBJECTION, WHICH STANDS
The rule is a HAND-SET THRESHOLD ON A SINGLE PROXY. Helix chi2 is not the objective; "does this
chain match a real sim track at >= 75%" is. A real track can fit badly (scattering, a kink, a
mis-measured hit) and a fake can fit well (collinear hits in a jet core). Computing a proxy
exactly is not answering the question exactly, and there is no principled reason a hand-set
threshold on one proxy should beat a learned function of 25 features judged on realness.

**The reason it wins today is that nothing was ever trained to compete with it.** The chain head
has never seen a variant, has never been trained to rank one, and does not receive the
counterfactual (the improvement ratio is computed and THROWN AWAY -- only `trimAction` is stored).
The comparison is not "physics beats NN", it is "physics beats nothing".

## WHAT SURVIVES OF THE ARCHITECTURAL DEFENCE
Only one thing, and it is structural rather than epistemic: **the trim EDITS, the gate CLASSIFIES.**
No arrangement of output classes changes which MDs a chain owns. Whatever decides this, the
variants must be CONSTRUCTED first -- that half is mechanical and happens either way. The decision
half is a realness question and should be learned.
