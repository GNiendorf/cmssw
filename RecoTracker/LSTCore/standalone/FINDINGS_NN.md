# FINDINGS_NN.md -- THE NETWORK-REDUCTION WORKSTREAM. SHARED INSIGHTS FILE.

**APPEND YOUR FINDINGS HERE. READ IT BEFORE YOU START AND AGAIN BEFORE EVERY NEW ARM.**
Post format: `[B<n> HH:MM] <one-line claim> -- <the number that proves it> -- <artifact path>`.
Post NEGATIVE results too, and post RETRACTIONS with the same prefix. Three agents (B1/B2/B3) are
working the SAME problem from different angles on purpose. Read each other's posts; if someone has
already falsified your idea, do not re-derive it.

--------------------------------------------------------------------------------------------------

# THE MISSION: FEWER NETWORKS, PHYSICS EQUAL OR BETTER

The chain algorithm currently carries **FOUR** neural networks. The maintainer wants that count
**reduced, ideally to ONE**, with physics **equal or better** -- not "acceptable for the
simplification", equal or better on the deployed gates. Simplicity is a first-class goal here
(scope rule 4: the algorithm must stay simpler than LST's), but it is NOT permitted to cost physics.

    1. Edge head       EdgeNetworkWeights.h     40 -> 1   weld eligibility (thetaEdge / E1 / E2)
    2. 3-class gate    Chain3NetworkWeights.h   25 -> 3   chain kills; bars on marginP/D/X
    3. Attach head     AttachNetworkWeights.h   20 -> 1   (pLS, chain) delivery; attachTheta/T/E
    4. 2-class chain   Chain2NetworkWeights.h   25 -> 1   ** ORDERED DELETED **

**Network 4 IS BEING DELETED. That decision is final and not open for re-litigation.** Its output
`gateLogit2` has exactly one consumer -- attach pair input 11 -- and is not cut on, not a kill, not
in the `-BK 1` order key, and no `ChainConfig` bar sits on its scale. It reads the SAME 25-float
chain row as the 3-class gate that runs immediately before it, so it re-encodes information the
gate already has.

**A FITTED SURROGATE IS FORBIDDEN, PERMANENTLY.** A previous attempt replaced the network with a
28-term least-squares fit in a new generated weights header and presented it as a deletion. It was
rejected: a fitted stand-in with its own coefficient file is still a learned component. Count
learned components and weight files, not lines. Distrust any diff that ADDS a generated weights
file.

**KEEPING A NETWORK IS NOT AN ACCEPTABLE OUTCOME EITHER.** If an arm cannot reach neutrality,
report exactly which row resists and by how much, with the full tuning history -- but the levers
below must be exhausted first.

--------------------------------------------------------------------------------------------------

## WHAT A4 ALREADY TRIED AND WHERE IT FAILED. DO NOT REPEAT ANY OF THIS.

Read `a4_ref/A4_README.md` in full before you plan anything. Summary of its state:

**IMPLEMENTED AND WORKING (start from this, do not rebuild it):**
`a4_ref/a4_delete_chain2mlp_3logits_WIP.patch` -- `Chain2NetworkWeights.h` deleted (453 lines), its
evaluation block and a dead SoA column removed, attach head retrained to **22 inputs** with the
three raw gate logits at slots 11/12/13. `git apply --check` clean on `4f1846078d9`, builds with 0
`error:`. **NOT physics-neutral, so not shippable as it stands.**

**THE FOUR DEPLOYED ARMS, ALL NON-NEUTRAL** (PU200 tune/holdout; both cube samples clean or
bit-identical for every arm, so **PU200 is the binding gate for an attach-head change**, not
cube50_highPt):

    arm                          eff_ovl   eff_bar   eff_trn   dup_bar   vxy[1,5)  conv_T5
    shipped                        --        --        --        --        --       .6871
    MX  (1 input = mX)           -.00227   -.00291   -.00366   +.00782   -.00599   .6082
    MX3 (3 logits, rate-matched) -.00139   -.00027   -.00314   +.01103   -.00192   .6098
    MX3 + conversion-matched     -.00137   +.00048   -.00299   +.01586   -.00342   .6899
    MX3 + per-band + xc offset   -.00154   -.00071   -.00560   +.00035   -.01248   .7410

**THE TWO ROWS THAT RESIST: `dup_barrel` and `eff_transition`.** Every calibration that fixes one
breaks the other -- the delivery bars and the crossclean offset trade against each other and no
setting A4 found holds both. `eff_barrel`, `fake_barrel` and `conv_T5` all reach neutral or better.
`eff_overall` is down in ALL FOUR arms, which is the metric weighted highest.

**THE ROOT CAUSE IS THE HEAD, NOT THE THRESHOLDS.** Under fixed per-bin signal efficiency (the only
calibration that removes threshold confounds): fake-weighted FPR **shipped .000387 vs retrained
.000441, ratio 1.141**. At equal signal efficiency the retrained head admits **14% more
background**. No further bar-fiddling fixes that.

**BUT THE HEAD IS BETTER ON DISPLACED**: the T3C head (lr 3e-3, epoch 70) beats shipped on
displaced vxy[1,5) by +.0032 and on prompt. Expected, because the input being deleted --
`gateLogit2` -- scores **.336 on disp-vs-prompt, i.e. INVERTED, worse than random**. The shipped
attach head is currently fed a signal anti-correlated with displacement. So a GAIN is mechanically
available; A4 just could not get it without paying background.

**CALIBRATION: THREE QUANTITIES, NOT INTERCHANGEABLE.** This is A4's most reusable finding.
 1. **pair acceptance rate** -- signal+background mixed, per-pair not per-target. WRONG.
 2. **per-target conversion rate** `conv_T5 = n_pT5/(n_pT5+n_T5)` (.6871 shipped) -- pins the NUMBER
    of attachments but lets their COMPOSITION drift. Ignoring it cost 7.8 points of conversion and
    explained every early gate failure; matching it DOUBLED `dup_barrel`. WRONG.
 3. **per-bin signal efficiency** -- the only one that isolates head quality. USE THIS.

**A4's OWN UNTRIED LEVERS, ITS PRIORITY ORDER (the first is the most likely fix and it never
reached it):**
 1. **WIDEN THE FIRST HIDDEN LAYER.** It is still **24 -- the width chosen for 20 inputs** -- while
    the head now takes 22. A uniformly worse FPR at matched signal efficiency is exactly what a
    capacity-limited head looks like. One constant plus a retrain.
 2. **log-softmax inputs** instead of raw logits (identical information, arbitrary offset removed,
    no saturation). Softmax PROBABILITIES are discouraged: they saturate, and the gate's decisions
    live on the logit-margin scale where probabilities are pinned at 0/1.
 3. **JOINT calibration** of the delivery bars WITH the crossclean offset (4 parameters), rather
    than solving them sequentially. Those are the two that trade, which is why sequential failed.
 4. **MORE DATA: 52.4M of 114.5M available pairs were used.**

--------------------------------------------------------------------------------------------------

## THE CALIBRATION PROTOCOL (maintainer's standard; apply it to every head you touch)

**Set every working point at a FIXED SIGNAL EFFICIENCY, per bin, on LST's binning.** Copy the
structure from `interface/alpaka/Common.h`: `kPtBins` = 2 (split at pT 5) x `kEtaBins` = 10
(eta / kEtaSize, last bin absorbing |eta| > 2.5) -- i.e. a **2x10 working-point table per
decision**, same bin edges and indexing as `kWp_prompt` / `kWp_displaced`.

Why: signal efficiency is pinned by construction, so **only the false-positive rate varies with the
head's quality.** A better head shows lower fake/dup at the same efficiency; a worse one higher.
Pair acceptance and conversion are MIXED quantities and holding them fixed does not hold physics
fixed -- proved above.

Calibrate offline on the labelled dumps; the shipped artefact is a constant table, exactly as
LST's WP tables are. **This is a legitimate use of truth** -- scope rule 3 forbids truth at RECO
time, not truth in fitting a threshold.

**Define "signal" per head and write it down before fitting:** a true edge = two T3s from the same
sim track; a true chain = a chain whose members share one sim track; a true attach pair = a pLS and
chain from the same sim track.

**THE ONE WAY TO QUIETLY WRECK US:** for the 3-class gate, a single lumped signal-efficiency target
will destroy the displaced advantage, because the shipped bars are deliberately PERMISSIVE for
displaced chains and that permissiveness is this project's lead over LST master. Hold
**prompt-class and displaced-class signal efficiency as TWO SEPARATE TARGETS** -- that is what
`zPrompt` and `zDisp` exist for. If the tabulated form cannot express what the shipped far cell
provides, say so with numbers rather than shipping a regression.

--------------------------------------------------------------------------------------------------

## ACCEPTANCE BAR

Physics **equal or better**, measured deployed (not on a test split), on:
 * PU200RelVal `event_1000.root` (tune) AND `event_2000.root` (holdout) -- eff/dup/fake in every
   cell and all six displaced bands.
 * The **pooled 6000-event holdout** (`event_2000..7000`) with A5's paired McNemar scoring
   (`a5_ref/paired.py` -- and note the writer emits events in STREAM-COMPLETION order, so naive
   entry-wise pairing gives nonsense; that script fixes it).
 * **Both cube samples.**
 * **conversion rate `conv_T5` reported for every deployed arm** (.6871 shipped) -- as an OUTPUT,
   never as a calibration target.
 * Ship-verification: patch applied to a tree reset to `4f1846078d9`, clean build with 0 `error:`
   in the FRESH `.make.log.<timestamp>`, output bit-identical to the measured arm on all 35 judge
   fields at full float precision.
 * Timing per scope rule 6 if component count or volume changes: same-binary config-only A/B
   (TRAP 6 -- a CPU total is not comparable between two binaries under ~45 ms; check the `pLS`
   column first).

Do NOT round a small regression up to "unchanged". State the number and say it is not at the bar.

--------------------------------------------------------------------------------------------------

## REUSE, DO NOT REBUILD (all in `standalone/a4_ref/`)

    a4_delete_chain2mlp_3logits_WIP.patch   the working deletion + 22-input attach head
    a4_instrument_and_arms.patch            env instrument: all 8 attach bars + attachDelta, inert
    cache_mx34.npz / cache_z39.npz          8.2 GB / 9.3 GB training caches (52.4M pairs)
    calib_fixed_eff.py                      fixed-signal-efficiency calibration
    conv.py                                 conversion rate / TC class composition
    attach_mlp_weights_{T3C,A4MX,Z3MX3}.h   the trained heads already produced
    r1_train.py, build_cache34.py           training + cache build
    A4_README.md                            READ THIS FIRST

Also: `a5_ref/paired.py` (paired scoring, 6000-event reference runs in `a5_ref/big/`),
`a2_ref/tcsurgery.py` (exact offline counterfactual for row-deletion rules),
`a6_ref/a6_candidate_C1.patch` and `a5_ref/candidate_r5far8.patch` (round-5 physics candidates --
your work must not conflict with them; if you need to measure on top of one, say so).

**Env-instrument trap:** the standalone driver constructs `LSTEvent` DIRECTLY and never calls
`LST::run`, so a hook there is silently invisible. The hook belongs in the constructor, and you must
confirm the override PRINTED (`grep -c "\[env\]" <arm>.log`).
**Build trap:** `lst_make_tracklooper` prints "compilation successful" even when a TU fails. Grep
the FRESH `.make.log.<timestamp>` for **`error:` with the colon** -- a bare `error` grep matches
every `-Werror=` flag.
**cube50_highPt segfaults** at `-s 8`/`-s 16`/`-s 32` in the writer; use `-s 4`. Identical for
baseline and arm -- it is not your patch.
**Artifacts in `standalone/b<n>_ref/`. NEVER write to /tmp.**

--------------------------------------------------------------------------------------------------

# LOG BELOW THIS LINE

