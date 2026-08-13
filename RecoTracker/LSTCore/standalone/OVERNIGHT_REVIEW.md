# OVERNIGHT REVIEW — 2026-08-12 into 2026-08-13

Written for you to read cold. Sections 1-4 are COMPLETE. Section 5 (agent AT) and section 6
(jet-core round 3) were still running when this was written and are appended below as they land.

**Nothing was shipped without your approval. Nothing was published anywhere.** All commits are on
`chain_tracking_proto`, pushed to `fork` only.

---

## 1. WHAT SHIPPED: `04c6e122e68` — jet-core round 2

Two changes. No new kernels, no new networks, no new weight files.

**A02** — the chain gate head retrained. Same architecture, same 25 inputs, same 3-class output.
**Only the training mix changed**: PU200 (7.07M rows) + jet-core rows at L=0.25 + **131k
`cube5_highPt` gun rows at share .02 with a FLAT per-row weight**. Those gun rows are 99.8% true
chains, so they supply the counterexample the head lacked — isolated high-pT displaced tracks that are
REAL. The old head had learned "dense + high-pT + displaced-looking => fake" from jet cores, which is
right inside a core and wrong for an isolated displaced track. **Trained on the 5 cm gun, gated on the
50 cm gun** — a 10x different displacement scale, so the win is generalisation, not memorisation.

**`dupXcDelta = 1.5f`** — region-conditioned pixel-seed retirement. Where `|eta| >= 1.1` and
`pt < 3 GeV`, the bar for retiring a bare pLS as redundant with a chain that already reconstructed
that track is lowered by 1.5, continuously ramped. Barrel and high-pT never see it. It exists purely
to remove A02's PU200 duplicate cost and is inert elsewhere (jet-core efficiency exactly unchanged,
both cube guns bit-identical).

### Measured on SEALED samples no agent tuned on (my own runs)

| jets holdout, rows 500-999, 500 evt, paired | SHIP | **SHIPPED** | LST master |
|---|---:|---:|---:|
| eff jet-core (genjet pT > 1 TeV) | .6513 | **.7611** | .7761 |
| eff dR < .0025 (innermost bin) | .2403 | **.3942** | .2604 |
| eff dR < .02 | .3138 | **.5172** | .5292 |
| fake | .2197 | **.1287** | .2235 |
| fake TCs/evt inside dR < .05 | 11.17 | **6.22** | 14.98 |
| dup | .0212 | .0241 | .0207 |
| dup dR < .05 | .0118 | **.0232** | ~.003 |
| TCs/evt | 130.6 | 125.6 | 127.6 |

**We pass master in the innermost bin** (.3942 vs .2604) and close 88% of the overall core gap, while
going from fake parity with master to 2.4x cleaner, on FEWER TCs per event.

| PU200 `event_2000`, 1000 evt, 73,470 sims, paired McNemar | SHIP | SHIPPED | p |
|---|---:|---:|---:|
| eff overall | .8107 | .8107 | 1.0 (215 vs 216 discordant) |
| eff dxy[1,5) | .5755 | .5748 | **.92** |
| every other displaced band | — | — | unresolved, p .16 to 1.0 |
| dup overall / transition | .0448 / .01125 | **.0431 / .00948** | both BELOW ship |
| fake overall | .0432 | .0447 | +3.4% relative |
| total TCs | 1,577,688 | 1,577,093 | -595 |

**Round 1's failure mode did not repeat.** Its best ranking arm measured dxy[1,5) at −.0143,
p 2.2e-08, on this exact file; this candidate measures −.0006 at p .92.

Cubes: cube50_highPt dxy[1,5) **.0795 -> .1209** (+52%, 112 gained / 0 lost, p 4e-34); dxy[5,10)
.0213 -> .0649; cube fake and dup both DOWN; cube50 all seven cells up. This was round 1's hardest
blocker, inverted.

### The four known costs, none hidden
1. **jet-core dup (dR<.05) .0118 -> .0232**, ~8x master. It is the (delivered chain, un-retired bare
   pLS) pair in the deep core; two agents independently showed no bar or mutual-argmax rule reaches it.
2. **PU200 fake_overall +3.4% relative.** Bars provably cannot move it (chain admission ratio 1.0011
   of ship); it is 73% T4-class, barrel, pT > 3 GeV.
3. **NOT TIMED.** You took timing out of scope for physics rounds. A quiet-box pass is still owed.
4. **Off-policy at attach** — see section 5. You caught this one yourself.

---

## 2. PROVENANCE AND REPO WORK (commits `206e1a35933`, `d4cf7ba409c`, `a607768b6e2`, `89fa1e9016d`)

### The bad news you were right to be angry about
`analysis/DNN/` had not been touched since `ba799a35912`, while the deployed weights were retrained
three times over — the NN loop (`49f54d8c971`, `81a9afe2d00`) retrained all three heads and deleted
the fourth network, jet round 1 retrained the gate, and round 2 retrained it again. **None of those
commits updated that directory.** So every `train_*.py` there reproduced NOTHING we ship:
`train_chain.py` was the M6-era "plan 5a" recipe, `train_edge.py` predated arm G's 3-class equal
weighting, `train_attach.py` predated the 22-input head.

### Fixed
The live recipes are now installed in `analysis/DNN/` as the canonical scripts:

| canonical file | now holds |
|---|---|
| `train_chain.py` | `g2_ref/train3mix2.py` — the shipped chain-gate recipe (mixing, `--share`, `--flat`, `--dup-mode`) |
| `train3.py` | `nnloop_ref/s2_work/train3.py` — the shared recipe it imports (COND, build_inputs, event_split) |
| `train_edge.py` | `nnloop_ref/s1_work/train_s1.py` — arm G |
| `train_attach.py` | `nnloop_ref/s3_work/train_s3.py` — the 22-input head |
| `export_edge_weights.py` | `nnloop_ref/s1_work/export3.py` — the arm-G DUAL-table exporter |
| `export_chain_weights.py` | `p3_ref/export3p.py` — wrote the deployed chain header |

Two bugs found while doing it:
- **The edge exporter could not run in this environment at all** — `tensor.numpy()` raises "PyTorch was
  compiled without NumPy support" under the CMSSW python. Nobody could have reproduced an edge header
  on this machine. Fixed via `.detach().cpu().tolist()`, exact for float32.
- **I first installed the WRONG edge exporter.** `export_edge.py` is an older single-table script that
  KeyErrors on `wp_G.json`. **THE TRAP: there are two different `export3.py`** — `s1_work/` exports the
  EDGE head, `s2_work/` exports the CHAIN head. Also, the deployed edge header's own "GENERATED by
  export_edge.py" comment is STALE; trust the re-export check over it.

### All three networks are now provably reproducible
Each deployed header self-documents its model and command. Re-exporting reproduces it:

| network | artifact | result |
|---|---|---|
| chain gate | `chain3_A02.pt` + norm + bars | **BYTE-IDENTICAL**, md5 `8d1e194ae350` |
| edge | `edge_G_pinned.pt` + `wp_G.json` | **2707/2707 float literals identical**, `5edc5f174314` |
| attach | `A2_cos3e3.pt` + `A2_cos3e3_norm.json` | **1265/1265 identical**, `c9f315c2bf37` |

Edge and attach match on payload rather than byte-for-byte because the exporters emit the PROTOTYPE
form (bare namespace, `PROTOTYPE_*` guard) which is rewrapped at install time. **The test
discriminates**: attach exported from the wrong candidate (`A1_ship.pt`) gives a different payload.

**Run this check on every future weights change.** It is the only thing that would have caught the
stale-script problem earlier.

### Also corrected
`nnloop_ref/PAIRDUMP_FORMAT.md` documented 20-float pair records; `kAttachFeatures` is **22**. The
instrument was always right (`float x[kAttachFeatures]` with a static_assert) — only the doc was
stale. The trap it created is real: `s3_work/train_s3.py` splices
`concatenate([a[:,:11], b, a[:,12:]])`, which on a 22-wide row silently **drops slot 11 and shifts
eight columns**. Agent AT found this before it corrupted its training.

### Cleanup
39 finished-round `*_ref` dirs and 18 old `proto*` dirs moved to `standalone/archive/` (gitignored —
on disk and in git history, out of the way). **87.7 GB of finished-round output ROOT files deleted**,
preserving everything under `lab/` or `models/` and anything named `master`. 54 stale
`.make.log.<timestamp>` removed. Disk **173 GB -> 260 GB free**. `prototype/` kept (shipped-head
provenance).

**Left for you to decide:** the two loose `LSTNtuple_PU200RelVal_*evt.root` in `standalone/` (15 GB).
Regenerable, but cited 1788 and 7 times in tracked findings, so I did not delete them unilaterally.

---

## 3. AGENT JPR2 — the cumulative jet recon (COMPLETE, full record in `FINDINGS_JETPHYS2.md`)

This is the most important document of the night. It re-attributed everything on the new baseline and
**changed where the problem is.**

1. **The funnel re-ranked.** Budget 6,747 -> 4,559 core sims. The claim is still the largest stage but
   its absolute loss HALVED (2,928 -> 1,442). New order: claim **.316** > gate **.181** >
   **a1 .171 (NOT OURS, recoverable ZERO)** > weld **.131** > purity **.109**. Stage g (purity) is the
   only stage that GREW.
2. **Master is no longer the frontier in the deep core.** We deliver **.820** of what the weld reaches
   (was .675), and **.500** in the deep core (was .315). Inside dR .05 the chain route welds .855 and
   matches .609 — **.246 of core efficiency built and thrown away**, against a .0148 gap to master.
   In the innermost bin we are **+.075 above master while still delivering only half our own reach**.
3. **The residual gap moved to an axis nobody had examined**: sim pt **10-100 GeV**, genjet pt
   **>= 500**, **|eta| < 0.6**, dR **.005-.04 annulus**. The whole −.0148 core gap is −540 sims at
   pt 10-100, partly repaid by **+148 above 300 GeV, where we beat master by +.1945** (master's pT5
   route collapses above 100 GeV; a bare chain needs no pixel seed).
4. **Softer jets — worth knowing.** Every table in the entire two-round record selected
   `genjet_pt > 1000`. On **200-500 GeV** jets we are **+.038 ahead of master in the annulus**, and
   +.016 on 50-200 GeV. The deficit exists only for genjet pt >= 500. This population dominates real
   data and nobody had looked at it. It is now a hard gate for round 3.
5. **The core duplicate cell is provably unreachable by the shipped rule** — 0 of 150 (chain, bare
   pLS) pairs lie inside its window; 96.7% are BARREL with a median pLS pt of **73.7 GeV**, the exact
   complement of (|eta|>=1.1, pt<3) on both legs. Exactly as the ship commit claimed.
6. **A fake BUDGET, not an exchange rate**: we are **8.29 core fake TCs/evt below master** while the
   whole remaining claim budget is 3.20 core sims/evt. But PU200 has NO headroom, so admission levers
   must be region- or density-conditioned.
7. **Lever list with ceilings priced on this head**, and **T4-class policy named as the one lever
   nobody has ever priced**: 37.5% of core fakes, 42.4% of the claim's recoverable ceiling, 73% of the
   PU200 fake excess, delivered non-fake fraction only .489.

---

## 4. AGENT SF — high-pT WP refit (COMPLETE: SHIP NOTHING, full record in `FINDINGS_SF.md`)

**The mission's premise is falsified in deployment.** SF was parked with an offline result saying
2-way |eta| regrouping of the pT>5 weld bars was best. Deployed, it **changes 2 sim tracks in 75,422,
both losses, zero above 5 GeV.**

**Why the proxy lied, diagnosed by SF itself:** `wpsf.py` pins each group's bar to reproduce *the
shipped head's own acceptance*, so regrouping preserves acceptance BY CONSTRUCTION. Its split-half
diagnostic measured instability of the fitted TARGET ESTIMATE, not of delivered efficiency — it had
conflated the two. That is a clean self-refutation.

**The ceiling arm settles it.** `SFOPEN` sets all twenty pT>5 entries of both tables to −1e30 (every
pT>5 edge weld-eligible — the strict upper bound on every regrouping and every loosening). It moves
the target pt[25,50) band by **ZERO sim tracks**, adds **8 chain TCs in 63,851**, and makes fake and
three displaced bands slightly worse. Confirmed on a **5000-event holdout** (`event_3000..7000`,
370,888 sims): pt[25,50) moves 1 sim vs 1 on 5,203.

**Why no edge-table arm can ever work here:** chain TCs carry only **3.3%** of matched sims at
25-50 GeV; **35 of 35** sims master finds and we miss in that band are carried by master's
**pT5/pLS/pT3** (zero T5, zero chain); and for all 35 we produce **no overlapping TC at all**. It is a
pixel-seeded **supply** deficit, not a working-point problem. SF recommends against ever commissioning
another edge-WP round for prompt high-pT.

**Also: the dip is +.0142, not −.024** on this baseline — 15 net sims of 1,054.

### One genuine trade SF found and correctly refused — you may want to look at it
On `cube50_highPt` the bars DO bind hard. `SFD10` gains **110/106/88/52 sims** across the displaced
bands with 0-1 losses each (p to 1.5e-33), **+8% to +51% relative**, and LOWERS cube dup. The
decomposition is clean: **the entire effect is `kWpDisp`** — the prompt table contributes nothing, so
the honest lever is 10 numbers, not 20. But the 5000-event holdout priced the other side: PU200
dxy[5,10) **p=.0025**, dxy[10,30) **p=.00082**, vxy[10,30) **p=.043**, fake +.0002. **It moves
displaced efficiency from PU200 to the high-pT gun.** SF rejected it because the hard gate protects
PU200 — not because the gun result was wrong. If you ever decide the guns matter more than PU200 in
that cell, `SF_D10.patch` (md5 `15f3386d0401d0821ffaaff52e4dae4a`) is banked and touches only 10
numbers in `EdgeNetworkWeights.h`.

### Three traps SF surfaced that outlive its round
1. **`p4_ref/paired_rle.py` silently produces nonsense against MASTER's ntuple** — it reports master
   `eff_dxy_0_1` as .0903 against a true .8297. Anyone who published a master-paired number from it
   must re-derive it. (I checked: every master column I committed came from `jetgate.py`; I only ran
   `paired_rle.py` on our-binary-vs-our-binary. Nothing of mine is affected.)
2. **The `cube50_highPt dxy[1,5) = .1209` gate number is a 5000-event value; over 10,000 events it
   reads .1120.** Quote the event count with it.
3. **A 1000-event displaced band cannot rank arms in either direction** — dxy[1,5) looked worst at
   p=.077 and came back neutral at p=.91, while dxy[5,10)/[10,30) looked like noise and came back
   significant. Use 5000 events for displaced verdicts.

**SF's parting recommendation:** the live lead for high-pT is the **pT5/pT3/pLS supply**, which is
pLS-side, and which it did not touch.

---

## 6. JET-CORE ROUND 3 — two of four agents complete, TWO REAL CANDIDATES

Full posts in `FINDINGS_JETROUND3.md`. I am judging both on the SEALED holdout (jets 500-999 and
PU200 `event_2000`) plus their union; those tables append below when the runs land.

### 6a. Agent WELD — `kChainWeldSweeps: 3 -> 2`. The diagnosis inverted the assumption.

**What the argmax actually loses to:** `.7017` of the slots a stage-d true edge fails to win are **still
EMPTY when the weld finishes** — the competitor that out-scores it mostly never welds either. The
mechanism is in the kernel: K6a lets an edge compete only while BOTH its slots are free, so a node's
argmax cannot advance past its own rank-1 edge until some other weld removes the blocker. K6a/K6b is
an iterated mutual-best whose fixed point is the greedy matching by key; it needs **12-20 sweeps** on a
jet core and **ships with 3**.

**It also falsified a documented claim:** `FINDINGS_JET.md:363-368` / `ChainConfig.h:292` assert the
weld is "at most 3 ranks deep" as a theorem. Empirically max rank is **9** (.9726 within 3). M2's
parked `C >= 16` result survives by measurement, not by its stated proof.

**But converging it is monotonically HARMFUL** — the marginal weld's same-sim purity collapses by
sweep: .5487, .3126, **.1208**, .0529. The third sweep's welds are **88% wrong**. Sixteen sweeps costs
.0164 core efficiency and adds .0108 fake. So the lever runs the other way: do LESS weld work.

| `weld_S02` (md5 `baa19ab0654220cc445f2287bb876dd1`, ONE constant, one file, a third less weld work) | result |
|---|---|
| jets tune core eff | .7659 -> **.7695** (p 9.9e-04 paired) |
| jets dR<.02 | .5074 -> **.5169** (p .0024); all 8 aggregates >= 0 |
| sim pt 100-300 GeV | **+.0192** |
| jets fake | .1317 -> **.1271** |
| PU200 | **all seven displaced bands neutral-or-better** (three significant); fake and dup DOWN everywhere — takes back 2.1% relative of round 2's fake regression |
| cube50_highPt | exactly **.00000** on every efficiency and fake band; dxy[1,5) preserved at .12089 |
| softer jets (200-500 GeV) | **PASS**, +.0047 |
| **debit** | jet-core dup (dR<.05) .0313 -> **.0345** (p .017), a cell we already owe master |
| **debit** | sim pt > 300 GeV **−.0210** (16 sims), where we lead master by +.1945 |

Sparse-region invariance passes *mechanically*: the third sweep has nothing to do where the iteration
already converged, so no density conditioning is needed. **Refused with numbers:** a `weldKey` re-key
separating the argmax scalar from the summed `logOdds` — built, proven inert at mode 0, deployed, and
all three variants negative or negligible (min(mP,mD) −.0042 core). Its offline proxy claimed .2226 of
stage-d recovered: **the fifth proxy mispredict in this project.** Stage d's remaining ~+.026 is a
genuine ranking loss (.8230 of stage-d sims have every eligible pair strictly out-ranked, median rank
63 of 195) and is not reachable from inside the weld.

### 6b. Agent T4 — the class is net positive and UNDER-admitted. Candidate `T4C1`.

**Suppression is forbidden by the crown jewels, measured.** Deleting the class costs, on PU200 paired
McNemar, with **zero displaced sims recovered by any other route**: dxy[10,30) .0538 -> .0228 (−58%
relative), dxy[5,10) .2502 -> .1917, dxy[1,5) .5621 -> .5318, vxy[10,30) .6963 -> .6764 — **284 sims
lost, 0 gained, p 2e-28 to 3e-15**. On jets it costs −.0218 core (−.0635 at dR<.02), and killing it at
the GATE is worse on both axes (−.0266 AND jet fake .1317 -> .1359) because the freed hits go to fakes.
This also closes round 2's unmeasured `t4p10` leftover.

**The upside runs the other way:** 677 of 873 stage-e core sims (.776) have a gate-killed chain made
entirely of their own T3s, 4 layers long, and **697 of 700 are branch 0 — the 4-layer IP bar, where the
gate kills 97.97%.**

| `T4C1` — density-conditioned loosening of the 4-layer IP gate bar (three floats, no kernel, no weight file, no new cell) | result |
|---|---|
| jets tune core eff | **+.0091** (p 1.0e-31) |
| jets dR<.02 / dR<.005 | **+.0241 / +.0261** |
| fine bins | **every bin from [0,.0025) to [.03,.04) positive AND significant**, incl. innermost +.0218 and the peak-gap bin [.0125,.015) +.0212 |
| jets fake | pooled −.0005, core −.0029 |
| PU200 | **3 discordant sims in 75,422, ALL GAINS**; +8 TCs of 1,583,791; displaced bands move slightly UP |
| both cubes | **BIT-IDENTICAL**, including the full 10,000-event cube50_highPt (all 21 fields, 0/0); dxy[1,5) unchanged at .1209 |
| softer jets | **PASS EXACTLY** — +.0000 in every dR band on 50-200 and 200-500 GeV; the whole gain sits at genjet pt >= 500 |
| JPR2's relocated gap | sim pt 50-100 gap to master −.0783 -> **−.0540** (31% closed); 20-50 −.0748 -> −.0607; >300 GeV +.1945 -> **+.2089** |
| **only adverse cell** | jet-core dup (dR<.05) .0313 -> .0324, **p .066, unresolved** — +0.02 core dup TCs/evt against +0.40 core sims/evt (**18:1**) |

Patch `t4_ref/t4_class_policy.patch`, md5 **`6550c79df1ff469319bdb44fbeabd606`**, 274 lines, 6 files,
inert by default and bit-identity-proven. Frontier variants banked: `T4C3` (+.0082, PU200 literally
0/0 discordant and n_tc +0) and `T4R10` (+.0096, PU200 dxy[1,5) 0 lost / 4 gained).

### 6c. **Your pT4 question, answered with a measurement: DO NOT PORT.**

`LST_T4_ATTACH_MIN=4` makes 4-layer chains real attach targets, and a granted seed upgrades the row in
place to type-7 with pixel hits prepended — **that object already IS a pT4, for one constant.**
Deployed: 927 confirmations per 500 jet events, and the net is **−16 core sims on jets (p .72)**; PU200
is +.0007 (2 lost / 57 gained, displaced bit-clean, fake better) but `dup_barrel` **+.00060**, which is
8:1 against us in the currency we already owe. So the port's product is already measured at ~zero — and
the branch commits sit on LST's T4 object that this tree no longer has, making it a rewrite rather than
a cherry-pick. Recorded, not recommended.

### 6d. Cross-agent notes the two of them left
- For **KEY**: 552 of 1,481 stage-f core sims (37% of the stage, rising with sim pt) have a pure
  gate-alive chain that is *only* 4-layer; `T4C1` makes that population bigger (T4 core-eff share
  1,016 -> 1,307).
- For **PUR**: a trim from 5 -> 4 layers lands in a class whose delivery-given-gate-alive is **.0415 vs
  .2085** — about 5x less likely to win its claim. That belongs in the ledger next to the dup hazard.
- For **AT**: neither agent touched attach weights or any of the eight attach bars.

### 6e. The shape of the decision
Both candidates move efficiency up and jet fake down. Both pay in the same single cell — jet-core
duplicates — where we already owe master. T4C1 is the cleaner of the two (PU200 all gains, cubes
bit-identical, softer jets exactly neutral, dup cost unresolved at p .066); WELD's is a one-constant
change that also improves PU200 fake and dup but its dup debit is resolved (p .017) and it loses
−.0210 above sim pt 300 GeV where we currently lead master by +.1945. **They are not independent — both
act on what reaches the claim — so I am measuring the union, not assuming it.**

### 6f. MY SEALED-HOLDOUT JUDGMENT OF BOTH CANDIDATES AND THEIR UNION — **the union is ADDITIVE**

Three arms built from `04c6e122e68` in separate CMSSW areas, each run printing its own binary and
library md5 (`d3`=WELD `bc0008d46dd8`, `r2`=T4C1 `9acc2b87fa75`, `r2b`=UNION `265fb3862ef2`; all three
0 `error:` in a fresh make log). Baseline is my round-2 sealed run of the shipped head.

#### JETS, holdout rows 500-999, 500 events, paired (`p4_ref/jetgate.py`)

| cell | SHIPPED | WELD | T4C1 | **UNION** | LST master |
|---|---:|---:|---:|---:|---:|
| eff jet-core | .7611 | .7648 | .7695 | **.7728** | .7761 |
| eff dR < .005 | .4051 | .4096 | .4218 | **.4288** | .2604 |
| **eff dR < .02** | .5172 | .5227 | .5389 | **.5424** | **.5292** |
| eff dR < .05 | .6097 | .6150 | .6262 | **.6310** | .6370 |
| eff all-sim | .8081 | .8100 | .8118 | **.8136** | .8141 |
| fake | .1287 | .1238 | .1282 | **.1236** | .2235 |
| fake TCs/evt dR<.05 | 6.22 | 5.98 | 6.31 | 6.09 | 14.98 |
| dup (pooled) | .0241 | .0235 | .0244 | **.0238** | .0207 |
| dup dR < .05 | .0232 | .0231 | .0242 | .0242 | ~.003 |
| **dup dR < .005** | **.0506** | .0656 | .0627 | **.0731** | .0000 |
| TCs/evt | 125.6 | 125.3 | 126.0 | 125.7 | 127.6 |

**The union is additive, not antagonistic** (+.0117 against +.0037 and +.0084 measured separately) —
the opposite of round 1's union, and it is additive because the two act at different stages.

**Two milestones on the sealed sample:** we now **PASS master inside dR < .02** (.5424 vs .5292), and
the overall core gap falls from .0150 to **.0033**. In the innermost bin we are +.168 above master.
Jet fake is 1.8x cleaner than master while total TCs/evt stay below master's.

#### PU200 `event_2000`, 1000 events, 73,470 sims, paired McNemar — BOTH GATES IMPROVE

| cell | SHIPPED | WELD | T4C1 | **UNION** | p (union) |
|---|---:|---:|---:|---:|---:|
| eff overall | .8107 | .8112 | .8107 | **.8112** | **.0044** (66 lost / 104 gained) |
| eff_barrel | .9220 | .9229 | .9220 | **.9229** | **.0066** |
| **eff dxy[1,5)** | .5748 | .5799 | .5748 | **.5799** | **.0226** (14 lost / 30 gained) |
| eff dxy[5,10) | .2566 | .2622 | .2566 | .2622 | .070 |
| eff dxy[10,30) | .0583 | .0590 | .0583 | .0590 | 1.0 |
| eff vxy[1,5)/[5,10)/[10,30) | — | — | — | +.0002/−.0006/+.0002 | 1.0 / 1.0 / 1.0 |
| dup overall | .04315 | .04283 | .04315 | **.04283** | better |
| fake overall | .04466 | .04373 | .04466 | **.04373** | better |
| dup_barrel | .01398 | .01284 | .01398 | **.01284** | better |
| fake_barrel | .05071 | .04864 | .05071 | **.04864** | better |
| n_tc | 1,577,093 | 1,575,583 | 1,577,095 | 1,575,588 | −1,505 |

**T4C1 is essentially bit-clean on PU200: ONE discordant sim in 73,470 and +2 TCs in 1.58 million.**
**WELD's PU200 effect is a resolved IMPROVEMENT**, including a resolved **displaced GAIN** at dxy[1,5)
(+.0051, p .023) and nothing negative anywhere. Its fake improvement recovers **64% of round 2's
+3.4% PU200 fake regression** (.04466 -> .04373 against the pre-round .04321).

#### The one cost, and it is where we already owed
**Jet-core duplicates in the deepest bin: dR<.005 goes .0506 -> .0731.** dup at dR<.05 rises only
+.0010 and POOLED dup actually falls (.0241 -> .0238), so the damage is concentrated in the innermost
bin — exactly the cell where master carries zero duplicates and we were already ~8x worse. Note
WELD's tune-half dup debit (core dup +.0032, p .017) did NOT reproduce on the holdout (−.0001), which
is consistent with SF's warning that small dup cells need far more than 500-1000 events to rank arms.

#### Still outstanding on the union before it could ship
Both cube samples on the UNION binary are running now (T4C1 alone is bit-identical on both, including
the full 10,000-event `cube50_highPt`; WELD alone measured exactly .00000 on every cube50_highPt band
on its own tune runs — but nobody has measured the COMBINATION, and it is a hard gate). Also still
unmeasured: timing, which remains owed on the round-2 ship as well.

### 6g. Agent PUR — stage-g purity repair: **REFUSAL, and the ceiling is structurally unreachable**

Five posts in `FINDINGS_JETROUND3.md`. Nothing built, nothing committed, working tree untouched.

**The signal I named in the brief is the wrong one, and PUR killed it with physics.** Per-mini-doublet
AUC for "is this MD foreign": leave-one-out combined chi2 **.494 (anti-informative)**, |xy resid|
.596, |rz resid| .517. Deployed as rules they lose on THREE independent samples (jets −.0134, PU200
−.0102, cube50 −.0002). The reason is physical and worth remembering: **inside dR<.05 the thief and
the victim are collinear at hit-resolution scale, so a stolen mini-doublet lies on the fitted circle
just as well as the track's own hits.** Fit quality cannot see the theft. Relatedly, the existing K6f
terminal trim cannot be relaxed into this population either — only 26.4% of it passes K6f's
`chi2Full > 1` guard, because a stitched jet-core chain FITS WELL.

**Ceiling reconfirmed at +.0258/+.0278** (D had +.0244), and **100% of stage-g owners are chain TCs**,
so the whole leg is ours. Two candidates were priced:

| | `sharMD>=1` (another emitted chain TC carries the same MD) | `t3 fakeScore > .5` on the terminal node |
|---|---|---|
| jets core | +.0041 (113 gained / 24 lost) | **+.0095** (230/22) — 36% of the ceiling |
| PU200 | +22 sims, all 7 displaced bands 0/0, dup BETTER | +21 sims, all 7 bands 0/0, dup +0 |
| cubes | **fires 0 times, bit-identical** | **FAILS**: 0 gained / 3-4 lost, dxy[1,5) 0/1 |
| cost | new kernel + per-MD counter | ~10 lines in `ChainEmitTCs`, no new launch |

**Why PUR refused anyway, and it is the important number: 65.0% of the mini-doublets that must be
removed are carried by nothing else in the event** (87.9% at MD granularity). So the ceiling is
**structurally unreachable rather than classifier-limited** — there is no evidence to find. Where
evidence does exist, the argmax is right **74 of 74**.

`t3 fakeScore` is the better physics in the wrong shape: clean on jets and PU200, failing only on the
displaced guns because it fires where there is nothing to repair (its AUC there is .42-.53). **Named
next experiment: a continuous occupancy-conditioned `fakeScore` bar** — the round's own allowed
conditioning shape, ~10 lines plus one plumbed column, and the cheapest remaining shot at this stage.

**Instrument note worth keeping:** PUR's purity rule reproduces the ntuple's own `tc_isFake` on
**1,226,917 of 1,226,917** chain-backed TC rows with zero disagreements, across jets, PU200 and both
cubes — an exact proxy for a K10-slot-skip. It was still never deployed, and deployment remains the
only referee.

**For agent T4's ledger:** a purity repair pushes ~169 five-layer chains per 500 jet events INTO the
4-layer class (+0.06-0.34 T4/evt) and none the other way; 70% of the population is 6-layer and never
leaves T5.

### 6h. UNION CUBE GATES — both pass, essentially bit-clean (10,000 common events each, paired)

| cell | cube50_highPt SHIPPED -> UNION | cube50 SHIPPED -> UNION |
|---|---|---|
| overall | .3678 -> .3678, **0/0** | .3509 -> .3509, **0/0** |
| dxy[1,5) | .1120 -> .1120, **0/0** | .1329 -> .1329, **0/0** |
| dxy[5,10) | .0630 -> .0630, **0/0** | .0828 -> .0826, 1 sim, p 1.0 |
| dxy[10,30) | .0085 -> .0085, **0/0** | .0299 -> .0299, **0/0** |
| vxy[1,5) / [5,10) / [10,30) | **0/0 / 0/0 / 0/0** | 0/0 / 1 sim p 1.0 / 0/0 |
| barrel / transition / endcap | **0/0 on all three** | **0/0 on all three** |

**cube50_highPt is 0/0 on every one of the twelve cells.** cube50 moves **two sims in 10,000 events**,
both at p = 1.0. Round 2's displaced headline is fully preserved. (Note SF's warning confirmed: that
headline reads **.1120 over 10,000 events**, not the .1209 of the 5,000-event slice — quote the event
count with it from now on.)

---

## 7. THE DECISION WAITING FOR YOU — the additive union, complete ledger

`weld_S02` (md5 `baa19ab0654220cc445f2287bb876dd1`, ONE constant) + `T4C1`
(md5 `6550c79df1ff469319bdb44fbeabd606`, three floats, inert by default). No new kernels, no new
networks, no new weight files, and the weld does a THIRD LESS WORK than it does today.

| gate | result | verdict |
|---|---|---|
| jets holdout core eff | .7611 -> **.7728** (master .7761) | gap .0150 -> **.0033** |
| **jets dR < .02** | .5172 -> **.5424** (master .5292) | **WE PASS MASTER** |
| jets dR < .005 | .4051 -> **.4288** (master .2604) | +.168 above master |
| jets fake | .1287 -> **.1236** (master .2235) | 1.8x cleaner than master |
| jets dup pooled | .0241 -> **.0238** | better |
| PU200 eff overall | .8107 -> **.8112** | **resolved GAIN**, p .0044 |
| PU200 eff dxy[1,5) | .5748 -> **.5799** | **resolved DISPLACED GAIN**, p .023 |
| PU200 other displaced bands | all neutral | pass |
| PU200 dup / fake | .04315 -> .04283 / .04466 -> **.04373** | both better; recovers 64% of round 2's fake regression |
| cube50_highPt | **0/0 on all 12 cells** | pass |
| cube50 | 2 sims of 10,000, p 1.0 | pass |
| softer jets (200-500 GeV) | WELD +.0047, T4C1 +.0000 | pass |
| **jet-core dup dR < .005** | **.0506 -> .0731** | **the ONLY adverse cell** |
| jet-core dup dR < .05 | .0232 -> .0242 | +.0010 |
| timing | **NOT MEASURED** | owed, on this and on the round-2 ship |

**Every gate in the set passes except the deepest duplicate bin.** That is the same cell round 2 also
paid in, where master carries zero duplicates and we were already ~8x worse — and pooled jet dup and
all four PU200 dup cells actually IMPROVE, so it is a narrow concentration rather than a broad
regression.

**What I would want you to weigh:** the prize is passing master inside dR<.02 on sealed data and
closing the core gap to .0033, with PU200 efficiency and displaced BOTH improving and the round-2 fake
regression partly repaid — against duplicates in the innermost jet bin getting worse in a cell we
already owed. Under your stated priority (efficiency >> dup > fake) the efficiency and fake columns
both move the right way and only one dup cell moves the wrong way.

**My recommendation: ship the union, and open the next round on the deep-core duplicate**, which is now
the single cell blocking a clean sweep and which three agents have independently localised to the
(delivered chain, un-retired bare pLS) pair — barrel, high-pT, hit-disjoint, and provably outside the
shipped retirement rule's window. I have NOT shipped it; the call is yours.

Still running when this was written: agent KEY (claim ordering) and agent AT (attach retrain). Their
sections append below.

### 6i. Agent KEY — `KE50`, a third candidate, plus the round's deepest structural finding

`KE50` puts an **eta-ramped weight on the hinge the order key already has**:
`orderKey = score - alphaEff*max(0, 5 - marginX)`, with `alphaEff` ramping from 50 at
|eta(innermost T3)| < 1.0 down to the shipped 10 by |eta| 1.3. **No new SoA column, no new kernel, no
weight file, no bar moved** — the eta value was already computed in that same kernel for the -WZ braid
band and is simply hoisted above the key. Patches `key_ref/key_orderkey_{inert,KE50}.patch`.

Its own tune-half measurements: jets core +.0042 (p 7.1e-06), dR<.02 +.0104, **|eta| < 0.6 +.0060 and
sim pt 100-300 +.0133** — exactly the cell JPR2 relocated the master gap into. Jet core fake DOWN
−.0059 (p .036). PU200 dup better at −3.9 sigma, fake better at −4.9 sigma, dxy[1,5) exactly flat
(b8 c8). **Both cubes: 0 discordant sims in all 12 cells at the full 10,000 events.** Softer jets:
200-500 GeV exactly inert, 50-200 GeV +.0007.

**The structural finding, which is worth more than the +.0042 and which unifies this round:**
**per-chain regime conditioning of a RANK KEY is structurally broken.** The same boost conditioned on
the chain's own `ptEst` is core **−.0747**; on junction degree **−.0360**; unconditioned it is +.0054.
A rank key is a *global total order*, so a per-chain weight scrambles comparisons across regimes.
**Eta is the exception because chains contending for the same hit SHARE it.** T4's density conditioning
works for the mirror-image reason: its knob is a per-chain *threshold*, not a rank weight. That is a
design rule for every future round: condition a threshold per chain, but only condition a rank key on
something the competitors share.

**It also closed L1 at the duel level rather than by tuning.** Of the duels the claim actually loses,
**a third have a thief that is itself a legitimate >=75% track**, and on the remainder **no function of
the chain's columns wins — including a trained ranker at AUC .973**, which takes only .41-.52 of them.
The information is not on the chain. And it re-priced the weight file in this world at **+.0113**
(C measured +.0224 pre-ship; the round-2 gate halved it again) against **+.0059** for free literals and
an oracle ceiling of +.0483 — so the expensive option is now barely better than free, the cheap linear
distillation is *worse* than the free form, and round 1's C1 replays at **−.0047**.

Caveats it stated: +.0042 is 9% of the +.0459 target; jet-core dup drifts +.0012 (p .27, unresolved);
jets |eta| 1.1-1.7 is −.0054 (7 sims of 1,291, where the ramp turns off); PU200 `eff_transition`
−.0006 (p .096). `KE20` halves all three for a fifth less gain. The unconditioned form
(`orderAlpha = 20`) is **rejected** — same jet gain, PU200 duplicates +3.4 sigma.

### 6j. THE THREE-WAY UNION ON SEALED JETS — **we now BEAT master on every jet efficiency aggregate**

Built in `gpu_wt/r2b` (bin `43990ea976ee`, 0 `error:`), all three patches applied, judged on jets
rows 500-999 which no agent opened.

| cell | SHIPPED | WELD | T4C1 | KE50 | UNION2 | **TRIPLE** | LST master |
|---|---:|---:|---:|---:|---:|---:|---:|
| eff jet-core | .7611 | .7648 | .7695 | .7658 | .7728 | **.7788** | .7761 |
| eff all-sim | .8081 | .8100 | .8118 | .8103 | .8136 | **.8163** | .8141 |
| eff dR < .005 | .4051 | .4096 | .4218 | .4167 | .4288 | **.4429** | .2604 |
| eff dR < .02 | .5172 | .5227 | .5389 | .5296 | .5424 | **.5561** | .5292 |
| eff dR < .05 | .6097 | .6150 | .6262 | .6199 | .6310 | **.6430** | .6370 |
| fake | .1287 | .1238 | .1282 | .1270 | .1236 | **.1203** | .2235 |
| fake TCs/evt dR<.05 | 6.22 | 5.98 | 6.31 | 6.20 | 6.09 | 6.00 | 14.98 |
| dup pooled | .0241 | .0235 | .0244 | .0244 | .0238 | **.0242** | .0207 |
| dup dR < .05 | .0232 | .0231 | .0242 | .0245 | .0242 | .0253 | ~.003 |
| **dup dR < .005** | **.0506** | .0656 | .0627 | .0620 | .0731 | **.0871** | .0000 |
| TCs/evt | 125.6 | 125.3 | 126.0 | — | 125.7 | **125.6** | 127.6 |

**The three are additive — slightly super-additive.** Separately +.0037, +.0084, +.0047 = +.0168;
together **+.0177**.

**FOR THE FIRST TIME WE BEAT LST MASTER ON EVERY JET EFFICIENCY AGGREGATE ON SEALED DATA:**
jet-core .7788 vs .7761, all-sim .8163 vs .8141, dR<.02 .5561 vs .5292, dR<.05 .6430 vs .6370, and
dR<.005 .4429 vs .2604 (+.183). Simultaneously jet fake is **1.86x cleaner** than master (.1203 vs
.2235, 6.00 vs 14.98 fake TCs/evt inside dR<.05) on **fewer TCs per event** (125.6 vs 127.6).

**The cost is unchanged in kind and larger in degree: duplicates in the deep core.** Pooled jet dup is
essentially flat (.0241 -> .0242, still below master-adjacent levels in the aggregate) and dR<.05 rises
+.0021, but **dR<.005 goes .0506 -> .0871** where master carries zero. Every point of this round's
efficiency has been paid for in that one bin.

PU200 `event_2000` for KE50 and the TRIPLE was still running when this was written; those rows and the
TRIPLE cube gates append below. **Nothing has been shipped.**
