# FINDINGS_JETPHYS4.md

CUMULATIVE JET-CORE RECON on the CURRENT head (agent JPR4). Measurement and attribution only:
no candidate, no patch, no build, nothing committed, nothing published.

Baseline **`30de402d238`** (G10). Every number comes from the **PRISTINE main-tree binary**
`standalone/bin/lst_cpu` md5 `ad68c0271e9c9edfdd93f2c446717e54`, `LST/liblst_cpu.so` md5
`e539649d896e0566dd40b2dccad08f73`, source tree clean. **No build, no worktree.**

Sample discipline: jets **TUNE half only** -- rows 0-449 for the three offline corpora, 0-499 for
the deployed arms. Jets 500-999 NEVER OPENED. PU200 = `event_1000.root` **by explicit path**;
`-i PU200RelVal` was never used; `event_2000..7000` never opened. Artifacts `jpr4_ref/` (1.6 GB).

**Deployed A/Bs were run and are declared as such.** They are ENV-TOGGLED ARMS OF THE ONE SHIPPED
BINARY -- no build, no patch -- and the env path is **proved** inert: arm `KID`, which sets the
order-key environment to the SHIPPED literals, is **BIT-IDENTICAL to the control on all 10 branches
over 500 jet events** and on **every one of the 35 PU200 judge fields**.

Three offline corpora, all 450 jet tune events, each validated against the kernel itself:
* `jpr4_ref/fun/` -- JPR3's funnel instrument. Offline K5 edge replay vs dumped `logOdds`:
  **max |dmX| = 9.03e-05 over 29,964,189 edges**.
* `jpr4_ref/claim/` -- N2's offline claim replay. Reproduces the kernel's `[CHAIN K9] accepted=`
  **exactly on 450/450 events** on this head.
* `jpr4_ref/key/` -- new: one row per CHAIN (2,831,031) with its own >=75% truth label, gate
  branch, margins, delivery and recomputed order key.

---

## [JPR4 22:05] Q1+Q2: **the budget fell 3,659 -> 3,511 and G10 took ALL of it out of stages e and g (gate -24%, purity -19%) while the claim, the weld and a1 did not move -- and the instrumentation check the brief asked for PASSES EXACTLY: R5/R7/R8 are unchanged to four decimals in every band AND every pt band, R9/R10 both rose, and e+f+g == unmatched-and-welded is still an exact identity (1,818 = 1,818).**

Caveats WITH the headline: 450 tune events, 19,693 core sims; the corpus reads **.8217** against
the 500-event deployed run's **.8197**, a +.0020 corpus offset (JPR3 +.0022) -- two orders below
every effect here. `nodeIndex == ntuple t3 row` asserted on all 6 hit rows, 450/450 events.

### Q1 -- the funnel. 19,693 core sims, **3,511 unmatched** (JPR3 3,659; JPR2 4,559)

| first stage that loses an unmatched core sim | JPR2 | JPR3 | **NEW n** | **frac** | **ceiling** | sims/evt |
|---|---:|---:|---:|---:|---:|---:|
| **f claim: gate-alive chain, no TC delivered** | 1442 | 1411 | **1379** | **.393** | **+.0700** | 3.06 |
| a1 hits: <3 OT layers carry a reco hit | 779 | 727 | 726 | .207 | +.0369 (recoverable **zero**) | 1.61 |
| **d weld: eligible but loses the argmax** | 599 | 585 | **591** | **.168** | **+.0300** | 1.31 |
| **e gate: the welded chain is gate-killed** | 823 | 303 | **229** | **.065** | **+.0116** | 0.51 |
| **g match: TC delivered, <=75% purity** | 499 | 260 | **210** | **.060** | **+.0107** | 0.47 |
| c0 no graph-adjacent pair among its T3s | 164 | 148 | 149 | .042 | +.0076 | 0.33 |
| a3 LS: no two >75% LSs share an MD | 156 | 135 | 137 | .039 | +.0070 | 0.30 |
| a2 MDs: <3 layers carry a >75% MD | 81 | 77 | 77 | .022 | +.0039 | 0.17 |
| b T3: no >75% T3 built | 14 | 11 | 11 | .003 | +.0006 | 0.02 |
| c1 the C=256 degree cap | 1 | 1 | **1** | .000 | +.00005 | -- |
| c2 the 80 edge weld WPs | 1 | 1 | **1** | .000 | +.00005 | -- |
| **efficiency (450-event corpus)** | .7685 | .8142 | **.8217** | | | |
| **TOTAL BUDGET** | 4559 | 3659 | **3511** | | **+.1783** | **7.80** |

**What moved is exactly what G10's published mechanism predicts, and nothing else.** e gate
303->229 (-24%) and g purity 260->210 (-19%). f claim 1411->1379 (-2.3%) barely moved, so its
FRACTION rose again .386->**.393**; it is still larger than the next two stages combined. d weld
585->**591** and a1 727->**726** are statistically unmoved, as they must be.

### The fine bands (~.0025 core binning; counts with rates)

| band | denom | eff | d weld | e gate | f claim | g purity | a1 |
|---|---:|---:|---:|---:|---:|---:|---:|
| dR < .0025 | 580 | **.5207** | 66/.237 | 25/.090 | 131/.471 | 15/.054 | 26/.094 |
| [.0025,.005) | 864 | .5139 | 94/.224 | 31/.074 | 209/.498 | 25/.060 | 42/.100 |
| [.005,.0075) | 738 | .5908 | 70/.232 | 27/.089 | 151/.500 | 16/.053 | 27/.089 |
| [.0075,.01) | 691 | .6483 | 55/.226 | 16/.066 | 125/.514 | 8/.033 | 21/.086 |
| [.01,.015) | 1153 | .7008 | 72/.209 | 23/.067 | 166/.481 | 22/.064 | 36/.104 |
| [.015,.02) | 974 | .7433 | 55/.220 | 14/.056 | 106/.424 | 16/.064 | 38/.152 |
| [.02,.05) | 3690 | .8252 | 103/.160 | 43/.067 | 264/.409 | 54/.084 | 107/.166 |
| [.05,.10) | 3686 | .8839 | 55/.129 | 24/.056 | 145/.339 | 31/.072 | 115/.269 |
| >= .10 | 7317 | .9180 | 21/.035 | 26/.043 | 82/.137 | 23/.038 | 314/.523 |

The claim owns **~.50 of the budget in every bin from dR .0025 to .01** and the weld a flat
**.21-.24 below dR .02**. Neither is a "deep core" effect that switches on at some radius.

### The sim-pt axis -- unchanged in shape; still a high-pt problem

| fraction of the band's own budget | 0.9-10 | 10-20 | 20-50 | 50-100 | 100-300 | >300 |
|---|---:|---:|---:|---:|---:|---:|
| denominator | 8687 | 2998 | 3570 | 1949 | 1806 | 683 |
| unmatched | 655 | 271 | 666 | 649 | 897 | 373 |
| efficiency | .9246 | .9096 | .8134 | .6670 | .5033 | .4539 |
| d weld argmax | .015 | .081 | .149 | .223 | **.244** | **.257** |
| e gate | .066 | .059 | .053 | .051 | .071 | .102 |
| f claim | .092 | .373 | .444 | **.508** | **.475** | .445 |
| g 75% purity | .049 | .100 | .063 | .057 | .062 | .043 |
| a1 (not ours) | **.533** | .247 | .182 | .092 | .098 | .110 |

**2,585 of the 3,511 lost core sims (73.6%) are above 20 GeV**, on 40.7% of the sims.

### Q2 -- the reach ladder, and the internal check

| | <.005 | [.005,.02) | [.02,.05) | [.05,.10) | >=.10 | ALL | JPR3 ALL |
|---|---:|---:|---:|---:|---:|---:|---:|
| R5 an adjacent T3 pair EXISTS | .9204 | .9238 | .9195 | .9045 | .8601 | **.8954** | .8954 |
| R7 some such edge is ELIGIBLE | .9204 | .9235 | .9192 | .9045 | .8601 | **.8953** | .8953 |
| **R8 some such edge is WELDED (reach)** | **.7929** | **.8293** | **.8762** | **.8766** | **.8543** | **.8536** | **.8536** |
| R9 its chain survives the gate | .7486 | .7989 | .8550 | .8619 | .8390 | **.8324** | .8273 |
| **R10 that chain is DELIVERED** | .4771 | .6021 | .7412 | .7998 | .8162 | **.7355** | .7254 |
| R11 matched, any route (= efficiency) | .5166 | .6794 | .8252 | .8839 | .9180 | **.8217** | .8142 |
| **delivery / reach, R10/R8** | .602 | .726 | .846 | .912 | .955 | **.862** | .850 |
| **UNION(matched OR welded) = ceiling** | .8186 | .8735 | .9230 | .9381 | .9359 | **.9140** | .9144 |
| **distance still to run** | **+.3019** | **+.1940** | **+.0978** | **+.0543** | **+.0179** | **+.0923** | +.1002 |

**VERIFIED, NOT ASSUMED: R5, R7 and R8 are identical to JPR3 to four decimals in EVERY dR band, in
the total, and in every sim-pt band** (R8 by pt: .8629/.9079/.8655/.8271/.7719/.7247, digit for
digit). The trim runs after the weld, so this is the clean instrumentation check it was proposed
as, and the instrument passes it.

**The exact identity SURVIVES: unmatched-and-welded = 1,818 = 229 + 1,379 + 210 = e + f + g.**
Stage d (591 sims, +.0300) is still NOT on it -- the only reach-EXPANDING lever in the pipeline.

Ladder on the pt axis: UNION ceiling .9401/.9576/.9179/.8722/.8056/.7760 against R10
.8250/.8279/.7294/.5957/.4463/.3880 -- distance to run **+.2052 at 50-100 GeV, +.3023 at 100-300,
+.3221 above 300**.

---

## [JPR4 22:15] Q5 bullet 1, DEPLOYED not inferred: **`marginX` alone TIES the shipped composite order key (core .8197 -> .8191, 132 lost/119 gained, p .45; PU200 +.00008, p .64) -- so `orderAlpha`, `orderHinge`, `orderAlphaCentral`, `orderEtaRampLo/Hi`, FIVE constants, buy nothing measurable. It is a SIMPLICITY deletion, not an efficiency lever. The hinge itself is LOAD-BEARING and must NOT be deleted: `score` alone costs .0592 jet core efficiency, +6.66 fake TCs/event, and .0208 of PU200 `vxy[10,30)`.**

Caveats WITH the headline: jets TUNE 0-499 and PU200 `event_1000` only; **the AUC ranking and the
deployment DISAGREE IN SIGN** (AUC says marginX beats the composite by .006, deployment says it
loses by .0006) -- a fifth instance of this project's offline-proxy record, and the reason the arm
was deployed at all.

### The arms -- one binary, env-toggled, control proved inert

| arm | environment | order key it produces |
|---|---|---|
| `CTRL` | nothing | `score - alphaEff*max(0, 5 - mX)`, the shipped composite |
| `KID` | the SHIPPED literals, set explicitly | the same -- **bit-identical to CTRL** |
| `KMX` | `ORDER_ALPHA=1000 ORDER_HINGE=1000 ORDER_ALPHA_CENTRAL=0` | `1000*mX + score - 1e6` |
| `KSC` | `ORDER_ALPHA=0 ORDER_ALPHA_CENTRAL=0` | **`score` alone** -- the hinge deleted |

| jets tune, 500 events | CTRL | **KMX = marginX** | **KSC = score alone** |
|---|---:|---:|---:|
| core-all | .8197 | **.8191 (-.00059, p .449, 132/119)** | .7605 (**-.0592**, p 0) |
| core dR<.005 | .5183 | .5165 (-.0019, p .76) | .3785 (**-.1398**) |
| core dR<.02 | .6288 | .6259 (-.0029, p .20) | .5050 (**-.1237**) |
| fake | .1174 | .1178 (+0.036 TC/evt, p .37) | .1705 (**+6.66 TC/evt**) |
| dup | .02093 | .02095 (**0.00 TC/evt**, p .95) | .02210 (+0.20 TC/evt) |

| PU200 `event_1000`, 1000 events | CTRL | **KMX** | **KSC** |
|---|---:|---:|---:|
| eff_overall | .80966 | **+.00008 (p .637, 53/59)** | **-.00476** |
| vxy[1,5)/[5,10)/[10,30) | | +.00021/-.00099/+.00000 | -.00984/-.01478/**-.02082** |
| dxy[1,5)/[5,10)/[10,30) | | +.00032/+.00000/-.00063 | **-.02047**/-.00497/+.00000 |
| **dup_overall** | .03991 | **+.00037** (all endcap) | +.00037 |

### The offline replay predicted the deployment on all three arms

| key | accepted/evt | fake accepted/evt | **CORE sims covered** |
|---|---:|---:|---:|
| SHIP composite | 102.62 | 12.10 | **32.72** |
| `1000*mX + score` | 102.54 | 12.16 | **32.65 (-0.2%)** |
| pure `mX`, ties on stableKey | 102.54 | 12.16 | **32.65** |
| `score` alone | 102.71 | **18.68 (+54%)** | **27.65 (-15%)** |

### Why the composite cannot beat marginX: its base term IS two of the head's own inputs

`score = edgeSum + lambdaLen*nLayers`, and the frozen contract is `f[1] = nLayers`, `f[2] = sumL`.
Over **265,362 gate-alive chains the identity `score == f[2] + 3*f[1]` holds with maximum absolute
error 0.000e+00** -- exactly zero. The order key is a hand-weighted linear function of two of the
twenty-five inputs the head already consumes, added to the head's own output.

### AUC of every decision variable against the objective (265,362 gate-alive chains)

| decision variable | AUC REAL | AUC DELIV | jet-core only, AUC REAL |
|---|---:|---:|---:|
| **orderKey (SHIPPED composite)** | .9566 | .9163 | .9598 |
| **marginX alone** | **.9624** | .9069 | **.9678** |
| marginP alone | .9555 | .8927 | .9699 |
| marginD alone | .9332 | .9009 | .9329 |
| `score` alone | .8338 | .8594 | .8021 |
| edgeSum alone | .8466 | .8558 | .8108 |
| nLayers alone | .7376 | .8083 | .7013 |
| orderKey without the eta ramp | .9574 | .9198 | .9597 |

Over 60,617 contests where a rejected >=75%-pure chain met the chain that blocked it, `marginX`
would rank the victim above its blocker **9.2%** of the time; over the 5,026 DECIDABLE contests
**4.2%**. marginX does not re-open the claim's lost duels either.

**VERDICT: the deletion is CLEAN but worth ~zero efficiency and +.00037 PU200 duplicates. Rank it
as simplicity work, never as a lever.**

---

## [JPR4 22:25] Q5 bullet 2: **the two counting cuts split into one dead end and one untouched lead -- `maxClaimedMDs` owns 97.8% of all claim rejections at 28.7 bad per good (needed likelihood ratio 10.2, best measured separator 2.41): unreachable. `braidFrac` owns 2.2% at 4.0 bad per good (needed 1.43) and TWO measured separators already CLEAR it. Nobody in this project has ever touched `braidFrac`. And `maxClaimedFrac = 0.20` is DEAD CODE under the shipped configuration.**

Caveats WITH the headline: the braid bar's prize is small -- 2.14 admissible good chains/event,
0.96 of them jet-core, ~0.25 core sims/event of ceiling before the /2.4 cascade -- so this is a
**clean +.002-.006 lever, not a round-maker**; all of it is offline claim replay, nothing deployed.

The claim on this head: **589.7 candidates/evt -> 102.6 accepted/evt, keep .1740**. The jet fake
budget is **2.81 admitted fake TCs per recovered core sim**.

| bar | rejections/evt | GOOD/evt | BAD/evt | **bad:good** | **needed LR** | best measured separator |
|---|---:|---:|---:|---:|---:|---|
| **count `maxClaimedMDs=1`** | 476.3 (97.8%) | 16.02 | 460.3 | **28.7** | **10.2** | `marginX` **2.41**; `|dcaXY|` 4.34 but keeps only 14% of GOOD |
| **braid `braidFrac=0.20`** | 10.8 (2.2%) | 2.14 | 8.6 | **4.0** | **1.43** | `marginX` **2.35 (CLEARS)**; `|dcaXY|` **2.60 (CLEARS)** |

Nothing else separates: `nLayers`/`nClaimHits` 1.1, "blocker is my own sim" 1.15,
`ChainFeatures[14]` density 1.3, `score` 1.28. The braid bar is structurally different: it is the
one OWNER-RELATIVE admission test, which is why its rejected pool is 4:1 rather than 29:1.

---

## [JPR4 22:35] Q5 bullet 3: **the four gate branch bars ARE mis-set, and the evidence is not an opinion -- they sit at signal efficiencies of .53 / .17 / .98 / .89 and at LOCAL LIKELIHOOD RATIOS of 8.5 / 6.0 / 0.11 / 1.4, an eighty-fold spread in the marginal exchange rate that one classifier at one operating point would equalise. Branch 0 (4-layer IP) is TOO TIGHT by ~1 logit; branch 2 (IP 5+) is TOO LOOSE. And 85.3% of branch 1's survivors are not admitted by its bar at all -- they come through the E1-B2 far-dca FREE PASS.**

Caveats WITH the headline: the four branch populations have very different PRIORS (6.9% / 0.9% /
39.6% / 3.3% real), so equal signal efficiency is not automatically the right target and the local
likelihood ratio is the principled statement of the two. **No network is proposed for these.**

| branch (kill rule) | n chains | prior real | **sigEff** | bkgEff | AUC of its own variable | **local LR at the bar** |
|---|---:|---:|---:|---:|---:|---:|
| b0 T4 IP, `mX >= m3Theta4` | 677,356 | .0689 | **.5279** | .0230 | .9022 | **8.51** |
| b1 T4 exempt, `mD >= m3Theta4D` | 1,411,107 | .0090 | **.1671** | .0736 | .6490 | **5.99** |
| b2 IP 5+, `mX >= m3ThetaRI` | 153,917 | .3956 | **.9802** | .2947 | .9748 | **0.11** |
| b3 exempt 5+, `mX >= barR` | 588,651 | .0327 | **.8886** | .0633 | .9747 | **1.36** |

**LR 8.5 means the marginal chain branch 0 REFUSES is 8.5x enriched in real tracks: too tight.
LR 0.11 means the marginal chain branch 2 ADMITS is 9:1 against being real: too loose.**

Relaxation ladder (450 events):

| branch | delta | extra gate-alive chains/evt | of which REAL | bad:good | **needed LR** |
|---|---:|---:|---:|---:|---:|
| **b0** | -0.5 | 19.6 | **.334** | **2.0** | **0.8 -- AFFORDABLE WITH NO SEPARATOR AT ALL** |
| **b0** | -1.0 | 45.7 | .269 | 2.7 | **1.0** |
| b0 | -2.0 | 130.1 | .173 | 4.8 | 1.8 |
| b1 | -1.0 | 61.3 | .033 | 29.1 | 11.0 |
| b2 | -1.0 | 56.0 | .064 | 14.5 | 5.5 |
| b3 | -1.0 | 107.8 | .050 | 19.2 | 7.2 |

and the other direction, where fake currency is bought back:

| branch | delta | chains killed/evt | of which REAL | **good:bad thrown away** |
|---|---:|---:|---:|---:|
| **b3 exempt 5+** | +2.0 | 53.5 | .111 | **0.125 (8 fakes per real)** |
| **b1 T4 exempt** | +2.0 | 31.7 | .098 | **0.109 (9 fakes per real)** |
| b2 IP 5+ | +2.0 | 38.0 | .174 | 0.211 |
| b0 T4 IP | +2.0 | 64.1 | .583 | 1.397 (**a real track per fake -- do not touch**) |

A -1.0 relaxation on all non-far branches **revives 185 of the 229 stage-e core sims (80.8%,
+.0094 of the +.0116 ceiling)**; branch 0 alone revives 115 (+.0058). Currency note: the "bad"
column counts gate-alive CHAINS and the claim discards 82.6% of what it is given, so these
OVER-state the delivered fake cost by roughly 6x. The DIRECTION and the ORDERING are what the
table establishes.

---

## [JPR4 22:45] Q3: **the claim still holds .393 of the budget and is still nearly unreachable (28.7:1, needed 10.2, best separator 2.41) -- but its CLEAN sub-population is measured for the first time: only 0.907 core sims/event (30% of stage f, ceiling +.0207) can be recovered without either duplicating a track we already have or displacing another real one. 62.9% of jet-core victims are blocked by their OWN SIM and 49.1% by a genuine >=75% track.**

| | all unmatched victims | **jet-core unmatched victims** |
|---|---:|---:|
| blocker is the victim's OWN SIM (recovery makes a duplicate) | .5604 | **.6292** |
| blocker is itself a >=75% real chain | .4745 | **.4914** |
| blocker's sim is itself already matched | .2425 | .1920 |
| **blocker is FAKE and a DIFFERENT sim (the only clean win)** | **.2036** | **.1705** |
| rejected by the COUNT bar / the braid bar | .8824 / .1176 | .9183 / .0817 |
| the victim is 4-LAYER | .6623 | **.7891** |
| another selected sim inside dRnn .005 | .4686 | **.7121** |

**JPR3's relocation of the problem is CONFIRMED and slightly stronger: 58.8% of all remaining
jet-core loss sits on sims with another selected sim inside dR .005** (JPR3: 57.5%).

| dRnn band | denom | eff | losses | share of budget |
|---|---:|---:|---:|---:|
| <.002 | 1946 | .5164 | 941 | **.275** |
| [.002,.005) | 3071 | .6500 | 1075 | **.314** |
| [.005,.01) | 3099 | .8077 | 596 | .174 |
| [.01,.02) | 3349 | .8991 | 338 | .099 |
| >=.02 | 7489 | .9364 | 476 | .139 |

---

## [JPR4 22:55] Q4 -- THE PARKED WORK RE-PRICED ON THIS HEAD

**`UN4` is worth MORE than it was, not less, and the 4-layer growth is the reason.**

*Attach half (`kAttachMinLayers 5->4`), DEPLOYED:*

| gate | round 4 | **this head** |
|---|---|---|
| PU200 eff_overall | +.00081 (1/62) | **+.00097 (2 lost / 75 gained, p 4.0e-20)** |
| PU200 all four dxy, vxy[5,10), vxy[10,30) | 0 discordant | **0 discordant, EXACTLY unchanged** |
| PU200 fake_overall | -.00064 | **-.00077** |
| PU200 dup_overall (its only debit) | +.000089 | **+.00005** |
| jets core-all | +9 sims, 0 lost | **+.0005 (0 lost / 11 gained, p .00098)** |
| PU200 `n_tc_t4cl` | | **84,300 -> 40,491 (halved)** |

Mechanism on jets: T4 15.52/evt -> 13.79 and pT5 38.05 -> 39.83. **G10 grew the 4-layer class and
this arm's job is to drain it, so the growth made it stronger, not weaker.**

*Far half (`T4F100`), CENSUS:* far-cell gate-alive chains **199.3/evt** at **REAL .0031**; **85.3%
of the entire exempt-T4 branch's survivors are admitted by this free pass, not by its bar**. Above
density 100: **164.1/evt**, delivering **1.84 TCs/evt of which 823 of 827 (99.5%) are FAKE**.
**The far half is intact at full value.**

**`N1C` and `P11`: unchanged in standing.** The population they act on is 476.3 count-bar
rejections/evt against N1's measured 474/evt (+0.5%). Their sealed +.0027/+.0033 at 82%
additivity carries over, and so does the arithmetic that parked them.

**`N3`'s weld family: the diagnosis is INTACT, verified rather than assumed.** Stage d is 591 sims,
R8 is bit-for-bit identical to JPR3 in every band, and the slot-contest statistics reproduce digit
for digit -- 1,588 lost contests/evt, winner is a true edge **.5847**, median margin **+0.695**,
and **0.0000 of contests are lost without being out-scored**. Its ceiling is **+.0300**. The
refusal was price, not reach.

---

## [JPR4 23:00] THE PRIORITISED LEVER LIST -- ranked by RECOVERABILITY

| # | lever | budget (ceiling) | **recoverable / base rate** | **best separator vs needed** | verdict |
|---|---|---:|---|---|---|
| **R1** | **weld argmax re-key (stage d)** | +.0300, 1.31 sims/evt | **83% of the stage, MEASURED BY DEPLOYMENT** (585 -> ~100 sims) | **no separator needed -- the decision is mis-ORDERED** | **HIGHEST RECOVERABILITY IN THE PIPELINE and the ONLY reach-expanding lever.** Refused on price. **The unlock is the one construction never tried: DENSITY/REGIME CONDITIONING of the re-key.** |
| **R2** | **gate branch bars, re-fitted** | +.0116 (-1.0 revives 80.8%) **plus** the largest clean fake prize available | **b0 loosening: 2.0 bad per good at -0.5, needed 0.8 -- affordable with NO separator. b1/b3 tightening: 8-9 fakes removed per real** | bars on a LEARNED score at AUC .90-.97 | **MOST REACHABLE ARM IN THE ROUND.** Two-sided. Four constants, no kernel, no weight file. |
| **R3** | **the braid bar `braidFrac = 0.20`** | +.0057 ceiling, ~+.0024 after /2.4 | **4.0 bad per good, needed 1.43** | **`marginX` 2.35 and `|dcaXY|` 2.60 BOTH CLEAR IT** | **NEVER TOUCHED BY ANYBODY.** The only claim-stage population whose base rate is inside the fake budget. |
| **R4** | **the claim COUNT bar (stage f)** | +.0700 -- **still the largest budget** | **28.7:1; needed 10.2; CLEAN sub-population only 0.907 core sims/evt (+.0207)** | best `marginX` **2.41 -- 4.2x short** | **STILL NEARLY UNREACHABLE.** Do not send three agents at it again. |
| **R5** | 75%-purity repair (stage g) | +.0107 | PUR's structural refusal unaffected | -- | Ceiling halved TWICE untouched. Below the bar. |
| **R6** | the order key's COMPOSITION | **zero efficiency** | -- | -- | **SIMPLICITY DELETION**: five constants plus dead code. |
| **R7** | `UN4` (parked, ready-to-go) | attach **+.00097** PU200; far **-1.84 fake TC/evt at 99.5% fake purity** | fully measured, fully gated | -- | **Its value ROSE.** Two constants. |
| -- | a1 (<3 OT layers carry a reco hit) | +.0369 | **recoverable ZERO** | -- | not ours. |

### The currency, updated (jets tune, per event)

| | MASTER | JPR3 head | **THIS HEAD** | headroom |
|---|---:|---:|---:|---:|
| fake TCs/evt, dR < .02 | 7.58 | 1.98 | **1.91** | 5.67 |
| fake TCs/evt, dR < .05 | 13.95 | 5.62 | **5.34** | **8.61** |
| fake TCs/evt, pooled | 27.69 | 14.39 | **13.64** | **14.05** |
| dup TCs/evt, dR<.05 (COST COLUMN) | 0.17 | 0.58 | **0.43** | -0.26 |
| **spendable fake TCs per recovered core sim** | | 2.65 | **2.81** | |

TC mix on jets (non-fake fraction): T5 43.9 (.873), pT5 38.1 (.997), pLS 16.4 (.946),
**T4 15.5 (.548)**, pT3 2.4 (.981).

---

## CLOSED, VERIFIED STILL CLOSED ON THIS HEAD

* **the C=256 degree cap** and **the 80 edge weld WPs** -- 1 core sim of 3,511 each, third round.
* **T3-DNN, `kChainMaxNodes`, occupancy, truncation** -- zero.
* **density (`ChainFeatures[14]`) as a RANK score** -- LR 1.32 in the rejected pool. Third
  independent measurement. A CONDITIONING variable and nothing else.
* **`score` as a rank term** -- NEW: AUC .834 against `marginX`'s .962, LR 1.28, and deleting it
  from the order key costs nothing.
* **`orderAlpha`/`orderHinge` GLOBAL re-weighting** -- the hinge cannot be deleted (`score` alone
  = -.0592 jet core, +6.66 fake TC/evt, PU200 vxy[10,30) -.0208, deployed) and cannot be improved.
* **`maxClaimedFrac = 0.20` and `claimFracAlt`** -- DEAD CODE under the shipped configuration.
* **the count-bar claim relaxation** -- refused for the second consecutive recon.
* **PUR's purity repair, the pT4 port for RANKING, mutual-best pLS retirement, dead-thief recovery,
  non-greedy assignment shape, confirmation-keyed claim rules, edge-head retrain, flat seed-bonus +
  purity-ranking union, lowering the 75% match bar** -- inherited refusals, untouched by G10.

### Instruments left behind (`jpr4_ref/`)
`py/reduce_evt.py`+`run_funnel.sh`, `py/reduce_key.py`+`run_key.sh`, `py/claim_reduce.py`+
`run_claim.sh`, `py/fun_agg.py`, `py/keyaudit.py`, `py/claimaudit.py`, `py/levers.py`, `gates.sh`.
Outputs: `funnel.json`, `keyaudit.json`, `claimaudit.{txt,json}`, `levers.json`, `runs/*.json`.
