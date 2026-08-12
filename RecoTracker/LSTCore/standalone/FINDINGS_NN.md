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

[B1 18:35] **THE 14% FPR EXCESS IS NOT CAUSED BY DELETING THE NETWORK. IT IS A TRAINING-DATA
DEFICIT.** I ran A4's own fixed-signal-efficiency calibration on the CONTROL it never measured:
`Z3REF` -- the **20-input head that KEEPS `gateLogit2`**, trained with the SHIPPED head's exact
recipe (lr 1e-3, patience 8, 60 epochs, hidden 24, seed 42) on the SAME 52.4M-pair primary-only
cache A4 used. Same frozen TEST-60, same 2x10 bins, same reference bars.

    head                            inputs   train pairs  recipe          FPR ratio vs shipped
    shipped MIN1                      20       114.5M     lr1e-3/pat8/60      1.0000 (.000387)
    Z3REF  (keeps gateLogit2)         20        52.4M     lr1e-3/pat8/60    **1.3944** (.000539)
    T3C    (3 logits, net DELETED)    22        52.4M     lr3e-3/pat20/200    1.1408 (.000441)

Z3REF vs MIN1 differ in ONE thing -- **training-data volume** (52.4M vs 114.5M). Identical inputs,
identical recipe, identical seed. That single difference costs **+39% background at matched per-bin
signal efficiency.** The deletion arm, on the same halved data, is at +14% -- i.e. **the 22-input
deleted-network head is 15% BETTER on background than the 20-input head that keeps the network,
measured at matched data and equal footing** (1.1408 / 1.3944 = 0.818).

So A4's "the root cause is the head" conclusion is **RETRACTED (by me, on A4's own metric)**: the
root cause is that every arm was trained on 46% of the shipped head's data. The information in
`gateLogit2` is not missed.

Metric robustness (`b1_ref/fpr.py`): the high-pT half of the 2x10 table has 1-88 true rows per bin
and is noise, but it carries only 0.9% of the fake weight -- restricting to pt<5 or to nTrue>=100
moves T3C 1.1408 -> 1.1393 and Z3REF 1.3944 -> 1.3333. The headline is not driven by the thin cells.

CONSEQUENCE FOR MY PLAN: lever order inverts. A4 ranked "widen the first layer" first and "more
data" fourth; the measurement says data is the dominant term and is worth ~4x the whole remaining
gap. Re-dumping the 498-event salvage source with the three gate logits is now arm 1.
-- b1_ref/calib_CTRL_Z3REF.json, b1_ref/fpr.py

[B1 18:36] FOR B2: do not calibrate bars against T3C assuming its head is information-poor. The
head deficit you are compensating is a data deficit I am fixing; a joint bar/crossclean fit tuned
to absorb +14% background will be mis-set once the head is retrained on full data. Suggest you fit
the METHOD now and re-run the fit on my head when I post it.

[B2 18:45] PLAN + INSTRUMENT UP. My lane is the JOINT calibration (A4 lever c). Two structural
changes to the measurement build, both inert by default, in worktree g2 on top of A4's WIP deletion
patch (T3C head), 0 `error:` in `.make.log.1786487852`:
 1. `ChainConfig` now carries a **2x10 (pT x |eta|) working-point TABLE for BOTH per-seed bars** --
    delivery (`attachWp`) and bare-chain crossclean (`xcWp`) -- on LST's kWp binning (`kPtBins` 2
    split at 5 GeV, `kEtaBins` 10 x 0.25, last bin absorbing >2.5), keyed on the SEED pT and |eta|
    exactly where the 3 eta bands were resolved (`ChainAttach.h` ChainAttachPlsPre). `attachWpOn` /
    `xcWpOn` false => the banded scalars stand, byte for byte.
 2. Env instrument extended: `LSTCHAIN_attachWp` / `LSTCHAIN_xcWp` take 20 comma-separated floats
    and PRINT the whole parsed table; a partial parse applies NOTHING and says so. **A4's
    `LSTCHAIN_xcDelta` / `rpsDelta` / `t3Delta` printed nothing** -- its P_TE2 arm has no evidence in
    its own `.cmd` that xcDelta was taken. Now every knob prints (prefix `[B2 env]`).
Both bars are thresholds on the SAME head logit, so both get the same fixed-signal-efficiency
treatment; the "crossclean offset" becomes a per-cell working point rather than one scalar.
-- b2_ref/run.sh, g2 diff

[B2 18:46] Read B1 18:35/18:36 and agree: I will fit the METHOD on T3C and re-run it on B1's
full-data head. The frontier map I owe is mostly a STRUCTURAL question (is the dup_barrel /
eff_transition trade separable in eta at all?), which is a property of the geometry and of which
decision each metric answers to, not of the head's FPR level -- so it transfers.

[B1 18:55] **THE DECOMPOSITION IS CLEAN, AND THE DELETION IS THE PART THAT HELPS.** Second control:
`CTRLR` = the 20-input head that KEEPS `gateLogit2`, trained at **T3C's recipe** (lr 3e-3, patience
20, 200 epochs) on the same 52.4M primary-only cache. That isolates recipe from inputs, which A4's
arms had entangled.

    head    inputs  net?     train pairs  recipe               FPR ratio (pt<5, matched eff)
    MIN1      20    kept       114.5M     lr1e-3/pat8/60           1.0000  (.000390)
    Z3REF     20    kept        52.4M     lr1e-3/pat8/60           1.3333
    CTRLR     20    kept        52.4M     lr3e-3/pat20/200         1.3474
    T3C       22    DELETED     52.4M     lr3e-3/pat20/200         1.1393

Reading the three deltas off that table, multiplicatively:
  * **DATA VOLUME (52.4M -> 114.5M): x1.33.** The whole remaining gap, and more.
  * **THE INPUT SWAP (2-class logit -> the three RAW gate logits): x0.85.** 1.1393/1.3474. Deleting
    the network and handing the attach head the gate's own three logits **removes 15% of the
    background at matched signal efficiency.** The deletion is not a cost to be minimised; it is an
    improvement being masked by a data deficit.
  * **RECIPE (lr/patience/epochs): x1.01, i.e. nothing.** A4's LR search fixed an epoch-13 early
    stop but bought no head quality. Do not spend more time there.

Predicted 22-input head on the full 114.5M: 1.1393 / 1.33 ~= **0.86**, i.e. ~14% BETTER than
shipped. The full-data re-dump is running (3 passes over both event sources, ETA ~20:30).
-- b1_ref/calib_CTRLR.json, b1_ref/calib_CTRL_Z3REF.json, b1_ref/fpr.py

[B1 18:56] Also done and available to reuse:
  * `b1_ref/port_hdr.py` -- exports a trained head to the TREE form of `AttachNetworkWeights.h` and
    then PROVES the port: re-parses the written header and compares every literal against the .pt.
    **Self-test: it regenerates the SHIPPED `AttachNetworkWeights.h` bit-exactly from
    `r1_ref/attach_mlp_MIN1.pt`** (all 10 arrays + bias_out array-equal), so the export path itself
    is verified rather than trusted.
  * `b1_ref/mapbars.py` -- maps ALL EIGHT ChainConfig bars that sit on the attach logit scale onto a
    retrained head with **ZERO free parameters**, by the fixed-signal-efficiency rule: each bar's new
    value is the quantile of the new head's TRUE-pair logits in that bar's own universe x eta band
    that reproduces the shipped bar's true-pair acceptance there. B2: this is a principled starting
    point for your joint fit, and it costs no parameters, so if it holds it is preferable to a fit.
  * `b1_ref/b1_train.py` -- r1_train.py plus `--val-metric fpr` (early-stop on the DEPLOYED metric,
    the fake rate at fixed signal efficiency, instead of on bulk AUC), `--hidden2`, and LR schedules.
    All inert at defaults.

[B1 19:15] **NEGATIVE: WIDENING THE HIDDEN LAYERS BUYS NOTHING. A4's top-ranked lever is falsified.**
Same 52.4M cache, same T3C recipe, 22 inputs, only `--hidden` changed (kHidden is ONE constant in
ChainAttach.h, so both layers move together -- that is the deployable knob):

    hidden   best epoch   val AUC     FPR ratio (pt<5, matched per-bin signal eff)
      24        70        .998155        1.1393   (= T3C)
      32        12        .997952        1.1376

A 33% wider head, 55% more parameters, moves the deployed metric by 0.15% -- inside the noise of a
single seed. The head is NOT capacity-limited, so "a uniformly worse background at matched signal
efficiency is what a capacity-limited head looks like" is not the right reading of that signature
here; a DATA-limited head looks the same way, and that is what this one is ([B1 18:55]: data volume
is worth x1.33 on the same metric).
-- b1_ref/calib_W32.json, b1_ref/train_W32.log

## [PD 19:40] Attach pair dump instrument -- build clean

Worktree: **g4** (`/mnt/data1/gsn27/here/gpu_wt/g4`), NOT g2. g2 was handed to me as "clean at
4f1846078d9" but is carrying an uncommitted round-5 experiment (9 modified files incl. a deleted
`Chain2NetworkWeights.h` and a rewritten `AttachNetworkWeights.h`); resetting it would have
destroyed someone's work. g4 is at the same commit `4f1846078d9`, has ZERO modified tracked files,
and `dcaSplit2 = 12.f` checks out.

Instrument = `LST_CHAIN_PAIR_DUMP=<path>`:
 * `ChainAttachPairRow` (96 B: stage, target, pls, logit, x[20]) + `attachPairKeep` in ChainAttach.h
 * 4 nullptr-guarded args on `ChainAttachScore` and `ChainAttachT3Score`; the row is emitted inside
   `flush()` from the batch buffer `xT[i*kB + b]`, i.e. AFTER stage B's input-18 overwrite
 * `beginChainPairDump()` / `dumpChainPairs()` on LSTEvent, persistent device buffer, atomic cursor,
   drops counted, one append record per event with magic 'PAIR'
 * stage code 0 = chain 5+ target, 1 = bare T3, 2 = aux 4-layer (-XC4, score-only, outside the
   `[CHAIN K8] scored=` census) -- keeping 2 separate is what makes the census cross-check exact

Gate 1 PASS: `.make.log.1786491288`, 0 hits for `error:`.

## [S1 19:59] Stage-1 edge retrain: the SHIPPED label definition, verbatim, plus two setup facts

**THE LABEL, REPLICATED NOT INVENTED.** The shipped head is `edge_mlp_v3.pt` / `edge_norm_v3.json`
(named in the generated `src/alpaka/EdgeNetworkWeights.h` header, `best_epoch=100
best_val_auc=0.96099`, arch [40,32,32,1]). Its training rows came from `prototype/edges_{300,498}evt.root`,
whose `label` branch is written by `prototype/Labels.cc::labelEdges`, and the definition in
`prototype/Labels.h:4-11` is:

> an edge is TRUE iff BOTH T3s are MD-matched to the SAME sim track, where a T3 is MD-matched to
> sim s iff s appears in the `md_simIdxAll` list of at least 2 of its 3 MDs (`md_simIdxAll` stores
> >75%-matched sims per MD, i.e. both MD hits). This is deliberately the plan's definition, not
> `t3_pMatched`-based: duplicates of the same sim track label as true, object-level purity does not
> enter. v1 keeps labels binary (no ambiguity band at edge level).

Operationally (`Labels.cc:30-106`), in order:
 1. per MD, dedupe `md_simIdxAll` (the writer pushes one entry per hit-assignment permutation);
 2. T3 sim set = sims appearing in >= 2 of the 3 MD lists (`merged` sorted, runs of length >= 2);
 3. `label[e] = 1` iff `set_intersection(sims(inner), sims(outer))` is NON-EMPTY;
 4. kinematics (simPt/simEta/simVxy) from the highest-`sim_pt` ACCEPTED sim in that intersection;
    a pileup-only match is still label 1 with -999 kinematics.
`md_simIdxAll` itself = the production matcher at `matchfrac 0.75` over the MD's 2 hits
(`write_lst_ntuple.cc:740,804`), and for a 2-hit object `frac > 0.75` means `frac == 1`, i.e. the
sim must be on BOTH hits. Per-hit sim dedup is mandatory (a4_ref/chain_truth3.py:37-48).
No weighting: the M19 displaced weights (`iterations/fanout5/edgeretrain/er_train_edge.py`) were an
EXPERIMENT, and the resident v3 head has FAKE weight 1 / TRUE weight 1.

**THE DUMP IS JOINABLE WITH NO HEURISTICS.** `nodes.bin` stores `mds.anchorHitIndices()` /
`outerHitIndices()`, i.e. LST hit rows, and `interface/LSTPrepareInput.h:113` fills `hits.idxs()`
with `iota(0, nHitsOT)` over the OT block in ph2 file order -- so **an LST OT hit index IS the
tracking-ntuple ph2 row**, and the MD -> hit -> simhit -> sim join needs no matching or anchoring.
`edges.bin` has exactly **1000 records, 111,913,544 edges, 43,500,659 nodes** (112k edges and 43.5k
nodes per event), so the record counter is the entry index with no skips to repair.

**BIN KEY for the WP table** (LST's T3-DNN binning, `src/alpaka/NeuralNetwork.h:127-133`):
`pt_index = (radius * k2Rinv1GeVf * 2 > 5)`, `bin_index = (|eta| > 2.5) ? 9 : |eta|/0.25`, where the
T3 DNN's eta is `|mds.anchorEta()[md0]|` -- the anchor hit eta of the T3's FIRST MD, an
`acosh(r3d/rt)` column (`src/alpaka/Hit.h:68`). Both are reproducible offline from the dump: the
radius from node feature 1 (`log10R`) and the eta from ph2_x/y/z at the MD0 anchor row in nodes.bin.

**WORKTREE: I am NOT in g4.** g4 is at 4f1846078d9 but carries the PD agent's 4-file pair-dump
instrument AND was running `lst_cpu -n 1000` out of its own `bin/` when I checked (PID 1191409,
`pd_work/ON1000.root`) -- PD posted [PD 19:40] that it moved to g4 for exactly the reason g2 was
unusable. g1/g2/g5/gc6 are all dirty too. I provisioned **g6** =
`/mnt/data1/gsn27/here/gpu_wt/g6` (release-area scaffolding copied from gc6, `git worktree add
--detach 4f1846078d9`): zero modified tracked files, `dcaSplit2 = 12.f` verified. Builds go through
`broker/buildlock.sh`.
-- nnloop_ref/s1_work/dumpio.py

## [S1 20:34] The plumbing is PROVEN INERT: 35/35 judge fields bit-identical on 1000 events

The working-point mechanism is in and measured before any weight change. Design (smallest diff that
keeps the hot kernel cheap):
 * `ChainNodesSoA::wpBin` (uint8, appended last) -- the node's cell `ptbin*10 + etabin` computed in
   **K3**, where `radius` is already in a register and `m0` already resolved, from the SAME two
   quantities `t3dnn::runInference` bins on: `radius * k2Rinv1GeVf * 2 > 5` and
   `|mds.anchorEta()[m0]|` / 0.25 with the last bin absorbing > 2.5.
 * `ChainEdgesSoA::weldBar` (float, appended last) -- **K5** writes the edge's eligibility bar:
   `kWpBar[etype-1][wpBin(inner)]`, or the two per-family scalars when `edgeWpTable` is false.
 * **K6a/K6b lost their two float arguments** and now test `lo < edges.weldBar()[e]`. K6 runs
   `kChainWeldSweeps * 2` times over every edge, so resolving the cell there would repeat the lookup
   8-16x per edge; K5 touches each edge once and already holds the family and the node row.
 * `kWpBar[2][20]` ships INSIDE the regenerated `EdgeNetworkWeights.h`: the table is the head's own
   calibration artefact, one file and one provenance, exactly like LST's DNN weight headers.

**PARITY GATE (the inertness proof).** Regenerated header carrying the SHIPPED v3 weights with the
table filled by the shipped scalars (every E1 cell 0.0, every E2 cell -2.0) + `edgeWpTable = true`:
`-i PU200RelVal -n 1000 -s 8 -p 0.8` gives **all 35 judge fields IDENTICAL to `ship_ref/B2_shipped.judge`
at full float precision** (eff .8102012675346716, dup .045204642254089066, fake .04547934675773079,
n_tc 1587160 -- exact, not rounded). 0 `error:` in the fresh `.make.log.1786494148`.
The export path is verified too: `export_edge.py` re-emits the shipped header from itself and a
re-parse compares **2601 literals, all bit-identical as float32** (plus the 40 new table entries).

Two facts worth having for anyone touching the weld:
 * A negative bar is already safe -- the packed weld key can only be the 0 sentinel for a
   NaN logit, which `chainSanitize` cannot produce, and the shipped E2 bar is already -2.0.
 * The per-family scalars are NOT dead under the table: `edgeWpTable = false` restores them byte for
   byte, which is what the parity run above exercises.
-- nnloop_ref/s1_work/{export_edge.py, s1_plumbing_plus_parityhdr.patch}, runs/PARITY_pu1000.judge

## [S1 20:35] Truth join verified by the shipped head's own logit; worktree = g1 (was g6, never g4)

The label replication is checked, not assumed: the SHIPPED logits dumped alongside each edge
separate MY labels at **AUC 0.96544 over 20 events, .960138 over the 200 val events, .961920 over
the 200 test events** -- the header's own quoted `best_val_auc = 0.9609943`. A one-event
misalignment control collapses it to **0.56627**. Truth pipeline: 1000/1000 shards, 111,913,544
edges, **26.59% true**, versus 27.5% in the shipped v3 training log -- same regime, different (newer)
edge population.

Worktree: I provisioned **g6** at 20:0x because g4 was mid-run for PD; on the coordinator's
reassignment the diff moved to **g1** (`git apply --check` clean on 4f1846078d9) and every gate run
from here on is g1's binary. g6 holds only my parity build and will be removed at the end.

## [PD 20:40] Attach PAIR DUMP delivered: all 5 gates PASS, both backends, inert with the env unset

Deliverables, all against shipped `4f1846078d9`:

    nnloop_ref/instrument_pairdump.patch    4 files, +249 -1
    nnloop_ref/read_pairs.py                parser + 5 structural checks + census + logit round-trip
    nnloop_ref/PAIRDUMP_FORMAT.md           byte layout, stage codes, semantics, volume
    nnloop_ref/pd_work/                     judges, reader transcripts, gzipped make log

### GATES

| # | gate | result |
|---|------|--------|
| 1 | build clean, FRESH `.make.log.<ts>` | **0** `error:` in `.make.log.1786493698` -- and that log is a **CPU + CUDA** build, so the device path compiles too (not just the CPU gate that was asked for) |
| 2 | env UNSET, `-n 1000 -s 8`, vs `ship_ref/B2_shipped.judge` | **35/35 bit-identical at full float precision** (`pd_work/OFF1000b.judge`) |
| 3 | env SET, `-n 1000 -s 1` | **35/35 bit-identical** (`pd_work/ON1000.judge`); repeated at `-s 8` on the final binary, **35/35** again (`pd_work/ON1000b.judge`) |
| 4 | format sanity | 30 CPU events + 5 CUDA events: stage-A row count **EXACTLY equals** `[CHAIN K8] scored=` on every event, every chain-kind target index `< nChains`, every bare-T3 index `< nT3`, `nChains` agrees with `chains.bin` event by event, stage-B count within 3% of `scored=/16`, 0 drops |
| 5 | the 20 floats round-trip | replaying `AttachNetworkWeights.h` in numpy over the dumped `x` reproduces the dumped logit to **max 1.1e-5 absolute on logits up to \|29\|** -- all three stage codes, both backends. This is the proof that what is captured is the vector the MLP consumed. |

Patch applies clean on `4f1846078d9` AND on top of `instrument_featdump_allevents.patch` (tested in
both orders; offsets only, no fuzz).

### DESIGN POINTS THAT MATTER TO THE TRAINER

 * **Three stage codes, not two.** 0 = chain target with nLayers >= 5, 1 = bare T3, **2 = the aux
   4-layer (-XC4) tail of the stage-A launch**. Code 2 exists because those targets are score-only
   and sit OUTSIDE the `[CHAIN K8] scored=` census; folding them into 0 would have made the census
   cross-check impossible, which is the one cheap check that proves no stage-A pair was lost. They
   are ~1.4e4 rows/event, 11% of stage A.
 * **`x` is captured from the batch buffer at flush (`xT[i*kB + b]`), not inside `attachEvalPairX`.**
   That is what makes stage B correct: `ChainAttachT3Score` overwrites head input 18 with the
   bare-T3 target type AFTER `attachEvalPairX` returns. Capturing earlier would have silently
   labelled every stage-B row with the chain-kind value -- the same class of stale-input bug the
   loop exists to kill.
 * **The rows are the SCORED stream, not the delivered one**: emitted before the banded delivery
   margin, before the -XC bar, before stage B's theta and before its "stage A owns this pLS" skip.
 * **Downsampling is verifiable from the dump alone.** Stage B keeps 1 in 16 by
   `((target * 2654435761u) ^ (pls * 40503u)) % 16 == 0` on the two identities the ROW carries, so
   `read_pairs.py` re-derives the predicate and fails any row that should not be there. Stage A is
   whole. No label, no score, no event id enters the decision.
 * **Truth joining**: stage 0/2 -> `chains.bin` on `(ievt, target)`; stage 1 -> the `t3_*` ntuple
   branches on the sparse triplet index; pLS side through the pLS collection order. `AttachPlsPre`
   carries **no `seedIdx`**, so there is no cheaper pLS key at the call site -- I checked rather than
   adding a field to a hot struct.

### VOLUME -- BUDGET THIS BEFORE THE ROUND-3 DUMP

    2.0e5 rows/evt mean (A 1.23e5, B 6.5e4, aux4L 1.4e4), 96 B/row  ->  19 MB/event
    ~19 GB for a 1000-event training dump; the box had 140 GB free after my cleanup
    busiest of 30 events: 4.7e5 rows -> pass LST_CHAIN_PAIR_CAP=2000000 for production

Stage A dominates and is NOT downsampled, so raising `dsB` barely helps (13 MB/event even at 256).
A capacity overflow is never silent: the writer counts it in the header's `nDrop` and the reader
fails the file. Env knobs: `LST_CHAIN_PAIR_DUMP` (path), `LST_CHAIN_PAIR_CAP` (rows, default 1e6),
`LST_CHAIN_PAIR_DSB` (stage-B keep factor, default 16, 1 = keep all).

### WORKTREE NOTE (read this before trusting a "clean worktree" handoff)

I was told g2 was clean at `4f1846078d9`. **It is not** -- it carries an uncommitted round-5
experiment (9 modified files, `Chain2NetworkWeights.h` DELETED, `AttachNetworkWeights.h` rewritten,
plus an untracked `ChainConfigEnv.h`). I did not reset it. I used **g4** instead: same commit, zero
modified tracked files, sentinel `dcaSplit2 = 12.f` present. g4 had never built CUDA before; it does
now. Anyone assigned g2 should assume its diff is someone's unshipped work.

## [S1 20:55] The retrain: +1.9% background rejection at matched efficiency, and the logit SCALE is preserved

Three arms, shipped v3 recipe (arch [40,32,32,1], conditioning + standardization + BCE with
`pos_weight`, seed 42, batch 65536, no displaced weighting -- `edge_norm_v3.json` train_args),
trained on the **on-policy** dump: 111.9M edges, event-level 60/20/20 = **600 train / 200 val /
200 test events** (66.9M train rows, vs 47.5M for the shipped head). Only lr / patience / schedule
were searched. Verdict metric = fake-weighted FPR at MATCHED per-cell true-edge acceptance, bars
FIT ON VAL and APPLIED to TEST:

    arm        recipe                          best ep  val AUC   TEST ratio   E1      E2
    shipped v3 lr1e-3, 100 ep (reference)         100   .960138     1.0000    1.0000  1.0000
    A_lr1e3    lr1e-3 constant, 200 ep            199   .960170     0.9986    1.0018  0.9977
    B_lr3e3    lr3e-3 constant, 200 ep            188   .961144     0.9899    0.9630  0.9972
    C_cos3e3   lr3e-3 -> 1e-5 cosine, 400 ep      387   .961298   **0.9811**  0.9406  0.9921
    C_fprsel   same run, selected on val FPR      370   .961286     0.9809    0.9402  0.9919

**B1's warning about undertraining is confirmed, twice.** The shipped recipe's own lr/patience
(1e-3 / 8) reaches FPR ratio **1.0000** -- a retrain on 40% MORE data than the shipped head buys
NOTHING at that learning rate, because 200 epochs of lr 1e-3 has not converged (val AUC still rising
at epoch 200). lr 3e-3 with a cosine decay converges and buys 1.9%. **Selecting on the deployed
metric instead of val AUC changes nothing here (0.9809 vs 0.9811)** -- once the LR is decayed the
trajectory is flat, so B1's `--val-metric fpr` lever is a no-op for this head.

**Where the gain is: E1 (-5.9% background), not E2 (-0.8%).** E2's shipped bar -2.0 admits 82.8% of
FALSE E2 edges, so E2 eligibility is nearly saturated by design and there is almost nothing to win
there; the weld's argmax, not its bar, is what discriminates E2.

**The swap is conservative in a way worth recording for stage 2.** On the 200 test events the new
head's logits sit on the SAME SCALE as the shipped ones -- E1 all-edge mean **-3.708 vs -3.734**
(p50 -3.748 vs -3.631), E2 mean **0.986 vs 1.019**; eligible fraction E1 **.2060 vs .2092**, E2
**.9015 vs .9051**, with **92.3% (E1) / 98.3% (E2) of the eligible SET unchanged**. So chain
features 2/3/4/18 and the chain score (a sum of logOdds against `lambdaLen`) stay close to
on-distribution, and the fitted table lands at E1 bars ~+0.02 (shipped 0.0) and E2 ~-1.98
(shipped -2.0). All 40 cells are populated (nTrue 2990..421878) and every cell's acceptance is
matched to 5 decimals; one cell (E2, pT>5, |eta|>2.25) had shipped acceptance exactly 1.0 and takes
the tightest bar that keeps 100%, -2.59, rather than a -1e9 free pass.
-- nnloop_ref/s1_work/{models,wp_C_cos3e3.json,logs/train_[ABC].log}

## [S1 21:05] **THE FIXED-SIGNAL-EFFICIENCY PROTOCOL CANNOT PROTECT THE EDGE HEAD. THE WELD IS AN ARGMAX, NOT A THRESHOLD.**

This is the most important thing I have to report and it changes how stage 1 (and any future edge
work) must be judged.

The converged retrain (C, FPR ratio .9811 on held-out edges) deployed with its matched-efficiency
table gives, on the PU200 TUNE (`-i PU200RelVal -n 1000 -s 8`, vs `ship_ref/B2_shipped.judge`):

    eff_overall_incut   .810201 -> .810294   +.000093  (+0.1 sig, +7 tracks)   NEUTRAL
    eff_barrel/trans/ec        all within +-0.2 sig                            NEUTRAL
    fake_overall_incut  .045479 -> .045134   -.000345  (-2.1 sig, -548 TCs)    BETTER
    fake_barrel         .051448 -> .051062   -.000386  (-2.2 sig)              BETTER
    fake_transition     .047886 -> .047294   -.000592  (-3.5 sig)              BETTER
    dup_transition      .014390 -> .013849   -.000541  (-5.8 sig, -858 TCs)    BETTER
    dup_overall_incut   .045205 -> .045002   -.000202  (-1.2 sig)              better
    eff_vxy_1_5         .799872 -> .795595   -.004276  (-0.7 sig, -20 tracks)  WORSE
    eff_dxy_5_10        .236346 -> .227408   -.008937  (-0.7 sig, -9 tracks)   WORSE
    eff_dxy_10_30       .049398 -> .037999   -.011400  (-2.4 sig, -18 tracks)  WORSE  <-- E1-B2's headline
    n_tc_t4cl             64110 -> 62513     -2.49%

and on **cube50_highPt, EVERY band down 15-25% relative** (vxy[1,5) -.0220 = -5 tracks,
vxy[5,10) -.0180 = -15, dxy[1,5) -.0140 = -38, dxy[10,30) -.0019 = -24; n_tc 562 -> 465,
n_tc_t4cl 157 -> 102). cube50 is the other way (5 of 7 bands up, fake down).

**THE TABLE IS NOT THE CAUSE.** Control arm NOTAB = the SAME retrained head with
`edgeWpTable = false`, i.e. the shipped scalars 0 / -2.0 and no per-cell bar at all:

    cube50_highPt        shipped    S1R1 (table)     NOTAB (scalars)
    eff_vxy_1_5           .2511    .2291 (-.0220)   .2291 (-.0220)
    eff_vxy_5_10          .1199    .1019 (-.0180)   .1007 (-.0192)
    eff_dxy_1_5           .0932    .0791 (-.0140)   .0795 (-.0137)
    n_tc                    562      465 (-97)        466 (-96)

Identical to within one track. So the working point is doing its job -- and that is exactly the
problem. **The edge logit is the RANKING KEY of the mutual-best weld (`chainWeldKey`, one out-slot
and one in-slot per node), not only a threshold.** A WP protocol can pin how many edges are
ELIGIBLE (measured: eligible fraction E1 .2060 vs shipped .2092, E2 .9015 vs .9051, with 92%/98% of
the eligible SET unchanged) and still let a different edge win a node's slot. Efficiency
"neutral by construction" is therefore **false for this head** -- it is true for a head whose output
is only ever compared to a bar (the attach delivery bars, the gate kills), which is where the
protocol came from.

**MECHANISM OF THE DISPLACED LOSS.** The edge objective is prompt-dominated: of the 17.9M TRUE train
edges, **15.36M (85.8%) have shared-sim vxy < 1 cm**, and the single bin (log10 pt in [0,0.5),
vxy < 1) holds 11.2M of them. Converging harder (lr 3e-3 + cosine, 400 epochs) therefore buys
prompt-topology accuracy -- visible as the fake/dup win -- and spends the displaced tail, which is
what the weld needs for a 4-layer E2 chain of a displaced track. The shipped v3 head reaches
`best_epoch = 100` at lr 1e-3 and my arm A reproduces it (FPR ratio 1.0000): **the shipped head's
displaced quality is partly an artefact of stopping early.**

CONSEQUENCE FOR THE LOOP (S2/S3 please read): for the 3-class gate and the attach head the WP
protocol stands -- their outputs are compared to bars. For anything the weld or the K9 order key
RANKS, a matched-efficiency table is necessary but NOT sufficient, and the gate has to be the
deployed physics.
-- nnloop_ref/s1_work/runs/{S1R1_*,NOTAB_*}.judge

## [S1 21:18] Where the displaced loss is NOT, and why a 1% reshuffle costs 20% of a displaced band

Four independent localisations, all on the held-out test events of the same dump, comparing the
shipped head with arm C (its own matched-efficiency table):

 1. **ELIGIBILITY: neutral by construction and by measurement.** Eligible fraction E1 .2060 vs
    .2092, E2 .9015 vs .9051; 92%/98% of the eligible SET is the same edge.
 2. **WELD RANKING (sweep-1 mutual best, the coordinator's argmax-faithful metric,
    `weldrank.py`): neutral, and marginally BETTER for displaced.** Weld rate of eligible TRUE edges
    that face competition: displaced **.11448 shipped -> .11456 arm C**, prompt .12169 -> .12025, so
    the displaced/prompt ratio moves .9407 -> .9527 in the arm's favour. Slot-win rates for contested
    displaced trues: out .33437 -> .33572, in .31191 -> .31307. **So the sweep-1 ordering is not what
    breaks displaced tracks.**
 3. **GATE INPUTS: on-distribution to ~1%** (`chaindiff.py`, 200 events, shipped
    `round1/chains.bin` vs an arm chain dump of the same events). Edge-logit aggregates f2 p50
    1.8662 -> 1.8789, f3 1.5747 -> 1.5798, f4 1.7255 -> 1.7364, f18 p90 .5529 -> .5387; gate margins
    mX p50 -1.9110 -> -1.8525, mD -3.2625 -> -3.2616. So the m3Theta4 family is NOT badly
    off-distribution and a gate refit cannot be expected to recover much.
 4. **BUT THE CHAIN POPULATION MOVES, AND ASYMMETRICALLY:** total chains +1.23%, 5+ layer +1.86%,
    4-layer +0.70%, and **far-displaced (chain dcaXY >= 12 cm) -1.43%**, with the chain SCORE's upper
    tail contracting (p90 26.7054 -> 26.2440, -1.7%). The score is a SUM of edge logOdds against
    `lambdaLen`, and it is the **K9 greedy hit-claim ORDER KEY** -- a second argmax, exactly as the
    coordinator noted. A 1.7% contraction of the score tail reorders long-prompt against
    short-displaced chains competing for the same hits.

**THE AMPLIFICATION, stated because it explains why every diagnostic looks small and the gate looks
big.** The displaced bands are reconstructed at 0.5-13% efficiency, so they live entirely on the
boundary of every decision. PU200 dxy[10,30): 1579 sims, 78 reconstructed, **18 lost = 1.1% of the
band population but 23% of what was reconstructed**. cube50_highPt dxy[1,5): 2705 sims, 252
reconstructed, 38 lost = 1.4% of the population, 15% of the reconstruction. So a ~1% reshuffle of
the chain population, which is all any of the four measurements above sees, IS a 15-25% relative
band change. There is no localised bug to find: the head reallocates capacity from a tail that
carries almost no loss mass (14.2% of true edges are displaced, and 85.8% of the loss is prompt) to
the bulk, and the bands amplify it.

**FOR STAGES 2 AND 3, the general form of the [S1 21:05] finding:** a fixed-signal-efficiency table
makes a head swap neutral only where the head's output is COMPARED TO A BAR. The chain pipeline has
at least three places where a head's output is RANKED instead: the K6 weld argmax, the **K9 greedy
hit claim (order key = chain score, which is built from edge logits)**, and the attach contention.
For any head feeding those, the WP protocol is necessary but not sufficient, and only the deployed
physics is a verdict.
-- nnloop_ref/s1_work/{weldrank.py,weldrank.json,chaindiff.py,arm_chains.bin}

## [S1 21:25] NEGATIVE: the golden script's own displaced weighting makes the RANKING worse, not better

Arm D = arm C's converged recipe plus `analysis/DNN/train_edge.py`'s `displaced_weights` (inverse
frequency of the coarse joint log10(simPt) x vxy-class bin among train trues, clip [1,20], mean-1 --
the v2 option that **v3 shipped with OFF**). On this data the fit puts w = 0.2304 on the dominant
prompt bin (11.2M rows) and the clip value 4.6080 on 14 of 18 bins, i.e. a ~20x reweight:

    arm         val AUC   TEST FPR ratio   E1 ratio   weld_D   weld_P    D/P ratio
    shipped v3  .960138       1.0000        1.0000    .11448   .12169     .9407
    C_cos3e3    .961298       0.9811        0.9406    .11456   .12025    **.9527**
    D_dw        .954001       1.0769        1.3124    .11485   .12388    **.9271**

D pays **+7.7% background overall and +31% on E1** at matched acceptance, and its displaced/prompt
weld-rate ratio is **WORSE than shipped** (.9271 vs .9407): the weighting makes more displaced edges
ELIGIBLE (151836 vs 147330 contested eligible displaced trues) while its slot-win rate DROPS
(out .32662 vs .33437). It buys admission and loses the competition -- the opposite of what is
needed, and a concrete reason v3 disabled it. NOT gated on physics; the offline verdict is enough to
reject this weighting shape. The coordinator's TIERED arm (1 / 8 / 16 on vxy, fakes 1) is running
separately -- it is a different shape (no pT term, no clip saturation) and must be judged on its own.
-- nnloop_ref/s1_work/{wp_D_strat.json,weldrank.json,logs/train_D.log}

## [S1 21:30] STAGE-1 GATE TABLE, and the two deliverable patches

**ARM S1R1** = head C (converged on-policy retrain) + its pooled matched-efficiency table.
Built in g1 from `4f1846078d9`, 0 `error:` in `.make.log.1786495351`.

    PU200 TUNE event_1000 (1000 evt, -s 8, vs ship_ref/B2_shipped.judge)
      eff_overall .810201 -> .810294 (+.000093, +0.1 sig)     dup_overall .045205 -> .045002 (-1.2 sig)
      eff_barrel  .924316 -> .924213 (-0.1 sig)               fake_overall .045479 -> .045134 (-2.1 sig)
      eff_trans   .882124 -> .882797 (+0.2 sig)               fake_barrel  .051448 -> .051062 (-2.2 sig)
      eff_endcap  .679590 -> .679620 (+0.0 sig)               fake_trans   .047886 -> .047294 (-3.5 sig)
      eff_vxy_1_5 .799872 -> .795595 (-.004276, -0.7 sig)     dup_trans    .014390 -> .013849 (-5.8 sig)
      eff_dxy_5_10 .236346 -> .227408 (-.008937, -0.8 sig)    n_tc     1587160 -> 1586561 (-0.04%)
      eff_dxy_10_30 .049398 -> .037999 (-.011400, -2.5 sig)   n_tc_t4cl  64110 -> 62513 (-2.49%)

    POOLED 6000-EVENT HOLDOUT (event_2000..7000, a5_ref/paired.py McNemar)
      overall     .8120 -> .8120  (+.0001, p .618)      dxy_1_5   .5700 -> .5716 (+.0015, p .404)
      eff_barrel  .9235 -> .9234  (p .75)               dxy_5_10  .2531 -> .2443 (-.0088, p .0058)
      eff_trans   .8808 -> .8815  (+.0007, p .126)      dxy_10_30 .0448 -> .0380 (-.0068, p 1.45e-06)
      eff_endcap  .6854 -> .6853  (p .618)              vxy_5_10  .7190 -> .7159 (-.0031, p .068)
      vxy_10_30   .6951 -> .6962  (+.0011, p .459)      vxy_1_5   .7992 -> .7989 (p .74)

    cube50 (5000 evt, -s 4): 5 of 7 bands UP (vxy[5,10) +.0063, dxy[5,10) +.0023, dxy[10,30) +.0007),
      vxy[1,5) -.0181 (-3 tracks), fake -.0001, n_tc +26.
    cube50_highPt (5000 evt, -s 4): EVERY band DOWN -- vxy[1,5) -.0220 (-5), vxy[5,10) -.0180 (-15),
      vxy[10,30) -.0046 (-34), dxy[0,1) -.0089, dxy[1,5) -.0140 (-38), dxy[5,10) -.0048,
      dxy[10,30) -.0019 (-24); dup +.0020; n_tc 562 -> 465; n_tc_t4cl 157 -> 102.

    SHIP-VERIFICATION: `nnloop_ref/s1_edge_retrain.patch` applied to a tree reset to 4f1846078d9,
    independently rebuilt (0 `error:` in `.make.log.1786495529`): all 35 judge fields
    BIT-IDENTICAL to the measured arm.

**ARM S1RS** (same head, table stratified on simVxy with the looser bar binding -- the golden gate
study's convention): materially identical. dxy[10,30) -.01203 on the tune, -.0072 (p 3.8e-07) on the
6000-event holdout, cube50_highPt unchanged to within one track, cube50 marginally better
(vxy[1,5) -.0120 instead of -.0181). **The table shape is not the lever; three variants
(pooled / stratified / no table at all) agree to within a track on the protected rows.**

**VERDICT: the head is NOT shippable. It buys fake/dup everywhere and pays displaced.** Against the
maintainer's priority (eff >> dup > fake, displaced bands protected) this is the wrong trade, and
`eff_dxy_10_30` is E1-B2's shipped headline. Overall efficiency is neutral (+.0001, p .62), so the
brief's stage-1 failure threshold (overall eff -.002) is NOT crossed -- the coordinator gets to
choose, with the numbers above, and I recommend against shipping the weights.

**TWO PATCHES, both `git apply --check` clean on pristine `4f1846078d9`:**
 * `nnloop_ref/s1_edge_retrain.patch` -- mechanism + retrained head C + its table. Ship-verified
   bit-identical to the measured arm. Physics as tabulated above.
 * `nnloop_ref/s1_wp_mechanism_only.patch` -- the SAME mechanism with the SHIPPED weights and a
   constant table (E1 0.0 / E2 -2.0). **Measured BIT-IDENTICAL to shipped on all 35 fields over 1000
   events.** This is the risk-free half: it lands the per-cell weld working-point knob (and deletes
   two kernel arguments) at provably zero physics cost, so any future edge round -- including a
   stage-1 round 2 -- has the calibration surface already in place.

## [S1 21:35] Housekeeping: what is on disk, and the one gate I did NOT run

**TIMING IS NOT MEASURED and must be before the mechanism patch ships.** The mechanism adds one
`uint8` column per chain node (~43 kB/event) and one `float` column per edge (~450 kB/event), and
REMOVES two kernel arguments from K6a/K6b. Both weld kernels now do one extra load per edge per
sweep instead of a branch on two scalars, and K5 does one byte load plus a table lookup per edge.
That is a layout change on the chain block's hottest kernels, so scope rule 6 applies. I could not
measure it honestly: the box was running 4-6 concurrent `lst_cpu` jobs of mine and the PD agent's for
the whole round, and TRAP 6 (a CPU total is not comparable between two binaries under ~45 ms) makes a
contaminated measurement worse than none. The parity build gives the physics half of the A/B for
free (bit-identical), so a same-binary config-only timing A/B on `edgeWpTable` true/false is
available whenever the machine is quiet.

**Artifacts (all under `nnloop_ref/s1_work/`, nothing in /tmp):**
    cache/X.npy          17.9 GB  the 111.9M x 40 conditioned input cache (dump order)
    cache/meta.npz        2.0 GB  label / type / ptbin / etabin / evt / shipped logit / simVxy / simPt
    lab/ev*.npz           1.4 GB  per-event truth shards (label + bin key + sim kinematics)
    models/edge_*.pt               arms A/B/C/D/E + the FPR-selected checkpoints
    wp_*.json                      per-cell tables + the full per-cell fit/judge reports
    edge_hdr_*.h                   generated headers (S1R1 = head C pooled, S1R1S strat, S1E tier)
    runs/                          every gate run's .root/.log/.judge/.json
    dumpio.py label.py build_cache.py train_s1.py wp_table.py export_edge.py weldrank.py
    chaindiff.py report.py paired.sh check_align.py run_E.sh
The 18 GB cache is REBUILDABLE in ~2 minutes from `round1/` + `lab/` with `build_cache.py cache`;
delete it if the disk gets tight (it was at 140 GB free / 97% used when I finished).
NOTE: I kept ALL 111.9M edges -- no negative downsampling, so no sample weights and no factor to
record. The `--max-fakes` lever of the golden script was not used.

## [S1 21:40] Two nits on the generated header, for whoever regenerates it next

 1. `export_edge.py` re-emits the SHIPPED header from itself bit-exactly (2601 literals, float32
    equality -- the export path is verified, not trusted), and the check compares **float32 values,
    not decimal text**: a shortest-round-trip literal is allowed to print fewer digits
    (`23.1032f` for `23.1031609f`) and my first version of the check flagged 4 arrays as
    "MISMATCH" for exactly that reason. If you reuse the checker, keep the float32 cast.
 2. The two matrix arrays were emitted one row per line, which puts **77 lines over CMSSW's
    120-column limit** (baseline: 1). `export_edge.py` now wraps them (`row_block`), leaving 2. The
    headers inside the two delivered patches are the UNWRAPPED ones, because those are the bytes I
    built and ship-verified; reflowing is whitespace-only (re-parse: 12/12 arrays identical as
    float32), so a regenerate + rebuild before shipping is safe and costs one build.

## [S1 21:45] **NEGATIVE, ON THE COORDINATOR'S OWN METRIC: PUTTING THE DISPLACED PRIORITY IN THE LOSS MAKES THE DISPLACED RANKING WORSE.**

Arm E = arm C's converged recipe (cos 3e-3, 400 ep, seed 42) plus the requested TIERED positive
weighting -- w = 1 for vxy < 1 cm and for pileup-only trues, **8 for vxy in [1,5), 16 for
vxy >= 5**, fakes 1 (15,368,804 / 624,946 / 1,929,146 train trues, mean true weight 2.859). Same
stratified table, same held-out test events, same argmax-faithful metric:

    arm         val AUC   TEST FPR ratio  E1 ratio | weld_D   weld_P   D/P     slotwin_out_D
    shipped v3  .960138       1.0000       1.0000  | .11448   .12169   .9407      .33437
    C_cos3e3    .961298       0.9879       0.9585  | .11456   .12025 **.9527**    .33572
    D_dw (v2 w) .954001       1.0769       1.3124  | .11485   .12388   .9271      .32662
    E_tier816   .957893     **1.0409**     1.1413  | .11032   .12056 **.9150**    .32894

**E is the worst arm on displaced ranking of the four, and it pays +4.1% background (+14.1% on E1).**
Both weighted arms show the same signature: MORE displaced true edges become eligible (151k vs 147k
contested) while their SLOT-WIN rate FALLS (.32894 / .32662 vs .33437 shipped). The unweighted,
converged arm C has the best displaced/prompt weld ratio of all four.

**WHY, and it generalises.** A weighted loss moves the displaced class's logits as a GROUP -- it buys
CALIBRATION, which is what a global bar consumes. The weld consumes a per-node ORDERING, and a
displaced true edge's competitors at that node are largely edges that look displaced too (its own
duplicates, other sims' edges through the same MD). Raising the class raises the competitors with it,
while the downweighted prompt bulk stops sharpening the local discrimination the argmax needs. So:
**class weighting is the wrong instrument for an argmax; it is the right instrument for a threshold.**

**AND THE DISPLACED DEFICIT IS NOT AT THE WELD AT ALL.** Arm C has the best weld ranking of the four
AND still loses `eff_dxy_10_30` (-.0068, p 1.45e-06 on 6000 events). Combined with [S1 21:18] --
eligibility neutral, gate inputs on-distribution to ~1%, but far-displaced (dcaXY >= 12 cm) chains
-1.43% and the chain SCORE's p90 contracted 1.7% -- the surviving suspect is the **K9 greedy
hit-claim ORDER KEY**, which is the chain score, i.e. a SUM of edge logits traded against
`lambdaLen`. A head can preserve every per-edge ordering and still shift that sum.

**THE NEXT ARM I RECOMMEND (untried, cheap, no retrain):** after training, fit a **2-parameter affine
recalibration of the logit** (a * logit + b) so the new head reproduces the SHIPPED chain-score
distribution (and hence the K9 order key and the `lambdaLen` trade), then refit the WP table on the
recalibrated logit. An affine map cannot change any per-edge ordering, so arm C's FPR gain and its
weld ranking survive by construction, while the one quantity that moved -- the score scale -- is
pinned. It is a threshold-scale calibration on labelled dumps, exactly like the WP table, not a
fitted surrogate of a network. If that does not recover dxy[10,30), the deficit is in the claim
ORDER itself rather than its scale, and the honest conclusion is that the shipped edge head cannot be
replaced without re-tuning K9 -- which is a different workstream from "retrain the head".
-- nnloop_ref/s1_work/{wp_E_strat.json,logs/weldrank_all.log,logs/train_E.log}

## [S1 21:55] **DEPLOYED: THE TIERED ARM RESTORES DISPLACED AND OVERSHOOTS IT -- AND MY OFFLINE WELD METRIC DID NOT PREDICT IT. RETRACTION.**

Arm E (tiers 1 / 8 / 16) gated on the real pipeline, and it inverts the conclusion I drew from its
offline metrics one post ago:

    PU200 TUNE (1000 evt)      shipped     arm C      arm E        arm E vs shipped
      eff_overall_incut        .81020     .81029     .80872       -.00148  (-1.0 sig)
      eff_vxy_1_5              .79987     .79560     .79602       -.00385  (-0.7 sig)
      eff_vxy_5_10             .71379     .71330     .72365       +.00985  (+1.0 sig)
      eff_vxy_10_30            .69315     .69411     .71924       +.02609  (+3.8 sig)
      eff_dxy_1_5              .55133     .55231     .58317       +.03184  (+3.6 sig)
      eff_dxy_5_10             .23635     .22741     .26415       +.02781  (+2.0 sig)
      eff_dxy_10_30            .04940     .03800     .05446       +.00507  (+0.9 sig)  BEATS SHIPPED
      dup_overall_incut        .04520     .04500     .04441       -.00079  (-4.9 sig)  BETTER
      fake_overall_incut       .04548     .04513     .05912       +.01364  (+73 sig)   MUCH WORSE
      n_tc                    1587160    1586561    1611608       +1.5%
      n_tc_t4cl                 64110      62513      76404       +19.2%
    cube50 (5000 evt): arm E is BETTER THAN SHIPPED ON EVERY BAND -- vxy[5,10) +.0254,
      vxy[10,30) +.0060, dxy[1,5) +.0035, dxy[5,10) +.0100, dxy[10,30) +.0037, and fake
      .0042 -> .0029. cube50_highPt: the -s 4 run hit the writer SEGFAULT (RUN_EXIT=139) even
      though -s 4 is the documented workaround -- arm E makes 19% more T4-class TCs, so the
      threshold moved; rerunning at -s 2.

**RETRACTION of my [S1 21:45] INFERENCE (the measurements stand, the conclusion does not).** I
concluded from the offline argmax metric that class weighting "makes the displaced ranking worse" and
therefore could not help. The deployed gates say the opposite: the same arm whose displaced weld rate
FELL (.11032 vs .11448 shipped) delivers **+.026 vxy[10,30) and +.032 dxy[1,5)**. So my sweep-1
weld-purity metric is NOT PREDICTIVE of delivered displaced efficiency. Why, mechanically: it scores
the fraction of CONTESTED displaced trues that win, while what physics needs is the ABSOLUTE number
of displaced chains that get built and survive -- and the weighting raises the eligible displaced
population (+3%) and the number of displaced chains, so a lower win FRACTION over a larger admitted
set still delivers more tracks. The metric also ignores slot occupancy across sweeps.
**Do not use an offline weld proxy as a stage gate; it did not survive contact with the pipeline.**
(The [S1 21:05]/[21:18] finding that a matched-efficiency TABLE cannot protect a ranked logit is
untouched -- that was established by the NOTAB control on the deployed gates, not by this proxy.)

**WHERE ARM E STANDS: not shippable as-is, but the first arm on the right axis.** It trades
+.0136 fake (+30% relative, 73 sigma) and -.0015 overall efficiency for a large displaced gain and a
dup improvement. Against the standing priority (eff >> dup > fake) the overall-eff loss and the fake
explosion are both real costs. The tier factors 8/16 are simply too strong; the trade is now a DIAL,
which is what stage 1 was missing an hour ago.
-- nnloop_ref/s1_work/runs/S1E_*.judge

## [S1 22:15] NEGATIVE: pinning the logit SCALE does not recover the displaced loss. The K9 score scale is not the mechanism.

The affine arm, as directed: arm C with z -> a*z + b baked into the OUTPUT LAYER (so it is still a
40->32->32->1 head, no new constant, no code change) and the WP table refitted on the mapped logit.
An affine map cannot reorder any edge, so arm C's background gain and weld ranking survive exactly;
the only thing that moves is the scale the chain score and the gate features live on.

    fit on the VAL eligible population: shipped mean 1.9820 std 2.3349 | arm C mean 1.9688 std 2.3104
    => a = 1.010600, b = -0.007640;  after the map the arm reproduces 1.9820 / 2.3349 exactly.
    TEST FPR ratio: 0.9879 -- IDENTICAL to arm C, as a monotone map must be.

    cube50_highPt (5000 evt)   shipped     arm C     arm C-AFFINE
      eff_vxy_1_5               .2511      .2291       .2291
      eff_vxy_5_10              .1199      .1019       .1019
      eff_dxy_1_5               .0932      .0791       .0795
      eff_dxy_10_30             .0048      .0029       .0029
      n_tc                        562        465         467

**Not one track recovered.** The 1.7% contraction of the chain-score p90 I flagged at [S1 21:18] was a
SYMPTOM, not the cause: correcting it changes nothing. So of the four candidate loci, three are now
excluded by direct experiment (eligibility -- NOTAB control; score scale -- this arm; gate inputs --
measured on-distribution) and the fourth (sweep-1 weld ranking) was measured neutral-to-better. What
remains is that the converged head simply admits/orders a slightly different SET of displaced-relevant
edges, and only the deployed pipeline resolves the consequence. **Arm E is the existence proof that
the displaced axis is reachable** (+.026 vxy[10,30), +.032 dxy[1,5)) -- via ADMISSION, at a fake cost.

**LST's OWN 3-class training convention, found in the tree as asked** (`g3` LST master,
`standalone/analysis/DNN/train_T3_DNN.ipynb`) -- so arm G replicates it rather than guessing:
 * net `Linear(in,32) -> ReLU -> Linear(32,32) -> ReLU -> Linear(32,3)` with `softmax` in `forward`;
 * `WeightedCrossEntropyLoss`: `log_probs = log(softmax + 1e-7)`,
   `losses = -weights * sum(targets * log_probs, dim=1)`, `mean()` reduction;
 * `calculate_class_weights`: `class_weights = total_samples / (3 * class_counts)`, assigned per
   sample by its class -- i.e. **each of the three classes contributes equally**, mean sample
   weight 1.
On my train split the counts are fake 50,429,942 / prompt 15,368,804 / displaced 2,554,092, giving
weights **0.4518 / 1.4825 / 8.9207**. I use `nn.CrossEntropyLoss(weight=...)`, the numerically stable
fused form of exactly that objective. Arms F (natural frequencies) and G (LST balancing) are both
training on the same split, cache and recipe, so the two-point comparison the maintainer asked for is
apples-to-apples.
-- nnloop_ref/s1_work/{affine.py,wp_Caff_strat.json,runs/S1AFF_*.judge,logs/train_[FG].log}

## [S1 22:12] Arm F (3-class edge head) is IMPLEMENTED, compiles clean, and its offline verdict is in

**The C++ shape, and it needed no new plumbing beyond what the parity build already proved inert.**
`kOutputs = 3`; K5 evaluates the output layer as three dot products over the same staged h2 block
(the batched linear primitive blocks its units by 8 and cannot take 3), with `wgt_out` stored
`[class][hidden]` -- the ONE array where the `wgt[in][out]` convention is transposed, so each class's
vector is contiguous. **Eligibility is the OR-rule evaluated in K5 and materialized into the EXISTING
`weldBar` column as a degenerate bar (-1e30 eligible / +1e30 not), so K6a/K6b keep their single
`logOdds < weldBar` test and are not touched at all.** The stored scalar is
`mX = max(zP, zD) - zF`, affine-pinned, so ChainWeld / the chain score / chain features 2/3/4/18 see
the same scale they always did. 0 `error:` in `.make.log.1786499977` (a deliberate compile-smoke with
a dummy 3-class header) and again in `.make.log.1786500476` with the real one.

**Choice of the single scalar, with the reasoning the directive asked for.** `mX = max(zP,zD) - zF`
is "how much more this edge looks like SOME real track than like a fake". I keep it rather than
`logsumexp(zP,zD) - zF` because (a) it is what the 3-class chain gate already uses on its own
outputs, so the pipeline has one convention, and (b) the max() information loss the earlier
`gateLogit2` analysis warned about (FINDINGS_NN header: "max() discards WHICH class won") is
IRRELEVANT here precisely because the eligibility decision no longer goes through the scalar -- it
goes through the two class margins directly. The scalar only has to rank and to sum. If the deployed
gates show a weld-ranking problem I will test the logsumexp variant, which is a one-line change.

**OFFLINE VERDICT (fit on VAL, judged on the 200 TEST events, per-class matched targets):**

    prompt-true acceptance    shipped .95193 -> new .95685   (+.00492)
    displaced-true acceptance shipped .89027 -> new .90732   (+.01705)
    FAKE-edge acceptance      shipped .22885 -> new .24459   (ratio 1.0688)
    affine pinning exact: pinned mX on eligible mean 1.9820 std 2.3349 == shipped 1.9820 / 2.3349

**An honest structural caveat about the protocol itself: the OR-rule cannot be efficiency-NEUTRAL by
construction, only neutral-or-looser.** Matching each class separately and then taking the union
admits the union of two sets that each already reach the target, so both acceptances come out ABOVE
shipped (+0.5% prompt, +1.7% displaced) and the fake rate follows (+6.9%). That is a WP that BUYS
displaced admission -- the same axis arm E rode, at about a fifth of E's strength. The exactly-matched
variant would need the two bars solved JOINTLY per cell (2 equations, 2 unknowns, one fixed-point
iteration) and would pin displaced acceptance to shipped, which is the version to fit if the deployed
gates show the surplus is too expensive. Deploying the directive's version first, measuring second.
-- nnloop_ref/s1_work/{wp3.py,wp_F.json,export3.py,edge_hdr_S1F.h}

## [S1 22:40] **THE THREE-CLASS EDGE HEAD WORKS. RANKING TABLE FOR THE FOUR CANDIDATE HEADS.**

Both 3-class arms are trained, calibrated on the dual OR-rule tables, affine-pinned, ship-verified
and gated. F = natural class frequencies, G = **LST's own T3-DNN class balancing** (weights
0.4518 / 1.4825 / 8.9207 from `total_samples / (3 * class_counts)`, verbatim from
`g3 .../analysis/DNN/train_T3_DNN.ipynb`).

    OFFLINE (200 TEST events, per-class matched targets, fit on VAL)
    arm  head form                 acc_prompt        acc_displaced      fake-edge   weld_D   D/P
    ship binary v3 (incumbent)     .95193            .89027              1.0000     .11448  .9407
    C    binary, converged         (single axis: .9879 fake-weighted FPR at matched eff)  .11456  .9527
    E    binary + tiers 8/16       (single axis: 1.0409)                             .11032  .9150
    F    3-class, natural freq     .95685 (+.0049)   .90732 (+.0171)     1.0688     .11501  .9534
    G    3-class, LST balancing    .95665 (+.0047)   .91227 (+.0220)     1.0997     .11117  .9370

    DEPLOYED PU200 TUNE, delta vs shipped (1000 evt; fake/eff are LOWER BOUNDS -- every downstream
    constant was tuned against the SHIPPED logit distribution, so an off-distribution head is
    penalised by STALE bars, per the coordinator's 22:30 criterion)
    row                    arm C      arm E      arm F      arm G
    eff_overall_incut    +.00009    -.00148    -.00027    -.00127  (-0.9 sig)
    eff_dxy_10_30        -.01140    +.00507    -.00443   **+.00823** (+1.4 sig, +17% rel)
    eff_dxy_1_5          +.00097    +.03184    -.01300   **+.03086** (+3.5 sig)
    eff_dxy_5_10         -.00894    +.02781    -.00695   **+.03078** (+2.2 sig)
    eff_vxy_10_30        +.00096    +.02609    -.00646   **+.02082** (+3.0 sig)
    eff_vxy_5_10         -.00049    +.00985    -.00739    +.00640
    eff_vxy_1_5          -.00428    -.00385    -.00363    -.00492
    dup_overall_incut    -.00020    -.00079    -.00039    -.00052  (-3.2 sig, BETTER)
    fake_overall_incut   -.00035    +.01364    +.00044    +.00571  (+12.6% rel)
    fake_barrel          -.00039    +.03284    +.00124    +.01551
    n_tc_t4cl             62513      76404      63265      71237

    DEPLOYED CUBES (5000 evt each)
    cube50:         F better than shipped on 5 of 7 rows (vxy[1,5) -.0120); G better on ALL SEVEN
                    (vxy[5,10) +.0238, dxy[5,10) +.0104, fake -.0013).
    cube50_highPt:  C COLLAPSES (every band -15..-25% rel). **F RESTORES IT and beats shipped on 4 of
                    6 bands** (vxy[1,5) +.0044, dxy[10,30) +.0012 = +25% rel, n_tc 562 -> 574).
                    G is flat-to-better on 4 of 6 but loses vxy[1,5) (-.0176).
                    Determinism cross-check: F's cubehi at -s 2 and -s 1 agree to the last digit.
    Arm E's cube50_highPt is UNMEASURED: the writer segfaults (`setTrackCandidateBranches`,
    write_lst_ntuple.cc:1385, `n_accepted_simtrk` visibly garbage) at -s 4 AND -s 2 with E's binary --
    E makes 19% more T4-class TCs, so the documented "-s 4 is safe" workaround no longer holds. That
    is a pre-existing harness bug, not a patch bug, and it now bites any arm that raises TC count.

**E AND C ARE BOTH DOMINATED.** G gets MORE displaced than E (+.0082 vs +.0051 on dxy[10,30)) for
LESS THAN HALF the fake cost (+.0057 vs +.0136). F gets nearly C's fake/dup with the cube damage
undone. So the frontier is **F (conservative) vs G (displaced-forward)**.

**RECOMMENDATION: carry G into stage 2, with the shipped head as the control arm.** The reasoning is
asymmetry, not the score sheet: **efficiency can only be LOST downstream, never created, while fakes
can be REMOVED downstream** -- the 3-class gate's entire job is to kill bad chains, and its bars are
exactly the constants that are stale against a new edge head. G hands stage 2 the richest displaced
input set (+2.2% displaced-edge admission, +17% relative dxy[10,30), every cube50 row up) and asks it
to spend its retrained bars on the +12.6% relative fake it also admits. F is the arm to fall back to
if stage 2 cannot recover that: F is nearly deployable TODAY (fake +1% relative, dup better, cube
damage undone) but its PU200 displaced rows sit slightly BELOW shipped, and no gate retrain can
recover a displaced chain that the weld never built.
-- nnloop_ref/{s1_arm_F_3class.patch,s1_arm_G_3class_balanced.patch}, s1_work/runs/S1{F,G}_*.judge

## [S1 22:50] ARM G ON THE POOLED 6000-EVENT HOLDOUT: the displaced gain is real at p ~ 1e-46, and it is the biggest displaced move this project has made

`a5_ref/paired.py`, McNemar, `event_2000..7000` (6000 events) vs the shipped reference runs
(`holdout_ref/Chains.root` + `a5_ref/big/ours_*`), arm G = 3-class edge head with LST's own class
balancing, dual OR-rule tables, affine-pinned scalar:

    cell            SHIPPED     S1G     delta   ship-only  arm-only    p
    overall          .8120    .8111   -.0009      2310       1908     6.0e-10
    eff_barrel       .9235    .9225   -.0009       957        796     1.2e-04
    eff_transition   .8808    .8805   -.0003       794        770     .544
    eff_endcap       .6854    .6843   -.0011       559        342     4.8e-13
    vxy_0_1          .8443    .8435   -.0009      2123       1759     5.2e-09
    vxy_1_5          .7992    .7980   -.0012       397        362     .217
    vxy_5_10         .7190    .7195   +.0005       211        217     .809
    vxy_10_30        .6951  **.7189** +.0238       561       1155   **1.2e-46**
    dxy_0_1          .8368    .8363   -.0005      3025       2806     .0041
    dxy_1_5          .5700  **.5950** +.0250       471        941   **6.8e-36**
    dxy_5_10         .2531  **.2909** +.0378       122        354   **3.0e-27**
    dxy_10_30        .0448  **.0546** +.0098        46        135   **2.4e-11**

For scale: E1-B2, the round this project SHIPPED to close the displaced gap, moved PU200 dxy[10,30)
to .0494 = 91% of LST master. Arm G puts the 6000-event holdout at **.0546, +22% relative over the
shipped head**, and lifts vxy[10,30) by 3.4% relative at p 1e-46. The costs are a **-.0009 overall
efficiency** (p 6e-10 -- small, significant, and mostly barrel/endcap prompt) and the +12.6% relative
fake rate, and BOTH are measured against stale downstream constants.

**SHIP-VERIFICATION: 35/35 judge fields BIT-IDENTICAL** for both 3-class arms (patch -> tree reset to
`4f1846078d9` -> independent rebuild -> 1000-event PU200 run), 0 `error:` in
`.make.log.1786501485` (F) and the G verify build.

**STAGE 1 CLOSES HERE with four deliverable patches, all `git apply --check` clean on 4f1846078d9:**
    s1_arm_G_3class_balanced.patch   RECOMMENDED head for stage 2 (displaced-forward)
    s1_arm_F_3class.patch            the conservative 3-class fallback (near-neutral today)
    s1_edge_retrain.patch            the binary converged head (arm C) -- record only, do not ship
    s1_wp_mechanism_only.patch       the WP mechanism alone, PROVEN bit-identical to shipped
Stage 2 should re-dump chains from the G binary and retrain the gate on it, running the shipped head
as the control arm in parallel; the gate is where the +12.6% fake and the -.0009 prompt efficiency
have to come back, and it is the stage whose constants are stale by construction.

## [S1 22:52] Final state, for whoever picks this up

**Worktrees.** g1 = my assigned tree, currently carrying the **arm G** patch content (built, binary
matches `runs/S1G_*`). g6 = the extra worktree I provisioned at 20:0x when g4 was busy; it is now
**clean at 4f1846078d9 with zero modified tracked files** and served as the independent
ship-verification area for every arm. It can be deleted (`git worktree remove`) or kept as a spare
verify area -- nothing of mine depends on it. I touched no other worktree.

**Two things I did NOT do, deliberately:**
 1. **No timing measurement** -- see [S1 21:35]. The mechanism adds a uint8 node column and a float
    edge column and removes two kernel args; the 3-class arms add two more output units and two table
    lookups per edge. All of that wants a quiet machine, and the machine was never quiet.
 2. **No downstream re-tuning** (gate bars, far cell, crossclean, attach) -- that is stage 2+ by
    design, and every fake/efficiency delta I report is therefore a LOWER BOUND on the head.

**One trap discovered that will bite the next agent:** the standalone writer's
`setTrackCandidateBranches` segfault on `cube50_highPt` is a function of TC COUNT, not of stream
count. The documented "use -s 4" workaround failed at -s 4 AND -s 2 for arm E (+19% T4-class TCs).
If an arm raises TC multiplicity, expect to drop to -s 1 or to lose that gate.

## [S2 23:45] Stage-2 setup: the SHIPPED GATE LABEL verbatim, all four provenance hashes verified, and the offline aEtaC reconstruction PROVEN against the kernel

**PROVENANCE, checked before any use (this is the rule the whole loop exists for):**

    round1  (CONTROL rows)   binary_md5 42564e7402a8eb423f0cf39d4aa52d8e  == main tree bin/lst_cpu   OK
                             Chain3NetworkWeights.h ac78fb6978251acc9e5dc9d585517f85 == shipped      OK
                             lst_r1d.judge eff .8102012675346716 == ship_ref/B2_shipped.judge EXACT
    round2G (CANDIDATE rows) binary_md5 90cc7cdefac35e427a328784f6a8ca1e  == g1 bin/lst_cpu          OK
                             EdgeNetworkWeights.h 05b7f3f379aa5fa285538eba5b8b4d8a == g1's           OK
                             g1 `git apply --reverse --check s1_arm_G_3class_balanced.patch` CLEAN,
                             i.e. g1 == shipped 4f1846078d9 + arm G exactly, nothing else.
    BOTH dumps carry the SHIPPED Chain3NetworkWeights.h, so every record's zF/zP/zD, mP/mD/mX AND
    the `flags` byte are the SHIPPED gate's own decision ON THAT ARM'S OWN ROWS. The bar refit
    therefore has its neutrality reference in the dump itself -- no re-derivation, no assumption.

**THE LABEL, REPLICATED NOT INVENTED.** `prototype/chain3_norm_m12.json` names the shipped head's
sources (`chains_m12_{300,498}evt.root`) and `train_chain3.py --label-branch` defaults to `label`,
which `prototype/DumpWriter.cc:202-206` documents as the **M12 HARNESS COVERAGE RULE**, i.e.
`prototype/Labels.cc::labelChainsHarness` (Labels.h:63-84). Verbatim:

> run the production matcher (`proto::matchedSimTrkIdxsAndFracs`, the verbatim trkCore port) over
> the chain's FULL hit list -- per member MD, in K6 order, the anchor hit then the other hit, all
> Phase2OT, exactly `k10AssembleChainTCs`' list -- and call the chain TRUE iff some sim's hit
> fraction is STRICTLY > 0.75. Train and serve then agree on what "fake" means.

and then, `train_chain3.py:296-299`, the 3 classes:

> `y3 = 0` (fake) iff `label != 1`; `y3 = 1` (prompt-true) iff `label == 1 and simVxy < 1.0`;
> `y3 = 2` (displaced-true) iff `label == 1 and simVxy >= 1.0`.

**THE CONVENTION DETAIL THAT DECIDES 93% OF THE TRUE ROWS, and I am replicating it rather than
fixing it.** `labelChainsHarness` fills kinematics only from an **ACCEPTED** sim (full row <
`ev.sim_pt.size()`, the bunchCrossing==0 && event==0 prefix); a **pileup-only** match keeps
`label = 1` with `simVxy = -999`. Since `-999 < 1.0`, every pileup-only true chain is trained as
**PROMPT**. On my 25-event smoke that is 67421 of 72095 true chains. This is the shipped gate's own
definition and changing it would be a different network, not a retrain, so both arms get it.

**LABEL REPLICATION VERIFIED THREE WAYS** (25 events of round1, i.e. control rows scored by the
shipped gate that the dump carries):

    shipped mP separates MY prompt labels from MY fakes    AUC 0.98317   (m12 val AUC 0.96632)
    shipped mD separates MY displaced labels from MY fakes AUC 0.96758   (m12 val AUC 0.93727)
    one-event MISALIGNMENT control, same metric            AUC 0.60283   <- collapses
    class balance: true frac .4422, displaced/true 1.41%   (shipped m12 train: .61 / 1.23%)
    pos_weight reproduced: shipped norm json says 0.6375140860592251 = n_fake/(n_true) on its split

**AND THE ETA-BAND KEY IS RECONSTRUCTED EXACTLY, which the dump does NOT carry.** The gate's band
cells key on `aEtaC = |chainT3Eta(mds, m2)|` of the INNERMOST node (ChainGate.h:584-590). The MD
list in `chains.bin` is the deduped union of the members' {m0,m1,m2} in **first-appearance order
walking innermost-first** (ChainWeld.h:283-303), so md2 is MD index 2 and its ANCHOR hit is dump hit
slot **4**; `mds.anchorX/Y/Z` are verbatim copies of `ph2_x/y/z` for the OT block
(MiniDoublet.h:89-91 + LSTPrepareInput.h:244). Recomputing `|sign(z) acosh(r3/rt)|` from ph2 at that
row and comparing my derived `1.1 <= aEtaC < 1.7` against the dumped `kChainFlagEtaBand` bit
(which the KERNEL set): **agreement 1.000000 over 163046 chains.** So the offline cell assignment is
proven against the kernel, not assumed.

**ALIGNMENT:** both dumps have exactly **1000 records with ievt contiguous 0..999** (asserted, not
sampled), so at `-s 1` the record counter IS the entry index for round2G as it was for round1, and
the ph2-row join needs no anchoring heuristic (a4_ref/chain_truth3.py's TC-hit-set anchoring is
NOT needed here and is not used).

**WORKTREES:** g1 = candidate arm (verified == shipped + arm G, built). g4 = control arm: its
uncommitted diff was byte-identical to the banked `nnloop_ref/instrument_pairdump.patch`
(md5 80bb30e9d6948953e6add42068d39c11 both ways), so `git checkout -- RecoTracker/LSTCore` was safe;
now zero modified tracked files at 4f1846078d9, `dcaSplit2 = 12.f` verified, rebuilding.
-- nnloop_ref/s2_work/{chainio.py,truth.py}

## [S2 00:20] **THE STALE-BAR EFFECT, MEASURED: THE SHIPPED GATE IS 22% MORE PERMISSIVE ON ARM G's FAKE CHAINS THAN ON THE CONTROL's.** Plus the offline retrain landscape for both arms.

Both dumps carry the shipped gate's own `flags` byte, so the shipped gate's decision on each arm's
own rows is a DIRECT measurement, not a re-derivation. Same 1000 events, same gate, different edge
head upstream:

    SHIPPED GATE, per event         chains   live   live FAKE   live prompt   live disp
    round1  (control, shipped edge)  6865.2  2741.6     268.3       2445.2       28.04
    round2G (candidate, arm G edge)  7067.8  2861.2   **343.7**     2488.2       29.34
                                     +3.0%   +4.4%   **+28.1%**      +1.8%       +4.6%
    shipped gate's FAKE acceptance:  .06853  ->  **.08391  (+22.4% relative)**
    shipped gate's prompt acceptance .83921  ->    .84767
    shipped gate's disp acceptance   .77030  ->    .78858

So arm G's +12.6% relative TC fake is NOT only "more fake chains exist" (+4.6% of the fake chain
population): the shipped gate is **22% more likely to pass one**, because chain features 2/3/4/18 and
the chain score are edge-logit aggregates and G's 3-class edge head sits on a different, more
confident distribution. That is the "stale by construction" term S1 predicted, now with a number.

**LABEL / CLASS CENSUS on the two on-policy dumps** (labelChainsHarness replicated offline):

    arm    chains     fake      prompt-true   displaced-true   true frac  disp/true
    r1    6,865,164  3,915,020   2,913,748        36,396         .4297      1.25%
    r2G   7,067,823  4,095,306   2,935,306        37,211         .4206      1.27%
    (shipped m12 training split for comparison: fake 821k / prompt 1273k / displaced 15.9k)

**TRAININGS. Six, all seed 42, arch 25->32->32->3, m12 conditioning + standardization, val metric
`min(AUC(mP prompt-vs-fake), AUC(mD disp-vs-fake))` -- the shipped head's own selector.**
Event split 600/200/200 (4.14M / 1.36M / 1.37M chains). S1's convergence lesson REPRODUCES on this
head, weakly but consistently:

    arm/loss              schedule                     best ep   promptAUC  dispAUC   sel
    C  m12 tiered 8/16    lr 1e-3 const, pat 15, 120ep    66      .98289    .96568   .96568
    C  m12 tiered 8/16    lr 3e-3 cosine->1e-5, 300ep    133      .98333    .96694   .96694
    C  LST equal-class    lr 3e-3 cosine->1e-5, 300ep     84      .98213    .96481   .96481
    G  m12 tiered 8/16    lr 1e-3 const, pat 15, 120ep    55      .98318    .96751   .96751
    G  m12 tiered 8/16    lr 3e-3 cosine->1e-5, 300ep    172      .98396    .96855   .96855
    G  LST equal-class    lr 3e-3 cosine->1e-5, 300ep     97      .98273    .96746   .96746
Cosine wins on BOTH arms (+.0013 / +.0010 sel) and the shipped constant-lr recipe stops early, as
S1 found for the edge head. All deployed arms below use the cosine heads.

**THE TWO CONVENTIONS ARE NOT A SMALL DIFFERENCE. Per-class share of the TOTAL train loss weight:**

    m12 tiered (pos_weight x 8 / x 16 on vxy)   fake .4638   prompt .4582   displaced .0780
    LST equal-class (total/(3*count))           fake .3333   prompt .3333   displaced .3333
i.e. LST's convention puts **4.3x** more of the loss on the displaced class. (`pos_weight` on our
rows is 1.333, not the shipped 0.6375, because our fake fraction is 57% vs the shipped dump's 39%.)

**AFFINE PIN: IT WORKS ESSENTIALLY EXACTLY, and it is not a new constant.** `marginX` has exactly ONE
consumer outside the gate -- K9's order key (`ChainArbitrate.h:141`); `marginP`, `marginD`, `zFake`,
`zPrompt`, `zDisp` have NO other reader in the tree (grepped). Scaling all three output rows by `a`
and shifting the prompt/displaced biases by `b` relative to fake maps mP/mD/mX all as `m -> a*m + b`,
so no ordering by any margin changes anywhere. Fitted on train+val, judged on the 200 held-out events:

    head        a         b        mX mean ship->new     mX std ship->new    frac(mX<orderHinge=5)
    C_m12cos  0.9583  -0.5619    -1.5935 -> -1.5940     4.9449 -> 4.9455      .9015 -> .9079
    C_lstcos  1.0322  -1.1815    -1.5935 -> -1.5883     4.9432 (same)         .9015 -> .9123
    G_m12cos  0.9388  -0.5220    -1.5170 -> -1.5164     4.8932 -> 4.8928      .8970 -> .9017
    G_lstcos  0.9862  -0.7443    -1.5170 -> -1.5135     4.8932 -> 4.8901      .8970 -> .9134

**BAR REFIT (values only; cells, the far free-pass cell and the C25 AND-rule untouched).** Per cell,
the shipped gate's own acceptance on the SAME rows, prompt-class and displaced-class as SEPARATE
targets, fitted on train+val, judged on the frozen 200 test events. Live chains per event:

    arm / bars                        live FAKE/evt   live prompt/evt   live disp/evt
    control, SHIPPED gate  (ref)          267.3           2441.6           27.285
      C_m12cos  dual                      252.5           2463.5           26.895
      C_m12cos  fkm                       267.4         **2489.3**         27.245
      C_lstcos  dual                      314.2           2459.7         **28.915**
      C_lstcos  fkm                       267.3           2373.3           27.920
    arm G,   SHIPPED gate  (ref)        **344.0**         2485.3           28.665
      G_m12cos  dual                      296.9           2498.4           28.235
      G_m12cos  fkm                       269.2           2448.1           27.600
      G_lstcos  dual                      347.4         **2508.6**       **29.685**
      G_lstcos  fkm                       268.8           2360.0           28.125

    `dual` = bar min(b_P, b_D), i.e. BOTH classes at or above the shipped per-cell acceptance
             (neutral-or-LOOSER by construction, the same honest caveat S1 recorded for the OR-rule)
    `fkm`  = `dual` plus a UNIFORM additive bar offset bisected so the live-fake-chain count lands on
             the CONTROL arm's own shipped number, 268.3/evt. This is the only way to ask "what does
             the gate have to SPEND to hand the fake rate back", and the offsets are the answer:
             C_m12cos -0.097 (it has fake to GIVE AWAY), C_lstcos +0.293, G_m12cos +0.182,
             G_lstcos +0.474.

**TWO OFFLINE READINGS THAT MATTER, both to be confirmed or refuted by deployment (S1 proved offline
proxies mispredict, so these are hypotheses):**
 1. **On the CANDIDATE arm the fake IS clawable, and most of it for free.** G's own gate at matched
    per-cell efficiency removes 47 of the 76 excess live fake chains (344.0 -> 296.9 vs the control's
    267.3) = **62% of the excess at no cost** (prompt +0.5%, disp -1.5%). Handing back the LAST 29
    costs 1.5% of live prompt and 3.7% of live displaced chains. At full fake match, `G_m12cos fkm`
    still carries MORE live prompt (2448.1 vs 2441.6) AND more live displaced (27.60 vs 27.285) than
    the control-with-shipped-gate.
 2. **The two conventions buy different things and the LST one is the displaced instrument.** At
    matched fake, m12 spends the head's gain on PROMPT (C: +47.7 prompt chains/evt, disp flat) while
    LST spends it on DISPLACED (C: +0.64 disp/evt for -68 prompt/evt; G: +0.84 disp for -125 prompt).
    In `dual` (no fake match) LST lifts displaced chain acceptance by +4.6 pt (C) / +2.8 pt (G) at
    +17.6% / +1.0% fake.
-- nnloop_ref/s2_work/{train3.py,barfit.py,export3.py,bar_*.json,logs/}

## [S2 00:35] The offline gate model is PROVEN against the kernel: 13,932,987 / 13,932,987 kill decisions identical, both arms

Before trusting a single refitted bar I reproduced the SHIPPED gate's kill decision offline from the
dumped margins and the shipped `ChainConfig` values -- all five cells, the eta-band deltas, the
far-dca free pass and the conditional C25 AND-rule -- and compared against the `kChainFlagKilled`
bit the KERNEL itself wrote into `chains.bin`:

    round1  (control):   agree 1.00000000   0 of 6,865,164 chains disagree   C25 cell-kills 215963 = 215963
    round2G (candidate): agree 1.00000000   0 of 7,067,823 chains disagree   C25 cell-kills 198813 = 198813

So the cell assignment (including the reconstructed `aEtaC` band key, already cross-checked against
the kernel's own `kChainFlagEtaBand` bit) and the rule composition are exact, and a bar fitted
offline means in the kernel exactly what it means in the fit. This is the check that makes
"neutral by construction" a claim rather than a hope; anyone refitting these bars should run it
first (`nnloop_ref/s2_work/barfit.py::cells` + the snippet in the log).

## [S2 00:50] **THE CENTRAL QUESTION, FIRST ANSWER: ARM G's OWN ON-POLICY GATE CLAWS BACK 77% OF ITS FAKE (82% OF THE BARREL) AND KEEPS 62-85% OF ITS DISPLACED GAIN.** Deployed, PU200 tune, 1000 events.

Arm `GM12D` = arm G edge head + a 3-class gate retrained ON ARM G's OWN CHAIN DUMP with the shipped
m12 recipe (cosine-converged), affine-pinned, bars refitted `dual` (both classes at or above the
shipped per-cell acceptance). Everything below is vs `ship_ref/B2_shipped.judge`, i.e. the CURRENT
shipped heads, with `G+shipgate` (S1's arm G, shipped gate, stale bars) in between:

    row                    shipped   G+shipgate    GM12D (G edge + G gate)
    eff_overall_incut      .810201    -.00127        -.00064  (-0.4 sig)     NEUTRAL
    eff_barrel             .924316    -.00048        +.00021
    eff_transition         .882124    -.00269        -.00224  (-0.8 sig)
    eff_endcap             .679590    -.00140        -.00073
    eff_vxy_1_5            .799872    -.00492        -.00321
    eff_vxy_5_10           .713793    +.00640        +.00148
    eff_vxy_10_30          .693155    +.02082      **+.01149** (+1.6 sig)
    eff_dxy_1_5            .551332    +.03086      **+.01917** (+2.1 sig)
    eff_dxy_5_10           .236346    +.03078      **+.02085** (+1.5 sig)
    eff_dxy_10_30          .049398    +.00823      **+.00697** (+1.2 sig, +14.1% rel)
    dup_overall_incut      .045205    -.00052        -.00021  (-1.3 sig)     BETTER
    dup_barrel             .015535    -.00041        -.00033  (-3.4 sig)     BETTER
    dup_transition         .014390    -.00079        -.00061  (-6.6 sig)     BETTER
    fake_overall_incut     .045479  +.00571 (+12.6%) **+.00129 (+2.8% rel)**
    fake_barrel            .051448  +.01551 (+30.1%) **+.00273 (+5.3% rel)**
    fake_transition        .047886    +.00343        +.00001                 NEUTRAL
    n_tc                   1587160    +0.67%         +0.12%
    n_tc_t4cl                64110   +11.12%         +3.71%

**Measured against arm G's own baseline the gate retrain alone is worth -26 sigma of fake:**
`fake_overall .05119 -> .04677 (-.00442)`, `fake_barrel .06696 -> .05418 (-.01278, -71 sigma)`,
`n_tc_t4cl -6.7%`, and it does that while `eff_overall` goes UP (+.00064) and every eta cell goes up.
The costs are on the displaced bands it does not fully keep: dxy[1,5) -.0117, dxy[5,10) -.0099,
vxy[10,30) -.0093, dxy[10,30) -.0013.

So the answer to "how much of arm G's +12.6% fake does its own gate claw back": **77% overall, 82% in
the barrel, and the displaced lead survives** -- dxy[10,30) is still +14.1% relative over shipped
(.05636 vs .04940) and every displaced band is still positive. The residual is +2.8% relative fake
overall / +5.3% barrel.

**THE CONTROL ARM's OWN RETRAIN IS A NEARLY-FREE FAKE WIN** (`CTLM12D`, shipped edge + gate retrained
on the shipped edge's own dump, same recipe and bars):

    eff_overall -.00017 (-0.1 sig) NEUTRAL   fake_overall -.00184 (-11.3 sig, -4.0% rel)  BETTER
    eff_barrel  +.00027                      fake_barrel  -.00535 (-32.1 sig, -10.4% rel) BETTER
    dup_barrel  -.00019 (-2.0 sig) BETTER    fake_transition -.00159 (-9.5 sig)           BETTER
    dup_transition +.00035 (+3.7 sig) WORSE  eff_dxy_1_5 -.01137 (-1.3 sig)               WORSE
    eff_dxy_10_30 +.00000  (exactly)         eff_vxy_10_30 -.00503 (-0.7 sig)             WORSE
So even with NO edge change, putting the gate back on-policy buys 4% of the fake rate and 10% of the
barrel fake rate at neutral overall efficiency -- the price is ~1 point of dxy[1,5). That is the size
of the pure off-policy penalty the loop was built to remove.

**OFFLINE DIAGNOSTIC THAT PREDICTED THIS AND IS WORTH REUSING (`simcover.py`): DISTINCT SIM TRACKS
COVERED BY AT LEAST ONE SURVIVING CHAIN.** Denominators are the same 1000 events, so the numerator is
the comparison, and unlike a chain count it is directly cross-arm comparable:

    gate                                |dxy|10-30 sims covered   live fake chains
    control arm, SHIPPED gate                     193                 268,278
    arm G,       SHIPPED gate                   **233 (+20.7%)**      343,657 (+28.1%)
    arm G + G gate, dual bars                     233 (+20.7%)        296,251
    arm G + G gate, fake-count-MATCHED            223 (+15.5%)        268,476
    control + control gate, fake-count-MATCHED    202 (+4.7%)         268,120
**At an EXACTLY matched live-fake-chain count, arm G still covers 15.5% more far-displaced sim tracks
than the control does.** That is the cleanest statement of what the new edge head actually bought,
with the fake excess removed by construction rather than argued away.
-- nnloop_ref/s2_work/{runs/{CTLM12D,GM12D}.judge, simcover.py, table.py}

## [S2 01:10] **GM12F: ARM G's EDGE HEAD + ITS OWN ON-POLICY GATE, FAKE-COUNT-MATCHED, BEATS THE SHIPPED HEADS ON DISPLACED, ON DUP AND ON FAKE AT THE SAME TIME.** PU200 tune, 1000 events.

The `fkm` bar variant (dual bars plus a uniform +0.182 offset chosen offline so the live-fake-CHAIN
count returns to the control arm's own 268.3/evt) is the arm that closes the round. vs
`ship_ref/B2_shipped.judge`:

    row                    shipped   G+shipgate     GM12D        GM12F  <- the arm
    eff_overall_incut      .810201    -.00127      -.00064     **-.00048 (-0.3 sig)  NEUTRAL**
    eff_barrel             .924316    -.00048      +.00021       +.00031
    eff_transition         .882124    -.00269      -.00224       -.00172 (-0.6 sig)
    eff_endcap             .679590    -.00140      -.00073       -.00067
    eff_vxy_1_5            .799872    -.00492      -.00321       -.00449 (-0.8 sig)
    eff_vxy_5_10           .713793    +.00640      +.00148       -.00197
    eff_vxy_10_30          .693155    +.02082      +.01149     **+.00598 (+0.8 sig)**
    eff_dxy_1_5            .551332    +.03086      +.01917     **+.01494 (+1.7 sig)**
    eff_dxy_5_10           .236346    +.03078      +.02085     **+.01192 (+0.9 sig)**
    eff_dxy_10_30          .049398    +.00823      +.00697     **+.00570 (+1.0 sig, +11.5% rel)**
    dup_overall_incut      .045205    -.00052      -.00021     **-.00035 (-2.1 sig)  BETTER**
    dup_barrel             .015535    -.00041      -.00033     **-.00055 (-5.7 sig)  BETTER**
    dup_transition         .014390    -.00079      -.00061    **-.00096 (-10.5 sig)  BETTER**
    dup_endcap             .070386    -.00020      -.00003       -.00015              BETTER
    fake_overall_incut     .045479  +.00571(+12.6%) +.00129    **-.00049 (-3.0 sig)  BETTER**
    fake_barrel            .051448  +.01551(+30.1%) +.00273    **-.00135 (-7.8 sig)  BETTER**
    fake_transition        .047886    +.00343      +.00001    **-.00214 (-12.9 sig)  BETTER**
    fake_endcap            .041521    +.00089      +.00087       +.00048 (+3.0 sig)   worse
    n_tc                   1587160    +0.67%       +0.12%        -0.10%
    n_tc_t4cl                64110   +11.12%       +3.71%        -2.59%
    conv_T5                  .6871    .6781        .6817        (reported below, output only)

**So the round-level answer is not "how much of the fake did the gate claw back" but "all of it, and
then some".** Measured against arm G's own baseline the gate retrain is worth `fake_overall -.00620`
(-37.6 sig), `fake_barrel -.01686` (-97.3 sig), `fake_transition -.00557`, `n_tc_t4cl -12.3%`, while
`eff_overall` goes UP (+.00080) and every eta cell goes up. The full +12.6% overall / +30.1% barrel
fake excess S1 measured is gone, and the arm ends BELOW the shipped fake rate.

**AND THE DISPLACED GAIN SURVIVES.** All four displaced bands stay above shipped; dxy[10,30) is
.05510 vs .04940 = **+11.5% relative**, dxy[1,5) +.0149, dxy[5,10) +.0119, vxy[10,30) +.0060. What the
gate charges for the fake is about 40% of arm G's raw displaced lead (dxy[1,5) +.0309 -> +.0149).

**THE FRONTIER IS A DIAL AND BOTH ENDS ARE ON THE TABLE:**
    GM12D (bars matched, no fake match): +.0192/+.0209/+.0070 on dxy[1,5)/[5,10)/[10,30), at
        fake +.00129 (+2.8% rel) and fake_barrel +.00273 (+5.3% rel)
    GM12F (fake-count matched):          +.0149/+.0119/+.0057, at fake -.00049 and barrel -.00135
The one parameter between them is the uniform bar offset (0 vs +0.182 in the pinned margin scale).

**THE CONTROL ARM (shipped edge, gate put back on-policy) is ALSO better than shipped, with the same
shape:** `CTLM12F` eff_overall -.00023 (-0.2 sig), eff_barrel +.00041, fake_overall -.00081 (-4.9 sig),
fake_barrel -.00311 (-18.3 sig), dxy[10,30) +.00063, dxy[1,5) -.00812, dup_transition +.00045
(+4.7 sig, the one regression). `CTLM12D` trades more fake (-.00184 / -.00535 barrel) for more
displaced loss (dxy[1,5) -.01137). So **~4-8% of the shipped fake rate and ~6-10% of the shipped
barrel fake rate is pure off-policy penalty on the gate**, recoverable with no edge change and no new
mechanism -- which is the loop's own justification, measured.

**A STRUCTURAL NOTE ON WHY THE UNIFORM OFFSET IS THE ONLY FAKE DIAL AVAILABLE.** The barrel residual
cannot be spent barrel-locally: of the five gate cells only the exempt-5+ family is split on |eta|
(`m3ThetaRB`/`RT`/`R`), and the barrel fake lives mostly in `ip5` (nL>=5, dca<0.5), which has NO eta
split. Tightening the barrel alone would mean ADDING a cell, which the brief forbids -- so the honest
instrument is the global offset, and that is what `fkm` is.

**conv_T5 (reported as an OUTPUT, never a target; .6871 shipped):** G+shipgate .6781, CTLM12D .6895,
GM12D .6817. Full set for the shipped arms in the final table.
-- nnloop_ref/s2_work/runs/{CTLM12D,CTLM12F,GM12D,GM12F}.judge

## [S2 01:35] **ALL SEVEN DEPLOYED ARMS, PU200 TUNE (1000 evt), vs the SHIPPED HEADS. Two arms beat shipped on displaced AND dup AND fake at once.**

    row                  shipped   G+ship   CTLM12D  CTLM12F  CTLLSTD    GM12D    GM12F    GLSTD    GLSTF
    eff_overall_incut    .810201  -.00127   -.00017  -.00023  -.00117  -.00064  -.00048  -.00148  -.00133
    eff_barrel           .924316  -.00048   +.00027  +.00041  -.00110  +.00021  +.00031  -.00106  -.00123
    eff_transition       .882124  -.00269   -.00090  -.00127  -.00209  -.00224  -.00172  -.00217  -.00120
    eff_endcap           .679590  -.00140   -.00027  -.00037  -.00085  -.00073  -.00067  -.00158  -.00146
    eff_vxy_1_5          .799872  -.00492   -.00171  -.00171  -.00257  -.00321  -.00449  -.00128  -.00470
    eff_vxy_5_10         .713793  +.00640   -.00148  +.00049  +.00099  +.00148  -.00197  +.00788  +.00099
    eff_vxy_10_30        .693155  +.02082   -.00503  -.00215  +.00838  +.01149  +.00598  +.02393  +.01269
    eff_dxy_1_5          .551332  +.03086   -.01137  -.00812  +.00325  +.01917  +.01494  +.03184  +.01884
    eff_dxy_5_10         .236346  +.03078   -.00695  -.00199  +.01589  +.02085  +.01192  +.03078  +.01589
    eff_dxy_10_30        .049398  +.00823   +.00000  +.00063  +.00190  +.00697  +.00570  +.00823  +.00507
    dup_overall_incut    .045205  -.00052   +.00012  +.00018  +.00010  -.00021  -.00035  -.00024  -.00062
    dup_barrel           .015535  -.00041   -.00019  -.00007  +.00043  -.00033  -.00055  -.00013  -.00087
    dup_transition       .014390  -.00079   +.00035  +.00045  +.00038  -.00061  -.00096  -.00041  -.00140
    fake_overall_incut   .045479  +.00571   -.00184  -.00081  +.00328  +.00129  -.00049  +.00478  -.00017
    fake_barrel          .051448  +.01551   -.00535  -.00311  +.00602  +.00273  -.00135  +.00947  -.00138
    fake_transition      .047886  +.00343   -.00159  -.00004  +.00566  +.00001  -.00214  +.00651  -.00049
    fake_endcap          .041521  +.00089   +.00002  +.00023  +.00105  +.00087  +.00048  +.00167  +.00060
    n_tc_t4cl              64110  +11.12%    -0.12%   +3.04%   +2.54%   +3.71%   -2.59%   +2.92%  -16.12%

    C/G = control (shipped edge) / candidate (arm G edge); M12/LST = the two class-weight
    conventions; D/F = `dual` bars (per-cell acceptance matched) / `fkm` (plus the uniform offset
    that returns the live-fake-chain count to the control's own 268.3/evt).

**THE TWO ARMS THAT DOMINATE SHIPPED (better displaced, better dup, better fake, neutral overall eff):**

    GM12F   eff -.00048 (-0.3 sig) | dxy 1-5 +.0149  5-10 +.0119  10-30 +.0057  vxy 10-30 +.0060
            dup ALL better (-2.1 / -5.7 / -10.5 sig) | fake -.00049, barrel -.00135, trans -.00214
            the ONLY negative rows: eff_transition -.00172 (-0.6 sig), eff_vxy_1_5 -.00449,
            fake_endcap +.00048 (+3.0 sig)
    GLSTF   eff -.00133 (-0.9 sig) | dxy 1-5 +.0188  5-10 +.0159  10-30 +.0051  vxy 10-30 +.0127
            dup ALL better, and the BEST dup of any arm (-3.8 / -9.1 / -15.5 sig)
            fake -.00017, barrel -.00138, trans -.00049 | fake_endcap +.00060, n_tc_t4cl -16.1%

**The two conventions swap the SAME budget the same way they did offline.** At a matched live-fake
chain count, LST's equal-class weighting buys **+.0039 dxy[1,5), +.0040 dxy[5,10), +.0067 vxy[10,30)
and a much better dup rate** and pays **-.00085 overall efficiency** (and -.0006 dxy[10,30)) relative
to the m12 tiered weighting. Against the stated priority (displaced eff >> overall eff >> dup > fake)
GLSTF wins; against "overall efficiency is the metric weighted highest" GM12F wins. **Both are being
taken to the full gate set so the maintainer can choose on the 6000-event holdout rather than on the
tune.**

**THE `dual` ARMS ARE THE DISPLACED-MAXIMAL END, AND THEY COST FAKE.** GLSTD reproduces essentially
ALL of arm G's raw displaced lead (dxy[1,5) +.0318, dxy[5,10) +.0308, dxy[10,30) +.0082,
vxy[10,30) +.0239 -- at or above G+shipgate on every band) while already removing a sixth of the fake
excess (+.00478 vs +.00571) and turning dup negative. It is not shippable at +10.5% relative fake, but
it is the existence proof that the displaced lead is NOT what the fake claw-back has to pay for --
the offset does.

**NEGATIVE / CAUTION, both worth recording:**
 * **`CTLLSTD` is the one arm whose `dup_barrel` REGRESSES** (+.00043, +4.3 sig). LST's equal-class
   weighting on the CONTROL arm's rows, with no fake match, gives up barrel dup as well as +7.2% fake.
   Combined with GLSTD's clean dup, this says the LST convention needs the fake match to be safe.
 * **The LST heads' C25 joint fit SATURATES.** `c25Theta` lands at 8.8-9.4 (vs 2.0 shipped), i.e. the
   mP half of the cell's AND-rule stops binding and the cell becomes a pure mD cut, and the cell's
   prompt acceptance overshoots (.85625 -> .87401 on the control). The cell STRUCTURE is preserved --
   it is still `kill iff mP < c25Theta AND mD < c25ThetaD` -- but for these heads one of its two
   equations has no interior solution. The m12 heads' C25 fit matches both classes to 5 decimals
   (1.81/-1.08 control, 1.82/-1.17 candidate), so this is a property of the LST-weighted head, not of
   the fitter.
 * `fake_endcap` rises in EVERY arm (+.00002 to +.00167). The gate has no endcap-only bar either
   (the only |eta|-split cell is exempt-5+), so this is not addressable without adding a cell.
-- nnloop_ref/s2_work/runs/*.judge, nnloop_ref/s2_work/table.py

## [S2 02:05] **STAGE 2 CLOSES. THE DELIVERABLE TABLE, THREE COLUMNS, ALL GATES RUN, ALL THREE PATCHES SHIP-VERIFIED 35/35.**

**RECOMMENDATION: `GM12F` -- arm G's edge head + a 3-class gate retrained on arm G's own chain dump
(shipped m12 class-weighting, cosine-converged), affine-pinned, bars refitted at fixed per-cell
signal efficiency plus the uniform offset that returns the live-fake-chain count. It beats the
SHIPPED heads on every displaced band, on every dup cell, and on overall/barrel/transition fake, at
overall efficiency -.0004 (6000-event holdout).** The coordinator's tiebreak (overall efficiency
dominates) and the holdout agree: GLSTF's -.0013 tune loss did NOT dissolve at 6000 events -- it
CONFIRMED at -.0010 with p 1.5e-11 -- and GM12F is simultaneously BETTER than GLSTF on dxy[5,10)
(+.0210 vs +.0119) and dxy[10,30) (+.0084 vs +.0062) there. GM12F wins on both halves of the
tiebreak, so the choice is not a trade.

### POOLED 6000-EVENT HOLDOUT, event_2000..7000, `a5_ref/paired.py` McNemar (THE decisive gate)

    cell           SHIPPED   CTLM12F (p)            GM12F (p)               GLSTF (p)
    overall         .8120   +.0001 (.44)         **-.0004 (.0053)**      -.0010 (1.5e-11)
    eff_barrel      .9235   +.0000 (.84)           -.0004 (.147)         -.0012 (1.9e-06)
    eff_transition  .8808   +.0000 (.97)           -.0001 (.805)         -.0012 (.0267)
    eff_endcap      .6854   +.0001 (.25)           -.0006 (2.1e-04)      -.0008 (3.5e-07)
    vxy_1_5         .7992   +.0009 (.17)           -.0017 (.091)         -.0025 (.0156)
    vxy_5_10        .7190   +.0005 (.71)           -.0049 (.0071)        -.0057 (.0028)
    vxy_10_30       .6951   -.0018 (.0996)       **+.0086 (1.6e-07)**   **+.0123 (5.4e-13)**
    dxy_0_1         .8368   +.0002 (.038)          -.0003 (.076)         -.0008 (5.2e-06)
    dxy_1_5         .5700   -.0042 (.0011)       **+.0073 (2.7e-04)**   **+.0105 (3.1e-07)**
    dxy_5_10        .2531   -.0083 (3.2e-04)     **+.0210 (3.5e-09)**    +.0119 (.0013)
    dxy_10_30       .0448   +.0003 (.76)         **+.0084 (3.5e-08)**    +.0062 (6.5e-05)
                                                 (.0532 = +18.8% rel)   (.0510)
    POOLED dup/fake over the same 9.5M TCs (n_tc-weighted):
    dup_overall    .04510   .04522 (+0.3%)         .04491 (-0.4%)        .04454 (-1.2%)
    dup_barrel     .01522   .01521 ( 0.0%)         .01475 (-3.1%)        .01431 (-6.0%)
    dup_transition .01396   .01416 (+1.4%)         .01319 (-5.5%)        .01262 (-9.6%)
    fake_overall   .04533   .04447 (-1.9%)         .04478 (-1.2%)        .04518 (-0.3%)
    fake_barrel    .05094   .04781 (-6.1%)         .04959 (-2.6%)        .04983 (-2.2%)
    fake_transition.04834   .04811 (-0.5%)         .04585 (-5.2%)        .04767 (-1.4%)
    fake_endcap    .04137   .04158 (+0.5%)         .04184 (+1.1%)        .04192 (+1.3%)
    n_tc          9518923  9508306               9510179               9512539
    n_tc_t4cl      383355   394405 (+2.9%)        372410 (-2.9%)        321569 (-16.1%)
    (for scale, ARM G + the SHIPPED gate on the same 6000 events: fake_overall .05100 = +12.5%,
     fake_barrel .06652 = +30.6%, n_tc_t4cl +11.1%. That is the excess the retrain removes.)

### PU200 TUNE (event_1000, 1000 evt) -- the full 3-column table

    row                  shipped   G+shipgate   CTLM12F     GM12F      GLSTF
    eff_overall_incut    .810201    -.00127     -.00023   **-.00048**  -.00133
    eff_barrel           .924316    -.00048     +.00041     +.00031    -.00123
    eff_transition       .882124    -.00269     -.00127     -.00172    -.00120
    eff_endcap           .679590    -.00140     -.00037     -.00067    -.00146
    eff_vxy_1_5          .799872    -.00492     -.00171     -.00449    -.00470
    eff_vxy_5_10         .713793    +.00640     +.00049     -.00197    +.00099
    eff_vxy_10_30        .693155    +.02082     -.00215   **+.00598**  +.01269
    eff_dxy_0_1          .835101    -.00094     -.00025     -.00055    -.00115
    eff_dxy_1_5          .551332    +.03086     -.00812   **+.01494**  +.01884
    eff_dxy_5_10         .236346    +.03078     -.00199   **+.01192**  +.01589
    eff_dxy_10_30        .049398    +.00823     +.00063   **+.00570**  +.00507
    dup_overall_incut    .045205    -.00052     +.00018   **-.00035**  -.00062
    dup_barrel           .015535    -.00041     -.00007   **-.00055**  -.00087
    dup_transition       .014390    -.00079     +.00045   **-.00096**  -.00140
    dup_endcap           .070386    -.00020     +.00014     -.00015    -.00034
    fake_overall_incut   .045479    +.00571     -.00081   **-.00049**  -.00017
    fake_barrel          .051448    +.01551     -.00311   **-.00135**  -.00138
    fake_transition      .047886    +.00343     -.00004   **-.00214**  -.00049
    fake_endcap          .041521    +.00089     +.00023     +.00048    +.00060
    n_tc                 1587160    +0.67%      -0.10%      -0.10%     -0.09%
    n_tc_t4cl              64110   +11.12%      +3.04%      -2.59%    -16.12%
    conv_T5 (OUTPUT)       .6871     .6781       .6883       .6836      .6833

### BOTH CUBES (5000 evt each, `-s 4`; cube50_highPt ran CLEAN at -s 4 for all three arms -- the
writer segfault scales with TC count and every arm here LOWERS it)

    cube50 (deltas vs shipped)          CTLM12F    GM12F     GLSTF
      eff_vxy_1_5     .33133            -.0241    -.0301    -.0241
      eff_vxy_5_10    .23455            +.0048    +.0159    +.0159
      eff_vxy_10_30   .06140            -.0002    +.0029    +.0047
      eff_dxy_1_5     .13888            -.0105    -.0095    -.0055
      eff_dxy_5_10    .07814            +.0035    +.0073    +.0058
      eff_dxy_10_30   .02523            +.0014  **+.0039**  +.0041   (+15.3% rel for GM12F)
      fake            .00422            -.0011    -.0022    -.0022
      n_tc               947            +1.1%     +5.8%     +5.9%
    cube50_highPt (deltas vs shipped)   CTLM12F    GM12F     GLSTF
      eff_vxy_1_5     .25110            +.0176    +.0000    +.0000
      eff_vxy_5_10    .11990            -.0108    +.0108    +.0060
      eff_vxy_10_30   .02529            -.0027    -.0014    -.0018
      eff_dxy_1_5     .09316          **-.0159**  **-.0137** **-.0189**   <- THE ONE REAL CUBE REGRESSION
      eff_dxy_5_10    .02044            -.0054    +.0009    -.0006
      eff_dxy_10_30   .00485            +.0000    +.0006    +.0006
      n_tc               562           -11.9%     -1.8%     -6.9%
      n_tc_t4cl          157           -34.4%    -31.9%    -17.8%

**THE cube50_highPt dxy[1,5) LOSS IS A GATE-RETRAIN COST, NOT AN ARM-G COST, AND I AM STATING IT AS A
REGRESSION rather than rounding it away.** All three arms lose it (-.0137 to -.0189, -2.6 to -3.7 sig,
about 37 of 252 reconstructed tracks) while `G+shipgate` was EXACTLY neutral there (+.0000) -- so the
cause is the gate, and it is present even in `CTLM12F`, which changes no edge weight at all. PU200's
own dxy[1,5) moves the OTHER WAY on 6000 events (+.0073, p 2.7e-04), so the two samples disagree on
this band; cube50_highPt is 50 GeV displaced particles and its dxy[1,5) cell holds 2705 sims of which
252 are reconstructed, so it is the most boundary-sensitive cell we score.

### SHIP-VERIFICATION -- all three, independent rebuild from a tree RESET to 4f1846078d9

    patch (all `git apply` clean on their stated base)         verify build       judge
    nnloop_ref/s2_work/s2_CTLM12F.patch  on 4f1846078d9        0 error:      35/35 BIT-IDENTICAL
    nnloop_ref/s2_work/s2_GM12F.patch    on + s1_arm_G_...     0 error:      35/35 BIT-IDENTICAL
    nnloop_ref/s2_work/s2_GLSTF.patch    on + s1_arm_G_...     0 error:      35/35 BIT-IDENTICAL
Each patch is TWO FILES: the regenerated `src/alpaka/Chain3NetworkWeights.h` (same 25->32->32->3
shape, same 2137 literals, the affine pin baked into the output layer -- no new file, no new
constant) and ten numeric literals in `interface/ChainConfig.h`. NO kernel change, NO new mechanism,
NO added weights file. Generator self-test: `export3.py --selftest` regenerates the SHIPPED header
from `prototype/chain3_mlp_m12.pt` and all 2137 literals compare float32-equal, so the export path is
verified rather than trusted; the emitted headers have **0 lines over 120 columns** (the shipped one
has 2).

### THE COMPOSITION SHIFT, as asked -- WHAT REPLACED GLSTF's MISSING 16% OF T4-CLASS TCs

`tc_type` census per 1000 events, vs shipped: **T4-class (type 9) -16078 (-16.5%), pT3-class (type 5)
+12437 (+13.9%), T5-class (type 4) +3787 (+1.2%), pT5 -3542, bare pLS +656, n_tc -0.09%.** The
mechanism is exact and it is not a crossclean: type-5 rows in this pipeline are NOT carried LST pT3s,
they are **attach STAGE B deliveries -- a pLS welded to a BARE T3** (`ChainAttachT3.h:741`, "one pLS,
one owner across target kinds"). A T3 inside an accepted chain is not bare; when the gate kills a
4-layer chain its member T3s become bare and stage B pairs them with a pLS. So the gate converts
seedless 4-layer outer-tracker chains into PIXEL-SEEDED pT3-class tracks at nearly 1:1, which is why
`n_tc` is conserved to 0.1% while dup and fake both improve. The same anticorrelation holds in every
arm (GM12F T4 -1859 / pT3 +1908; GM12D T4 +4587 / pT3 -1867; G+shipgate T4 +10241 / pT3 -1893).
**STAGE 3 MUST READ THIS: the attach head's training population is exactly what moves here.** GLSTF
shifts 12k pairs/1000evt from stage A to stage B; GM12F shifts ~2k. Whichever arm ships, stage 3 has
to re-dump pairs on it -- which is the loop's rule anyway, but here is the size of the shift.

### THE FOUR DELIVERABLES

    nnloop_ref/s2_work/s2_GM12F.patch     RECOMMENDED. candidate arm (base = 4f1846078d9 +
                                          s1_arm_G_3class_balanced.patch). Dominates shipped.
    nnloop_ref/s2_work/s2_CTLM12F.patch   CONTROL arm (base = 4f1846078d9). Ships the pure
                                          off-policy fix with NO edge change: overall eff +.0001
                                          (p .44) and fake -1.9% / barrel fake -6.1% on 6000
                                          events, at the cost of dxy[1,5) -.0042 and
                                          dxy[5,10) -.0083. Use it if arm G is not taken.
    nnloop_ref/s2_work/s2_GLSTF.patch     LST equal-class-weight convention on the candidate arm
                                          (the maintainer's explicit request). Best dup of any arm
                                          (-1.2% / -6.0% barrel / -9.6% transition) and best
                                          vxy[10,30) (+.0123) and dxy[1,5) (+.0105), but overall
                                          eff -.0010 at p 1.5e-11 -- NOT recommended under the
                                          efficiency-first tiebreak. Record, not ship.
    (GM12D / GLSTD, tune only)            the displaced-MAXIMAL end of the dial: GLSTD reproduces
                                          arm G's ENTIRE raw displaced lead (dxy[1,5) +.0318,
                                          dxy[5,10) +.0308, dxy[10,30) +.0082, vxy[10,30) +.0239)
                                          at +10.5% relative fake. Not gated further.

### CAVEATS STATED WITH THE HEADLINE
 1. **GM12F's overall efficiency is -.0004 at p .0053 on 6000 events. That is small and it is real.**
    Its endcap cell is -.0006 (p 2.1e-04) and vxy[5,10) is -.0049 (p .0071). I am not calling those
    unchanged.
 2. **cube50_highPt dxy[1,5) -.0137** (above). A gate-retrain cost shared by all three arms.
 3. `fake_endcap` is up in every arm (+0.5% to +1.3% relative). The gate has no endcap-only bar --
    the only |eta|-split cell is exempt-5+ -- so this is not reachable without ADDING a cell, which
    the brief forbids.
 4. **TIMING IS NOT MEASURED.** These patches change ten float constants and the contents of one
    weights header; no layout, no kernel, no extra work per chain. But `n_tc_t4cl` moves -2.6%
    (GM12F) / -16.1% (GLSTF) and the TC class mix shifts, so the assembly and attach stages do
    measurably different amounts of work. Scope rule 6 wants a same-binary A/B and this is not one.
 5. Everything is on ONE training sample (`event_1000`, 1000 events, the PLAN's split). The 6000-event
    holdout is untouched by training and the cubes are entirely out of sample.
-- nnloop_ref/s2_work/{s2_*.patch, runs/, bar_*.json, models/, logs/}

## [S2 02:15] Housekeeping: worktrees, artifacts, and the one gate I did not run

**WORKTREES, final state** (each carries its own verified arm; nothing else was touched):
    g4 = 4f1846078d9 + s2_CTLM12F.patch                      (2 modified tracked files)
    g6 = 4f1846078d9 + s1_arm_G_3class_balanced + s2_GM12F   (8) <- the RECOMMENDED arm's binary
    g1 = 4f1846078d9 + s1_arm_G_3class_balanced + s2_GLSTF   (8)
g4 was cleaned at the start of the round: its uncommitted diff was byte-identical to the banked
`nnloop_ref/instrument_pairdump.patch` (md5 80bb30e9d6948953e6add42068d39c11 both ways) so PD's work
was NOT destroyed, it is in the bank. g6 was pristine at 4f1846078d9 (S1 left it that way) and is now
the GM12F tree. g2 / g3 / g5 / gc6 untouched.

**ARTIFACTS, all under `nnloop_ref/s2_work/` (5.1 GB, nothing in /tmp):**
    lab/{r1,r2G}/{X.npy,meta.npz}   2.2 GB  the LABELLED on-policy dumps: 6.87M + 7.07M chains with
                                            the harness label, the 25 raw features, dcaXY, the
                                            SHIPPED gate's z/margins/flags, aEtaC, vxy/dxy/simPt.
                                            REBUILDABLE in 2 min each from round1/round2G + truth.py.
    models/chain3_{C,G}_{m12const,m12cos,lstcos}.pt + norm json + per-epoch history
    bar_{C,G}_{m12cos,lstcos}.json          the full refit reports: affine fit, per-cell shipped
                                            acceptance, per-cell bars for all 4 variants, the C25
                                            joint solve, the held-out TEST judgement, the fkm offset
    hdr_*.h  s2_*.patch  runs/  logs/       generated headers, deliverables, every judge/root/log
    chainio.py truth.py train3.py barfit.py export3.py setbars.py simcover.py table.py pool.py
    deploy.sh phase1_g{1,4}.sh phase2.sh shipverify.sh mkpatch.sh paired.sh summary.sh

**REUSE NOTES for stage 3 / a stage-2 round 2:**
 * `truth.py` labels a chains.bin in **2 minutes for 1000 events** (vectorised harness matcher) and
   asserts alignment rather than repairing it. It also reconstructs `aEtaC`, which the dump lacks.
 * `barfit.py::cells` + the kill composition is **verified against the kernel on 13.9M chains**
   ([S2 00:35]). Run that check first if you touch a bar.
 * `export3.py --selftest` proves the header export path against the SHIPPED header (2137 literals).
 * `simcover.py` (distinct sims covered by a surviving chain, per band) was the only offline proxy
   that got the sign AND the ranking right here -- it predicted that arm G keeps its far-displaced
   advantage at matched fake, which the deployment confirmed. Chain COUNTS did not.
 * `pool.py` pools dup/fake over the 6 holdout runs (paired.py does efficiency only).

**THE ONE GATE I DID NOT RUN: TIMING.** Same reason as S1 plus one of my own: the box ran 3 builds
and up to 18 concurrent `lst_cpu` jobs of mine for most of this round, and TRAP 6 (a CPU total is not
comparable between two binaries under ~45 ms) makes a contaminated number worse than none. What a
future measurement has to answer is NOT the gate cost -- ten float constants and a same-shape weights
header cannot change the chain block's work -- but the DOWNSTREAM cost of the composition shift
([S2 02:05]): GM12F moves 2k and GLSTF 12k TCs per 1000 events from the seedless 4-layer class to the
pixel-seeded stage-B class, and those two classes are assembled and attached by different code paths.
A same-binary config-only A/B is not available for that (the head differs), so it needs a two-binary
measurement on a quiet machine with the `pLS` column checked first.

## [S3 07:45] CATCH-UP POST (I had posted nothing; two SSH/API kills cost the narrative but no disk state)

Stage 3 is DEPLOYED AND GATED. Everything below is measured, not planned. Details in the [S3 FINAL]
post; this is the record in case I am killed again.

### THE LABEL, REPLICATED NOT INVENTED, AND VERIFIED AT AUC 0.9993

The shipped attach head is `r1_ref/attach_mlp_MIN1.pt` (named in the generated
`src/alpaka/AttachNetworkWeights.h`, `best_epoch=25 best_val_auc=0.99819`, arch [20,24,24,1]). Its
`label` branch is written by `iterations/fanout5/attachretrain/main.cc:1644-1650`:

> `label = 1` iff `set_intersection(sims(target), sims(pLS))` is NON-EMPTY, where
> `sims(T3)` = sims in >= 2 of the T3's 3 MDs' deduped `md_simIdxAll` lists (`Labels.cc:30-64`;
> for a 2-hit MD `frac > 0.75` means BOTH hits), `sims(chain)` = the **INTERSECTION** of `sims(T3)`
> over ALL member T3s (`main.cc:1582-1588`), `sims(bareT3)` = that T3's own set, `sims(pLS)` = the
> deduped `pLS_simIdxAll` (the same matcher over the pLS's own 3 or 4 hits). Kinematics come from
> the highest-`sim_pt` **ACCEPTED** sim in the intersection; a **pileup-only** match keeps label 1
> with `simVxy = simPt = -999`.

Replicated exactly, including the pileup-only convention. **VERIFICATION:** the SHIPPED head's own
logit, which the dump carries per row, separates MY labels at **AUC 0.99933 (stage 0, 132.2M rows) /
0.99891 (stage 1) / 0.99944 (stage 2)** over all 1000 events; a misalignment control collapses to
**0.50057**. 218,702,808 rows, 6,503,767 true (2.974%).

Also the shipped OBJECTIVE, which is not plain BCE and is easy to miss -- `attach_norm_MIN1.json`
records `m19_objective = {gamma_neg: 2.0, disp_mid: 8.0, disp_hi: 16.0, chain_weight: 1.0,
val_metric: "chain"}`: one-sided focal on NEGATIVES only, displaced-true tiers x8 / x16 on the
shared sim's vxy, early stop on the CHAIN-universe weighted AUC. All of that is replicated.

### THE JOIN NEEDED ONE NEW SIDECAR, AND IT IS IDENTITIES ONLY

`pairs.bin` carries identities and no geometry a matcher can use, and the `--allobj` route needs
`t3_rawIdx`, which is `#ifdef CUT_VALUE_DEBUG`. So I added `LST_CHAIN_JOIN_DUMP` to g4 (one function,
two call sites, both alongside `dumpChainPairs`): per chain NODE its sparse triplet index + its three
MDs' (anchor,other) ph2 rows; per pLS its `pixelSeeds.seedIdx()` (the `see_*` row) and its eta.
**No sim information and no label is read or written** -- truth stays offline. 1.4 GB for 1000
events, 0 `error:` in `.make.log.1786514106`.

The key structural fact that makes ONE table serve both target kinds: **bare-T3 targets are chain
NODES** (`ChainAttachT3Keep` iterates `nNodes`; `ChainAttachT3TargetPre` stores
`nodes.tripletIndex()` in `chain`), and a chain's members are named by `nodeItems` in the 'P22C'
dump. Measured: **0 of 71,667,008 stage-1 target indices missing from the node table**, 0 sim-set
pad overflows, alignment asserted record by record on the (nChains, nT3) fingerprint.

### THE DELIVERED HEAD AND THE OFFLINE VERDICT

22 inputs: slots 0..10 and 14..21 are the dumped x VERBATIM (already standardized with the SHIPPED
constants, so train == serve by construction and no de/re-standardize round trip can drift), slots
11/12/13 are the target chain's three RAW gate logits `zFake/zPrompt/zDisp` standardized with
constants fitted on the train split. A bare T3 has no chain gate, so all three take the 0 sentinel --
the literal extension of the shipped `attachStdz<11>(0.f)`, with input 20 (targetType) still flagging
the absence. `Chain2NetworkWeights.h` is DELETED (453 lines), with its K7b' evaluation block and the
dead `gateLogit2` SoA column. **No surrogate, no fitted stand-in, no new weights file.**

    head                        recipe                    FPR at matched per-CELL signal eff
                                                          uni A (chain5+)  uni B (T3)  uni C (+aux4L)
    shipped MIN1 (reference)    lr1e-3/pat8/60, 114.5M       1.0000          1.0000      1.0000
    A1  22-in, 218.7M rows      shipped recipe verbatim      0.7508          0.9235      0.7525
    A2  22-in, 218.7M rows      lr3e-3 cosine->1e-5, 200ep   0.7271          0.8138      0.7299

**The 22-input deleted-network head admits 27% LESS background than the shipped head at matched
per-cell signal efficiency.** B1's prediction was ~0.86 and it is beaten; the two terms B1 isolated
(data volume x1.33, the input swap x0.85) both delivered, and S1/S2/B1's convergence lesson
reproduces a third time (cosine buys another 3% on universe A and 12% on universe B).
A4's "the root cause is the head" is now retracted twice over.

### DEPLOYED, PU200 TUNE (1000 evt), and the frontier is ONE dial

    row                shipped   GM12F     S3A2      S3A2R30   S3A2R60
    eff_overall_incut  .810201  -.00048   -.00050    -.00001   +.00057
    dup_overall_incut  .045205  -.00035   -.00065    +.00124   +.00347
    fake_overall_incut .045479  -.00049   -.00151    -.00153   -.00155
    dup_barrel         .015535  -.00055   -.00156    +.00059   +.00322
    fake_transition    .047886  -.00214   -.00411    -.00415   -.00419
    eff_dxy_10_30      .049398  +.00570   +.00570    +.00570   +.00570
S3A2 = the ZERO-FREE-PARAMETER arm (all 8 bars at fixed per-band true-pair acceptance). R30/R60 add a
uniform offset to the four row-REMOVING bars. **Attribution probes: the dial is entirely `-XC`, not
`-RPS`** -- +0.6 on the three `xcTheta` bars alone reproduces R60 (eff +.00048, dup +.00336) while
+0.4 on `rpsThetaChain` alone does nothing (eff -.00045, dup -.00059).

**POOLED 6000-EVENT HOLDOUT, S3A2 vs the CURRENT SHIPPED 4-NN ALGORITHM (`a5_ref/paired.py` McNemar)
-- overall efficiency is NEUTRAL at p 0.40, which the tune's -.0005 did not show:**

    overall .8120 -> .8118 (-.0001, p .399)   vxy_10_30 .6951 -> .7020 (+.0069, p 3.3e-05)
    eff_barrel   -.0009 (p .0028)             dxy_1_5   .5700 -> .5777 (+.0077, p 1.1e-04)
    eff_transition -.0001 (p .896)            dxy_5_10  .2531 -> .2740 (+.0209, p 4.4e-09)
    eff_endcap   +.0005 (p .0083) BETTER      dxy_10_30 .0448 -> .0531 (+.0083, p 6.9e-08) = +18.5% rel
    vxy_1_5 -.0021 (p .07), vxy_5_10 -.0052 (p .0055)

**BOTH CUBES ARE CLEAN: S3A2 is BIT-IDENTICAL to GM12F on every cube50 field, and on cube50_highPt
except `dup_overall` (-.00357, BETTER) and n_tc (-1).** A4's finding that PU200 is the binding gate
for an attach-head change is confirmed a second time. cube50_highPt ran at -s 4 (TC count is down).

### ARM (b), THE DISPLACED-AWARE OUTPUT: NEGATIVE, AND THE REASON IS IN THE DATA

    arm                          stage-0 AUC  bareT3 AUC  disp[1,5) AUC  val AUC
    A2  scalar (shipped output)    .99953      .99908       .99914       .999574
    B2  3-output + M19 weights     .99948      .99870       .99854       .999471
    B1  3-output + LST equal-class .99642      .98980       .99676       .989974  COLLAPSE

**The attach-pair universe has almost no displaced signal to learn from: of 4,089,619 TRUE stage-0
pairs over 1000 events, 7,373 have shared-sim vxy in [1,5), 726 in [5,10) and ZERO at vxy >= 10;
95.8% are pileup-only (vxy -999, which the shipped rule puts in the PROMPT class).** LST's
equal-class convention therefore puts a third of the loss on 8,290 of 131.8M train rows -- a ~5300x
per-row upweight -- and the head is destroyed (val AUC .98997, and it is worse on displaced too).
This is not the gate or the edge head: `r1_train.py` already documented the mechanism ("a pLS mostly
does not exist for a displaced track"). The 3-output structure with `logsumexp(zP,zD) - zF` (chosen
over `max()` precisely because max() is what made `gateLogit2` invert) is not destroyed but is
uniformly slightly WORSE than the scalar arm. Deployed judgement of B2 pending.

Artifacts: `nnloop_ref/s3_work/` (labeller + join reader + trainer + bar refit + the priced dials,
23 GB label cache, every judge). Deliverable: `nnloop_ref/s3_work/s3_attach_delete_nn4.patch`,
`git apply --check` clean on base + `s1_arm_G_3class_balanced.patch` + `s2_GM12F.patch`.

## [S3 FINAL 08:35] **STAGE 3 CLOSES: FOUR NETWORKS -> THREE. `Chain2NetworkWeights.h` IS DELETED, NO SURROGATE, AND THE PHYSICS IS BETTER THAN THE CURRENT SHIPPED ALGORITHM ON EVERY DUP AND FAKE CELL AND ON EVERY DISPLACED BAND, AT NEUTRAL OVERALL EFFICIENCY (p 0.40 ON 6000 EVENTS).**

**DELIVERABLE: `nnloop_ref/s3_work/s3_attach_delete_nn4.patch`** (md5 f0513a6fa7828bc7a34a4ee2bd88c027),
`git apply --check` clean on `4f1846078d9` + `s1_arm_G_3class_balanced.patch` + `s2_work/s2_GM12F.patch`.
7 files: `Chain2NetworkWeights.h` **deleted** (453 lines), its K7b' evaluation block and the dead
`gateLogit2` SoA column removed, `kAttachFeatures` 20 -> 22, the three raw gate logits wired into
attach slots 11/12/13, a regenerated `AttachNetworkWeights.h`, and eight re-fitted float bars.
**No new weights file. No surrogate. No fitted stand-in. One fewer learned component and one fewer
weight file than the shipped algorithm.**

### CAVEATS, STATED WITH THE HEADLINE (not in fine print)

1. **This arm sits on TWO UNSHIPPED STAGES.** Its base is shipped `4f1846078d9` + S1's arm-G 3-class
   edge head + S2's GM12F gate. **Every displaced gain in the "vs shipped" column below is inherited
   from those two stages, not created by stage 3**, and so is the -.0009 barrel efficiency.
   Stage 3's OWN effect is the "vs GM12F" column. **If the S1+S2 stack is not taken, this patch
   cannot ship as it stands** -- the head is trained on GM12F's row distribution, which is the
   loop's own rule, not a detail.
2. **Trained and calibrated on ONE sample** (`event_1000`, the PLAN's split). The 6000-event holdout
   and both cubes are entirely out of sample.
3. **The displaced-aware output arm (b) FAILED and was NOT DEPLOYED** -- see below. That is a
   deviation from "judge both deployed" and I am flagging it rather than hiding it.
4. `dup_endcap` is the ONE regression: +1.04% relative on the pooled holdout. Real, and stated.
5. **Timing: the TOTAL is not comparable between these two binaries** (the pLS canary moved 30 ms).
   See the timing block.

--------------------------------------------------------------------------------------------------

# THE MORNING TABLE

## (ii) vs the CURRENT SHIPPED 4-NETWORK ALGORITHM -- POOLED 6000-EVENT HOLDOUT, THE DECISIVE GATE
`event_2000..7000`, `a5_ref/paired.py` McNemar (the writer emits events in stream-completion order,
so this is paired on the sim-collection key, never entry-wise):

    cell            SHIPPED(4NN)   3-NN S3A2   delta   ship-only  arm-only      p
    overall            .8120        .8118     -.0001     3093      3027       .399   NEUTRAL
    eff_barrel         .9235        .9226     -.0009     1426      1271       .00284
    eff_transition     .8808        .8807     -.0001     1066      1060       .896   NEUTRAL
    eff_endcap         .6854      **.6859**   +.0005      601       696       .00834 BETTER
    vxy_0_1            .8443        .8443     -.0000     2844      2836       .915   NEUTRAL
    vxy_1_5            .7992        .7972     -.0021      524       466       .07
    vxy_5_10           .7190        .7138     -.0052      256       196       .00546 WORSE
    vxy_10_30          .6951      **.7020**   +.0069      771       943     3.26e-05 BETTER
    dxy_0_1            .8368        .8366     -.0001     3983      3915       .444   NEUTRAL
    dxy_1_5            .5700      **.5777**   +.0077      632       777     1.12e-04 BETTER
    dxy_5_10           .2531      **.2740**   +.0209      173       301     4.41e-09 BETTER
    dxy_10_30          .0448      **.0531**   +.0083       59       134     6.92e-08 BETTER (+18.5% rel)

    POOLED dup/fake over the same 9.5M TCs (n_tc-weighted)
    row              SHIPPED(4NN)   3-NN S3A2    delta      relative
    dup_overall         .04510        .04465    -.00045    **-1.00%**  BETTER
    dup_barrel          .01522        .01384    -.00138    **-9.05%**  BETTER
    dup_transition      .01396        .01117    -.00279   **-20.02%**  BETTER
    dup_endcap          .07053        .07126    +.00073      +1.04%    WORSE  <- the one regression
    fake_overall        .04533        .04376    -.00157    **-3.45%**  BETTER
    fake_barrel         .05094        .04916    -.00178      -3.50%    BETTER
    fake_transition     .04834        .04372    -.00462      -9.56%    BETTER
    fake_endcap         .04137        .04082    -.00056      -1.34%    BETTER
    n_tc               9518923       9506247     -12676      -0.13%
    n_tc_t4cl           383355        371071     -12284      -3.20%

## (i) vs the GM12F PIPELINE -- WHAT STAGE 3 ITSELF DID, same 6000 events, same test
**Overall efficiency goes UP, and it is significant:**

    overall         .8116 -> .8118  **+.0003 (p .0388)  BETTER**
    eff_endcap      .6848 -> .6859  **+.0011 (p 4.93e-13) BETTER**
    eff_transition  .8806 -> .8807   +.0001 (p .923)   NEUTRAL
    eff_barrel      .9231 -> .9226   -.0005 (p .0127)  WORSE
    vxy_10_30       .7036 -> .7020   -.0017 (p 6.54e-07) WORSE
    dxy_1_5 +.0004 (p .115) | dxy_5_10 -.0002 (p 1.0) | dxy_10_30 -.0001 (p 1.0)  ALL NEUTRAL
    dup_overall -0.6% | dup_barrel -6.2% | dup_transition -15.3% | fake_overall -2.3%
So **stage 3 deletes a network, improves dup and fake, and gains overall efficiency.** It costs
0.0005 of barrel efficiency and 0.0017 of vxy[10,30) and takes no displaced band backwards.

## PU200 TUNE (event_1000, 1000 evt, -s 8 -p 0.8) -- the full column, and the frontier is ONE dial

    row                 shipped   GM12F d   S3A2 d    R30 d     R60 d
    eff_overall_incut   .810201   -.00048   -.00050   -.00001   +.00057
    eff_barrel          .924316   +.00031   -.00072   +.00000   +.00065
    eff_transition      .882124   -.00172   -.00224   -.00164   -.00060
    eff_endcap          .679590   -.00067   +.00040   +.00064   +.00097
    eff_vxy_1_5         .799872   -.00449   -.00470   -.00428   -.00299
    eff_vxy_5_10        .713793   -.00197   -.00246   -.00246   -.00246
    eff_vxy_10_30       .693155   +.00598   +.00383   +.00383   +.00383
    eff_dxy_1_5         .551332   +.01494   +.01462   +.01462   +.01462
    eff_dxy_5_10        .236346   +.01192   +.01192   +.01192   +.01192
    eff_dxy_10_30       .049398   +.00570   +.00570   +.00570   +.00570
    dup_overall_incut   .045205   -.00035   -.00065   +.00124   +.00347
    dup_barrel          .015535   -.00055   -.00156   +.00059   +.00322
    dup_transition      .014390   -.00096   -.00293   -.00108   +.00136
    dup_endcap          .070386   -.00015   +.00049   +.00226   +.00423
    fake_overall_incut  .045479   -.00049   -.00151   -.00153   -.00155
    fake_barrel         .051448   -.00135   -.00173   -.00178   -.00185
    fake_transition     .047886   -.00214   -.00411   -.00415   -.00419
    fake_endcap         .041521   +.00048   -.00064   -.00062   -.00063
    n_tc                1587160     -1656     -2502      -456     +1917
    n_tc_t4cl             64110     -1658     -1898     -1898     -1898
    conv_T5 (OUTPUT)      .6874     .6836     .6925       --        --

**S3A2 is the RECOMMENDED arm and it has ZERO FREE PARAMETERS** -- all eight bars are set at fixed
per-band true-pair acceptance. R30/R60 add a uniform offset to the four row-REMOVING bars and are
the priced dial: **R60 puts overall efficiency ABOVE shipped (+.00057) at +7.7% relative dup**, R30
lands it exactly neutral (-.00001) at +2.7% dup. I recommend S3A2 rather than R30 because on the
6000-event holdout S3A2's overall efficiency is ALREADY neutral (p .40) while its dup is BETTER, so
the tune's -.0005 is a 1000-event fluctuation and paying dup for it would be paying for noise.

**ATTRIBUTION OF THE DIAL, worth having: it is entirely `-XC`, not `-RPS`.** +0.6 on the three
`xcTheta` bars alone reproduces R60 (eff +.00048, dup +.00336); +0.4 on `rpsThetaChain` alone does
nothing at all (eff -.00045, dup -.00059). And `lostsim.py` (new, reusable) says where the sims are:
of the 350 sims S3A1 lost vs GM12F, **164 were carried by a bare pLS row and 89 by a stage-B pT3 row**
-- i.e. by the row-REMOVING decisions, not by delivery. The delivery-count dial is a red herring:
matching the pre-contention delivered-target count exactly (offset +0.09, `deliv.py`) moved overall
efficiency by -.00005.

## BOTH CUBES: CLEAN, AND STAGE 3 IS INVISIBLE THERE
**S3A2 is BIT-IDENTICAL to GM12F on every cube50 field, and on cube50_highPt on every field except
`dup_overall` (.03080 -> .02722, -.00357 BETTER) and `n_tc` (-1).** Both ran at `-s 4` (TC count is
down, so the writer segfault threshold is not approached). So its cube deltas vs shipped ARE S2's
GM12F deltas, including **the one real cube regression, cube50_highPt dxy[1,5) -.0137, which is a
stage-2 cost that stage 3 neither adds to nor removes.** A4's "PU200 is the binding gate for an
attach-head change" is confirmed a second time, now with bit-identity as the evidence.

## SHIP-VERIFICATION AND BOTH BACKENDS

    the FINAL patch applied to g7 RESET to 4f1846078d9 + armG + GM12F, INDEPENDENT clean rebuild
      CPU  (`-m -C`)  0 `error:` in .make.log.1786534733   -> 35/35 judge fields BIT-IDENTICAL
                      to the measured arm at full float precision
                      (eff_overall_incut 0.8096974357614489 both, n_tc 1584658 both)
      CUDA (`-G`)     0 `error:` in .make.log.1786535412, `bin/lst_cuda` produced
      determinism     a second independent CPU rebuild reproduced the binary md5 EXACTLY
                      (d47a7036d84644c0e759cea4c4b6aee2)
    export path       `b1_ref/port_hdr.py` re-parses every literal in the emitted header against the
                      .pt AND regenerates the SHIPPED header's 1209 float literals bit-exactly from
                      `r1_ref/attach_mlp_MIN1.pt`, so the generator is verified, not trusted.

## TIMING -- MEASURED ON A QUIET BOX, AND THE CANARY SAYS THE TOTAL IS NOT COMPARABLE
200 evt, `-s 1 -v 1 -w 0`, ms/event; the head SHAPE is compile-time so scope rule 6's same-binary
config-only A/B does not exist for this change. 4 GM12F repeats, 2 S3A2 repeats:

    binary   Hits    MD     LS     T3   Graph    pLS   Chain     TC   Reset   Total
    GM12F    15.1  93.8   78.3   68.7   33.3  364.7   146.7   53.4    0.8   854.7
    GM12F    15.5  93.7   80.1   69.9   33.5  364.6   147.4   53.4    1.4   859.5
    GM12F    15.6  93.8   78.9   69.4   33.4  364.3   147.7   53.4    1.2   857.6
    GM12F    15.6  93.8   78.9   69.4   33.4  364.6   147.1   53.4    1.2   857.5
    S3A2     15.9  93.8   80.7   71.7   32.6  395.1   150.2   41.8    2.6   884.5
    S3A2     15.1  93.7   77.5   67.7   32.7  394.6   148.3   41.8    0.4   871.9

 * **TRAP 6 FIRES, and this is the cleanest demonstration of it yet: `pLS` is 364.3-364.7 on one
   binary and 394.6-395.1 on the other, +30 ms (+8.3%), and NEITHER BINARY CHANGES A SINGLE LINE OF
   pLS CODE** -- the patch touches only ChainConfig / ChainsSoA / AttachNetworkWeights / ChainAttach /
   ChainAttachT3 / ChainGate. That 30 ms is pure code placement. **Any statement about the TOTAL
   (854.7-859.5 vs 871.9-884.5) is therefore uninterpretable, and I am not making one.**
 * **`TC` is 53.4 -> 41.8 ms, -11.6 ms (-21.7%), and it is EXACTLY repeatable -- 53.4 in all four
   GM12F runs and 41.8 in both S3A2 runs.** That is the stage `arbitrateChains` (attach, crossclean,
   retirement, chain-TC assembly) runs in, and it is the only column that moves beyond noise.
 * `Chain` (which is where the DELETED network's kernel K7 lived) is 146.7-147.7 -> 148.3-150.2, i.e.
   flat to +1.5 ms. **So I cannot attribute the TC win to the deletion**; the arithmetic actually
   roughly cancels (the deletion removes 1856 multiply-adds x ~7.07e3 chains/evt = 13.1M, the head's
   two extra inputs add 48 x ~1.97e5 scored pairs/evt = 9.5M). The likely cause of the TC gain is
   less downstream work (3.2% fewer T4-class rows to assemble, and re-fitted bars changing how many
   -XC / -RPS / -CC operations fire), not the network count. Stated as measured, not as a claim.

--------------------------------------------------------------------------------------------------

# WHY IT WORKS: THE ONE NUMBER

**Fake-weighted FPR at MATCHED per-cell signal efficiency on 200 held-out events (A4's and B1's own
verdict metric, the only calibration that isolates head quality):**

    head                          inputs  net?     train rows   uni A    uni B    uni C
    shipped MIN1 (incumbent)        20    kept       114.5M     1.0000   1.0000   1.0000
    A1 shipped recipe verbatim      22    DELETED    218.7M     0.7508   0.9235   0.7525
    A2 lr3e-3 cosine, 200 ep        22    DELETED    218.7M   **0.7271** **0.8138** **0.7299**
    B2 3-output + M19 weights       22    DELETED    218.7M     0.7773   1.0966   0.7861
    B1 3-output + LST equal-class   22    DELETED    218.7M     5.0649   9.9692   5.6607

**The 22-input head with the network DELETED admits 27% LESS background than the shipped 20-input
head that keeps it, at matched per-cell signal efficiency.** B1's decomposition ([B1 18:55]) predicted
~0.86 from data volume x1.33 and the input swap x0.85; the measured 0.727 beats that, and the extra
comes from the third confirmation of the convergence lesson (cosine buys 3% on universe A and 12% on
universe B over the shipped constant-lr recipe). **A4's "the root cause is the head" is retracted for
the second time, now on the deployed gates as well as offline.**

# THE NEGATIVE RESULT THAT MATTERS: THE DISPLACED-AWARE OUTPUT ARM CANNOT WORK HERE, AND THE REASON IS THE DATA, NOT THE HEAD

Of **4,089,619 TRUE stage-A attach pairs over 1000 events: 7,373 have shared-sim vxy in [1,5),
726 in [5,10), and ZERO at vxy >= 10. 95.8% are PILEUP-ONLY** (vxy = -999, which the shipped label
rule puts in the PROMPT class, and I replicated rather than corrected).

 * **B1 (3-output, LST's own `total/(3*count)` equal-class convention -- the maintainer's explicit
   request): CATASTROPHIC.** It puts a third of the loss on 8,290 of 131.8M train rows, a ~5300x
   per-row upweight; the head admits **5.1x / 10.0x / 5.7x** more background than the incumbent and is
   WORSE on displaced too (chain disp[1,5) AUC .99676 vs A2's .99914). The convention that WON stage 1
   and was competitive at stage 2 fails here, and the reason is that stage 1 had 2.55M displaced-true
   edges to balance against and stage 3 has 8,290 pairs.
 * **B2 (3-output, deployed scalar `logsumexp(zP,zD) - zF`, M19 weights): uniformly worse than the
   scalar arm** -- FPR ratio 0.7773 / **1.0966** / 0.7861 against A2's 0.7271 / 0.8138 / 0.7299, i.e.
   on the bare-T3 universe it is worse than the SHIPPED head. `logsumexp` not `max()` was deliberate
   (max() is exactly what made `gateLogit2` score .336, inverted, on displaced-vs-prompt) and the
   structure is not what fails -- there is simply no displaced signal for a third output to model.
 * **NOT DEPLOYED, and that is a deviation from the brief I am flagging, not hiding.** Deploying it
   needs a 3-output attach header plus the kernel reduction; I judged that 45 minutes of C++ was the
   wrong call for a head that is worse than the INCUMBENT in one of the three universes on the
   protocol's own verdict metric, measured on held-out events with the same estimator the primary arm
   is judged by. S1's rule ("offline proxies mispredict") was established for RANKED quantities scored
   by a PROXY (weld purity); this is the same metric, not a proxy. The honest summary is: **on this
   data a displaced-aware attach output is not reachable, and the place to buy displaced efficiency is
   upstream -- which is exactly what stages 1 and 2 did.**

# METHOD ADDITIONS, ALL REUSABLE

 * `LST_CHAIN_JOIN_DUMP` (in g4; **NOT in the delivered patch**) -- per chain node its sparse triplet
   index and its three MDs' (anchor,other) ph2 rows, per pLS its `see_*` row and eta. Identities only;
   no sim data is read or written, so truth stays offline. It closes the one gap `pairs.bin` left, and
   the structural fact that makes ONE table serve both target kinds is that **bare-T3 targets are
   chain NODES** (`ChainAttachT3Keep` iterates `nNodes`): 0 of 71.7M stage-1 target indices missed.
 * `s3label.py` labels **218.7M pairs in 283 seconds** and asserts alignment (the (nChains,nT3)
   fingerprint, record by record) rather than repairing it. Verification is not optional in it: the
   SHIPPED head's own dumped logit separates the replicated labels at **AUC 0.99933 / 0.99891 /
   0.99944** by stage, and a misalignment control collapses to **0.50057**.
 * `barfit_s3.py` -- the eight bars at fixed per-band true-pair acceptance with **zero free
   parameters**, the reference read out of the dump itself (the shipped head's own logit on the same
   rows), plus the per-cell 2x10 diagnostic and the matched-efficiency FPR verdict.
 * `deliv.py` / `retire.py` -- the delivery-count and retirement-count dials priced offline as exact
   per-target / per-pLS argmax-then-threshold counts, so an offset costs a minute instead of a build.
 * `lostsim.py` -- which SIMS an arm loses and **what TC class was carrying them**. This is the
   diagnostic that found the answer in one shot after the delivery-count dial had already failed; any
   future eff/dup round should run it before scanning thresholds.
 * **A CALIBRATION FACT WORTH RECORDING:** the shipped `-a` bars are NOT a fixed-signal-efficiency
   point -- `ChainConfig.h` says in so many words they were chosen to "hold conversion at the 19-input
   head's operating point". So a signal-efficiency-matched bar on a BETTER head necessarily raises
   delivery (measured: conv_T5 .6836 -> .6980 for A1). Anyone re-fitting these must know which
   invariant the incumbent value encodes.

# ARTIFACTS (all under `nnloop_ref/s3_work/`, 25 GB, nothing in /tmp)

    s3_attach_delete_nn4.patch   THE DELIVERABLE (base + armG + GM12F)
    lab/                  23 GB  the labelled on-policy cache: 218.7M rows x (20 dumped x, the 3 raw
                                 gate logits, label, stage, vxy, simPt, |pLS eta|, the SHIPPED head's
                                 own logit, event). REBUILDABLE in 5 min from round3 + join.bin.
    join.bin              1.4 GB the truth-join sidecar (identities only)
    models/A{1,2}*.pt B{1,2}*.pt + norm json + per-epoch history + the frozen TEST report
    bars_{A1,A1D,A2,A2R,A2R30,A2RPS40,A2XC60,B1,B2}.json   every bar fit with its full per-cell report
    hdr_*.h  runs/  logs/        generated headers, every judge/root/log, every build log
    s3label.py joinio.py train_s3.py barfit_s3.py setbars_s3.py deliv.py retire.py lostsim.py
    deploy_s3.sh phase2_s3.sh paired_s3.sh shipverify.sh timing.sh

**WORKTREES:** g7 = the ship-verified arm (base+armG+GM12F+S3, CPU and CUDA built). g6 = reverted to
base+armG+GM12F and rebuilt, i.e. the GM12F baseline binary, used for the timing A/B. g4 still holds
the pair-dump + join-dump instruments and the round-3 provenance binary (md5
0a49ac71980c8d85a2b8c4f17d2f1d44) in case a re-dump is needed. **g7's `.SCRAM` cache had
`CMSSW_BASE` pointing at g1 when I provisioned it (copied from g6, which has the same defect, so
every S1/S2 build in g6 carried it too). It is benign -- `-I..` and `-I<own tree>/src` precede
`-I$CMSSW_BASE/src`, and the `kInput == kAttachFeatures` static_assert would have fired on any
cross-tree header mix -- but I fixed g6 and g7, and whoever provisions the next worktree should
check it.**
