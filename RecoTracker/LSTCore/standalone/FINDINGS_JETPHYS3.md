# FINDINGS_JETPHYS3.md

CUMULATIVE JET-CORE RECON, ROUND-3 BASELINE (agent JPR3). Measurement and attribution only; no fix,
no tuning, no candidate, nothing built, nothing committed, nothing published. Posted by the
coordinator on JPR3's behalf (the harness blocks subagent report-file writes).

Baseline: **`2840340b6f1`** (the four-way ship: `weld_S02` + `T4C1` + `KE50` + `ATJ25R`). Every run
used the PRISTINE main-tree ship binary `standalone/bin/lst_cpu` md5 `4da4e362d2867427d82726b329dbfdb6`,
`LST/liblst_cpu.so` `64c208c2beb32407e64145dd2d0f1c4d` -- the same binary that produced the
coordinator's ship-verification run. **No build, no worktree.**

Sample discipline: **jets TUNE half only** (rows 0-449 for the funnel corpus, 0-499 for rate tables).
Jets 500-999 never opened; holdout numbers quoted are the coordinator's. **No PU200 was run** --
items 4 and 5 of the brief were cancelled mid-round by the maintainer, so the track-length
substitution question and the PU200 low-pT gap are NOT in this file. Artifacts: `jpr3_ref/` (830 MB).

---

## [JPR3 09:20] THE FUNNEL RE-ATTRIBUTED: **the loss budget fell 4,559 -> 3,659 core sims, the GATE stage collapsed by 63% (823 -> 303) and the 75%-purity stage by 48% (499 -> 260) -- and with those two out of the way the CLAIM is now .386 of the whole budget, while the four arbitration stages own .700 of it, worth +.1300 of core efficiency.**

Caveats WITH the headline: 450 tune events, JPR/JPR2's instrument unchanged except one appended
diagnostic column; the corpus reads .8142 core efficiency against the 500-event `-s 16` run's .8120,
a **+.0022 corpus offset** (JPR +.0022, JPR2 +.0026), two orders below every effect here.
Validations reproduced on this head: offline K5 edge replay vs dumped `logOdds` **max |dmX| =
9.03e-05 over 29,964,189 edges**; delivery predicate vs the kernel's `[CHAIN K9] accepted=`
**101.20/evt offline vs 101.95/evt**, max per-event difference 8 of ~102; dense chain-node index ==
ntuple `t3` row asserted on all 6 hit rows, 450/450 events.

### Q1 -- the funnel. 19,693 core sims, **3,659 unmatched** (was 4,559, was 6,747).

| first stage that loses an unmatched core sim | JPR n | JPR2 n | JPR2 frac | **NEW n** | **NEW frac** | ceiling |
|---|---:|---:|---:|---:|---:|---:|
| **f claim: gate-alive chain, no TC delivered** | 2928 | 1442 | .316 | **1411** | **.386** | **+.0717** |
| a1 hits: <3 OT layers carry a reco hit | 807 | 779 | .171 | 727 | .199 | +.0369 (recoverable **zero**) |
| **d weld: eligible but loses the argmax** | 646 | 599 | .131 | **585** | **.160** | **+.0297** |
| **e gate: the welded chain is gate-killed** | 1428 | 823 | .181 | **303** | **.083** | **+.0154** |
| **g match: TC delivered, <=75% purity** | 481 | 499 | .109 | **260** | **.071** | **+.0132** |
| c0 no graph-adjacent pair among its T3s | 186 | 164 | .036 | 148 | .040 | +.0075 |
| a3 LS: no two >75% LSs share an MD | 170 | 156 | .034 | 135 | .037 | +.0069 |
| a2 MDs: <3 layers carry a >75% MD | 84 | 81 | .018 | 77 | .021 | +.0039 |
| b T3: no >75% T3 built | 15 | 14 | .003 | 11 | .003 | +.0006 |
| c1 the C=256 degree cap | 1 | 1 | .000 | **1** | .000 | +.00005 |
| c2 the 80 edge weld WPs | 1 | 1 | .000 | **1** | .000 | +.00005 |
| efficiency (450-event corpus) | .6574 | .7685 | | **.8142** | | |

**What moved is not what the last two rounds moved.** Round 2's gate retrain drained the *claim*;
round 3 drained the *gate* and the *purity* stage:
* **e gate 823 -> 303 (-63%)** is `T4C1`. The 4-layer IP branch's kill rate on core-sim chains is now
  .2422 (branch 2 kills .0285); population gate-kill went .9277 -> **.9084**. Stage e is now the
  SMALLEST of the four arbitration stages.
* **g match 499 -> 260 (-48%)** moved without anybody shipping a purity arm -- `PUR` shipped nothing.
  It fell because the delivered TC set changed underneath it (T4 and bare pLS both up).
* **f claim 1442 -> 1411** barely moved in absolute terms, so its FRACTION rose .316 -> .386. It is
  now larger than the next two stages combined.
* a1/a2/a3/b/c0 are upstream of every arm and moved only because more of those sims are matched
  off-chain (bare pLS core wins 1,340 -> 1,807).

### The fine bands

| band | denom | JPR2 eff | **NEW eff** | weld d | gate e | claim f | purity g | a1 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| dR < .005 | 1444 | .4086 | **.5000** | .217 | .100 | **.486** | .053 | .096 |
| [.005,.02) | 3556 | .5546 | **.6637** | .208 | .094 | **.467** | .067 | .101 |
| [.02,.05) | 3690 | .7523 | **.8138** | .151 | .093 | **.394** | .096 | .157 |
| [.05,.10) | 3686 | .8511 | **.8804** | .125 | .066 | .331 | .086 | .263 |
| >= .10 | 7317 | .9101 | **.9162** | .033 | .042 | .137 | .062 | .511 |

### The sim-pt axis -- the arbitration problem is a HIGH-pt problem

| fraction of the band's own budget | 0.9-10 | 10-20 | 20-50 | 50-100 | 100-300 | >300 |
|---|---:|---:|---:|---:|---:|---:|
| denominator | 8687 | 2998 | 3570 | 1949 | 1806 | 683 |
| unmatched | 664 | 293 | 714 | 691 | 928 | 369 |
| efficiency | .9236 | .9023 | .8000 | .6455 | .4862 | .4597 |
| d weld argmax | .017 | .078 | .136 | .208 | **.234** | **.252** |
| e gate | .069 | .065 | .080 | .080 | .088 | .119 |
| f claim | .096 | .386 | .424 | **.482** | **.473** | .431 |
| g 75% purity | .056 | .113 | .083 | .081 | .064 | .043 |
| a1 (not ours) | **.524** | .229 | .171 | .085 | .096 | .114 |

**Below 10 GeV the funnel is essentially over.** **2,702 of the 3,659 lost core sims (73.8%) are
above 20 GeV**, on 40.7% of the sims.

### The claim, measured again
**gate-alive 576.2 chains/evt -> K9-accepted 102.0/evt = the claim keeps .1769** (JPR2 .1723, JPR
.0669). Chains/evt fell 8,133 -> **6,291** (the weld does a third less work) while accepted/evt held.
For 1,406 stage-f core sims joinable to their thief, **the thief out-ranks the victim .7532 of the
time** (JPR2 .7883, JPR .738), **median advantage +5.008** (JPR2 +5.766, JPR +11.2).

### The claim's recoverable ceiling went UP, and so did its price
All 1,411 stage-f core sims have a gate-alive chain owning >=2 of their own >75% T3s (1.0000).
**1,138 (.8065, was .6262) have one whose ENTIRE pre-trim node run is their own T3s** ->
**+.0578 absolute core efficiency**; the whole stage-f budget is +.0717.

**Caveat 1, bigger than before: 64.9% of those purest chains are 4-LAYER** (was 42.4%). Two thirds of
the claim's recoverable ceiling is reachable only through the T4 class, whose delivered non-fake
fraction is **.507** and which is **52.0% of core fake TCs** (purity .375; .693 of them share >=1 hit
with a MISSED core sim).

**Caveat 2, new and cutting the other way: the thief is a REAL track 46.6% of the time.** Stage-f
owners are FAKE only **.5344** of the time (stage g .9846 fake; stage e .4386; stage d .4988). `KEY`
refused the re-ordering lever partly because "a third of lost duels have a thief that is itself a
legitimate >=75% track"; on this head it is closer to a half. **The claim is not a fake-suppression
problem any more -- it is a genuine two-real-tracks-one-hit-set contest in half its volume.**

### Nulls RE-CONFIRMED
C=256 degree cap **1 of 3,659**; the 80 edge weld WPs **1 of 3,659**; T3-DNN; `kChainMaxNodes` /
occupancy / truncation zero.

---

## [JPR3 09:35] THE REACH LADDER, MEASURED FRESH BECAUSE THE WELD CHANGED: **reach FELL .8585 -> .8536 overall and .8061 -> .7929 in the deep core, delivery rose .820 -> .850, and the honest remaining distance is one number -- .9144 reachable against .8142 delivered, i.e. 1,974 core sims per 450 events (4.4/evt, +.1002) already HAVE a welded chain and are not matched.**

Caveats WITH the headline: R8 is the CHAIN route's reach and R11 exceeds R10 beyond dR .05 because
pT5/pLS cover those tracks, so the reach gap is only meaningful inside dR .05. **R5-R8 are NOT
inherited from JPR2 this time** -- `weld_S02` moved them.

| | <.005 | [.005,.02) | [.02,.05) | [.05,.10) | >=.10 | ALL |
|---|---:|---:|---:|---:|---:|---:|
| R5 an adjacent T3 pair EXISTS | .9204 | .9238 | .9195 | .9045 | .8601 | .8954 |
| R7 some such edge is ELIGIBLE | .9204 | .9235 | .9192 | .9045 | .8601 | .8953 |
| **R8 some such edge is WELDED (reach)** | **.7929** | **.8293** | **.8762** | **.8766** | **.8543** | **.8536** |
| ... R8 on the OLD head (JPR2) | .8061 | .8400 | .8816 | .8801 | .8553 | .8585 |
| R9 its chain survives the gate | .7355 | .7868 | .8472 | .8589 | .8391 | .8273 |
| **R10 that chain is DELIVERED** | **.4564** | **.5838** | **.7257** | **.7911** | **.8141** | **.7254** |
| R11 matched, any route (= efficiency) | .5000 | .6637 | .8138 | .8804 | .9162 | .8142 |
| **delivery / reach, R10/R8** | **.576** | **.704** | **.828** | **.903** | **.953** | **.850** |
| ... JPR2 | .500 | .635 | .791 | .881 | .951 | .820 |
| ... JPR | .315 | .394 | .562 | .721 | .910 | .675 |
| **UNION(matched OR welded) = honest ceiling** | **.8193** | **.8749** | **.9225** | **.9381** | **.9364** | **.9144** |
| **distance still to run** | **+.3193** | **+.2112** | **+.1087** | **+.0578** | **+.0202** | **+.1002** |

**Two structural facts, neither previously in the record.**
1. **The 1,974 unmatched-but-welded core sims are EXACTLY stages e + f + g** (303 + 1,411 + 260 =
   1,974, an exact identity). The reach frontier and the arbitration budget are the same object, and
   **stage d (585 sims, +.0297) is NOT on it** -- it is the only reach-EXPANSION lever left, and it
   lives at high pt (.234/.252 of the >100 GeV budget vs .017 below 10 GeV).
2. **`weld_S02` bought its gain by trading reach for delivery, exactly as designed.** R8 is uniformly
   lower (-.0132 deep core) while R10/R8 is uniformly higher (+.076 deep core); net +.0914 deep-core
   efficiency. "More reach" is not automatically good -- the third sweep's marginal welds were .12
   same-sim purity.

### The ladder on the sim-pt axis

| | 0.9-10 | 10-20 | 20-50 | 50-100 | 100-300 | >300 |
|---|---:|---:|---:|---:|---:|---:|
| R5 adjacent T3 pair | .8668 | .9219 | .9162 | .9236 | .9142 | .9048 |
| **R8 welded (reach)** | .8629 | .9079 | .8655 | **.8271** | **.7719** | **.7247** |
| R9 gate-alive | .8432 | .8936 | .8409 | .7917 | .7176 | .6545 |
| **R10 delivered** | .8227 | .8182 | .7109 | .5700 | **.4264** | **.3909** |
| R11 matched | .9236 | .9023 | .8000 | .6455 | .4862 | .4597 |
| **UNION ceiling** | .9405 | .9573 | .9174 | .8733 | **.8073** | **.7804** |
| **distance to run** | +.0169 | +.0550 | +.1174 | **+.2278** | **+.3212** | **+.3206** |
| LST master | .9159-.9064 | .8805 | .7870 | .6146 | .3541 | .1932 |

Above 100 GeV the weld loses 14-18 points of its own reach in the argmax and then throws away another
33-35 points in arbitration: **1,297 core sims per 450 events in two bands, with master at .354/.193
and therefore no reference at all.** The largest single block of recoverable efficiency in the sample.

---

## [JPR3 09:50] WHAT NOBODY HAS LOOKED AT, AND IT RELOCATES THE PROBLEM AGAIN: **the coordinate that governs jet-core loss is not distance to the JET AXIS, it is distance to the NEAREST OTHER TRACK. 57.5% of all remaining loss sits on sims with another selected sim inside dR .005 -- and a third of that population is OUTSIDE the dR<.02 "deep core" that every table in this project instruments.**

Caveats WITH the headline: `dRnn` is a TRUTH quantity (nearest other sim passing the same
`performance.cc` selection, at the production vertex), so this is a diagnosis, not a feature -- see
the proxy measurement at the end, which is a NEGATIVE. 500-event tune half for rate tables, 450-event
corpus for funnel/reach rows; unpaired ratio tables on shared events and denominators.

### (a) The loss budget on the two axes, same 3,659 sims

| axis | band | denom | eff | losses | share |
|---|---|---:|---:|---:|---:|
| **separation dRnn** | <.002 | 1946 | .4943 | 984 | **.269** |
| | [.002,.005) | 3071 | .6356 | 1119 | **.306** |
| | [.005,.01) | 3099 | .7980 | 626 | .171 |
| | [.01,.02) | 3351 | .8929 | 359 | .098 |
| | >=.02 | 8226 | .9306 | 571 | .156 |
| jet-axis dR | <.005 | 1444 | .5000 | 722 | .197 |
| | [.005,.02) | 3556 | .6637 | 1196 | .327 |
| | [.02,.05) | 3690 | .8138 | 687 | .188 |
| | >=.05 | 11003 | .9042 | 1054 | .288 |

### (b) The cross table that settles which axis is causal (efficiency, denom)

| axis dR \ dRnn | <.002 | [.002,.005) | [.005,.01) | [.01,.02) | >=.02 |
|---|---|---|---|---|---|
| <.005 | .431 (708) | .529 (594) | .696 (115) | .895 (19) | .750 (8) |
| [.005,.02) | .458 (698) | .613 (1252) | .760 (1050) | .844 (488) | .926 (68) |
| [.02,.05) | .537 (283) | .682 (570) | .792 (855) | .884 (1119) | .922 (863) |
| >=.05 | .720 (257) | .736 (655) | .851 (1079) | .912 (1725) | .932 (7287) |

**The `dRnn >= .02` column is FLAT** (.926/.922/.932 on 8,218 sims): once a track is isolated, its
distance to the jet axis is worth nothing. **The `axis dR >= .05` row is NOT flat** (.720 -> .932 on
11,003 sims): a track far from the axis but sitting on top of another behaves like a deep-core track.
At fixed own-pt the asymmetry holds -- 20-100 GeV spans .5634 -> .9524 on separation against
.6004 -> .8328 on axis distance; above 100 GeV .3802 -> .8413 against .4283 -> .6206. **Separation is
roughly twice as discriminating as axis distance in exactly the pt bands that carry the loss.**

A CONFOUND, stated so nobody chases it: efficiency vs `pt(neighbour)/pt(self)` runs .744 -> .878
monotonically, but controlled for own pt it disappears. It is own-pt in disguise.

### (c) The mechanism separates cleanly: the weld does NOT care, arbitration does

R8 (weld reach): in the `axis dR >= .05` row it is **.852 / .850 / .865 / .882 / .858** across
separation bands -- flat. R10/R8 (delivery given reach) in the same row: **.749 / .801 / .886 / .913
/ .967**. **The chain is built and then discarded, and what decides whether it is discarded is
whether another track is next to it.** Stage split: 976 of 1,411 stage-f sims (69.2%) and 474 of 585
stage-d sims (81.0%) have a neighbour inside dR .005, while a1 -- the stage that is not ours -- is
44.3% at dRnn >= .02.

### (d) The proxy test, a NEGATIVE that saves a round
The only local-crowding number the kernel computes is `ChainFeatures[14]` (max `degIn*degOut` over
the chain's weld junctions) -- what `T4C1` ramps on. On 60 tune events, 2,125 core sims:

| | value |
|---|---:|
| rank correlation( log10(1+dens), log10(dRnn) ) | **-0.554** |
| AUC, density for "has a neighbour inside .005" | **.6715** |
| AUC, density for "this core sim is UNMATCHED" | .6707 |
| **AUC, density for "this sim's purest gate-alive chain is NOT delivered"** | **.5169** |

**Density finds the hard REGION and is near chance at picking the loser inside it** -- exactly
consistent with a density-conditioned *bar* working (`T4C1`) and density as a *rank* term measuring
-.17. So "separation-conditioned arbitration" cannot be built from the crowding number we have; it
needs a new local observable, and nobody has proposed one. Caveat: 60 events, one proxy, one head.

### (e) Two smaller things, both closing open ledger items
* **The softer-jet gate listed as NOT MEASURED for `ATJ25R`.** Tune half, `04c6e122e68 ->
  2840340b6f1`: genjet 50-200 GeV **-.0007** (3 sims of 4,067), 200-500 **+.0047**, 500-1000
  **+.0223**, 1000-2000 **+.0455**, >2000 **+.0476**. **No regression; monotone in jet pt.**
* **pT5 has VACATED the high-pt jet core.** Winning-TC type at 50-100 / 100-300 / >300 GeV: master
  1,290 / 653 / 90; previous ship 58 / 20 / 1; **now 7 / 0 / 0.** Above 50 GeV the jet core is carried
  entirely by bare chains and bare pLS. A future "pixel confirmation" lever has no pT5 supply up
  there -- though 75.8% of stage-f sims do have a >75% pLS of their own.

### (f) Master is no longer information on any jet axis

| axis cell | JPR2 delta to master | **NEW delta** |
|---|---:|---:|
| sim pt 10-20 | -.0216 | **+.0207** |
| sim pt 20-50 | -.0748 | **+.0088** |
| sim pt 50-100 | -.0783 | **+.0252** |
| sim pt 100-300 | +.0177 | **+.1331** |
| sim pt >300 | +.1945 | **+.2641** |
| \|eta\| < 0.6 | -.0211 | **+.0269** |
| genjet 1000-2000 | -.0166 | **+.0289** |
| genjet >2000 | -.0110 | **+.0366** |

Master still leads only in sim pt 2-5 (-.0042), \|eta\| 1.1-1.7 at dR [.02,.05) (-.0188 on 213 sims),
genjet 500-1000 at dR [.005,.02) (-.0131 on 229 sims) and the [0.4,inf) fine bin (-.0025, p .774) --
all unresolved or tiny. Core-all on the tune half is **.7807 -> .8120, +.0313 (p 2.4e-33)**.

---

## [JPR3 10:00] THE PRIORITISED LEVER LIST, RANKED BY EFFICIENCY

Duplicates appear only as a cost column, per the maintainer's directive.

| # | lever | efficiency ceiling | priced | dup cost column | verdict |
|---|---|---:|---|---|---|
| **L1** | **claim ORDER key** (stage f) | **+.0717**; recoverable **+.0578** | +.0224 (C's replay, two heads ago -- **stale**) | claim feeds the (chain, bare pLS) pair | **Still the biggest lever by 2.4x**, but HARDER: the thief is a real >=75% track .466 of the time (was ~.33), and **64.9%** of the recoverable subset's purest chains are 4-layer. |
| **L2** | **weld argmax re-key** (stage d) | **+.0297** | `WELD` refused the re-key in round 3; the sweep-count direction shipped instead | `weld_S02` cost dup<.05 .0313 -> .0345 | **The only lever that expands the REACH.** High-pt (.234/.252 of the >100 GeV budget) and close pairs (81% at dRnn<.005). Winner is a true edge .5847 of the time; median 31 competitors, p90 252, median margin +0.695. |
| **L3** | **gate** (stage e) | **+.0154** | `T4C1` took 63% of it | -- | **Nearly exhausted** (was +.0418). Separation +4.491 mX units. Not worth a round alone. |
| **L4** | **75%-purity repair** (stage g) | **+.0132** | `PUR-SB` +.0041 (new kernel, gates green); `PUR-T3` +.0095 but FAILS both cubes | -- | Ceiling HALVED this round untouched (was +.0253). PUR's refusal unchanged: **65.0% of the MDs that must be removed are carried by nothing else in the event.** Below the bar that declined a 4th weight file for +.02. |
| **L5** | upstream MD/LS/T3-pair residue | +.0188 | -- | -- | LST-carried; "no changes that help LST too" rejects it. |
| **L6** | **T4 class policy, second pass** | not priced on this head | pT4 port -16 core sims (p .72), attach-eligibility +0 -- **both on the PRE-`T4C1` head** | 124 of 378 core dup pairs have a 4-layer member | **Re-price flag, not a recommendation.** T4 core wins 1,016 -> 1,849 (+82%), 14.2 TCs/evt, 52.0% of core fakes, 64.9% of the claim's ceiling. Every T4 measurement predates an 82% population growth. |
| -- | a1 (<3 OT layers with a reco hit) | +.0369 | **zero** | -- | not ours, shared with master. |

### The currency, updated (tune half, per event)

| | MASTER | **NEW** | headroom |
|---|---:|---:|---:|
| fake TCs/evt, dR < .02 | 7.58 | **1.98** | 5.60 |
| fake TCs/evt, dR < .05 | 13.95 | **5.62** | **8.33** |
| fake TCs/evt, pooled | 27.69 | **14.39** | **13.30** |
| TCs/evt total | 132.8 | **125.1** | 7.7 |
| dup TCs/evt, dR<.05 (COST COLUMN) | 0.17 | **0.58** | -0.41 |
| stage-f budget, core sims/evt | | **3.14** | |
| its recoverable subset | | **2.53/evt** | |

**We can spend 2.65 admitted fake TCs per recovered core sim and still be cleaner than master on
jets.** Caveat: this budget exists on JETS only, PU200 has none, and T4 -- carrying two thirds of the
claim's recoverable ceiling -- is already 52.0% of core fake TCs at purity .375.

Duplicate cost column, record only: core duplicate groups 156 -> **378**, of which **359 (95.0%) have
a bare-pLS member**, **95.8% share zero OT hits**, chain-vs-chain population 15. Same object JPR2
located, 2.4x larger.

### CLOSED, verified still closed on this head
degree cap C=256; the 80 edge weld WPs; T3-DNN; `kChainMaxNodes`/occupancy/truncation; `dcaXY` as a
rank term; `orderAlpha`/`orderHinge` GLOBAL re-weighting (the shipped `KE50` eta RAMP is a different
object); density as a RANK score (-.17, independently reproduced here at AUC .517 for non-delivery);
bar/admission levers for PU200 fake; bar levers for jet-core dup; mutual-best pLS retirement;
dead-thief recovery; non-greedy assignment shape; confirmation-keyed claim rules; edge-head retrain;
attach retrain for RANKING; flat seed-bonus + purity-ranking union; lowering the 75% match bar;
fit-residual identification of stolen hits; the pT4 port (**flagged for re-price only**, L6).

---

## [JPR3 10:05] HOW FAR WE STILL ARE

We deliver **.8142** of jet-core sims on the tune corpus (.8087 sealed holdout). The weld already
builds a chain for **1,974 of the 3,659 we miss**, so the honest ceiling of "deliver what we already
build" is **.9144** -- ten points away, 4.4 core sims per event, and it is exactly stages e+f+g.
Inside dR .005 the distance is **+.319**; above sim pt 100 GeV it is **+.321**. LST master is at
.7807 on the same tune events and leads in no cell that matters, so it is no longer information.
**The frontier is a two-real-tracks-one-hit-set arbitration contest at high pt in close pairs, and
the single number that describes it is that 57.5% of everything we still lose has another selected
track within dR .005.**

-- JPR3. No candidate, no patch, no build, no commit, nothing published.
