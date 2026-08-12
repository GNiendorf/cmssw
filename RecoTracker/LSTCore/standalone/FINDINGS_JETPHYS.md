# FINDINGS_JETPHYS.md

JET-CORE PHYSICS RECON (JPR). Measurement and attribution only, no fixes, no tuning.
Binary under test: main tree at `ec08aba9e5b` ("Jet speed/memory round: allocation guard + per-MD
degree cap (C=256)"), `standalone/bin/lst_cpu` built 2026-08-12 15:32, `src/`+`interface/` clean.
Sample: `standalone/jet_ref/trackingNtuple_jets_1000.root` (local copy; `/mnt/data1/kk829` untouched).
Artifacts: `standalone/jpr_ref/`.

---

## [JPR 15:55] THE 1000-EVENT JET BASELINE. M3's first-100 shape holds at 10x the statistics.

`lst_cpu -i jet_ref/trackingNtuple_jets_1000.root -n 1000 -s 16 -v 1 -w 1 -J -o
jpr_ref/jets1000_base.root`, **rc 0, 1000/1000 events, 80 s wall** (0.96 s/evt CPU-time,
16 streams). No crash, no skipped event -- the guard+cap commit makes the full sample routine, so
every number below is on all 1000 events. Analysis: `m3_ref/jetphys.py` (unbinned, literal
transcription of `performance.cc`), output `jpr_ref/base1000_phys.json`.

| | first-100 (M3 13:20) | **1000 events (JPR)** |
|---|---|---|
| eff all-sim | .7510 | **.7585** (75523/99565) |
| eff jet-core | .6371 | **.6533** (28633/43831), 43.8 denom/evt |
| fake | .2299 | **.2206** (26494/120119) |
| dup | .0199 | **.0209** |
| TCs/evt | 123.5 | **129.2** (120.1 in the fake/dup denominator) |
| TC mix /evt | T5 52.8 pT5 37.3 pLS 16.0 T4 5.4 pT3 3.1 | T5 53.4 pT5 39.8 pLS 16.5 T4 7.2 pT3 3.3 |
| core sims with dR > 0.1 | 36.1% | **36.9%** |

### The dR-resolved collapse, 1000 events

| dR(sim, its genjet) | denom | eff | | dR(reco, closest genjet) | n | fake | dup |
|---|---:|---:|---|---|---:|---:|---:|
| [0, .02) | 11032 | **.3167** | | [0, .02) | 4892 | **.6241** | .0153 |
| [.02, .05) | 8554 | **.5615** | | [.02, .05) | 12602 | **.5879** | .0102 |
| [.05, .10) | 8063 | .7446 | | [.05, .10) | 17721 | .4956 | .0113 |
| [.10, .20) | 8734 | .8634 | | [.10, .20) | 17810 | .2848 | .0122 |
| [.20, .40) | 5898 | .9188 | | [.20, .40) | 11412 | .0586 | .0094 |
| [.40, inf) | 1550 | .8852 | | [.40, inf) | 55682 | .0271 | .0320 |

Every cell is within ~.015 of the first-100 value, so **M3's baseline was not a statistical
accident** and the three funnel bands have 11032 / 8554 / 8063 sim tracks in them (7538 / 3751 /
2059 of them UNMATCHED -- that is the total budget the funnel has to attribute).

### The TC-mix reading that sets up Q2
Per-type non-fake fraction over the whole sample (fake/dup denominator, no dR cut):

| type | TCs/evt | non-fake frac |
|---|---:|---:|
| pT5 | 39.8 | **.9742** |
| pLS | 16.5 | .9687 |
| pT3 | 3.3 | .9045 |
| **T5 (chain, 5-layer)** | **53.4** | **.6116** |
| **T4 (chain, 4-layer)** | **7.2** | **.4551** |

The chain block emits 60.6 of the 120.1 denominator TCs/evt and **carries 21.7 of the 26.5 fake
TCs/evt (82%)**: T5 20.7 + T4 3.9 vs pT5 1.0 + pLS 0.5 + pT3 0.3. Meanwhile the chain block wins
10641 of the 28633 matched core tracks (T5 9910 + T4 731) against pT5's 13693. So on jets the
chain block is simultaneously the second-largest efficiency source and 82% of the fake problem.
Both Q1 and Q2 therefore live inside the chain block, and the funnel is being built there.

Next: the stage-by-stage funnel. Machinery survey running (dump formats + truth join), master
(b42d8f97ad5) 1000-event jet arm running in parallel as the "is this loss ours or shared" control.

## [JPR 16:25] THE FUNNEL IS BUILT AND IT HAS ONE DOMINANT STAGE: **the K9 hit claim**. 57% of the missing core tracks die there, not in any network.

Machinery (all in `jpr_ref/`, all offline truth, nothing added to the tree):
* `run_funnel.sh` -- one isolated process per event (`-x i -n -1 -s 1 -v 2 -w 1 --allobj -J`) with the
  four in-tree sidecars on (`LST_CHAIN_{EDGE,NODE,CHAIN,FEAT}_DUMP`), reduce, delete the dumps
  (600 MB - 5 GB per event, so they cannot be kept).
* `reduce_evt.py` -- the per-event truth join. Uses the ntuple's own `>75%` reverse maps
  (`sim_{md,ls,t3,pls,tc}IdxAll(+Frac)`) for the upstream stages and the sidecars for the chain
  stages. **The dense chain-node index is PROVEN equal to the ntuple `t3` row** by asserting the
  6 hit rows of `P25N` against `t3_lsIdx{0,1} -> ls_mdIdx{0,1} -> md_{anchor,other}HitIdx`; the
  assert holds on every event, so no remapping is needed.
* `edgew.py` -- **the K5 edge head replayed offline**, because the eligibility bit (`weldBar`) is
  written to no dump. `P21F` carries 13+13 node features + 14 edge features = the head's 40 inputs,
  so the three logits and the OR-rule are exactly reconstructible.
  **Validation: `max |mX_replay - logOdds_dumped| = 7.4e-05` over 10.2 million replayed edges.**
  (One correctness note for anyone reusing this: `kWpPrompt` and `kWpDisp` are DIFFERENT tables,
  so `max(mP,mD) >= bar` is NOT the rule and the dumped scalar alone cannot decide eligibility.
  A regex that grabs `kWpDisp` without anchoring on the declaration picks up the comment's
  `kWpDisp[cell]` and silently returns the `kWpPrompt` array -- it cost me one iteration.)
* `valid_deliv.py` -- the delivery predicate ("the chain's OT hit rows are a subset of one TC's")
  is checked against the kernel's own `[CHAIN K9] accepted=` census: **103.70/evt offline vs
  104.09/evt in the kernel, max per-event difference 5 of ~104**, so the predicate is sound at the
  0.4% level, two orders below the effect it measures.

### Q1 -- THE FUNNEL (188 jet events so far, 7079 core sims, 2398 unmatched; 1000-event rerun pending)

| first stage that loses an unmatched core sim | dR<.02 | [.02,.05) | [.05,.10) | ALL |
|---|---:|---:|---:|---:|
| unmatched budget | 1163 | 568 | 320 | 2398 |
| a1 hits: <3 OT layers carry a reco hit of this sim | .074 | .097 | .181 | .167 |
| a2 MDs: <3 layers carry a >75% MD | .008 | .014 | .009 | .020 |
| a3 LS: no two >75% LSs share an MD | .009 | .042 | .056 | .050 |
| **b T3: no >75% T3 built** | **.003** | **.000** | **.013** | **.005** |
| c0 no graph-adjacent pair among its T3s | .027 | .033 | .116 | .080 |
| **c1 adjacent pair has no ChainEdges row (the cap)** | **0** | **0** | **0** | **0** |
| **c2 no same-sim edge passes the 80 weld WPs** | **0** | **0** | **0** | **0** |
| d weld: eligible but loses the argmax | .136 | .102 | .097 | .108 |
| e gate: the welded chain is gate-killed | .285 | .213 | .231 | .241 |
| **f claim: gate-alive chain, no TC delivered** | **.498** | **.683** | **.744** | **.567** |
| g match: TC delivered, <=75% purity | .056 | .113 | .072 | .079 |

and the monotone reach table, which is the same statement without the attribution:

| | dR<.02 | [.02,.05) | [.05,.10) | [.10,.20) | ALL |
|---|---:|---:|---:|---:|---:|
| R4 has a >75% T3 | .9383 | .9357 | .9372 | .9145 | .9195 |
| R5 has an adjacent T3 pair | .9208 | .9188 | .9069 | .8817 | .8908 |
| R6 that pair has an edge row | .9208 | .9188 | .9069 | .8817 | .8908 |
| R7 some such edge is ELIGIBLE | .9208 | .9188 | .9069 | .8817 | .8908 |
| R8 some such edge is WELDED | .8314 | .8744 | .8829 | .8744 | .8544 |
| R9 its chain survives the gate | .6435 | .7818 | .8254 | .8521 | .7728 |
| **R10 that chain is DELIVERED as a TC** | **.3158** | **.4847** | **.6408** | **.7640** | **.5807** |
| R11 matched (any route incl. pT5/pLS) | .3418 | .5651 | .7517 | .8494 | .6613 |

**Read: 92% of jet-core sim tracks build a truth-matched T3 and 85% get one WELDED. The efficiency
is .34 in the core because R9 -> R10 loses a third of them and R8 -> R9 loses another eighth.
Nothing upstream of the weld is a significant loss, and neither the degree cap nor the edge
working points lose a single core track.**

The one-line reason R9 -> R10 is so violent: **the K9 claim admits 104.1 of 1565.6 gate-alive
chains per event = 6.65%.** In the jet core the claim is not a tie-breaker, it is the primary
selector, and it runs on `orderKey = score - 10*max(0, 5 - mX)` -- i.e. on the gate head's score,
which has never seen core topology.

Still running: the remaining jet events (target 200, then 1000-event rerun if the reduce cost
allows), the PU200 control with the identical instrument (for the off-distribution comparison),
and LST master's own 1000-event jet arm (is the core collapse ours or shared?).

## [JPR 16:45] CORRECTION to my 16:25 stage table + THE CONTROL THAT REFRAMES THE ROUND: **LST MASTER IS AT .78 CORE EFFICIENCY WHERE WE ARE AT .65, AND IT GETS THERE WITH pT5**

### Correction first
The [JPR 16:25] stage table counted a stage row over ALL core sims, not only unmatched ones. A
MATCHED core sim can also fail a chain-path predicate (it was matched by pT5 / pLS / pT3 instead),
so some rows exceeded their band's own unmatched budget -- visible as the `1.318` in the
`[.40,inf)` column, which is impossible. Fixed (`agg.py`, the histogram now carries `& ~m`);
every table from here is over unmatched core sims only and the rows sum to the budget exactly.
The dominant-stage conclusion does not move.

### LST master on the SAME 1000 jet events
`m3_ref/run_master_jets.sh 1000 16 jpr_master_jets1000` (worktree `g3` at `b42d8f97ad5`,
`lst_cpu` md5 `0189e8848a2fa6ba129f73a129dc5937`), rc 0, **1000/1000 events**, same
`-n 1000 -s 16 -w 1 -J` protocol as our arm, same `jetphys.py`:

| dR(sim, its genjet) | denom | **master eff** | **our eff** | delta | master fake | our fake |
|---|---:|---:|---:|---:|---:|---:|
| [0, .02) | 11032 | **.5287** | .3167 | **-.212** | .5644 | .6241 |
| [.02, .05) | 8554 | **.7800** | .5615 | **-.219** | .4637 | .5879 |
| [.05, .10) | 8063 | **.8549** | .7446 | -.110 | .4366 | .4956 |
| [.10, .20) | 8734 | .9044 | .8634 | -.041 | .2663 | .2848 |
| [.20, .40) | 5898 | .9235 | .9188 | -.005 | .0741 | .0586 |
| [.40, inf) | 1550 | .8865 | .8852 | -.001 | .0331 | .0271 |
| **jet-core** | 43831 | **.7784** | **.6533** | **-.125** | | |
| all-sim | 99565 | .8145 | .7585 | -.056 | | |
| pooled fake / dup | | .2235 / .0222 | .2206 / .0209 | +.003 / +.001 | | |

**So the jet-core collapse is OURS, not a property of the sample.** Outside dR 0.2 the two
binaries are identical; inside dR 0.05 master is 21 efficiency points ahead **and simultaneously
6-12 points better on fake rate**. This is the first time the two have been compared on jets and
it changes the framing of the round: there is a demonstrated 21-point core headroom, not a
detector limit. (Caveat stated with the headline: the pooled fake/dup are a wash, and we still
beat master on dup; the deficit is a pure jet-core efficiency deficit.)

### WHERE master's core efficiency comes from: pT5, and only pT5

| per event | master | ours |
|---|---:|---:|
| T3 built | 81,611 | 78,964 |
| 5-layer objects built | **34,797 Quintuplets** | 8,102 chains (2,165 of them 5-layer) |
| pixel-confirmed 5-layer built | **237 pixel quintuplets** | -- |
| **pT5 TCs delivered** | **74.4** | **39.8** |
| T5 TCs delivered | 32.8 | 53.4 (+7.2 T4) |
| pLS / pT3 TCs | 18.2 / 6.2 | 16.5 / 3.3 |
| **jet-core sim tracks won by pT5** | **30,264 of 34,118 (89%)** | **13,693 of 28,633 (48%)** |
| jet-core tracks won by the bare OT object | 720 (T5) | 10,641 (T5+T4) |

Master's jet-core efficiency is **89% pixel-seeded**. We substitute bare 5-layer chain TCs
(53.4/evt, non-fake .61) for master's pixel-confirmed ones (74.4/evt, non-fake .86) and lose
5,485 core tracks doing it.

### The structural reason, verified in our own code
`ChainTargetFlags` (`src/alpaka/ChainAttach.h:1205-1222`) takes its stage-A target list from
`accepted` -- **the K9-claim output** -- filtered to `nLayers >= kAttachMinLayers`:
```cpp
uint32_t const c = accepted[ai];
k = (chains.nLayers()[c] >= kAttachMinLayers) && !(chains.dcaXY()[c] >= cfg.attachDcaMax);
```
launched at `LSTEvent.dev.cc:2322` with the comment "the same filtered copy of the K9 accepted
array". Measured on 250 jet events: gate-alive **1563.3/evt -> K9-accepted 103.4/evt -> attach
targets (accepted and nLayers>=5) 94.6/evt -> 39.8 pT5**. Of the 1,015 gate-alive 5-layer chains
per event, **~950 are never offered to a pixel seed at all.**
Master's order is `createQuintuplets(); ... createPixelQuintuplets(); ... createTrackCandidates();`
(`g3 src/alpaka/LST.cc:78,99,128`) -- its pixel matching runs on the **whole** T5 pool and the
final selection happens last. **Ours reserves pixel confirmation for objects that have already won
the arbitration; master uses it to decide the arbitration.** That inversion is the single largest
structural difference between the two on this sample.

## [JPR 17:20] FINAL: the funnel on 450 jet events, the fake-origin table, the off-distribution evidence, and the prioritized lever list

Corpus: **450 jet events**, one isolated process each, 0 failures, `jpr_ref/fun/e{0..449}.npz`
(+ `.census`, `.log`). Aggregate `jpr_ref/funnel_450.{txt,json}`. PU200 control with the identical
instrument: **30 events**, `jpr_ref/funpu/`, `jpr_ref/funnel_pu.{txt,json}`. The 1000-event
rate baseline and the master arm are the [JPR 15:55] / [JPR 16:45] tables.

Validation carried by this corpus:
* K5 replay vs the dumped `logOdds`: **max |dmX| = 9.0e-05 over 29,964,189 replayed edges**.
* delivery predicate vs the kernel's `[CHAIN K9] accepted=`: **104.03/evt offline vs 104.49/evt**,
  max per-event difference 6 of ~104, 172/450 events exact.
* dense chain-node index == ntuple `t3` row: asserted on all 6 hit rows, **450/450 events**.

---

### Q1 -- THE FUNNEL. 19,693 core sim tracks, 6,747 unmatched. Rows sum to the budget exactly.

| first stage that loses an unmatched core sim | dR<.02 | [.02,.05) | [.05,.10) | [.10,.20) | [.20,.40) | [.40,inf) | ALL | ALL frac |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| **unmatched budget** | **3384** | **1561** | **949** | **546** | **224** | **83** | **6747** | |
| a1 hits: <3 OT layers carry a reco hit | 214 | 134 | 136 | 148 | 118 | 57 | 807 | .120 |
| a2 MDs: <3 layers carry a >75% MD | 18 | 24 | 8 | 12 | 17 | 5 | 84 | .012 |
| a3 LS: no two >75% LSs share an MD | 39 | 41 | 39 | 27 | 14 | 10 | 170 | .025 |
| b T3: no >75% T3 built | 7 | 0 | 3 | 2 | 1 | 2 | **15** | **.002** |
| c0 no graph-adjacent pair among its T3s | 74 | 30 | 30 | 26 | 21 | 5 | 186 | .028 |
| c1 adjacent pair has no ChainEdges row (the C=256 cap) | 0 | 1 | 0 | 0 | 0 | 0 | **1** | **.000** |
| c2 no same-sim edge passes the 80 weld WPs | 1 | 0 | 0 | 0 | 0 | 0 | **1** | **.000** |
| d weld: eligible but loses the argmax | 436 | 123 | 65 | 20 | 2 | 0 | 646 | .096 |
| e gate: the welded chain is gate-killed | 911 | 296 | 147 | 56 | 14 | 4 | 1428 | .212 |
| **f claim: gate-alive chain, no TC delivered** | **1472** | **793** | **454** | **191** | **18** | **0** | **2928** | **.434** |
| g match: TC delivered, <=75% purity | 212 | 119 | 67 | 64 | 19 | 0 | 481 | .071 |
| efficiency of the band | .3232 | .5770 | .7425 | .8623 | .9142 | .8877 | .6574 | |

per-band fractions of that band's budget, for the three bands the round is about:

| | dR<.02 | [.02,.05) | [.05,.10) |
|---|---:|---:|---:|
| upstream of the chain block (a1+a2+a3+b) | .080 | .127 | .196 |
| chain graph (c0+c1+c2) | .022 | .020 | .032 |
| **weld (d)** | **.129** | .079 | .068 |
| **gate (e)** | **.269** | .190 | .155 |
| **K9 claim (f)** | **.435** | **.508** | **.478** |
| 75% match (g) | .063 | .076 | .071 |

and the monotone reach table, which says the same thing without attribution:

| | dR<.02 | [.02,.05) | [.05,.10) | [.10,.20) | ALL |
|---|---:|---:|---:|---:|---:|
| R1 >=3 OT layers with a reco hit | .9538 | .9575 | .9485 | .9397 | .9434 |
| R3 an LS pair sharing an MD | .9412 | .9347 | .9281 | .9160 | .9221 |
| R4 a >75% T3 | .9404 | .9358 | .9278 | .9163 | .9221 |
| R5 an adjacent T3 pair | .9228 | .9195 | .9045 | .8838 | .8954 |
| R6 that pair has an edge row | .9228 | .9192 | .9045 | .8838 | .8954 |
| R7 some such edge is ELIGIBLE | .9226 | .9192 | .9045 | .8838 | .8953 |
| R8 some such edge is WELDED | .8302 | .8816 | .8801 | .8767 | .8585 |
| R9 its chain survives the gate | .6336 | .7832 | .8256 | .8485 | .7709 |
| **R10 that chain is DELIVERED** | **.3090** | **.4951** | **.6346** | **.7658** | **.5792** |
| R11 matched, any route | .3232 | .5770 | .7425 | .8623 | .6574 |

**The headline: 94% of jet-core sim tracks have an adjacent T3 pair and 86% get one WELDED. Nothing
in the graph construction, the degree cap, or the 80 edge working points loses them (2 tracks of
6747, combined). They are lost in the three arbitration steps AFTER the weld -- and 43% of the
whole loss is one step, the K9 hit claim.**

The claim's scale, measured: **gate-alive 1562.5 chains/evt -> K9-accepted 104.5/evt = it keeps
6.69%.** With the identical instrument, **PU200 keeps 39.4%** (2592.3 -> 1020.7). The claim is 6x
tighter on jets than on the sample everything was tuned on, and it is not a tie-breaker there, it
is the primary selector.

#### The claim is not arbitrating at random -- it is ranking fakes above core truth
* For 2,882 stage-f core sims we could join their own best gate-alive chain to the chain behind the
  TC that ended up owning their hits. **The thief out-ranks them on `orderKey` 73.8% of the time**,
  median advantage **+11.2** (`orderKey = score - 10*max(0, 5 - mX)`, `ChainArbitrate.h:141`).
  So the ordering is decisive, and it is decided by the gate head's score.
* Of the 5,686 unmatched core sims that built a T3, **98.9% have a delivered TC carrying their T3
  hits, and that TC is FAKE 91.9% of the time**. Per stage: f .706, e .750, d .827, g .990.

#### Ceiling of the claim, measured rather than assumed (`jpr_ref/ceiling.py`)
Every one of the 2,928 stage-f core sims has a gate-alive chain owning >=2 of its own >75% T3s.
**1,921 of them (65.6%) have a gate-alive chain whose ENTIRE pre-trim node run is its own T3s**
(98.3% have one that is >=60% its own), and 87.6% of those chains are 5+ layers. Those chains would
deliver a creditable match if the claim admitted them: **+1921/19693 = +.098 absolute core
efficiency from the claim alone, i.e. .657 -> .755.** The full stage-f budget is +.149 (-> .806).
Compare master's .7784 -- the claim lever's measured ceiling brackets the entire master gap.

---

### Q2 -- WHERE THE CORE FAKES ARE BORN

| dR(reco, closest genjet) | nTC | fake | fake T5 | fake T4 | fake pT5 | fake pLS | fake pT3 |
|---|---:|---:|---:|---:|---:|---:|---:|
| [0,.02) | 2088 | **.6106** | 934 | 233 | 47 | 46 | 15 |
| [.02,.05) | 5307 | **.5728** | 2379 | 463 | 108 | 46 | 44 |
| [.05,.10) | 8183 | .4970 | 3286 | 556 | 145 | 44 | 36 |
| [.10,.20) | 8087 | .2912 | 1925 | 303 | 88 | 19 | 20 |
| [.20,.40) | 4900 | .0651 | 222 | 61 | 24 | 10 | 2 |
| [.40,inf) | 24576 | .0286 | 439 | 128 | 73 | 58 | 5 |

Core (dR<0.05): **4,315 fake TCs of 7,395 = 9.6 fake TCs/evt.** Class, and what they are:

| class | n | frac of core fakes | nLayers | purity (median) | purity>0.5 | purity==0 | shares >=1 hit with a MISSED core sim |
|---|---:|---:|---:|---:|---:|---:|---:|
| **T5 (bare chain, 5+ layers)** | **3313** | **.768** | 5 | **.333** | .170 | 0 | **.928** |
| **T4 (bare chain, 4 layers)** | **696** | **.161** | 4 | **.250** | .096 | 0 | **.825** |
| pT5 (chain + pLS) | 155 | .036 | 7 | .571 | .555 | 0 | .923 |
| pT3 (bare T3 + pLS) | 59 | .014 | 5 | .444 | .339 | 0 | .864 |
| pLS (carried seed) | 92 | .021 | 0 | .000 | 0 | **1.000** | .000 |

('purity' = the largest fraction of the TC's own hits belonging to a single sim track, pileup
included -- my own hit-level matcher, the closed form of `matchedSimTrkIdxsAndFracs`.)

**Answers to Q2, in order:**
1. **Which class?** 93% of core fakes are BARE chain TCs (T5 .768 + T4 .161). Everything with a
   pixel seed attached is 7% of the problem. Bare-chain core fakes are also genuinely combinatoric,
   not threshold misses: median purity .333 for a 10-hit T5 (about 1/3 of its hits from one track)
   and only 17% exceed 0.5. Contrast the pT5 core fakes, median purity .571 with 55% above 0.5 --
   those ARE near-threshold. **Pixel confirmation is what separates the two populations**, and our
   pT5 non-fake fraction is .973 against bare T5's .610.
2. **Which stage admitted them?** Not the edge WPs (they admit 99.0% of all E2 and 30.5% of all E1
   edges on jets -- they gate nothing here). The **gate**: all 4,164 chain-class core fakes join a
   `P22C` record and the gate scored them `mX` median **+4.22** against **+5.03** for delivered TRUE
   core chains -- a separation of **0.81** in a variable whose own p10-p90 spread is 11 units.
   **The 3-class gate head cannot tell a core fake from a core track.** Cell composition of the
   core fakes it passed: branch 3 (5+ layer, dcaXY >= 0.5, the DISPLACED-EXEMPT cell) **59.1%**,
   branch 2 (5+ IP) 24.2%, branch 1 (T4 exempt) 14.3%, branch 0 (T4 IP) 2.5%. **Core fakes look
   displaced** -- stitched from several tracks, they carry a large dcaXY, and the exempt branch's
   bar is the loose one. That is where they get in.
3. **Are they hit-thieves?** Yes, directly: **92.8% of core T5 fakes and 82.5% of core T4 fakes
   share at least one hit with a MISSED jet-core sim's own T3s.** This closes the loop with funnel
   stage f: the fake that wins the claim is built out of the hits of the track that loses it.

---

### OFF-DISTRIBUTION: the same three heads, jets vs PU200, one instrument

| quantity | PU200 (30 evt) | **jets (450 evt)** | ratio |
|---|---:|---:|---:|
| **K5 edge head** | | | |
| edges scored / evt | 6,475 chains from ~99k edges | 5,199,336 | ~52x |
| E1 share of edges | .696 | .842 | |
| **TRUE-edge prevalence (all edges)** | **.1279** | **.000788** | **162x lower** |
| ... E1 | .0820 | .000411 | 200x |
| ... E2 | .2328 | .002795 | 83x |
| **ELIGIBLE fraction, E1** | .2382 | **.3054** | *higher* |
| **ELIGIBLE fraction, E2** | .9268 | **.9900** | *higher* |
| **K7 gate head** | | | |
| chains / evt | 6,475 | 8,133 | |
| chain nLayers == 4 | .533 | **.705** | |
| gate-killed | .5996 | **.8079** | |
| mX median | -1.601 | **-4.728** | |
| branch mix 0/1/2/3 | .218/.314/**.238**/.229 | .203/**.502**/**.052**/.243 | |
| branch 2 (5+ IP) killed / delivered | .0897 / .4436 | .2913 / .1554 | |
| **attach stage (outcome, no pair dump in-tree)** | | | |
| chain-class TCs / evt | 854.0 | 98.8 | |
| **pT5 share of chain-class TCs (attach rate)** | **.6678** | **.3976** | |
| bare-T5 non-fake fraction | .933 | **.610** | |
| **K9 claim** | | | |
| gate-alive -> accepted | 2592.3 -> 1020.7 = **.3937** | 1562.5 -> 104.5 = **.0669** | 5.9x tighter |

**The quantitative statement of "off-distribution":** the edge head's working points were
calibrated where 1 edge in 8 is true and are applied where 1 edge in 1,270 is true -- and the bars
admit a *larger* fraction on jets than on PU200, so the same cut now passes roughly 900 false edges
per true one instead of 6. The gate head is asked about a different object mix (4-layer chains
.53 -> .71, and its best-behaved cell, 5-layer IP-like, collapses from 24% of chains to 5%). The
attach stage's confirmation rate halves (.668 -> .398) exactly where the bare objects' purity
collapses (.933 -> .610). Master, on the same jet events, keeps a **.694** attach rate
(74.4 pT5 of 107.2 OT TCs) -- i.e. master's pixel-confirmation rate on jets equals OUR PU200 rate.

---

### HONEST NULLS (things measured and found NOT to be the problem)

1. **The C=256 per-shared-MD degree cap: null on core efficiency.** 108,752 of 547,211 adjacent
   TRUE same-sim core T3 pairs (**19.9%**) lose their ChainEdges row to the first-come keep, all of
   them E1 (the cap touches E1 only) -- and exactly **1 of 6,747** unmatched core sims is lost by
   it, because a core sim offers a median of ~30 redundant adjacent pairs. Consistent with M3's
   paired McNemar ([M3 13:20]: C=256 vs uncapped +.0051 +- .0039 eff, i.e. capped is if anything
   better). **Do not spend the next round on the cap.**
2. **The 80 edge weld working points: null on core efficiency.** 94.0% of TRUE adjacent core edges
   with a row are eligible (E1 .849, E2 .9995) and **1 of 6,747** core sims dies for lack of an
   eligible edge. The WPs are also not protecting anything (see off-distribution). They are inert
   in both directions on jets.
3. **LST's shared T3 code and its T3-DNN: null.** 15 of 6,747 (0.2%). 92.2% of core sims build a
   >75% T3. The `t3dnn::kWp_prompt/kWp_displaced` tables (`interface/alpaka/Common.h:74-79`) are
   not a jet-core problem.
4. **No silent per-module truncation anywhere in the builders.** `CreateMDArrayRangesGPU`
   (`MiniDoublet.h:887`), `CreateSegmentArrayRanges` (`Segment.h:988`) and
   `CreateTripletArrayRanges` (`Triplet.h:837`) all size from an **exact dynamic count** (sum of
   `nMDs` / `connectedMax`), with no static per-module maximum; the "excess alert" guards are
   under-count safety nets. Empirically over 438-450 events: `TC slot fallbacks=0`, `overflow=0`,
   `capHit=0`, `rdtCapDrop=0`, `rdtHashOverflow=0`, `rdtInsRefused=0`, and **no `[CHAIN OVERFLOW]`
   in any run log**. `kChainMaxNodes = 64` (whose assert is compiled out in release) is never
   approached: the longest jet chain has **5 nodes**, and 89% of all chains have exactly 2.
5. **The K9 candidate cut is not the claim loss.** 0 of 31,012 gate-surviving core-sim chains are
   dropped by `candKeep` (`ChainArbitrate.h:143-154`, exempt and score < 0). The loss is in the
   hit-ownership greedy itself.
6. **Stage a1 (807 tracks, 12%) is not ours and is not core-specific.** It rises monotonically with
   dR (.063 in the core to .527 at [.20,.40)) -- these are low-quality tracks with fewer than 3 OT
   layers of reco hits anywhere in the event, shared with LST master, which loses them too
   (master's [.40,inf) efficiency .8865 vs ours .8852).

---

### THE PRIORITIZED LEVER LIST (nothing implemented; ceilings are my own counts)

**L1 -- Move the pLS attach UPSTREAM of the K9 claim (or feed pixel confirmation INTO the claim's
priority).** Attacks stages **f and e** at the root.
* Mechanism: `ChainTargetFlags` (`ChainAttach.h:1205`) draws its targets from `accepted`, the K9
  output, so only **94.6 of the 1,015 gate-alive 5-layer chains/evt** are ever offered a pixel
  seed. Master's `createPixelQuintuplets()` runs on its whole 34,797-T5 pool before its final
  selection (`g3 LST.cc:78,99,128`).
* Ceiling: **+.098 core efficiency measured** (the 1,921 stage-f core sims with a fully-own
  gate-alive chain), up to +.149 for the whole stage-f budget; master's realized value on the same
  events is **+.125** (.6533 -> .7784), reached with 74.4 pT5/evt against our 39.8.
* Why it should also *help* fakes: attaching a pLS takes a chain from non-fake .610 to .973, and
  pixel seeds are not the scarce resource -- **478.5 pLS/evt are still unowned at attach stage B**.
* Risk surface: HIGH engineering, LOW physics. It reorders kernels (attach currently consumes
  `accepted`, `plsOwned`, and the -RD/-CC/-XC retirement chain keyed on chain rows), and PU200
  regression risk is real because on PU200 the claim already keeps 39% and the attach rate is
  already .668 -- the reorder must be provably a no-op there, which is testable (bit-identity gate).
  Feeds the maintainer's "simpler than LST" rule: it *removes* an ordering constraint.

**L2 -- Retrain the K7 gate head, and its cells, ON CORE TOPOLOGY.** Attacks stages **e** and, via
`orderKey`, **f**; and it is the only lever that attacks Q2's core fakes.
* Evidence: gate `mX` separates core fakes from delivered core truth by **0.81** on an 11-unit
  spread; 59% of core fakes enter through branch 3 (5+ layer, dcaXY >= 0.5, displaced-exempt);
  branch composition shifts .238 -> .052 (5+ IP) and .314 -> .502 (T4 exempt) from PU200 to jets;
  gate-kill goes .600 -> .808. The head has never seen a 1-TeV jet core.
* Ceiling: stage e is **+.072** core efficiency; because `orderKey` is a function of the gate score,
  a better-separating gate also converts part of stage f's +.098-.149 -- but that part is not
  separately measurable without building it.
* Risk surface: MEDIUM-HIGH. Retraining touches every shipped cell; PU200 eff/dup/fake and the
  cube50 displaced lead are all downstream of this head (the E1-B2 displaced win lives in the
  exempt branches). Needs the NN-loop's on-policy protocol and a jet+PU200+cube joint training set,
  and the `dcaXY` split may need to stop being the displaced proxy in dense environments.

**L3 -- Make the claim's priority resolution density-aware / truth-ranked, without loosening it.**
Attacks stage **f** directly.
* Evidence: 73.8% of the time the thief out-ranks the victim on `orderKey` by a median +11.2; the
  claim keeps 6.69% on jets vs 39.4% on PU200; `claimStuck` averages 1,589/evt.
* Ceiling: bounded above by stage f, **+.149**; the measured recoverable part is **+.098**.
* Risk surface: MEDIUM on fakes -- the claim is currently the *only* thing holding the core fake
  rate at .58 rather than far worse (it discards 1,458 gate-alive chains/evt). **Any change that
  admits more chains without a better ranking will convert efficiency gain into fake rate
  one-for-one.** This lever is only safe as "rank better", never "admit more", which is why L1 (add
  a genuinely discriminating input) should come before it.

**L4 -- Give the weld more than one slot per node, or rank its argmax by something density-aware.**
Attacks stage **d**.
* Evidence: the weld is a **median 31-way** contest per slot in the core (p90 252, max 408) and the
  winner is itself a true edge only **58.3%** of the time; each node has exactly one in- and one
  out-slot (`ChainWeld.h:80-126`), so the welded graph is disjoint paths and a core track that
  shares an MD with a neighbour simply loses.
* Ceiling: **+.033** core efficiency (646 tracks), concentrated at dR<0.02 (.129 of that band's
  budget).
* Risk surface: HIGH on cost and fakes. Chains/evt is already 8,133 on jets with 81% gate-killed;
  a second slot multiplies that and lands straight in the L3 bottleneck. Cheapest first: keep the
  single slot but re-rank (the argmax key is `max(mP,mD)` from a head with a 162x prior shift).

**L5 -- The 75% match miss (stage g), and the upstream MD/LS residue.** Lowest priority.
* Stage g is **+.024** (481 tracks; the TC is delivered and 99% of the time it is FAKE, i.e. its
  hits are shared badly, so this is really an L1/L3 symptom). Stages a2+a3+c0 together are **+.022**
  and live in shared LST MD/LS code plus the T3-layer-coverage requirement.
* Risk surface: LOW value, and a2/a3 changes move LST-carried cells too, which the maintainer's
  "no changes that help LST too" rule rejects.

**NOT a lever (do not fund):** the degree cap, the 80 edge weld WPs, the T3-DNN, any occupancy or
fixed-array limit, `kChainMaxNodes`. All measured null above.

---

### Reproduce
```
jpr_ref/run_funnel.sh <first> <last> <slot> {jet|pu}    # per-event: run + reduce + delete dumps
jpr_ref/agg.py jpr_ref/fun --tag jets_450 --json jpr_ref/funnel_450.json
jpr_ref/valid_deliv.py jpr_ref/fun                      # delivery predicate vs [CHAIN K9] accepted
jpr_ref/ceiling.py   jpr_ref/fun                        # the stage-f recoverable ceiling
m3_ref/jetphys.py jpr_ref/jets1000_base.root m3_ref/jpr_master_jets1000.root
```
| artifact | what |
|---|---|
| `jpr_ref/jets1000_base.root` / `.log` | our 1000-event jet arm, `-s 16 -w 1 -J`, rc 0 |
| `m3_ref/jpr_master_jets1000.root` / `.log` | LST master `b42d8f97ad5`, same 1000 events, rc 0 |
| `jpr_ref/base1000_phys.json`, `master_vs_ours_1000.json` | the rate tables |
| `jpr_ref/fun/` (450 x npz+census+log), `funpu/` (30) | the funnel corpus |
| `jpr_ref/funnel_450.{txt,json}`, `funnel_pu.{txt,json}` | the aggregates |
| `jpr_ref/{jio,edgew,reduce_evt,agg,valid_deliv,ceiling}.py` | readers, offline K5 replay, reduction, aggregation, the two validations |
