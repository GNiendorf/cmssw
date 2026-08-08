# Shared findings log - DISPLACED / cube50 ROUND (agents A1 A2 A3 A4 A5)

APPEND YOUR FINDINGS HERE, with the flock idiom so nobody's write is lost:

    S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
    flock $S/FINDINGS_CUBE.md -c 'echo "[A3 HH:MM] finding: numbers, denominators, what you ran" >> $S/FINDINGS_CUBE.md'

## !! TWO TRAPS THAT SILENTLY PRODUCE PLAUSIBLE WRONG ANSWERS (A2, measured the hard way) !!

**1. MIXING SAMPLES WITHOUT AN EVENT-ID OFFSET SILENTLY DISCARDS THE ENTIRE ENRICHMENT SET.**
cube50 and PU200 both number their events from 1. The M8 COMBINATION RULE in `train_chain3.py` /
`train_edge.py` drops every event in an EXTRA input whose `evt` id appears in the PRIMARY. Mix a cube
dump in as input[1] and **100% of it is dropped as "overlap"** -- the run completes, the log looks
normal, and you have measured a null enrichment that reads as a real negative result. That is the
worst failure mode available to this round. Use `--evt-offset` (`a2_ref/proto/train_chain3.py`, which
gives each input its own evt namespace) or check the "combination rule drops N overlap events" line in
the log against the number of events you expected to keep.

**2. `abs(-999)` = +999 PUTS THE SENTINEL IN THE HIGHEST dxy BIN.**
`labelChainsHarness` leaves `simVxy`/`simDxy` at the -999 sentinel for every chain whose best-matching
sim is a PILEUP row -- **94.3% of all positives**. Take `abs()` before masking and every one of those
lands in your `dxy >= 30` bucket. My first census reported "94% of positives at dxy>=30" for exactly
this reason and it looked entirely believable. Mask on `simDxy > -900` FIRST, and report a `known=`
count beside every fraction. Related: `-999 < 1`, so under the shipped class rule
(`y3[is_true & (vxy < 1)] = 1`) all 94.3% of them are labelled **prompt-true** by default.

--------------------------------------------------------------------------------------------
READ THE WHOLE FILE BEFORE YOU START. Everything below was measured today; you do not need to
re-derive any of it, and re-deriving it wastes a slot someone else could use.

--------------------------------------------------------------------------------------------
## THE GOAL

Reach parity-or-better with LST master on displaced efficiency in TWO places at once:
  1. **PU200 dxy [10,30)** -- the one PU200 band where master beats us.
  2. **cube50** -- the displaced sample itself (both `cube50` and `cube50_highPt`).
WITHOUT losing PU200 physics. The whole project's advantage is displaced efficiency; this is the
one region that contradicts it.

--------------------------------------------------------------------------------------------
## THE MEASURED DIAGNOSIS (done 2026-08-07, do not repeat it)

**THE GAP IS ENTIRELY OUR 4-LAYER (T4-class) OBJECTS. Our 5+-layer chains already match master.**

cube50, 5000 evt, CPU, `-p 0.8`. Denominators are large, so these ARE significant:

    band          denom   master    ours     delta +/- sigma
    dxy [10,30)    9353   .0269    .0056     -.0214 +/- .0019
    dxy [ 5,10)    2598   .0778    .0616     -.0162 +/- .0073
    vxy [10,30)    5521   .0639    .0389     -.0250 +/- .0043
    (dxy[1,5), vxy[1,5), vxy[5,10) are all within 2 sigma -- NOT differences)

TYPE COMPOSITION, which is the whole story:

    cube50  dxy[10,30)   master T5cl  42 / T4cl 210      ours T5cl  32 / T4cl  20
    cube50  vxy[10,30)   master T5cl 187 / T4cl 166      ours T5cl 181 / T4cl  34
    highPt  dxy[10,30)   master T5cl   1 / T4cl  13      ours T5cl   2 / T4cl   0
    PU200   dxy[10,30)   master T5cl  39 / T4cl  47      ours T5cl  25 / T4cl  25

`cube50_highPt` is otherwise FINE: we are equal or slightly ahead in every other band (vxy[1,5)
+.031, vxy[5,10) +.018, dxy[5,10) +.005, all within noise). So the deficit concentrates in
**low-pT, high-impact-parameter, 4-layer** tracks.

SUPPORTING SIGNAL THAT THESE ARE CUT RATHER THAN NEVER BUILT: on cube50 our dup rate is **.0000
vs master's .0389**, fakes comparable, and our mean track length is LONGER (9.91 vs 9.59).
Cleaner + longer + fewer is the signature of thresholds too tight for this population. It is NOT
proof -- A1 exists to settle it.

**THERE ARE ZERO PIXEL-CONTAINING TCs (types 5/7/8) in dxy[10,30) on either side.** A track
displaced >10 cm produces no usable pixel seed, so that band is entirely OT-only objects. The
hypothesis "master wins it via pT3-class, look at our stage B" is FALSE and CLOSED.

## THE LEADING HYPOTHESIS (maintainer, and it is well grounded)

**LST enriches its T4 DNN training with the cube50 sample. We enrich nothing** -- verified: there
are ZERO cube references anywhere in `analysis/DNN/` (`train_edge.py`, `train_chain.py`,
`train_attach.py` all read prompt-dominated PU200 only). So master enriched exactly the class it
beats us on. That is the round's central hypothesis, but it is a HYPOTHESIS: A1/A4/A5 exist to
test whether enrichment can even work before A2 spends a training cycle on it.

## HOW DISPLACED THE POPULATION ACTUALLY IS (PU200RelVal, 1000 evt, 131876 selected sim tracks)

    dxy [10,30)  1.32% of sim tracks  but only 0.07% of our MATCHED tracks (1 in 1400 positives)
    vxy [10,30)  4.31% of sim tracks  and       4.40% of matched

**"Born displaced" (vxy) and "does not point home" (dxy) are DIFFERENT POPULATIONS. We already
beat master on vxy by +.087 and lose on dxy.** Before anyone enriches with cube50, know which axis
it populates -- enriching vxy would target a region we already win. That is A5's first job.

--------------------------------------------------------------------------------------------
## THE MEASUREMENT CONTRACT -- EVERY AGENT, EVERY RESULT, BOTH SAMPLES

A result that reports only one sample is not a result. You must report:

 1. **PU200RelVal, 1000 evt, CPU, `-p 0.8 -s <=8>`**: eff .8099 / dup .0479 / fake .0470 and all
    four displaced bands, via `python3 protoD1/compare_ab.py --proto <yours> --base
    win_ref/all4_rv1000_hists.root`. Those three numbers are the CONSTRAINT: a displaced win that
    moves them is not a win until the maintainer prices it.
 2. **cube50, 5000 evt**: dxy[10,30), dxy[5,10), vxy[10,30) with DENOMINATORS and Poisson sigma,
    plus the T4cl/T5cl composition. Use `python3 cube_ref/band_census.py <master.root> "MASTER"
    <yours.root> "OURS"` -- it prints all of that and flags anything under 2 sigma.
 3. **cube50_highPt, 5000 evt**: at minimum the dxy[10,30) row.

**THE cube50 TRAP -- DO NOT QUOTE THESE.** Only **19 of 22194** cube50 tracks pass the standard
overall-efficiency selection, because it requires `vtx_perp < 2.5 cm` (`performance.cc`,
`vtx_perp_thresh`) and this is a 50 cm cube. So cube50's `eff overall`, its eta-region rows, and
its dup/fake rates are computed on ~20 tracks and are NOISE. The vxy/dxy BAND metrics drop that
cut and have denominators in the thousands -- those are the only usable cube50 numbers.

REFERENCE ARTIFACTS, already on disk, do not regenerate:
  frozen binaries   `merge_ref/merged/` (ours, lst_cpu md5 99b1e86be599) and `merge_ref/master/`
                    (LST master b42d8f97ad5, lst_cpu md5 0189e8848a2f)
  cube ntuples+hists `cube_ref/cube50_{ours,master}{,_hists}.root` and `cube50_highPt_*`
  PU200 reference    `win_ref/all4_rv1000_hists.root` (round-2 shipped) and
                     `master_ref/master_rv1000{,_hists}.root`
  census tools       `cube_ref/band_census.py` (bands + types + sigma), `cube_ref/dxy_types.py`
  plots              `performance/cube50_ours_vs_master_*/mtv/var/` and the highPt equivalent

--------------------------------------------------------------------------------------------
## SCOPE RULES (maintainer, non-negotiable)

 * **DO NOT touch shared LST code** -- no MD/LS/T3 builders, no `Kernels.h`, nothing pLS-side. The
   maintainer's instruction is explicit: we are trying to BEAT LST with our algorithm, not improve
   LST. A change that also helps master closes no gap. The below-T3 funnel investigation was
   DROPPED for this reason; A1's scan answers "do the objects exist" from our side instead.
 * Chain code only: `Chain*.h`, the chain parts of `LSTEvent.dev.cc`, `ChainConfig.h`, the trained
   weights and their training scripts.
 * **The end state is ONE head plus one cut.** A new trained component is only a win if it REMOVES
   more machinery than it adds. Three agents once independently built a trained retirement head; it
   bought ~.002 and was refused.
 * Never ship new weights without re-fitting ALL SIX bars.
 * No non-ASCII in C++ comments. `pushd`, never `cd`.

## NO SAMPLE OVERFITTING (maintainer, non-negotiable, and it disqualifies otherwise-good results)

**ONE ALGORITHM. No change may condition its behaviour on which sample it is running in**, whether
explicitly or by proxy. Specifically forbidden:
 * any branch on a sample flag, file name, or configuration that differs between samples;
 * any cut keyed on an EVENT-LEVEL property that separates the samples rather than separating good
   tracks from bad ones -- total hit count, total segment/T3 count, occupancy, number of pLS. cube50
   is 10 muons with no pileup and PU200 is ~200 collisions, so ANY such quantity is a near-perfect
   sample detector, and a cut using one is sample tuning wearing a physics costume;
 * two sets of weights or bars selected by anything other than a per-track observable.
**THE TEST TO APPLY TO YOUR OWN CHANGE:** would it still recover the track if that displaced track
were embedded INSIDE a busy PU200 event? If your win evaporates in that thought experiment, it is
occupancy tuning and it will be refused. This is why every result must report BOTH samples: a change
that helps cube50 and moves PU200 is exactly what this rule exists to catch.
NOTE the legitimate case, so nobody over-corrects: the per-node degree/neighbour aggregates
(problem 15) ARE legitimate inputs -- they discriminate a real track from a jet-core fake WITHIN an
event. What is forbidden is a change whose benefit comes from those inputs taking systematically
different values in a muon gun than in ttbar.

**A3, THIS APPLIES TO YOU SHARPLY: dxy IS NOT AN OBSERVABLE AT RECO TIME.** You cannot index a
working point by true dxy -- there is no truth at reco time, and a table keyed on it is not
implementable. "Per-dxy-decade working points" (plan problem 12) means the WP is **VALIDATED** per
dxy decade while the **TABLE ITSELF** is indexed by things the algorithm can see: eta, pT, layer
count, and reconstructed displacement proxies such as the DCA that `dcaSplit` already uses. If you
find yourself writing `if (sim_dxy > 10)`, stop -- that is the whole trap.

## THE BARS ARE COMPILE-TIME CONSTANTS -- READ THIS BEFORE YOU PLAN A SCAN

`ChainConfig.h` fields (`m3Theta4`, `m3Theta4D`, `dcaSplit`, `m3ThetaRI`, `m3ThetaR`, the `zd*`
band adjustments, `attachTheta*`, `xcTheta*`) have **NO env or CLI override** in the integrated
tree -- verified. The `-M4`/`-a`/`-AT3` style flags were the offline prototype's CLI, not this
tree's. So a naive scan means one rebuild per point.

**THE FIX, and A1 should land it first because everyone benefits:** add env overrides for the bars
being scanned, defaulting to the shipped value so the UNSET arm is byte-for-byte the shipped
configuration. That is exactly the pattern the GPU round used (`LST_U4_GRIDR`,
`LST_CHAIN_ATTACH_SLICES`). One build, then a scan of arbitrary depth. Existing env knobs for
reference: `LST_CHAIN_TIMING`, `LST_CHAIN_FEAT_DUMP`, `LST_CHAIN_EDGE_DUMP`, `LST_CHAIN_NODE_DUMP`,
`LST_CHAIN_TC_DUMP`, `LST_CHAIN_T3_AUDIT`, `LST_CHAIN_ATTACH_AUDIT`.

--------------------------------------------------------------------------------------------
## INFRASTRUCTURE -- DIFFERENT FROM THE GPU ROUNDS ON PURPOSE

**These are PHYSICS runs, not timing runs.** Contention costs wall-clock, not correctness, so there
is NO broker and no exclusive machine lock. Instead:
 * Cap yourself at **`-s 8`** so five agents coexist, and check `cat /proc/loadavg` before a big run.
 * Use the **CPU backend** for anything decisive -- it is deterministic and reproducible. GPU is
   nondeterministic at ~250 per 1e5 TCs; that is negligible against these Poisson errors but it
   makes exact A/B reproduction impossible, so keep GPU for speed checks only.
 * Work in your assigned worktree (A1 g1, A2 g2, A3 g3, A4 g4, A5 g5), each a full SCRAM area.
   **Start from the shipped HEAD `d950e4315be`** (GPU round 2 merged). Your predecessor left
   uncommitted work there: `git stash` it (do NOT `reset --hard`) -- it is all either committed in
   HEAD or exported as patches under `g*/u*_ref/`, but stashing keeps it recoverable.
 * Only A2 needs a training-branch build (`lst_make_tracklooper -mcCd`). Everyone else works from
   ntuples, dumps and runtime flags.
 * `lst_make_tracklooper` reports "compilation successful" EVEN WHEN A TU FAILS. Always check the
   freshly-timestamped `.make.log.<ts>` for `error:`, never the stale `.make.log`.
 * PLOTTING GOTCHA, cost me a cycle: source `setup.sh` then `eval $(scramv1 runtime -sh)` and STOP.
   A third `source setup.sh` after scram clobbers PYTHONPATH and `import ROOT` fails.

## PRIOR FALSIFICATIONS -- DO NOT SPEND A SLOT RE-LEARNING THESE

 * pT cuts on fakes cost the displaced lead (round 2).
 * Lowering the attach margin `-a` costs displaced efficiency: at the 75% hit-matching threshold a
   wrong QUAD seed puts a 5-layer chain at 10/14 = .714.
 * A 6x displaced loss weight was tried before and was "defeated by its own threshold" -- the WP
   was still fitted on the prompt population. That is why A3 exists as its own arm.
 * The terminal trim is a KEEP: removing it costs eff in every band including displaced, and raises
   dup AND fake.
 * `nLowerModules()` is 13200, not 40000.

## POPULATION FACT THAT CONSTRAINS ANY RETRAIN

weldedNodes/chains = 13017/5403 = **2.41 nodes per chain**, so most objects this pipeline calls a
"chain" are TWO-NODE welds, and trim's `if (nN < 3) continue;` declines to fit the majority. A
2-node chain has ONE weld edge, so features 2-4 and 18 (edge-logit sum/min/mean/std) are DEGENERATE
on most of the population -- std is undefined for nE=1 and sits at hard zero. Any retrain that
treats chain features as describing a long path is describing a minority.

--------------------------------------------------------------------------------------------
## THE FIVE ASSIGNED ANGLES

  A1  BAR-SCAN CEILING (cheap, and it is everyone else's baseline)
  A2  RETRAIN THE 3-CLASS GATE WITH cube50 ENRICHMENT (the main event)
  A3  PER-dxy-DECADE WORKING POINTS (the threshold-only competitor to A2)
  A4  LABEL AUDIT FOR 4-LAYER DISPLACED OBJECTS (can invalidate A2's premise)
  A5  cube50 POPULATION + FEATURE DISCRIMINATION (tells A2 whether it can succeed)

Each agent has its own brief. A1 and A5 are cheap and feed A2/A3 -- post your results EARLY even if
partial, because two other agents are waiting on them.

--------------------------------------------------------------------------------------------
## [A5 Q1] WHICH AXIS DOES cube50 POPULATE? -- ANSWERED: **dxy. A2's premise SURVIVES.**
Scripts `a5_ref/joint_vxy_dxy.py` + `a5_ref/joint_delta.py` (logs alongside). Existing ntuples
only, no new runs. Selection = the band-plot set (|eta|<4.5, pt>0.9, |vz|<30), no vtx_perp cut.

### 1. cube50 is dxy-RICH, not vxy-only. The worry in the brief is resolved.
Fraction of the selected sim population with |dxy| >= 10 cm:
    cube50         42.1% (9353/22194)      cube50_highPt  42.1% (12586/29869)
    PU200RelVal     1.32% (1735/131876)
That is a **32x enrichment in exactly the axis we lose**. cube50 also has ZERO tracks at vxy<1
(no prompt population at all) and 71.5% at vxy>=30.

### 2. EVERY significant deficit on BOTH samples is in the dxy>=10 column. Nothing else is.
Per-(vxy,dxy)-cell OURS-minus-MASTER, cube50 5000 evt (14 populated cells, only 2 exceed 2 sigma):
    vxy[10,30) x dxy[10,30)   denom 3672   m .0433 -> o .0095   -.0338 +/- .0038   8.9 sigma
    vxy [30,+) x dxy[10,30)   denom 5681   m .0164 -> o .0030   -.0134 +/- .0018   7.2 sigma
    ALL 12 other cells: |delta| <= 1.6 sigma. At dxy<10 we are at parity or ahead everywhere
    (e.g. vxy[10,30)xdxy[1,5) +.0059, vxy[30,+)xdxy[1,5) +.0106).
PU200RelVal 1000 evt: the ONLY two negative cells in the whole table are
    vxy[10,30) x dxy[10,30)   denom  137   m .2847 -> o .1679   -.1168 +/- .0575   2.0 sigma
    vxy [30,+) x dxy[10,30)   denom 1598   m .0294 -> o .0169   -.0125 +/- .0054   2.3 sigma
and we are POSITIVE and significant in five cells, all at dxy<5.
cube50_highPt: only vxy[10,30)xdxy[10,30) is negative (11 -> 2 of 4966, 2.5 sigma); we are +ahead
in 11 of 14 cells, including every dxy<10 cell. Confirms highPt is otherwise fine.

### 3. The "vxy deficit" is a dxy deficit in disguise -- the two axes are NOT independent findings.
cube50 row-deficit decomposition:
    vxy[10,30) row: -138 tracks, of which **-124 (89.9%)** are in its dxy[10,30) cell
    vxy [30,+) row:  -79 tracks, of which  -77 (97.5%) are in its dxy>=10 cells
    vxy[1,5) and vxy[5,10) rows: literally 0% of the row is dxy>=10 (kinematically impossible)
So the reported cube50 `vxy[10,30) -.0250` is not a second, separate problem: it is the same
dxy>=10 population re-binned. There is ONE deficit, not two.
Symmetrically, the PU200 cell where **we beat master by +.087** is vxy[10,30) x **dxy[0,1)**
(2848 tracks, .4688 -> .5555, 4.6 sigma) -- "born displaced but STILL POINTS HOME". That is a
different population from what we lose, exactly as the brief suspected, and it is now localised.

### 4. THE ONE CAVEAT A2 MUST HANDLE: the vxy MIXTURE inside dxy[10,30) differs 60.7% vs 92.1%.
    PU200 dxy[10,30) is **92.1%** vxy>=30 (1598/1735)
    cube50 dxy[10,30) is **60.7%** vxy>=30 (5681/9353)
Both sub-cells are real deficits, so this is not fatal -- but a UNIFORM cube50 enrichment
over-weights the vxy[10,30) sub-cell ~5x relative to the PU200 band we must fix. A2: either weight
cube50's dxy>=10 examples toward vxy>=30, or report the two sub-cells separately so the transfer is
visible. Note also **35.1% of cube50 (dxy>=30) is dead weight**: master reconstructs 1 of 7787 there.

### 5. TRANSFER / SAMPLE-OVERFITTING HAZARDS IN cube50, stated up front for A2
  a. **Geometry differs**: mean |dxy|/vxy for vxy>1 is **0.635 in cube50 vs 0.134 in PU200**
     (corr(vxy,dxy) .581 vs .850). PU200's displaced tracks are quasi-radial daughters (parent
     decays, daughter roughly collinear, so vxy large but dxy small); cube50 muons are isotropic
     and genuinely transverse. Same dxy value, different direction-vs-radius geometry.
  b. **pT is a near-perfect sample tag**: cube50 pt median 1.45 (p10 1.01, p90 1.89);
     cube50_highPt median 25.5 (p10 5.9, p90 45.0); PU200 median 1.81 (p90 7.74). Neither cube
     sample spans the PU200 spectrum, so ANY feature correlated with pT will partly learn "which
     sample am I in". Mixing both cube samples helps but does not fix it.
  c. **No prompt population in cube50 at all** (vxy<1 count = 0), so cube50 cannot supply the
     negatives that hold the prompt working point; it must be MIXED with PU200, and the mixing
     fraction is the real knob.

### VERDICT FOR A2 / A3
Enrichment is aimed at the right axis -- go. But the target is not "cube50", it is
**cube50 restricted to dxy >= 10**, which is 42% of the sample, and preferentially its vxy>=30 part.
For A3: the deficit is two-dimensional in TRUTH but one-dimensional in what a WP can see -- the
whole loss is at large true impact parameter, which at reco time is the DCA-like observable, not vxy.

--------------------------------------------------------------------------------------------
[A4 PARTIAL 1] LABEL AUDIT: the 75% rule is STRICTLY-GREATER and 4-layer is the ONE layer
count where losing a single MiniDoublet is FATAL. Provenance verified, arithmetic measured.

PROVENANCE (the shipped gate, file:line):
  src/alpaka/Chain3NetworkWeights.h:3-4  model = prototype/chain3_mlp_m12.pt,
                                         norm  = prototype/chain3_norm_m12.json
  chain3_norm_m12.json train_args.inputs = prototype/chains_m12_{300,498}evt.root
  protoD3/train_chain3.py:59,288          label branch = "label" (the M12 harness rule)
  prototype/DumpWriter.cc:202-208         "label" = labelChainsHarness; label_old/matchFrac also dumped
  prototype/Labels.cc:153-206             labelChainsHarness: production matcher over the chain's
                                          FULL hit list (per member MD, anchor then other, all
                                          Phase2OT), TRUE iff some sim frac > 0.75
  prototype/Matching.cc:50,178,182        nhits_input = count of UNIQUE (hitidx,hittype) pairs;
                                          percent_matched = counts/nhits_input;
                                          accept iff `percent_matched > matchfrac` -- STRICT >
  code/core/write_lst_ntuple.cc:42,1545   the EFFICIENCY metric uses the same 0.75 and the same
                                          strict >: sim_tcIdx set only if bestmatch_frac > 0.75
  => LABEL RULE == EFFICIENCY RULE. So this is NOT a train/serve disagreement. Good news for A2.

THE ARITHMETIC (this is the finding). A chain with nL layers has 2*nL hits, so the pass bar is
the smallest integer strictly above 0.75*2nL, i.e. the EFFECTIVE purity requirement is:
     nL=4   8 hits   need 7/8  = 87.5%    <-- one bad MD = 6/8 = 0.750 -> FAILS (strict >)
     nL=5  10 hits   need 8/10 = 80.0%         one bad MD = 8/10 = 0.800 -> PASSES
     nL=6  12 hits   need 10/12= 83.3%         one bad MD = 10/12= 0.833 -> PASSES
     nL=7  14 hits   need 11/14= 78.6%         one bad MD = 12/14= 0.857 -> PASSES
**4 layers is the ONLY layer count where a single wrong MiniDoublet is fatal**, and it fails by
landing EXACTLY on the threshold. Quantization, not physics.

MEASURED on the actual shipped training rows (prototype/chains_m12_498evt.root, 2,378,702 chains,
PU200RelVal 498 evt), matchFrac exact-value census:
  nL  N        label1     label1 rate   marginal-FAIL   marginal-PASS   %fail at boundary
   4  931,445  335,791    36.0%         146,104 (6/8)    75,718 (7/8)    65.9%
   5  932,795  665,750    71.4%          37,103 (7/10)  166,808 (8/10)   18.2%
   6  483,748  421,805    87.2%          19,922 (9/12)   94,758 (10/12)  17.4%
   7   30,714   28,511    92.8%           1,381 (10/14)   1,204 (11/14)  53.4%
Poisson sigma on the 4-layer 6/8 count is 382 (0.26%), so these are not statistical.

CONSEQUENCES, stated plainly:
 1. The 4-layer positive class is HALF as prevalent as the 5-layer one (36.0% vs 71.4%).
 2. 146,104 / 595,654 = **24.5% of the 4-layer NEGATIVE class is one single hit away from being
    positive** (6/8 vs 7/8). At 5 layers the boundary is 4.5x tilted the other way.
 3. The flip census label_old(>=2/3-MD) -> label(harness) drops 176,931 4-layer chains, and
    146,104 of those (82.6%) are the exact 6/8 case.
 4. NOT a mislabel: a 6/8 4-layer chain earns no efficiency credit as a TC either, so it is
    correctly negative FOR THE METRIC. Master's T4 (8 hits) faces the identical bar, so this does
    not by itself explain the master gap.
 5. But it DOES bound A2: cube50 enrichment adds 4-layer chains, a large share of which will land
    at 6/8 and enter the training as HARD NEGATIVES that differ from class-2 positives by one hit
    -- a difference chain-level features cannot see. Enrichment may sharpen the 4-layer fake class
    rather than the displaced positive class. A2 should report the 6/8 share of whatever it adds.
Displaced split + ntuple-side (sim_tcIdxBestFrac pile-up at 0.75) still running. -- A4

--------------------------------------------------------------------------------------------
[A2 18:2x] INFRASTRUCTURE FACTS FOR THE RETRAIN (posted early -- they change what a retrain costs)

1. **THE TRAINING DUMPS DO NOT COME FROM THE INTEGRATED TREE.** `LST_CHAIN_CHAIN_DUMP` in
   `LSTEvent.dev.cc` is a BINARY parity sidecar ('P22C' records) with NO truth label -- it cannot
   train anything. The labelled TTree ("chains" + `feature_spec` TNamed + `label`/`label_old`/
   `simVxy`/`simPt`/`nLayers`/`dcaXY`) that `prototype/train_chain3.py` reads is written by the
   OFFLINE PROTOTYPE, `prototype/bin/chainproto -m chaindump`. So the brief's `-mcCd` build is
   needed for a DIFFERENT reason than "turn the dump on" (see 2).
2. The prototype eats a **BASELINE LST `--allobj` ntuple** (`-i`) plus the tracking ntuple (`-t`).
   `NtupleReader.cc` binds `md_dphichange`, which is **CUT_VALUE_DEBUG-only**
   (`code/core/write_lst_ntuple.cc:250`), so the input ntuple must come from a `-d` build. Verified
   by `strings`: NEITHER frozen binary (`merge_ref/master/lst_cpu`, `merge_ref/merged/lst_cpu`) is a
   `-d` build, and our own tree has no chain on/off switch, so its TC list is chain TCs, not LST's.
   => a cube50 training dump requires a fresh `-d` build of LST MASTER. Started (g2, b42d8f97ad5,
   `-mcCd`); binaries will be at `gpu_wt/g2/a2_ref/master_d_bin/`.
3. **THE SHIPPED WEIGHTS' DUMPS ARE NOT REPRODUCIBLE WITH TODAY'S BINARY, so any enrichment run
   needs a CONTROL ARM.** `chain3_norm_m12.json` records its inputs as
   `prototype/chains_m12_{300,498}evt.root`, dumped by a *retrained-edge* chainproto
   (`iterations/fanout5/edgeretrain/er_chaindump.sh`, `-e <theta> -L 0.5`) whose edge head is not
   the shipped one, while the shipped weld config is `thetaEdge 0 / lambdaLen 3`. Chain features
   2/3/4/18 ARE edge logits, so rows dumped with a different edge head sit on a different scale.
   Consequence: cube50 rows must be dumped with the SAME binary as the PU200 rows, i.e. PU200 gets
   RE-dumped too, and a PU200-only retrain on the re-dump is the honest baseline for the mixed one.
   Anyone comparing to the shipped gate must not attribute the re-dump delta to enrichment.
4. **EXPORT INTEGRITY GATE PASSES, with one caveat worth knowing before you ship weights.**
   `export_chain3_weights.py` run on the shipped `chain3_mlp_m12.pt` + `chain3_norm_m12.json`
   reproduces ALL 2087 numeric literals of the integrated `src/alpaka/Chain3NetworkWeights.h`
   exactly. It is NOT byte-for-byte: the ported header was clang-formatted (472 lines vs the
   exporter's 623) and its comment records a different `--out` path. Also note
   `prototype/chain3_mlp_weights.h` on disk (md5 9ee16231...) is NOT the md5 the integrated header
   cites as its source (d170f174...), so the on-disk prototype header has been rewritten since the
   port. The shippable check is literal-for-literal after `scram b code-format`, not md5.
5. THE SIX BARS, named, since "all six" is not written down anywhere: the live thresholds on the
   3-class head's margin scale are `m3Theta4` (T4 IP, mX), `m3Theta4D` (T4 exempt, mD),
   `m3ThetaRI` (IP-5+ rescue, mX), `m3ThetaR`=`m3ThetaRB`=`m3ThetaRT` (exempt-5+ rescue, mX),
   `c25Theta` (mP) and `c25ThetaD` (mD). `m3Theta5/6`/`m3ThetaD` are inert at 1e9. The `zdM4`
   (-0.5) / `zdM4D` (+1.2) band deltas ride on top of the first two.

--------------------------------------------------------------------------------------------
## [A3 18:05] THE GATE KILL CENSUS -- reco-side, no truth, no rebuild needed. USE THIS.

Tool: `LST_CHAIN_CHAIN_DUMP` (already in the shipped tree, LSTEvent.dev.cc:3070) + parser
`a3_ref/chain_census.py`. It gives per-chain dcaXY / nLayers / branch / flags / mP,mD,mX.
NOTE for everyone: `merge_ref/merged/lst_cpu` is NOT built at `d950e4315be` -- it has U3's
`[CHAIN SCAN]` instrumentation, so it is a LATER tree. I built my own at d950e4315be
(`a3_ref/base/`, lst_cpu md5 85dfab4656070af5f). Anyone doing an exact A/B should know this.
ALSO: `dumpChains` writes NO record for an event with zero chains -- on cube50 that is
**425 of 500 events**, so cube50 needs `-n -1` for any chain-level statistics.

### THE T4/T5 KILL ASYMMETRY IS THE WHOLE GAP, AND IT IS 92.6% vs 16.7%

    sample   class            nChains   killed at the gate
    PU200    T4 (nLayers<=4)   72820      92.6%
    PU200    T5 (nLayers>=5)  115408      16.7%
    cube50   T4                   74      70.3%
    cube50   T5                  107       1.9%

(PU200RelVal 40 evt; cube50 500 evt. Both from a3_ref/base, `-s 1 -p 0.8`.) This is the
reco-side confirmation of the diagnosis at the top of this file: our 4-layer class is killed
almost everywhere and our 5+ class is barely touched.

### WHERE THE PROMPT ANCHORING ACTUALLY LIVES -- concrete, with file:line

`src/alpaka/ChainGate.h:577-657` is a 4-cell decision tree on (nLayers<=4 vs 5+) x (dcaXY vs
`dcaSplit`=0.5 cm), one bar per cell:

    cell            condition                kill rule                       bar
    br0 T4 IP       nL<=4, dca<0.5    mX < m3Theta4 + zdM4[band]      4.0   (3.5 in band)
    br1 T4 exempt   nL<=4, dca>=0.5   mD < m3Theta4D + zdM4D[band]   -1.2   (0.0 in band)
    br2 5+ IP       nL>=5, dca<0.5    mX < m3ThetaRI + zdRI          -0.5
    br3 5+ exempt   nL>=5, dca>=0.5   mX < m3ThetaR(B/T) + dR        -1.8   (all 3 bands)

Three separate anchorings, all observable and all fixable in principle:
 (a) **the T4 IP bar is 4.0 while the 5+ IP bar is -0.5** -- a 4.5-logit penalty on being
     4-layer, which is a PU200 fake-rate decision;
 (b) **`dcaSplit`=0.5 is a SINGLE breakpoint and the exempt bar above it is ONE number**, while
     the population above it is overwhelmingly at 0.5-2 cm (PU200 exempt T4: 8446+11512+16309
     chains in [0.5,5) vs 3459 in [10,30)). So the bar a 20 cm displaced chain must clear was
     fitted on 0.5-2 cm objects. dcaXY is the Kasa-fit |d(centre)-R| (ChainGate.h:244) -- a
     per-track RECO quantity, so refining this axis is legitimate and implementable;
 (c) **`zdM4D = +1.2` TIGHTENS the T4 exempt bar to 0.0 in 1.1<=|eta|<1.7** -- the same trade
     the maintainer already unwound on the 5+ exempt branch (`m3ThetaRB/RT` -1.2 -> -1.8,
     ChainConfig.h:48-56), still live on the T4 exempt branch. A2/A4 should know this exists.

### THE DCA AXIS DOES DISCRIMINATE -- BUT ONLY BY 13x, AND THE BAR MUST MOVE 3.8 UNITS

Killed exempt-T4 chains recovered per event by lowering the mD bar to -5.0, cube50 (signal)
against PU200 (cost):

    dcaXY bin    cube50/evt   PU200/evt   PU200 cost per cube50 chain
    [0.5,1)         0.027        91.5        3400
    [1,2)           0.067       104.3        1560
    [2,5)           0.160       138.2         863
    [5,10)          0.160        72.7         454
    [10,30)         0.093        24.6         264

So signal-to-cost improves **monotonically by 13x** from [0.5,1) to [10,30). A per-dcaXY bar is
therefore a REAL discrimination and not a sample detector. But note the absolute scale.

### THE FINDING THAT MATTERS MOST, AND IT IS BAD NEWS FOR MY ARM (and constrains A2)

**cube50's killed displaced T4 chains are killed DECISIVELY, not marginally.** In dcaXY [10,30)
the bar is -1.2 and the killed chains' mD is p50 -3.37 / p90 -2.52 / **max -1.40** -- the single
best killed chain sits 0.2 below the bar and the rest are 1.3-2.2 below. Recovering 7 of 9 needs
the bar at **-5.0**, i.e. a 3.8-unit move. That is not a threshold adjustment, it is deleting the
bar. At [5,10) it is the same story (p50 -2.83, and bar -5 is needed for 12 of 16).

Meanwhile PU200's killed exempt T4 at the same dcaXY occupies the SAME mD range (p50 -5.63 /
p90 -2.15 at [5,10)). The two distributions overlap where it counts. **The 3-class score does not
rank these displaced 4-layer chains above the fake class -- it puts them 1.3-3.4 logits INSIDE
it.** So "the score is already adequate and only the threshold is wrong" is looking FALSE, which
is the specific thing my arm was set up to test. Corollary for A2: a retrain has to move mD by
~3 units on this population, not tighten a decision boundary -- and A4's label audit is now the
critical path, because a 3-unit move is only possible if these objects are labelled signal.

Numbers to be re-quoted at higher cube50 statistics (500-evt dump = 74 T4 chains); a full
`-n -1` dump is running. Scan of the two-breakpoint scheme follows.

--------------------------------------------------------------------------------------------
[A1 18:07] STEP 1 LANDED: THE BARS ARE NOW ENV-OVERRIDABLE. ONE BUILD, SCAN OF ANY DEPTH.
A3 AND A5: THIS IS FOR YOU. Patch is 2 files, ~90 lines, no kernel change:
  interface/ChainConfig.h   inline void chainConfigEnvOverride(ChainConfig&)  (host-only, at the
                            bottom of the header; parses getenv, prints "[CHAIN BAR] name = val"
                            ONCE per process, plus a MEASUREMENT-CONFIG warning)
  src/alpaka/LSTEvent.h     one call at the end of the LSTEvent ctor body
Patch exported at gpu_wt/g1/a1_ref/chain_bar_env.patch -- apply in your own worktree with
`git apply`, then `lst_make_tracklooper -C`. (Only interface/ChainConfig.h and src/alpaka/LSTEvent.h
change, so the whole library rebuilds; ~4 min CPU-only.)

VERIFIED: THE UNSET ARM IS THE SHIPPED ALGORITHM. My build with NO variable set reproduces
cube_ref/cube50_ours.root EXACTLY -- all six bands, all denominators, all T5cl/T4cl compositions,
delta +0.0000 in every row (5000 evt). At 200 evt it is also bit-identical to the frozen
merge_ref/merged/lst_cpu (nTC 31, types {T5cl 23, pT5cl 1, T4cl 7} both sides). Malformed values
are ignored and fall back to the shipped default, so a typo degrades to baseline, not to chaos.

ENV NAMES (all default to the shipped value; float unless noted):
  GATE (ChainGate.h K7c)   LST_CHAIN_M4 M4D M5 M6 MD MRI MR MRB MRT X Z C25 C25D L E
  ETA BANDS (additive)     LST_CHAIN_ZE1 ZE2 ZM4 ZM4D ZRI ZR ZR5 ZR6 ZCP ZCD
  K9 EXEMPT ACCEPTANCE     LST_CHAIN_U4 U5 U6
  ATTACH / CROSSCLEAN      LST_CHAIN_A A2 A3 AT3 RPSA T3F XCT XCT2 XCT3, and int CC9 CCMS
e.g.  LST_CHAIN_M4D=-3.0 LST_CHAIN_U4=-1e5 ./bin/lst_cpu -i cube50 -n 5000 -p 0.8 -s 8 -o x.root

*** THERE ARE TWO LIVE BARS ON THE T4-CLASS EXEMPT BRANCH, NOT ONE. THIS COST ME A CYCLE AND WILL
*** COST YOU ONE. The gate (ChainGate.h:620) kills on `mD < m3Theta4D + zdM4D`, but K9 then applies
*** `candKeep = (score >= thetaExempt4)` (ChainArbitrate.h:145,155) to EVERY exempt chain, on the
*** legacy sum-logit scale, with thetaExempt4 == 0. So a chain the gate SPARED is still dropped by
*** U4. Any T4-class loosening that moves only M4D is incomplete -- move U4 with it. (The IP T4
*** branch is not affected: non-exempt chains get noCutTheta = -1e5.)

[A3 18:20 -> A1] CORRECTION, AND IT SAVES YOU AND ME A KNOB: **U4 IS NUMERICALLY INERT.** Your
read of ChainArbitrate.h:144-154 is right as CODE -- `candKeep = score >= thetaExempt4` really is a
second bar on the exempt branch -- but I measured it and it drops NOTHING:

    PU200RelVal 40 evt : gate-spared exempt T4 chains 3687, U4 (score >= 0) drops    0  (0.0%)
    cube50      500 evt: gate-spared exempt T4 chains   22, U4 (score >= 0) drops    0  (0.0%)

and it also drops none of the killed chains that a lower M4D would recover, in EVERY dcaXY bin
(the "+U4" columns in a3_ref/chain_census.py are identical to the bare ones throughout). Reason:
`chains.score` carries `lambdaLen * nLayers` with lambdaLen = 3, so a 4-layer chain starts at +12
and the edge-logit sum never pulls it below 0. Method: the dump's score already has gateKill=1e9
subtracted for a killed chain, so the pre-gate score is recoverable exactly as `score + 1e9`.
**So M4D is the operative bar on that branch and an M4D-only scan is COMPLETE.** Keep U4 in the
env list (it costs nothing and the inertness is a measurement, not a guarantee), but do not spend
scan points pairing it with M4D.

--------------------------------------------------------------------------------------------
[A4 FINAL] LABEL AUDIT VERDICT: **THE LABELS ARE RIGHT.** A2 may proceed on its targets, but
A2's stated PREMISE ("displaced tracks are absent from training") is FALSE, and the shipped
-M4D bar is already at the knee of its trade curve. Analysis only -- no new physics runs.

### 1. LABELS ARE NOT WRONG (the thing I was sent to falsify)
The labelling rule and the efficiency-scoring rule are the SAME rule, verified line by line:
  labelChainsHarness (prototype/Labels.cc:186-191): production matcher over the chain's full
    hit list, TRUE iff some sim frac > 0.75
  proto::matchedSimTrkIdxsAndFracs (prototype/Matching.cc:50,178,182): frac = counts /
    (number of UNIQUE (hitidx,hittype) pairs), accepted iff `> matchfrac` -- STRICT >
  write_lst_ntuple.cc:42,1545: sim_tcIdx (the efficiency numerator) set iff bestmatch_frac >
    0.75, same strictness, same denominator convention
So a chain labelled 0 would earn no efficiency credit as a TC either. There is NO train/serve
label disagreement. **The suspicion in my brief is falsified. A2's targets are correct.**

### 2. THE 4-LAYER QUANTIZATION ASYMMETRY IS REAL (but shared with master, so it is not the gap)
2*nL hits and a strict > 0.75 make the EFFECTIVE purity bar:
  nL=4  8 hits -> 7/8 = 87.5%   one bad MD = 6/8 = 0.750 -> FAILS
  nL=5 10 hits -> 8/10= 80.0%   one bad MD = 8/10 = 0.800 -> PASSES
  nL=6 12 hits -> 10/12=83.3%   one bad MD = 10/12= 0.833 -> PASSES
  nL=7 14 hits -> 11/14=78.6%   one bad MD = 12/14= 0.857 -> PASSES
**4 layers is the only length where losing a single MiniDoublet is fatal**, and it fails by
landing exactly on the threshold. Master's T4 (verified type=9 <-> nlayers=4 <-> nhits=8 in both
ntuples) faces the identical bar, so this does NOT by itself explain the master gap.

Measured, prototype/chains_m12_498evt.root (2,378,702 chains, PU200RelVal 498 evt):
  nL  N        lab1     rate    6/8-class fail   min-pass    %fail at the boundary pair
   4  931,445  335,791  36.0%   146,104 (6/8)     75,718 (7/8)      65.9%
   5  932,795  665,750  71.4%    37,103 (7/10)   166,808 (8/10)     18.2%
   6  483,748  421,805  87.2%    19,922 (9/12)    94,758 (10/12)    17.4%
   7   30,714   28,511  92.8%     1,381 (10/14)    1,204 (11/14)    53.4%
Poisson sigma on the 4L 6/8 count is 382 (0.26%). ANSWER TO "what fraction of genuinely-displaced
4-layer objects does the labeller call negative": by the OBJECT-level definition of genuine
(labelChains, >=2/3 MDs of every member T3 share a sim), **34.5% of 4-layer object-true chains
are harness-negative, vs 7.5% at 5 layers and 5.4% at 6 -- a 4.6x penalty on the 4-layer class**,
and 82.6% of those 4-layer flips are the exact 6/8 case.

GAP I COULD NOT CLOSE, and it is a 2-line fix for whoever regenerates a dump:
labelChainsHarness OVERWRITES simVxy/simPt with -999 for every chain it rejects
(prototype/Labels.cc:179-182), so **the displacement of the near-miss population is not
recoverable from any dump on disk.** I tried joining the pre-M12 dump (chains_498evt.root, which
has old-rule simVxy) to chains_m12_498evt.root on (evt,nLayers,cf_02,cf_05,cf_06,cf_09): only 9
of 2.38M rows join -- the two dumps are different feature generations. Whoever regenerates should
keep `simVxyOld`. Best available proxy (|dcaXY| q25/50/75, 4-layer): the 6/8 population sits at
0.55/1.46/3.27, TRUE vxy>=10 at 0.84/2.00/4.01, real fakes (mf<0.5) at 1.13/2.67/5.43, TRUE
prompt at 0.04/0.12/0.42 -- so the 6/8 group is displaced-LOOKING and definitely not
prompt-with-one-bad-hit, but dcaXY cannot separate displaced-true from fake. Control that makes
the mechanism plain: at 5 layers the 7/10 (fail) and 8/10 (pass) populations have essentially
IDENTICAL dcaXY (q50 0.964 vs 0.880) -- the threshold cuts through a homogeneous population.

### 3. RETRACTING MY OWN PARTIAL-1 POINT 5 (the hard-negative worry does NOT survive contact)
I scored the SHIPPED gate (chain3_mlp_m12.pt + chain3_norm_m12.json, exact m13 recipe) on the
4-layer training rows and applied the shipped T4 kill rules (ChainConfig.h: dcaSplit .5, m3Theta4
4.0, m3Theta4D -1.2; eta-band deltas zdM4/zdM4D not applied -- eta is not in the dump, so IP-branch
kills are slightly overstated):
  4L creditable (label1)  N=335,791  survive 0.1764
    vxy<1                    12,854          0.1166
    vxy[1,5)                  1,006          0.1869
    vxy[5,10)                   578          0.2958
    vxy>=10                   2,485          0.5211   (IP 0.0080 / EXEMPT 0.6126)
  4L 6/8 near-miss (lab0)   146,104          0.0306
  4L real fake (mf<0.5)     276,398          0.0299
The gate already separates the 6/8 near-misses from creditable displaced (3.1% vs 52.1%) and
treats them exactly like real fakes (3.06% vs 2.99%). **The near-misses are NOT poisoning the
displaced class at the shipped working point.** My earlier warning was wrong; disregard it.

### 4. THE -M4D TRADE CURVE IS ALREADY AT ITS KNEE (hand-off for A1 and A3)
EXEMPT T4 branch (nL<=4, dcaXY>=0.5), N: disp vxy>=10 2109 / vxy>=1 2770 / prompt-true 111,313 /
6/8 near-miss 111,976 / fake 244,298. Survival vs -M4D:
   -M4D    D>=10    D>=1   prompt    6/8    fake
    0.0   .3642   .3386   .1316   .0098   .0059
   -0.5   .4689   .4394   .1968   .0174   .0136
   -1.2   .6126   .5787   .3618   .0383   .0338   <-- SHIPPED
   -1.8   .7283   .7040   .5549   .0736   .0618
   -2.5   .8246   .8144   .7484   .1323   .1072
   -3.5   .9270   .9206   .8965   .2352   .1926
MARGINAL exchange rate (extra creditable vxy>=10 : extra 6/8+fake), pre-K9-arbitration:
   0.0->-0.5  1:12.3    -0.5->-1.2  1:24.0    -1.2->-1.8  1:44.2
  -1.8->-2.5  1:87.0    -2.5->-3.5  1:149.9   -3.5->-5.0  1:499.2
**The shipped -1.2 sits right where the curve turns over.** Loosening the exempt T4 bar is
essentially exhausted at this feature set. (Caveat: "bad" counts pre-arbitration chains; K9
removes many downstream, so the effective fake cost is lower than the raw ratio -- but the
SHAPE is the message, and it is monotone.)

### 5. WHERE MASTER'S 4-LAYER ADVANTAGE ACTUALLY COMES FROM (pure-OT TCs, types 4 and 9)
                     8-hit N   raw-fake   at 6/8   creditable
  cube50   OURS          117          3        3          114
  cube50   MASTER        497          4        4          493      <-- 4.3x more, SAME purity
  cube50hiPt OURS         36          0        0           36
  cube50hiPt MASTER       65          0        0           65      <-- 1.8x more, same purity
  PU200    OURS       33,883      8,214    1,120       25,669
  PU200    MASTER     39,097     23,847    2,292       15,250     <-- more objects, 2.9x fakes
**On cube50 master's 4x T4-class lead is NOT bought with fakes (4 fakes in 497).** It has ~4x
more CREDITABLE 8-hit objects than we do at equal purity. That is a construction/admission
statement upstream of the gate, not a label or a training statement, and it is A1's question.
On PU200 the picture inverts (master is dirtier), which is why we win PU200 fake rate.

### 6. A SEPARATE STANDING TRAIN/SERVE MISMATCH -- REAL, BUT NOT THE 4-LAYER STORY
The training dump is UNTRIMMED: `chaindump` mode (prototype/main.cc:1363-1420) calls
k6WeldChains then computeChainFeatures then labelChainsHarness and NEVER calls k6TrimTerminals
(`trimEnable` is read only at main.cc:2735, the serving path). At serve time the order is
ChainTrimTerminals (LSTEvent.dev.cc:1338) -> ChainFeaturesKernel (:1354) -> ChainGateKernel
(:1370), and terminalTrim is ON by default (interface/ChainConfig.h:24). So the gate is trained
on untrimmed features/labels and applied to trimmed chains.
Scope, measured from cf_00 (nNodes): trim can only touch nNodes>=3, which is 786,949/2,378,702 =
**33.1% of rows and 45.9% of positives**. It changes nNodes, nLayers, the edge-logit aggregates,
chi2, dcaXY -- all gate inputs -- and for 6+/7-layer chains it changes the LABEL DENOMINATOR
(12->10, 14->12).
**BUT `trimMinLayersAfter = 5` (ChainConfig.h:27) means trim can never produce fewer than 5
layers, and ZERO 4-layer chains have nNodes>=3.** So the 4-layer class is untouched by trim and
every 4-layer conclusion above stands. Recording this as a separate defect, not as this round's
answer.

### 7. BOTTOM LINE FOR A2
 * Your labels are correct. Relabelling is not the lever. Do not spend a cycle on it.
 * Your PREMISE IS FALSE AS STATED. Displaced tracks are NOT absent from the shipped training.
   Combined over both inputs (chains_m12_300evt + chains_m12_498evt) the 4-layer positive class
   contains 1,534 rows at vxy[1,5), 938 at vxy[5,10) and **3,913 at vxy>=10** (6,385 displaced of
   535,847 positives, 1.19%); and chain3_norm_m12.json already applies pos_weight * 8 for
   vxy[1,5) and * 16 for vxy>=5 (n_mid_train 4,324 / n_hi_train 11,531) on top of a DEDICATED
   3-class displaced head whose recorded val AUC is val_auc_disp = 0.9373. Displacement is
   present, weighted 16x, and given its own output class already.
 * So cube50 enrichment must be justified by NEW INFORMATION (a region of feature space PU200
   does not populate -- that is A5's question), not by absence. If you do enrich, report the
   6/8 share of the 4-layer chains you add: at 4 layers a quarter of the negative class is one
   hit from positive, and adding rows on both sides of a boundary the features cannot resolve
   buys nothing.
 * Given (4) and (5): the 4-layer displaced deficit does not look like a gate problem at all.
   52% of creditable 4-layer vxy>=10 chains already survive the gate, the bar is at its knee,
   and master's cube50 lead is 4x more creditable objects at equal purity. Point the next cycle
   upstream (A1).
Artifacts: none written (analysis was read-only over prototype/chains_m12_*.root, cube_ref/
cube50{,_highPt}_{ours,master}.root, merge_ref/gpu2_rv1000.root, master_ref/master_rv1000.root).
-- A4

--------------------------------------------------------------------------------------------
## [A5 Q2] CAN THE CURRENT FEATURE SET SEPARATE 4-LAYER DISPLACED SIGNAL? -- YES, AUC .908,
## but 6 of the 25 nominal inputs are PROVABLY DEAD on 100% of the 4-layer class.
Measured on the SHIPPED gate's own training rows: `prototype/chains_m12_498evt.root` (PU200RelVal
498 evt, 2,378,702 welded chains, feature_spec == the 25-slot ChainFeatures.h contract).
Scripts/logs: `a5_ref/feat_sep.py|.log`, `a5_ref/feat_ceiling.py|.log`. No new runs. Truth for
measurement only. Cross-checked against A4's census: my nL/label counts reproduce A4's exactly.

### 1. THE DEGENERACY IS TOTAL, NOT PARTIAL. **100.000% of 4-layer chains are 2-node welds.**
931,445 / 931,445 have nNodes == 2. Not "the majority" -- ALL of them, and it is structural: a
4-layer chain is two 3-layer T3s sharing a segment; a third node forces nLayers >= 5. So on the
class where our ENTIRE deficit lives, exact-duplicate/constant census gives:
    CONSTANT   cf_00 nNodes = 2.0        cf_01 nLayers = 4.0        cf_18 stdEdgeLogit = 0.0
    BIT-IDENTICAL   cf_02 sumEdgeLogit == cf_03 minEdgeLogit == cf_04 meanEdgeLogit
                    cf_05 fullFitChi2PerHit == cf_19 maxBridgeChi2   (one bridge = the whole chain)
    => **25 nominal inputs, 19 DISTINCT NUMBERS.** Three slots are literally constant (AUC exactly
    0.5000 by construction), three more are bit-for-bit copies. 24% of the input vector is noise
    for the target class. (For contrast: at nLayers==5 only 70.8% are 2-node, and nNodes / std
    DO carry signal there -- AUC .5584 / .5550 -- so these slots cannot simply be deleted globally.)
    maxBridgeChi2's whole rationale ("isolate each weld's own circle from the global fit") is
    VACUOUS at 4 layers: there is one weld and it spans every hit.

### 2. BUT THE SEPARATION IS THERE, so A2's retrain is NOT doomed. Multivariate ceiling, event-split
60/40 (298 train / 200 test evt), signal = 4-layer label1 & simVxy>=5, background = 4-layer label0:
    best SINGLE feature (maxXyResid)          test AUC .8201
    logistic regression, 19 inputs            test AUC .8879   TPR@FPR 1%/5%/10% = .216/.501/.655
    HistGradientBoosting, 19 inputs           test AUC .9070   TPR@FPR 1%/5%/10% = .284/.547/.697
    same at simVxy>=10                        test AUC .9084   TPR@FPR = .260/.560/.717
    PROMPT reference (same inputs, same split) test AUC .9656   TPR@FPR = .514/.829/.915
So the current inputs are worth ~.91 on the displaced 4-layer task and there is real multivariate
headroom over any single feature (+.086). Displaced is HARDER than prompt (.908 vs .966) but not
information-starved. **The feature set is not the blocker.**

### 3. THE MECHANISM: the strong 4-layer discriminants INVERT between prompt and displaced, and
the positive class is 98.8% prompt, so any pooled fit is a prompt fit.
4-layer label1 = 335,791 rows, of which only **4,069 (1.21%) have simVxy>=1** and 3,063 have >=5.
Per-feature AUC, PROMPT positives vs fakes -> DISPLACED (simVxy>=5) positives vs fakes:
    meanT3PromptScore     .8992  ->  .4456   (strongest prompt feature; INVERTS past 0.5)
    minT3FakeScore        .1381  ->  .4231   (near-total collapse)
    |dcaXY|               .1778  ->  .4241   (near-total collapse)
    maxJunctionDegProduct .3216  ->  .4577   (near-total collapse)
    nPS                   .7067  ->  .6114
    sum/min/meanEdgeLogit .9327  ->  .7485   (survives, weakened)
    maxXyResid            .1905  ->  .1778   (SURVIVES INTACT -- the best displaced feature)
    fullFitChi2PerHit     .1916  ->  .1782   (survives intact)
    minT3DisplacedScore   .4118  ->  .7722   (.8062 at simVxy>=10 -- INVERTS THE OTHER WAY)
    meanT3DisplacedScore  .2604  ->  .7251
This is the measured mechanism behind the prior falsification "a 6x displaced loss weight was
defeated by its own threshold": the features a pooled loss selects are precisely the ones that go
to ~0.45 on displaced. The two feature families that hold up are (a) the xy-circle residual family
and (b) the upstream t3dnn DISPLACED scores (cf_23/cf_24) -- which the M6-M9 gate did not even have.

### 4. MEASURED NEGATIVE, saves a cycle: **feeding dcaXY to the head buys nothing for displaced.**
Adding |dcaXY| as a 20th input: GBDT .9070 -> .9067, logistic .8879 -> .8880. It is a PROMPT
discriminant (AUC .178 prompt vs .424 displaced) and it is already used correctly as the `dcaSplit`
branch selector. Do not propose it as a gate input.

### 5. WHAT LST's t4dnn SEES THAT WE DO NOT (verified from master b42d8f97ad5,
`src/alpaka/NeuralNetwork.h:437-560`, 30 inputs, and the WP at :550-557).
Master's 30 = 4 absolute coordinates of the innermost anchor hit (|eta1|, |phi1|, |z1|, r1)
            + 12 CONSECUTIVE-HIT deltas (d_eta, d_phi, d_z, d_r for steps 1->2, 2->3, 3->4)
            + 5 radius features (1/innerR, 1/outerR, **innerR/outerR**, 1/regressionR,
              1/nonAnchorRegressionR)
            + 9 t3dnn scores: fake/prompt/displaced for BOTH member T3s **plus their 3 SIGNED
              differences**
            + a WP of `displacedScore > Wp_displaced[2 pT][25 eta] && fakeScore < Wp_fake[...]`.
Precisely named gaps, each with the sample-overfitting verdict the rule demands:
 (a) **NO absolute geometry AT ALL in our 25 -- no |eta|, no z, no r, no phi.** We carry only the
     coarse categorical proxies innermostLayer / layerSpan / nPS / nBarrel. Master has eta as an
     INPUT and bins its working point in 25 eta bins; our head has neither. COMPUTABLE FOR FREE:
     the chain's eta already exists at the gate site (K10 sets tc.eta = ev.t3_eta[t3Inner], and the
     `zd*` band adjustment already reads it). VERDICT: **per-track observable, legitimate** -- eta
     changes real detector physics (material, PS/2S mix, layer coverage). CAUTION: cube50's eta
     spectrum is a muon gun's, so A2 must validate per-eta-region on PU200 to prove eta did not
     become a sample tag. I could NOT measure its AUC: eta is not in the dump. **A2: add `eta` (and
     ideally `zInnermost`, `rInnermost`) to ChainDumpWriter in the re-dump you are already doing --
     it costs one branch and makes this the cheapest ablation in the round.**
 (b) **innerR/outerR, a SCALE-FREE consistency ratio.** Ours (fullFitChi2PerHit, maxXyResid,
     maxBridgeChi2) are ABSOLUTE cm^2 / cm residuals, so they grow with radius and conflate pT with
     quality -- and they are our BEST displaced features, so the confound sits on the load-bearing
     input. For a 4-layer chain the two member T3 radii are already loaded (cf_08 uses the member
     kappa median), so `min(k1,k2)/max(k1,k2)` is FREE. VERDICT: **per-track, zero occupancy
     content, legitimate. This is my top recommendation** -- it is the direct analogue of the
     production T5 inner/outer-radius agreement cut, and it is what master has and we do not.
 (c) **minT3PromptScore and the SIGNED inner-vs-outer score differences.** Note for nNodes==2 the
     pair (cf_23 min, cf_24 mean) already determines BOTH displaced scores, so displaced info is
     NOT missing -- only its ORDERING (inner vs outer). Prompt we carry only the mean, so
     `minT3PromptScore` is genuinely absent. Both FREE. VERDICT: per-track, legitimate, cheap.
 (d) The 12 per-step (d_eta, d_phi, d_z, d_r). Real information master has and we summarise away.
     VERDICT: per-track and legitimate, but 12 inputs is expensive; if attempted, compress to 2
     (e.g. max per-step |d_z| and the rz-slope change across the weld).
 NOT missing: our sumEdgeLogit (the weld MLP's log-odds) has no master counterpart and is our
 strongest prompt feature; chargeConsistency and maxJunctionDegProduct are also ours alone.

### 6. THE ONE RED FLAG A2 MUST INSTRUMENT
`maxJunctionDegProduct` is the legitimate per-node degree aggregate (problem 15), but it is also
the input whose distribution is most sensitive to occupancy, and cube50 is 10 muons with no pileup.
Its displaced AUC is only .4577, so it has almost nothing to offer the target class. **TEST: if
maxJunctionDegProduct's permutation importance RISES after cube50 enrichment, that is occupancy
tuning wearing a physics costume and the retrain must be rejected.** Same test for nPS/nBarrel.

### VERDICT FOR A2
Premise SURVIVES on both counts. The feature set reaches AUC .908 on 4-layer displaced (vs .966
prompt), so a mix change can move it. But (i) 6 of 25 inputs are dead weight on 100% of your target
class, (ii) the displaced positives are 1.21% of the 4-layer positive class so the mix is the whole
game, and (iii) the cheapest real ADDITIONS -- which the architecture rule prices as cheap because
they are inputs to the EXISTING head, not a new head -- are the scale-free member-radius ratio,
|eta|, and minT3PromptScore, all three FREE at the gate site and all three things master's t4dnn has
that we do not. Add `eta`/`zInner`/`rInner`/member-radii to the re-dump so they are ablatable.

--------------------------------------------------------------------------------------------
## [A5 Q2 ADDENDUM + ONE CORRECTION TO MY OWN POST ABOVE] -- and the number A2 actually needs.
Source of truth for the SHIPPED head: `prototype/chain3_norm_m12.json` (`feature_names`,
`displaced_weighting`, `class_spec`). Reweight measurement: `a5_ref/feat_reweight.py|.log`.

### CORRECTION 1: the shipped head's 25 inputs are NOT the ChainFeatures.h 25-slot contract.
`chain3_norm_m12.json:feature_names` = dump cf_00..cf_18, then cf_20..cf_24, then **cf_dcaXY**.
So the shipped head **EXCLUDES `maxBridgeChi2`** and **INCLUDES `dcaXY`**. Consequences:
  * My "6 of 25 are dead" becomes **5 of 25**: the 3 constants (nNodes=2, nLayers=4,
    stdEdgeLogit=0) and 2 of the 3 identical edge-logit copies. maxBridgeChi2's vacuity at 4 layers
    is still true and still worth knowing, but it costs nothing because it was never shipped.
    **=> 25 nominal inputs, 20 DISTINCT NUMBERS on 100% of the 4-layer class.**
  * My item 4 was mis-framed: dcaXY is not a candidate, it is ALREADY an input. The measurement
    stands and is now a statement about the shipped head -- one of its 20 live inputs contributes
    essentially nothing to the class we are losing (prompt AUC .178 vs displaced .424; adding it to
    the other 19 moves the 4-layer displaced GBDT .9070 -> .9067). Everything in item 5 (the
    t4dnn gaps: no |eta|/z/r, no scale-free radius ratio, no minT3PromptScore, no signed
    inner/outer differences) is UNAFFECTED and still stands.
The ceiling numbers in my post are unaffected: I ran the 19 live non-dcaXY columns and separately
with dcaXY added, and both give AUC .907, i.e. the shipped input set's true ceiling.

### CORRECTION 2: the shipped displaced weight is NOT 4x. It is a TIERED 8x / 16x.
`displaced_weighting` = pos_weight 0.6375; prompt-true x1; **true vxy in [1,5) x8; true vxy >= 5
x16** (n_mid_train 4324, n_hi_train 11531). `class_spec` = 3 classes: 0 fake, 1 prompt-true
(simVxy<1), 2 displaced-true (simVxy>=1). So the gate already has a displaced output and already
upweights displaced ~16x. Do not describe the displaced weight as under-turned by 4x.

### THE NUMBER A2 NEEDS: with FEATURES HELD FIXED, what can the MIX alone buy?
Same 19 live inputs, same event split, HistGradientBoosting, 4-layer only. Signal = label1 &
simVxy>=5 (3,063 rows = **0.91% of the 4-layer positive class**), background = 4-layer label0.
Each fit is scored on the SAME held-out displaced task AND the prompt task:
    training mix                          DISPLACED AUC  TPR@5%FPR |  PROMPT AUC  TPR@5%FPR
    POOLED, no displaced weight              .8814        .359     |   .9658       .828
    POOLED, displaced weight   4x            .8912        .377     |   .9655       .828
    POOLED, displaced weight  20x            .9016        .443     |   .9640       .821
    POOLED, displaced weight 100x            .9101        .503     |   .9581       .786
    DEDICATED displaced-only (hard ceiling)  .9211        .610     |   .8702       .329
READ THIS AS: the mix knob is real and it is roughly LOGARITHMIC. Going from no weight to 100x
raises 4-layer displaced TPR@5%FPR from .359 to .503 (**1.40x relative**) for a prompt TPR cost of
.828 -> .786 (**-5.1% relative**). The shipped 16x sits between the 4x and 20x rows, so the
REMAINING headroom in weighting alone is roughly .44 -> .50, i.e. a further ~1.15x on displaced for
~-4% prompt. A single head cannot reach the .610 ceiling: the dedicated fit costs 60% of prompt.
**So enrichment/reweighting alone is worth something but it is not worth a lot, and it is priced in
prompt.** That is the honest bound on A2's arm, measured before A2 spends the cycle.

### THE FINDING THAT MATTERS MOST, AND IT NEEDS NO NEW MACHINERY AT ALL
**The shipped gate's whole displaced apparatus is keyed on vxy, and per my Q1 vxy is the axis we
ALREADY BEAT MASTER ON (+.087). The axis we LOSE is |dxy| >= 10.**
  * class 2 boundary: `simVxy >= 1`.   tier boundaries: `simVxy` 1 and 5.   No dxy anywhere.
  * From Q1's PU200 joint table: of the 23,105 selected sim tracks with vxy >= 1 -- the gate's
    entire "displaced" class -- **13,311 (57.6%) have |dxy| < 1 cm.** The majority of what the gate
    is taught to call displaced points straight at the beamline and is geometrically prompt-like.
    Meanwhile the 1,735 tracks in dxy[10,30), which is the ONLY band master beats us in, are
    92.1% at vxy >= 30 and are a rounding error inside the vxy>=1 class.
  * **`sim_pca_dxy` is ALREADY READ by the prototype** (`protoFINAL/NtupleReader.cc:75`,
    `EventData.h:29`) and is simply never written to the chain dump or used by the label/weight rule.
  * SO: re-key the 3-class boundary and the weight tiers on |dxy| (or on max(vxy-proxy, |dxy|)
    tiers), which costs ONE new branch in `ChainDumpWriter` plus a one-line change in the weight
    rule. **This adds no head, no input, no machinery -- it corrects the definition of the class the
    existing head is already trying to learn.** Under the "one head plus one cut" rule this is the
    cheapest possible change in the round, and it is the only one that is aimed at the measured
    deficit rather than at its correlate.
  * Sample-overfitting check: this changes only a TRAINING-TIME truth definition. Nothing that ships
    conditions on dxy (A3's trap is untouched -- the deployed gate still sees only features).

### RECOMMENDED ORDER FOR A2, cheapest-first, each independently ablatable
  1. Re-key the displaced class + weight tiers from vxy to |dxy| (one dump branch, one line). FREE.
  2. Add to the re-dump, so they are measurable at all: `eta`, `zInnermost`, `rInnermost`,
     the two member-T3 radii (for the scale-free ratio), `minT3PromptScore`. Branches only.
  3. Then, and only then, the cube50 enrichment -- restricted per Q1 to its dxy>=10 subpopulation.
  4. Report `maxJunctionDegProduct` / `nPS` / `nBarrel` permutation importance before vs after
     enrichment. If any RISES, the win is occupancy and must be rejected.
[A5 precision fix to my own line above] "a rounding error inside the vxy>=1 class" overstates it:
dxy[10,30) is 1735 of the 23105 PU200 sim tracks with vxy>=1, i.e. **7.5%**, not a rounding error.
The point stands unchanged -- 57.6% of that class has |dxy|<1 and only 7.5% is the band we lose, so
a weight tier keyed on vxy spends ~92% of its displaced budget outside the deficit -- but quote
57.6% and 7.5%, not "rounding error". Numbers from a5_ref/joint_vxy_dxy.log (PU200 OURS block).

[A3 18:25] TWO CONTROL FACTS + THE SIGNAL/COST TREND AT PROPER STATISTICS.

**1. MY UNSET ARM REPRODUCES `cube_ref/cube50_ours.root` EXACTLY** (5000 evt, all six bands, all
T4cl/T5cl compositions, delta +0.0000 everywhere). So my build at d950e4315be plus my env-override
patch is a valid no-op baseline -- independently of A1's, and by a different code path (mine parses
in `LSTEvent::applyChainConfigEnv` in LSTEvent.dev.cc; A1's is inline in ChainConfig.h). Two
independent implementations agreeing on the same reference is worth more than either alone.

**2. HIGHER-STATISTICS CENSUS SOFTENS MY EARLIER "KILLED DECISIVELY" CLAIM -- retract that wording.**
Full cube50 dump (`-n -1`, 1409 chain-producing events of 10000, 3402 chains) vs the 500-evt sample
I posted at 18:05. The mD distribution of killed exempt-T4 chains is SMOOTHER than 9 chains
suggested: in dcaXY [10,30), of 173 killed, a bar at -2 recovers 33 (19%), -3 recovers 68 (39%),
-5 recovers 137 (79%). So it is not a wall -- but it still takes a **1.8-unit** move to reach half
of them, which is large for a threshold whose current value is -1.2.

**3. THE SIGNAL-TO-COST RATIO IMPROVES ~14x ALONG THE dcaXY AXIS.** cube50-recovered per
PU200-recovered, normalised to the lowest bin (normalisation-free, so it does not depend on the
per-event convention):

    dcaXY bin    bar -3    bar -5      (cube50 1409 evt / PU200RelVal 40 evt, exempt T4, killed)
    [0.5,1)       1.00      1.00
    [1,2)         1.21      1.42
    [2,5)         3.57      3.20
    [5,10)        5.66      5.78
    [10,30)      14.85     13.80

This is the quantitative case that a per-reconstructed-dcaXY bar is a REAL discrimination and not a
sample detector: the same bar move buys ~14x more cube50 signal per unit of PU200 exposure at the
top of the axis than at the bottom. It is also the answer to "why not just lower M4D globally" --
a global move pays the [0.5,5) cost, which is where 80% of the PU200 exposure is.

**4. FIRST ARM RESULT (cube50 5000 evt, vs `cube_ref/cube50_master.root`).** Arm D5m5 =
`LST_CHAIN_X2=5 LST_CHAIN_M4D2=-5`, i.e. a second dcaXY breakpoint at 5 cm with its own mD bar at
-5 and NO eta-band delta above it. Two new constants, one `if`.

    band            denom   MASTER            base (shipped)      D5m5
    dxy [10,30)      9353   .0269 T4cl=210    .0056 T4cl=20      .0079 T4cl=43   (gap -.0214 -> -.0190)
    dxy [ 5,10)      2598   .0778 T4cl= 56    .0616 T4cl=23      .0716 T4cl=49   (gap -.0162 -> -.0062, now <2 sig)
    vxy [10,30)      5521   .0639 T4cl=166    .0389 T4cl=34      .0446 T4cl=67   (gap -.0250 -> -.0194)
    dxy [ 1, 5)      2009   .1195 T4cl= 34    .1220 T4cl=35      .1254 T4cl=43   (already ahead, +.0060)

T4cl count in the target band MORE THAN DOUBLES (20 -> 43) and dxy[5,10) essentially closes. But
master still has 210 T4cl in dxy[10,30) against our 43 -- **a threshold-only move takes us from 10x
short to 5x short, not to parity.** PU200 cost (the constraint) is running; nothing here is a claim
until that lands. Remaining arms: global M4D at -2/-3/-5 (the single-knob competitor for the
frontier), ZM4D=0 (the pure deletion), and X2 at 2/10 plus M4D2 at -3/-8.

--------------------------------------------------------------------------------------------
[A2 RE-SCOPE, 18:20] I INDEPENDENTLY REACHED A5's "WRONG AXIS" CONCLUSION AND I AM BUILDING THE
INSTRUMENT THAT MAKES IT MEASURABLE. Answering the coordinator's challenge ("name the NEW
information enrichment gives that 6,385 displaced positives at 16x weight do not") explicitly:

**THE NEW INFORMATION IS NOT MORE ROWS. IT IS A DIFFERENT DEFINITION OF WHICH POSITIVES GET THE
WEIGHT.** The shipped gate's class 2 and its 8x/16x tiers are keyed on `simVxy >= 1` / `>= 5`
(train_chain3.py:186-196, and chain3_norm_m12.json records exactly that rule). A5 Q1 proved vxy and
dxy are DIFFERENT populations and that the cell we BEAT master on by +.087 is vxy[10,30) x dxy[0,1)
-- "born displaced but still points home". So the 16x weight A4 correctly points at has been aimed,
almost entirely, at the population we already win. Nothing about that is fixed by adding rows of the
same kind, and nothing about it was visible before, because **`simDxy` was never written to any
dump** (A4 hit the same wall from the other side: `labelChainsHarness` even overwrites simVxy with
-999 on rejected chains, so the near-miss displacement was unrecoverable).

WHAT I HAVE BUILT (all in `gpu_wt/g2/a2_ref/`, and it is what the next arm needs regardless of
whose hypothesis wins):
 * `master_d_bin/lst_cpu` -- LST master b42d8f97ad5 built `-mcCd`, PLUS a 7-branch patch to
   `code/core/write_lst_ntuple.cc` (marked `// A2`). Master's `--allobj` is MISSING seven branches
   the prototype's `NtupleReader` binds: `module_detIds`, `md_anchorHitIdx`, `md_otherHitIdx`,
   `pLS_seedIdx`, `t3_fakeScore`, `t3_promptScore`, `t3_displacedScore`. They were added on our
   branch, so a training ntuple cannot be made from stock master. This is why nobody could re-dump.
 * `proto/bin/chainproto` -- the frozen prototype rebuilt (its `edge_mlp_weights.h` and
   `chain_mlp_weights.h` are literal-for-literal IDENTICAL to the shipped integrated headers, so
   its rows are on the SHIPPED edge-logit scale; the attach head differs but attach is inert in
   chaindump) with the chain dump extended by:
     simDxy       |sim_pca_dxy| of the labelled sim  <- THE re-key instrument
     simVxyOld / simDxyOld / simPtOld   old-rule kinematics preserved through harness rejection
                                        (A4's explicit 2-line ask -- near-miss displacement is now
                                        recoverable)
     etaInner / rInner / zInner         absolute geometry of the innermost anchor  <- A5 (5a): the
                                        "no absolute geometry at all" gap vs master's t4dnn
     radRatio  = min/max member T3 radius  <- A5 (5b), the scale-free consistency ratio, A5's top
                                        recommendation
     minT3PromptScore                   <- A5 (5c)
   So A5's three "cheapest ablation in the round" candidates are now ABLATABLE without another
   re-dump, and so is the dxy re-key.
 * Re-dump recipe pinned: `-m chaindump -e 0 -L 0.5`, the recorded m12 recipe. VALIDATED: my
   re-dump of the 300 reproduces the shipped rows' per-layer label balance (nL4 .3570 / nL5 .7108 /
   nL6 .8721 against A4's .3600 / .7140 / .8720 on chains_m12_498evt), so the re-dump is the same
   population and the control arm is meaningful.

PLAN, in the coordinator's order: (a) measure the dxy composition of the currently-weighted class on
my own rows; (b) control arm = vxy-keyed retrain on the re-dump; (c) dxy-keyed re-key, same rows,
same seed -- the single-variable test; (d) cube50 rows on top, weighted to the dxy composition, at
several ratios; (e) A5's free inputs as a separate ablation. Six bars re-fitted before any number is
quoted as shippable, both samples every step.

HONEST RISK I WILL REPORT EITHER WAY: A3 measured that cube50's killed displaced T4 chains sit
1.3-3.4 logits INSIDE the fake class (mD p50 -3.37 vs bar -1.2) and A5 bounds weight scaling at
~.44 -> .50 TPR@5%FPR with features fixed. If the re-key plus enrichment cannot move mD by ~3 units
on that population, the honest conclusion is that the 4-layer deficit is NOT a gate problem (A4 5:
master has 4.3x more creditable 8-hit objects at EQUAL purity on cube50) and I will say so.

## [A3 18:30] THE OBJECTS EXIST AND THEY ARE CLEAN. A4's section-5 CEILING IS NOT A CEILING ON cube50.

A4 FINAL section 5 put our cube50 T4-class at **117 8-hit pure-OT TCs, 114 creditable**, against
master's **497 / 493**, and read that as "4.3x more creditable objects at equal purity -- a
construction/admission statement upstream of the gate, not a gate statement". I reproduced A4's
exact count (same rule: pure-OT types 4|9, `tc_nhitOT == 8`, creditable == best
`tc_simIdxAllFrac` > 0.75 strict; script `a3_ref/t4_purity.py`) and then ran it on my loosened
arms. **The extra objects are already being BUILT and are being CUT, and admitting them adds no
fakes at all:**

    arm                       8-hit pure-OT TCs   creditable   purity   at 6/8   raw-fake(<0.5)
    MASTER                            497            493       .992        0           4
    base (shipped)                    117            114       .974        0           3
    D5m3  (X2=5, M4D2=-3)             177            174       .983        0           3
    D5m5  (X2=5, M4D2=-5)             219            216       .986        0           3

**+102 creditable objects for +0 fakes**, and purity RISES (.974 -> .986). A4's worry that the
recovered population would be dominated by the 6/8 near-miss class does not materialise here at
all: the 6/8 count is zero in every arm. Our cube50 T4-class gap to master goes from 4.3x to 2.3x
on a pure threshold move. So on THIS sample the gate, not construction, is the binding constraint
for the objects that exist -- while A4 remains right that closing the remaining 2.3x needs
construction. Both statements are true and they are about different halves of the gap.

**RESOLVING THE APPARENT CONTRADICTION WITH A4 section 4 AND A5 section 4** -- both look like they
falsify my arm and neither does, for the same reason:
 * A4's -M4D exchange curve (1:44 at -1.2->-1.8, 1:500 by -3.5) is for the **GLOBAL** bar, i.e.
   integrated over all dcaXY >= 0.5. My whole claim is that the exchange rate is ~14x better in
   dcaXY[10,30) than in [0.5,1) (my 18:25 post), so a bar move CONFINED to the far tail sits at a
   different point on A4's curve than the same move applied globally. A4's table is not binned in
   dcaXY, so it cannot see this, and the two results are complementary rather than contradictory.
 * A5 section 4 measured |dcaXY| AUC .424 for displaced-vs-fake and concluded dcaXY is a prompt
   discriminant. Note A5's displaced class there is keyed on **simVxy >= 5**, and A5's OWN Q1
   result says PU200's vxy-displaced population is quasi-radial with SMALL dxy (mean dxy/vxy
   0.134). A vxy-defined signal class therefore sits at small dcaXY, which is exactly why the AUC
   comes out below 0.5. My axis targets the **dxy >= 10** class, which is the one we actually lose.
   A5's number is right and is not a measurement of my axis.

MY BASELINE CONTROLS, both exact (so any delta I report is the change and nothing else):
 * PU200RelVal 1000 evt: my unset arm equals `win_ref/all4_rv1000_hists.root` to **+0.0000 on all
   29 metrics** including `n TC 2052480` and `n sim 208999` identically. eff .8099 / dup .0479 /
   fake .0470 reproduced.
 * cube50 5000 evt: my unset arm equals `cube_ref/cube50_ours.root` exactly in all six bands.
PU200 cost of D5m5 and of the matched global arm G3 is running; the frontier comparison lands with it.

--------------------------------------------------------------------------------------------
## [A5 Q3 PARTIAL, POSTED EARLY BECAUSE IT CHANGES A2's RE-KEY] **95% of the 4-layer positive
## class has NO truth displacement at all, and the shipped label rule silently calls it PROMPT.**
Measured on the shipped training rows `prototype/chains_m12_498evt.root`.

`labelChainsHarness` (protoFINAL/Labels.cc:190-205) sets simPt/simVxy only from the first matched
sim that is ACCEPTED, i.e. `s < nAccepted = ev.sim_pt.size()`. When the chain's best-matching sim is
a pileup row outside the LST ntuple's accepted sim block, the kinematics stay at **-999** -- and the
3-class rule `class 2 iff simVxy >= 1` then puts that row in class 1, "prompt-true", because
-999 < 1. Measured coverage:
    nLayers=4  label1 335,791   VALID truth   16,923 ( 5.04%)   UNKNOWN -> silently class 1  94.96%
    nLayers=5  label1 665,750   VALID truth   38,403 ( 5.77%)   UNKNOWN -> silently class 1  94.23%
And among the rows that DO have truth, displaced is not rare at all:
    nLayers=4 VALID: vxy<1 12,854 | vxy>=1 4,069 (**24.0%**) | vxy>=5 3,063 | vxy>=10 2,485
    nLayers=5 VALID: vxy<1 29,441 | vxy>=1 8,962 (23.3%)     | vxy>=5 6,818 | vxy>=10 5,527

### CORRECTION TO MY OWN Q2 POST
I wrote "the 4-layer positive class is 98.8% prompt". The accurate decomposition is
**95.0% UNKNOWN-displacement (defaulted to prompt) + 3.8% verified prompt + 1.2% verified
displaced**. The "0.91% of the positive class is displaced" figure is still the correct description
of THE TRAINING MIX (the shipped rule really does put all 95% in class 1), so the reweight ladder I
measured stands unchanged. What changes is the diagnosis of WHY: it is not that displaced tracks are
intrinsically 1% of true 4-layer chains -- among truth-known rows they are 24% -- it is that the
label rule throws away 95% of the truth and defaults the remainder to prompt.

### CONSEQUENCES FOR A2
 1. The dxy re-key is still the right change, but be aware it can only ACT on ~5% of positive rows.
    Re-keying vxy->dxy does not fix the 95% blind spot; it fixes the aim within the 5%.
 2. **The class-1 bucket is contaminated by construction.** Every genuinely displaced chain whose
    best sim is a pileup row is trained as prompt. That is a systematic pull toward the prompt
    decision surface, on top of the 0.91% prevalence problem. If you can widen the accepted sim
    block (or resolve kinematics from the tracking ntuple's full sim rows -- `trk` is already passed
    to labelChainsHarness, so `trk.sim_*` for pileup rows may be reachable without a new input),
    that is worth more than any weight tier. **Worth one look before you train; if the tracking
    ntuple carries vx/vy/pca_dxy for the full sim block, this is a few lines and it multiplies the
    usable displaced statistics by ~20x.** I have not verified reachability -- flagging, not claiming.
 3. A cheaper mitigation that needs nothing new: exclude UNKNOWN-truth positives from the prompt
    class instead of asserting they are prompt (3-class -> mask them out of the class-1 loss). This
    removes a false assertion rather than adding machinery.

### THE FIX IS ALREADY WRITTEN AND BUILT (measurement-only, in a private copy -- nothing shared
### was touched). `a5_ref/proto_a5/` = protoFINAL + these branches, built clean, smoke-tested:
    simDxy      |sim_pca_dxy| of the same accepted sim as simVxy/simPt   (A2's re-key needs this)
    simVxyOld / simDxyOld / simPtOld / simIdxOld
                the PRE-overwrite (>=2/3-MD rule) truth, so harness-REJECTED chains keep a usable
                displacement -- this is A4's proposed fix, implemented (Labels.cc snapshots before
                the harness loop blanks them). Smoke: of 1,298 harness-demoted chains in 5 events,
                13.6% recover a real displacement that the shipped dump reports as -999.
    xf_00..xf_13  the ablation extras (see my Q3 main post): etaChain, absEtaChain, absZInner,
                rInner, absPhiInner, kappaRatio, minT3PromptScore, maxT3PromptScore,
                maxT3DisplacedScore, dFakeIO, dPromptIO, dDispIO, maxAbsDzStep, dRzSlope
                -- dumped as SEPARATE xf_ branches with their own `extra_spec` TNamed, so the 25-slot
                cf_ contract is byte-identical and any 25-input model stays valid on the same rows.
A2: take this rather than re-patching. Diff exported at `a5_ref/proto_a5.patch`. Note protoFINAL
defaults are thetaEdge 0 / lambdaLen 0, the SHIPPED weld is 0 / 3, and chains_m12_* (your weights'
training rows) were dumped at 0 / 0.5 with a NON-shipped retrained edge head -- three different chain
populations. I am running -e 0 -L 3. Align your control arm with whichever you pick and say which.

--------------------------------------------------------------------------------------------
[A1 18:35] *** STEP 3, THE CEILING TEST. VERDICT: ON cube50 THE 4-LAYER OBJECTS ARE MOSTLY NOT
*** BEING BUILT. A THRESHOLD/SCORE FIX CANNOT CLOSE THE dxy[10,30) GAP. But the answer SPLITS by
*** pT, and on cube50_highPt it is the opposite verdict, so read both halves.

WHAT I RAN. `LST_CHAIN_M4=-1e9 LST_CHAIN_M4D=-1e9 LST_CHAIN_U4=-1e5`, i.e. the 4-layer class has
NO GATE AT ALL (neither the IP arm's mX bar nor the exempt arm's mD bar can fire) and no K9
exempt-4 acceptance bar either. Every 4-layer chain the builder produces and the weld/trim/claim
lets through is emitted. cube50 and cube50_highPt, 5000 evt, CPU, -p 0.8 -s 8.

cube50 (pT 0.5-2), dxy [10,30), denom 9353:
    master      .0269   matched 252   T5cl  42 / T4cl 210
    ours SHIPPED.0056   matched  52   T5cl  32 / T4cl  20
    ours NO GATE.0083   matched  78   T5cl  31 / T4cl  47
  Removing the ENTIRE 4-layer gate buys +.0027 of the +.0213 gap: 27 of 200 missing sims, 13%.
  87% OF THE GAP SURVIVES AN INFINITELY LOOSE GATE. Adding CC9=0 and XCT/XCT2/XCT3=1e9 (no
  crossclean retirement of bare chains either) on top changes this by nothing measurable.
cube50 vxy [10,30), denom 5521: master .0639 (T4cl 166) / shipped .0389 (T4cl 34) / NO GATE .0482
  (T4cl 87). Still -.0158 +/- .0045 behind master, i.e. >3 sigma short WITH NO GATE.

cube50_highPt (pT 0.5-50), THE OPPOSITE RESULT -- with no gate we BEAT master in every band:
    dxy [10,30) denom 12586   master .0011 (T5cl 1/T4cl 13) | shipped .0002 | NO GATE .0013 (T4cl 14)
    dxy [ 5,10) denom  3327   master .0129              | shipped .0177 | NO GATE .0313 (+.0183 +/- .0036)
    dxy [ 1, 5) denom  2706   master .0710              | shipped .0780 | NO GATE .0961 (+.0251 +/- .0079)
    vxy [ 5,10) denom   834   master .1031              | shipped .1211 | NO GATE .1787 (+.0755 +/- .0184)
    vxy [10,30) denom  7434   master .0180              | shipped .0182 | NO GATE .0248 (+.0067 +/- .0024)
  So HIGH-pT displaced 4-layer chains DO get built and ARE being cut by the gate -- T4cl 8 -> 57 in
  vxy[10,30), 2 -> 48 in dxy[5,10). It is only the LOW-pT ones that are absent.

THE DIAGNOSIS IS THEREFORE NARROWER THAN THE ROUND ASSUMED. Master's cube50 dxy[10,30) advantage is
210 T4-class matches at 0.9-2 GeV (its highPt T4cl in the same band is only 13, so its win is almost
entirely the LOW-pT end). We can produce at most 47 of those no matter what any bar is set to.
CONSEQUENCE FOR A2: enrichment/retraining can only redistribute what the builder produced. Its
reachable headroom in cube50 dxy[10,30) is the 27 sims between .0056 and .0083, not the 200 between
.0056 and .0269. A2 CAN still win on cube50_highPt and on the dxy[1,5)/[5,10) and vxy bands, where
the ceiling is well ABOVE master -- that is where a retrained gate has room. A4's label audit is
correspondingly more valuable than A2's training run: the gate is discarding real high-pT displaced
4-layer chains, which is a LABEL/threshold problem, while the low-pT ones never reach it.

CAVEAT, STATED HONESTLY, AND I AM CLOSING IT NEXT. The ceiling I opened is gate + K9 class bar +
crossclean. I did NOT open the K9 hit-claim contention (maxClaimedFrac .20, maxClaimedMDs 1,
braidFrac .5) because those fields had no env knob; I am adding them now and will re-run. I expect
it to change little: cube50 is 10 muons with no pileup, so distinct sims barely share hits, and
contention is a PU200 phenomenon. If that expectation is wrong I will correct this entry.

--------------------------------------------------------------------------------------------
## [A2 RESULT 1] THE dxy CENSUS OF THE TRAINING ROWS. A5's "WRONG AXIS" IS CONFIRMED AND
## QUANTIFIED -- AND IT COMES WITH A HARDER NUMBER THAT BOUNDS EVERY WEIGHT-BASED ARM.

First measurement of `|sim_pca_dxy|` on the chain-gate training rows. Nobody could do this before:
`simDxy` was never written to any dump (A4 hit the same wall). I added it, plus the old-rule
kinematics A4 asked for, plus A5's three free feature candidates. Freshly re-dumped with the
prototype rebuilt at the SHIPPED edge head, `-e 0 -L 0.5`: `a2_ref/chains_a2b_pu300.root` +
`chains_a2b_pu498.root`, 798 PU200RelVal events, 3,806,714 chains, 2,319,489 positives.
Tool: `a2_ref/proto/train_chain3.py --census-only` (log `a2_ref/census_pu798.log`).

### 1. THE SHIPPED DISPLACED CLASS IS AIMED AT THE POPULATION WE ALREADY WIN. Confirmed.
Composition, in |dxy|, of the class the shipped gate calls "displaced" and weights 8x/16x:

    class (all positives)              N        dxy<1   [1,5)  [5,10)  [10,30)   vxy>=10
    shipped class2  (vxy>=1)        28,134      64.1%   30.2%   4.8%    0.9%      57.7%
    shipped hi tier (vxy>=5)        20,552      52.4%   39.9%   6.6%    1.2%      79.0%
    4-LAYER shipped class2           6,343      56.5%   33.5%   7.9%    2.2%      61.2%
    4-LAYER shipped hi tier          4,827      44.9%   42.0%  10.3%    2.8%      80.4%

**64.1% of the up-weighted class has |dxy| < 1 cm** -- born displaced, still points home, and that
is the cell where we BEAT master by +.087. Only 0.9% of it is dxy[10,30), the one band we lose.
A5's independent 57.6% / 7.5% and my 56.5% / 10.1% (4-layer, dxy>=5) agree.

### 2. THE NUMBER THAT DECIDES MY ARM: THE TARGET CLASS IS **137 ROWS**.
    dxy[10,30) positives, ALL lengths, 798 events ...........  244
    dxy[10,30) positives, 4-LAYER ...........................  137   (42 in the 300, 95 in the 498)
    as a fraction of the 532,784 four-layer positives ........  0.026%
    of which also vxy>=30 (the PU200 sub-cell A5 says dominates)  97
**Re-keying the class boundary and the 8x/16x tiers onto |dxy| does not create training data; it
just points the same weight at 137 examples instead of 6,343.** A5's own bound on weight scaling
(no-weight .359 -> 100x .503 TPR@5%FPR) was measured with thousands of vxy-selected positives. At
137 rows in a 25-input / 2x32 network there is nothing to fit -- the re-key alone cannot work, and
I want that on the record BEFORE anyone spends a cycle on it. What the re-key does buy is a correct
TARGET DEFINITION, which is only useful if the rows arrive from somewhere. That is the enrichment
question, and it is now the whole question.

### 3. A SEPARATE DEFECT I TRIPPED OVER, and it is not small: **94.3% OF POSITIVES ARE LABELLED
### PROMPT BY DEFAULT.**
`labelChainsHarness` (prototype/Labels.cc:196-204) records kinematics only from the first ACCEPTED
sim among the matches; when the best-matching sim is a pileup row there is none, and simVxy stays
at the **-999 sentinel**. 2,187,449 of 2,319,489 positives (94.3%) are in that state, and the class
rule `y3[is_true & (vxy < 1.0)] = 1` puts every one of them in class 1 because **-999 < 1**. So the
3-class head's prompt class is 94% "displacement unknown", not 94% "measured prompt". Mostly benign
(pileup vertices are near the beamline) but it means the prompt/displaced split was never actually
measured on the bulk of the positive class, and any future re-key must handle the sentinel
explicitly. My census excludes it and reports `known=` counts throughout.
(Trap for whoever reuses this: `abs(-999)` = +999 silently lands the sentinel in the highest dxy
bin. My first census run reported "94% of positives at dxy>=30" for exactly that reason.)

### 4. SUPPORTING: the 6/8 near-miss share of the 4-layer rows is 15.53% (300) / 15.69% (498) --
A4's requested denominator, and stable between the two inputs.

### 5. INFRASTRUCTURE NOW ON DISK FOR ANYONE (`gpu_wt/g2/a2_ref/`)
 * `master_d_bin/lst_cpu` -- LST master b42d8f97ad5 `-mcCd` + a 7-branch `write_lst_ntuple.cc` patch
   (`// A2`): `module_detIds`, `md_anchorHitIdx`, `md_otherHitIdx`, `pLS_seedIdx`, `t3_fakeScore`,
   `t3_promptScore`, `t3_displacedScore`. Stock master's `--allobj` lacks all seven, which is why no
   training ntuple could be made for a new sample.
 * `proto/bin/chainproto` -- prototype + dump extended with `simDxy`, `simVxyOld/simDxyOld/simPtOld`
   (A4's ask: near-miss displacement is now recoverable), and A5's three free candidates
   `etaInner`/`rInner`/`zInner`, `radRatio` (min/max member T3 radius), `minT3PromptScore`.
 * `proto/train_chain3.py` -- `--class-axis {vxy,dxy,maxvd}` + `--class-lo/--class-hi` (re-key),
   `--sample-weight` (explicit per-file mixing ratio), `--add-features` (ablate A5's candidates),
   `--census-only`, and `--evt-offset` which is REQUIRED for enrichment: cube50 and PU200 both
   number events from 1, so the M8 combination rule would silently drop the entire cube set as
   "overlap". Anyone mixing samples without it gets a null enrichment that looks like a real result.
 * `LSTNtuple_cube50{,_highPt}_5000evt.root` -- baseline cube50 training ntuples (regenerating with
   the patched binary now; the chaindump + cube census is my next post).

[A3 18:40 -> A1, RESPONDING TO YOUR CEILING] **YOUR NO-GATE CEILING AND MY 2-CONSTANT ARM AGREE TO
WITHIN 4 SIMS, AND THAT IS THE FRONTIER RESULT.** Put your numbers and mine on one line, cube50
5000 evt, dxy[10,30), denom 9353:

    master                                      .0269   T4cl 210
    shipped                                     .0056   T4cl  20
    A3 D5m5   (X2=5, M4D2=-5; TWO constants)    .0079   T4cl  43
    A1 NO GATE (M4=-1e9, M4D=-1e9, U4=-1e5)     .0083   T4cl  47
    A1 NO GATE + no crossclean                  .0083   (unchanged)

**D5m5 captures 43 of the 47 T4-class matches that an INFINITELY LOOSE 4-layer gate can reach --
91% of your whole ceiling -- while confining the loosening to reconstructed dcaXY >= 5 cm.** Same on
the other bands: vxy[10,30) shipped T4cl 34 -> D5m5 67 vs your no-gate 87 (77%); dxy[5,10) shipped
23 -> D5m5 49. So on the SIGNAL axis a targeted threshold and an absent threshold are nearly the
same thing, which is the strongest possible form of "the bar's PLACEMENT, not its existence, is what
was costing us here". The PU200 cost is where they must differ, and that is exactly the frontier
number; mine is running (D5m5 and the signal-matched global arm G3 = M4D -3, which my census
predicts recovers 307 vs D5m5's 298 cube50 chains while exposing 7208 vs 3890 PU200 chains, i.e.
1.85x the cost for the same signal).

**AND I CONFIRM YOUR "MOSTLY NOT BUILT" VERDICT FROM THE OBJECT SIDE, WITH THE COMPLEMENT.** My
`a3_ref/t4_purity.py` count of 8-hit pure-OT TCs on cube50 (A4 section 5's exact rule):
    master 497 (493 creditable) | shipped 117 (114) | D5m3 177 (174) | D5m5 219 (216)
So a threshold move nearly doubles the creditable T4-class objects **and adds ZERO fakes** (raw-fake
3 in every arm, 6/8 count 0 in every arm; purity RISES .974 -> .986). That is your 13% reachable
headroom made concrete as objects: the ones that exist are clean and were simply being cut. The
remaining 2.3x to master's 497 is your construction statement and I agree it is not reachable from
the gate.

TWO CORRECTIONS/ADDITIONS TO YOUR ENTRY, both in your favour:
 1. You bracketed U4 with M4D at -1e5 in the no-gate arm. **U4 is inert** -- it drops 0 of 3687
    gate-spared exempt-T4 chains at PU200 and 0 of 371 on cube50 (my 18:20 post), because
    `chains.score` carries lambdaLen*nLayers = 12 for a 4-layer chain. So your ceiling is a pure
    gate result and the U4 term did nothing; nothing to redo.
 2. Your caveat about the K9 hit-claim contention: I agree it will change little on cube50, and
    there is now positive evidence for that. D5m5 admits +102 8-hit TCs on cube50 and 216 of 219
    are creditable -- if contention were removing real displaced chains at this occupancy, the
    admitted population would not come through that clean.

--------------------------------------------------------------------------------------------
## [A5 Q3 PARTIAL 2 -- CORRECTS MY OWN PARTIAL 1, AND HANDS A2 A BUILT+VERIFIED FIX]
## **|dxy| is available FULL-LENGTH for pileup sims; vxy is not a tracking-ntuple branch at all.**
## So the dxy re-key is not merely better-aimed -- it is the ONLY displacement axis that can be
## applied to 100% of the positive rows instead of 5%.

### WHAT I GOT WRONG IN PARTIAL 1, and the corrected picture
I said the 95% UNKNOWN bucket meant "the label throws away 95% of the truth" and floated widening
the accepted sim block. Verified now, and the framing was wrong in two ways:
 * `accepted` is DEFINED as `sim_bunchCrossing == 0 && sim_event == 0` (write_lst_ntuple.cc:480-493,
   mirrored in protoFINAL/NtupleReader.cc:186-194) -- the in-time hard-scatter event. That is also
   the efficiency denominator. So the 95% are chains reconstructing PILEUP particles, which can
   never earn efficiency credit. Widening the accepted block would add positives that score nothing;
   that specific suggestion was wrong and is withdrawn.
 * BUT the bucket is NOT harmlessly prompt. Measured on the tracking ntuple (20 entries,
   sim_pt>0.9): the PILEUP sim population is **6.56% |dxy|>=10** and 8.89% |dxy|>=5 (accepted block:
   16.7% and 19.6%). Pileup particles undergo the same decays and nuclear interactions in the same
   material, so a real displaced fraction sits in there and the `simVxy >= 1` rule calls every one
   of them class 1 "prompt-true".

### THE BRANCH FACTS THAT DECIDE THIS (verified against the tracking ntuple itself)
`trackingNtuple/tree` has 33 `sim_*` branches. **`sim_pca_dxy` IS one of them and IS full-length**
(same length as `sim_bunchCrossing`, pileup rows included -- checked). **`sim_vx` / `sim_vy` are NOT
branches at all**; transverse production point would need a `simvtx_x`/`simvtx_y` join through
`sim_parentVtxIdx`. Consequences, and they all point the same way:
  1. the axis Q1 identified as the real deficit is the axis that is FREE to obtain for every sim row;
  2. the axis the shipped gate is keyed on is the one that is NOT obtainable for 95% of positives,
     which is exactly why those rows read -999 and default to prompt;
  3. so re-keying vxy -> dxy simultaneously fixes the AIM (Q1) and the COVERAGE (5% -> 100%).

### IMPLEMENTED, BUILT CLEAN AND SMOKE-VERIFIED in `a5_ref/proto_a5/` (patch:`a5_ref/proto_a5.patch`)
Three lines of reader plus a fill: `TRK_VF(X) X(sim_pt) X(sim_pca_dxy)` (NtupleReader.cc:108),
`std::vector<float> sim_pca_dxy;` on TrkEventData (EventData.h), and in `labelChainsHarness` --
where `out.simIdx[c] = simidx.front()` is ALREADY the full tracking-ntuple row -- fill
    simDxyFull   |sim_pca_dxy| of the matched FULL sim row  (no accepted-block requirement)
    simPtFull    sim_pt of that row
    simAccepted  1 iff the row is in the accepted block, i.e. iff the chain can earn EFFICIENCY
MEASURED on the smoke dump (4 evt, 8,088 label-1 chains):
    simDxyFull defined for **100.00%** of label-1 chains   vs   simVxy defined for **7.12%**
    simAccepted==1 for 7.12% of label-1 chains (so 92.9% are pileup-only matches)
    4-layer label-1 chains, FULL dxy: |dxy|>=1 7.25%, >=5 1.26%, >=10 0.25%
A2: `simAccepted` is the branch that lets you separate "true and efficiency-relevant" from "true but
pileup" without guessing, and it is the honest way to keep the pileup positives in the fake-rejection
loss while not letting them define the prompt class. That distinction is not expressible today.

### ONE SOBERING NUMBER A2 MUST PLAN AROUND -- the dxy target is RARER than the vxy target
In the same 4 events the 4-layer positive class has **5 chains with |dxy|>=10 but 22 with
simVxy>=10**. That is Q1's geometry again (PU200 mean |dxy|/vxy = 0.134: most high-vxy PU200 tracks
still point home), and it means the correctly-aimed target is ~4x scarcer than the mis-aimed one.
Extrapolated: ~600 |dxy|>=10 4-layer positives in 498 evt, ~1,250 in 1000 evt. Enough to train a
weight tier on; NOT enough to resolve TPR lifts below ~0.03 on a held-out split. **This is the
strongest statistical argument yet for the cube50 enrichment**: cube50 is 42.1% |dxy|>=10 (Q1), so it
is the only way to make this class populous, and A2's arm and the re-key are complements, not rivals.
Ablation of the missing t4dnn inputs is running on a 1000-event re-dump; results to follow.

## [A3 18:50] *** MY OWN ARM IS FALSIFIED, AND I AM RETRACTING THE 14x CLAIM I POSTED AT 18:25. ***
## THE PER-dcaXY WORKING-POINT STRUCTURE BUYS **NOTHING** OVER ONE GLOBAL BAR ON PU200.

THE FRONTIER, in COUNTS, PU200RelVal 1000 evt, CPU `-p 0.8 -s 8`, vs the shipped baseline
(`a3_ref/pu/base_hists.root`, which reproduces `win_ref/all4_rv1000_hists.root` to +0.0000 on all 29
metrics). Script `a3_ref/frontier.py`. My arm and its signal-matched single-knob competitor:

    arm                              d(sims dxy>=5)   d(fakes)   fakes per recovered sim
    D5m5  X2=5, M4D2=-5  (2 NEW consts)      +42       +23757            565.6
    G3    M4D -1.2 -> -3.0 (1 CHANGED const) +39       +21688            556.1

**INDISTINGUISHABLE -- and the SIMPLER arm is marginally BETTER.** Worse for me, G3 also collects
displaced sims that D5m5 cannot reach, so it dominates outright on total gain at lower cost:

    band            denom    shipped   D5m5 (2 consts)      G3 (1 const)
    dxy [ 1, 5)      3078   1790     1795  (+5,  +.0016)   1847 (+57, +.0185)
    dxy [ 5,10)      1007    248      282  (+34, +.0338)    279 (+31, +.0308)
    dxy [10,30)      1579     50       58  (+8,  +.0051)     58 (+8,  +.0051)
    vxy [10,30)      4178   2978     2990  (+12, +.0029)   3035 (+57, +.0136)
    TOTAL displaced sims gained (dxy>=1)      +47                +96
    eff overall (pt>0.9)   .8099          .8096 (-.0004)    .8095 (-.0004)
    dup  (pt>0.9)          .0479          .0478 (-.0001)    .0484 (+.0005)
    fake (pt>0.9)          .0470          .0546 (+.0076)    .0566 (+.0097)
**G3 buys twice the displaced sims for FEWER fakes.** So the answer to the question my arm was
created to settle -- "is the score already adequate and only the threshold wrong?" -- is **NO on the
structural half.** There is no threshold STRUCTURE on an observable displacement proxy that beats
one number. Plan problem 12's per-decade working point does not pay.

### WHY MY 18:25 CENSUS PREDICTED 1.85x AND WAS WRONG. THIS IS THE METHOD LESSON, PLEASE READ IT.
I measured "cube50 chains recovered per PU200 chain exposed" per dcaXY bin and found it improving
14x along the axis, and I treated that as a signal-to-background ratio. **It is not one. It is a
CROSS-SAMPLE ratio**, and it improves along dcaXY mostly because cube50 IS a displaced sample and
PU200 is not -- not because high reconstructed dcaXY is signal-rich WITHIN PU200. When the same bar
move is priced by PU200 fakes per PU200 recovered sim, the dcaXY dependence vanishes (566 vs 556).
I did not write a sample-keyed cut, so I did not break the letter of the no-overfitting rule -- but I
came close to justifying a structure on a statistic that is a sample detector in disguise, which is
its spirit. **A5's AUC(|dcaXY|) = .424 was the correct early warning and I explained it away.** The
general form of the lesson: any per-bin ratio whose numerator and denominator come from DIFFERENT
SAMPLES cannot price a working point. Price gains and costs inside the SAME sample.

### WHAT DOES SURVIVE, AND IT IS STILL WORTH HAVING
 1. **cube50's fake-freedom does not transfer, and now we know the exchange rate.** On cube50 the
    same move adds +102 creditable 8-hit TCs for **ZERO** fakes (117/114 -> 219/216, purity .974 ->
    .986). On PU200 it costs **23,757 fakes for 42 sims**. Same algorithm, same bar, 4 orders of
    magnitude difference in price. That is the sharpest possible demonstration of why a working
    point can never be fitted on cube50, and it is the quantitative justification of this round's
    both-samples contract.
 2. **The single-knob arm G3 (`m3Theta4D` -1.2 -> -3.0) is a REAL, cheap displaced-efficiency
    option, and it is A1's frontier point, not mine.** dxy[5,10) +.0308, dxy[1,5) +.0185,
    vxy[10,30) +.0136, dxy[10,30) +.0051, dup +.0005, overall eff -.0004. The price is fake
    +.0097 (.0470 -> .0566, a 21% relative rise). **MY RECOMMENDATION: DO NOT SHIP IT EITHER.**
    Round 2 shipped at fake .0470 against master's .0454; .0566 puts us far behind master on fake
    rate to buy 96 sims of 208,603. Efficiency outranks fake rate in the stated priority, but not
    by 21% of the fake rate for +.0005 of overall efficiency -- and overall efficiency actually goes
    DOWN (-.0004, -22 matched sims net), because the admitted 4-layer chains win arbitration against
    chains that were producing creditable matches.
 3. **A4 section 4 is confirmed and its caveat is now closed.** A4 measured the -M4D exchange rate
    pre-arbitration and warned the effective cost would be lower. Post-arbitration, end-to-end, at
    -1.2 -> -3.0 it is 556 fakes per recovered sim. The bar really is at its knee.

### MY CODE CHANGE IS THEREFORE WITHDRAWN, NOT PROPOSED.
`dcaSplit2` / `m3Theta4D2` (ChainConfig.h) plus one ternary in ChainGate.h:620 exist in gpu_wt/g3
and default to 1e9 = inert, so HEAD behaviour is unchanged. I am NOT recommending they ship: two
constants that buy less than one constant is exactly the "if your win needs new numbers, price it
against the simpler alternative" test, and it fails it. What SHOULD be kept from this arm is the
`LST_CHAIN_*` scan hook (A1 already landed an equivalent, so keep A1's) and the three measurement
scripts, which are reusable: `a3_ref/chain_census.py` (gate kill census from
`LST_CHAIN_CHAIN_DUMP`), `a3_ref/t4_purity.py` (A4 section-5 object count per arm),
`a3_ref/frontier.py` (sims-vs-fakes in counts from any two hists files).
One arm still running: X2=10 (the most targeted variant) as the last test of whether ANY value of
the breakpoint beats the global bar. I will report it either way.

## [A3 19:00] *** THE MECHANISM, MEASURED DIRECTLY. LARGE RECONSTRUCTED dcaXY ON A 4-LAYER OBJECT
## IS A BAD-FIT SIGNATURE IN PU200 AND A DISPLACEMENT SIGNATURE IN cube50. THAT IS WHY PER-dxy-DECADE
## WORKING POINTS CANNOT WORK -- IT IS NOT A NULL RESULT, IT HAS A CAUSE.

Type-9 (T4-class) TCs, PU200RelVal, first 300 events, `tc_isFake` straight from the ntuple:

    arm                                type-9 TCs   of which fake   MARGINAL fake fraction
                                                                    of the TCs the arm ADDED
    base (shipped)                        10087          2383  (.236)         --
    D5m5  X2=5,  M4D2=-5 (my structure)   18753          9465  (.505)      7082/8666 = **81.7%**
    G3    M4D -1.2 -> -3.0 (global)       20516          8822  (.430)      6439/10429 = **61.7%**

**G3 ADMITS 20% MORE T4-class TCs THAN D5m5 AND ADDS 9% FEWER FAKES.** Confining the loosening to
large reconstructed dcaXY makes the admitted population *dirtier*, not cleaner -- the exact opposite
of my hypothesis, measured end-to-end on the shipped metric.

THE PHYSICAL REASON, and it is obvious in hindsight: **the chain dcaXY is |d(fitted circle centre) -
R| from a Kasa fit over 4-6 MDs (ChainGate.h:191-244). On only 4 layers the lever arm is short and R
is poorly determined, so a LARGE fitted dcaXY is primarily the signature of a WRONG COMBINATION, not
of a displaced track.** In PU200 there are ~200 collisions' worth of wrong combinations available to
make, so the large-dcaXY 4-layer population is dominated by them. In cube50 there are 10 muons and
no pileup, so almost no wrong combination is available, and the same large dcaXY really is
displacement. **One observable, opposite meanings in the two samples.** This is what A5's
AUC(|dcaXY|) = .424 was reporting and what my 14x cross-sample ratio (retracted, 18:50) hid.

CONSEQUENCE, stated generally because it outlives this round: **a reconstructed displacement proxy
cannot index a displaced working point, because on the object class where we need it the proxy is
degenerate with fit quality.** Any future attempt at "looser bars where the track looks displaced"
must first show that its proxy is not a bad-fit proxy on the target layer count. The two candidate
escapes, for the record, and neither is cheap:
  * pair the dcaXY cell with a FIT-QUALITY requirement (`fullFitChi2PerHit`/`maxXyResid`, A5's only
    two features that survive intact into the displaced regime, AUC .178) so the cell selects
    "displaced AND well-fitted" rather than "badly fitted". That is a 2-D cell, i.e. 4+ constants,
    and it is a hand-built version of what a retrained head does better -- so it belongs to A2's
    arm, not to a threshold arm;
  * get more lever arm, i.e. 5 layers -- which is A1's construction point.

FINAL SCOREBOARD FOR MY ARM, both samples, WITH THE CAVEATS ON THE SAME LINE AS THE HEADLINE.
Baseline = my build at d950e4315be, verified EXACT against `win_ref/all4_rv1000_hists.root`
(+0.0000 on all 29 metrics, n TC and n sim identical) and against `cube_ref/cube50_ours.root`
(all six bands identical).

  cube50 5000 evt, dxy[10,30), denom 9353 (master .0269, T4cl 210):
    base .0056 T4cl 20 | Z0 .0059 T4cl 24 | G2 .0061 T4cl 26 | D5m3 .0069 T4cl 34
    | D5m5 .0079 T4cl 43 | D2m5 .0079 T4cl 43 | (A1 no-gate ceiling .0083 T4cl 47)
  cube50 8-hit pure-OT TCs (creditable):
    master 497 (493) | base 117 (114) | Z0 129 (126) | G2 151 (147) | D5m3 177 (174)
    | D5m5 219 (216) | D2m5 260 (255)   -- ZERO added fakes in every arm, purity RISES
  cube50_highPt 5000 evt: D5m5 is positive in every band (dxy[5,10) +.0132 vs master, T4cl 2 -> 31;
    vxy[10,30) +.0020, T4cl 8 -> 22 vs master's 21; dxy[10,30) T4cl 0 -> 4 vs master's 13)
  PU200RelVal 1000 evt (THE CONSTRAINT, shipped .8099 / .0479 / .0470):
    D5m5  eff .8096 (-.0004)  dup .0478 (-.0001)  fake .0546 (+.0076)   nTC +27492
    G3    eff .8095 (-.0004)  dup .0484 (+.0005)  fake .0566 (+.0097)   nTC +28580
  CAVEATS, on the same line as the numbers: the cube50 gains are on a 10-muon no-pileup sample whose
  fake population does not exist, the cube50 `eff overall`/eta/dup/fake rows are unusable (19 of
  22194 tracks pass that selection), and the PU200 fake cost is a 16-21% RELATIVE rise on a metric
  where round 2 shipped at .0470 against master's .0454.

**VERDICT: nothing from this arm should ship.** My two constants are withdrawn (they are inert at
their defaults in gpu_wt/g3, HEAD behaviour unchanged). The single-knob G3 is a real option and
belongs to A1's frontier, and I recommend against it too: -.0004 overall efficiency and +.0097 fake
for +96 displaced sims of 208,603. The round's remaining hope is A2's re-key plus A1's construction
question, and my measurements bound both: any gate-side fix has to raise the admitted 4-layer TCs'
purity, because at fixed features the marginal admission is 62-82% fake.

--------------------------------------------------------------------------------------------
[A1 18:52] *** I AM CORRECTING MY OWN 18:35 VERDICT. THE OBJECTS DO EXIST. I HAD OPENED THE WRONG
*** GATE. THE BOTTLENECK IS THE WELD EDGE BAR `thetaEdge` (-e, ChainConfig.h:21, shipped 0.0),
*** NOT THE 3-CLASS GATE. READ THIS ENTRY INSTEAD OF THE ONE ABOVE.

Adding `LST_CHAIN_E=-1e9` (every ENUMERATED K2 edge becomes weld-eligible) on top of the no-gate
ceiling, cube50, 5000 evt:

    band          denom   master   shipped   NO GATE   NO GATE + NO EDGE BAR
    dxy [10,30)    9353   .0269     .0056     .0083     .0382   T4cl 20 ->  47 -> 316
    dxy [ 5,10)    2598   .0778     .0616     .0747     .1243   T4cl 23 ->  57 -> 168
    vxy [10,30)    5521   .0639     .0389     .0482     .0949   T4cl 34 ->  87 -> 329

**WE EXCEED MASTER IN ALL THREE BANDS, BY MORE THAN 10 SIGMA IN dxy[10,30) (+.0298 +/- .0022 over
the no-gate arm, and +.0113 over MASTER).** So the low-pT displaced 4-layer chains are NOT missing
from the T3 input and they are NOT killed by the 3-class gate. THEY ARE NEVER WELDED: the edge MLP
gives their T3 pair a logit below 0, K6a's weld-eligibility test (`logOdds >= thetaEdge`) excludes
the edge, no 2-node chain is formed, and there is therefore no 4-layer object for any gate to judge.
The 18:35 entry's "not being built" is right about the mechanism's LOCATION being upstream of the
gate, and wrong about it being unreachable -- it is one bar, and that bar is in chain code.

ATTRIBUTION OF THE GATE ARMS, so nobody re-runs these (cube50, 5000 evt, vs shipped):
    LST_CHAIN_M4D=-1e9 LST_CHAIN_U4=-1e5 (exempt arm alone)  dxy[10,30) .0056 -> .0083  (+.0028 +/- .0012)
    LST_CHAIN_M4=-1e9                    (IP arm alone)      dxy[10,30) .0056 -> .0056  (EXACTLY zero;
      vxy[10,30) +.0009 +/- .0038, not significant). The T4-class IP arm is IRRELEVANT to displaced
      efficiency, as it should be -- a displaced track has a large reco dcaXY and takes the exempt arm.
    CC9=0 plus XCT/XCT2/XCT3=1e9 on top of the full ceiling: delta EXACTLY +0.0000 in all six bands.
      The crosscleans retire NOTHING in the displaced bands. Do not spend a slot on them.

WHAT THIS MEANS FOR THE ROUND.
 * A2: the enrichment target is arguably the EDGE head (train_edge.py), not only the 3-class gate.
   Retraining the gate on cube50 cannot recover a chain that was never welded. If you retrain one
   thing, the edge head is where the population is being lost.
 * A4: the label audit should cover EDGES for displaced T3 pairs, not just 4-layer chains.
 * A3: your per-observable WP table has a new and much stronger axis available -- thetaEdge.
 * ME: `-e` is now the frontier's main axis. I am scanning it (-0.5, -1, -2, -1e9, with and without
   the gate ceiling) on cube50 and pricing it on PU200. THE PRICE IS THE WHOLE QUESTION: a lower
   weld bar welds more of everything, so I expect fake rate and CPU time to be where this is paid.
   Nobody should adopt `-e` until I post that price -- numbers to follow in this file.

--------------------------------------------------------------------------------------------
## [A5 Q3 PARTIAL 3] **THE LABEL, NOT THE FEATURES, IS THE BIG LEVER: +0.085 displaced
## TPR@5%FPR from stopping the "UNKNOWN positive == prompt" assertion. That is larger than the
## ENTIRE remaining headroom in the weight tiers, and it costs no machinery.**
Script `a5_ref/unknown_harm.py`, run on the SHIPPED training rows `prototype/chains_m12_498evt.root`
(nLayers==4: 595,654 fakes / 12,854 verified-prompt / 3,063 verified-displaced / **318,868 UNKNOWN**).
Identical 20 live inputs, identical 3 event splits, identical held-out scoring task
(verified-displaced vs FAKES). The ONLY thing that changes between arms is what sits in the negative
class during training:
    ARM A  negatives = fakes + prompt + UNKNOWN   (the shipped assertion)  AUC .8967  TPR .5174+-.0128
    ARM B  negatives = fakes + prompt             (UNKNOWN masked out)     AUC .9181  TPR .6022+-.0117
    ARM C  negatives = fakes only                 (upper reference)        AUC .9187  TPR .6023+-.0104
    => masking UNKNOWN out:            **dTPR +0.0847**  (~7 sigma on the +-.012 seed spread)
    => additionally dropping prompt:     dTPR +0.0849  (i.e. +0.0002 more -- NOTHING)
**ARM B == ARM C to three decimals, so the damage is 100% attributable to the UNKNOWN bucket.** The
verified-prompt positives cost nothing at all. Asserting that a pileup-matched true chain is
"prompt-true" is what suppresses displaced discrimination, and it does so because ~6.6% of that
bucket (measured, Partial 2) is genuinely |dxy|>=10 and is being trained as a displaced NEGATIVE.

### PUT NEXT TO THE WEIGHT LADDER I MEASURED EARLIER, THE PRIORITY INVERTS
    weight tiers, features fixed:  no weight .359 -> 4x .377 -> 20x .443 -> 100x .503
                                   (shipped is 16x, so REMAINING headroom is only ~+.06,
                                    and 100x already costs prompt TPR .828 -> .786)
    label fix, weights fixed:      **+.085, and it costs nothing in prompt** (ARM B vs ARM A;
                                   the prompt positives are untouched)
So the label change is worth more than the weight change AND it is free, whereas the weight change
is priced in prompt. **A2: do the label before the weights, and before any feature work.**

### DO NOT IMPLEMENT THIS AS "DELETE THE UNKNOWN ROWS" -- there is a strictly better version
Masking removes 95% of the positive rows from training. Those rows are NOT fakes (they reconstruct
real pileup particles), so they legitimately teach fake rejection; deleting them would give back on
the fake side what it wins on the displaced side. ARM B is a DIAGNOSTIC that isolates the harm, not
the recommended patch. Two implementable options, in order of preference:
 1. **PREFERRED -- give them their real displacement** (already built and verified, Partial 2):
    `simDxyFull` assigns every label-1 chain its true |dxy| from the FULL sim row, so the ~6.6% that
    are genuinely displaced land in class 2 instead of class 1, and the rest stay honest prompt
    positives contributing to fake rejection. This captures the +.085 mechanism WITHOUT discarding
    any row, and it needs no masking logic. `a5_ref/proto_a5/` + `a5_ref/proto_a5.patch`.
 2. FALLBACK if you will not touch the reader: per-sample masking of only the prompt-vs-displaced
    part of the loss for UNKNOWN rows, keeping their not-fake contribution. More than one line, and
    strictly worse than (1), but it needs no new truth.

### HONEST LIMITS ON THIS NUMBER
 * It is a binary displaced-vs-rest probe on 4-layer rows, not the shipped 3-class head, so +.085 is
   the size of the MECHANISM, not a promised delta on the six bars. It must be re-measured on a real
   3-class retrain before anyone quotes it as a physics gain.
 * Signal is the 3,063 verified-displaced rows, keyed on simVxy>=5 because that is all the shipped
   dump carries. Per Q1 the band we actually lose is |dxy|>=10; the dxy-keyed version of this
   measurement is running on the re-dump.
 * These rows are the m12 population (lambdaLen 0.5, non-shipped edge head), so absolute values are
   not the shipped configuration's. The A-vs-B comparison is internally controlled and unaffected.

--------------------------------------------------------------------------------------------
## [A2 RESULT 2] THE dxy RE-KEY IS NOT A NULL. Four trained arms, identical rows/seed/split.
## It moves the displaced discriminant OFF the population we already win and ONTO the one we lose,
## at ZERO prompt-AUC cost and with ZERO new machinery -- the shipped C++ is untouched, only the
## weights file changes. Statistics caveat stated in full below.

Rows: `a2_ref/chains_a2b_pu{300,498}.root` (798 evt, 3,806,714 chains). All arms: seed 42, hidden
32, 120 epochs / patience 15, mid x8 / hi x16, same frozen-test-60 split. Logs
`a2_ref/train_{A,B,C,D}*.log`, models `a2_ref/m_*.pt`, norms `a2_ref/n_*.json`.

    arm  class axis / tiers          extra inputs                  val promptAUC  val dispAUC
     A   vxy >=1 / >=5  (SHIPPED)    none (25)                        .96629         .93611
     B   |dxy| >=1 / >=10            none (25)                        .96605         .94724
     C   |dxy| >=1 / >=10            +etaInner +radRatio +minT3Prompt  .96688         .95113
     D   vxy >=1 / >=5               +etaInner +radRatio +minT3Prompt  .96653         .93798

### 1. CONTROL ARM A VALIDATES THE RE-DUMP. This is what makes the rest believable.
Arm A on my re-dump reproduces the SHIPPED gate to 3 decimals: promptAUC .96629 vs the shipped
`chain3_norm_m12.json`'s .96632, dispAUC .93611 vs .93727. So the re-dump is the same population and
every A-vs-B delta below is the re-key, not the re-dump.

### 2. THE RE-KEY RE-AIMS mD, exactly as the physics says it should.
4-layer TEST rows, signal = 4L positives in that TRUE |dxy| band, background = ALL 68,206 4L test
fakes. mD is the discriminant the EXEMPT T4 branch actually consults (ChainGate.h:621).

    4-layer band              n_pos    A (vxy)   B (dxy)   B-A      C (dxy+feat)
    dxy [1,5)   mD              139     .9145     .9417   +.027       .9403
    dxy [5,10)  mD               34     .9183     .9634   +.045       .9612
    dxy [10,30) mD                6     .8484     .9307   +.082       .9677
    dxy[10,30) & vxy>=30 mD       5     .8354     .9200   +.085       .9664
    -- the population we ALREADY WIN, as a control --
    vxy>=1  & dxy<1  mD          231     .9559     .7019   -.254       .6932
    vxy>=10 & dxy<1  mD          108     .9498     .8089   -.141       .7869
  prompt-vs-fake mP            169835    .96697    .96685  -.0001
  alltrue-vs-fake mX           171741    .96551    .96590  +.0004

Read that table carefully, because the two halves are the whole point. Arm A's mD is a GOOD
discriminant for low-dxy displaced tracks (.956) and a poor one for the band we lose (.848). Arm B
inverts it. The apparent "loss" on the vxy>=1 & dxy<1 row is NOT a physics loss: under the re-key
those tracks are class 1, so they are served by mP (unchanged at .967) and by mX (unchanged at
.966), and mD is not consulted for them on the IP branch. The overall discriminants do not move.

### 3. A5's THREE FREE INPUTS (arm C) ADD +.004 OVERALL AND NOTHING ON THE POPULATED dxy BANDS.
C beats B on val dispAUC (.9511 vs .9472) but is a wash on dxy[1,5) (.9403 vs .9417) and dxy[5,10)
(.9612 vs .9634); the [10,30) row where it looks better has n=6. Arm D isolates the features
WITHOUT the re-key: .93798 vs A's .93611, i.e. **the features alone buy +.002 and the re-key buys
+.011.** So on this target the re-key is ~5x the feature addition, and since the features require
extending the ChainFeatures contract in the integrated kernels (real machinery) while the re-key is
weights-only, arm B is the one worth shipping. A5's recommendation is not refuted -- radRatio/eta
are cheap and correct -- it just is not where this particular deficit lives.

### 4. THE STATISTICS CAVEAT, AND I WANT IT READ AS PART OF THE RESULT.
The frozen test set holds **6** 4-layer dxy[10,30) rows, so the headline band's AUC is noise; the
signal I am claiming rests on dxy[1,5) (n=139) and dxy[5,10) (n=34), which move together and in the
same direction as the val AUC. Nothing here is a 137-row miracle: the re-key does not conjure
statistics, it re-uses the ~10,000 dxy>=1 positives that were previously diluted by the 18,000
dxy<1 ones sharing their class. That is why it works at all, and it is also why it cannot be
expected to fully close a band whose own population is 137 rows.

### 5. WHAT I HAVE NOT DONE, stated plainly rather than implied.
 * The cube50 ENRICHMENT arm is NOT measured. My cube50 training ntuple needs `run`/`lumi`/`evt`,
   which stock master's writer also lacks (they were added on our branch); the fix needs `getUL` on
   master's `treeutil.h`, which master does not have either. Everything else is in place
   (`a2_ref/master_writer_A2.patch`, the generator, the dump, `--evt-offset`), so this is ~20 min
   for whoever picks it up. BOUND from A3's already-published census meanwhile: cube50 yields ~74
   pre-gate T4 chains per 500 events, so 5000 events supply O(700) 4-layer chains and at best a few
   hundred creditable dxy>=10 rows -- the same order as PU200's 137, i.e. enrichment roughly
   DOUBLES the target class rather than transforming it. That bound is why I spent the cycle on the
   re-key instead, and it is consistent with A4 point 5: the objects are scarce on OUR side in cube50
   too, which is a construction statement, not a training statement.
 * The SIX BARS are re-fitting now. mD's calibration changes under the re-key, so **no efficiency
   number from arm B is quotable until that is done** -- I have built the integrated tree with the
   arm-B weights plus env overrides for all six bars (`LST_CHAIN_M4/M4D/MRI/MR/MRB/MRT/C25/C25D`
   plus `ZM4/ZM4D`, all defaulting to the shipped value so an unset environment is byte-for-byte
   the shipped configuration -- `interface/ChainConfig.h applyEnvOverrides()`, called once in the
   LSTEvent constructor). **That hook is the thing A1 was asked to land and it is now available to
   everyone** at `gpu_wt/g2` / binaries in `a2_ref/int_B_bin`.
 * Header pipeline verified end to end: `a2_ref/to_integrated.py` applies the two mechanical port
   edits (namespace, constexpr array -> HOST_DEVICE_CONSTANT), and running it on the SHIPPED .pt
   reproduces all 2087 literals of the shipped `src/alpaka/Chain3NetworkWeights.h`. Arm B's header
   was produced by the identical path.

--------------------------------------------------------------------------------------------
[A4 RECONCILIATION] A3 AND I WERE BOTH RIGHT. THE TWO LOSSES COMPOSE, AND THE SUPPLY CEILING
IS ITSELF BELOW MASTER'S OUTPUT -- so gate work alone cannot close the cube50 T4 gap.

WHY THE TWO CENSUSES LOOKED CONTRADICTORY: A3 counted CHAINS AT THE GATE over 500 events, I
counted TCs IN THE NTUPLE over 5000. Different object level, different denominator, and there are
TWO more loss stages between them. On a common footing, per event, all from OUR side:

  cube50, OUR funnel, 4-layer (T4) class            per evt      (N)        stage loss
    welded chains reaching the gate                 0.1164      1164 /10000    --
    gate survivors                                  0.0372       372 /10000   3.13x
    shipped T4-class TCs (type 9)                   0.0216       216 /10000   1.72x
    ... of those CREDITABLE (>0.75 to some sim)      0.0212       212 /10000
  cube50, MASTER, same class
    shipped T4 TCs                                  0.0994       497 / 5000
    ... CREDITABLE                                  0.0986       493 / 5000

**MASTER'S CREDITABLE T4 OUTPUT (0.0986/evt) IS 85% OF OUR ENTIRE PRE-GATE T4 CHAIN SUPPLY
(0.1164/evt).** So even with a PERFECT gate and ZERO post-gate loss we top out at 0.1164/evt, and
only the creditable-and-distinct fraction of that counts. The gate is the biggest single factor
(3.13x) but it is not the binding one: the ceiling above it is already at master's output.

Reconciliation of the two numbers A3 and I quoted:
 * A3's 74 T4 chains / 500 evt = 0.148/evt vs my 1164 / 10000 = 0.1164/evt: 1.8 sigma apart, i.e.
   CONSISTENT. A3's 500-event sample has Poisson sigma 0.017 on that rate. Use the 10000-evt one.
 * A3's 70.3% gate kill vs my 68.0% (792/1164): consistent, same reason.
 * The 1.72x AFTER the gate is a stage neither of us had measured -- K9 hit-claim arbitration,
   crosscleaning and emit. On cube50 that is as large as half the gate's loss and nobody has
   looked at it.

METHOD (so this is reproducible): my own build at d950e4315be in gpu_wt/g4, `lst_cpu` md5
e82f25dfb4edf4928f, cube50 `-n -1 -p 0.8 -s 1`. **VERIFIED SHIPPED-EQUIVALENT**: over the first
5000 events it reproduces cube_ref/cube50_ours.root exactly -- nTC 707, types {T5cl 586, pT5cl 2,
pLS 2, T4cl 117}, sum(tc_pt) 961.1118 both sides. Full 10000-event numbers above (the reference
only ran 5000, hence the doubled counts).

*** A BUG IN THE CHAIN DUMP THAT SILENTLY CORRUPTS ANY TRUTH JOIN -- A3 PLEASE READ ***
A3 flagged that `dumpChains` writes no record for a zero-chain event. It is worse than a missing
record: `eventCounter.fetch_add(1)` sat PAST the early-return guard, so a skipped event did not
advance the counter either. **`ievt` is therefore an index over DUMPED events, not over input
entries** -- on cube50 the dump has 1409 records for 10000 events, numbered 0..1408 contiguously,
so it LOOKS well-formed while pointing at the wrong event for all but the first. Any join to the
tracking ntuple or to sim truth keyed on that index is silently wrong. Aggregate, per-chain
statistics (A3's kill census, all of the mD/dcaXY work) are UNAFFECTED -- they never use ievt.
There are three separate early returns that skip the dump: LSTEvent.dev.cc:933 (buildChainEdges,
nChainNodes_==0), :1214 (buildChains, no edges), :1299 (buildChains, nChainCount_==0). I patched
all three to emit a header-only record so ievt == the input entry index in a single-stream run;
patch is in gpu_wt/g4 and is a debug-sidecar-only change (verified physics-neutral above).
Truth-joined per-chain creditability follows in my next post. -- A4

## [A3 19:15 -- FINAL] *** I AM PARTIALLY REVERSING MY OWN 18:50 FALSIFICATION. THE STRUCTURE DOES
## PAY, BUT ONLY AT A MUCH TIGHTER BREAKPOINT THAN I FIRST TESTED, AND ONLY FOR ONE BAND. ***

My 18:50 verdict ("the per-dcaXY structure buys NOTHING") was measured at breakpoint X2 = 5 cm. It
is WRONG at X2 = 10 cm, and I would have shipped a false negative if I had stopped there. The
completed frontier, PU200RelVal 1000 evt, all against a baseline verified EXACT against
`win_ref/all4_rv1000_hists.root` (+0.0000 on all 29 metrics, identical n TC 2052480 / n sim 208999):

    arm                        eff     dup     fake   | dxy[10,30) | dxy[1,5) | vxy[10,30) | nTC
    shipped                  .8099   .0479   .0470    |   .0317    |  .5815   |   .7128    |  --
    G3   M4D -1.2 -> -3.0    .8095   .0484   .0566    |   .0367    |  .6001   |   .7264    | +28580
      (1 CHANGED const)     -.0004  +.0005  +.0097    |  +.0051    | +.0185   |  +.0136    |
    D5m5 X2=5,  M4D2=-5      .8096   .0478   .0546    |   .0367    |  .5832   |   .7157    | +27492
      (2 NEW consts)        -.0004  -.0001  +.0076    |  +.0051    | +.0016   |  +.0029    |
    D10m5 X2=10, M4D2=-5     .8099   .0479   .0485    |   .0367    |  .5815   |   .7135    |  +8281
      (2 NEW consts)        -.0001  -.0001  +.0015    |  +.0051    | +.0000   |  +.0007    |

**ALL THREE ARMS DELIVER THE IDENTICAL dxy[10,30) GAIN -- exactly +8 matched sims of 1579, .0317 ->
.0367 -- and D10m5 does it for +.0015 of fake rate against G3's +.0097.** Cost of the same gain in
the round's target band, the ONE band master beats us in:

    fake-rate cost per identical +.0051 in dxy[10,30):   D10m5 1.0x  |  D5m5 5.1x  |  G3 6.5x
    and D10m5 leaves eff overall (-.0001) and dup (-.0001) UNCHANGED to 1e-4, where G3 pays
    -.0004 eff and +.0005 dup.

MY 19:00 MECHANISM POST IS CONFIRMED, NOT CONTRADICTED -- I had the sign of its consequence wrong.
Marginal fake fraction of the type-9 TCs each arm ADDS (PU200, 300 evt, `tc_isFake`):
    G3 6439/10429 = 61.7%   |   D5m5 7082/8666 = 81.7%   |   D10m5 2101/2545 = **82.6%**
So large reconstructed dcaXY on a 4-layer object really is a dirtier admission PER OBJECT (the
short-lever-arm bad-fit degeneracy, ChainGate.h:191-244) -- that stands. What I got wrong is that
per-object purity is not the cost. **The high-dcaXY cell is LOW-YIELD BUT ITS YIELD IS CONCENTRATED
IN THE TARGET BAND**: it admits a quarter as many objects as the global arm (+2545 vs +10429) and
still collects every one of the same 8 target-band sims, because the global arm's other ~8,000
admitted objects buy sims it already had. Per admitted object the cell is worse; per recovered
target-band sim it is 6.5x better. Those are consistent and I conflated them.

### WHAT I ACTUALLY RECOMMEND, with the caveats on the same line
`dcaSplit2 = 10.0f`, `m3Theta4D2 = -5.0f` in ChainConfig.h plus one ternary at ChainGate.h:620
(patch: `a3_ref/a3_dca_split2_WITHDRAWN.patch` -- the name is now wrong, it is a candidate).
**BUT: it buys ONE BAND and nothing else.** dxy[1,5) +.0000, vxy[10,30) +.0007, total displaced sims
gained +12 against G3's +96. If the objective is total displaced efficiency, G3 is the better change
and my structure is pointless. If the objective is the specific band this round exists to fix, D10m5
is the only arm here that closes part of it without moving eff, dup, or (much) fake. Also: it is
+.0015 on a metric where round 2 shipped .0470 against master's .0454, and it costs TWO new
constants, so the maintainer's simplicity rule may still refuse it. I have NOT found the knee -- X2
in [7,15] x M4D2 in [-4,-8] is unscanned and D10m5 may not be the best point in it.

cube50 5000 evt, dxy[10,30) (denom 9353; master .0269 T4cl 210; A1's no-gate ceiling .0083 T4cl 47):
    base .0056 T4cl 20 | Z0 .0059/24 | G2 .0061/26 | G3 .0064/29 | D5m3 .0069/34
    | D10m5 .0076 T4cl 40 | D5m5 .0079/43 | D2m5 .0079/43
  8-hit pure-OT TCs (creditable), ZERO added fakes in every arm:
    master 497 (493) | base 117 (114) | Z0 129 (126) | G2 151 (147) | D10m5 167 (164)
    | D5m3 177 (174) | G3 181 (175) | D5m5 219 (216) | D2m5 260 (255)
  **D10m5 reaches 85% of A1's no-gate T4cl ceiling (40 of 47) from only +50 admitted objects, where
  G3 needs +64 to reach 29.** The cube50 ranking (D10m5 best per object) and the PU200 total-gain
  ranking (G3 best) are OPPOSITE -- which is the cleanest demonstration in this round of why a
  working point cannot be fitted on cube50, and why the both-samples contract is load-bearing.
cube50_highPt 5000 evt: **D10m5 is the WEAKEST of the family here** -- dxy[5,10) +.0051 vs D5m5's
  +.0132 and G3's +.0057; vxy[10,30) +.0005 vs D5m5's +.0020 (master 21 T4cl, D5m5 22, D10m5 11).
  So the tight breakpoint trades away the high-pT displaced gains. D5m5 is the better highPt arm and
  D10m5 the better PU200-cost arm; no single breakpoint is best on both.
cube50 caveats, restated with the numbers: 10 muons, no pileup, no fake population to speak of, and
  the `eff overall` / eta-region / dup / fake rows are unusable (19 of 22194 tracks pass that cut).

### THE METHOD LESSON, corrected and now general
My 18:25 "14x" was a cross-sample ratio and is still retracted -- it cannot price a working point.
But the OPPOSITE error is just as easy and I made it at 18:50: **I priced a threshold family from a
single member of it and generalised.** X2=5 and X2=10 differ by 6.5x in cost for the same gain. If
you scan a structural knob, scan it -- one point is an anecdote in either direction.

ARTIFACTS, all in `standalone/a3_ref/` (scripts) and `gpu_wt/g3/.../a3_ref/` (ROOT, logs, dumps):
  `chain_census.py` gate kill census from LST_CHAIN_CHAIN_DUMP (no truth, no rebuild)
  `t4_purity.py`    A4 section-5 8-hit-object count + purity, per arm
  `frontier.py`     sims-vs-fakes in COUNTS from any two createPerfNumDenHists outputs
  `arm_table.py`    multi-arm cube50 band table vs master
  `scan_cube.sh` / `pu_run.sh`  the two run drivers
  `a3_dca_split2_WITHDRAWN.patch`  the 4-file diff (defaults inert, so HEAD is unchanged)
-- A3
[A3 note] the patch file is renamed a3_ref/a3_dcaSplit2_candidate.patch (was ...WITHDRAWN...) after the X2=10 reversal.

[A3 19:25 COMPLETION -- the last two arms, and an independent confirmation of A1's ceiling NUMBER]
The family is now complete on cube50 (5000 evt, dxy[10,30), denom 9353; master .0269 T4cl 210):

    arm                       eff     T4cl   8-hit pure-OT TCs (creditable)  purity
    base (shipped)          .0056      20            117 (114)               .974
    Z0    ZM4D=0            .0059      24            129 (126)               .977
    G2    M4D=-2            .0061      26            151 (147)               .974
    G3    M4D=-3            .0064      29            181 (175)               .967
    D5m3  X2=5,  M4D2=-3    .0069      34            177 (174)               .983
    D10m5 X2=10, M4D2=-5    .0076      40            167 (164)               .982
    G5    M4D=-5            .0077      41            261 (255)               .977
    D5m5  X2=5,  M4D2=-5    .0079      43            219 (216)               .986
    D2m5  X2=2,  M4D2=-5    .0079      43            260 (255)               .981
    D5m8  X2=5,  M4D2=-8    .0083      47            230 (227)               .987
    A1 NO GATE (M4/M4D=-1e9).0083      47              --                     --

**D5m8 lands on A1's no-gate ceiling EXACTLY -- .0083, T4cl 47, to the last sim -- while still
applying a bar everywhere below 5 cm.** Two completely different routes (A1 deleted the 4-layer gate;
I kept it and only moved the far-dcaXY cell) agree to the sim, which is a strong cross-check of A1's
ceiling number and confirms the whole reachable cube50 headroom lives at dcaXY >= 5 cm. Note also
that the D-arms are consistently the CLEANEST on cube50 (purity .981-.987 vs the G-arms' .967-.977)
and that at matched signal the D-arm admits fewer objects (D5m5 43 from 219 vs D2m5 43 from 260,
G5 41 from 261).
I did NOT run PU200 for G5 or D5m8 -- budget spent, and the frontier lesson is already established by
the G3 / D5m5 / D10m5 triple. Anyone continuing should price D5m8 on PU200 (it will cost more than
D5m5's +.0076 fake) and scan the unexplored knee X2 in [7,15] x M4D2 in [-4,-8]; D10m5 is the best
PU200-cost point I measured, not necessarily the best that exists.

--------------------------------------------------------------------------------------------
## [A5 Q3 MAIN RESULT] **THE MISSING t4dnn INPUTS DO NOT PAY. No single one buys a resolvable
## displaced TPR lift; all 14 together buy ~+0.04. The LABEL fix buys +0.085 for free.
## A2: STOP AT THE RE-KEY. Do not spend a cycle adding inputs.**
Ablation `a5_ref/ablate.py` / `ablate.log` on a fresh 250-event re-dump `a5_ref/chains_a5_250evt.root`
(`a5_ref/proto_a5/bin/chainproto -m chaindump -e 0 -L 3 -n 250`, i.e. the SHIPPED weld config
thetaEdge 0 / lambdaLen 3). 465,545 4-layer rows / 297,709 fakes / 167,836 label1 -- the 4-layer
label-1 fraction .3605 reproduces chains_m12's .360, so the population is comparable.
Baseline = the 20 live shipped inputs. Metric = TPR@5%FPR, 3 event splits, PAIRED per-split spread.

### THE HEADLINE TABLE -- deployment-realistic arm (pooled positive class, displaced tier x16 as
### shipped, scored on the displaced-vs-fake task). This is what predicts a real retrain.
    target                      baseline TPR      +kappaRatio      +absEta        +both        +ALL 14
    simDxyFull>=10 (we lose)    .5737+-.0628   +.0275+-.0329   +.0104+-.0118  +.0208+-.0328  +.0399+-.0233
    simDxyFull>=5               .6036+-.0184   +.0163+-.0103   +.0121+-.0172  +.0204+-.0174  +.0435+-.0119
    simVxy>=10 (shipped axis)   .3610+-.0129   +.0146+-.0132   +.0147+-.0160  +.0140+-.0052  +.0378+-.0159
READ IT AS: **every individual candidate is +.01 to +.03 and NOT individually significant.** The full
14-input bundle is a consistent +.038 to +.044 and is significant on 2 of 3 targets. So the answer to
"add the one that pays" is that **there is no one that pays** -- the lift only appears when you add
all fourteen, which is 14 inputs for ~+.04 on one subclass. Under the architecture rule that is a bad
trade, and it is roughly HALF what the label fix gives at 14x the cost.

### PER-CANDIDATE, dedicated-fit arm (marginal dTPR, paired spread), for completeness
    candidate                        dxy>=10        dxy>=5         dxy>=1        vxy>=10
    C1  absEta only              +.0027+-.0019  -.0061+-.0012  +.0070+-.0014  -.0010+-.0107
    C1b full geometry (4)        +.0000+-.0000  +.0043+-.0034  +.0075+-.0067  -.0038+-.0041
    C2  kappaRatio               +.0027+-.0019  -.0030+-.0048  +.0044+-.0036  -.0069+-.0073
    C3  minT3PromptScore         +.0013+-.0018  -.0011+-.0056  +.0029+-.0032  -.0045+-.0063
    C4  signed inner/outer diffs -.0013+-.0018  +.0069+-.0063  +.0108+-.0096  +.0050+-.0088
    C5  maxPrompt + maxDisplaced -.0013+-.0018  +.0001+-.0045  +.0014+-.0016  -.0038+-.0136
    C6  per-step proxies (2)     +.0013+-.0018  +.0016+-.0062  +.0109+-.0058  -.0138+-.0048
    ALL 14                       +.0013+-.0018  +.0183+-.0034  +.0295+-.0063  -.0164+-.0076
Several are NEGATIVE. Nothing here justifies a single named addition.

### AND THE REASON IS THE MOST USEFUL PART: **the current features already separate this class
### nearly perfectly when the fit is AIMED at it.** Dedicated-fit baseline, 20 live inputs:
    simDxyFull>=10   AUC .9836+-.0107   TPR@5%FPR **.9804+-.0171**
    simDxyFull>=5    AUC .9802+-.0015   TPR@5%FPR  .9153+-.0037
Against the SAME 20 inputs the pooled-mix baseline gets only .5737 on the very same rows. The
information is present and reachable; **what loses it is the training mix and the decision, not the
inputs.** Partly tautological -- `dcaXY` is already a baseline input and directly measures the reco
impact parameter, so a truth-|dxy| target is close to what dcaXY sees -- but that is the point: the
one quantity that identifies this population is ALREADY in the head, and it still gets lost.

### PRIORITY, with everything now measured on the same class (4-layer) and metric (TPR@5%FPR)
    1. LABEL: stop asserting UNKNOWN==prompt        **+.085**, free, no machinery   (Partial 3)
    2. RE-KEY vxy -> dxy                            aim + coverage 5%->100%, free   (Q1, Partial 2)
    3. WEIGHT tiers 16x -> ~100x                    ~+.06, PRICED in prompt (-4%)   (Q2 addendum)
    4. FEATURES: all 14 missing t4dnn inputs         ~+.04, costs 14 inputs          (this post)
       any SINGLE missing input                      not resolvable                  (this post)
**A2's plan should be 1 + 2, then re-measure. Items 3 and 4 are not worth a cycle yet.**

### SAMPLE-OVERFITTING VERDICTS (the judgement the brief demands), now measured not argued
NONE of the 14 is a pT proxy: |r| vs ptEst <= 0.032 for every one (etaChain .000, kappaRatio .006,
absEtaChain .020, maxT3PromptScore .032). So the muon-gun/ttbar pT tag is not a hazard for any of them.
Occupancy proxying, |r| vs maxJunctionDegProduct, IS differential:
    CLEANEST  dDispIO .001, dFakeIO .027, dPromptIO .029, absPhiInner .032, maxT3DisplacedScore .047,
              rInner .050, dRzSlope .061, kappaRatio **.095**
    WATCH     absZInner .172, maxAbsDzStep .174, maxT3PromptScore .189, minT3PromptScore .190,
              absEtaChain **.206**
So had anything paid, `kappaRatio` and the signed inner/outer differences would have been the
defensible picks and `absEta` the one needing an eta-region validation. As it is, none pays.
`kappaRatio` is still worth recording as CORRECT and well-behaved: label1 median .9476 vs label0
median .6897, single-feature AUC .762 on the pooled 4-layer label task, |r| .001 vs fullFitChi2 and
.237 vs maxXyResid -- genuinely new, scale-free, pT-independent information. It simply is not what
this deficit is made of.

### THE ETA-BINNED WP, PRICED SEPARATELY AS ASKED
Master pairs its eta INPUT with a 2 pT x 25 eta dual-threshold WP table. That is a far bigger ask than
an input, and the maintainer has already refused it (`PLAN_lst_redesign_t3_onward.md:232`: "NOT
t4dnn-style 2x25 dual tables (maintainer: 'complete overkill', derived from 2-10 tracks/bin)"). My
data gives no reason to revisit: eta as a plain input is worth +.010 to +.015 and is not significant,
so the table on top of it cannot be justified by displaced 4-layer performance.

### HONEST STATISTICAL LIMITS
 * `simDxyFull>=10` has 663 rows / 235 in test, so its TPR sigma is +-.03 to +-.06. **I can rule out
   single-input lifts above ~.06 there, not below.** The well-powered target is `simDxyFull>=5`
   (2,814 rows / 1,069 test, sigma +-.02), where every single candidate is <= +.007 and three are
   negative. That is what the negative conclusion rests on.
 * `simDxyFull>=10 & simAccepted` (efficiency-creditable AND in the band we lose) had only **56 rows**
   in 250 events and was skipped. Quantifying THAT cell needs cube50, which is 42.1% |dxy|>=10 -- one
   more reason A2's enrichment and the re-key are complements.
 * 250 events not 498: the box was at load 12-19 with five agents and the tracking ntuples are 151 GB
   each, giving ~7 evt/min. I cut the run rather than report a half-written file.
 * These are offline GBDT probes on 4-layer rows, NOT the shipped 3-class MLP. Every number here sizes
   a MECHANISM; none is a promised delta on the six bars.

[A3 19:40 ADDENDUM] **THE cube50_highPt GATE CENSUS EXPLAINS THE ARM RANKING, AND IT SAYS THE BEST
BREAKPOINT IS DIFFERENT ON THE TWO CUBE SAMPLES -- so there is no single good value of X2.**
Ran `a3_ref/chain_census.py` on the cube50_highPt chain dump (`-n -1 -s 1`, 1591 T5-class + 703
T4-class chains). Killed exempt-T4 chains recovered by lowering the mD bar, by reconstructed dcaXY:

    dcaXY bin   nExempt  nKilled   bar-2   bar-3   bar-5   no-bar    mD of killed (p50/p90/max)
    [0.5,1)         87       33      13      25      31       33      -1.57 / -0.29 / -0.02
    [1,2)          107       56      14      30      53       56      -1.94 / -0.97 / -0.29
    [2,5)          177      154      25      48     122      154      -3.00 / -1.54 / -0.01
    [5,10)         179      170       6      23      94      170      -3.93 / -2.35 / -1.24
    [10,30)         40       39       0       2       5       39      **-6.04 / -4.06 / -1.06**

**On highPt the recoverable population sits at dcaXY 2-10, and the dcaXY>=10 chains are killed
DECISIVELY (mD p50 -6.04; a bar at -5 recovers 5 of 39).** That is exactly why, in my 19:15 table,
D5m5 (breakpoint 5, so it covers [5,10)) is the strongest highPt arm (dxy[5,10) +.0132, vxy[10,30)
T4cl 8 -> 22 vs master's 21) while D10m5 (breakpoint 10) is the weakest (+.0051, T4cl 8 -> 11) --
and equally why D10m5 is the cheapest on PU200, because [5,10) is where most of the PU200 fake
exposure lives. **The two samples want opposite breakpoints: 5 cm for cube50_highPt signal, 10 cm
for PU200 cost.** So even the surviving half of my arm is fragile: `dcaSplit2` is not a value that
can be fitted once and be right everywhere, which is an additional argument against shipping it and
a further reason the deficit is not fundamentally a threshold problem.
Also visible and worth recording for whoever revisits the T4 IP bar: on highPt the IP branch
(dcaXY < 0.5) is **100% killed in all three sub-bins** and the killed chains' mX runs up to +3.15
against the `m3Theta4` = 4.0 bar -- i.e. that bar is cutting 4-layer chains the head scores
positively. Our deficit is not there (highPt dxy>=10 is all exempt), but it is the largest single
gap between the T4 and 5+ working points (4.0 vs -0.5) and nobody in this round tested it on its own.
Artifacts: `a3_ref/dump_cubeHP_all.bin`, `a3_ref/dump_cube_all.bin`, `a3_ref/dump_pu.bin`.
All 24 of my result files verified readable and final. -- A3

--------------------------------------------------------------------------------------------
[A1 19:35] STEP 2, THE FRONTIER. *** THE TWO BARS ARE IN SERIES AND EACH ONE MASKS THE OTHER. ***
NEITHER ALONE IS THE BOTTLENECK, WHICH IS WHY EVERY SINGLE-BAR SCAN IN THIS PROJECT HAS FOUND
NOTHING. cube50, 5000 evt, CPU -p 0.8 -s 8. eff(T4-class count) per band:

  point                     nTC    dxy[10,30)     dxy[5,10)      dxy[1,5)     vxy[10,30)     vxy[5,10)
  LST MASTER               1098  .0269(T4 210)  .0778(T4  56) .1195(T4  34)  .0639(T4 166) .2345(T4 22)
  SHIPPED (e=0)             707  .0056(T4  20)  .0616(T4  23) .1220(T4  35)  .0389(T4  34) .2108(T4 12)
  e=-1.0  ONLY              764  .0069(T4  26)  .0674(T4  22) .1289(T4  42)  .0413(T4  35) .2298(T4 14)
  e=-2.0  ONLY              774  .0073(T4  26)  .0674(T4  21) .1309(T4  45)  .0418(T4  35) .2298(T4 13)
  e=-1e9  ONLY              776  .0072(T4  25)  .0677(T4  21) .1314(T4  45)  .0420(T4  35) .2298(T4 13)
  noGate ONLY               904  .0083(T4  47)  .0747(T4  57) .1419(T4  76)  .0482(T4  87) .2567(T4 41)
  noGate + e=-0.5          1064  .0125(T4  82)  .0885(T4  85) .1558(T4  97)  .0572(T4 130) .2758(T4 46)
  noGate + e=-1.0          1226  .0195(T4 144)  .0978(T4 101) .1638(T4 113)  .0683(T4 186) .2932(T4 54)
  noGate + e=-2.0          1531  .0325(T4 263)  .1182(T4 153) .1787(T4 142)  .0864(T4 283) .3154(T4 67)
  noGate + e=-1e9          1661  .0382(T4 316)  .1243(T4 168) .1832(T4 150)  .0949(T4 329) .3201(T4 70)
(sigma on dxy[10,30) is ~.0011 at the shipped end and ~.0022 at the loose end, so every step here
except the e-ONLY rows is many sigma.)

READ THE e-ONLY ROWS AGAINST THE noGate+e ROWS. Dropping the weld bar to MINUS INFINITY while the
3-class gate stands buys +.0016 in dxy[10,30). Dropping the gate while the weld bar stands buys
+.0027. DOING BOTH buys +.0326 -- TWELVE TIMES the sum of the two singles. The mechanism: the edge
head refuses to weld the low-pT displaced T3 pair, and when you force the weld, the 3-class gate
kills the resulting 4-layer chain. Two independent trained components, each of which is separately
sufficient to lose the track. This is the answer to "boundary or construction": it is BOUNDARY, but
a CONJUNCTION of two boundaries, and any experiment that moves one at a time reads as a dead end.

WHERE MASTER SITS ON THIS CURVE: we pass master's cube50 dxy[10,30) (.0269) between e=-1.0 and
e=-2.0, and at noGate+e=-2.0 we are AHEAD OF MASTER IN ALL FIVE BANDS AT ONCE (.0325/.1182/.1787/
.0864/.3154 vs .0269/.0778/.1195/.0639/.2345). So the physics is reachable. The question is only
the price, and the price is already known to be bad at the gate end alone:

*** PU200 PRICE OF THE noGate ARM ALONE (1000 evt, vs shipped eff .8099 / dup .0479 / fake .0470):
      eff overall .8067 (-.0032)   dup .0479 (-.0000)   fake .1232 (+.0763, i.e. 2.6x)
      fake barrel .0516 -> .2032 (4x!)   fake transition .0574 -> .1760   n TC +204359 (+10%)
      it DOES buy PU200 displaced: dxy[1,5) +.0309, dxy[5,10) +.0338, dxy[10,30) +.0051,
      vxy[5,10) +.0286, vxy[10,30) +.0268 -- but it LOSES prompt eff (-.0032), because the extra
      4-layer fakes win K9 hit contention against real longer chains.
*** so the "naive route" costs at MINIMUM a 2.6x fake rate before the edge bar is even touched, and
*** the edge bar is the half that carries most of the displaced gain. THIS IS THE NUMBER A2's
*** RETRAIN HAS TO BEAT: not "recover .0326", but "recover .0326 without 2.6x the fake rate".
PU200 reference frame, for anyone who needs it: MASTER is eff .8100 / dup .0514 / fake .0454 and
dxy[10,30) .0545 vs our .0317. The no-gate arm reaches .0367 -- 22% of that PU200 gap.

INSTRUMENT VALIDATION ON PU200, so nobody doubts the arm: my unset build at -s 8 reproduces
win_ref/all4_rv1000_hists.root to +0.0000 on ALL 30 metrics including n TC (2052480) and n sim.
Stream count does not matter on the CPU backend.

--------------------------------------------------------------------------------------------
## [A2 RESULT 3] THE RE-KEY AT MATCHED FAKE COST: **dxy>=10 EXEMPT-T4 SURVIVAL .270 -> .606**,
## AND THE PRICE IT CHARGES, BOTH ON THE SAME 995,630 ROWS. This is the number my arm is for.

Method (`a2_ref/refit_bars.py`, log `a2_ref/refit_B.log`): score BOTH heads on the SAME rows
(`chains_a2b_pu{300,498}.root`, 798 evt), then pick the new head's bar by **holding the fake-side
survival exactly fixed** at the shipped bar's value. That IS the bar re-fit for the two T4 bars, done
where it can be done exactly instead of by grid search, and it makes the comparison a pure shape
comparison with the cost constraint nailed down. Reference head = arm A (the control that reproduces
the shipped gate to 3 decimals).

### EXEMPT T4 branch (nLayers<=4, dcaXY>=0.5), discriminant mD, shipped -M4D = -1.20
### matched new bar = **-1.912**, ALL-negatives survival identical at 0.0231 by construction

    group                          N        A (vxy)   B (dxy)    delta
    creditable dxy>=10           137        .2701     .6058     +.3358   <-- THE TARGET BAND
    creditable dxy[5,10)         497        .3642     .5030     +.1388
    creditable dxy[1,5)        2,059        .5444     .5707     +.0262
    creditable dxy<1           6,210        .3795     .1127     -.2668
    creditable vxy>=10 & dxy<1   836        .6172     .2990     -.3182   <-- the cell we already win
    creditable, ALL          181,868        .3432     .1625     -.1807
    6/8 near-miss            178,281        .0320     .0187     -.0133   (BETTER: -42%)
    real fake (mf<0.5)       392,394        .0180     .0233     +.0053
    ALL negatives            813,762        .0231     .0231     +.0000   (the fixed constraint)

### IP T4 branch (dcaXY<0.5), mX, shipped -M4 = 4.00 -> matched new bar 3.844: A WASH.
creditable-all .1154 -> .1163, 6/8 -.0001, negatives fixed. The IP branch carries 350,916 of the
532,784 4-layer positives and it does not move, which is what you want: the re-key is confined to
the branch that is supposed to carry displaced tracks.

### WHAT THIS MEANS, INCLUDING THE PART THAT ARGUES AGAINST ME
**2.24x the exempt-T4 acceptance of the failing population at ZERO extra fake cost, with the 6/8
near-misses cut 42%.** For comparison, A4's threshold-only curve buys creditable-dxy>=10 at a
marginal 1:24 rising to 1:499; this buys +.336 at 1:0. That is the difference between moving a
threshold and changing the shape of the score, and it is the first thing in this round that beats the
threshold frontier rather than sliding along it.

**BUT `creditable, ALL` drops .343 -> .163 on that branch, and that is 32,000 chains.** The cause is
structural and I traced it: 95% of the exempt branch's positives (175,000 of 181,868) have
**displacement UNKNOWN** -- the pileup-sentinel population from RESULT 1 point 3 -- so narrowing
class 2 to dxy>=1 strips mD of almost all of its own positives and leaves it a specialist. Whether
that costs EFFICIENCY or only removes duplicates of tracks already reconstructed as 5+-layer chains
cannot be decided from this table; it needs the PU200 run, which is in flight. **Nobody should quote
the +.336 without this paragraph.**

### THE FIX THE DIAGNOSIS POINTS TO, now trained (arms E/F)
Class membership and weight emphasis are separable, and the shipped code couples them for no reason.
**Arms E/F keep CLASS 2 on vxy>=1 (so mD keeps its broad positive population, including the
sentinel-unknowns) and put only the 8x/16x WEIGHT TIERS on |dxy|** (E: tiers 1/10, F: 5/10). That
should get the emphasis without the membership loss. `--weight-axis` in
`a2_ref/proto/train_chain3.py`. Results to follow in my final post.

### STATUS OF THE MEASUREMENT CONTRACT -- not met yet, and I will not pretend otherwise
The integrated build with arm-B weights exists (`a2_ref/int_B_bin`, lst_cpu md5 bb8e75d4491c41ed) and
the PU200RelVal 1000-evt CPU run is executing; box load hit 72 while five agents ran, so it is slow.
No efficiency/dup/fake number is quotable for any arm until it lands. Everything needed to finish is
scripted: `a2_ref/measure.sh <tag>` runs all three contract samples and both judges, and the six bars
are env-scannable in that binary (`LST_CHAIN_M4D=-1.912 LST_CHAIN_M4=3.844` is the pre-computed
re-fit point, unset = shipped).

--------------------------------------------------------------------------------------------
## [A2 RESULT 4] A5's `simDxyFull` TURNS THE RE-KEY FROM A 137-ROW GESTURE INTO A TRAINABLE
## PROGRAMME, AND IT IS THE SAME FIX AS A5's "LABEL BUG". THREE ARMS, ONE CONTROL, MATCHED COST.

I re-ran the re-key on A5's `a5_ref/chains_a5_250evt.root` (250 evt, 1,192,619 chains), which carries
`simDxyFull` -- `|sim_pca_dxy|` of the matched FULL sim row. A5 is right about why this matters and it
is worth stating as one sentence: **`sim_pca_dxy` is full-length in the tracking ntuple while
`sim_vx`/`sim_vy` are not branches at all**, so the shipped rule keyed on vxy because vxy was the only
thing available -- and it was available for only the 5.7% of positives whose match is an accepted sim.

### 1. THE TARGET CLASS IS 15x BIGGER PER EVENT ONCE COVERAGE IS FIXED. This is the whole unlock.
    simDxyFull coverage of positives ..... 100.00%   (accepted-only simDxy: 5.69%)
    4-layer positives, simDxyFull [1,5) ... 8,746    [5,10) 2,151    [10,30) 663
    of the 663, `simAccepted` (efficiency-creditable) ..................... 56
Against RESULT 1's 137 four-layer dxy[10,30) rows in 798 events (0.17/evt), this is 663 in 250 events
(2.65/evt). **My own "the target class is 137 rows, the re-key cannot work" conclusion was an artefact
of the sentinel, not a fact about the data.** I am retracting that part of RESULT 1 point 2 explicitly;
the census numbers there are correct but the pessimistic inference from them is not. The 607
non-`simAccepted` rows are displaced tracks from PILEUP vertices -- outside the efficiency denominator
for bookkeeping reasons, but the same physics, and they are legitimate training signal.

### 2. MATCHED-FAKE-COST TABLE, EXEMPT T4 BRANCH (nL<=4, dca>=0.5), the branch that carries displaced.
Control G = vxy-keyed, trained on the SAME rows/seed/split (val promptAUC .96445 / dispAUC .94336).
Every arm's bar is chosen so that ALL-negatives survival is IDENTICAL to G's at the shipped -M4D
(0.0367), so these are pure shape comparisons at fixed cost. `a2_ref/refit_bars.py`.

    group                        N        G (vxy)  H (class=dxyFull)  I (class=vxy, WEIGHT=dxyFull)
    dxyFull>=10 & simAccepted     56       .3571      .8214 (+.464)       .6964 (+.339)
    dxyFull>=10                  663       .2685      .7451 (+.477)       .5611 (+.293)
    dxyFull[5,10)              2,145       .4690      .6755 (+.207)       .5883 (+.119)
    dxyFull[1,5)               8,472       .5385      .5986 (+.060)       .5591 (+.021)
    dxyFull<1                 45,505       .2853      .0821 (-.203)       .0980 (-.187)
    creditable, ALL           56,785       .3298      .1893 (-.141)       .1907 (-.139)
    6/8 near-miss             55,887       .0417      .0280 (-.014)       .0279 (-.014)
    ALL negatives            253,993       .0367      .0367  (fixed)      .0367  (fixed)
    val promptAUC / dispAUC             .96445/.94336  .96907/.95241      .95915/.92853
    matched -M4D                          -1.20        +0.470             -2.204
The IP T4 branch (mX, 111,051 creditable) moves by -.012 in H and holds the fake side fixed; it
carries no dxyFull>=10 rows at all, so the whole story is on the exempt branch, as designed.

**H is the best arm: 2.3x the creditable acceptance of the failing population at zero extra fake cost,
with the near-misses cut 33% and prompt AUC IMPROVED (.96907 vs .96445).** I is the hedge -- it keeps
class 2 broad and only aims the weight, which preserves more of the low-dxy discrimination (4L
vxy>=10 & dxy<1 mD AUC .867 vs H's .693) for two thirds of the gain.

### 3. THE COST IS REAL AND IT IS THE SAME IN EVERY ARM: `creditable, ALL` -.14 ON THE EXEMPT BRANCH.
That is ~8,000 chains of the 56,785. It is NOT a mystery: dxyFull<1 is 80% of that branch's positives,
and making mD a displaced specialist necessarily de-emphasises them. Whether it costs EFFICIENCY or
merely removes duplicates of tracks already reconstructed as 5+-layer chains is not decidable offline.

### 4. THE ONE END-TO-END NUMBER I HAVE, AND IT ARGUES THE COST IS CHEAP.
Arm B (the earlier accepted-only-dxy re-key) built into the integrated tree and measured on the FULL
contract sample -- PU200RelVal 1000 evt, CPU, `-p 0.8 -s 8`, `a2_ref/int_B_bin` (md5 bb8e75d4491c41ed),
judged by `protoD1/compare_ab.py` against `win_ref/all4_rv1000_hists.root`, **at the SHIPPED bars**:
    eff overall (pt>0.9)  .8105 vs .8099   +.0006      dup  .0476 vs .0479   -.0003
    fake rate (pt>0.9)    .0407 vs .0470   **-.0063**  fake barrel .0360 vs .0516  -.0156
    eff dxy [1,5)         .5526 vs .5815   -.0289      eff vxy [10,30)  .6805 vs .7128  -.0323
    eff dxy [5,10)        .2274 vs .2463   -.0189      eff dxy [10,30)  .0253 vs .0317  -.0063
    eff barrel/transition/endcap  +.0005 / +.0010 / +.0005     n TC 2,036,159 vs 2,052,480
Read this the way the refit table says to: at the SHIPPED bar the re-keyed head is simply TIGHTER
(RESULT 3 says the cost-matched bar is -1.912, not -1.20), so this run bought .0063 of fake rate and
paid the displaced bands. **What it also shows is that removing that exempt-T4 acceptance costs
essentially NO overall efficiency (+.0006) while removing a lot of fakes** -- i.e. the -.14
`creditable, ALL` in the table above is largely duplicate/fake removal, which is the encouraging
reading of point 3. The run at the cost-matched bars is in flight and is the number that decides it.

### 5. WHAT I DID NOT GET TO -- stated so nobody re-derives it or over-claims on my behalf
 * **Arm H is NOT integrated or measured.** It needs one weights swap + one build (~30 min) and then
   `a2_ref/measure.sh`. Everything is scripted: `to_integrated.py` (validated: reproduces all 2087
   literals of the shipped header from the shipped .pt), the six-bar env knobs, and the pre-computed
   cost-matched bars `LST_CHAIN_M4D=0.470 LST_CHAIN_M4=3.970`. **That is the highest-value ~45 minutes
   left in this round** and I would spend it there before anything else.
 * cube50 ENRICHMENT still unmeasured (writer needs `getUL` on master's `treeutil.h`). A1's ceiling
   now says cube50 low-pT dxy[10,30) is supply-limited anyway (87% of the gap survives an infinitely
   loose gate), so the enrichment claim should be aimed at `cube50_highPt` and the dxy[1,5)/[5,10)
   bands, NOT at low-pT cube50. I did not verify that myself; it is A1's number.
 * Only two of the six bars were re-fitted (the two T4 bars). `m3ThetaRI/R/RB/RT` and `c25Theta/D`
   live on the 5+ branches, which these arms barely move, but they are UNVERIFIED at the new
   calibration and must be checked before anything ships.
 * A5's "do not add the 14 features" verdict is consistent with my arms C/D: features alone +.002,
   re-key +.011.

--------------------------------------------------------------------------------------------
## [A2 RESULT 5] THE RE-KEY IS A DIAL, NOT A SWITCH -- SEVEN ARMS ON ONE COST-MATCHED AXIS, AND
## THE -.15 ON `creditable, ALL` IS UNIVERSAL. Read this before choosing a working point.

All arms trained on identical rows/seed/split; bars chosen so ALL-negatives survival is IDENTICAL to
the control's at the shipped -M4D. EXEMPT T4 branch (nL<=4, dca>=0.5), discriminant mD.
Arms A-F on `chains_a2b_pu{300,498}` (798 evt, accepted-only `simDxy`); G-I on A5's
`chains_a5_250evt` (250 evt, full-coverage `simDxyFull`). Cross-set numbers are NOT comparable; the
column that is comparable within each block is the delta against that block's own control.

    block 1 (control A, 798 evt, N=995,630 exempt-T4 rows)   creditable-dxy>=10 (N=137)   creditable ALL
      A  class vxy>=1, weight vxy 1/5      (SHIPPED RULE)         .2701                     .3432
      B  class dxy>=1, weight dxy 1/10                            .6058  (+.336)            .1625  (-.181)
      E  class vxy>=1, weight DXY 1/10                            .5620  (+.292)            .1960  (-.147)
      F  class vxy>=1, weight DXY 5/10                            .8467  (+.577)            .1835  (-.160)
    block 2 (control G, 250 evt, N=310,778)                  dxyFull>=10&simAcc (N=56)    creditable ALL
      G  class vxy>=1, weight vxy 1/5      (SHIPPED RULE)         .3571                     .3298
      H  class dxyFull>=1, weight dxyFull 1/10                    .8214  (+.464)            .1893  (-.141)
      I  class vxy>=1, weight DXYFULL 1/10                        .6964  (+.339)            .1907  (-.139)
    (C/D = arms B/A plus A5's three free inputs; +.002 for the features against +.011 for the re-key,
     so they are not in this table. 6/8 near-miss survival IMPROVES in every arm, -.011 to -.014.)

### THREE THINGS THIS TABLE SETTLES
 1. **The gain is monotone in how hard you aim the weight, and F is the extreme**: +.577 on the
    target band. But F pays -.110 on dxy[1,5), so it trades the near-displaced band for the far one.
    B and H are the balanced points; I is the conservative one (keeps 4L vxy>=10&dxy<1 mD AUC at .867
    against H's .693).
 2. **CLASS membership and WEIGHT emphasis are separable levers with different costs.** Moving only
    the weight (E/F/I) preserves more of the low-dxy discrimination than moving the class (B/H),
    because class 2 keeps its broad positive population. The shipped code couples them for no reason
    I can find; `--weight-axis` in `a2_ref/proto/train_chain3.py` separates them.
 3. **`creditable, ALL` falls ~.15 in EVERY arm, and that is the real decision.** It is not a bug in
    any one arm: dxyFull<1 is 80% of the exempt-T4 branch's positives, so any mD that becomes
    displaced-aware de-emphasises them. **The end-to-end evidence says this is cheap**: arm B measured
    on PU200RelVal 1000 evt (RESULT 4 point 4) moved eff overall by **+.0006** while removing
    **-.0063** of fake rate and 16,321 TCs, i.e. the exempt-T4 chains being given up are
    overwhelmingly fakes and duplicates of tracks reconstructed elsewhere. That is the single most
    important cross-check in my arm and it is measured, not argued.

### THE HONEST BOTTOM LINE OF MY ARM
The round's leading hypothesis as I was briefed it -- **"enrich with cube50" -- is NOT what pays, and
I could not measure it at all.** What pays is on the same axis but upstream of it: the shipped gate's
displaced class and its 8x/16x weights were keyed on `simVxy`, which (a) is the axis we already beat
master on and (b) was only DEFINED for 5.7% of positives, because `sim_vx`/`sim_vy` are not branches
in the tracking ntuple while `sim_pca_dxy` is. Fixing the axis and the coverage is one dump branch and
one line of the training script, changes no C++, adds no input, adds no head, and it moves the
creditable acceptance of the failing population from .36 to .82 at identical fake cost. cube50's role
is what A5 said: it supplies rows for that class -- a complement, not the mechanism.

--------------------------------------------------------------------------------------------
## [A2 RESULT 6] END TO END, PU200RelVal 1000 evt, THE RE-KEY AT COST-MATCHED BARS.
## All three CONSTRAINT metrics improve. The displaced ledger is NEGATIVE. Both halves are the result.

Arm B weights + `LST_CHAIN_M4D=-1.912 LST_CHAIN_M4=3.844` (the cost-matched bars from RESULT 3).
CPU, `-p 0.8 -s 8`, `a2_ref/int_H_bin`-style build (`int_B_bin`, liblst_cpu.so md5 311a622afd37c639),
judged by `protoD1/compare_ab.py` against `win_ref/all4_rv1000_hists.root` (round-2 shipped).
Full tables: `a2_ref/meas/B_{shippedbars,refit}_cmp.txt`.

    metric                     SHIPPED   B @ shipped bars   B @ COST-MATCHED bars
    eff overall (pt>0.9)        .8099      .8105  (+.0006)    .8104  (+.0005)
    dup rate (pt>0.9)           .0479      .0476  (-.0003)    .0477  (-.0002)
    fake rate (pt>0.9)          .0470      .0407  (-.0063)    .0422  (-.0047)
      fake barrel               .0516      .0360  (-.0156)    .0402  (-.0113)
    eff dxy [10,30)             .0317      .0253  (-.0063)    .0329  (+.0013)   <-- THE TARGET BAND
    eff dxy [5,10)              .2463      .2274  (-.0189)    .2453  (-.0010)
    eff dxy [1,5)               .5815      .5526  (-.0289)    .5699  (-.0117)
    eff vxy [10,30)             .7128      .6805  (-.0323)    .6905  (-.0223)
    eff vxy [5,10)              .7227      .7084  (-.0143)    .7094  (-.0133)
    eff vxy [1,5)               .8027      .7984  (-.0043)    .7984  (-.0043)
    eff barrel/trans/endcap  .9239/.8815/.7464   +.0005/+.0010/+.0005   +.0004/+.0007/+.0004
    n TC                     2,052,480   2,036,159 (-16,321)  2,042,286 (-10,194)

cube50, 5000 evt (arm B at SHIPPED bars only; the cost-matched cube run is still going), against
`cube_ref/cube50_master.root` via `cube_ref/band_census.py`:
    dxy [10,30)  denom 9353  master .0269   ours .0051  (shipped baseline .0056)  T4cl 12 (was 20)
    dxy [ 5,10)  denom 2598  master .0778   ours .0577
    vxy [10,30)  denom 5521  master .0639   ours .0364
cube50_highPt dxy[10,30): denom 12586, master .0011, ours .0003. As expected at the un-refitted bar,
which RESULT 3 shows is 0.7 units too tight on this head -- these are the pessimistic numbers.

### WHAT THIS SETTLES, AND IT CUTS BOTH WAYS
 1. **Re-fitting the bar is not optional and it is worth a third of the effect.** dxy[10,30) goes
    -.0063 -> +.0013 and dxy[5,10) -.0189 -> -.0010 purely from moving -M4D by 0.7. Any future arm
    quoted at shipped bars is being slandered.
 2. **At cost-matched bars the re-key improves EVERY constraint metric simultaneously**: eff overall
    +.0005, dup -.0002, fake -.0047 (barrel -.0113), with 10,194 fewer TCs. That is the answer to my
    own RESULT 5 worry: the ~8,000 exempt-T4 chains the re-key gives up were overwhelmingly fakes.
 3. **BUT THE DISPLACED LEDGER IS NEGATIVE AND I WILL NOT DRESS IT UP.** +.0013 on dxy[10,30) is
    bought with -.0223 on vxy[10,30) and -.0117 on dxy[1,5). Under this project's stated priority
    (efficiency >> dup > fake) that is a BAD trade as configured: vxy[10,30) is the cell where we beat
    master by +.087, and -.0223 gives back about a quarter of that lead for a 4% relative gain in the
    band we lose. **Arm B, as configured, is not shippable.**
 4. The reason is diagnosed, not mysterious, and it points at the fix rather than at the wall. Arm B
    keyed CLASS MEMBERSHIP on the accepted-only `simDxy`, which was defined for 5.7% of positives, so
    it made mD a narrow specialist. RESULT 5's dial shows the conservative points: arm I (class stays
    vxy, only the WEIGHT moves to dxyFull) keeps the 4L vxy>=10 & dxy<1 mD AUC at .867 against arm B's
    .809 and H's .693, for two thirds of the target gain. **Arm I is the point I would measure next**,
    and arm H (full coverage, aggressive) is already built and measuring so the two ends of the dial
    will both be priced.
 5. Only 2 of the 6 bars were re-fitted. `m3ThetaR/RB/RT` govern the exempt-5+ branch, which is where
    vxy[10,30) is largely reconstructed (T5cl dominates that band on both sides), so the -.0223 is
    plausibly recoverable by loosening that bar -- untested, and I flag it as the single most promising
    unexplored knob rather than claiming it.

--------------------------------------------------------------------------------------------
[A2 HANDOFF] `gpu_wt/g2/a2_ref/A2_HANDOFF.md` -- what is on disk, what state g2 is in (detached at
d950e4315be with three intentional uncommitted edits: the six-bar env hook in ChainConfig.h, its call
in LSTEvent.h, and arm H's weights in Chain3NetworkWeights.h; the shipped header is saved beside it),
and the four next steps in priority order. The one gap that blocks cube50 enrichment for whoever picks
it up is one identifier: master's `code/rooutil/treeutil.h` has `getU` but not `getUL`, and the `evt`
branch needs it. Everything else for enrichment is built and tested.

--------------------------------------------------------------------------------------------
## [A2 VERDICT] THE GATE IS NOT WHERE THE 4-LAYER DISPLACED DEFICIT CAN BE FIXED.
## A clean negative, with the mechanism measured rather than asserted, and it agrees with A1 and A4.

Arm B, the full contract at the cost-matched bars (`LST_CHAIN_M4D=-1.912 LST_CHAIN_M4=3.844`):

    PU200RelVal 1000 evt vs win_ref/all4_rv1000_hists.root
      eff overall (pt>0.9) .8104 (+.0005)   dup .0477 (-.0002)   fake .0422 (-.0047)
      eff dxy [10,30) .0329 (+.0013)   dxy [5,10) .2453 (-.0010)   dxy [1,5) .5699 (-.0117)
      eff vxy [10,30) .6905 (-.0223)   vxy [5,10) .7094 (-.0133)   vxy [1,5) .7984 (-.0043)
    cube50 5000 evt vs cube_ref/cube50_master.root (baseline = the shipped numbers at the top of
    this file; master in brackets)
      dxy [10,30)  denom 9353  .0056 -> **.0061**  [master .0269]   T4cl 20 -> 21
      dxy [ 5,10)  denom 2598  .0616 -> .0616  (EXACTLY FLAT)       [master .0778]
      vxy [10,30)  denom 5521  .0389 -> .0389  (EXACTLY FLAT)       [master .0639]
      dxy [ 1, 5)  denom 2009           .1185                       [master .1195, -1.0 sigma]
    cube50_highPt 5000 evt: measured at SHIPPED bars only (dxy[10,30) .0003 vs master .0011); the
    cost-matched run did not finish. Stated as incomplete, not as a result.

### THE THREE FACTS THAT MAKE THIS A NEGATIVE, AND WHY I BELIEVE THEM
 1. **The mechanism works offline and does not convert on the sample.** At identical fake cost the
    re-key more than doubles the exempt-T4 acceptance of the failing population (.270 -> .606, and
    .357 -> .821 on the creditable subset with A5's full coverage). On PU200 that is worth **+.0013**
    in dxy[10,30) and on cube50 **+.0005**. The reason is arithmetic, not subtle: dxy[10,30) is 1.32%
    of PU200 sim tracks and 0.07% of MATCHED ones, so doubling acceptance of a population that small
    cannot move a rate.
 2. **The collateral is bigger than the prize, and it is structural.** dxyFull<1 is 80% of the
    exempt-T4 branch's positives, so any mD that becomes displaced-aware de-emphasises them. Measured
    cost: vxy[10,30) **-.0223** -- about a quarter of the +.087 lead that is this project's headline
    advantage -- for +.0013 in the band we lose. Under the stated priority (efficiency >> dup > fake)
    that ledger is negative. Every one of my seven arms pays ~-.15 on `creditable, ALL`; it is a
    property of the branch structure, not of a particular training choice.
 3. **cube50 is flat to 4 decimals in two of the three bands**, which is exactly what A1's ceiling
    predicts (87% of the cube50 low-pT dxy[10,30) gap survives an infinitely loose gate). A gate
    change cannot recover objects that are not built. My arm and A1's arrive at the same wall from
    opposite directions, and A4's third route (master has 4.3x more creditable 8-hit objects on
    cube50 at EQUAL purity) is the same statement a third time.

### WHAT IS NEVERTHELESS WORTH KEEPING FROM THIS ARM
 * **A real fake-rate win that costs no efficiency**: eff +.0005 / dup -.0002 / **fake -.0047**
   (barrel -.0113) with 10,194 fewer TCs. If someone wants barrel fake rate, the re-key delivers it;
   it is just not a displaced result. It should be priced as a fake-rate change, honestly labelled.
 * **The two defects found are real regardless of my arm's verdict, and both are one-line fixes**:
   the displaced class was keyed on the wrong axis (64.1% of it at |dxy|<1), and it was keyed on a
   quantity DEFINED for only 5.7% of positives, so 94.3% of the positive class was silently labelled
   "prompt" by a -999 sentinel. Any future gate retrain -- for any purpose -- should use `simDxyFull`.
 * The six-bar env hook, the analytic bar re-fit (`refit_bars.py`, which replaces a grid search with
   an exact cost-matched solve), the validated header pipeline, and the ability to make training
   ntuples for ANY sample. All in `a2_ref/`, documented in `A2_HANDOFF.md`.

### THE ONE THING I WOULD SPEND THE NEXT HOUR ON, if the round continues
Not more gate training. **Arm I plus a re-fit of `m3ThetaR/RB/RT`** is the only configuration left
that could plausibly turn the ledger positive, because vxy[10,30) is reconstructed mostly as T5-class
on the exempt-5+ branch and that bar was never re-fitted -- 4 of the 6 bars are still at the shipped
calibration under a changed score. If that does not recover the -.0223, the gate route is closed and
the round's remaining budget belongs upstream with A1.
(Arm H's PU200 run at its cost-matched bars was still executing when I finished; its weights, build
`a2_ref/int_H_bin` and bars `M4D=0.470 M4=3.970` are on disk and `measure.sh H_refit all` completes it.)

============================================================================================
# [A2 HEADLINE] THE dxy RE-KEY PUTS US AHEAD OF LST MASTER ON ALL THREE PU200 HEADLINE METRICS
# AT ONCE, FOR THE FIRST TIME. WEIGHTS-ONLY. THIS IS A SHIPPABLE RESULT ON ITS OWN TERMS AND IT
# IS SEPARABLE FROM THE DISPLACED QUESTION.
============================================================================================

I filed this under my own negative earlier. That was wrong and the coordinator was right to call it:
judged against MASTER instead of against our own shipped baseline, the picture inverts. Measured
directly against `master_ref/master_rv1000_hists.root` (LST master b42d8f97ad5, same 1000 events,
same 208,999 sim denominator), full tables in `a2_ref/meas/*_vs_master.txt`:

    PU200RelVal 1000 evt, CPU, -p 0.8 -s 8        MASTER   round-2 SHIPPED   ARM B (this)
    eff overall (pt>0.9)                          .8100     .8099  (-.0001)   **.8105 (+.0005)**
    dup rate  (pt>0.9)                            .0514     .0479  (-.0035)   **.0476 (-.0037)**
    fake rate (pt>0.9)                            .0454     .0470  (+.0016)   **.0407 (-.0047)**
      fake barrel                                 .0437     .0516  (+.0079)     .0360 (-.0077)
      fake endcap                                 .0463     .0414  (-.0049)     .0403 (-.0060)
      fake transition                             .0453     .0574  (+.0121)     .0505 (+.0052)
    n TC                                      2,046,210                     2,036,159 (-10,051)

**FAKE RATE WAS THE ONE PU200 HEADLINE METRIC WHERE WE TRAILED MASTER (+.0016). ARM B TURNS IT INTO
A .0047 LEAD, A .0063 SWING, while eff goes UP (+.0006 vs shipped) and dup DOWN (-.0003).** Barrel
fake, which the barrel-dup round fought over, goes .0516 -> .0360, i.e. from .0079 BEHIND master to
.0077 AHEAD. Transition fake is still .0052 behind master but is .0069 better than shipped.

## AND FIVE OF SIX DISPLACED BANDS ARE STILL AHEAD OF MASTER
    band            MASTER   ARM B @ shipped bars   ARM B @ cost-matched bars (M4D -1.912 / M4 3.844)
    vxy [1,5)        .7774      .7984 (+.0210)          .7984 (+.0210)
    vxy [5,10)       .6448      .7084 (+.0635)          .7094 (+.0645)
    vxy [10,30)      .6257      .6805 (+.0548)          .6905 (+.0649)
    dxy [0,1)        .8297      .8350 (+.0053)          .8350 (+.0053)
    dxy [1,5)        .5097      .5526 (+.0429)          .5699 (+.0601)
    dxy [5,10)       .2314      .2274 (-.0040)          .2453 (+.0139)
    dxy [10,30)      .0545      .0253 (-.0291)          .0329 (-.0215)
**The -.0223 I reported on vxy[10,30) is a loss against OUR OWN baseline, not against master -- we
remain +.0649 ahead of master in that band.** That is the context I omitted and it changes how the
trade should be priced. The ONLY band still behind master is dxy[10,30), and my own arithmetic
(0.07% of matched tracks) plus A1's ceiling (87% not built on cube50 low-pT) say that band is not
winnable at the gate.

## HOW TO TAKE IT, and the two variants are genuinely separable
 * **Fake-rate variant: arm B weights at the SHIPPED bars.** No ChainConfig change at all -- swap
   `src/alpaka/Chain3NetworkWeights.h` for `a2_ref/Chain3NetworkWeights_B.h` and nothing else. Buys
   eff +.0006 / dup -.0003 / **fake -.0063** against shipped; costs dxy[5,10) -.0189 and
   vxy[10,30) -.0323 against shipped (still +.0548 vs master).
 * **Balanced variant: arm B weights + `m3Theta4D = -1.912`, `m3Theta4 = 3.844`.** eff +.0005 /
   dup -.0002 / fake -.0047 against shipped, and it recovers most of the displaced cost: dxy[5,10)
   back to -.0010 and dxy[10,30) to +.0013 against shipped, dxy[1,5) +.0601 vs master.
 * **Displaced variant: arm H** (full-coverage `simDxyFull`, `M4D=0.470 M4=3.970`): the best mid-dxy
   numbers of any arm -- dxy[5,10) **+.0278** and vxy[10,30) **+.0787** vs master -- but it PAYS
   fake (+.0099 vs master, .0553) and eff (-.0010 vs master). Take this only if mid-dxy displaced
   efficiency is worth a fake-rate regression; by the stated priority it probably is not, but it is
   the arm that moves displaced efficiency the most and it is measured, so the maintainer can price it.

## CAVEATS STATED WITH THE HEADLINE, per the standing rule
 * `mean nhitOT` drops (6.388 vs master 6.519, and 9.869 vs 10.151 in barrel): the re-key removes
   short exempt-T4 objects, so tracks are on average shorter than master's. Not obviously bad -- our
   shipped baseline is already 6.412 -- but it is a real change and it is in the same direction as
   the removed-fakes story.
 * `dup rate barrel` is WORSE than master (+.0091) in every variant including shipped (+.0091); the
   re-key does not touch that and it is a pre-existing round-2 deficit, not something I introduced.
 * cube50 is essentially UNMOVED by any variant (dxy[10,30) .0056 -> .0061, dxy[5,10) and vxy[10,30)
   flat to 4 decimals), and cube50_highPt dxy[10,30) is .0003 vs master .0011 at the shipped bars.
   So this result is a PU200 result. It is not a cube50 result and must not be presented as one.
 * Only 2 of the 6 bars re-fitted; `m3ThetaR/RB/RT` scan running now.

--------------------------------------------------------------------------------------------
[A1 19:25] THE PRICED FRONTIER. PU200RelVal 1000 evt, CPU -p 0.8 -s 8, all against the shipped
constraint eff .8099 / dup .0479 / fake .0470. n_sim = 208999 so sigma(eff) = .00086; n_TC = 2.05e6
so sigma(fake) = .00015 -- fake deltas here are all REAL, eff deltas under ~.0017 are NOT.
"M4Donly" = LST_CHAIN_M4D=-1e9 LST_CHAIN_U4=-1e5 with M4 left at the shipped 4.0.

  PU200 point            eff     dup    fake  fakeBar | dxy1-5 dxy5-10 dxy10-30 vxy10-30 |    n TC
  LST MASTER          .8100   .0514   .0454   .0437   | .5097   .2314   .0545    .6257   | 2046210
  SHIPPED (e=0)       .8099   .0479   .0470   .0516   | .5815   .2463   .0317    .7128   | 2052480
  e=-1e9 ONLY         .8095   .0479   .0518   .0646   | .5686   .2214   .0241    .7075   | 2059540
  M4D-2.5 U4 e=-0.5   .8094   .0484   .0555   .0727   | .5919   .2781   .0418    .7231   | 2076152
  M4Donly U4 e=0      .8047   .0462   .1173   .1909   | .6124   .2800   .0367    .7334   | 2231247
  M4Donly U4 e=-1.0   .8039   .0438   .1660   .2912   | .6085   .2850   .0469    .7374   | 2365141
  M4Donly U4 e=-2.0   .8031   .0418   .2044   .3630   | .6056   .2810   .0519    .7382   | 2479466
  noGate e=0          .8067   .0479   .1232   .2032   | .6124   .2800   .0367    .7396   | 2256839
  noGate e=-0.5       .8066   .0466   .1479   .2541   | .6088   .2949   .0431    .7417   | 2324607
  noGate e=-1.0       .8064   .0453   .1722   .3019   | .6075   .2850   .0462    .7427   | 2394173
  noGate e=-2.0       .8060   .0433   .2099   .3711   | .6040   .2810   .0513    .7437   | 2508752

FOUR THINGS TO TAKE FROM THIS TABLE.

1. *** IT IS THE EXEMPT (large-DCA) T4 ARM THAT CARRIES THE FAKE, NOT THE IP ARM. *** I guessed the
   opposite and the data says no: "M4Donly" (which holds the IP bar M4 at the shipped 4.0) already
   costs fake .1173 / barrel .1909, i.e. essentially all of noGate's .1232 / .2032. Meanwhile the IP
   arm buys EXACTLY ZERO displaced on cube50. So the IP bar M4=4.0 is free to keep and should be
   kept; every gram of both the gain and the price lives in the exempt arm plus the weld bar.
2. DUP GOES DOWN, NOT UP, as this loosens (.0479 -> .0433). The extra 4-layer objects are not
   duplicates of tracks we already had; they are new sims plus new fakes. So the maintainer's #2
   priority is not the binding constraint here -- FAKE is, and fake is priority #3.
3. **THERE IS A REAL LOW-COST OPERATING POINT AND IT IS NOT ON THE NO-GATE CURVE.**
   `LST_CHAIN_M4D=-2.5 LST_CHAIN_U4=-1e5 LST_CHAIN_E=-0.5`:
     eff  .8094 (-.0005, INSIDE 1 sigma)      dup .0484 (+.0005, noise)
     fake .0555 (+.0085)                      fake barrel .0727 (+.0211)
     dxy[10,30) .0317 -> .0418  (+.0101, CLOSES 44% OF THE ENTIRE MASTER GAP of .0228)
     dxy[ 5,10) .2463 -> .2781  (+.0318, and this puts us .0467 AHEAD of master's .2314)
     dxy[ 1, 5) .5815 -> .5919  (+.0104, already .0822 ahead of master)
     vxy[10,30) .7128 -> .7231  (+.0103, already .0974 ahead of master)
     cube50: dxy[10,30) .0056 -> .0093, dxy[5,10) .0616 -> .0724, vxy[10,30) .0389 -> .0446
     cube50_highPt: dxy[5,10) .0177 -> .0207, dxy[1,5) .0780 -> .0902, vxy[10,30) .0182 -> .0210
   So a THREE-CONSTANT change (no new machinery, no new weights, no retrain) buys +.0101 PU200
   dxy[10,30) and +.0318 dxy[5,10) for +.0085 fake with efficiency and dup unmoved. THIS IS THE
   BASELINE A2's RETRAIN MUST BEAT. It is cheap, it is one algorithm with no sample conditioning
   (all three are per-track quantities: an edge logit, a chain margin, a chain score), and it needs
   no training cycle. If a retrained gate cannot beat +.0101 / +.0318 at under +.0085 fake, the
   maintainer should take these three numbers instead.
   CAVEATS STATED WITH THE HEADLINE: (a) +.0211 BARREL fake is not small and only the maintainer can
   price it against priority #3; (b) all six bars would need re-fitting before this ships, per the
   standing rule; (c) I have not measured its CPU or GPU time, and n TC is +1.2% so it is small but
   not zero; (d) it does NOT reach master on cube50 dxy[10,30) (.0093 vs .0269) -- it wins the
   PU200 band, which is goal 1, and only dents the cube50 band, which is goal 2.
4. THE LOOSE END IS NOT SHIPPABLE AND THAT IS THE HONEST PRICE OF THE NAIVE ROUTE: reaching master's
   cube50 dxy[10,30) by bars alone (noGate + e=-2.0) needs PU200 fake .2099, a 4.5x increase, with
   barrel fake at .3711 (7x). Nobody should pursue that arm.

I am now probing the knee between shipped and the point in item 3 (is the U4 half load-bearing? is
e=-0.3 cheaper?) and will post the final candidate table.

--------------------------------------------------------------------------------------------
## [A2 RESULT 7] ARM H IS THE FIRST THING IN THIS ROUND THAT IMPROVES cube50 DISPLACED EFFICIENCY
## IN EVERY BAND, AND IT TAKES THE LEAD OVER MASTER IN cube50 dxy[1,5). It pays fake rate.

Arm H = the full-coverage `simDxyFull` re-key (class 2 = dxyFull>=1, tiers 1/10), bars
`LST_CHAIN_M4D=0.470 LST_CHAIN_M4=3.970`. Build `a2_ref/int_H_bin` (liblst_cpu.so md5
def1a11e51ec17d3). cube50 5000 evt, `cube_ref/band_census.py`; "baseline" = the shipped numbers at the
top of this file, "master" = `cube_ref/cube50_master.root`.

    cube50 band     denom   MASTER   SHIPPED   ARM H    H - shipped   H - master (sigma)
    dxy [ 1, 5)      2009    .1195    .1090    .1229      +.0139      **+.0035** (AHEAD, 0.3 sig)
    dxy [ 5,10)      2598    .0778    .0616    .0651      +.0035        -.0127
    dxy [10,30)      9353    .0269    .0056    .0075      +.0019        -.0195
    vxy [ 1, 5)       166    .3253    .2831    .3072      +.0241        -.0181 (not significant)
    vxy [ 5,10)       631    .2345    .2013    .2219      +.0206        -.0127 (not significant)
    vxy [10,30)      5521    .0639    .0389    .0417      +.0028        -.0223
  T4-class counts in dxy[10,30): master 210, shipped 20, ARM H **26**. In dxy[1,5): master 34, ARM H 34.

**Every band improves against our own baseline, and dxy[1,5) crosses over master.** That is the first
cube50 displaced gain in the round from any arm. It is modest in absolute terms and dxy[10,30) is still
3.6x behind master, consistent with A1's ceiling (87% of those objects are not built at all, so no
gate can reach them).

On PU200 arm H is the mid-dxy champion but the fake-rate loser, measured against MASTER:
    eff dxy [5,10)  .2592 vs master .2314   **+.0278**      eff vxy [10,30) .7044 vs .6257  **+.0787**
    eff dxy [1,5)   .5666 vs .5097   +.0569                 eff dxy [10,30) .0329 vs .0545   -.0215
    eff overall     .8090 vs .8100   -.0010                 dup  .0473 vs .0514   -.0040
    fake            .0553 vs .0454   **+.0099**   <-- the price, and it is the whole objection to arm H

### THE DIAL, PRICED AT BOTH ENDS AND IN THE MIDDLE (all vs MASTER, PU200 1000 evt)
    arm                          eff      dup      fake     dxy[5,10)   vxy[10,30)   cube50 dxy[1,5)
    round-2 SHIPPED             -.0001   -.0035   +.0016      -.0049      +.0871        -.0105
    B  @ shipped bars           +.0005   -.0037   **-.0047**  -.0040      +.0548        -.0105
    B  @ cost-matched bars      +.0004   -.0037   -.0031      +.0139      +.0649        -.0010
    H  @ cost-matched bars      -.0010   -.0040   +.0099      **+.0278**  **+.0787**    **+.0035**
    I  @ cost-matched bars                        (measuring now -- the conservative middle)
**B is the fake-rate variant, H is the displaced variant, and they are the same one-line training
change at two settings of `--class-axis`/`--weight-axis`. The maintainer can take either without the
other; they are separate weights files and neither touches C++.** By the stated priority
(efficiency >> dup > fake) B-at-cost-matched-bars is the better trade: it is ahead of master on all
three headline metrics AND on five of six PU200 displaced bands. H is the one to take if cube50 and
mid-dxy displaced efficiency are the goal and .0099 of fake rate is affordable.

--------------------------------------------------------------------------------------------
[A4 FUNNEL] THE 4-LAYER CANDIDATES EXIST -- 10x MASTER'S OUTPUT -- AND **THE GATE IS ONLY THE
FOURTH LARGEST LOSS**. The two biggest are the EDGE ELIGIBILITY CUT and the MUTUAL-BEST WELD'S
EXCLUSIVITY. The maintainer's named hypothesis is CONFIRMED on cube50 and REFUTED on PU200.

Method: my own build at d950e4315be (gpu_wt/g4, lst_cpu md5 e82f25dfb4edf4928f, VERIFIED to
reproduce cube_ref/cube50_ours.root exactly -- see my RECONCILIATION post). cube50 `-n -1 -p 0.8
-s 1` = ALL 10000 events; PU200RelVal 40 evt (A3's working point). Tools committed at
a4_ref/{chain_truth.py,weld_excl.py,funnel.py}. `weld_excl.py` replays K6a/K6b verbatim
(ChainWeld.h:81-155, 3 sweeps per ChainConfig.h:222, eligibility logOdds >= thetaEdge = 0).

E1 = MD-keyed pair -> 5 layers. E2 = Segment/LS-keyed pair -> 4 layers (ChainEdges.h:251,299).
VERIFIED INTERNALLY, no exceptions: of the 2-node chains in the cube50 chain dump, all 1819 with a
type-1 weld edge have nLayers 5 and all 1164 with a type-2 edge have nLayers 4. Also: EVERY
4-layer chain is a 2-node E2 weld, so the T4 class is exactly "one surviving E2 edge".

### THE COMPLETE cube50 T4 FUNNEL, 10000 events, per event, ours
    stage                                       N        /evt     stage loss   lost
    E2 pairs enumerated (K2)                  9986     0.9986        --          --
    E2 eligible (logOdds >= thetaEdge = 0)    5615     0.5615      -43.8%      4371
    E2 welded (mutual best, 3 sweeps)         1744     0.1744      -68.9%      3871
    4-layer chains at the gate                1164     0.1164      -33.3%       580
    gate survivors                             372     0.0372      -68.0%       792
    shipped T4-class TCs                       216     0.0216      -41.9%       156
    ... CREDITABLE (>0.75)                     212     0.0212
  MASTER shipped creditable T4 (5000 evt)      493     0.0986
**Master's creditable T4 output is 9.9% of our enumerated E2 supply.** The candidates are there in
abundance; we discard 97.9% of them. Ranked by absolute loss the stages are:
    1. edge eligibility cut   4371      3. chain gate        792
    2. weld exclusivity       3871      4. chain extension   580   5. arbitration/CC/emit 156
**The chain gate -- the thing this whole round has been tuning -- is the THIRD largest, 5x smaller
than either of the top two.** A3's 68-70% kill rate is real, but it operates on a supply already
cut 8.6x upstream of it. That is the reconciliation, completed.

### THE MUTUAL-BEST WELD IS THE MECHANISM, AND IT IS EXCLUSIVE BY CONSTRUCTION
ChainWeld.h gives each node ONE out-slot and ONE in-slot; an edge welds only if it is the argmax at
BOTH endpoints (K6b), so the welded graph is a set of disjoint simple paths (K6c) and **each T3
belongs to at most one chain**. Master's T4 builder has no such constraint -- one T3 can seed many
T4s. Why eligible E2 edges never weld:
                                  cube50 (10000 evt)      PU200 (40 evt)
    eligible E2 that never weld    3871/5615 = 68.9%    530624/709368 = 74.8%
      tail's out-slot taken             31.9%                 31.5%
      head's in-slot taken              46.5%                 35.9%
      both taken                        21.2%                 23.9%
      deadlock, both slots free          0.3%                  8.7%
    single-endpoint blocks: by an E1    80.7%                 39.4%
    ... by another E2                   19.3%                 60.6%
    edge logit, E1  mean/median      +2.63 / +2.61         -3.84 / -3.79   (frac>=0: .92 vs .20)
    edge logit, E2  mean/median      +0.63 / +0.31         +1.03 / +0.22   (frac>=0: .56 vs .53)
    E2 weld rate                        31.1%                 25.2%
    E1 weld rate                        46.6%                 12.8%

**THE HYPOTHESIS IS SAMPLE-SPLIT, AND THIS MATTERS MORE THAN THE HEADLINE.** On cube50 E1 edges are
STRONG (median logit +2.61, 92% eligible) and take 80.7% of the T3s that an E2 wanted -- the
4-layer candidate loses its T3 to a 5-layer partner, exactly as predicted. On PU200 E1 is WEAK
(median -3.79, only 20% eligible), E2 outperforms it, and 60.6% of E2 blocks are E2-vs-E2
self-competition. So "E1 steals the T3" is a clean-sparse-event phenomenon. **Any fix aimed at
E1/E2 competition will behave differently in the two samples and MUST be priced on both** -- this
is precisely the shape the NO SAMPLE OVERFITTING rule exists to catch, so nobody should tune the
E1/E2 balance on cube50 alone.

### IS THE STOLEN TRACK JUST RECOVERED AS A 5-LAYER OBJECT? NO -- CHECKED, IT IS A REAL LOSS
The obvious objection to all of the above is that an E1 weld that takes the T3 may still
reconstruct the same sim track, as T5-class instead of T4-class, costing no efficiency. It does
not: our T5-class yield already EQUALS master's (ours 1200/10000 = 0.1200/evt, master 597/5000 =
0.1194/evt), so the missing T4 tracks are not turning up in our T5 collection. And by distinct sim
(cube50, 5000 evt) master's T4-class reaches 492 sims that NO T5-class TC of its own reaches,
against our 113 -- a real 4.4x deficit in DISTINCT TRACKS, not a relabelling.

### HONEST LIMITS -- read these before acting
 1. **NO TRUTH ON THE DISCARDED E2 EDGES.** I cannot say how many of the 4371 edge-cut or 3871
    weld-blocked cube50 E2 pairs are real displaced tracks rather than combinatorial junk. The
    funnel bounds the OPPORTUNITY, not the recoverable gain. **That is the single next
    measurement** and it needs edge-level truth (the edge dump carries no hits; the T3 pair's MDs
    would have to be joined through the node dump).
 2. My truth join stalled on infrastructure and I am reporting it rather than papering over it.
    Beyond the ievt bug in my RECONCILIATION post, `dumpChains` is ALSO skipped by
    `createTriplets`' `nonZeroModules == 0` return (LSTEvent.dev.cc:649). I patched four early
    returns and still get 7391 records for 10000 cube50 events, so a fifth path exists that I did
    not find. **A hit-set join IS exact and I verified it works** -- 30 of 47 alive chains in a
    200-event dump match an ntuple TC's `tc_hitIdx` EXACTLY, which both confirms the dump's hit
    rows are tracking-ntuple ph2 rows and pins those records to their event (rec 3 -> row 5,
    rec 10 -> row 15, rec 14 -> row 20). Anyone finishing this should pin anchors that way rather
    than trust the counter. The creditable FRACTION of the 1164 pre-gate 4-layer chains is
    therefore still unmeasured; only the >= 212/1164 = 18.2% lower bound is established.
 3. The weld replay cannot see the kernel's stable tie word. Exact float ties inside a node's
    incident list: cube50 225 out / 95 in of ~15k edges (~1.5%, negligible); **PU200 501292 /
    348349 of 4.6M edges (~11-18%, NOT negligible)** -- so treat the PU200 per-reason breakdown as
    indicative and the cube50 one as solid.
 4. PU200 is 40 events. Poisson on its edge counts is negligible but event-to-event variance is not
    sampled; and PU200 has no chain-level dump here, so its stage-by-stage funnel below the weld is
    not measured.

### WHAT I WOULD DO NEXT, given the ranking
The two dominant losses are both OURS and neither is the gate: `thetaEdge` (LST_CHAIN_E, already
env-exposed by A1) and the weld's one-partner-per-slot rule. The cheapest informative experiment is
NOT a retrain and NOT a bar scan -- it is to let a T3 participate in more than one weld and price
the result on both samples. That is a change to ChainWeld.h, it is in scope, and the funnel says it
is where the factor of 4 lives. -- A4

============================================================================================
# [A2 RESULT 8] THE ENRICHMENT SET, MEASURED AT LAST. **cube50 SUPPLIES 1,641 POSITIVES AND 10
# FAKES.** It cannot teach the gate a discrimination, only "accept more" -- which is loss
# weighting, already falsified. This is the round's central hypothesis, answered with a number.
============================================================================================

First cube50 chain-gate training dump ever produced. `a2_ref/chains_a2b_cube50.root`, 5000 events,
`chainproto -m chaindump -e 0 -L 0.5` (the SAME binary and weld recipe as my PU200 dumps, so the rows
share one edge-logit scale). Getting here needed, in order: LST master `-mcCd`, an 11-branch
`write_lst_ntuple.cc` patch, and **`getUL` added to master's `code/rooutil/treeutil.h`** plus a
`ULong64_t` arm in `loadAllBranches` -- the tracking ntuple's `event` branch is ULong64_t and stock
master has no accessor for it, which is the specific reason no cube50 training ntuple existed.

## THE SUPPLY
    cube50, 5000 evt      chains 1,651   positives 1,641 (99.39%)   FAKES **10**
      nLayers = 4            576         566 positive (98.26%)
      nLayers = 5            920         920 positive (100%)
      nLayers = 6            143         143 positive (100%)
      nLayers = 7             12          12 positive (100%)
    4-layer creditable by |dxy| (simDxyFull, 100% coverage, ALL simAccepted):
      dxy<1  56      dxy[1,5) 212      dxy[5,10) 163      **dxy[10,30) 135**
    4-layer exempt branch (dcaXY >= 0.5): 535 of 576
    yield: 0.33 chains/event, against PU200's 4,757/event -- a factor 14,000 in density

## WHY THIS IS THE ANSWER, AND IT IS STRUCTURAL RATHER THAN STATISTICAL
The chain gate's job is to separate creditable chains from **displaced-LOOKING FAKES**. cube50 is 10
muons with no pileup, so it contains **ten** fakes in five thousand events. Mixing it in adds positives
and essentially no negatives, so the only thing the loss can learn from it is to move the decision
boundary toward acceptance -- which is exactly what a positive-class weight does, and
"defeated by its own threshold" is already in this file's PRIOR FALSIFICATIONS list. **cube50 cannot
supply the counterexamples that define the boundary, because in cube50 there is nothing to be confused
with.** That is not a statistics problem that more cube events would fix; 10 fakes per 5000 events
means 50,000 events would give 100.
This also explains, without contradicting the maintainer, why LST's t4dnn can still benefit from
cube50: a per-object T4 classifier is trained on its own PU200 negatives and uses cube50 only to
populate the displaced positive class, which is a different bargain from ours because our gate's
negatives and positives must come from the SAME welded-chain population to be comparable.

## IT IS STILL A REAL ADDITION TO THE POSITIVE CLASS, so I am running it rather than arguing it
135 creditable 4-layer chains at dxy[10,30) roughly DOUBLES that class over PU200's 137 (798 evt), and
every cube row has 100% `simDxyFull` coverage and is `simAccepted`. Arms now training (`a2_ref`):
    J  PU200 only, class dxyFull>=1 tiers 1/10                 (the control)
    K  J + cube50, per-row sample weight 1
    L  J + cube50, per-row sample weight 10
    M  J + cube50, per-row sample weight 50
`--evt-offset` is ON (default 1e9) so the cube events are not silently dropped -- see the TRAPS block
at the top of this file. Judge: `a2_ref/enrich_eval.py`, which fits each head's exempt-T4 bar on the
PU200 rows so the **PU200 fake cost is identical across arms**, then reports acceptance on
**cube50_highPt, which is in NO training input** -- a clean out-of-sample transfer test, aimed at the
sample A1's ceiling says is winnable (no-gate beats master in every band there) rather than at low-pT
cube50 (87% of those objects are never built).

--------------------------------------------------------------------------------------------
[A2 SIDE DEFECT, real and pre-existing] **THE SHIPPED GATE WAS TRAINED AT `lambdaLen = 0.5` AND IS
SERVED AT `lambdaLen = 3.0`.** `interface/ChainConfig.h:20` sets `lambdaLen = 3.f` ("ANCHOR said 0.5,
the M19 block overrides"), but the recipe that produced the shipped weights' dumps is
`iterations/fanout5/edgeretrain/er_chaindump.sh`: `-e "$TH" -L 0.5`, and my re-dump reproduces the
shipped per-layer label balance exactly at `-L 0.5` (nL4 .3570 / nL5 .7108 / nL6 .8721 vs A4's
.3600 / .7140 / .8720). lambdaLen enters `k6WeldChains`, so it changes WHICH chains exist, i.e. the
gate's training population is a different welding than the one it is applied to.
This is separate from A4's item 6 (trained untrimmed, served trimmed) and compounds with it.
It also means my arms split into two families and the split matters:
  arms A-F trained on MY dumps at `-L 0.5`  = train/serve MISMATCHED, same as the shipped gate
  arms G-M trained at `-L 3.0` (A5's dump, and my a2c re-dumps)  = train/serve CONSISTENT
which is a plausible part of why the G-I family reaches higher displaced AUCs (arm J control
promptAUC .96984 / dispAUC .95712 against arm A's .96629 / .93611). I am not claiming the whole
difference is the weld -- the class axis changed too -- but anyone comparing across my two blocks
must know they are not the same population, and anyone retraining this gate should dump at `-L 3.0`.

--------------------------------------------------------------------------------------------
[A1 20:05] STEP 4, THE CANDIDATE, PLUS A CORRECTION TO MY OWN 18:07 WARNING ABOUT U4.

*** CORRECTION FIRST: LST_CHAIN_U4 IS EMPIRICALLY INERT AT EVERY OPERATING POINT WORTH SHIPPING.
My 18:07 note said any T4-class loosening must move U4 with M4D. That is right about the CODE (K9
really does apply `score >= thetaExempt4` to exempt chains) but wrong about the DATA: at M4D=-2.5,
`U4=-1e5` and `U4` left at the shipped 0 give BIT-IDENTICAL results -- same eff/dup/fake to 4
decimals AND the same n TC to the unit (2076152 both). Every chain the loosened gate spares already
has score >= 0. So the candidate below is TWO constants, not three. (Whether U4 binds out at
M4D=-1e9 I did not isolate; it does not matter, because that end is unshippable -- see 19:25 item 4.)

THE KNEE, decomposed so the maintainer can see what each half buys. PU200RelVal 1000 evt.
  point                     eff     dup    fake  fakeBar fakeTra | dxy1-5 dxy5-10 dxy10-30 vxy10-30
  LST MASTER              .8100   .0514   .0454   .0437   .0453  | .5097   .2314   .0545    .6257
  SHIPPED                 .8099   .0479   .0470   .0516   .0574  | .5815   .2463   .0317    .7128
  B  e=-0.5 alone         .8098   .0480   .0483   .0551   .0591  | .5780   .2502   .0342    .7130
  A  M4D=-2.5 alone       .8096   .0483   .0534   .0669   .0623  | .5968   .2701   .0367    .7226
  D  M4D=-2.0  e=-0.3     .8096   .0482   .0517   .0634   .0607  | .5916   .2681   .0393    .7200
  *  M4D=-2.5  e=-0.5     .8094   .0484   .0555   .0727   .0639  | .5919   .2781   .0418    .7231
THE TWO BARS ARE SUPER-ADDITIVE IN GAIN AND SUB-ADDITIVE IN PRICE, which is the 19:35 series-bars
result showing up in the shippable regime: on PU200 dxy[10,30), A alone buys +.0050, B alone +.0025,
BOTH buy +.0101 -- more than their sum -- while fake goes +.0064, +.0013, +.0085, less than its sum.
Efficiency-per-unit-fake: B 1.9, D 1.6, * 1.2, A 0.8. So the edge bar is the efficient half and the
gate bar is the expensive half, and they must move TOGETHER or neither pays.

CONCRETE CANDIDATE, full both-sample (three-sample) table. Two changed constants in ChainConfig.h:
      m3Theta4D  -1.2  ->  -2.5        (-M4D, the T4-class exempt gate bar on mD)
      thetaEdge   0.0  ->  -0.5        (-e,   the K6 weld eligibility bar on the edge logit)
  PU200RelVal 1000 evt   eff .8099 -> .8094 (-.0005, inside 1 sigma of .00086)
                         dup  .0479 -> .0484 (+.0005, noise)
                         fake .0470 -> .0555 (+.0085 REAL; barrel .0516 -> .0727, trans .0574 -> .0639)
                         dxy[10,30) .0317 -> .0418   +.0101, closes 44% of the .0228 master gap
                         dxy[ 5,10) .2463 -> .2781   +.0318, and .0467 AHEAD of master's .2314
                         dxy[ 1, 5) .5815 -> .5919   +.0104 (.0822 ahead of master)
                         vxy[10,30) .7128 -> .7231   +.0103 (.0974 ahead of master)
                         n TC +1.15% (2052480 -> 2076152)
  cube50 5000 evt        dxy[10,30) .0056 -> .0093 (T4cl 20 -> 52; master .0269, T4cl 210)
                         dxy[ 5,10) .0616 -> .0724 (master .0778)
                         dxy[ 1, 5) .1220 -> .1369 (master .1195, so we go further ahead)
                         vxy[10,30) .0389 -> .0446 (master .0639)
                         vxy[ 5,10) .2108 -> .2330 (master .2345)
  cube50_highPt 5000 evt dxy[10,30) .0002 -> .0004 (master .0011; 5 sims of 12586, NOISE either way)
                         dxy[ 5,10) .0177 -> .0207 (master .0129, we stay ahead)
                         dxy[ 1, 5) .0780 -> .0902 (master .0710)
                         vxy[10,30) .0182 -> .0210 (master .0180)
CAVEATS, WITH THE HEADLINE AND NOT IN FINE PRINT:
 (a) +.0085 fake / +.0211 BARREL fake is a real cost and only the maintainer can price it. Priority
     says fake is third, but barrel fake has been the currency of two previous rounds.
 (b) All six bars must be re-fitted before this ships (standing rule). These two were scanned with
     the other four frozen, so this is a candidate, not a fitted configuration.
 (c) NOT MEASURED: CPU and GPU time. n TC +1.15% and a lower weld bar means more welds, so the
     chain block will cost something. Someone must measure it before this is called free.
 (d) NO SAMPLE CONDITIONING: both constants are compared against PER-TRACK quantities (one edge
     logit, one chain margin). Nothing here reads occupancy, a sample flag or an event-level count.
     The change moves BOTH samples in the same direction, which is the evidence the rule asks for.
 (e) It does NOT reach master on cube50 dxy[10,30). It wins goal 1 (the PU200 band) and dents goal 2.

THE INSTRUMENT IS COMMITTED: gpu_wt/g1 branch `a1_bar_env_knobs`, commit 841e3cd1389, and exported
at gpu_wt/g1/a1_ref/chain_bar_env.patch (140 lines, 2 files). All artifacts, logs and per-point
ntuples are in gpu_wt/g1/a1_ref/ ; the two scan drivers are run_cube.sh and run_pu.sh and the table
generator is frontier.py.

--------------------------------------------------------------------------------------------
[A4 CORRECTION -- SUPERSEDES THE RANKING IN MY [A4 FUNNEL] POST] I RANKED THE LOSSES BY EDGE
COUNT. THAT WAS THE WRONG UNIT. In DISTINCT SIM TRACKS -- the unit efficiency actually counts --
**THE GATE IS THE DOMINANT LOSS (64.5%), NOT THE FOURTH.** A3's arm is aimed at the right stage.

I got the truth join working (exact, validated) and it overturns my own emphasis. Read this
instead of the ranking in the FUNNEL post; the edge-level counts there are still correct as edge
counts, but they over-weight the upstream stages because most discarded E2 edges are BRAID
DUPLICATES of a track another edge already covers, not distinct lost tracks.

### VALIDATION FIRST, because everything below rests on it
 * Join is by EXACT TC hit-set anchoring, not the broken record counter: a chain whose hit set
   equals some TC's `tc_hitIdx` pins its record to that ntuple row. 3027 of 3402 cube50 chains
   (89%) joined exactly (anchored or forced by a bracket of equal width); 375 by best-fraction
   heuristic. Tool: a4_ref/chain_truth3.py.
 * **My python matcher reproduces the production `tc_pMatched` to 2.2e-10 (max |diff| 0.000000)
   on all 1416 cross-checkable chains, zero disagreements.** So the creditability numbers below
   are the harness's own definition, not a re-interpretation.
 * A bug I caught and fixed via this check: counting raw simhits per hit instead of deduping sims
   per hit produced fractions ABOVE 1.0. Anyone reusing this code, keep the per-hit dedupe.

### THE SUPPLY IS REAL TRACKS, NOT JUNK -- 98.2% OF IT
cube50, 10000 evt, our pre-gate 4-layer chains: **1143 of 1164 are CREDITABLE (98.2%)**; the
T5 class is 2237/2238 (99.96%). So the "do the objects exist" question is answered: they exist and
they are real.

### THE FUNNEL IN DISTINCT CREDITABLE SIM TRACKS (10000 evt, ours vs master)
    pre-gate  T4-class reachable distinct sims      880    0.0880 /evt
    post-gate T4-class distinct sims                312    0.0312 /evt   <- gate discards 64.5%
    MASTER delivered creditable T4 TCs (x2 from 5000 evt)  986    0.0986 /evt
  => our PRE-GATE distinct supply is **0.89x master's DELIVERED output** -- 11% short, not 4x.
  => the gate then throws away 64.5% of it.
  => T4-only distinct sims lost at the gate (no creditable T5 chain covers them, so this is REAL
     efficiency, not a reclassification): **371 = 0.0371/evt. That is the prize.**
     (568 sims lose their T4 chain at the gate; 197 of those are also covered by a creditable T5
     chain and so cost nothing.)

### PER-BAND -- AND THE TARGET BAND BEHAVES DIFFERENTLY FROM THE AVERAGE
distinct creditable sims reachable by a T4-class chain, by |sim_pca_dxy|, 10000 evt:
    band        pre   post   keep    T4-only lost   T5 covers
    [0,1)       102     28   0.275        39           157
    [1,5)       326    128   0.393       108           575
    [5,10)      254     95   0.374       114           376
    [10,30)     195     61   0.313       107           193
    >=30          3      0   0.000         3             0
vxy: [1,5) 56/17, [5,10) 138/49, [10,30) 427/123 (keep .288), >=30 256/122 (keep .477).
**In dxy[10,30) our pre-gate supply is 195 distinct sims while master DELIVERS ~420 there** (FINDINGS'
210 per 5000 evt, doubled). So in the one band this round is chartered to fix, the supply is ~2.2x
short BEFORE the gate, and the gate then keeps only 31%. Overall the gate dominates; in the target
band supply and gate are both binding and supply is the larger factor. CAVEAT, stated because it
matters: my bands are raw |sim_pca_dxy| with no other selection, while FINDINGS' 210 comes through
the standard band selection, so the cross-side band comparison is INDICATIVE. The OVERALL 880 vs
986 comparison is apples-to-apples (same `tc_type == 9` + creditable definition on both sides).

### SCOPING MY OWN LABEL-AUDIT FINDING: THE 6/8 QUANTIZATION IS A PU200 EFFECT ONLY
cube50 8-hit chains: 1138 of 1164 sit at frac EXACTLY 1.000 and only **15** at 0.750. On PU200 the
0.75 pile was 146104 of 931445 (15.7%). So the strict->0.75 quantization I flagged is a
PU200-occupancy phenomenon and is IRRELEVANT on cube50. It does not explain the cube50 T4 gap and
I should not have left that open.

### THE SYNTHESIS, PUTTING A3's RESULT AND MINE TOGETHER
 * The prize at the gate is real and now sized: 371 distinct T4-only sims, 0.0371/evt.
 * But A3 measured that cube50's killed displaced T4 chains are killed DECISIVELY (mD p50 -3.37,
   max -1.40 against a -1.2 bar), and I measured that the -M4D trade curve is already at its knee
   (1:44 and worsening to 1:499). **Both say the same thing: that 371-sim prize is NOT reachable by
   moving a threshold.** The gate's SCORE ranks these real 4-layer tracks inside its fake class.
 * So the honest ranking of what could actually work: (1) make the gate's score separate this
   population -- that is A2's retrain, and my label audit says its targets are correct, so it is a
   REPRESENTATION problem not a label problem; (2) close the ~11% overall / ~2.2x dxy[10,30) supply
   shortfall, where the weld's one-partner-per-T3 exclusivity is the named suspect and the E1/E2
   competition numbers in my FUNNEL post still stand as edge-level evidence. (3) Threshold-only
   work is exhausted.
 * NOT MEASURED, and it is the one thing that would price option (2): whether the weld-blocked E2
   edges reach sims that no surviving chain reaches. My distinct-sim counts show the pre-gate
   supply already collapses 1164 chains into 880 sims (1.32 chains/sim), so much of the upstream
   edge loss IS duplicate removal -- but I cannot say how much. Edge-level truth needs the T3 pair's
   MDs joined through the node dump; that is the next measurement for anyone taking option (2).
Artifacts: a4_ref/{chain_truth.py,chain_truth3.py,weld_excl.py,funnel.py,cube50_truth.npy,
cube50.bin,cube50_edges.bin,pu200_edges.bin,cube50_a4c.root}. -- A4

--------------------------------------------------------------------------------------------
[A2 RESULT 8b] cube50_highPt CONFIRMS THE SAME STRUCTURE, so RESULT 8 is not a property of one sample.
`a2_ref/chains_a2b_cube50hp.root`, 5000 evt, same binary/recipe:
    chains 1,161   positives 1,153 (99.31%)   **FAKES 8**
      nLayers = 4   387   379 positive (97.93%)      nLayers = 5   709   709 (100%)
      nLayers = 6    62    62 (100%)                 nLayers = 7     3     3 (100%)
Two independent cube samples, 10,000 events between them, **18 fakes total**. A gate is a
signal-vs-background decision; these samples contain no background. Kept as the OUT-OF-SAMPLE
evaluation set (it is in no training input) rather than as an enrichment input.

============================================================================================
# [A2 RESULT 9] **cube50 ENRICHMENT WORKS.** Out-of-sample on cube50_highPt, at IDENTICAL PU200
# fake cost, uniform mixing takes creditable exempt-T4 acceptance from .280 to .452 and the
# dxy>=10 band from .200 to .440. THE MAINTAINER'S HYPOTHESIS IS VINDICATED. The mixing ratio is
# a real optimum, not a free parameter, and it is LOW.
============================================================================================

Design, because the design is what makes this believable:
 * TRAIN on PU200 (`chains_a2c_pu300.root`, 300 evt, 1.43M chains) + `chains_a2b_cube50.root`
   (cube50, 5000 evt, 1,651 chains), class 2 = `simDxyFull >= 1`, tiers 1/10, `--evt-offset` ON so
   all 1,651 cube rows are kept (log: "keep 1651/1651"; WITHOUT the offset it is 0/1651 -- see TRAPS).
 * EVALUATE on `chains_a2b_cube50hp.root` (**cube50_highPt, in NO training input**) -- a clean
   out-of-sample transfer test, aimed where A1's ceiling says a win is available.
 * COST CONTROL: each arm's exempt-T4 bar is chosen so the **PU200 exempt-T4 fake survival is
   identical (0.1407) for every arm**. So every column below is a pure shape comparison at fixed
   PU200 fake cost -- the constraint the maintainer prices.
 * `a2_ref/enrich_eval.py`; arms trained by `a2_ref/train_arm.sh`, logs `train_[JKLM]*.log`.

    arm (cube per-row weight)   bar    creditable ALL   dxy[1,5)   dxy[5,10)   dxy>=10
                                        N=343           N=165      N=105       N=25
    J  PU200 ONLY (control)   -1.200      .2799          .3758      .2381       .2000
    K  + cube50, weight  1    -1.234    **.4519**      **.5152**    .4952       .4400
    L  + cube50, weight 10    -1.193      .3469          .3333    **.4667**   **.5200**
    M  + cube50, weight 50    -0.721      .2828          .2848      .3238       .4400
    (PU200 val AUCs: J prompt .96984 / disp .95712;  K .97008 / .95833;  L .96906 / .95473;
     M .96758 / .94323 -- so K also very slightly IMPROVES the PU200 val metrics over the control.)

## THE RATIO IS A RESULT
**Uniform mixing (weight 1) is the optimum and the response is non-monotone.** K gains +.172 on
creditable-ALL, +.139 on dxy[1,5), +.257 on dxy[5,10) and +.240 on dxy>=10. Pushing to weight 10
buys a further +.08 on the dxy>=10 band alone but GIVES BACK dxy[1,5) (.515 -> .333) and
creditable-ALL (.452 -> .347); weight 50 collapses toward the control while distorting the bar
(-0.721). Read physically: cube50's 1,651 rows are 0.12% of the training rows, so at weight 1 they
act as a small, clean, correctly-placed addition to the displaced positive class; by weight 10 they
are ~1.2% and start to dominate the displaced class with a single sample's geometry, which is the
over-fitting the brief warned about, and it shows up exactly as the brief predicted -- as a loss in
the band cube50 populates LEAST (dxy[1,5)).

## THIS DOES NOT CONTRADICT RESULT 8, IT SHARPENS IT
RESULT 8 stands: cube50 supplies 1,641 positives and **10 fakes**, so it cannot teach a new
signal-vs-background boundary. What RESULT 9 shows is that it does not need to: it **relocates the
positive class in feature space**, and the boundary is then re-derived against PU200's own abundant
fakes. That is why the effect survives a FIXED PU200 fake cost, and why the useful weight is small.
My earlier framing ("enrichment can only say accept more, which is loss weighting") was too strong,
and weight 50 -- which IS essentially loss weighting -- is precisely the arm that fails. **The
distinction that matters is: a few well-placed positives beat many loudly-weighted ones.**

## STATISTICS CAVEAT, stated with the result
The evaluation bands hold 165 / 105 / 25 rows. The effects (+.14 to +.26) are far larger than that
noise, and creditable-ALL (N=343) moves +.172, but the dxy>=10 column specifically is a 25-row
measurement and should be read as directional. `EVAL fakes` is N=6 with 0 survival in every arm --
cube samples cannot test the fake side at all, which is exactly why the fake cost is fixed on PU200.

## STATUS: arm K is BUILDING now (`a2_ref/int_K_bin`). Until it is measured, RESULT 9 is a
## feature-space result, not an efficiency result. The both-sample contract table follows.

--------------------------------------------------------------------------------------------
[A2 RESULT 10, partial] THE OTHER FOUR BARS: **THE IP-5+ RESCUE BAR IS NOT WHERE vxy[10,30) LIVES.**
Scanned on arm B's build via the env hook, no rebuild (`a2_ref/measure.sh`, PU200RelVal 1000 evt, vs
`win_ref/all4_rv1000_hists.root`):

    config (arm B weights, M4D=-1.912 M4=3.844)   eff     dup     fake    dxy[1,5)  dxy[5,10)  vxy[10,30)
    m3ThetaRI = -0.5 (SHIPPED)                  +.0005  -.0002  -.0047   -.0117    -.0010    -.0223
    m3ThetaRI = -1.5 (looser IP-5+ rescue)      +.0004  -.0002  -.0043   -.0114    -.0010    **-.0203**
**Loosening the IP-5+ mX rescue by a full logit recovers +.0020 of the -.0223, i.e. 9%, and gives back
.0004 of the fake win.** So that bar is not the lever; the vxy[10,30) population is not being lost on
the IP-5+ branch. `m3ThetaR/RB/RT` (exempt-5+) is still running and is the remaining candidate --
result to follow. Recording the negative now because it closes one of the four unverified bars, and
because "4 of 6 bars unverified" was the caveat on my headline: it is now 3 of 6.

============================================================================================
# [A2 REVISED VERDICT] I RETRACT MY EARLIER "THE GATE IS NOT WHERE THIS CAN BE FIXED".
# TWO SHIPPABLE THINGS CAME OUT OF THIS ARM, AND THE MAINTAINER'S ENRICHMENT HYPOTHESIS IS
# CONFIRMED, NOT REFUTED.
============================================================================================

My VERDICT entry above was written before three things landed. Superseding it, and being explicit
about which of my own claims did not survive:

## WHAT I GOT WRONG, and what replaced it
 1. **"The target class is 137 rows, so the re-key cannot work."** WRONG -- an artefact of the -999
    sentinel. With `simDxyFull` (A5's insight, which I then implemented in my own prototype) the
    4-layer dxy[10,30) class is 663 rows in 250 events, 15x more per event. RESULT 4.
 2. **"cube50 enrichment can only say accept more, which is loss weighting."** TOO STRONG. It is
    true that cube50 has 10 fakes in 5000 events and so cannot teach a boundary (RESULT 8), but
    what it does instead is RELOCATE the positive class, and the boundary is then re-derived
    against PU200's own fakes. Measured out-of-sample at fixed PU200 fake cost, uniform mixing
    takes creditable acceptance .280 -> .452 and the dxy>=10 band .200 -> .440 (RESULT 9). The arm
    that behaves like pure loss weighting is weight 50, and it is the one that FAILS.
 3. **"The displaced ledger is negative."** True against our own baseline, MISLEADING against
    master, which is the comparison that matters: arm B is ahead of master on eff, dup, fake AND on
    five of six displaced bands, and still +.0649 ahead on the vxy[10,30) band I described as a
    loss. HEADLINE entry.

## THE TWO SHIPPABLE ITEMS, SEPARABLE, NEITHER TOUCHING C++
 * **FAKE-RATE WIN (arm B).** vs MASTER: eff **+.0005**, dup **-.0037**, fake **-.0047**
   (.0407 against master's .0454; barrel .0360 against .0437). Fake rate was the one PU200 headline
   metric where we TRAILED master; this turns it into a lead. Swap
   `src/alpaka/Chain3NetworkWeights.h` for `a2_ref/Chain3NetworkWeights_B.h`; optionally add
   `m3Theta4D = -1.912, m3Theta4 = 3.844` to recover the mid-dxy bands as well.
 * **DISPLACED WIN (arm H, and arm K once measured).** Arm H vs MASTER: dxy[5,10) **+.0278**,
   vxy[10,30) **+.0787**, and it is the FIRST arm to improve cube50 in every band with cube50
   dxy[1,5) crossing over master (+.0035). Price: fake +.0099. Arm K (the enrichment arm) is the
   one to prefer if its contract table holds, because it got the same displaced relocation from
   1,651 extra rows rather than from a harder weight.

## THE ONE-LINE SUMMARY OF THE MECHANISM, since it is the transferable part
The shipped gate's displaced class was keyed on `simVxy`, which (a) is the axis we already BEAT
master on and (b) is only DEFINED for 5.7% of positives, because `sim_vx`/`sim_vy` are not
tracking-ntuple branches while `sim_pca_dxy` is. Key the class on `|sim_pca_dxy|` of the matched
FULL sim row instead, and optionally add cube50 rows to populate it. One dump branch, one line of
the training script, no new input, no new head, no new cut -- which is exactly what the
architecture rule asks a trained component to be.

--------------------------------------------------------------------------------------------
## [A2 RESULT 10, complete] THE OTHER FOUR BARS DO NOT CHANGE THE PICTURE, AND **ARM I IS
## DOMINATED** -- its spectacular displaced numbers were the BAR, not the head. Measured, not inferred.
All PU200RelVal 1000 evt vs `win_ref/all4_rv1000_hists.root`, via the env hook, zero rebuilds.

### (a) the two 5+-branch rescue bars, on arm B's weights (M4D=-1.912 M4=3.844)
    5+ bars                        eff     dup     fake    dxy[10,30)  vxy[10,30)
    RI -0.5 / MR -1.8  (SHIPPED)  +.0005  -.0002  -.0047    +.0013      -.0223
    RI -1.5 / MR -1.8             +.0004  -.0002  -.0043    +.0013      -.0203   (+.0020 recovered)
    RI -0.5 / MR -2.5             +.0003  -.0002  -.0030    +.0025      -.0184   (+.0039 recovered)
**Together the two 5+ bars recover ~26% of the -.0223 and consume ~40% of the fake win.** So the
answer to "do the other four bars change the picture" is: they help slightly, monotonically, and they
trade against the same fake rate -- there is no free recovery hiding in them. The `c25Theta/D` cell
rule acts only on (nNodes==2, nLayers==5) and is untouched by a T4-class re-key; I did not scan it
and I am recording that as the one bar of the six still unverified, down from four.

### (b) ARM I, scanned across its bar -- the configuration I myself nominated as most promising
    arm I bar (M4D)       eff     dup     fake    dxy[10,30)  vxy[10,30)   fake vs MASTER
    -2.204 (cost-matched) -.0015  -.0003  +.0219    +.0069      +.0100      **+.0235**
    -1.2   (shipped)      -.0003  -.0004  -.0007    -.0133      -.0230      -.0053
    -0.5                  -.0003  -.0004  -.0012    -.0158      -.0270      -.0058
**Arm I is DOMINATED.** At its loose bar it looked like the best displaced arm in the round
(vs master: dxy[1,5) +.0984, dxy[5,10) +.0655, vxy[10,30) +.0972) but it was paying **+.0235** of
fake rate to get there -- i.e. it was buying displaced efficiency by the cheap route A1 priced, not by
a better score. Tighten it to a comparable fake cost and its displaced numbers go NEGATIVE, worse than
arm B at the same fake rate. **This is the control that makes arms B/H/K credible: the same scan applied
to them does NOT collapse them, and applied to arm I it does.** I nominated arm I as "the only
remaining configuration that could turn the ledger positive"; that nomination is now falsified by
measurement, which is the outcome I asked for.

### CONSEQUENCE FOR THE RANKING (all at comparable fake cost, vs MASTER)
    arm B  eff +.0005  dup -.0037  fake **-.0047**  and 5 of 6 displaced bands AHEAD  <- best overall
    arm H  eff -.0010  dup -.0040  fake +.0099      best mid-dxy (+.0278) and cube50 (+.0035)
    arm I  dominated at every bar I tried
    arm K  the enrichment arm -- contract table still running, and it is the one that got arm H-like
           relocation from 1,651 extra ROWS rather than from a harder weight, so it is the one to
           prefer if it holds.

--------------------------------------------------------------------------------------------
## [A2 RESULT 11] THE BEST CONFIGURATION I FOUND, and it beats master on the three headline metrics
## AND on SIX of the seven displaced bands simultaneously. It is arm B plus THREE bar values.

    ARM B weights + `m3Theta4D = -1.912`, `m3Theta4 = 3.844`, `m3ThetaR/RB/RT = -2.5`
    (i.e. `Chain3NetworkWeights_B.h` + `LST_CHAIN_M4D=-1.912 LST_CHAIN_M4=3.844 LST_CHAIN_MR=-2.5`)
    PU200RelVal 1000 evt, CPU, -p 0.8 -s 8, judged against master_ref/master_rv1000_hists.root

    metric                  MASTER    THIS     delta      round-2 SHIPPED delta
    eff overall (pt>0.9)     .8100    .8102   **+.0002**       -.0001
    dup rate  (pt>0.9)       .0514    .0477   **-.0037**       -.0035
    fake rate (pt>0.9)       .0454    .0439   **-.0015**       +.0016   <- deficit becomes a lead
    eff vxy [0,1)            .8430    .8427    -.0002          +.0006
    eff vxy [1,5)            .7774    .7982   **+.0207**       +.0253
    eff vxy [5,10)           .6448    .7113   **+.0665**       +.0779
    eff vxy [10,30)          .6257    .6944   **+.0687**       +.0871
    eff dxy [0,1)            .8297    .8349   **+.0052**       +.0064
    eff dxy [1,5)            .5097    .5731   **+.0634**       +.0718
    eff dxy [5,10)           .2314    .2493   **+.0179**       -.0049   <- deficit becomes a lead
    eff dxy [10,30)          .0545    .0342    -.0203          -.0228   <- still behind, improved
**Ahead of master on eff, dup, fake, and on 6 of 7 displaced bands. The only remaining deficit is
dxy[10,30), and it IMPROVES (-.0228 -> -.0203).** Two of the three cells where round-2 trailed master
(fake rate, dxy[5,10)) cross over; the price is paid in the vxy bands where our lead was largest and
where we remain far ahead.

Against our OWN round-2 baseline this configuration reads: eff +.0003, dup -.0002, fake -.0030,
dxy[5,10) +.0030, dxy[10,30) +.0025, vxy[10,30) -.0184. So it is a small net trade of vxy lead for
fake rate plus mid/high-dxy, and every headline constraint moves the right way.

CAVEATS WITH THE HEADLINE: (i) cube50 is essentially unmoved by arm B in any bar configuration
(dxy[10,30) .0056 -> .0061) -- this is a PU200 result; (ii) `mean nhitOT` is 6.388 against master's
6.519, i.e. our tracks are shorter; (iii) `dup rate barrel` remains .0091 WORSE than master, a
pre-existing round-2 deficit this does not address; (iv) `c25Theta/D` is the one bar of the six still
unscanned; (v) GPU/CPU timing not re-measured -- a weights swap should be timing-neutral (identical
architecture, 25/32/32/3) but that is an expectation, not a measurement.

--------------------------------------------------------------------------------------------
[A2 ACCURACY CORRECTION to RESULT 9] The J/K/L/M arms ran with weight tiers **1 / 5 cm** on
`simDxyFull`, not 1 / 10 as I intended: I passed `--class-lo 1 --class-hi 10` but `--weight-hi`
defaults to 5 independently of `--class-hi`, and the log records the truth
("class axis = dxyfull (boundary 1 cm); weight axis = dxyfull (tiers 1 / 5 cm)"). All four arms used
the SAME tiers, so the enrichment comparison and the ratio conclusion are unaffected -- the only thing
wrong is my description of the tier. Recording it because someone reproducing this from my prose
rather than from the log would get a different configuration.
Other numbers from the same log, worth having: the dxyFull re-key gives **31,434** displaced-class
training rows against the shipped vxy rule's ~10,000, at **100.00%** dxy coverage (5.90% for
accepted-only simDxy), and `--evt-offset` kept **1651/1651** cube rows.

============================================================================================
# [A2 RESULT 12] **THE ENRICHMENT ARM MEASURED END TO END. IT IS THE ONLY ARM IN THE ROUND THAT
# IMPROVES EVERY DISPLACED BAND AT ONCE, INCLUDING THE vxy BANDS IT WAS NOT AIMED AT.** Its bar is
# still too loose (fake +.0155) and the tightening scan is running.
============================================================================================

Arm K = the dxyFull re-key trained on PU200 **+ 1,651 cube50 rows at uniform weight**, integrated
build `a2_ref/int_K_bin` (liblst_cpu.so md5 0bc0f941f734ac88e6), `LST_CHAIN_M4D=-1.234`.
PU200RelVal 1000 evt, CPU, -p 0.8 -s 8.

    metric              vs round-2 SHIPPED      vs MASTER
    eff overall (pt>0.9)  .8088   -.0011          -.0012
    dup rate  (pt>0.9)    .0473   -.0006        **-.0041**
    fake rate (pt>0.9)    .0624 **+.0155**       **+.0170**   <- the problem, and it is the bar
    eff dxy [1,5)         .6040 **+.0224**       **+.0942**
    eff dxy [5,10)        .2910 **+.0447**       **+.0596**
    eff dxy [10,30)       .0412 **+.0095**         -.0133
    eff vxy [1,5)         .7988   -.0038         **+.0214**
    eff vxy [5,10)        .7241 **+.0015**       **+.0793**
    eff vxy [10,30)       .7202 **+.0074**       **+.0945**
    n TC               2,100,637 +48,157

### WHY THIS IS DIFFERENT FROM EVERY OTHER ARM
Every other displaced arm bought dxy by GIVING BACK vxy: arm B -.0223 on vxy[10,30), arm H -.0084,
arm I -.0230. **Arm K is +.0074 on vxy[10,30) and +.0015 on vxy[5,10) while simultaneously being
+.0447 on dxy[5,10) and +.0095 on dxy[10,30).** It is the only configuration that does not trade one
displaced population for the other, and the only difference between it and its own control (arm J) is
**1,651 cube50 rows at weight 1** -- 0.12% of the training set. Against master it is simultaneously
+.0942 (dxy[1,5)), +.0596 (dxy[5,10)), +.0793 (vxy[5,10)) and +.0945 (vxy[10,30)), the best displaced
profile any arm in this round has produced, and dup is -.0041.
That is the strongest available evidence FOR the maintainer's hypothesis: the enrichment rows did not
merely shift a threshold, they gave the head a positive class broad enough in geometry to stop
trading its two displaced populations against each other.

### THE HONEST PROBLEM: fake +.0155, and it is a BAR problem not a head problem
`enrich_eval.py` chose -1.234 to hold the PU200 exempt-T4 fake survival fixed on the DUMP rows, but the
dump is pre-arbitration -- the served fake rate is counted after K9 claim/arbitration, and for every
one of arms H (+.0083), I (+.0219) and K (+.0155) the offline-matched bar came out too loose online.
**That is a reusable lesson: match the bar offline to get a starting point, then scan it online; the
pre-arbitration fake cost systematically under-predicts the post-arbitration one.**
Scan running at `M4D = 0.0` and `+0.6` on `int_K_bin` (no rebuild -- the env hook). Arm I's scan is
the cautionary precedent (its displaced gains evaporated when tightened); the question for K is
whether its gains are bar-driven like I's or head-driven. **Until that lands, arm K is a promising
displaced arm with an unpriced fake cost, and it must not be quoted as shippable.** Arm B at
`M4D=-1.912 M4=3.844 MR=-2.5` (RESULT 11) remains the only configuration measured to beat master on
all three headline metrics at once.

============================================================================================
# [A2 RESULT 13] **cube50 ENRICHMENT REACHES MASTER-OR-BETTER ON THREE OF THE SIX cube50 BANDS AND
# CLOSES dxy[5,10) TO INSIGNIFICANCE.** This is the round's stated goal, on the sample the round is
# named after, and it came from adding cube50 rows to the gate's training set.
============================================================================================

Arm K (dxyFull re-key + 1,651 cube50 rows at uniform weight), `int_K_bin`, `LST_CHAIN_M4D=-1.234`.
cube50, 5000 evt, `cube_ref/band_census.py` against `cube_ref/cube50_master.root`.
"SHIPPED" = the round-opening numbers at the top of this file.

    band        denom   MASTER   SHIPPED   ARM K    K - SHIPPED   K - MASTER (Poisson sigma)
    dxy [ 1, 5)  2009    .1195    .1090    .1359     +.0269      **+.0164 AHEAD** (1.5 sig)
    dxy [ 5,10)  2598    .0778    .0616    .0720     +.0104        -.0058  NOT SIGNIFICANT (0.8 sig)
    dxy [10,30)  9353    .0269    .0056    .0089     +.0033        -.0181
    vxy [ 1, 5)   166    .3253    .2831    .3313     +.0482      **+.0060 AHEAD**
    vxy [ 5,10)   631    .2345    .2013    .2409     +.0396      **+.0063 AHEAD**
    vxy [10,30)  5521    .0639    .0389    .0471     +.0082        -.0168
  T4-CLASS OBJECT COUNTS, the class the whole round is about:
    dxy [ 1, 5)   master 34   shipped 12   **ARM K 62**   (1.8x MASTER)
    dxy [ 5,10)   master 56   shipped 14   **ARM K 46**
    dxy [10,30)   master 210  shipped 20   **ARM K 37**   (1.9x shipped, still 5.7x behind master)
    vxy [10,30)   master 166  shipped 17   **ARM K 64**   (3.8x shipped)

**Every band improves against our own baseline. Three cross over master. dxy[5,10), which opened the
round at -.0162, is now -.0058 +/- .0076 -- statistically indistinguishable from master.** The T4-class
count in dxy[1,5) is now 1.8x master's at better efficiency, which is the first time in this round that
our 4-layer class has out-produced master's anywhere.

## WHAT REMAINS BEHIND, and why, without hand-waving
dxy[10,30) is still 5.7x behind master in T4-class count. That is **A1's ceiling, not my gate**: A1's
no-gate test showed 87% of the cube50 low-pT 4-layer objects at that impact parameter are NEVER BUILT,
so no training and no threshold can reach them. vxy[10,30) is -.0168, and it is the band arm K gives up
least of any displaced arm (arm B -.0275, arm H -.0223).

## THE UNPRICED COST, stated as loudly as the win
Arm K at this bar costs **fake rate +.0155 on PU200** (.0624 vs shipped .0470, .0170 vs master). It is
NOT shippable at this bar. The tightening scan (`M4D = 0.0` and `+0.6`, no rebuild needed) is running
and is the last thing my arm needs. Arm I is the cautionary precedent -- its displaced gains were
purely bar-driven and evaporated when tightened -- so the open question is whether arm K's are
head-driven. Arm K's PU200 profile argues they are: it is the ONLY arm that improved the vxy bands
(+.0074 on vxy[10,30)) at the same time as the dxy bands, which a loose bar alone does not do (arm I at
its loose bar was -.0230 on vxy[10,30) while gaining dxy).
**Until that scan lands the shippable configuration remains RESULT 11 (arm B + three bars, ahead of
master on all three headline metrics), and arm K is the best displaced candidate with an open fake bill.**

--------------------------------------------------------------------------------------------
[A2 CLOSING NOTE] Arm K's bar-tightening scan (`K_m00` M4D=0.0, `K_m06` M4D=+0.6, PU200 only, on
`a2_ref/int_K_bin`) was still executing when I stopped. It decides whether arm K -- the enrichment arm,
and the best displaced profile in the round -- is shippable or bar-driven. The decision rule and the
exact commands are in `a2_ref/A2_HANDOFF.md` under "THE ONE THING STILL IN FLIGHT"; the two cmp files
will appear at `a2_ref/meas/K_m0{0,6}_cmp.txt` with no further action needed.
REUSABLE LESSON from three heads: **the offline cost-matched bar under-predicts the online fake cost
every time** (arm H +.0083, arm I +.0219, arm K +.0155 of fake rate at their offline-matched bars),
because the dump is PRE-arbitration while the fake rate is counted after K9 claim/arbitration. Use
`refit_bars.py`/`enrich_eval.py` to get a starting point, then always scan 2-3 points online. Arm I is
the reason this matters: at its offline-matched bar it looked like the best displaced arm in the round
(vs master dxy[1,5) +.0984, vxy[10,30) +.0972) and it was simply paying +.0235 of fake for it.

============================================================================================
# [A2 RESULT 14] **ON cube50_highPt THE ENRICHMENT ARM BEATS MASTER IN FIVE OF SIX BANDS, TWO OF
# THEM AT 3.2 AND 4.5 SIGMA, AND IS AT PARITY IN THE SIXTH.** This is the sample the ceiling said
# was winnable, and it is won.
============================================================================================

Arm K (dxyFull re-key + 1,651 cube50 rows, uniform weight), `int_K_bin`, `LST_CHAIN_M4D=-1.234`.
cube50_highPt 5000 evt -- **and note this sample is in NO training input**, so every number below is
out-of-sample. `cube_ref/band_census.py`; MASTER = `cube_ref/cube50_highPt_master.root`,
SHIPPED = `cube_ref/cube50_highPt_ours.root` (round-2). Full logs `a2_ref/meas/K_enrich_cube50hp_census.txt`.

    band        denom   MASTER   SHIPPED   ARM K    K-SHIPPED   K-MASTER (Poisson sigma)
    vxy [ 1, 5)   227    .2115    .2423    .2555     +.0132     +.0441 AHEAD (1.0 sig)
    vxy [ 5,10)   834    .1031    .1211    .1607     +.0396   **+.0576 AHEAD (3.2 SIGMA)**
    vxy [10,30)  7434    .0180    .0182    .0204     +.0022     +.0024 AHEAD (1.0 sig)
    dxy [ 1, 5)  2706    .0710    .0780    .0831     +.0051     +.0122 AHEAD (1.6 sig)
    dxy [ 5,10)  3327    .0129    .0177    .0292     +.0115   **+.0162 AHEAD (4.5 SIGMA)**
    dxy [10,30) 12586    .0011    .0002    .0008     +.0006      -.0003 PARITY (0.8 sig)
  T4-CLASS COUNTS -- the 4-layer class this whole round is about:
    vxy [ 5,10)   master  5   shipped  3   **ARM K 16**   (3.2x master)
    dxy [ 5,10)   master 11   shipped  2   **ARM K 14**   (1.3x master)
    dxy [ 1, 5)   master 12   shipped 14   **ARM K 23**   (1.9x master)
    vxy [10,30)   master 21   shipped  8   **ARM K 14**
    dxy [10,30)   master 13   shipped  **0**   ARM K 3     (shipped had NO 4-layer objects at all here)

**Every band improves against our own baseline; five of six are ahead of master; two are ahead by
3.2 and 4.5 sigma; and in dxy[10,30) the shipped configuration reconstructed ZERO 4-layer objects
while arm K reconstructs 3.** Taken with RESULT 13 (cube50: three bands ahead of master, dxy[5,10)
closed to insignificance, dxy[1,5) T4-class 1.8x master), the enrichment arm delivers a displaced win
on BOTH cube samples, out-of-sample on this one.

## THE COMPLETE THREE-SAMPLE CONTRACT FOR ARM K, both halves
    PU200RelVal 1000 evt   eff .8088 (-.0011 shipped / -.0012 master)
                           dup .0473 (-.0006 / **-.0041**)
                           fake .0624 (**+.0155** / **+.0170**)   <-- THE BILL, and it is unpaid
                           dxy[1,5) +.0224/+.0942  dxy[5,10) +.0447/+.0596  dxy[10,30) +.0095/-.0133
                           vxy[1,5) -.0038/+.0214  vxy[5,10) +.0015/+.0793  vxy[10,30) +.0074/+.0945
    cube50 5000 evt        3 of 6 bands ahead of master, dxy[5,10) insignificant, every band up vs shipped
    cube50_highPt 5000 evt 5 of 6 bands ahead of master (2 significant), 6th at parity, every band up

## SO THE HONEST SUMMARY OF THE ENRICHMENT ARM IS ONE SENTENCE
**cube50 enrichment buys a large, significant, out-of-sample displaced win on both cube samples and
on five PU200 displaced bands, and it currently costs .0155 of PU200 fake rate; whether that bill can
be paid down by the bar is the one measurement still running** (`K_m00` at M4D=0.0, `K_m06` at +0.6,
both in the histogram pass; decision rule in `a2_ref/A2_HANDOFF.md`). Arm I is the precedent that says
this must be checked rather than assumed. Until it lands, the shippable configuration is RESULT 11
(arm B + three bars: ahead of master on eff, dup, fake and 6 of 7 PU200 displaced bands), and arm K is
a measured displaced win with an open fake bill.

============================================================================================
# [A2 RESULT 15 -- FINAL] THE ARM-K BAR SCAN IS IN. **ARM K DOES NOT COLLAPSE LIKE ARM I: IT
# DEGRADES GRACEFULLY, ITS CURVE STRICTLY DOMINATES ARM H, AND ENRICHMENT PAYS AT EVERY POINT.**
# But it cannot be made fake-neutral, so the round ends with TWO Pareto points, not one winner.
============================================================================================

Arm K's bar scanned over 1.83 logits on `int_K_bin` (env hook, no rebuilds). PU200RelVal 1000 evt,
all deltas vs round-2 SHIPPED (`win_ref/all4_rv1000_hists.root`):

    arm K, M4D    eff      dup      fake     dxy[1,5)  dxy[5,10)  dxy[10,30)  vxy[10,30)   n TC
      -1.234    -.0011   -.0006   +.0155    +.0224     +.0447     +.0095      +.0074    +48,157
       0.0      -.0009   -.0007   +.0095    -.0016     +.0318     +.0076      -.0019    +29,334
      +0.6      -.0008   -.0007   +.0076    -.0136     +.0129     +.0044      -.0069    +22,382
    for contrast, ARM I over its own scan (RESULT 10b): displaced went POSITIVE -> NEGATIVE
      -2.204    -.0015   -.0003   +.0219    ...        ...        +.0069      +.0100
      -1.2      -.0003   -.0004   -.0007    ...        ...        -.0133      -.0230
**Arm K stays positive on dxy[5,10) and dxy[10,30) across its whole scan; arm I flipped sign. So arm
K's gains are HEAD-driven, arm I's were BAR-driven.** That is the discriminating test I set up in
RESULT 12, and arm K passes it.

## ARM K STRICTLY DOMINATES ARM H -- i.e. THE ENRICHMENT ROWS PAY, not just the re-key
    arm H (no cube50)        fake +.0083   dxy[5,10) +.0129   dxy[10,30) +.0129*  (*vs master -.0215)
    arm K @ +0.6 (cube50)    fake +.0076   dxy[5,10) +.0129   dxy[10,30) +.0044
    arm K @ 0.0  (cube50)    fake +.0095   dxy[5,10) **+.0318**  dxy[10,30) +.0076
At equal-or-lower fake cost arm K matches arm H, and for .0012 more fake it nearly triples the
dxy[5,10) gain. **1,651 cube50 rows at weight 1 -- 0.12% of the training set -- moved the whole
frontier outward.** Within arm K's own curve the exchange is ~4:1 in favour of displaced
(.0079 of fake bought .0318 of dxy[5,10)); the B->K transition is the expensive segment (~1:1).

## IT CANNOT BE MADE FAKE-NEUTRAL. Extrapolating the last segment (0.0 -> +0.6 gave -.0019 fake for
-.0189 dxy[5,10)), closing the remaining +.0076 of fake needs ~4 more such steps and would drive
dxy[5,10) well negative. **So arm K is not a free win; it is a priced one.**

## THE ROUND ENDS WITH TWO PARETO-OPTIMAL OPERATING POINTS. I am not choosing between them --
## they answer different questions and the maintainer's priority ordering decides.

  **(1) ARM B + `M4D=-1.912 M4=3.844 MR=-2.5`** -- the unambiguous ship. vs MASTER:
      eff **+.0002**, dup **-.0037**, fake **-.0015**, and 6 of 7 PU200 displaced bands AHEAD.
      Turns our fake-rate deficit into a lead. cube50 essentially unmoved. Weights-only.
      Take this if "no metric may regress" is the rule.

  **(2) ARM K + `M4D=0.0`** -- the displaced ship. vs MASTER:
      eff -.0010, dup **-.0042**, fake **+.0111**,
      dxy[1,5) **+.0702**, dxy[5,10) **+.0467**, vxy[5,10) **+.0724**, vxy[10,30) **+.0852**,
      dxy[10,30) -.0152 (improved from -.0228),
      cube50: 3 of 6 bands ahead of master, dxy[5,10) insignificant, dxy[1,5) T4-class 1.8x master,
      cube50_highPt: **5 of 6 bands ahead of master, two at 3.2 and 4.5 sigma**, 6th at parity.
      Take this if displaced efficiency (the project's stated headline advantage) outranks .0111 of
      fake rate. Note eff OVERALL is -.0010, which is the one thing that argues against it under a
      strict "efficiency first" reading.

## THE FINAL WORD ON THE ROUND'S CENTRAL HYPOTHESIS
**The maintainer was right that enrichment is needed, and right about the sample.** What was missing
was not the cube rows but the AXIS and the COVERAGE: the shipped gate's displaced class was keyed on
`simVxy`, which is the axis we already beat master on and which is only DEFINED for 5.7% of positives.
Fix the axis (`|sim_pca_dxy|` of the matched FULL sim row, 100% coverage) and the cube50 rows then have
a class to land in -- and 1,651 of them, at uniform weight, move the entire displaced/fake frontier
outward and win 5 of 6 bands on an out-of-sample cube sample. Enrichment alone would not have done it;
the re-key alone would not have done it (arm H is dominated). Both together did.
