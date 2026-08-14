# FINDINGS_JETPHYS5.md

CUMULATIVE JET-CORE RECON on the CURRENT head (agent JPR5). Measurement, attribution and
re-pricing only: **no candidate, no patch, nothing committed, nothing published.**

Baseline **`ba9e42ec9e5`** (D128). Three binaries: the head (main tree `bin/lst_cpu` md5
`4804af6b3a1009a543056193db33b07f`); the pre-D128 control (`w5_ref/bin1`, no weld env set); and the
re-pricing instrument (`gpu_wt/j5` md5 `d4dcd8c338a764a3a5c132e5b5097dc7` = `ba9e42ec9e5` + N4's
`t4_fardens.patch` + G5's `g5_gatebars.patch` + C5's `c5_braid_ipwaiver.patch`, all applied cleanly,
0 `error:`). **Inertness PROVED:** the zero-knob arm `J5C` is BIT-IDENTICAL to the pristine main-tree
binary on all ten branches over 500 jet TUNE events.

Sample discipline: jets TUNE only (0-449 offline, 0-499 deployed); **jets 500-999 NEVER OPENED**;
PU200 = `event_1000.root` by explicit path; `event_2000..7000` never opened. **Nothing timed.**
Artifacts `jpr5_ref/` (3.2 GB). Deployed A/Bs were run for Q4 and are declared as such.

Instruments, each validated against the kernel ON THIS HEAD: the funnel (offline K5 edge replay vs
dumped `logOdds`, max |dmX| 9.03e-05; the BASE corpus reads **.8217**, JPR4's number to four
decimals); the claim replay (reproduces `[CHAIN K9] accepted=` **exactly on 450/450 events**); and a
new contested-slot corpus (**the D128 weld replay equals the kernel's own welded set with 0
mismatches over 268,787,666 edge rows**).

---

## [JPR5 00:20] Q1: **the budget fell 3,511 -> 3,089 and D128 spent it in two stages and NOWHERE ELSE -- the weld argmax collapsed 591 -> 129 (-78%) and the gate 229 -> 144 -- but it also DOUBLED a stage nobody was watching: 75%-purity failures went 210 -> 430 (+105%), now the third-largest budget and the single largest thing this head created.**

Caveats WITH the headline: 450 tune events, 19,693 core sims; corpus reads .8431 against the
500-event deployed .8413, a +.0018 offset (JPR4 +.0020).

| first stage that loses an unmatched core sim | JPR3 | JPR4 | **NEW n** | **frac** | **ceiling** | move |
|---|---:|---:|---:|---:|---:|---|
| **f claim** | 1411 | 1379 | **1279** | **.414** | **+.0649** | -7.3% |
| a1 hits (not ours) | 727 | 726 | 729 | .236 | +.0370 (recoverable **zero**) | unmoved |
| **g match: <=75% purity** | 260 | 210 | **430** | **.139** | **+.0218** | **+105%** |
| c0 no graph-adjacent pair | 148 | 149 | 148 | .048 | +.0075 | unmoved |
| **e gate** | 303 | 229 | **144** | .047 | +.0073 | **-37%** |
| a3 LS | 135 | 137 | 139 | .045 | +.0071 | unmoved |
| **d weld argmax** | 585 | 591 | **129** | **.042** | **+.0066** | **-78%** |
| a2 MDs / b T3 / c1 cap / c2 WPs | 77/11/1/1 | | 77/12/1/1 | | +.0039/+.0006/+.00005/+.00005 | unmoved |
| **efficiency (450-event corpus)** | .8142 | .8217 | **.8431** | | | |
| **TOTAL BUDGET** | 3659 | 3511 | **3089** | | **+.1569** | **-12.0%** |

**The published mechanism is confirmed and INCOMPLETE.** D128 did not say it manufactures purity
failures, and it does: **+220 core sims into stage g, 52% of the budget reduction handed back.**

The claim still owns ~.55 of the budget in every bin below dR .01. The weld's flat .21-.24 share
below dR .02 is **gone** (now .03-.09); what replaced it is the purity stage at .13-.18.

**2,149 of 3,089 (69.6%) are above 20 GeV.** The whole D128 gain landed there: +.0250 / +.0662 /
+.0908 / +.0790 at 20-50 / 50-100 / 100-300 / >300 GeV.

---

## [JPR5 00:25] Q2: **THE REACH LADDER MOVED FOR THE FIRST TIME IN FOUR RECONS -- R8 .8536 -> .8853 (+.0317), +.0942 in the deepest band -- while R5/R7 are unchanged to four decimals in every band. The identity SURVIVES EXACTLY: unmatched-and-welded = 1,853 = 144 + 1,279 + 430.**

| | <.005 | [.005,.02) | [.02,.05) | [.05,.10) | >=.10 | **ALL** | JPR4 |
|---|---:|---:|---:|---:|---:|---:|---:|
| R5 adjacent pair EXISTS | .9204 | .9238 | .9195 | .9045 | .8601 | **.8954** | .8954 |
| R7 some edge ELIGIBLE | .9204 | .9235 | .9192 | .9045 | .8601 | **.8953** | .8953 |
| **R8 WELDED (reach)** | **.8871** | **.9064** | **.9100** | **.8956** | **.8570** | **.8853** | .8536 |
| R9 survives the gate | .8636 | .8920 | .8940 | .8812 | .8412 | **.8694** | .8324 |
| **R10 DELIVERED** | .6053 | .7191 | .7997 | .8209 | .8222 | **.7832** | .7355 |
| R11 matched (= efficiency) | .5935 | .7340 | .8482 | .8885 | .9200 | **.8431** | .8217 |
| delivery/reach R10/R8 | .682 | .793 | .879 | .917 | .959 | **.885** | .862 |
| **UNION = ceiling** | .8982 | .9322 | .9458 | .9493 | .9370 | **.9372** | .9140 |
| **distance still to run** | **+.3047** | **+.1983** | **+.0976** | **+.0608** | **+.0169** | **+.0941** | +.0923 |

**The distance to run did NOT fall: +.0923 -> +.0941.** The ceiling rose .0232 and the efficiency
.0214, so D128 moved the whole ladder up without closing the build-vs-deliver gap. **That gap is
still the whole game: 4.118 core sims/event.** On the pt axis: +.2242 at 50-100, +.2945 at 100-300,
+.3309 above 300 -- every one bigger than JPR4's.

**Stage c0, measured and CLOSED (new).** Censused every non-adjacent overlapping pair of a c0 sim's
own >=75% triplets, 40 events: **2,212 pairs, shared-MD position matrix PERFECTLY DIAGONAL -- 546
md0<->md0, 1,206 md1<->md1, 1,502 md2<->md2, ZERO off-diagonal.** They are **parallel alternatives
on the same three layers**, not a chain the definition failed to link. **No third edge family exists
to add.** So stage d is again the only reach-expanding lever we own, and it is 78% spent.

---

## [JPR5 00:35] Q3, THE OPEN MECHANISM QUESTION, SETTLED: **D128's gain is NOT mainly a reach gain and NOT an argmax-accuracy gain -- 71.1% of the 972 recovered sims were ALREADY WELDED under the shipped key and were recovered DOWNSTREAM, at the CLAIM. The mechanism is chain ASSEMBLY: an E2 step advances one mini-doublet and an E1 step two, so promoting E2 at a dense junction lets MORE of a sim's own triplets land in ONE chain.**

Caveats WITH the headline: funnel comparison is 450 jets TUNE events, sims paired one-to-one; the
contested-slot table is 48 events; **nothing here is PU200 or cube evidence.**

Paired, 450 events. **RECOVERED 972, LOST 550, net +422 = +.0214** (deployed +.0216).

| BASE stage of a RECOVERED sim | n | frac | | D128 stage of a LOST sim | n | frac |
|---|---:|---:|---|---|---:|---:|
| **f claim** | **569** | **.585** | | f claim | 332 | .604 |
| **d weld argmax** | 262 | .270 | | **g purity** | **150** | **.273** |
| e gate | 62 | .064 | | e gate | 28 | .051 |
| g purity | 60 | .062 | | d weld | 16 | .029 |

**Only 27.0% of the gain is the stage D128 was aimed at. 58.5% is the claim.**

| | n | frac |
|---|---:|---:|
| recovered sims **ALREADY welded** under BASE (downstream) | **691** | **.711** |
| recovered sims that **gained** a same-sim weld (reach) | 259 | .266 |
| recovered that LOST their same-sim weld and matched anyway | 0 | .000 |

**R1 through R7 flip ZERO sims in either direction** -- verified, not assumed.

What changed on a recovered sim (paired means, BASE -> D128): its own T3s / adjacent pairs / edge
rows / eligible edges are **identical, all four**; **same-sim edges WELDED 2.03 -> 5.33**; gate-alive
chains carrying it 1.77 -> 4.99; **delivered chains carrying it 0.068 -> 1.94**; best carrying
chain's `mX` 2.42 -> 4.59. **The supply is bit-for-bit unchanged and the consumption nearly triples.**

The delivered chain of a recovered sim: nLayers **4.72 -> 5.28**, 6-layer-plus **.215 -> .476**,
own/run purity **.506 -> .888**, `mX` median 2.76 -> 4.66. **A longer, dramatically purer chain with
a much better head score is what wins a claim contest.**

**THE TENSION WITH W5 IS A SECOND SIMPSON'S PARADOX, IN THE OPPOSITE DIRECTION.** W5's table is per
eligible edge ROW; the weld chooses at a SLOT, and family order can only matter where the slot
carries BOTH families. Census: slots carrying an eligible E1 are 29,803/evt and **97.8% also carry
an E2**; slots carrying an eligible E2 are 120,885/evt and only **24.1%** also carry an E1. Three
quarters of E2 slots have no E1 competitor, and W5's marginal averages over them. Restricted to
where the decision is made:

| degProd | contested slots | P(top E1 TRUE) | P(top E2 TRUE) | **LR(E2/E1)** |
|---|---:|---:|---:|---:|
| 1 | 2,774 | .9813 | .9874 | 1.01 |
| 8-15 | 3,060 | .5108 | .5644 | 1.10 |
| 64-127 | 3,457 | .0986 | .1198 | 1.21 |
| 4k-16k | 78,355 | .0088 | .0105 | 1.19 |
| **>=16k** | **1,246,511** | **.00080** | **.00205** | **2.53** |
| **>= the knee 128** | 1,376,264 | .0022 | .0034 | **1.59** |
| < the knee 128 | 22,829 | .5776 | .6101 | 1.06 |

**LR(E2/E1) is above 1 in every one of the twelve decades and largest at and above the knee.** Both
tables are correct; they are different conditionals. An eligible E1 drawn at random is better
evidence, because E1 rows concentrate where the geometry is unambiguous -- but **where a choice must
be made, the best E2 at that slot beats the best E1 at that slot**, and the margin grows with
occupancy.

Cost side of the same mechanism: welded edges 7,568 -> **10,146/evt (+34%)**, same-sim 295 -> 336
(+14%), **weld purity .0390 -> .0331 (-15%)**; the delivered chain of a stage-g core sim has
own/run purity **.708 -> .639** and the fraction that is entirely the sim's own triplets falls
**.175 -> .044**.

---

## [JPR5 00:40] STAGE g, OPENED FOR THE FIRST TIME: **the 430 core sims that reach a TC and are not credited sit in a TC that is FLAGGED FAKE 97.2% of the time, and 223 of the 430 (51.9%) sit at pMatched EXACTLY 0.75 -- nine of twelve hits on a 6-layer T5, ONE contaminating mini-doublet short of the bar. Ceiling +.0218 for the stage, +.0113 for the on-the-boundary half, and those TCs are 0.92 fake TC/evt (6.7% of the pooled jet fake budget) today, so the same edit pays twice.**

Caveats WITH the headline: read from the TC side (`bestTCfrac` is 0 for stage-g sims by writer
construction); **no arm built, none proposed** -- whether the address is the trim, the attach or the
weld is a round question.

| | BASE | **D128** |
|---|---:|---:|
| stage-g core sims | 210 | **430** |
| holder TC is flagged FAKE | .976 | **.972** |
| holder is T5 / pT5 / T4 | .748/.057/.195 | **.914**/.028/.058 |
| holder's own best-sim fraction, mean | .715 | .716 |
| holder OT hits / layers, median | 12 / 6 | **12 / 6** |
| **holder at pMatched EXACTLY 0.75** | 110 (.524) | **223 (.519)** |
| the sim's own delivered chain, own/run purity | .708 | **.639** |
| … fraction 100% the sim's own T3s | .175 | **.044** |
| distinct holder TCs/evt / of which fake | -- | **0.95 / 0.92** |

Of D128's 430: 43 were stage d under BASE, 18 at e, 116 at f, 128 already stage g, and **125 WERE
MATCHED**. A 12-hit 6-layer TC that is 9/12 one particle is one contaminating mini-doublet from
9/10 = 0.90. The contamination is **in the chain**, not added downstream.

---

## [JPR5 00:50] Q4: THE PARKED WORK RE-PRICED, ALL DEPLOYED ON THE D128 HEAD

Control `J5C` = .8413 core, 13.76 fake TC/evt, 2.37 dup TC/evt. **NOT re-measured: the cubes and the
sealed halves.**

| arm | jets core | delta | lost/gained | p | dR<.005 | **fake TC/evt** | **dup TC/evt** |
|---|---:|---:|---|---:|---:|---:|---:|
| `ATT4` | .8418 | +.0005 | 1/12 | .0034 | +.0000 | -0.03 | +0.00 |
| `F100` | .8417 | +.0004 | 0/8 | .0078 | -- | **-2.10 (-15.3%)** | +0.00 |
| **`UN4`** | .8422 | +.0009 | 1/20 | 2.1e-05 | +.0016 | **-2.12 (-15.4%)** | **+0.00** |
| `G5H` | .8425 | +.0011 | 11/36 | 3.5e-04 | +.0019 | **-1.86** | +0.02 |
| `G5U` | .8426 | +.0013 | 22/50 | .0013 | +.0012 | -1.49 | +0.05 |
| `W01` | .8466 | +.0052 | 62/177 | 5.3e-14 | +.0186 | +0.07 (p .83) | +0.27 |
| **`X005`** | .8526 | **+.0113** | 121/369 | 3.4e-30 | **+.0429** | +0.67 | **+0.79 (+33%)** |
| **`XU` = X005+UN4** | **.8534** | **+.0121** | 122/387 | 3.9e-33 | +.0435 | **-1.25 (-9.1%)** | +0.79 |
| `XG` = X005+G5U | .8536 | +.0123 | 147/416 | 9.8e-31 | +.0447 | -0.43 | +0.86 |

PU200 `event_1000`, paired:

| arm | overall | vxy[10,30) | dxy[1,5) | dxy[5,10) | dxy[10,30) | fake | dup |
|---|---:|---:|---:|---:|---:|---:|---:|
| `UN4` | **+.0010** (2/75) | **0/0** | **0/0** | **0/0** | **0/0** | **-.00079** | +.00005 |
| `F100` | **0 discordant EVERY cell** | 0 | 0 | 0 | 0 | -.00003 | **+.00000** |
| `X005` | +.0001 (4/14) | +.0002 | 0/0 | 0/0 | 0/0 | +.00004 | +.00081 |
| **`XU`** | **+.0011** (6/89, p 4.7e-20) | +.0002 | **0/0** | **0/0** | **0/0** | **-.00077** | +.00096 |

**`UN4` rose AGAIN and is the largest clean FAKE object in the pipeline** -- the far half alone
removes **2.10 fake TC/evt, 15.3% of the jet fake budget**, for +.0004 core, **zero PU200 discordant
sims in every cell**, **zero** duplicate cost. Priced -1.80 (round 4), -1.63 (round 5), **-2.10
now**, because D128 manufactures more far-dca 4-layer chains. Two constants.

**`X005` held 89% of its value; its fake cost fell and its DUPLICATE cost grew** (+29% -> **+33%
pooled**, deep-core dup rate .0984 -> .1199, +.0215 p .013). **Both maintainer objections survive
re-pricing.**

**`G5U` is CORRECTED and the ordering INVERTED: `G5H` now dominates it** on every axis -- same
efficiency, more fake removed (-1.86 vs -1.49), less duplicate cost. Both dominated by `UN4`.

**`N1C`/`P11`: population GREW 45%** (476.3 -> 692.3 rejections/evt), bad:good 28.7 -> **34.2**.
Standing unchanged, arithmetic worse.

**THE UNION IS THE FINDING: `XU` is 99% additive on efficiency (+.0121 measured vs +.0122 summed)
and it converts X005's fake DEBIT into a fake CREDIT: 13.76 -> 12.51 TC/evt, -9.1%.** Its only cost
is duplicates. `XG` is dominated by it.

---

## [JPR5 00:55] Q5 bullet 1, A CLEAN MEASURED NEGATIVE: **`ptEst` is a pT METER, not a realness separator. Pooled AUC .862 / LR 6.69 is a base-rate ladder: inside a band of its own value the AUC collapses to .43-.67, median .51 -- no ranking information at all once the pT band is fixed. R4 does NOT re-open.**

Count bar's rejected pool on this head: **692.3/evt** (JPR4 476.3), GOOD .0284, **bad:good 34.2**.

| separator | AUC | best LR |
|---|---:|---:|
| **`ptEst`** | .862 | 6.69 |
| `\|dcaXY\|` | .858 | 6.02 |
| `mX` | .810 | 3.16 |
| **`ChainFeatures[14]` density** | **.479** | **1.04** |

Within a band of `ptEst` itself: AUC **.491 / .666 / .559 / .564 / .581 / .505 / .509 / .433** across
the eight bands while the base rate climbs 73-fold. **It selects a population; it does not rank
inside one.** And the base rate TURNS OVER above 300 GeV (.177 -> .116). The reason: median
|log(ptEst/simPt)| runs .021 -> .054 -> **.105** (50-100) -> **.233** (100-300) -> **.572** (>300),
with within-band correlation to true pT falling .857 -> .511 -> .486 -> **.143**. **Problem #14,
confirmed as a number on our own objective.**

---

## [JPR5 01:00] Q5 bullet 2, THE POSITIVE RESULT OF THIS RECON: **"my blocker is the SAME PARTICLE as me" IS identifiable at claim time -- AUC .986 on a HELD-OUT half from 16 columns the claim already computes, and AUC .850 / LR 13.8 from ONE of them (`primn`, how many of my claimed hits my blocker already owns). The DIFFERENT-PARTICLE class, which is what a resolver acts on, is selectable at LR 34.1 with 88.5% recall.**

Caveats WITH the headline: offline claim-replay currency, so deployed value will be smaller;
**part of the power is near-definitional** (a chain of my own sim necessarily shares many hits with
me) -- a feature for a resolver, a caution for reading the AUC as physics; nothing deployed.

6,192 jet-core victim rows on 1,639 distinct core sims (3.64/evt).

| the three failures one counting bar must prevent | all | **jet-core** |
|---|---:|---:|
| **DUPLICATE** -- blocker is my own sim | .5525 | **.6329** |
| **DISPLACE** -- blocker is a different genuinely real track | .2592 | .2229 |
| **FAKE blocker** | .1883 | .1442 |

| predictor of SAME-PARTICLE | AUC | best LR |
|---|---:|---:|
| **`primn`** (hits of mine my blocker owns) | **.850** | **13.80** |
| `ncontLay` | .819 | 11.75 |
| `nClaimed` | .794 | 8.89 |
| blocker's own columns `pfrac`/`pmatched`/`pnLay` | .360/.233/.462 | |
| **16-input logistic, HELD-OUT** | **.986** | **20.36** |
| **… the DIFFERENT-PARTICLE class** | **.986** | **34.11** (88.5% recall) |

The crudest rule already works: **`primn <= 2` selects 26.2% of jet-core victims and 80.5% of them
are blocked by a DIFFERENT particle.**

**Why this succeeds where C5's question failed:** C5 asked whether the BLOCKER IS FAKE, a property
of the blocker's truth, which its claim-time columns barely carry. Same-vs-different is a property
of the **CONTEST**, and `primn`/`ncontLay`/`nClaimed` measure it directly and are computed by the
claim walk anyway. **`owner[]` gives the pairs for free; no N^2 loop, no new column.**

| population | distinct core sims | per event | **ceiling** |
|---|---:|---:|---:|
| jet-core unmatched claim victims | 1,639 | 3.64 | +.0832 |
| … with at least one different-particle blocker | 835 | 1.856 | **+.0424** |
| … with **ONLY** different-particle blockers (no duplicate risk) | **557** | **1.238** | **+.0283** |
| … C5's closed objective (blocker fake AND different) | 356 | 0.791 | +.0181 |

At C5's corrected /1.66 the no-duplicate-risk subset is **~+.0170 deployed**.

---

## [JPR5 01:05] THE PRIORITISED LEVER LIST -- ranked by RECOVERABILITY

| # | lever | budget | **recoverability** | verdict |
|---|---|---:|---|---|
| **R1** | **`XU` = braid IP-waiver + `UN4`, MEASURED AS A UNION** | -- | **+.0121 core, +.0435 deep core, fake -1.25 TC/evt, PU200 +.0011 with 0 discordant in all four dxy** | **HIGHEST -- a measured result waiting on a duplicate answer and the sealed halves.** Cost: dup +33% jets, PU200 +.00096. Cubes NOT re-run. |
| **R2** | **the SAME-PARTICLE overlap resolver** | **+.0283** / +.0424 | **identifiable: AUC .986 held-out, LR 34.1 at 88.5% recall; ONE existing column gives .850/13.8** | **THE UNLOCK FOR R1's ONLY DEBIT.** ~+.0170 deployed. Inputs free. |
| **R3** | **stage g, the 75%-purity boundary** | **+.0218**, **+.0113** exactly at 0.75 | one contaminating MD on a 12-hit 6-layer TC; those TCs are 0.92 fake TC/evt today | **LARGEST THING THIS HEAD CREATED.** No separator measured -- the round's first job. |
| **R4** | **`UN4` alone as a FAKE lever** | -- | **-2.12 fake TC/evt (-15.4%), ZERO PU200 discordant, ZERO dup cost** | **Cheapest object in the pipeline.** Value risen three rounds running. |
| **R5** | the claim's remaining budget | **+.0649** | **unreachable as a bar**: bad:good 34.2, `ptEst` LR 6.69 with no within-band ranking | **CLOSED AS A BAR, OPEN AS A RESOLVER (R2).** |
| **R6** | stage d, what is LEFT | +.0066 (was +.0300) | 78% consumed | **LARGELY SPENT.** |
| **R7** | `G5H` (not `G5U`) | -- | +.0011 core, -1.86 fake TC/evt | Dominated by `UN4`. |
| -- | **stage c0, the adjacency definition** | +.0075 | **ZERO -- CLOSED this round** | position matrix perfectly diagonal. |
| -- | a1 | +.0370 | **zero** | not ours. |

### The currency (jets TUNE, per event)

| | MASTER | JPR4 head | **THIS HEAD** |
|---|---:|---:|---:|
| fake TCs/evt, dR<.05 | 13.95 | 5.34 | **5.43** |
| fake TCs/evt, pooled | 27.69 | 13.64 | **13.76** |
| dup TCs/evt, dR<.05 (COST) | 0.17 | 0.43 | **0.37** |
| **recoverable budget e+f+g** | -- | 4.04 | **4.118** |

TC mix (non-fake fraction): T5 46.7 (.877), pT5 37.9 (.997), pLS 16.0 (.946), **T4 14.4 (.516)**,
pT3 2.4 (.982). D128 moved T5 43.9 -> 46.7 and T4 15.5 -> 14.4.

Cost per recovered core sim: `X005` 1.35 fake TCs, `W01` 0.31, **`XU` NEGATIVE -- it removes 1.25
fake TC/evt while adding 0.53 core sims/evt.**

---

## CLOSED, VERIFIED STILL CLOSED ON THIS HEAD

* **C=256 degree cap** and **the 80 edge weld WPs** -- 1 core sim of 3,089 each, fourth recon.
* **stage c0 / the adjacency definition** -- **NEW closure**, position matrix perfectly diagonal.
* **density as a RANK score** -- AUC .479, LR 1.04. **Fourth independent measurement.**
* **the count-bar relaxation as a THRESHOLD** -- third consecutive refusal, arithmetic WORSE
  (population +45%, bad:good 34.2). C5's `ptEst` correction is priced and does **not** re-open it.
* **"my blocker is a FAKE of a different sim"** -- C5's refusal stands; the answerable question is
  a different one (R2).
* **the gate branch bars' tightening half**; **the weld family term as a GLOBAL correction** (and
  the reason the knee is required: below degProd 128 the top eligible E1 is already the true edge
  .578 of the time).
* **`maxClaimedFrac`/`claimFracAlt` dead code**, **the order key's composition**, PUR's purity
  repair, the pT4 port for ranking, mutual-best pLS retirement, dead-thief recovery, non-greedy
  assignment shape, confirmation-keyed claim rules, edge-head retrain, flat seed-bonus + purity
  union, lowering the 75% bar -- **inherited refusals, declared as inherited, not re-verified.**

## WHAT I DID NOT MEASURE
The sealed halves. **Nothing timed.** The cubes were not re-run for any Q4 arm. No GPU run. The
stage-g lever has **no separator measured**. Stage c0's census is 40 events. The Q5.2 classifier was
never deployed and its union with `X005` is unmeasured.
