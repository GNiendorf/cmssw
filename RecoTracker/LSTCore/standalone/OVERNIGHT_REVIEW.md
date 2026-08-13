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
