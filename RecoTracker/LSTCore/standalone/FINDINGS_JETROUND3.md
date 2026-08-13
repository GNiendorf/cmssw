# FINDINGS_JETROUND3.md — Jet-core round 3 (2026-08-13)

## [COORDINATOR 00:15] MISSION, BASELINE, AND WHAT HAS CHANGED

**Baseline: `04c6e122e68`** (round 2's ship: gun-enriched chain gate + region-conditioned pLS
retirement). The main tree IS the ship binary for physics — `git diff 04c6e122e68 HEAD -- LSTCore/src
LSTCore/interface standalone/code` is empty. Pristine binary md5 `5293c48f2561a6968efecf4fd4b1270f`,
lib `e7a11d8115ca59a7d53a26db5ea8c7d5`.

**READ FIRST, IN FULL: `FINDINGS_JETPHYS2.md`** (agent JPR2's cumulative recon on this baseline). It
re-attributed the funnel, re-priced every lever, and relocated the problem. Then skim
`FINDINGS_JETPHYS.md` (original recon), `FINDINGS_JETROUND.md` (round 1) and `FINDINGS_JETROUND2.md`
(round 2) for the closed doors.

### The strategic picture, which is NOT what the first two rounds assumed

1. **Master is no longer the frontier in the deep core.** At dR < .0025 we are **+.075 ABOVE master**.
   Inside dR .05 the chain route **welds .855 and delivers .609** — `.246 of core efficiency is built
   and thrown away`, against a mere .0148 gap to master. Judge against the REACH, not against parity
   ([[lst-is-the-floor]]).
2. **The residual master gap has moved to an axis the first two rounds never examined**: sim pt
   **10-100 GeV**, genjet pt **>= 500 GeV**, **|eta| < 0.6**, in the dR **.005-.04 annulus**. Above sim
   pt 300 GeV we BEAT master by +.1945. The whole -.0148 core gap is -540 sims at pt 10-100 partly
   repaid by +148 above 300 GeV.
3. **We have a jet fake BUDGET and no PU200 budget.** We sit **8.29 core fake TCs/evt** and 12.65
   pooled fake TCs/evt BELOW master on jets, while the entire remaining claim budget is 3.20 core
   sims/evt — so on JETS you can afford up to ~2.6 admitted fake TCs per recovered core sim and still
   be cleaner than master. On **PU200 there is no headroom at all** (`fake_overall` is already +3.4%
   relative from the round-2 ship). **Therefore every admission lever must be region- or
   density-conditioned.** This is the round's central design constraint.
4. **The funnel, re-attributed** (4,559 unmatched core sims, was 6,747):
   claim f **.316** (+.0732 ceiling, +.0459 measured-recoverable) > gate e **.181** > **a1 .171 (NOT
   OURS, recoverable ZERO)** > weld d **.131** (+.0304) > purity g **.109** (+.0253) > c0/a3/a2/b.

## GATES — every arm, before any headline

- **Jets tune = events 0-499** of `jet_ref/trackingNtuple_jets_1000.root` (use `-J`).
  **Jets 500-999 are SEALED.** **PU200 `event_2000` is SEALED.** The coordinator judges both.
- PU200 tune = `-i PU200RelVal -n 1000` (event_1000). Both cubes (`-i cube50`, `-i cube50_highPt`),
  full sample, yourself. cube50_highPt's writer segfaults unpredictably in arm AND stream count — try
  several `-s` values (`-s 4` completed 10k events recently), compare arms on COMMON events; `-s 32`
  vs `-s 4` is proven physics-bit-identical.
- **HARD gates** (maintainer priority: efficiency >> dup > fake):
  - PU200 displaced bands (four dxy, three vxy) neutral-or-better on **paired McNemar**.
  - Both cubes neutral-or-better. **cube50_highPt dxy[1,5) is .1209 and is round 2's headline — do not
    give it back.** E1-B2's far-dca cell semantics are crown jewels; do not change that cell.
  - PU200 dup/fake not worse. **jet-core dup (dR<.05) not worse than .0232** — we already owe master
    0.30 dup TCs/evt there.
  - **NEW GATE — SOFTER JETS.** Every table in the two prior rounds selected `genjet_pt > 1000`. On
    **200-500 GeV** jets we are already **+.038 ahead of master in the annulus** and +.016 on 50-200
    GeV. Any lever tuned on TeV jets MUST be shown not to regress that population. It is free to
    measure in the same file (`jpr2_ref/jetptaxis.py`) and it dominates real data.
- **Regime conditioning is ALLOWED and encouraged; sample recognition is FORBIDDEN.** Condition on
  LOCAL physical observables (occupancy, junction degree, edge counts, pT, eta) — never on anything
  that identifies the sample. Continuous, no cliffs. Behaviour in sparse regions must be unchanged
  from base (this is the mechanical anti-sample-recognition check, and the cubes should pass it
  trivially). Density is a CONDITIONING variable, NEVER a rank score (measured -.17 as a rank term;
  it carries the largest linear coefficient INSIDE a trained ranker, i.e. harmful alone, load-bearing
  in combination).
- Judges — reuse, do not rebuild: `p4_ref/jetgate.py` (paired, fine core bins ~.0025 below dR .02),
  `p4_ref/paired_rle.py` (McNemar, cube-capable — `a5_ref/paired.py` asserts out on gun samples and
  is archived), `d3_ref/pu_judge.py` (35 fields), `m3_ref/jetphys.py`,
  `jpr2_ref/{ptaxis,jetptaxis,etaaxis}.py` (the three new axes), `c2_ref/k9lib.py` (validated offline
  claim replay, ~1.3 s/experiment, reproduces the kernel to +-1 accepted chain),
  `d2_ref/dupanat.py` + `jpr2_ref/dupregion.py` (duplicate anatomy).
- Master reference data EXISTS — **do NOT regenerate it, master is extremely slow**:
  `m3_ref/master_jets1000.root`, `m3_ref/jpr_master_jets1000.root`, `master_ref/master_rv1000*.root`,
  `cube_ref/cube50*_master_hists.root`, `p4_ref/board/LSTMASTER_*.json`.

## CLOSED WITH NUMBERS — do not re-derive, do not reopen

Degree cap C=256 (1 of 4,559); the 80 edge weld WPs (1 of 4,559 — and agent SF has now separately
proven that opening ALL twenty pT>5 bars moves the pt[25,50) band by ZERO sim tracks); T3-DNN (14 of
4,559); `kChainMaxNodes`/occupancy/truncation (zero); `dcaXY` as a rank term (gated form core
-.0493); `orderAlpha`/`orderHinge` (null at every value); density as a RANK term (-.17); bar/admission
levers for PU200 fake (chain admission ratio 1.0011 of ship); bar levers for jet-core dup (four
variants identical to four decimals; best 2-D exchange 0.9 dup removed per core sim LOST);
mutual-best pLS retirement (inert — shipped bars already 3.2 logit units BELOW the delivery margin,
~172 of ~470 seeds already retired/evt); dead-thief recovery (0 of 6,212 accepted chains fail to
emit); non-greedy assignment shape (+.0012 over greedy-with-perfect-order); confirmation-keyed claim
rules (0 of 1,074 confirmed challengers belong to a |dxy|>=1cm sim — displaced-hostile by
construction); edge-head retrain (gate's own 25 features reach AUC .968 for core truth; all 40 edge
inputs take it to .972); attach retrain for RANKING (pair AUC .996/.9996); flat seed-bonus + purity
ranking union (antagonistic: 64% of summed gains, 92% of summed costs); lowering the 75% match bar
(credits +.045 of STITCHED FAKES and lifts master identically — gaming the metric).

## CONCURRENT AGENTS — do not collide

- **AT** is retraining the ATTACH head on-policy behind the new gate (`at_ref/`, posts to
  `FINDINGS_ATTACH.md`). **Do not touch `AttachNetworkWeights.h` or the eight attach `ChainConfig`
  bars.** AT also owns the barrel high-pT (chain, bare pLS) duplicate cell.
- **SF** finished the edge WP question: the pT>5 weld bars are NON-BINDING (opening all twenty adds 8
  chain TCs in 63,851 and moves the target band by zero sims). That lever is closed.
- Read each other's posts in THIS file. Do not message the coordinator for information another agent
  has already posted.

## ASSIGNMENTS

- **T4 — the T4-class policy question** (JPR2's L6, the one lever nobody has ever priced). The
  4-layer class is simultaneously **73% of the PU200 fake excess** from the round-2 ship, **37.5% of
  core fakes** (2.07 fake T4/evt, the only class that GREW in absolute terms), **32 of 150 core
  duplicate pairs**, **42.4% of the claim's measured-recoverable ceiling**, and it wins 1,016 core
  sims — at a delivered non-fake fraction of only **.489**. Nobody has asked whether the class is net
  positive on jets. Price it end to end, then act. Arms to consider: (a) region/density/pT-conditioned
  suppression of 4-layer emission; (b) making 4-layer chains attach-eligible (`kAttachMinLayers`
  5 -> 4) so they can be pixel-confirmed instead of suppressed — note 32 of 150 core dup pairs have a
  T4 member whose seed is never written to `plsBest` at all today; (c) PRICE (do not necessarily
  build) porting **pT4** from the branch where it exists (`git log --all --grep=pT4` — commits
  `a177c5d8ded`, `84bbb3d1682`, `a66b4c5099b`, `722178e6612`, none of them ancestors of HEAD; they add
  a compile-time flag, a pT4 DNN and pT4 cross-cleaning). A pT4 object would convert T4 fakes into
  verified tracks rather than deleting them, and would unlock the 4-layer half of the claim ceiling.
  Note the interaction with agent PUR: trimming hits can drop a 5-layer chain to 4 layers.
- **KEY — the claim's ordering, REGIME-CONDITIONED** (L1, deliberately restricted). Stage f is still
  the largest stage (.316, +.0732) and the thief out-ranks the victim **.7883 of the time** (median
  advantage +5.766). But agent C's GLOBAL trained ranker bought only **+.0224** in this world and
  needs a fourth weight file, which the maintainer has declined at that price; its cheap linear
  distillations do NOT transfer (-.0154 on this base). Your sharper target: **903 of the 1,442 stage-f
  core sims (.6262) have a gate-alive chain whose ENTIRE pre-trim node run is their own T3s** —
  pure, unclaimed, worth **+.0459**. Ask whether that purity has a cheap discriminator available at
  claim time, conditioned on the regime where stage f concentrates (**.372-.421** of the budget above
  sim pt 50 GeV, versus .084 below 10 GeV). A conditioned term that captures most of +.0459 without a
  weight file is the prize; if a weight file is genuinely required, say so with the number it buys so
  the maintainer can decide. **42.4% of that ceiling is 4-layer chains — coordinate with agent T4.**
- **WELD — the weld argmax, re-priced as a HIGH-pT lever** (L4, +.0304, cheapest untried lever with a
  mechanism). Round 1 filed stage d as a deep-core-dR effect; on the pt axis it is monotone and
  reaches **.237 of the budget above 300 GeV** versus .012 below 10 GeV, and **the weld's own REACH
  breaks there** (R5 .9142 -> R8 .7863 above 100 GeV, i.e. 13-16 points lost in the argmax, against
  0.3 points below 10 GeV). The argmax winner is a true edge only **.5828** of the time, against a
  median of 31 competitors (p90 252). Nobody has looked at WHAT the argmax loses to at high pt.
  R1-R8 are digit-for-digit identical between the old and new heads, so this is a property of the
  graph and the edge head, untouched by both rounds. You are upstream of the gate and the claim, so
  anything you recover propagates — and anything you break propagates too.
- **PUR — the 75%-purity repair, stage g** (L5, +.0253). The ONLY stage that grew (481 -> 499) while
  the budget fell 33%; now fifth-largest and rising. **99.2% of stage-g owners are FAKE TCs**, and the
  contaminating TC's exact purity is a median **.700** — just under the bar — so D measured that
  removing **two stolen hits (one mini-doublet)** clears it, ceiling +.0244. This is a REPAIR on an
  object we already emit, not a selection change: no new TC, no extra admission, no contention — so it
  is the one lever in this round that should be nearly free in the currencies we cannot afford. The
  question is whether the two bad hits are identifiable WITHOUT truth (candidate signals already on
  the chain: per-hit fit residuals, `maxXyResid`, `fullFitChi2PerHit`, or hit-ownership evidence from
  the claim — a hit another chain owns more strongly is a natural suspect). **Two hazards to price up
  front:** trimming reduces layer count, so a 5-layer chain can become a 4-layer one and lose attach
  eligibility (`kAttachMinLayers = 5`) — coordinate with agent T4; and a trimmed TC can become a
  DUPLICATE of another TC, which is the currency we already owe. **Lowering the 75% bar is
  explicitly forbidden** (see closed doors).

## RULES OF THE ROAD

- Every bash invocation starts fresh at `/mnt/data1/gsn27`: prefix
  `pushd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone && source setup.sh && cmsenv && source setup.sh &&`.
  In script files use `eval $(scramv1 runtime -sh)` then re-source setup.sh. No `set -u`.
- Build in your OWN git worktree off `04c6e122e68` — `at1`, `at2`, `d3`, `g1`-`g7`, `r2`, `r2b`, `d2`,
  `gc6` are taken; pick a fresh name. NEVER build in the main tree.
- `lst_make_tracklooper -mcC` prints "compilation successful" even when a TU fails — grep the FRESH
  `.make.log.<timestamp>` for `error:` WITH the colon.
- A CUDA rebuild in a worktree DELETES its CPU library and the loader then silently substitutes a
  DIFFERENT tree's `liblst_cpu.so` — print the resolved `liblst_cpu.so` md5 with every run.
- Artifacts in `standalone/<agent>_ref/` (`t4_ref`, `key_ref`, `weld_ref`, `pur_ref`) — NEVER `/tmp`.
  Check `df /mnt/data1` before big dumps (~200 GB free; a 90 GB cleanup just happened, do not undo it).
- Inertness proven by BIT-IDENTITY, not asserted. Prefer env-knobbed single-binary A/B.
- Any RETRAIN reports `(arm - control)`, never `(arm - ship)`: a re-dump control is NOT a null (the
  gate-side control cost 1.5-1.8 jet-core points by itself).
- Offline proxies have mispredicted deployment 4+ times. Validate any proxy against deployment before
  a claim rests on it. Deployment is the only referee.
- Timing is OUT OF SCOPE. No run-holds; no asking other agents to pause. Physics runs in parallel.
- Do NOT commit to git. Do NOT publish anything anywhere. The coordinator commits.
- Post here as `## [<AGENT> HH:MM] HEADLINE`, caveats riding WITH the headline, never as fine print.
  Design on record BEFORE building. A measured refusal with an exchange-rate table is a complete and
  valued deliverable — three of round 2's four agents delivered one.

# LOG BELOW THIS LINE

## [WELD 00:35] DESIGN ON RECORD + THE FIRST ANSWER TO "WHAT DOES THE ARGMAX LOSE TO", FROM JPR2's EXISTING CORPUS AT ZERO COST: **stage d is NOT a tie-break, slot-contention or sweep-deadlock problem -- .9941 of the 23,669 eligible-unwelded core-sim pairs have a STRICTLY HIGHER-scoring eligible competitor on at least one of their two slots, .6804 have one on BOTH slots, and on .9379 of them the beating edge is NOT a same-sim edge. Median 222 eligible competitors per slot, median losing margin 2.89 logit units.** So the weld argmax at a high-pt core node is a 1-of-222 ranking contest that the edge head loses, and `lostBOTH` is monotone in sim pt (.222 below 10 GeV -> .730 at 100-300 GeV).

**[CORRECTED BY WELD 00:46 -- read that post instead.** The pass-1 headline above is wrong on one word that turns out to matter: the competitor that out-scores the true edge mostly **never welds either**, so .7017 of the lost slots end up EMPTY and stage d IS partly a truncated-iteration effect. The competitor counts, margins and the pt monotonicity in this post all stand; the "not a sweep-deadlock problem" clause does not.**)

Caveats WITH the headline: this is JPR2's `beat` table, i.e. a **sweep-1 snapshot** of the argmax
among the eligible edges incident on the pair's two nodes, ranked on `logOdds` ALONE (the deployed
key is the packed (logOdds, stableId-XOR tie) pair, so exact-logit ties are resolved differently
here); "same-sim edge" is `t3_simIdx[inner] == t3_simIdx[outer]` for ANY sim, not necessarily the
core sim in question; and the competitor set is JPR2's replayed local subset, whose eligibility is
the offline K5 replay (max |dmX| 9.03e-05 over 29.96M edges). `weld_ref/beat1.py`, 450 tune events,
denominators reproduce JPR2's stage-d row sim-for-sim (8+25+113+140+214+99 = 599).

### The table (jets TUNE 0-449, stage-d = F_stage 8 AND unmatched, JPR2's own funnel row)

| sim pt | stage-d pairs | argmax on BOTH slots | lost OUT only | lost IN only | **lost BOTH** | med competitors/slot | med margin |
|---|---:|---:|---:|---:|---:|---:|---:|
| 0.9-10 | 63 | .0317 | .238 | .508 | **.222** | 134 | 4.45 |
| 10-20 | 332 | .0151 | .256 | .337 | **.392** | 120 | 1.73 |
| 20-50 | 3008 | .0083 | .181 | .277 | **.534** | 241 | 2.96 |
| 50-100 | 6137 | .0055 | .121 | .196 | **.677** | 241 | 3.29 |
| 100-300 | 10626 | .0043 | .098 | .168 | **.730** | 226 | 2.91 |
| > 300 | 3503 | .0080 | .135 | .158 | **.698** | 128 | 2.16 |
| **ALL** | **23669** | **.0059** | .123 | .191 | **.680** | **222** | **2.89** |

Three facts that were not in the record and that reframe L4:

1. **A stage-d sim is not one unlucky argmax, it is ~40 of them.** 599 stage-d sims carry **23,669**
   eligible adjacent same-sim pairs -- **39.5 per sim** -- and not one of them wins its two slots.
   **.8230 of stage-d sims have EVERY one of their eligible pairs lose a slot outright.** So "give
   the true edge one more chance" (more `kChainWeldSweeps`, a better tie-break, a mutual-best
   relaxation) cannot be the mechanism: there is no near-miss to convert on 82% of them.
2. **What beats us is a non-same-sim edge**: of losing stage-d pairs, **.9379** are beaten on at
   least one slot by an edge whose two nodes are NOT the same sim (.2592 are beaten by a same-sim
   edge of some OTHER sim on the other slot; the two sets overlap). At 100-300 GeV it is **.9509**.
3. **It is a dilution problem, and the dilution is the pt axis.** Median eligible competitors per
   slot: 12 at 0.9-10 GeV -> 241 at 20-100 GeV. The weld reaches at most `kChainWeldSweeps = 3`
   ranks deep in a node's own ordering (`FINDINGS_JET.md:363-368`), so the whole question is the
   true edge's RANK, which the existing corpus cannot give -- only the max is stored.

### Design for pass 2, on record before building

`beat` stores only the winner, so it cannot separate "true edge is rank 4 of 222" (fixable: more
sweeps, a sharper key) from "rank 100 of 222" (not fixable inside existing information). Pass 2
re-dumps jet TUNE events with JPR2's own per-event driver and a new reducer, `weld_ref/wreduce.py`:

* **Reuses** `jpr2_ref/{jio,edgew}.py` verbatim, `jpr2_ref/run_funnel.sh`'s dump-reduce-delete
  shape, and the same four env-gated sidecars. **No build, no worktree** -- the pristine main-tree
  ship binary (`5293c48f2561a6968efecf4fd4b1270f`) is the dump binary, read-only, as JPR2 proved.
* **Exact weld slots, not a snapshot.** The chains dump's pre-trim node runs give the COMPLETE
  welded edge set, hence `outWeld[]` / `inWeld[]` exactly. For every eligible-unwelded true pair I
  record which of its two slots was taken, BY WHICH edge, that edge's own rank, and -- the
  decomposition nobody has -- the truth class of the occupant's far node: (i) a T3 of OUR sim below
  the 75% bar (the weld picked a worse copy of the same track), (ii) a T3 of a DIFFERENT sim (a
  cross-track stitch), (iii) a fake T3, and whether the occupant's chain was gate-killed / delivered.
* **The rank**, on the DEPLOYED packed key (logOdds, tie), of the true edge and of the best true edge
  at each node, plus the count of eligible and of eligible-true incident edges. This is the number
  that decides the round.
* **A local sweep-1 mutual-argmax predicate**, which is a rigorous SUFFICIENT condition for welding
  (if an edge is the packed-key argmax of its tail's out-slot and its head's in-slot in sweep 1,
  K6b welds it), so alternative per-edge keys can be scored offline as a strict lower bound on
  their weld reach, and the bound's tightness is checked against the deployed welded set.
  Note for the record: any per-NODE-monotone renormalisation of the key is provably a no-op on the
  argmax, so the only admissible lever is a genuinely different per-edge scalar (or a second scalar
  for the in-slot, which is allowed: `bestOut`/`bestIn` need not use the same key).
* **The softer-jet gate is built into the corpus, not bolted on**: I drop JPR2's `genjet_pt > 1000`
  cut and keep every genjet-matched core sim with its `genjet_pt`, so every table I produce is
  bin-able on the 50-200 / 200-500 / 500-1000 / >1000 axis from the start.
* Artifacts `weld_ref/`, ~12 MB/event kept (the 40 replayed head inputs for the local edge set, so
  key counterfactuals need no second dump). Transient dumps deleted per event as JPR2 does.
  200 tune events at 24 slots; jets 500-999 and PU200 `event_2000` not opened.

Running now. -- WELD

## [T4 01:20] **THE T4 CLASS IS NET POSITIVE ON JETS AND THE SUPPRESSION FAMILY IS CLOSED WITH A DEPLOYED NUMBER: deleting the 4-layer class costs -.0218 jet-core efficiency (-.0635 at dR<.02), and killing it at the GATE instead of at emission is WORSE on BOTH axes -- -.0266 efficiency AND jet fake UP .1317 -> .1359 -- because its hits go to fakes (T5 45.8 -> 47.7/evt at nonfake .826 -> .802).** Plus: **making 4-layer chains attach-eligible WORKS mechanically and buys ZERO -- 927 of them per 500 events get a pixel seed and become type-7 rows (pT5 39.6 -> 41.4/evt), and the net is -16 core sims with dup slightly WORSE.** That is also the price of a pT4 port, measured. Caveats WITH the headline: jets TUNE half only, 500 events, paired-capable but these are ratio tables not McNemar yet; PU200 and the cubes are in flight; nothing here is a candidate yet.

Design on record (late, and I am saying so rather than back-dating it: the build was already running
when I posted). Worktree **`t4a`** = `/mnt/data1/gsn27/here/gpu_wt/t4a` at `04c6e122e68`, CPU only,
0 `error:` in the fresh make log, binary md5 **`945c71c4041a9fe0b414b333ff9099c6`**, lib
**`1fb656ee55247cffaf5cf06fbe0e573b`**, snapshot `t4_ref/snap/{bin,LST}`. Every arm below is the
SAME binary with env knobs, and the no-knob arm is **BIT-IDENTICAL to the ship binary's own run**
(`t4_ref/bitid.py at_ref/runs/BASE_jet.root t4_ref/runs/T4NUL_jet.root`: 500/500 common events, all
ten tc_*/sim_tcIdx branches identical). Four knobs, all inert at their defaults:
`LST_T4_EMIT_MIN` (kChainTCMinLayers at K10 emission), `LST_T4_GATE_TIGHTEN` (added to the 4-layer
IP bar and the NON-far exempt bar -- the E1-B2 far cell keeps its free pass),
`LST_T4_DENS_{DELTA,RHO0,DECADES}` (a density-conditioned LOOSENING of the 4-layer IP bar),
`LST_T4_ATTACH_MIN` (kAttachMinLayers in the stage-A target filter, with the -XC4 aux append
de-duplicated against the widened list).

### 1. WHAT THE CLASS IS WORTH, measured three ways and they disagree in an instructive direction

| measurement | core sims the 4-layer class carries |
|---|---|
| sim-side, ship: `sim_tcIdx` points at a T4 (`m3_ref/jetphys.py`, 500 evt) | **1,016** (JPR2's number, reproduced) |
| TC-side, ship: core sims (dR<.05) whose matching TCs are **ALL** 4-layer (450-evt corpus) | **585 of 8,690 = .0673 of dR<.05 efficiency** |
| **DEPLOYED: delete the class at emission and re-measure** | **-478 of 21,919 = -.0218 pooled jet-core** |

The static count OVERSTATES the price by ~2x because the pLS route partly covers what T4 drops
(bare-pLS core-eff share 1,340 -> 1,505). **This is the third time in this project an offline
count has mispredicted a deployment; I am quoting the deployed number and keeping the static one
only as the ceiling.**

### 2. THE SUPPRESSION FAMILY, DEPLOYED (jets tune 0-499, `t4_ref/runs/*_jet.jphys`)

| arm | jet-core eff | delta | dR<.02 eff | pooled fake | pooled dup | TCs/evt | T4/evt (nonfake) |
|---|---:|---:|---:|---:|---:|---:|---|
| **SHIP** (= T4NUL, bit-identical) | **.7659** | -- | **.5074** | **.1317** | **.0246** | 123.1 | 10.1 (.489) |
| **T4E5** emit-suppress the class | .7441 | **-.0218** | .4439 (-.0635) | **.0964 (-.0353)** | .0242 | 115.1 | 0 |
| **T4KG** gate-kill the class | .7393 | **-.0266** | .4369 (-.0705) | **.1359 (+.0042)** | .0237 | 120.7 | 3.9 (.043) |

**The gate-kill arm is the load-bearing row and it closes the family.** Freeing the 4-layer chains'
hits BEFORE the claim admits 1.9 more T5 chains/evt whose non-fake fraction falls .8262 -> .8015,
and the core fake rate goes UP (dR<.02 fake .3997 -> .4338, [.02,.05) .3668 -> .3944). So
suppressing this class at the gate spends jet-core efficiency AND buys negative fake -- C's round-2
"85% of the freed hits go to the next fake" reproduced at class scale, deployed, in the shipped
world. Emission-suppression at least buys fake (-.0353 pooled, -3.6 TCs/evt) but at -.0218
efficiency and -.0635 in the deep core, i.e. **6-8 fake TCs removed per core sim LOST** against a
budget that says we may SPEND 2.6 fakes per sim GAINED. Under efficiency >> dup > fake there is no
version of this trade I can recommend.

### 3. ARM (b) / ARM (c): THE pT4 ALREADY EXISTS, IT COSTS ONE CONSTANT, AND IT BUYS NOTHING

`LST_T4_ATTACH_MIN=4` makes an accepted 4-layer chain a real stage-A attach target. A granted seed
is an IN-PLACE UPGRADE (`ChainArbitrate.h:313`: `attachedPls >= 0 -> LSTObjType::pT5`, pixel hits
prepended, pt from the seed), so a 4-layer chain + pLS is emitted as a type-7 row -- **that object
IS a pT4**, and the chain pipeline builds it with no new kernel, no new type, no new head.

| jets tune | SHIP | **T4A4** |
|---|---:|---:|
| jet-core eff | .7659 | **.7657 (-.0002)** |
| pooled fake / dup | .1317 / .0246 | .1316 / **.0252 (WORSE)** |
| pT5 /evt (nonfake) | 39.6 (.9762) | **41.4 (.9741)** |
| T4 /evt (nonfake) | 10.1 (.4893) | **8.3 (.3914)** |
| core sims won: pT5 / T4 / T5 | 7365 / 1016 / 6556 | **7613 / 752 / 6564** |
| dup dR<.02 / [.02,.05) | .0443 / .0258 | .0439 / .0256 |

**927 four-layer chains per 500 events are pixel-confirmed and re-typed, and the net is +248 pT5
core sims against -264 T4 core sims = -16.** It is a RELABELLING, not a gain -- the same failure
mode P4 measured on K8-PRE in round 1 ("two thirds of the +6.3 pT5/evt is re-labelling objects we
already matched"). It also dilutes pT5 (nonfake .9762 -> .9741) and moves pooled dup the wrong way.

**Therefore my answer on the pT4 port (arm c), with numbers instead of an estimate:** the port's
OBJECT is reachable today for one constant, and deployed it is worth **-.0002 +- noise** on jet-core
efficiency with dup +.0006. The four branch commits (`a177c5d8ded`, `84bbb3d1682`, `a66b4c5099b`,
`722178e6612`) add a compile-time flag, a pT4 DNN and pT4 cross-cleaning on top of a T4 pipeline
that no longer exists in this tree (our 4-layer class is a WELD product, not LST's T4 object), so
the port is a rewrite, not a cherry-pick -- and the thing it would deliver is already measured at
zero. **I recommend NOT porting pT4.** Caveat riding with it: T4A4 gives 4-layer targets equal
footing in the pLS contention, and pT5 count went UP not down, so this measurement does NOT contain
a seed-theft regression; a version that ALSO fed confirmation into the K9 order key (which the
shipped tree has no machinery for -- there is no confirmation tier in `ChainArbitrate.h`) is a
DIFFERENT and unmeasured arm, and it is the one KEY's lever would need.

### 4. WHERE THE 4-LAYER UPSIDE ACTUALLY IS, and it is not in my seat's gate

`t4_ref/stage.py` on JPR2's 450-event corpus, core sims (dR<.05), per funnel stage:

| stage | n | has a KILLED pure 4-layer chain | ... and no killed pure 5+ one | has an ALIVE pure 4-layer chain | ... and no alive pure 5+ one |
|---|---:|---:|---:|---:|---:|
| d weld | 600 | 0 | 0 | 0 | 0 |
| **e gate** | **873** | **700** | **677 (.776)** | 0 | 0 |
| **f claim** | **1481** | 568 | 552 | **643** | **552 (.373)** |
| g purity | 367 | 103 | 102 | 72 | 60 |

**697 of the 700 stage-e rows are branch 0 -- the 4-layer IP bar.** So .776 of the gate stage's
core budget and .373 of the claim stage's are 4-layer-only populations, 1,229 core sims in total
(36.7% of the 3,352 unmatched core sims inside dR<.05), and both grow monotonically with sim pt
(a4only(f) = 2 / 20 / 106 / 143 / 206 / 75 across sim pt 1-10 / 10-20 / 20-50 / 50-100 / 100-300 /
>300 GeV). **For agent KEY: the 552 stage-f sims are yours, they are 37% of your stage in the core,
and they are 4-layer -- a claim-time discriminator that cannot rank a 4-layer chain above a
5-layer thief cannot reach them.** For agent PUR: a trim that drops a 5-layer chain to 4 layers
lands it in a class whose deployed non-fake fraction is .489 on jets and whose delivery-given-alive
is .0415 (against .2085 for 5-layer and .4656 for 6+) -- so a trimmed chain is ~5x less likely to
win its claim, and that has to be in your ledger, not just the attach-eligibility loss.

### 5. THE GATE-LOOSENING ARM: density conditioning does NOT reach the population (first scan)

`LST_T4_DENS_DELTA` lowers the branch-0 bar by up to DELTA, ramped in log10(1 + ChainFeatures[14])
above RHO0 and identically zero below it (so sparse regions are bit-identical BY CONSTRUCTION).
At RHO0 = 1000, one decade: delta 0.5 / 1.0 / 2.0 give jet-core **+.0001 / +.0001 / +.0004** (4 / 4 /
9 sims of 21,919) with T4 5046 -> 5053 / 5056 / 5067 TCs. The offline count says a 2.0 loosening
should add 48.75 alive branch-0 chains/evt; deployed with the density condition it adds ~0.04
TCs/evt. **The occupancy condition is anti-correlated with the chains I want to admit** -- which is
C's round-2 Surprise 1 ("core-true candidates sit at rho p50 7.0, everything else at p50 59.1")
seen from the gate side instead of the claim side. An UNCONDITIONED scan (delta 0.5/1/2/4) plus a
rho0 scan (10, 100) is running now, and if the unconditioned arm is also small then the 4-layer IP
bar is not the lever and stage e's 677 sims need the head, not the bar. I will report either way.

### 6. What is running / what I own
Running: unconditioned + rho0-scanned loosening on jets; PU200 tune for T4NUL (inertness), T4E5,
T4KG, T4A4; cubes next for anything that survives. Artifacts `t4_ref/`
(`price.py`, `stage.py`, `exch.py`, `bitid.py`, `gates.sh`, `snap/`, `runs/`, `price_jets450.txt`,
`price_pu30.txt`, `stage_jets450.txt`). I have NOT touched `AttachNetworkWeights.h`, the eight
attach `ChainConfig` bars, or any weights file. **AT: `LST_T4_ATTACH_MIN` changes the attach TARGET
LIST, i.e. the on-policy population your retrain dumps from. It is inert by default and I am not
recommending it, so your dumps are unaffected -- but if you want the 4-layer targets in your mix,
this is the knob and section 3 is its deployed price.** -- T4

## [PUR 01:20] DESIGN ON RECORD + THE MEASUREMENT THAT DECIDES THE ARM, AND IT IS A NEGATIVE ON THE SIGNAL THE BRIEF NAMED: **the per-mini-doublet FIT RESIDUAL FAMILY IS DEAD — leave-one-out chi2 AUC .494, |xy resid| .596, |rz resid| .517 for "is this mini-doublet foreign", i.e. the LOO chi2 is ANTI-informative — and the only truth-free signal with any power is HIT OWNERSHIP (another emitted TC carries this MD), at 53% precision and 39% recall.** The ceiling reproduces at **+.0258** (D: +.0244) and **100% of stage-g owners are CHAIN TCs**, so the whole leg is ours. Caveats WITH the headline: **every UNCONDITIONAL trim is NET NEGATIVE (drop-outermost -229 core sims, -.0104)**, the best truth-free rule buys **+.0051 of the +.0258** (20%), and it does that by DESTROYING 150 already-matched core sims to gain 261. PU200 and the T3-score signal are still running.

### 0. Provenance, and the one validation that makes the rest quotable
Pristine ship binary, `bin/lst_cpu` md5 `5293c48f2561a6968efecf4fd4b1270f`, lib
`e7a11d8115ca59a7d53a26db5ea8c7d5` (= JPR2's / AT's / SF's). **No build, no worktree, no deployment**:
every number below is reduced from ntuples that already existed (`at_ref/runs/BASE_jet.root`, jets
TUNE 0-499, aligned to the input tracking ntuple through the `evt` -> `event` branch, which makes a
`-s 16` run usable and is why no re-run was needed).

**My purity instrument reproduces the ntuple's own `tc_isFake` on 52,008 of 52,008 chain-backed TC
rows, 0 disagreements**, over 500 jet events (`pur_ref/gtrim2.py`, the exact `matchedSimTrkIdxsAndFracs`
rule: denominator = distinct (hitrow, hittype), numerator = distinct hits whose sim set contains s,
bar `> 0.75` STRICT). The only disagreements in the whole file are 9,295 **bare-pLS (type 8)** rows,
and they are an ntuple artefact, not an error: `tc_hitIdx` for a type-8 row is written from the TC's
layer slots while `tc_isFake` is computed from `getHitIdxsFrompLS`. Type 8 is not in my population.

### 1. WHO owns the stage-g sims: **100% chain TCs**, and it is a 6-layer problem more than a 5-layer one
On JPR2's own 450-event corpus (`pur_ref/g_census.py`, stage 11 = leg g, 499 sims, his number):

| | share | note |
|---|---:|---|
| owner is a CHAIN TC | **1.0000** (499/499) | nothing here is LST-carried; "no changes that help LST too" is satisfied by construction |
| owner is FAKE | .9920 | D's .987, reconfirmed |
| type 4 (bare T5-class chain) | .723 | nOT p50 12 |
| type 7 (attach-confirmed, pT5) | .244 | 12 OT hits + 3-4 PIXEL hits in the purity denominator |
| type 9 (T4-class) | .032 | **cannot be repaired at all** (4 layers -> 3 does not emit) |
| owner has 12 OT hits (6 layers) | .701 | trim leaves **5** layers: attach eligibility and T5 class SURVIVE |
| owner has 10 OT hits (5 layers) | .267 | trim leaves 4 -> **T4 class, attach-ineligible** |
| owner has 8 OT hits (4 layers) | .032 | dead |

**For agent T4:** on the addressable cell (below) the split is **396 six-layer / 169 five-layer**, so a
purity repair would push **~169 five-layer chains into your 4-layer class per 500 jet events** and
NONE the other way. That is +0.34 T4 TC/evt against your 2.07 fake T4/evt, i.e. the interaction is
real but small, and it is the 5-layer subset only — 70% of the population never leaves the T5 class.

### 2. The ceiling, re-measured TC-centrically: **+.0258** (D +.0244, JPR2 +.0253)
`pur_ref/gtrim.py` over 500 tune events, 21,919 core sims, eff .7659 (= P4's row). Of 7,271 fake
chain TCs (14.5/evt), **4,485 have >= 5 OT layers and are therefore trimmable at all**. Dropping the
single least-owned mini-doublet from each (the ORACLE) newly matches **565 core sims = +.0258**, at
50 new core duplicates and 638 newly matched non-core sims (1,253 fake TCs removed).
**The oracle's target is TERMINAL only .660 of the time** (inner .312 / outer .349), so a third of
the leg is not reachable by any terminal trim, which is what the existing K6f is.

### 3. THE DECISIVE NUMBER: the per-mini-doublet discriminator's power, measured
Per-unit AUC for "this mini-doublet contains a hit that does not belong to the TC's own best sim",
244,206 units over 44,433 chain TCs with nOT >= 5 (`pur_ref/curve.py`):

| truth-free signal | AUC | as an argmax rule, d core eff |
|---|---:|---:|
| leave-one-out combined chi2 ratio (the K6f criterion) | **.4938** | **-.0128** |
| \|xy circle residual\| | .5962 | -.0134 |
| \|rz line residual\| | .5172 | -.0174 |
| terminal-ness (position only) | .6643 | -.0013 |
| **hit ownership: another emitted TC carries this MD** | **.7892** | **+.0019** |
| ... both hits shared | .7896 | +.0015 |
| ... shared by an EARLIER-accepted chain TC only | .7513 | +.0019 |

**The fit residual carries NOTHING, and the K6f chi2 criterion is on the wrong side of a coin flip.**
The physics is the reason and it is not a defect: at dR < .05 the thief and the victim are collinear
at the scale of the hit resolution, so the stolen mini-doublet lies on the fitted circle as well as
the track's own hits do. Any geometric criterion is structurally blind here.

Hit ownership is the only live signal and its rates say why it cannot carry the leg alone:
**P(some other TC shares this MD | FOREIGN) = .388 vs .034 | CLEAN**, precision **.531** at recall
.388; requiring BOTH hits shared gives precision .497 at recall .134.

### 4. The exchange curve every future attempt should be judged against
A synthetic classifier `score = foreign + sigma * N(0,1)`, swept, scored through the SAME per-sim
aggregation (a sim is gained only if nothing else matched it, lost only if nothing else still does):

| per-MD AUC | GAIN core | LOSE core | NET | **d core eff** |
|---:|---:|---:|---:|---:|
| 1.000 | 476 | 36 | +440 | **+.0201** |
| .921 | 450 | 123 | +327 | +.0149 |
| .825 | 393 | 220 | +173 | +.0079 |
| **.760** | 353 | 298 | **+55** | **+.0025** |
| .679 | 287 | 359 | -72 | -.0033 |
| .500 | 201 | 511 | -310 | -.0141 |

**BREAK-EVEN IS AT PER-MINI-DOUBLET AUC ~ .74.** That is the bar. The measured best signal is .789,
which is why the best real rule lands at +.005 and not at +.026.

### 5. The honest rule table (truth-free predicate AND truth-free target, both)
Firing is unconditional on truth — a kernel does not know which TC is fake. 44,433 chain TCs, nOT>=5:

| rule | fires | GAIN core | LOSE core | NET | d core eff | gain non-core | lose non-core | d dup core |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| drop outermost, always | 44433 | 264 | 493 | **-229** | -.0104 | 142 | 751 | -3 |
| drop innermost, always | 44433 | 302 | 330 | -28 | -.0013 | 492 | 232 | +3 |
| drop worse-terminal by LOO chi2 | 44433 | 285 | 429 | -144 | -.0066 | 236 | 571 | -2 |
| **shar >= 1, any position** | 11787 | 261 | 150 | **+111** | **+.0051** | 340 | 89 | +3 |
| **both hits shared, any position** | 5242 | 133 | 28 | **+105** | **+.0048** | 199 | 20 | +1 |
| shar >= 1, terminal only | 7042 | 179 | 77 | +102 | +.0047 | 261 | 29 | +4 |
| both hits shared, terminal only | 2898 | 89 | 11 | +78 | +.0036 | 152 | 5 | +1 |
| shar>=1 terminal & chi2 > 1 (K6f guard) | 1609 | 41 | 18 | +23 | +.0010 | 54 | 6 | +0 |
| ORACLE, any position | 44433 | 609 | 39 | +570 | +.0260 | 636 | 16 | +6 |

Three things ride with that table:
* **`both hits shared` is the risk-cheapest point**: net +105 for only 28 destroyed core matches
  (4.8:1), against `shar>=1`'s 261/150 (1.7:1) for net +111. If anything here is ever built, it is
  that point, not the higher-recall one.
* **The K6f concentrating guard (`chi2Full > trimAbsChi2`) throws away 80% of what is left** (+.0047
  -> +.0010): the chains that need repairing are exactly the ones that already FIT WELL, which is the
  same collinearity fact as section 3. So "relax `trimMinLayersAfter` 5 -> 4" is NOT the cheap
  config-only version of this arm; the guard it would run behind is anti-selecting the population.
* the gained sims are **not** concentrated in the deep core: `shar>=1 any` gains 24/86/74/41/36 across
  dR <.005 / [.005,.02) / [.02,.05) / [.05,.1) / >=.1.

### 6. What is still running before I take a position
PU200 tune (1000 events, same instrument, `--allsims`) for the LOSS side where there is no budget, and
one `--allobj` jets run to price the last untried signal with a mechanism (the member-T3 fake/prompt
scores, i.e. "is the terminal T3 the stitch"). **I am not building anything until those land**, and on
the numbers above my prior is a refusal with this exchange table as the deliverable.

Artifacts: `pur_ref/{g_census,gtrim,gtrim2,score,curve,rep}.py`, `pur_ref/G4_*` (244k unit rows),
`pur_ref/rep_BASE.txt`. Nothing built, nothing deployed, nothing committed.
-- PUR

## [WELD 00:46] THE ARGMAX DOES NOT LOSE TO A BETTER EDGE -- IT LOSES TO AN EDGE THAT NEVER WELDS EITHER, AND THE FIX IS TO WELD **LESS**: **.7017 of the slots a stage-d true edge fails to win are STILL EMPTY when the weld finishes**, because K6a/K6b is an iterated mutual-best (greedy-matching) fixed-point iteration stopped 5+ rounds early -- and **converging it is monotonically NEGATIVE** (16 sweeps: core eff -.0164, dR<.02 -.0329, fake +.0108), while **going the OTHER way to 2 sweeps is a Pareto win on jets: core eff +.0036 (p=9.9e-04, paired McNemar), dR<.02 +.0095 (p=.0024), fake -.0046 (p=4.8e-12), all 8 dR aggregates >= 0, sim pt 100-300 GeV +.0192 -- for ONE CONSTANT and a THIRD LESS WORK in the weld.**

Caveats riding WITH the headline, all four of them:
* **jet-core (dR<.05) DUPLICATE gets WORSE at 2 sweeps: .0313 -> .0345, +.0032, p=.017** (0.47 ->
  0.52 dup TCs/evt) in the cell where we already owe master 0.30/evt. **1 sweep instead removes dup
  (all: .0246 -> .0230, p=.003; core flat -.0005) and removes 3x more fake (-.0114), but its
  core-all efficiency gain is NOT significant (+.0021, p=.218) and it REGRESSES dR>.05 by -.0035
  (p=.033).** Both points are on the table; 2 sweeps is the efficiency-priority choice.
* **sim pt > 300 GeV loses .0210 at 2 sweeps** (denom 761, -16 sims) while 100-300 gains +.0192
  (denom 2036, +39 sims). We are +.1945 ahead of master above 300 GeV, so this spends part of a lead.
* PU200, cube50 and cube50_highPt are RUNNING, not done. **No ship claim until they land.**
* Jets TUNE 0-499 only; 500-999 and PU200 `event_2000` never opened.

### 0. Provenance and the inertness proof
Worktree **`w1`** = `/mnt/data1/gsn27/here/gpu_wt/w1` at `04c6e122e68` (fresh, nobody else's).
`LST_CHAIN_WELD_SWEEPS` overrides `kChainWeldSweeps`, which occurs in **exactly one place** in the
whole tree (`LSTEvent.dev.cc:1448`, the K6a/K6b loop bound) and sizes no buffer, so unset == frozen.
Build `-mcC`, **0 `error:`** in the fresh make log, binary `338ef61cea7464367634faaa884fc1f3`, lib
`f6cb563a1cfbe0d98d66266c036a1f1b`, frozen at `weld_ref/swbin/` so a later rebuild cannot move a
finished arm. **INERTNESS PROVEN, NOT ASSERTED: arm BASE (w1, variable unset) is bit-identical to
the PRISTINE main-tree ship binary `5293c48f2561a6968efecf4fd4b1270f` on all 23 `m3_ref/jetphys.py`
fields over the same 500 jet events** (`weld_ref/runs/{BASE,PRIS}_jet.json`, dict equality).

### 1. THE DIAGNOSIS -- and it is not the one both prior rounds assumed

Pass 1 (`weld_ref/beat1.py`, JPR2's corpus, free): of 23,669 eligible-but-unwelded adjacent same-sim
pairs on 599 stage-d sims -- **39.5 pairs per sim, not one of them welded** -- .9941 have a strictly
higher-scoring eligible competitor on a slot, .6804 on BOTH slots (monotone in pt: .222 below 10 GeV
-> .730 at 100-300), median **222** eligible competitors per slot, median losing margin 2.89.

Pass 2 (200 fresh jet tune events, `weld_ref/{wreduce,wagg}.py`; exact weld slots recovered from the
chains dump's pre-trim node runs, 0 welded pairs unmapped; replay max |dmX| 7.9e-05) says the
competitor **wins the slot and then does not use it**:

| stage-d slots, both sides pooled | ALL | 20-50 | 50-100 | 100-300 | >300 |
|---|---:|---:|---:|---:|---:|
| **slot is EMPTY when the weld ends** | **.7017** | .6513 | .7321 | .7109 | .6795 |
| taken by an edge into a T3 of ANOTHER sim | .0069 | .0034 | .0076 | .0062 | .0121 |
| taken by an edge into a FAKE T3 | .2914 | .3453 | .2604 | .2829 | .3084 |
| taken by an edge into a T3 of OUR sim | **.0000** | .0000 | .0000 | .0000 | .0000 |

and of the taken slots, the occupant's chain is **gate-killed .7855** and delivered **.0359**.

**The mechanism, from the kernel.** K6a lets `e = (n,m)` compete only when `outWeld[n] == -1` AND
`inWeld[m] == -1` (`ChainWeld.h:104`). So a node's argmax NEVER advances past its own rank-1 edge on
its own: if that edge is not also rank-1 at its head, it wins `bestOut[n]` again in every later
sweep and n welds nothing. Progress happens only when some OTHER weld removes the blocker. That
makes K6a/K6b an iterated mutual-best -- whose fixed point is exactly the greedy matching by key,
at which **no slot with any eligible edge can be empty**. `FINDINGS_JET.md:363-368`'s "the weld
consumes at most `kChainWeldSweeps` edges per node-slot, so at most 3 ranks deep" is **wrong**: the
depth is not 3, it is however far the blocking cascade happens to unwind. My local replay
(`weld_ref/wsweep.py`, reproduces 95.5% of the 83,327 tree-welded local edges at N=3) puts the free
stage-d slot fraction at **.328 at N=3, .053 at N=8, .0027 at N=20** -- i.e. the shipped weld runs
about a quarter of its own iteration.

The true edge's RANK, on the deployed packed key, among the eligible edges at its slot
(`wagg.py`, 9,808 stage-d pairs / 274 sims):

| | ALL | 20-50 | 50-100 | 100-300 | >300 |
|---|---:|---:|---:|---:|---:|
| median eligible edges at the slot | 195 | 218 | 216 | 202 | 110 |
| max(rank) p25 / **p50** / p90 | 19 / **63** / 249 | 14/44/241 | 20/75/256 | 22/73/244 | 17/41/254 |
| P(max rank <= 3) per PAIR | .0324 | .0359 | .0359 | .0267 | .0316 |
| **per SIM, best pair's max(rank): p50** | **4** | 5 | 3 | 4 | 4 |
| **P(best pair <= rank 3)** | **.4854** | .360 | .524 | .495 | .447 |
| P(best pair == rank 1) | **.0000** | | | | |

`P(best == 1) = .0000` is the instrument validating itself: rank 1 at both slots welds in sweep 1 by
construction, so no stage-d sim can have one.

### 2. THE DEPLOYED SWEEP CURVE -- the truncation is load-bearing REGULARISATION

The weld is monotone in the sweep count (sweeps 1-3 produce the same welds whatever N is), so every
arm's weld set is a superset of the arm below it, and the whole curve is a clean marginal-value
measurement of the extra welds. Jets TUNE 500 events, one binary, env-toggled, identical denominators
(21,919 core sims / 49,459 all):

| sweeps | eff core | eff all | eff dR<.02 | fake | dup | TC/evt | T4 core wins |
|---|---:|---:|---:|---:|---:|---:|---:|
| **1** | .76792 | .81031 | **.5183** | **.12029** | **.02296** | 121.2 | 2419 |
| **2** | **.76947** | **.81099** | .5169 | .12712 | .02431 | 122.9 | 1239 |
| **3 = SHIPPED** | .76587 | .80909 | .5074 | .13170 | .02463 | 123.1 | 1016 |
| 4 | .76468 | .80847 | .5070 | .13464 | .02516 | 123.4 | 900 |
| 5 | .76244 | .80736 | .5027 | .13618 | .02519 | 123.4 | 823 |
| 6 | .75957 | .80588 | .4971 | .13848 | .02518 | 123.4 | 764 |
| 8 | .75437 | .80349 | .4847 | .14111 | .02473 | 123.5 | 666 |
| 16 | .74944 | .80117 | .4745 | .14250 | .02492 | 123.4 | 532 |

**Every marginal weld past sweep 2 is net NEGATIVE, and monotonically so on efficiency, on deep-core
efficiency and on fake at once.** Because existing welds are never revoked (weld-set nesting checked
on 6 events: 0 welds ever LOST going up in sweeps), the loss cannot be "a good weld displaced": it
is the added welds themselves. Visible in `core_by_type`, where T4 core wins collapse 2419 -> 532 as
T5 rises 6556 -> 6711, i.e. correct 4-layer chains are displaced by wrong 5-layer ones. **This retires an entire class of levers with a number: on this
graph and this key, anything that adds welds is negative, so the weld argmax cannot be attacked by
giving the true edge more chances.** Stage d's +.0304 is not reachable that way -- .8230 of stage-d
sims have every eligible pair strictly out-ranked, at median rank 63 of 195.

### 3. THE CANDIDATE: `kChainWeldSweeps: 3 -> 2` (paired, jets TUNE, `weld_ref/sweepgate.txt`)

| band | BASE | S02 | delta | p (McNemar) |
|---|---:|---:|---:|---:|
| **core-all** | .7659 | **.7695** | **+.0036** | **9.9e-04** |
| <.005 | .4108 | .4164 | +.0056 | .41 |
| [.005,.01) | .4773 | .4931 | +.0158 | .011 |
| [.01,.02) | .5932 | .6013 | +.0080 | .090 |
| **<.02** | .5074 | **.5169** | **+.0095** | **.0024** |
| [.02,.05) | .7459 | .7483 | +.0024 | .42 |
| <.05 | .6091 | .6156 | +.0065 | .0025 |
| >.05 | .8902 | .8915 | +.0013 | .21 |
| fake all | .1317 | .1271 | **-.0046** | 4.8e-12 |
| dup all | .0246 | .0243 | -.0003 | .38 |
| **dup <.05** | .0313 | **.0345** | **+.0032** | **.017** |

**All eight dR aggregates are >= 0** (the only negative fine bins are [.0175,.02) -.0020 p=1 and
[.04,.05) -.0043 p=.47). **SOFTER JETS PASS** (`jpr2_ref/jetptaxis.py`, same file, free):
genjet 50-200 .9277 -> .9270 (-3 sims of 4067), **200-500 .9164 -> .9211**, 500-1000 .8761 -> .8778,
1000-2000 .7721 -> .7770, >2000 .7527 -> .7534 -- so it is not a TeV-tuned lever.
Sim pt: **100-300 +.0192**, 50-100 +.0069, 20-50 +.0053, 10-20 +.0027, **>300 -.0210** (-16 sims).

**Why this should be near-inert off-distribution, mechanically and not by construction:** the
sweep count only binds where the mutual-best iteration has NOT converged. PU200 core nodes carry a
median 12 eligible incident edges against the jet core's 222, so sweeps 2-3 have far less to do
there, and on the guns less still. **That is a prediction, not a result -- PU200 and both cubes are
in flight and I will report them whatever they say.** No ship claim before they land.

### 4. Still open on my side (running / next)
* The full gate set for S01 and S02 (PU200 35 fields + displaced bands, cube50, cube50_highPt 0-4999).
* The re-KEY arm, which is the only remaining re-ranking lever and adds NO welds: `weldKey`, a
  per-edge ordering scalar separate from the summed `logOdds`, built in `w1` (mode 0 = max(mP,mD) =
  today, bit for bit). Offline screen on the 200-event corpus (`weld_ref/wkey.py`, validated: the
  CURRENT key recovers exactly 0 stage-d sims, as the sufficient condition demands): **min(mP,mD)
  recovers .2226 of stage-d sims at sweep 1 and mP .1934, versus .0000 today** -- the winner's-curse
  reading, that an argmax over ~200 competitors should not order on the OPTIMISTIC max of the two
  class margins -- but it also displaces ~15% of the currently welded local edges, and section 2
  just showed how sensitive the weld is to that. Screen only; deployment decides.

**For agent T4:** the sweep count is a large, free handle on your class. T4 core wins go
1016 (shipped) -> 1239 (2 sweeps) -> **2419 (1 sweep)** with jet fake going the RIGHT way
(.1317 -> .1271 -> .1203) -- so "more T4" and "less fake" are not opposed along this axis, and any
T4 policy you price should be priced at the sweep count we end up shipping. **For agent PUR:** the
harm from extra sweeps is chains grown past their correct endpoint, i.e. it manufactures exactly
your stage-g population; the K6f terminal trim is single-ended and demonstrably cannot keep up.
-- WELD

## [PUR 02:05] THE ARM CLOSES WITH A MECHANISM, NOT A THRESHOLD: **65.0% of the mini-doublets that must be removed are carried by NOTHING ELSE IN THE EVENT (87.9% at MD granularity), so the +.0278 ceiling is 82% unreachable no matter how good a classifier gets — and where the evidence DOES exist the argmax is right 74 of 74 times.** The limit is EVIDENCE AVAILABILITY, not discriminator quality. Attached anyway: a priced candidate that clears EVERY hard gate — **jets core +.0041 to +.0051, PU200 +22 sims with ZERO discordant sims in all four dxy and all three vxy bands, PU200 duplicates BETTER (-13 core / -143 pooled), BOTH CUBES fire ZERO times (bit-identical by mechanism)** — whose price is a new kernel for **+.005**, i.e. below the bar the maintainer set when declining a 4th weight file for +.02. **Caveat riding with the whole post: this is an OFFLINE PROXY, nothing was built or deployed.**

### 1. Why the ceiling is unreachable (`pur_ref/reach.py`, jets tune 0-499)
Take the 609 delivered chain TCs where removing ONE mini-doublet newly matches a previously
unmatched core sim (**ceiling +.0278**), and ask what truth-free evidence exists on the unit that
has to go. (Reconciliation of the two ceiling numbers in my two posts: **+.0258** is the ORACLE RULE
"always drop the least-owned mini-doublet", which pays 39 destroyed matches for its 609 gains;
**+.0278** = 609/21,919 is what a bar-aware oracle that fires only when it helps would deliver, i.e.
the addressability ceiling. Both are the same 609 sims; the .0020 is the oracle rule's own loss side.
JPR2's stage-g row is +.0253 and D's is +.0244 on the older corpus.)

| evidence on the unit that must be removed | present | share |
|---|---:|---:|
| terminal position (inner or outer) | 423 | .6946 |
| **some other TC carries a hit of it (`shar >= 1`)** | **213** | **.3498** |
| both of its hits are carried elsewhere | 88 | .1445 |
| **one other TC carries the WHOLE mini-doublet (`sharMD >= 1`)** | **74** | **.1215** |
| `chi2Full > trimAbsChi2` (the K6f concentrating guard would even look) | 161 | .2644 |
| it has the largest \|xy residual\| of its own TC | **0** | **.0000** |

**`shar == 0` for .6502 of them and `sharMD == 0` for .8785.** The thief stole a mini-doublet that no
other reconstructed object claims, and the geometry cannot see it (section 3 of [PUR 01:20]: LOO chi2
AUC .494). There is no observable left. **And where evidence does exist it is already sufficient: of
the 74 units with `sharMD >= 1`, the unit that must be removed is the argmax of `sharMD` in its own
TC 74 of 74 times (1.0000).** A better classifier buys nothing; there is nothing left to classify.

That also kills the cheap version of this arm for the record: **only 26.4% of the addressable
population even passes `chi2Full > 1`**, so relaxing `trimMinLayersAfter` 5 -> 4 (or `trimFactor`)
cannot reach stage g — the K6f guard anti-selects it, because a stitched jet-core chain FITS WELL.

### 2. The candidate, if the maintainer wants it anyway: **PUR-SB**
> In `ChainEmitTCs` (K10), for an emitted chain row with **>= 5 outer-tracker mini-doublets**, if some
> member mini-doublet is also carried by **another emitted chain TC**, skip the one with the highest
> such count when filling the layer slots. Nothing else changes.

**The proxy is EXACT for this shape, which is unusual in this program and is why I am quoting it.**
Every quantity the standalone metric measures (`tc_isFake`, `tc_simIdx`, `sim_tcIdx`,
`tc_isDuplicate`, `tc_nhitOT`, `tc_nlayers`) is derived from `getHitIdxsAndHitTypesFromTC`, i.e. from
the TC row's layer slots (`parseChainTC` -> `write_lst_ntuple.cc:1752`), so removing a unit from the
slot fill is precisely what my instrument computes. The instrument reproduces the ntuple's own
`tc_isFake` on **1,226,917 of 1,226,917 chain-backed TC rows across jets + PU200 + both cubes, 0
disagreements**.

| gate | JETS tune 0-499 (21,919 core sims) | PU200 tune event_1000 (72,481 sims) | cube50 (10k evt) | cube50_highPt (5k evt) |
|---|---|---|---|---|
| rule FIRES on | 4,564 of 44,433 chain TCs | 16,289 of 977,099 | **0 of 1,329** | **0 of 519** |
| efficiency sims gained / lost | **113 / 24 = +89 (+.0041)** | **24 / 2 = +22 (+.00030)** | 0 / 0 | 0 / 0 |
| dxy[0,1) / [1,5) / [5,10) / [10,30) | 113/24, 0/0, 0/0, 0/0 | 24/2, **0/0, 0/0, 0/0** | inert | inert |
| vxy[1,5) / [5,10) / [10,30) | 3/1, 0/0, 0/0 | 2/0, **0/0, 0/0** | inert | inert |
| non-core sims gained / lost (= fake TCs converted) | 169 / 18 | 604 / 41 | 0 / 0 | 0 / 0 |
| d duplicates, in-acceptance | **+0** | **-13** | 0 | 0 |
| d duplicates, pooled | -0 | **-143** | 0 | 0 |

(`sharMD >= 1` above; the hit-level `shar >= 1` variant is jets **+111 (+.0051)** / PU200 +13, and the
`sharboth` variant jets +105 / PU200 +23 — all three have the same all-zero displaced-band profile.)

**Both cubes are inert BY MECHANISM, not by tuning**: a displaced single-muon gun has no second track
to share a mini-doublet with, so `sharMD` is identically 0 there and **cube50_highPt dxy[1,5) = .1209
is untouched, bit-identically**. That is the same structural argument that protected D's shipped
bare-pLS rule, and it doubles as the anti-sample-recognition check (sparse-region behaviour identical
to base, by construction, no conditioning variable involved).

### 3. What the candidate costs, stated as a cost and not as fine print
* **A new kernel.** `ccClaimed[md]` is written INSIDE `ChainEmitTCs` today, so the count that the trim
  reads does not exist until every row is emitted. A build needs K10 split into emit-slots then
  count-and-trim, i.e. one extra launch over the accepted chains plus a `uint8` per-MD counter. Kernel
  count is a real cost in this project ([[simplicity-over-cleverness]]) and this buys **+.005**.
* **A design decision that propagates if you get it wrong:** whether `ccClaimed` (the -CC pre-claim
  map, which feeds the pLS retirement sweep) reflects pre- or post-trim mini-doublets. My proxy
  assumes **PRE-trim / unchanged**, which is what keeps the arm a pure repair. Post-trim would leak
  into the CC sweep and my numbers would not describe it.
* **T4 pressure, for agent T4:** 188 of the 609 addressable TCs (30.9%) are 5-layer, so a repaired
  one becomes a **4-layer T4 row**; 421 are 6-layer and stay T5. At the delivered rate that is about
  +0.06 T4 TC/evt on jets, small against your 2.07 fake T4/evt, and every one of them is a NEWLY
  CORRECT track rather than a fake.
* **It is an offline proxy.** Exact for the shape above, and validated against `tc_isFake` on 1.23M
  rows — but this project's proxies have mispredicted deployment 4+ times and **deployment is the only
  referee**. Nothing here has been deployed.

### 4. My position
**Refusal, with the exchange table as the deliverable.** Stage g is a real +.0278 of jet-core
efficiency and it is 100% ours, but **82% of it is invisible to any truth-free observable that exists
in this kernel**, for a physics reason (collinearity inside the jet core) that no retrain or threshold
touches. The reachable 18% is clean enough to ship on every gate and small enough that I am not
recommending a new kernel for it. If the maintainer wants the +.005 anyway, section 2 is the whole
specification and section 3 is the whole bill.

**For the round:** stage g should be re-filed from "fifth-largest and rising, a near miss" to
**"+.0278 ceiling with a measured .0050 reachable, mechanism understood, closed"**, and the funnel's
remaining money is in f / e / d where the missing information still exists somewhere in the event.

Artifacts (`pur_ref/`): `gtrim2.py` (the instrument), `score2.py` (vectorised rule scorer, agrees with
`score.py` to the sim), `curve.py` (the AUC -> efficiency exchange curve), `reach.py` (the
evidence-availability decomposition), `g_census.py`, `rep.py`; corpora `J9_*` (jets, 244k unit rows),
`P9_*` (PU200, 5.3M), `C9_*` / `H9_*` (cubes); `score_{J9,P9}.txt`. **No build, no worktree, no
deployment, no commit, nothing published.**
-- PUR

## [PUR 03:10] I HAVE TO CORRECT MY OWN CLOSING VERDICT: **there IS a per-mini-doublet discriminator with real power and it is FREE — the T3-DNN fake score of the chain's TERMINAL member node, `triplets.fakeScore()`, which `ChainEmitTCs` already has in its signature and already loops over. AUC .807, and thresholded at .5 on the two terminal mini-doublets it delivers +.0100 core efficiency at 48 gained / 4 lost (12:1) — twice the hit-ownership family and ~38% of the +.0267 ceiling, with NO new kernel, NO counter and NO propagation.** Caveats WITH the headline, and they are large: **this is 100 jet events only** (the same 100 reproduce ORACLE +.0267 and `shar>=1 any` +.0052 against the 500-event +.0260 / +.0051, so the corpus scales, but a 48/4 cell is 52 tracks); **PU200 and the cubes are NOT yet measured for this rule** and the T3 score is NOT structurally zero on a displaced gun the way `sharMD` is, so the cube inertness argument of [PUR 02:05] DOES NOT TRANSFER; 500-event jets and 200-event PU200 `--allobj` runs are in flight. **SUPERSEDED IN PART BY
[PUR 04:15] BELOW: the 500-event number held (+.00949) but the rule FAILS BOTH CUBE GATES — read that
post, not this one, for the verdict.**

### What I got wrong at [PUR 02:05]
I concluded "the limit is evidence availability, not discriminator quality" from the fact that .6502 of
the addressable mini-doublets are claimed by no other TC. That statement is true of OWNERSHIP evidence
and it does not generalise: **the T3-DNN already scores the three-mini-doublet object that contains the
stolen hit**, and a stitched terminal T3 is exactly what that network was trained to call fake. My
[PUR 01:20] statement that "the geometry is blind" is also too broad — the RAW residual is blind
(AUC .494), but a TRAINED function of the same geometry is not (AUC .807). The honest version:
**a linear read of the fit is blind; the T3-DNN is not.** The T3 columns needed an `--allobj` run,
which is why this arrived last.

### The measurement (jets tune events 0-499 -> first 100 only, `pur_ref/T3S_*`, 47,284 units)
Same instrument, same per-sim aggregation, `--allobj` run md5-verified against the pristine ship binary.
Reference rows on the SAME 100 events so the comparison is like-for-like:

| rule | fires | AUC | GAIN | LOSE | NET | d core eff |
|---|---:|---:|---:|---:|---:|---:|
| ORACLE any (the ceiling) | 8611 | .9988 | 127 | 9 | +118 | +.02672 |
| `shar>=1` any (best of [PUR 02:05]) | 2279 | .8057 | 57 | 34 | +23 | +.00521 |
| `sharMD>=1` any | 825 | .9466 | 21 | 4 | +17 | +.00385 |
| any, max \|xy resid\| | 8611 | .5863 | 41 | 121 | -80 | -.01812 |
| **t3 fakeScore, worse terminal** | 8521 | **.8074** | 92 | 55 | +37 | **+.00838** |
| t3 promptScore (low), worse terminal | 8521 | .8082 | 97 | 60 | +37 | +.00838 |
| **t3 fakeScore > .5, worse terminal** | **929** | .6752 | **48** | **4** | **+44** | **+.00996** |
| t3 fakeScore, worse terminal, AND `shar>=1` | 1364 | .6720 | 39 | 15 | +24 | +.00543 |
| t3 fakeScore, max over ALL positions | 8521 | .7805 | 51 | 91 | -40 | -.00906 |

Two structural facts in that table:
* **The threshold is what makes it safe, not the ranking.** Unthresholded it is 92/55; at
  `fakeScore > 0.5` it is 48/4 while KEEPING more gain than any ownership rule. That is the shape a
  candidate should have.
* **It must be TERMINAL-restricted.** The same score applied at any position is -.00906: an interior
  mini-doublet sits in three member T3s and dropping it breaks the chain's own geometry.

### The in-kernel form, and it is the cheapest thing in this round
> In `ChainEmitTCs` (K10): for an emitted chain row with **>= 5 outer-tracker mini-doublets**, read
> `triplets.fakeScore()` of the **innermost and outermost member nodes** (both already reachable —
> the kernel walks `items.nodeItems()[off + k]` for the pt median two lines above); if the worse of the
> two exceeds `0.5`, skip that terminal mini-doublet when filling the layer slots.

No new kernel, no new launch, no new array, no per-MD counter, no `ccClaimed` interaction, no weight
file (the T3-DNN is already there and is LST's own, not ours to add). It is ~10 lines and one
`ChainConfig` float defaulting to OFF. **This supersedes the `sharMD` candidate of [PUR 02:05] on
every axis except the cube-inertness proof, which it does not have.**

### What is NOT done, stated as a gap and not as a caveat
1. **500-event jets** (in flight) — the 48/4 cell must survive the other 400 events.
2. **PU200** (200-event `--allobj` in flight). The T3 fake score is a per-T3 quantity with a jet/PU200
   prior shift, and the rule fires on 929 of 8,611 chain TCs on jets; on PU200 it will fire on ~1.2M
   chain rows. **The loss side there is prompt efficiency and there is no budget.** This is the gate
   that decides the arm.
3. **Both cubes.** `sharMD` was inert on a displaced gun BY MECHANISM; `t3 fakeScore > .5` is NOT, so
   `cube50_highPt dxy[1,5) = .1209` is at genuine risk and must be measured, not argued.
4. Still an offline proxy. Exact for a K10-slot-skip (the reasoning in [PUR 02:05] section 2 applies
   unchanged), but never deployed.

**My position moves from "refusal" to "one candidate worth finishing, gates pending."** If the PU200
and cube gates come back clean, `t3fk>.5 term` is a ~+.010 jet-core repair for ten lines and no new
object, which is a better price than anything else I priced this round; if they come back dirty, the
refusal of [PUR 02:05] stands and this paragraph is the reason it was worth checking.
-- PUR

## [WELD 01:15] CANDIDATE HANDED OVER, ALL HARD GATES GREEN, ONE NAMED DEBIT: **`kChainWeldSweeps: 3 -> 2`, ONE CONSTANT, a THIRD LESS WELD WORK -- jets tune core eff .7659 -> .7695 (p = 9.9e-04 paired), dR<.02 .5074 -> .5169 (p = .0024), sim pt 100-300 GeV +.0192, fake .1317 -> .1271; PU200 all SEVEN displaced bands neutral-or-better (three significantly BETTER) with fake and dup down everywhere; cube50_highPt EXACTLY .00000 on every efficiency and fake band including round 2's dxy[1,5) = .12089; cube50 exactly .00000 on all but two tiny bands (-.00093 and -.00019). The debit: jet-core (dR<.05) DUPLICATE .0313 -> .0345 (p = .017, 0.47 -> 0.52 TC/evt) in a cell we already owe master 0.30/evt.** The re-key arm is REFUSED with an exchange-rate table.

Patch `weld_ref/pat/weld_S02.patch`, md5 **baa19ab0654220cc445f2287bb876dd1**, 29 lines, ONE file,
`git apply --check` CLEAN against the main tree (== `04c6e122e68` for `LSTCore/{src,interface}`).
**Verified end to end, not asserted:** a clean worktree `w2` at `04c6e122e68` + this patch and nothing
else (no env knob, no `weldKey` column) is **bit-identical to the env-knobbed S02 arm on all 23
`m3_ref/jetphys.py` fields over the same 500 jet events** (`weld_ref/runs/{S02,P02}_jet.json`, dict
equality). The same check on the 35 PU200 fields is still RUNNING (`P02_pu`) and I will post it
whatever it says; the S02 PU200 numbers below come from the env-knobbed binary, whose knob-unset arm
is itself bit-identical to the pristine ship binary on all 35 fields.
Binary `dcdd6dec417454034ec75bf94b509228`, lib `82fbf9df079a7569e71576ca79a5e490`, 0 `error:` in the
fresh make log. Arms `weld_ref/runs/{BASE,S01,S02,P02,...}_{jet,pu,cube50,cubehi5}.*`, all with
`RUN_EXIT=0` and a printed library-provenance check.

Everything above rests on the diagnosis in [WELD 00:46] plus two things measured since. Jets TUNE
0-499 and PU200 `event_1000` only; **jets 500-999 and PU200 `event_2000` never opened** -- the
holdout is the coordinator's.

### 1. THE MECHANISM, now measured directly rather than inferred (and my 00:46 wording corrected)

The marginal weld's TRUTH QUALITY collapses sweep by sweep (`weld_ref/wqual.py`, 200-event local
corpus, label = both endpoints the same sim; the sim's per-sweep alignment cross-checks against the
tree at .787/.636/.431 tree-welded for sweeps 1/2/3 and ~.001 for sweeps >= 4, which is exactly what
a 3-sweep tree should give):

| welds produced BY sweep | 1 | 2 | **3** | 4 | 5 | 8 | 12 |
|---|---:|---:|---:|---:|---:|---:|---:|
| count | 58881 | 36499 | 18417 | 10093 | 5859 | 1489 | 244 |
| **same-sim purity** | **.5487** | **.3126** | **.1208** | .0529 | .0306 | .0148 | .0041 |

**The third sweep's welds are 88% wrong.** That is the whole result: sweeps 1 and 2 are mutual-best
over nearly-full incident sets, sweep k is mutual-best only among k-1 rounds of leftovers, so the
weld is not being truncated too early, it is being run one round too long.

**Correction to my own 00:46 post:** I attributed the harm to chains being EXTENDED past their
correct endpoint. The chain census on 6 events (`weld_ref/{chcmp.sh,chagg.py}`) says the dominant
effect is chain CREATION, not extension -- chains 38,349 -> 61,328 -> 78,686 -> 130,944 at 1/2/3/8
sweeps while nodes per chain move only 2.055 -> 2.087 -> 2.106 -> 2.157, and the gate-killed
fraction rises .871 -> .897 -> .913 -> .945. Extension is real but secondary (2-node chains .944 ->
.891 -> .835; 5-layer chains .194 -> .238 -> .293), and it is the part that displaces a correct
4-layer chain with a wrong 5-layer one -- which is what `core_by_type` shows (T4 core wins 2419 at 1
sweep -> 1016 at 3 -> 532 at 16). **Weld-set nesting verified: 0 welds are ever LOST going up in
sweeps, on all 6 events**, so the whole sweep curve is a clean marginal-value measurement of added
welds and no result here can be "a good weld displaced by re-ranking".

### 2. THE FULL GATE TABLE for `kChainWeldSweeps = 2`

**Jets TUNE, 500 events, paired McNemar on identical sims** (`p4_ref/jetgate.py`,
`weld_ref/sweepgate.txt`; 0 sim-misaligned events, identical denominators 21,919 core / 49,459 all):

| | BASE | S02 | delta | p |
|---|---:|---:|---:|---:|
| **eff core-all** | .7659 | **.7695** | **+.0036** | **9.9e-04** |
| eff <.005 | .4108 | .4164 | +.0056 | .41 |
| eff [.005,.01) | .4773 | .4931 | +.0158 | .011 |
| eff [.01,.02) | .5932 | .6013 | +.0080 | .090 |
| **eff <.02** | .5074 | **.5169** | **+.0095** | **.0024** |
| eff [.02,.05) | .7459 | .7483 | +.0024 | .42 |
| eff <.05 | .6091 | .6156 | +.0065 | .0025 |
| eff >.05 | .8902 | .8915 | +.0013 | .21 |
| eff all-sim | .8091 | .8110 | +.0019 | |
| fake all | .1317 | **.1271** | **-.0046** | 4.8e-12 |
| dup all | .0246 | .0243 | -.0003 | .38 |
| **dup <.05** | .0313 | **.0345** | **+.0032** | **.017** |
| TCs/evt | 123.15 | 122.90 | -0.25 | |

All eight dR aggregates >= 0. Only negative fine bins: [.0175,.02) -.0020 (p=1) and [.04,.05) -.0043
(p=.47). **SOFTER JETS** (`jpr2_ref/jetptaxis.py`): genjet 50-200 .9277 -> .9270 (-3 sims of 4067),
**200-500 .9164 -> .9211**, 500-1000 .8761 -> .8778, 1000-2000 .7721 -> .7770, >2000 .7527 -> .7534.
**SIM pt** (`jpr2_ref/ptaxis.py`): 10-20 +.0027, 20-50 +.0053, 50-100 +.0069, **100-300 +.0192**,
**>300 -.0210** (denom 761, -16 sims, in the band where we lead master by +.1945).

**PU200 tune, 1000 events, paired McNemar** (`p4_ref/paired_rle.py` + `d3_ref/pu_judge.py`; the
pristine ship binary and the knob binary with the knob unset agree on all 35 fields, so BASE is a
true null):

| cell | BASE | S02 | delta | p |
|---|---:|---:|---:|---:|
| overall | .8097 | .8099 | +.0002 | .33 |
| barrel | .9242 | .9251 | +.0008 | .011 |
| transition | .8796 | .8791 | -.0005 | .49 |
| endcap | .6795 | .6794 | -.0001 | .66 |
| **dxy [0,1)** | .8345 | .8349 | +.0004 | .033 |
| **dxy [1,5)** | .5621 | .5669 | **+.0049** | .053 |
| **dxy [5,10)** | .2502 | .2582 | **+.0079** | .039 |
| **dxy [10,30)** | .0538 | .0551 | +.0013 | .63 |
| **vxy [1,5) / [5,10) / [10,30)** | | | **+.0017 / +.0015 / +.0041** | .15 / .61 / .036 |
| fake overall / barrel | .0454 / .0519 | .0444 / .0499 | **-.0010 / -.0021** | |
| dup overall / barrel | .0428 / .0140 | .0424 / .0127 | **-.0004 / -.0013** | |

**All seven displaced bands neutral-or-better, three significantly better.** The PU200 fake drop is
worth flagging on its own: round 2 shipped with `fake_overall` +3.4% relative, and this takes back
-2.1% of it for free.

**Cubes, full samples, own runs** (`cube50 -n -1 -s 4`; `cube50_highPt -n 5000 -s 4`, `RUN_EXIT=0`,
n_evt 5000 and every denominator identical across arms):

| | cube50 delta | cube50_highPt delta |
|---|---|---|
| every efficiency band | **+.00000** except `eff_vxy_5_10` **-.00093** and `eff_dxy_5_10` **-.00019** | **+.00000 on ALL TWELVE**, incl. **dxy[1,5) = .12089 unchanged** |
| every fake band | **+.00000** | **+.00000** |
| dup | +.00000 (endcap -.00002) | **-.00207 overall, -.00990 endcap** |

**This is the sparse-region invariance guard passing MECHANICALLY, not by construction, and it is why
the arm needs no density conditioning.** The sweep count only binds where the mutual-best iteration
has not converged, and how much there is to converge scales with incidence degree, which
`ChainConfig.h:285-292` already measures at **2.08 mean per shared key on PU200 against 62-272 on
jets** (max 554 vs 2303) -- my own stage-d slots carry a median **195-222** eligible edges. So the
third sweep simply has almost nothing left to do off-distribution, and the cube tables above are the
direct evidence rather than an argument. **Stated as a limit, not a claim: I did not measure the
eligible-incidence distribution on PU200 or the guns myself** -- the cube deltas of exactly .00000
and the PU200 overall +.0002 are the measurements. The E1-B2 far-dca cell is untouched (no attach
code, no bar, no weight file is in the patch).

### 3. THE RE-KEY ARM: REFUSED, with the exchange-rate table

Built and deployed, because it was the only remaining lever that adds NO welds -- a `weldKey` column
on `ChainEdgesSoA` carrying the scalar the K6a/K6b ARGMAX orders on, separate from the `logOdds` that
K6e sums and the gate reads (`ChainEdges.h chainWeldKeyScalar`, mode 0 = max(mP,mD) = today).
**Inertness proven: the weldKey binary `c2953e626b41743caaa5008416efda7d` with the mode unset is
bit-identical to the pristine ship binary on all 23 jetphys fields**, so the extra column costs no
physics. Jets TUNE 500 events, 3 sweeps, single variable:

| weld argmax key | eff core | eff <.02 | fake | dup | TC/evt | offline proxy said |
|---|---:|---:|---:|---:|---:|---:|
| **max(mP,mD) = SHIPPED** | .76587 | .5074 | .13170 | .02463 | 123.1 | .0000 stage-d sims |
| min(mP,mD) | **-.0042** | **-.0155** | -.0146 | +.0008 | 119.4 | **.2226** |
| mP | -.0031 | -.0133 | -.0133 | -.0005 | 119.5 | .1934 |
| mP + mD | +.0012 | -.0043 | -.0070 | -.0001 | 121.9 | .1752 |
| **`kChainWeldSweeps = 2` for comparison** | **+.0036** | **+.0095** | -.0046 | -.0003 | 122.9 | -- |

**All three alternative keys lose deep-core efficiency**, and the best of them (mP+mD, +.0012 core /
-.0043 deep core) is dominated by the one-constant arm on every axis while costing an SoA column.
**This is the fifth time an offline proxy in this project has mispredicted deployment**: the sweep-1
mutual-argmax predicate is a rigorous lower bound on weld reach and it correctly said min(mP,mD)
recovers .2226 of stage-d sims -- what it could not price is the ~15% of currently-welded edges the
re-key displaces, which section 1 shows is exactly the currency the weld is most sensitive to.
The winner's-curse reading of `max` is therefore **wrong as a lever even though the argmax really is
an extreme-value selection over ~200 competitors.** I recommend the `weldKey` column NOT be shipped
(the patch above does not contain it); it is banked at `weld_ref/keybin/` + `w1` if anyone wants it.

### 4. TWO CORRECTIONS TO THE RECORD, both with numbers, because a PARKED candidate rests on one

**(a) "The weld can only reach ~3 ranks deep in a node's own ordering" is false as a theorem.**
`FINDINGS_JET.md:363-368` and `interface/ChainConfig.h:292` ("the weld consumes at most
`kChainWeldSweeps` edges per node-slot") both state it, and M2's parked per-node top-C weld cap
(`m2_ref/`, "provably lossless C >= 16") is justified by it. K6a excludes an edge whose HEAD's
in-slot is taken, so a later sweep's argmax at a node is the highest-ranked edge with a free head,
which can be arbitrarily deep. **Empirically the conclusion survives but the proof does not**: over
36,026 tree-welded edges with a complete slot list (`weld_ref/wrank.py`), the out-slot rank of a
consumed edge is p50 1, p90 3, p99 4, **max 9**, with .9726 within rank 3 and 1.0000 within rank 8
(in-slot: p50 1, p90 2, max 9). So C >= 16 is safe on this sample by MEASUREMENT, not by the sweep
count. **Caveat with that number: those slots carry a median of 10 eligible edges, i.e. the
complete-slot subset is biased toward the sparser slots where a weld actually happened**, so read it
as "no counterexample above rank 9 was found", not as a bound.
**(b) The fixed point of K6a/K6b is the greedy matching by weld key**, so at convergence no slot with
any eligible edge can be empty; the shipped 3 sweeps leaves **32.8%** of the slots a true core edge
loses still EMPTY (local sim: .659 at 1 sweep, .328 at 3, .053 at 8, .0027 at 20).

### 5. What I did NOT do, and what I would do next
* No union arm (S02 x re-key) -- the re-key is negative alone and unions are measured, never summed;
  with the re-key refused there is nothing to union. **Timing untouched** (out of scope), but the
  direction is favourable and structural, not measured: K6a/K6b runs 2/3 as many times, and
  `FINDINGS_JET.md:508` put K6ab at 18.8% of chain time, so ~6% of chain time should come back.
* **Stage d's remaining +.026 is NOT reachable from inside the weld.** After the sweep move, what is
  left is a genuine ranking loss: .8230 of stage-d sims have EVERY eligible pair strictly out-ranked,
  at median rank 63 of 195 competitors, and the three cheapest re-orderings of the head's existing
  outputs all lose deep-core efficiency. The next honest step is a sharper EDGE head (declined) or a
  multi-slot weld (an admission lever, and section 1 just priced admission on this graph as
  negative). I would spend the next round elsewhere.
* Two stale comments the coordinator may want to fix while committing: `ChainConfig.h:292`'s
  "the weld consumes at most `kChainWeldSweeps` edges per node-slot" (see 4a) and
  `ChainEdgesSoA.h:39`'s "repeat the lookup ~8-16x per edge" (4x at two sweeps). Both are comments
  only; I left them out of the patch to keep it one constant and verified.

### Reproduce
```
weld_ref/beat1.py jpr2_ref/fun                       # pass 1, free, JPR2's corpus
weld_ref/wlaunch.sh 0 199 24                         # the 200-event pass-2 corpus (needs ~250 MB)
weld_ref/wagg.py weld_ref/fun                        # ranks, slot occupancy, competitor census
weld_ref/{wsweep,wqual,wkey,wrank}.py weld_ref/fun   # sweep sim, per-sweep purity, key screen, ranks
weld_ref/gates.sh <TAG> <sweeps|base> <all|jet|pu|cubes>   # env-knobbed arms (WELD_BIN, WELD_KEY)
weld_ref/patgates.sh P02 all                         # the SHIP-PATCH arm from worktree w2
weld_ref/chcmp.sh 0 5 ; weld_ref/chagg.py weld_ref/ch     # chain census vs sweep count
p4_ref/jetgate.py --split tune weld_ref/runs/{BASE,S01,S02}_jet.root m3_ref/jpr_master_jets1000.root
p4_ref/paired_rle.py weld_ref/runs/{BASE,S02}_pu.root BASE S02
```
1.2 GB in `weld_ref/`. Nothing committed, nothing published, nothing in `/tmp`.
-- WELD

## [T4 02:40] **THE CLASS IS NET POSITIVE, IT IS UNDER-ADMITTED, AND SUPPRESSING IT IS FORBIDDEN BY THE CROWN JEWELS: deleting the 4-layer class costs PU200 `dxy[10,30)` .0538 -> .0228 (-58% relative), `dxy[5,10)` .2502 -> .1917, `dxy[1,5)` .5621 -> .5318 and `vxy[10,30)` .6963 -> .6764 -- 284 displaced sims lost and ZERO gained, paired p 2e-28 to 3e-18.** Turning that around gives the round a CANDIDATE: **`T4C1`, a density-conditioned loosening of the 4-layer IP gate bar, buys +.0091 jet-core efficiency (+.0241 at dR<.02, +.0261 at dR<.005, EVERY fine bin from [0,.0025) to [.03,.04) positive and significant) for EIGHT extra TCs in 1,583,791 on PU200, three discordant sims of 75,422 all in its favour, and BOTH CUBES BIT-IDENTICAL including `cube50_highPt` dxy[1,5) = .1209 unchanged to four decimals.** Caveats WITH the headline: **jet-core dup (dR<.05) goes .0313 -> .0324 (+3.5% rel, p .066, NOT resolved) -- the one cell that moves the wrong way**; jets TUNE half only (500-999 sealed, I never opened them); PU200 `event_2000` never opened; the full-sample `cube50_highPt` runs are still in flight (the 5000-entry slice that carries the .1209 headline is IN and bit-identical); nothing is timed.

### 1. THE POLICY ANSWER: the class carries the displaced crown jewels on PU200

`LST_T4_EMIT_MIN=5` deletes the class at emission; `LST_T4_GATE_TIGHTEN=1000` kills it at the gate.
PU200 tune, 1000 events, 75,422 sims, paired McNemar (`p4_ref/paired_rle.py`):

| cell | SHIP | **T4E5** (delete) | lost / gained | p | **T4KG** (gate-kill) |
|---|---:|---:|---:|---:|---:|
| **eff_dxy[1,5)** | .5621 | **.5318 (-.0302)** | **93 / 0** | 2.0e-28 | .5344 (-.0276) |
| **eff_dxy[5,10)** | .2502 | **.1917 (-.0586)** | **59 / 0** | 3.5e-18 | .1966 (-.0536) |
| **eff_dxy[10,30)** | .0538 | **.0228 (-.0310)** | **49 / 0** | 3.6e-15 | .0488 (-.0051) |
| **eff_vxy[10,30)** | .6963 | **.6764 (-.0199)** | **83 / 0** | 2.1e-25 | .6821 (-.0141) |
| eff_vxy[5,10) | .7128 | .6975 (-.0153) | 32 / 1 | 7.9e-09 | .7015 (-.0113) |
| eff_overall | .8097 | .8086 (-.0011) | 204 / 121 | 4.8e-06 | .8099 (+.0002, n.s.) |
| fake_overall | .04535 | .03533 (-22% rel) | | | .03982 |
| n_tc / n_tc_t4cl | 1583791 / 63851 | -25038 / -63851 | | | -14296 / -56706 |

**The 4-layer class is where our displaced advantage lives**: it carries 58% of `dxy[10,30)`, 23% of
`dxy[5,10)`, 5.4% of `dxy[1,5)` and 2.9% of `vxy[10,30)`, and **not one displaced sim is recovered by
another route when it is removed** (B-only = 0 in all four dxy bands). Under
[[feedback_tuning_priority]] and [[feedback_headline_caveats]] that closes the whole suppression
family, including round 2's unmeasured leftover: **the `t4p10` idea (a +0.10 offset on the four
4-layer bars, left UNMEASURED at [COORDINATOR 22:05]) is a small version of exactly this arm and
should not be built.** On jets the same arms cost -.0218 (emit) / -.0266 (gate) jet-core efficiency,
and the gate version makes jet fake WORSE (.1317 -> .1359, dR<.02 .3997 -> .4338) because the freed
hits go to fakes: T5 45.8 -> 47.7/evt at non-fake .8262 -> .8015. **6-8 fake TCs removed per core sim
LOST, against a budget that says we may SPEND 2.6 fakes per sim GAINED.**

### 2. SO THE LEVER IS THE OTHER DIRECTION -- and 3/4 of the gate stage's core budget is one bar

`t4_ref/stage.py`, 450-event corpus, core sims dR<.05: **677 of 873 stage-e sims (.776) have a
gate-KILLED chain whose entire node run is their own T3s, 4 layers long -- and 697 of the 700 are
BRANCH 0, the 4-layer IP bar `m3Theta4`.** (Stage f's mirror: 552 of 1,481 have a gate-ALIVE pure
chain that is ONLY 4-layer -- KEY's population, not mine.) The gate kills **95.15% of all 4-layer
chains and 97.97% of branch 0**; a 4-layer chain that survives delivers at .0415 against .2085 for
5-layer and .4656 for 6+.

### 3. THE CANDIDATE FRONTIER (jets tune 0-499 paired; PU200 tune 1000 evt paired; both cubes)

`bar = m3Theta4 + zdM4 - delta * min(1, max(0, log10(1+deg) - log10(1+rho0)) / decades)`, where
`deg` = `ChainFeatures[14]` = max over the chain's OWN weld junctions of degIn*degOut. Zero at or
below rho0 by construction, so sparse regions are bit-identical without needing a measurement.
Design table (`p3_ref/lab/{jet,pu}`, head-independent columns, 839k jet + 1.55M PU200 branch-0
4-layer chains): fraction above rho0 = **jets core-TRUE .964 / .930 / .801 / .517** vs
**PU200 .1095 / .0199 / .0018 / .0001** at rho0 = 10 / 30 / 100 / 300 -- a 47x to 5000x separation
with no sample-level quantity anywhere in it.

| arm (delta, rho0, decades) | jet core | dR<.02 | jet fake (pooled) | jet dup <.05 | PU200 sims lost/gained | PU200 n_tc | cube50 | cube50_hp |
|---|---:|---:|---:|---:|---:|---:|---|---|
| SHIP | .7659 | .5074 | .1317 | .0313 | -- | 1583791 | -- | -- |
| **T4C3** (2, 100, 0.5) conservative | .7741 **+.0082** | +.0216 | -.0003 | +.0011 | **0 / 0, all 12 cells** | **+0** | -- | -- |
| **T4C1** (2, 30, 1.0) **PRIMARY** | .7749 **+.0091** | **+.0241** | **-.0005** | +.0012 | **0 / 3** | **+8** | **BIT-IDENTICAL** | **BIT-IDENTICAL** |
| **T4R10** (2, 10, 1.0) aggressive | .7754 **+.0096** | +.0250 | +.0000 | +.0011 | 5 / 8 (dxy[1,5) **0/4**) | +31 | **BIT-IDENTICAL** | pending |
| T4U20 unconditioned (2) | .7753 +.0094 | +.0255 | +.0007 | +.0011 | not run | | | |
| T4U40 unconditioned (4) | .7749 +.0091 | +.0255 | **+.0101** | +.0009 | not run | | | |
| T4D20 (2, 1000, 1.0) | .7663 +.0004 | +.0013 | -.0000 | +.0000 | -- | | | |

Two things this table says that I did not expect. **(a) The condition costs almost nothing on jets:
T4C1 gets 97% of the unconditioned gain, and T4R10 gets 102% of it at LOWER fake** -- the occupancy
cut removes admissions that were pure fake. **(b) The unconditioned arm is peaked at delta 2**;
delta 4 buys no efficiency and +.0101 of pooled fake, so the bar is genuinely mis-set by ~2 units
for the jet regime and not more. rho0 = 1000 is inert on BOTH samples and is the null control that
proves the conditioning variable is doing the work.

### 4. `T4C1` FULL GATE TABLE

**JETS tune 0-499, paired McNemar** (`p4_ref/jetgate.py --split tune`, `t4_ref/jetgate_C1only.json`):

| cell | SHIP | T4C1 | delta | lost | gained | p |
|---|---:|---:|---:|---:|---:|---:|
| **core-all** | .7659 | **.7749** | **+.0091** | 55 | 254 | **1.0e-31** |
| **<.005** | .4108 | .4369 | **+.0261** | 6 | 48 | 3.3e-09 |
| [.005,.01) | .4773 | .4968 | +.0195 | 6 | 37 | 1.6e-06 |
| [.01,.02) | .5932 | .6190 | +.0258 | 12 | 73 | 8.1e-12 |
| **<.02** | .5074 | **.5315** | **+.0241** | 24 | 158 | 2.2e-25 |
| [.02,.05) | .7459 | .7565 | +.0106 | 21 | 65 | 2.2e-06 |
| [0,.0025) (the shared wall) | .4277 | .4495 | **+.0218** | 3 | 17 | .0026 |
| [.0125,.015) (the peak-gap bin) | .5831 | .6042 | **+.0212** | 4 | 17 | .0072 |
| every other fine bin to [.03,.04) | | | **all positive**, +.0101 to +.0314 | | | .0001-.019 |
| fake pooled / <.05 / <.02 | .1317/.3766/.3997 | .1312/.3737/.4069 | **-.0005 / -.0029** / +.0072 | | | .27/.19/.038 |
| **dup <.05** | **.0313** | **.0324** | **+.0012 (FLAG)** | | | .066 |
| dup pooled | .0246 | .0249 | +.0003 | | | .021 |
| TCs/evt | 123.1 | 123.6 | +0.5 | | | |

In objects per event: **+0.40 core sims/evt matched (+199 core sims per 500 events) against +0.01
core fake TCs/evt and +0.02 core duplicate TCs/evt** -- 40 core sims per admitted core fake and 18
core sims per admitted core duplicate. The core fake RATE inside dR<.02 rises (+.0072) because the
denominator grows; the core fake COUNT there is +0.06/evt against +0.27 core sims/evt.

**NEW SOFTER-JETS GATE: PASS, exactly** (`jpr2_ref/jetptaxis.py --split tune`):

| genjet pt | denom | delta ALL | delta dR<.005 | delta [.005,.02) | delta [.02,.05) |
|---|---:|---:|---:|---:|---:|
| **50-200** | 4067 | **+.0000** | -- | **+.0000** | **+.0000** |
| **200-500** | 2357 | **+.0000** | **+.0000** | **+.0000** | **+.0000** |
| 500-1000 | 1751 | +.0023 | +.0000 | +.0131 | +.0029 |
| 1000-2000 | 14916 | +.0084 | +.0215 | +.0222 | +.0125 |
| >2000 | 7003 | **+.0104** | **+.0340** | +.0254 | +.0064 |
**The populations where we ALREADY beat master (50-200 and 200-500 GeV, +.038/+.016 in the annulus
per JPR2) do not move by a single track, in any dR band.** The arm is self-targeting because
occupancy is what a hard jet makes -- this is the cleanest evidence I have that the conditioning is
physics and not sample recognition.

**AND IT LANDS ON THE CELL JPR2 RELOCATED THE GAP TO** (`jpr2_ref/ptaxis.py`, sim pt, tune):

| sim pt | denom | MASTER | SHIP | **T4C1** | gap to master, SHIP -> T4C1 |
|---|---:|---:|---:|---:|---|
| 10-20 | 3330 | .8805 | .8589 | **.8673** | -.0216 -> **-.0132** |
| **20-50** | 3971 | .7870 | .7122 | **.7263** | -.0748 -> **-.0607** |
| **50-100** | 2185 | .6146 | .5364 | **.5606** | **-.0783 -> -.0540 (31% of the gap)** |
| 100-300 | 2036 | .3541 | .3718 | **.3954** | +.0177 -> **+.0413** |
| >300 | 761 | .1932 | .3876 | **.4021** | +.1945 -> **+.2089** |
| 0.9-10 | 9636 | -- | -- | **+.0000 to +.0032** | untouched |
137 of the 540 sims of the pt 10-100 GeV master gap, plus 47 above 100 GeV, with the low-pt
population untouched.

**PU200 tune 1000 evt, paired** (`p4_ref/paired_rle.py`): **3 discordant sims in 75,422, ALL
gained** -- `dxy[1,5)` 0/1, `vxy[10,30)` 0/1, `dxy[0,1)` 0/2; every band delta +.0000 to +.0003; all
21 `d3_ref/pu_judge.py` fields identical to five decimals except `eff_dxy[1,5)` +.00032 and
`eff_vxy[10,30)` +.00024 (both UP); `fake_overall` +.00000, `dup_overall` -.00000; **n_tc +8 of
1,583,791**, n_tc_t4cl +14 of 63,851.

**BOTH CUBES: BIT-IDENTICAL.** `cube50` (5000 evt, -s 4) and `cube50_highPt` entries 0-4999 (-s 4):
**every one of the 21 judge fields identical, including n_tc, and 0 discordant sims in all 11 paired
cells.** `cube50_highPt` dxy[1,5) stays **.1209**, dxy[5,10) .0649, vxy[1,5) .3172 -- round 2's
headline is untouched to four decimals, by construction (a displaced gun's junction occupancy is
p50 2, three decades below rho0). The full-sample cubehi runs are in flight and I will post them.

### 5. ARM (b) AND THE pT4 VERDICT, now with the PU200 side

`LST_T4_ATTACH_MIN=4` (see [T4 01:20] for the jet numbers: core -.0002, p .72, i.e. neutral).
**PU200: eff_overall +.00073 with 2 lost / 57 gained (p 6e-15), eff_barrel +.00082, eff_transition
+.00127, ALL FOUR dxy bands and vxy[5,10)/[10,30) with ZERO discordant sims, `fake_overall`
-.00053 (BETTER), and both cubes BIT-IDENTICAL.** Its price: **`dup_overall` +.00025 and
`dup_barrel` +.00060** (+439 duplicate TCs per 1000 events for +55 sims = 8:1 in the currency we
already owe, against D's round-2 rule which ran 228:1 the other way), and it re-types 30,722 of
63,851 PU200 T4-class TCs into pT5-class. **My recommendation: do not ship it, but record it** -- it
is a real +.0007 PU200 efficiency for one constant, and if AT's retrain changes the pair head's
calibration this arm should be re-measured on top of it rather than inherited from here.
**For the pT4 question (arm c): the object is reachable TODAY for one constant, it is emitted as a
type-7 row with the pixel hits prepended, and deployed it is worth -.0002 on jets and +.0007 on
PU200 with dup worse. I recommend NOT porting pT4.** The four branch commits sit on LST's T4 object,
which this tree no longer has (our 4-layer class is a WELD product), so the port is a rewrite whose
product is already measured.

### 6. THE ARTEFACT

| item | value |
|---|---|
| patch | `standalone/t4_ref/t4_class_policy.patch` md5 **`6550c79df1ff469319bdb44fbeabd606`**, 274 lines, 6 files, `git apply --check` CLEAN (the main tree's `src`+`interface` are byte-identical to `04c6e122e68`) |
| worktree / binary | `gpu_wt/t4a`, `bin/lst_cpu` md5 `945c71c4041a9fe0b414b333ff9099c6`, `liblst_cpu.so` md5 `1fb656ee55247cffaf5cf06fbe0e573b`, snapshot `t4_ref/snap/{bin,LST}`, 0 `error:` in `.make.log.1786595247` |
| inertness | knob-free arm is **BIT-IDENTICAL to the ship binary's own runs** on jets (500/500 events, 10 branches) AND on PU200 (all 21 judge fields, n_tc exact) |
| **to ship T4C1** | `ChainConfig::t4DensDelta = 2.f; t4DensRho0 = 30.f; t4DensDecades = 1.f;` -- three floats, NO new kernel, NO new weight file, NO new cell, NO new feature |
| to reproduce | `bash t4_ref/gates.sh $PWD/t4_ref/snap T4C1 all "LST_T4_DENS_DELTA=2.0 LST_T4_DENS_RHO0=30 LST_T4_DENS_DECADES=1"` then `python3 p4_ref/jetgate.py --split tune at_ref/runs/BASE_jet.root t4_ref/runs/T4C1_jet.root --labels SHIP,T4C1` |
| other knobs in the patch | `LST_T4_GATE_TIGHTEN`, `LST_T4_EMIT_MIN`, `LST_T4_ATTACH_MIN` -- all inert at their defaults; they are the PRICING instruments for sections 1 and 5 and the coordinator may strip them |

**Simplicity disclosure** ([[feedback_simplicity_over_cleverness]]): the shippable part is three
floats and one three-line inline function in `ChainGate.h`. The other three knobs are measurement
scaffolding; if the maintainer wants the minimum diff, sections 1 and 5 are already measured and the
patch can be reduced to the density term alone.

### 7. FOR THE OTHER AGENTS
* **KEY** -- 552 of 1,481 stage-f core sims (dR<.05) have a pure gate-alive chain that is ONLY
  4-layer, and it rises with sim pt (2 / 20 / 106 / 143 / 206 / 75 across 1-10 / 10-20 / 20-50 /
  50-100 / 100-300 / >300 GeV). **T4C1 makes this population BIGGER** (T4 core-eff share 1016 ->
  1307, T4 TCs/evt 10.1 -> 11.4), so your discriminator will see more 4-layer candidates than the
  base does; measure on top of T4C1 if the coordinator takes it, and note that a key which cannot
  rank a 4-layer chain above a 5-layer thief cannot reach 37% of your stage in the core.
* **PUR** -- your trim hazard is worse than the attach-eligibility line suggests: a chain that drops
  from 5 layers to 4 lands in a class whose **delivery-given-gate-alive is .0415 against .2085 for
  5-layer**, i.e. it is ~5x less likely to win its claim, and its non-fake fraction on jets is .489.
  That belongs in your ledger next to the duplicate hazard. (The attach-eligibility half of the
  hazard is real but cheap to remove: `LST_T4_ATTACH_MIN=4` is measured neutral on jets.)
* **AT** -- `LST_T4_ATTACH_MIN` is inert by default and I am NOT recommending it, so your on-policy
  dumps are unaffected. I did not touch `AttachNetworkWeights.h` or any of the eight attach
  `ChainConfig` bars. T4C1 does not touch the attach path at all. If you want the 4-layer targets in
  your mix, section 5 is their deployed price on both samples.
* **SF / WELD** -- T4C1 moves the 4-layer IP GATE bar only. It does not touch `thetaEdgeE1/E2`, the
  80 weld WPs, or the weld argmax, so it is orthogonal to both of your levers by construction; the
  union is measurable with one env string on my binary if the coordinator wants it.
-- T4

## [WELD 01:25] THE LAST OPEN CHECK LANDS: **the ship patch is bit-identical to the env-knobbed S02 arm on the PU200 side too -- all 35 `d3_ref/pu_judge.py` fields, 1000 tune events (`RUN_EXIT=0`).**

So `weld_ref/pat/weld_S02.patch` (md5 `baa19ab0654220cc445f2287bb876dd1`) is verified against the
measured arm on **both** judges: 23/23 jetphys fields on 500 jet events and 35/35 PU200 fields on
1000 events. Nothing in [WELD 01:15] is now resting on the env knob. That closes my brief.

One handover note for whoever commits it: my worktrees are `w1` (the measurement scaffolding -- the
`LST_CHAIN_WELD_SWEEPS` knob, the `weldKey` column and the `LST_CHAIN_WELD_KEY` mode, binary
`c2953e626b41743caaa5008416efda7d`) and `w2` (the ship patch and nothing else, binary
`dcdd6dec417454034ec75bf94b509228`). **Only w2's diff should be committed.** Frozen binaries at
`weld_ref/{swbin,keybin}/`; the corpus and every arm's ROOT/JSON under `weld_ref/{fun,runs,ch}`.
-- WELD

## [T4 03:05] CUBE GATE COMPLETE FOR THE CANDIDATE: **`T4C1` is BIT-IDENTICAL on the FULL 10,000-event `cube50_highPt` as well -- all 21 judge fields including `n_tc`, and 0 discordant sims in all 12 paired cells** (`p4_ref/paired_rle.py`, `-s 4`, no writer segfault on either arm). Also bit-identical: `T4R10` and `T4A4` on `cube50_highPt` entries 0-4999, and `T4C1`/`T4R10`/`T4A4` on `cube50`. So every displaced-gun cell in the round's gate set is untouched at the level of individual sim tracks for all three arms, which is what the conditioning was designed to guarantee by construction rather than by luck.

| sample | events | arms verified BIT-IDENTICAL to SHIP |
|---|---:|---|
| `cube50` | 5000 | T4C1, T4R10, T4A4 |
| `cube50_highPt` entries 0-4999 (carries the .1209 headline) | 5000 | T4C1, T4R10, T4A4 |
| **`cube50_highPt` FULL** | **10000** | **T4C1** (dxy[1,5) .1120, dxy[5,10) .0630, vxy[1,5) .3097, all +.0000, 0/0 discordant) |

Remaining open on `T4C1`, stated so nobody over-reads the gate set: the **jets 500-999 HOLDOUT and
PU200 `event_2000` are SEALED and I never opened either** -- the coordinator owns both; the
**jet-core dup cell (dR<.05) .0313 -> .0324 at p .066 is unresolved and is the one cell moving the
wrong way** (+0.02 core duplicate TCs/evt against +0.40 core sims/evt, 18:1 in favour); and
**nothing is timed** (the arm adds one `log10` and one `min` per 4-layer chain inside a kernel that
already evaluates a 25-32-32-3 MLP for every chain, so I expect free, and I have not measured it).
-- T4

## [PUR 04:15] BOTH CANDIDATES NOW FULLY MEASURED AND THEY SPLIT CLEANLY: **the T3-score rule holds at +.0095 jet core on the full 500-event corpus (230 gained / 22 lost, 36% of the ceiling, twice the ownership family) and FAILS BOTH CUBE GATES — 0 gained / 3 lost on cube50 and 0 gained / 4 lost on cube50_highPt, with `dxy[1,5)` at 0/1 and 0/1, i.e. it gives back round 2's headline cell and buys nothing there.** The ownership rule `sharMD>=1` is HALF the jet gain (+.0041) and **fires ZERO times on either cube, bit-identically, by mechanism.** So: **+.0041 that clears every gate, or +.0095 that does not.** PU200 for the T3 rule is still running and cannot rescue it — the cube fail is already a hard-gate fail.

### The two candidates side by side, all four samples, one instrument
Jets = tune 0-499 (21,919 core sims). PU200 = event_1000 (72,481 in-acceptance sims). Cubes = full
samples, gun denominators (**no vertex cut** — the guns are judged in bands; my first cube pass used
the prompt vertex cut and its band cells were void, this table is the corrected one).

| | **PUR-SB: `sharMD >= 1`** | **PUR-T3: `t3 fakeScore > .5`, terminal** |
|---|---|---|
| what it reads | another emitted chain TC carries the same mini-doublet | `triplets.fakeScore()` of the terminal member node |
| cost to build | new kernel + per-MD counter (K10 must split) | **~10 lines inside `ChainEmitTCs`, no new launch** |
| **jets core eff** | **+.00406** (113 gained / 24 lost) | **+.00949** (230 / 22) |
| ... as a share of the +.0260 ceiling | .156 | **.365** |
| jets fake TCs converted (non-core sims) | 169 / 18 | 233 / 34 |
| jets core duplicates | +0 | +1 |
| jets dxy / vxy bands | 113/24 in dxy[0,1); **0/0 in all other bands** | 439/253 in dxy[0,1); 0/0 elsewhere |
| PU200 eff | **+22** (24 / 2) | running |
| PU200 dxy[1,5) / [5,10) / [10,30) | **0/0, 0/0, 0/0** | running |
| PU200 duplicates | **-13 core, -143 pooled (BETTER)** | running |
| **cube50** | **fires 0 of 1,329 — BIT-IDENTICAL** | fires 590; **0 gained / 3 lost** |
| **cube50_highPt** | **fires 0 of 519 — BIT-IDENTICAL** | fires 345; **0 gained / 4 lost** |
| ... cube50_highPt dxy[0,1)/[1,5)/[5,10)/[10,30) | 0/0, **0/0**, 0/0, 0/0 | 0/1, **0/1**, 0/1, 0/1 |

**Why the T3 rule fails the guns, mechanistically, so nobody retunes the threshold and hopes:** on a
single-particle displaced gun there ARE no stolen mini-doublets, so every firing is a false positive.
The measurement says exactly that — the T3 fake score's per-mini-doublet AUC is **.4194 on
cube50_highPt and .5307 on cube50** (against .80 on jets), i.e. on the guns it carries no information
and simply deletes hits. 345 firings, 4 destroyed matches, 0 repairs. A lower threshold fires more; a
higher one keeps firing on the same population because the score is uninformative there, not small.

**The conjunction does not rescue it either, and this is the load-bearing negative:** `t3 fakeScore
terminal AND shar >= 1` fires **0 times on both cubes** (inheriting `sharMD`'s mechanism) and delivers
**+.00493** on jets — i.e. it throws away exactly the extra reach that made the T3 rule interesting and
lands back on the ownership family's number. The T3 score's extra .005 lives precisely in the chains
where no ownership evidence exists, which is the population the guns cannot distinguish from their own.

### What I hand over
1. **PUR-SB (`sharMD >= 1`) is a certified-on-every-gate candidate worth +.0041 jet core**, with PU200
   efficiency positive, all seven displaced bands at zero discordant sims, PU200 duplicates BETTER, and
   both cubes bit-identical by mechanism. Its bill is a new kernel launch and a per-MD counter for
   +.004. **I am not recommending it** — [[simplicity-over-cleverness]] prices a kernel higher than
   that, and the maintainer already declined +.02 for a weight file. It is on record, fully priced, if
   the round ends short.
2. **PUR-T3 is the better physics and the wrong shape.** +.0095 for ten lines is the best price in my
   assignment, and the only thing standing between it and a ship is that it fires in regions where
   there is nothing to repair. **The named next experiment is a REGIME-CONDITIONED threshold** — make
   the `fakeScore` bar a continuous function of local occupancy so it relaxes to "never fire" in sparse
   regions, which is exactly the round's allowed conditioning shape and exactly what the cube failure
   asks for. I did not build it: it needs the occupancy column plumbed into K10 and a deployed sweep,
   which is a round's work, not a measurement.
3. **The residual/chi2 family stays closed** (AUC .494) and **the K6f trim relaxation stays closed**
   (only 26.4% of the addressable population even passes its concentrating guard).

**One measurement still in flight and it cannot change the verdict:** the PU200 200-event `--allobj`
run for PUR-T3 (the cube fail is already a hard-gate fail, and PUR-SB's PU200 row is complete). It is
chained to reduce and score itself into `pur_ref/chain2.log` when the run lands, tagged `TPU`.

Artifacts added: `pur_ref/{TJ5B,TC5B,TCHB,T3S}_*` (the `--allobj` corpora and their scores),
`pur_ref/{A500_jet,A200_pu,AC50,ACHI}.root` (the four `--allobj` runs, pristine-binary md5 verified),
`pur_ref/chain{2,3,4}.log`. **No build, no worktree, no deployment, no commit.**
-- PUR
