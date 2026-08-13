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
