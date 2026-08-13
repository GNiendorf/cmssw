# FINDINGS_JETPHYS2.md

CUMULATIVE JET-CORE RECON, ROUND-2 BASELINE (agent JPR2). Measurement and attribution only; no fix,
no tuning, no candidate, nothing committed. Posted by the coordinator on JPR2's behalf (subagents
return findings as text, not files).

Baseline: **`04c6e122e68`**. Every run used the PRISTINE ship binary already in the main tree,
`standalone/bin/lst_cpu` md5 `5293c48f2561a6968efecf4fd4b1270f`, `LST/liblst_cpu.so`
`e7a11d8115ca59a7d53a26db5ea8c7d5` -- byte-identical to AT's `at_ref/basebin/` and SF's
independently derived `sf_work/basebin/`. **No build, no worktree**:
`git diff 04c6e122e68 a607768b6e2 -- LSTCore/src LSTCore/interface standalone/code` is EMPTY, so the
main tree is the ship binary for physics.

Sample discipline: **jets TUNE half only** (rows 0-449 for the funnel corpus, 0-499 for rate runs).
Jets 500-999 and PU200 `event_2000` were never opened. The PU200 control is the ttbar timing file,
not a holdout. Artifacts: `standalone/jpr2_ref/`.

---

## [JPR2 00:20] THE FUNNEL RE-ATTRIBUTED: **the loss budget fell 6,747 -> 4,559 core sims and the K9 claim is STILL the largest single stage (.316), but its ABSOLUTE loss halved (2,928 -> 1,442) and the ranking below it reshuffled -- the weld and the 75%-purity bar are now bigger fractions than they were, and stage a1 (unrecoverable, shared with master) is the third-largest row.**

Caveats WITH the headline: the corpus is 450 tune events reduced with JPR's own instrument
(comparable, not re-derived); the corpus runs `-s 1 --allobj` and reads .7685 core efficiency against
the 500-event `-s 16` run's .7659 -- a +.0026 corpus offset that JPR's corpus also had (+.0022), two
orders below every effect.

Corpus: **450 jet tune events**, 0 failures, `jpr2_ref/fun/e{0..449}.npz`. Validations:
* offline K5 edge replay vs dumped `logOdds`: **max |dmX| = 9.03e-05 over 29,964,189 edges**
* delivery predicate vs the kernel's `[CHAIN K9] accepted=`: **99.95/evt offline vs 101.25/evt**, max per-event difference 12 of ~101
* dense chain-node index == ntuple `t3` row: asserted on all 6 hit rows, 450/450 events

### Q1 -- the funnel. 19,693 core sims, **4,559 unmatched** (was 6,747). Rows sum to the budget.

| first stage that loses an unmatched core sim | OLD n | OLD frac | **NEW n** | **NEW frac** | ceiling if fully recovered |
|---|---:|---:|---:|---:|---:|
| **f claim: gate-alive chain, no TC delivered** | 2928 | .434 | **1442** | **.316** | **+.0732** |
| **e gate: the welded chain is gate-killed** | 1428 | .212 | **823** | **.181** | **+.0418** |
| a1 hits: <3 OT layers carry a reco hit | 807 | .120 | 779 | .171 | +.0396 (recoverable **zero**) |
| **d weld: eligible but loses the argmax** | 646 | .096 | **599** | **.131** | **+.0304** |
| **g match: TC delivered, <=75% purity** | 481 | .071 | **499** | **.109** | **+.0253** |
| c0 no graph-adjacent pair among its T3s | 186 | .028 | 164 | .036 | +.0083 |
| a3 LS: no two >75% LSs share an MD | 170 | .025 | 156 | .034 | +.0079 |
| a2 MDs: <3 layers carry a >75% MD | 84 | .012 | 81 | .018 | +.0041 |
| b T3: no >75% T3 built | 15 | .002 | 14 | .003 | +.0007 |
| c1 the C=256 degree cap | 1 | .000 | **1** | **.000** | +.00005 |
| c2 the 80 edge weld WPs | 1 | .000 | **1** | **.000** | +.00005 |
| efficiency (450-event corpus) | | .6574 | | **.7685** | |

**A re-ranking, not a repeat.** The gate retrain took 1,486 sims out of the claim's row and 605 out of
its own, and NOTHING out of a1/a2/a3/b/c0 (upstream, untouched by construction) or out of stage g
(which grew, +18). The four stages worth funding are now f (.316) > e (.181) > d (.131) > g (.109),
with a1 (.171) sitting among them as a row that is **not ours** -- it rises monotonically with dR
(.083 of the core budget, .482 beyond dR .10), master loses the same tracks, and [D 21:45] priced its
recoverable ceiling at ZERO.

### The same table on the maintainer's fine bands (deep core and annulus are now different problems)

| band | denom | OLD eff | **NEW eff** | weld d | gate e | claim f | purity g | a1 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| dR < .005 | 1444 | .2445 | **.4086** | .181 | .213 | **.413** | .063 | .083 |
| [.005,.02) | 3556 | .3552 | **.5546** | .167 | .213 | **.358** | .120 | .088 |
| [.02,.05) | 3690 | .5770 | **.7523** | .118 | .214 | **.296** | .135 | .138 |
| [.05,.10) | 3686 | .7425 | **.8511** | .098 | .129 | .293 | .133 | .230 |
| >= .10 | 7317 | .8834 | **.9101** | .027 | .056 | .137 | .090 | .482 |

(columns = fractions of that band's own unmatched budget)

### The claim, measured again
**gate-alive 587.7 chains/evt -> K9-accepted 101.2/evt = the claim keeps .1723** (JPR: 1562.5 ->
104.5 = .0669). With the identical instrument on 30 PU200 events, **PU200 keeps .3938** (JPR measured
.3937 -- unchanged to four decimals). The off-distribution ratio fell from **5.9x to 2.3x**, and it
fell by *field-starving* (chains/evt still 8,133; gate-killed .8079 -> .9277), not by admitting more:
accepted/evt 104.5 -> 101.2.

The ordering is still adversarial, by half as much: for 1,398 stage-f core sims joinable to their
thief, **the thief out-ranks the victim on `orderKey` .7883 of the time** (JPR .738), **median
advantage +5.766** (JPR +11.2).

### The claim's measured recoverable ceiling (`jpr2_ref/ceiling.py`)
All 1,442 stage-f core sims have a gate-alive chain owning >=2 of their own >75% T3s. **903 (.6262)
have one whose ENTIRE pre-trim node run is their own T3s** -> **+.0459 absolute core efficiency**
(.7685 -> .8144); the whole stage-f budget is +.0732 (-> .8417). JPR's numbers were +.098 / +.149.
**New caveat riding with the ceiling: 42.4% of those purest chains are 4-LAYER** (5-layer .306,
6-layer .270), and a 4-layer chain is not an attach target (`kAttachMinLayers = 5`) and delivers at
.0035-.0014 today -- so nearly half of the claim's recoverable ceiling is reachable only by admitting
more T4, whose delivered non-fake fraction on jets is **.489**.

### Nulls RE-CONFIRMED on the new head
* **C=256 degree cap: 1 of 4,559.** 108,752 of 547,211 adjacent TRUE same-sim core T3 pairs (19.9%) still lose their row, all E1, still costing exactly one core sim.
* **the 80 edge weld WPs: 1 of 4,559.** 94.0% of TRUE adjacent core edges with a row are eligible (E1 .8493, E2 .9995). Relevant to **agent SF**: the pT>5 WP grouping can be a PU200 high-pT lever; it cannot be a jet-core lever.
* **T3-DNN / shared-T3 code: 14 of 4,559** (.003).

---

## [JPR2 00:30] THE REACH CEILING: **the weld's reach did not move a single decimal (it was untouched), so the honest remaining distance is now DELIVERY -- .8585 reached vs .7685 matched overall, and inside dR .005 the weld builds a chain for .8061 of core sims while .4086 are matched.**

Caveat WITH the headline: R8 is the CHAIN route's reach; R11 exceeds R8 beyond dR .10 because pT5/pLS
cover those tracks, so the reach gap is only meaningful inside dR .05. The master row is the
500-event `-s 16` run; the R-rows are the 450-event corpus.

| | <.005 | [.005,.02) | [.02,.05) | [.05,.10) | >=.10 | ALL |
|---|---:|---:|---:|---:|---:|---:|
| R5 an adjacent T3 pair EXISTS | .9204 | .9238 | .9195 | .9045 | .8601 | .8954 |
| R7 some such edge is ELIGIBLE | .9204 | .9235 | .9192 | .9045 | .8601 | .8953 |
| **R8 some such edge is WELDED (the reach)** | **.8061** | **.8400** | **.8816** | **.8801** | **.8553** | **.8585** |
| R9 its chain survives the gate | .6662 | .7247 | .8108 | .8459 | .8389 | .8017 |
| **R10 that chain is DELIVERED** | **.4030** | **.5332** | **.6970** | **.7759** | **.8133** | **.7039** |
| R11 matched, any route (= efficiency) | .4086 | .5546 | .7523 | .8511 | .9101 | .7685 |
| **delivery / reach, R10/R8** | **.500** | **.635** | **.791** | **.881** | **.951** | **.820** |
| ... the same ratio, OLD head | .315 | .394 | .562 | .721 | .910 | .675 |
| LST master's efficiency (tune, paired) | .3362 | .6064 | .7904 | .8557 | .9102 | .7807 |

**Updating JPR's headline number.** JPR: "the weld BUILDS a chain for .9208 of deep-core sims while
only .3167 were delivered". On `04c6e122e68`, at dR < .02: a weldable pair exists for **.9228**, one
is actually WELDED for **.8302**, and **.5074** are matched (500-event run). **We now deliver 55% of
what is weldable in principle and 60% of what is actually welded; it was 34% and 38%.**

**The honest distance.** Inside dR .05 the chain route reaches .855 and matches .609. That is **.246
of core efficiency built and thrown away**, against a .0148 gap to master. Master is not the frontier
in this cell and has not been since the ship: the frontier is 21-40 points of already-welded chains
that arbitration discards -- and in the deep core (<.005) we are **+.075 ABOVE master while still
delivering only half of our own reach**.

---

## [JPR2 00:40] THE AXIS NEITHER ROUND EXAMINED, AND IT RELOCATES THE PROBLEM: **the residual master gap is NOT "the deep core" -- it is sim pt 10-100 GeV, genjet pt >= 500 GeV, |eta| < 0.6, in the dR .005-.04 ANNULUS (8 consecutive fine bins, all negative). Above sim pt 300 GeV we beat master by +.1945; below genjet pt 500 GeV we beat master in every dR band.**

Caveats WITH the headline: 500-event tune half; the dR bins are paired McNemar
(`p4_ref/jetgate.py`), the three new axes are unpaired ratio tables on the same events and
denominators (`jpr2_ref/{ptaxis,jetptaxis,etaaxis}.py`), so treat cells with denominators under ~200
as indicative.

### (a) SIM pt. Master's route dies at high pt; ours dies in the middle.

| sim pt | denom | MASTER | **NEW** | delta | our winners |
|---|---:|---:|---:|---:|---|
| 0.9-2 | 2805 | .9159 | .9191 | +.0032 | pT5 1537, T5 483 |
| 2-5 | 3573 | .9298 | .9278 | -.0020 | pT5 2515, T5 424 |
| 5-10 | 3258 | .9064 | .9153 | +.0089 | pT5 1883, T5 833 |
| **10-20** | 3330 | .8805 | **.8589** | **-.0216** | T5 1489, pT5 1106 |
| **20-50** | 3971 | .7870 | **.7122** | **-.0748** | T5 2117, pT5 245 |
| **50-100** | 2185 | .6146 | **.5364** | **-.0783** | T5 695, T4 230, pLS 166 |
| 100-300 | 2036 | .3541 | .3718 | +.0177 | T5 382, T4 225, pLS 121 |
| **> 300** | 761 | .1932 | **.3876** | **+.1945** | T5 133, T4 114, pLS 45 |

The whole -.0148 core gap is (-.0216x3330 -.0748x3971 -.0783x2185) = **-540 sims at pt 10-100**,
partly repaid by **+148 sims above 300 GeV**. Master's pT5 route collapses above 100 GeV
(.3541 -> .1932); ours does not, because a bare chain needs no pixel seed.

### (b) The funnel on the same axis -- stage d (the weld) is a HIGH-pt stage, not a deep-core one

| fraction of the band's unmatched budget | 0.9-10 | 10-20 | 20-50 | 50-100 | 100-300 | >300 |
|---|---:|---:|---:|---:|---:|---:|
| d weld argmax | .012 | .059 | .112 | .160 | **.189** | **.237** |
| e gate | .110 | .222 | .185 | .207 | .182 | .184 |
| f claim | .084 | .234 | .316 | **.372** | **.421** | .385 |
| g 75% purity | .071 | **.194** | .151 | .119 | .081 | .043 |
| a1 (not ours) | **.497** | .194 | .140 | .081 | .082 | .110 |
| **R8 weld reach** | .8634 | .9099 | .8742 | .8353 | **.7863** | **.7452** |
| **R11 matched** | .9203 | .8576 | .7168 | .5505 | .3715 | .3880 |
| **reach - matched** | -- | .052 | .157 | **.285** | **.415** | **.357** |

R1-R8 are IDENTICAL, digit for digit, between the old head and the new one (nothing upstream of the
gate changed), so this table is a property of the graph and the edge head. Two things not previously
in the record: **the weld's own reach starts failing above 100 GeV** (R5 .9142 -> R8 .7863, i.e.
13-16 points lost in the argmax, against 0.3 points below 10 GeV), and **the largest
reach-minus-delivery gap in the sample is at sim pt > 100 GeV (.36-.42) -- where master delivers
.354/.193 and therefore offers no reference at all.**

### (c) GENJET pt -- the two-round record describes only the extreme tail, and we are already ahead outside it

| genjet pt | denom | MASTER | **NEW** | delta | delta dR [.005,.02) | delta [.02,.05) |
|---|---:|---:|---:|---:|---:|---:|
| 50-200 | 4067 | .9208 | **.9277** | **+.0069** | +.0159 | +.0261 |
| 200-500 | 2357 | .9109 | **.9164** | **+.0055** | +.0378 | +.0101 |
| 500-1000 | 1751 | .8869 | .8761 | -.0109 | -.0655 | -.0029 |
| 1000-2000 | 14916 | .7887 | .7721 | -.0166 | -.0688 | -.0438 |
| > 2000 | 7003 | .7637 | .7527 | -.0110 | -.0419 | -.0461 |

Every table in the three prior findings files selects `genjet_pt > 1000`. **On the 200-500 GeV jets
in the same file we are ahead of master in the annulus by +.038, and on 50-200 GeV jets by +.016.**
The deficit exists only for genjet pt >= 500.

### (d) |sim eta| -- the residual gap is CENTRAL BARREL

| \|eta\| | denom | MASTER | NEW | delta | delta dR [.005,.02) |
|---|---:|---:|---:|---:|---:|
| 0-0.6 | 14609 | .7966 | .7755 | **-.0211** | -.0765 |
| 0.6-1.1 | 5888 | .7655 | .7653 | -.0002 | -.0276 |
| 1.1-1.7 | 1291 | .6638 | .6514 | -.0124 | -.0239 |
| 1.7-4.5 | 131 | -- | -- | +.0000 | -- |

---

## [JPR2 00:50] WHERE MASTER IS STILL AHEAD, AND THE DUPLICATE CELL IS PROVEN UNREACHABLE BY THE SHIPPED RULE: **0 of the 150 core (chain TC, bare pLS) duplicate pairs lie inside `dupXcDelta`'s window -- 96.7% are BARREL (|eta| < 1.1) with a median pLS pt of 73.7 GeV, i.e. the population is the exact complement of the rule's (|eta| >= 1.1, pt < 3 GeV) window on BOTH legs.**

Caveat WITH the headline: this confirms the rule is inert here BY CONSTRUCTION, exactly as the ship
commit claimed; it also means the cell is untouched, and any future lever for it must act in the
barrel above 10 GeV -- precisely where a global loosening was measured to cost efficiency ([D 21:45]).

### The two cells where master leads (tune, paired, `jpr2_ref/tune3.txt`)

| cell | MASTER | NEW | delta | p |
|---|---:|---:|---:|---:|
| eff core-all | .7807 | .7659 | **-.0148** | 4.2e-08 |
| eff [.005,.0075) ... [.03,.04) | -- | -- | **-.043 to -.096, all 8 bins negative** | 3e-11 to .05 |
| eff [.04,.05) and every band beyond | -- | -- | +.0017 to -.005 | unresolved |
| dup core (<.05) | .0063 | .0313 | **+.0250** (0.17 -> 0.47 TC/evt) | 8.7e-19 |
| dup [0,.005) | .0000 | .1018 | +.1018 | 2.8e-06 |
| dup pooled | .0223 | .0246 | +.0023 | .011 |

Everything else on jets is ours: fake pooled .2220 -> **.1317**, core fake .5066 -> **.3766**, core
fake TCs/evt 13.95 -> **5.66**, TCs/evt 132.8 -> 123.1, eff dR<.005 .3362 -> **.4108**.

### The duplicate anatomy on the new head (`jpr2_ref/anat_NEW.txt` + `dupregion.py`; D's validated reader, flag reproduced with 0 mismatching TCs)

| jets tune, 500 evt | SHIP (pre-round) | J25C (round-2 arm) | **04c6e122e68** | master (1000 evt) |
|---|---:|---:|---:|---:|
| core (dR<.05) duplicate groups | 63 | 227 | **156** | 84 |
| ... with a bare-pLS member | 61 (96.8%) | 220 (96.9%) | **150 (96.2%)** | 8 (9%) |
| ... T5+pLS / T4+pLS | 60 / 1 | 171 / 49 | **117 / 32** | 4 / 0 |
| ... chain-vs-chain pairs | 0 | 3 | **2 (1.3%)** | 73 (T5+T5) |
| core pairs sharing 0 OT hits | 96.8% | -- | **96.8%** | -- |
| **inside the shipped rule's window** | 0/61 | -- | **0/150** | -- |
| bare-pLS member: barrel / transition / endcap | 59/2/0 | -- | **145/5/0** | -- |
| bare-pLS member pt p10/p50/p90 | 6.2/29.2/380.8 | -- | **15.8/73.7/461.3** | -- |

**D's mechanism is confirmed unchanged, and the population is now precisely located: barrel,
high-pt, hit-disjoint.** For **agent AT**: the rows your retrain must move are BARREL pairs at a
median 74 GeV, not the endcap sub-3-GeV rows D's shipped rule handles -- `dupXcDelta` and your arm
are disjoint by construction, and the barrel-band `xcTheta` is the only bar that reaches this cell.
Note the T4 half: 32 of 150 pairs have a 4-layer chain member, whose seed is never written to
`plsBest` at all (`kAttachMinLayers = 5`), so only the -XC arm can retire it.

---

## [JPR2 01:00] THE CURRENCY CHANGED, AND THIS IS THE ROUND'S MOST ACTIONABLE NUMBER: **we are 8.29 core fake TCs/evt and 12.65 pooled fake TCs/evt BELOW master, while the entire remaining claim budget is 3.20 core sims/evt -- so the next round can afford up to 2.6 admitted fake TCs per recovered core sim and still not reach master's jet fake rate.**

Caveat WITH the headline: this is a BUDGET, not an exchange rate. Round 1 measured that admitting
chains without a better ranking converts efficiency into fake ~one-for-one, and PU200
`fake_overall` is already +3.4% relative from this ship -- the budget exists on the JET sample and
PU200 has none, so any admission lever still has to be density- or region-conditioned to spend it.

| currency, tune half, per event | MASTER | NEW | our headroom |
|---|---:|---:|---:|
| fake TCs/evt, dR < .02 | 7.58 | 1.79 | **5.79** |
| fake TCs/evt, dR < .05 | 13.95 | 5.66 | **8.29** |
| fake TCs/evt, pooled | 27.69 | 15.04 | **12.65** |
| TCs/evt total | 132.8 | 123.1 | **9.7** |
| dup TCs/evt, dR < .05 | 0.17 | 0.47 | **-0.30 (we owe)** |
| the whole stage-f budget, in core sims/evt | | **3.20** | |
| the claim's measured-recoverable subset | | **2.01/evt** | |

### The gate's own opinion is no longer the problem it was
The retrained gate separates core FAKE chains from DELIVERED TRUE core chains by **+5.055 mX units**
(fake median +0.463, true median +5.518). JPR measured **+0.81** on an 11-unit spread. The core-fake
entry cell also moved: branch 3 (5+ exempt, the displaced-exempt cell) was **59.1%** of core fakes
and is now **20.2%**; the leaders are branch 2 (5+ IP) **39.1%** and branch 1 (T4 exempt) **31.3%**.
**The "core fakes look displaced and enter the exempt branch" story is retired by measurement.**

Core fakes fell 9.6 -> **5.5 TCs/evt**, and their composition inverted: T5 .768 -> **.492**,
**T4 .161 -> .375** (1.55 -> 2.07 fake T4/evt, the only class that grew in absolute terms).

### One head is now on-distribution and the other is not
With the identical instrument on 30 PU200 events, the retrained gate is **indistinguishable from the
old one on PU200**: gate-killed .5996 -> .5988, branch mix identical to three decimals, mX medians
within 0.15, claim selectivity .3937 -> .3938. **The retrain moved the head only on jets.** So
"off-distribution" is now much narrower than JPR's statement: the CLAIM's jets/PU ratio is 2.3x (was
5.9x) and the GATE's jet kill rate is .9277 vs PU200's .5988 -- but the EDGE head is exactly where
JPR left it (jets true-edge prevalence .000788 vs PU200 .1279, a **162x** prior shift, with
eligibility HIGHER on jets: E1 .3054 vs .2382, E2 .9900 vs .9268).

---

## [JPR2 01:10] THE PRIORITISED LEVER LIST, CEILINGS MEASURED ON `04c6e122e68`, CLOSED DOORS MARKED

Ceilings are absolute jet-core efficiency points on the 450-event tune corpus. "Priced" = somebody
deployed or replayed it in the gate-retrained world.

| # | lever | ceiling | priced value | verdict |
|---|---|---:|---:|---|
| **L1** | **claim ORDER key** (trained ranker on the 25 existing chain features, C arm 5) | +.0732 stage f; **+.0459** measured-recoverable | **+.0224** (C's replay, A02 world) | funds a 4th weight file for ~+.02. Maintainer call, currently declined. Cheap/linear forms **do not transfer** (-.015). |
| **L2** | **attach / pT5 route** (pixel confirmation before, or inside, the claim) | +.0418 (e) + part of f; **78.0%** of stage-f sims have a >75% pLS | **+.018** (EA, structural, A02 world) | the only lever aimed at the pt 10-100 cell master owns. "Offer everything" is CLOSED (-.029). Needs a contention-safe design, not a wider target list. |
| **L3** | **gate retrain, again** (on-policy, after AT) | +.0418 (e) | -- | separation is already +5.055 and PU200 is untouched, so stage-e is thinner than it looks. Fake entry has moved to branch 2 / branch 1 -- a next retrain should target THOSE cells. |
| **L4** | **weld argmax re-rank** (one slot, better key) | +.0304 (d) | -- | **re-priced as a HIGH-pt lever**: .189/.237 of the pt>100 budget vs .012 below 10 GeV, and R5->R8 loses 13-16 points there. Winner is a true edge only .5828 of the time; median 31 competitors, p90 252. Cheapest untried lever with a mechanism. |
| **L5** | **75%-purity repair** (stage g) | **+.0253** (was +.0243) | +.024 (D, "one mini-doublet") | it GREW (.071 -> .109 of the budget) while everything else shrank; 99.2% of stage-g owners are FAKE TCs. Now 5th-largest and rising. |
| **L6** | **T4 class policy** (nobody has priced it) | -- | -- | T4 = 10.1 TC/evt at non-fake **.489**, wins 1,016 core sims, is **37.5%** of core fakes, 32 of 150 core dup pairs, 73% of the PU200 fake excess, and **42.4%** of the claim's recoverable ceiling. Nobody has asked whether the class is net-positive on jets. |
| -- | c0/a2/a3 (upstream MD/LS residue) | +.0203 combined | -- | LST-carried code; "no changes that help LST too" rejects it. |
| -- | a1 (<3 OT layers with a reco hit) | +.0396 | **zero** ([D 21:45]) | not ours, shared with master, rises with dR. |

**CLOSED WITH NUMBERS -- do not re-derive:** degree cap C=256 (1 of 4,559); the 80 edge weld WPs
(1 of 4,559 -- **SF's territory**, so SF's lever cannot be a jet-core lever); T3-DNN (14 of 4,559);
`kChainMaxNodes`/occupancy/truncation (all zero); `dcaXY` as a rank term (gated form core -.0493);
`orderAlpha`/`orderHinge` (null at every value); density as a RANK term (-.17); bar/admission levers
for PU200 fake (chain admission ratio 1.0011); bar levers for jet-core dup (4 variants identical to
4 dp; the 2-D family's best exchange is 0.9 dup removed per core sim LOST); mutual-best pLS
retirement (inert, bars 3.2 logits below the delivery margin); dead-thief recovery (0 of 6,212);
non-greedy assignment (+.0012); confirmation-keyed claim rules (0 of 1,074 confirmed challengers are
displaced); edge-head retrain (gate's 25 features already reach .968 vs .972 with all 40 edge
inputs); attach retrain for RANKING (pair AUC .996/.9996); P1 flat-bonus + purity-ranking union
(antagonistic).

---

## [JPR2 01:15] WHAT NOBODY HAS LOOKED AT

1. **Sim pt, genjet pt and |eta|** -- unexamined until this post; all three relocate the problem. The
   most consequential: **above sim pt 100 GeV nobody delivers** -- we are at .372/.388, master at
   .354/.193, and the weld reaches .786/.745. A 36-42 point reach gap with no external reference,
   which is exactly what "LST is the floor" points at.
2. **Softer jets.** The entire two-round record is `genjet_pt > 1000`. At 200-500 GeV we are already
   ahead of master in the annulus by +.038. Nobody has checked whether a >1 TeV-tuned lever
   REGRESSES the 200-500 GeV population -- same file, free to measure, and it dominates real data.
3. **The T4 class as a policy question** (L6): simultaneously 6% of core efficiency, 37.5% of core
   fakes, 21% of core duplicate pairs and 42% of the claim's recoverable ceiling.
4. **Stage g (purity) is growing:** 481 -> 499 sims while the budget fell 33%. A delivered-TC defect,
   not an arbitration one, untouched since D's single-mini-doublet diagnosis.
5. **The weld at high pt.** Round 1 filed stage d as a deep-core-dR effect (.129 of the <.02 budget).
   On the pt axis it is monotone in pt, reaches .237 above 300 GeV, and the weld's own REACH breaks
   there. Nobody has looked at what the argmax loses to at high pt.
6. **The barrel high-pt (chain, bare pLS) duplicate** -- located here for the first time; untouched by
   the shipped rule, in the same barrel/high-pt region as the PU200 population every dup lever must
   protect.
7. **Not examined by anyone, including me:** the round-2 ship was NOT timed (chains/evt unchanged at
   8,133 but delivered chains and TC counts moved), and the `-J` histogram path still cannot see the
   battleground (36% of core sims sit beyond its dR axis).

### Reproduce
```
jpr2_ref/launch.sh 0 449 12                      # the funnel corpus (per-event run+reduce+delete)
jpr2_ref/agg.py  jpr2_ref/fun --tag jets2_450    # JPR's 6-band table, fake anatomy, off-distribution
jpr2_ref/agg2.py jpr2_ref/fun --tag jets2_450    # fine-dR + sim-pt funnel, reach, pixel evidence
jpr2_ref/agg2.py archive/jpr_ref/fun --tag OLD   # the SAME tables on the old head's corpus (free)
jpr2_ref/ceiling.py jpr2_ref/fun ; jpr2_ref/valid_deliv.py jpr2_ref/fun
p4_ref/jetgate.py --split tune m3_ref/jpr_master_jets1000.root at_ref/runs/BASE_jet.root \
    p4_ref/meas/SHIP_jets.root --labels MASTER,NEW,SHIP
jpr2_ref/{ptaxis,jetptaxis,etaaxis}.py --split tune <master.root> <new.root> --labels MASTER,NEW
d2_ref/dupanat.py at_ref/runs/BASE_jet.root --label NEW ; jpr2_ref/dupregion.py <root>
```

| artifact | what |
|---|---|
| `jpr2_ref/fun/` (450 npz+census+log), `funpu/` (30) | the funnel corpora, new head |
| `jpr2_ref/funnel2_450.txt`, `funnel2b_450.txt`, `funnel_old_450.txt`, `funnel2_pu.txt` | aggregates (new; new fine/pt; OLD head re-aggregated; PU200 control) |
| `jpr2_ref/tune3.txt/.json` | master vs new vs pre-ship, paired, fine bins, tune half |
| `jpr2_ref/{ptaxis,jetptaxis,etaaxis}.txt` | the three new axes |
| `jpr2_ref/anat_NEW.txt`, `dupregion.py` | the duplicate anatomy and the rule-reach proof |
| `jpr2_ref/{ceiling,valid_deliv}.txt` | the claim ceiling and the delivery-predicate validation |

-- JPR2. No candidate, no patch, no commit, nothing published. 771 MB in `jpr2_ref/`.
