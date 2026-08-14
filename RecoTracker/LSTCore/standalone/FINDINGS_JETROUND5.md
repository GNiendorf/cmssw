# JET ROUND 5 -- SHARED FINDINGS FILE

**Baseline HEAD: `efb7e1e98c5`** (physics identical to `30de402d238`, the G10 ship; the later
commit adds only the JPR4 recon file).

**GOAL ORDER: jet-core EFFICIENCY first, then FAKE, then duplicates.** Duplicates are a cost
column, not a mission -- master's ~0 jet dup rate is an artifact of its low jet efficiency.

**READ FIRST:** `FINDINGS_JETPHYS4.md` (the recon this round is built on). Do not re-derive its
funnel, its reach ladder, its recoverability table or its closed list.

---

## THE STATE OF THE PROBLEM IN ONE PARAGRAPH

We deliver **.8217** of jet-core sims on the 450-event tune corpus. The remaining budget is
**3,511 core sims (7.80/evt)**. The weld already BUILDS a chain for 1,818 of them, so
"deliver what we already build" = **.9140**, and that distance is an EXACT identity with stages
e + f + g (1,818 = 229 + 1,379 + 210). **Stage d (591 sims, +.0300) is NOT on it** -- it is the
only lever that expands the reach itself. 73.6% of all remaining loss is above 20 GeV. 58.8% of it
sits on sims with another selected sim inside dRnn .005. Against master we lead on every jet
aggregate, so master is no longer information -- judge against the REACH ceiling.

## THE FAKE BUDGET (jets only; PU200 has none)

| | MASTER | THIS HEAD | headroom |
|---|---:|---:|---:|
| fake TCs/evt, dR < .02 | 7.58 | 1.91 | 5.67 |
| fake TCs/evt, dR < .05 | 13.95 | 5.34 | **8.61** |
| fake TCs/evt, pooled | 27.69 | 13.64 | **14.05** |

**We can spend ~2.81 admitted fake TCs per recovered core sim and still be cleaner than master.**

---

## HARD RULES -- ALL THREE AGENTS

1. **SEALED**: jets **500-999** and PU200 **`event_2000`**. Never opened. Tune halves are jets
   0-499 and PU200 **`event_1000.root` NAMED BY EXPLICIT PATH** -- `-i PU200RelVal` is the
   DIRECTORY and globs the sealed files. This has already bitten one agent.
2. **NO TIMING GATES.** Physics round. No broker, no run-holds, no ms/evt claims. Report added
   work as a COUNT if your arm adds any.
3. **NO CUBE IN TRAINING.** The cube samples are artificial displaced gun samples used to TEST
   displaced behaviour. Cube is nearly empty and PU200/jets are dense, so any occupancy-derived
   input is a near-perfect SAMPLE FINGERPRINT. If your arm fits or refits anything, cube rows are
   OUT unless the same gain shows on PU200's own `dxy`/`vxy` bands.
4. **Density conditioning on local physical observables is LEGITIMATE; recognising the artificial
   cube samples is FORBIDDEN.** The safe construction is `T4C1`'s: a ramp identically zero at and
   below a knee the cubes sit entirely below, so cube behaviour is invariant BY CONSTRUCTION and
   then VERIFIED bit-identical. Density as a RANK score is CLOSED (three independent measurements:
   AUC .517, .499, LR 1.32).
5. **BOTH CUBES ARE A GATE on every candidate.** `cube50` and `cube50_highPt`. Bit-identical is
   the target; movement must be a GAIN and must be priced.
6. **PU200 is a CONSTRAINT.** The crown jewels bind: overall efficiency, four `dxy` bands, three
   `vxy` bands. A jet gain that costs displaced efficiency is not a candidate.
7. **DUPLICATES** are a cost column. Report deep-core (dR<.005) and dR<.05. Do not let an arm blow
   them up without a reason; do not commission work to close them.
8. **DECLARE BEFORE BUILDING** if your arm touches a file another agent owns, and **MEASURE
   UNIONS, NEVER SUM**. Ownership this round: **A owns `ChainWeld.h` + the weld WP tables. B owns
   `ChainGate.h`. C owns `ChainArbitrate.h`.** Anything crossing those lines gets posted here first.
9. **NO PUBLISHING.** Local PNGs under `standalone/<your>_ref/`, paths quoted here.
10. **All artifacts under `standalone/<your>_ref/`. NOTHING in /tmp.** Disk is tight -- clean ROOT
    files as you go.
11. **POST HERE**, append-only, `## [AGENT hh:mm] HEADLINE` stating the RESULT not the activity,
    caveats riding WITH the headline never as fine print. This file is the cross-agent channel;
    do not expect coordinator messages.
12. **A candidate is:** a patch with md5, exact reproduction commands, all gate numbers on the TUNE
    halves, and an honest statement of what you did NOT measure. Nothing is shipped by an agent;
    the coordinator judges on the sealed halves.

## MEASUREMENT DISCIPLINE (earned the hard way)

* **Offline claim-replay EFFICIENCY predictions must be divided by ~2.4** (the greedy cascade);
  COST predictions are accurate at face value. Measured independently by two agents.
* **Offline proxies have mispredicted deployment 5+ times**, most recently in JPR4 where AUC and
  deployment disagreed IN SIGN on the order key. An offline result is a licence to build, NEVER a
  result.
* 1000-event deltas under ~.002 are noise. Paired McNemar for small effects. The writer emits
  events in stream-completion order -- naive entry pairing is nonsense.
* **Prove inertness, do not assert it**: a knob-free arm of your binary must be bit-identical to
  the shipped binary before any A/B number is quoted.
* Fine core binning is a maintainer directive: ~.0025-wide bins below dR .02, counts with rates.
  The `-J` deltaR hist axis caps at 0.1 and 36% of jet-core sims are beyond it -- read deltaR
  unbinned off the ntuple.
* **KNOWN BUG, avoid don't chase:** our CUDA build fails reproducibly with
  `cudaErrorInvalidDevice` on `cube50` / `cube50_highPt` (master's CUDA build is fine; ours is fine
  on PU200 and jets). **Run the cube samples on CPU.**

## BUILD

```bash
pushd <your area>/src/RecoTracker/LSTCore/standalone \
  && source setup.sh && cmsenv && source setup.sh && <cmd>
```
* `cmsenv` is an ALIAS -- in a script use `eval $(scramv1 runtime -sh)` then RE-SOURCE `setup.sh`.
* `lst_make_tracklooper` prints "compilation successful" even when a TU fails. Grep the FRESH
  `.make.log.<ts>` for `error:` **with the colon** AND for `failed to compile`. Prefer `-m`.
* `set -u` breaks under `setup.sh`.
* Do NOT build in the main tree.

---

---

## [G5 21:50] DECLARATION -- G5 takes `ChainGate.h` (R2, the four branch bars + the far-dca free pass) and adds INERT env scaffolding to the SHARED `interface/ChainConfig.h`

What I am touching, so nobody collides with it:

* **`src/alpaka/ChainGate.h`** -- mine this round (round brief). Four call sites in `ChainGateKill`,
  one new inline helper `chainG5Bar`, plus a re-apply of N4's `t4_fardens.patch` (md5
  `1928b66f3aea47f231a19ed3f2f791a1`) which reaches the same block.
* **`interface/ChainConfig.h`** -- SHARED, so this is the declaration. Additive only: ten new floats
  (`gB0Delta..gB3Delta`, `gB0Dens..gB3Dens`, `gBarRho0`, `gBarDecades`), all defaulting to 0 /
  inert, and ten `LST_G5_*` reads appended inside the existing `chainConfigT4Env`. **No existing
  field, default or literal is modified**, so a rebase over anyone else's ChainConfig edit is a
  pure append. If W5 or C5 also needs env scaffolding there, append below mine.

Semantics, so the numbers are readable: a **POSITIVE** delta TIGHTENS its branch (kills more),
a NEGATIVE one LOOSENS it. `gBnDens` is the same delta ramped on `ChainFeatures[14]` with the G5
family's own knee (default 100, one decade), identically zero at and below the knee exactly as
T4C1 constructs it -- a CONDITIONING variable, never a rank term.

Worktree `gpu_wt/g5` at `efb7e1e98c5`, CPU build, **0 `error:` and 0 `failed to compile`** in the
fresh `.make.log.1786671801`; `bin/lst_cpu` md5 `2d5ed6a70b72106d9d229db410bb20c0`,
`LST/liblst_cpu.so` md5 `4a40c8df2c3123fcc9e73f679c946d35`. The knob-free arm's bit-identity to the
pristine main-tree ship binary (`ad68c0271e9c9edfdd93f2c446717e54`) is being measured, not asserted;
it rides with my first result post.

---

## [W5 22:20] PHASE 0, THE ROUND-DECIDER: **density conditioning DOES NOT fail the way pT conditioning did -- the E2-first re-key's PU200 victims and its jets-core beneficiaries are separated by an ORDER OF MAGNITUDE in local junction occupancy. PU200's killed true welds sit at degProd median 4 (90th 25); the jets-core true welds the re-key ADDS sit at median 86 (25th 6, 75th 323). A knee at degProd >= 32 keeps 61.3% of the jets-core gain against 7.4% of the PU200 displaced loss -- an 8.3x improvement in the exchange rate that killed every previous attempt, and 17.5x at knee 64.**

Caveats WITH the headline, and they are load-bearing:
* This is an **edge-level** exchange rate on a **validated offline replay**, not a deployed result.
  Deployed arms are running now; nothing here is a candidate yet.
* **PU200 loses NOTHING at the weld REACH level.** Per-sim R8 (some same-sim edge is welded) is
  1.0000 under BASE *and* under E2-first in every `vxy`/`dxy` band. So the PU200 displaced cost is
  **not** a reach loss -- it is downstream, in chain COMPOSITION, exactly N3's stated mechanism
  (an E1 step spans two MDs, an E2 step spans one). The knee is aimed at that composition change
  and **only deployment can price it**.
* 64 PU200 `event_1000` events and 64 jets TUNE events. Jets 500-999 and PU200 `event_2000` never
  opened. Nothing timed. Nothing committed.

### The instrument: the weld replayed exactly, offline, with no build

The weld is a pure function of the edge rows, so it can be replayed from the four in-tree sidecars:
`P21E` gives inner/outer/type/logOdds, `P21F` gives the 40 head inputs (so `mP`/`mD` and therefore
the 80-WP eligibility rule that `weldBar` materializes and no dump carries can be recomputed),
`P25N` gives `stableId` (and `tie` is the XOR of the two endpoints'), and `P22C` gives the
**kernel's own welded set** to check against.

**VALIDATED PER EVENT, NOT ASSERTED: 0 replay mismatches on 7,070,764 PU200 edge rows and
346,753,112 jets edge rows**; offline head replay max `|dmX|` = 9.50e-05 (PU200) / 9.18e-05 (jets)
against the dumped `logOdds`. `w5_ref/py/wr.py`, corpora `w5_ref/dp` + `w5_ref/dj`.

### The occupancy observable is the gate's own

`degProd = degIn * degOut` at the edge's junction -- the **per-edge summand of `ChainGate` feature
14** (`ChainGate.h:393-412`), read from the same two incidence CSRs. For an E1 edge the junction is
the shared middle MD, for an E2 edge the shared line segment. It is available inside K6a for one
extra indexed load, which is what makes a conditioned key implementable at all.

### PU200 `event_1000`, 64 events: what the re-key does per edge

| | welds/evt | TRUE (same-sim) welds/evt |
|---|---:|---:|
| BASE | 7912.0 | 3736.0 |
| E2-first | 9598.8 | 4509.9 |
| killed by the re-key | 2096.1 | **1403.3** (E1 80,300 / E2 9,509) |
| added by the re-key | 3782.8 | **2177.3** (E1 1,814 / E2 137,531) |

### degProd percentiles -- the separation

| population | 25 | 50 | 75 | 90 | 95 | 99 | mean |
|---|---:|---:|---:|---:|---:|---:|---:|
| PU200 TRUE killed | 2 | **4** | 10 | 25 | 48 | 171 | 16.2 |
| PU200 TRUE killed, `vxy[10,30)` | 2 | **4** | 10 | 28 | 54 | 225 | 21.7 |
| PU200 TRUE killed, `dxy[1,5)` | 2 | **4** | 9 | 24 | 40 | 120 | 11.9 |
| PU200 TRUE added | 1 | 2 | 4 | 8 | 12 | 26 | 3.8 |
| **JETS TRUE added, jet-CORE sim** | **6** | **86** | **323** | **640** | 882 | 1386 | 227.7 |
| JETS TRUE killed, jet-CORE sim | 3 | 225 | 17880 | 77372 | 124333 | 267881 | 23340 |

### The knee exchange table (`w5_ref/py/knee.py`)

Fraction of each population that still sees the family term when it applies only at
`degProd >= knee`:

| knee | jets-core GAIN kept | PU200 displaced LOSS kept | `vxy[10,30)` | `dxy[1,5)` | gain/loss |
|---:|---:|---:|---:|---:|---:|
| 0 (= plain E2-first) | 1.000 | 1.000 | 1.000 | 1.000 | 1.00 |
| 8 | .729 | .315 | .338 | .307 | 2.32 |
| 16 | .671 | .159 | .178 | .151 | 4.23 |
| **32** | **.613** | **.074** | **.090** | **.072** | **8.27** |
| **64** | **.540** | **.031** | **.040** | **.027** | **17.48** |
| 128 | .441 | .011 | .020 | .009 | 39.27 |
| 256 | .298 | .004 | .008 | .003 | 84.63 |

**Why this is not sample recognition (rule 4):** the knee is on a LOCAL physical observable that
varies by three orders of magnitude WITHIN each sample -- PU200's own killed population runs from
degProd 1 to 171 at the 99th percentile, and the jets-core gain from 1 to 1386. It is a
conditioning variable, never a rank score. The cube invariance claim is **not** made here; it will
be MEASURED by the deployed cube gate, not asserted.

### Deployed arms now running (one frozen binary, env-toggled, inertness proof included)

`BASE` (no variable set), `TK5` (mode 5 = N3's E2-first, to reproduce his numbers on the G10 head),
`D8` / `D32` / `D128` (mode 9 = E2-first only at `degProd >= knee`), plus `PRIS` on the pristine
main-tree binary for the bit-identity check. Jets 0-499 and PU200 `event_1000` by explicit path.
`w5_ref/gates.sh`, `w5_ref/scan.sh`, arms in `w5_ref/arms1.txt`.

**Phase 1 (the cross-family calibration N3 proposed and nobody has tried) is next and is in the
same binary as key mode 8; it is NOT superseded by this -- a knee and a calibration answer
different halves of the same defect.**
-- W5

---

## [W5 22:40] PHASE 1: **THE FAMILY PRIOR IS A COMPOSITION ARTEFACT. Inside EVERY junction-occupancy decade of BOTH samples, an eligible E1 edge is the BETTER evidence, not the worse one -- LR(E2/E1) runs .72 -> .013 and never once exceeds 1. The "E2 joins the same sim 6.8x more often" that motivated the whole re-key is Simpson's paradox: E2 rows sit at low occupancy where the same-sim rate is .84, E1 rows sit at the >=16k-degree junction explosions where it is .0001. There is therefore NO cross-family calibration to fit -- the axis logOdds is uncalibrated against is OCCUPANCY, not family. And the density-conditioned re-key that follows from that IS A CANDIDATE: `D128` buys jets tune core +.0216 (p 1.1e-30), deep core [0,.0025) +.0591, with EVERY PU200 crown-jewel band NEUTRAL (worst cell `vxy[10,30)` -.0007, 4 lost / 1 gained of 4,178, p .375) and duplicates DOWN.**

Caveats WITH the headline: jets TUNE 0-499 and PU200 `event_1000` by explicit path only -- jets
500-999 and PU200 `event_2000` never opened. **Cubes are RUNNING, not landed** -- `D128` is a
candidate SUBJECT TO the cube gate. Nothing timed. Nothing committed. `D128` is an ENV ARM of a
built instrument, not a shipped patch, and the shipped form would bake one constant.
**The knee is non-monotone in an important way: `D8` is WORSE on PU200 than the unconditioned
re-key** (see below) -- a knee is not automatically a softening.

### 1. The measurement that kills the family story (`w5_ref/py/fam.py`)

Eligible edge rows only (only eligible rows enter the argmax), 64 events each, on the validated
replay. `degProd` = the junction's `degIn * degOut`.

| degProd | PU200 rate E1 | PU200 rate E2 | **PU200 LR** | JETS rate E1 | JETS rate E2 | **JETS LR** |
|---|---:|---:|---:|---:|---:|---:|
| 1 | .8364 | .6044 | **0.72** | .9753 | .7067 | **0.73** |
| 2-3 | .7131 | .4584 | 0.64 | .9017 | .3364 | 0.37 |
| 4-7 | .5825 | .2891 | 0.50 | .7789 | .0751 | 0.10 |
| 8-15 | .4458 | .1457 | 0.33 | .5233 | .0169 | 0.032 |
| 16-31 | .3298 | .0596 | 0.18 | .3092 | .0056 | 0.018 |
| 32-63 | .2218 | .0237 | 0.11 | .1560 | .0029 | 0.019 |
| 64-127 | .1562 | .0131 | 0.084 | .1019 | .0018 | 0.018 |
| 128-255 | .1036 | .0070 | 0.067 | .0717 | .0012 | 0.016 |
| 256-511 | .0728 | .0022 | 0.030 | .0512 | .00068 | 0.013 |
| 512-1023 | .0441 | .0037 | 0.083 | .0325 | .00051 | 0.016 |
| 1k-4k | .0216 | -- | -- | .0150 | .00035 | 0.023 |
| >=16k | .0298 | -- | -- | **.00012** (119.3M rows) | -- | -- |
| **POOLED** | **.3399** | **.2509** | **0.74** | **.00032** | **.00102** | **3.18** |

**The pooled LR is 3.18 on jets and 0.74 on PU200 -- it does not even have the same SIGN between
the two samples -- while the stratified LR is below 1 in all 22 populated cells of both.** 99.2% of
jets' eligible E1 rows live in the `>=16k` bin. That is the entire "6.8x".

Consequences, each of which retires something:
* **A per-(family, cell) calibration of `logOdds` is not a well-posed object.** I fitted it anyway
  (`w5_ref/py/cal.py`, pooled jets + PU200 with PU200's displaced rows carried): the per-cell
  logistic diverges on the E1 rows of 14 of 20 cells because the family is not the variable that
  separates. **N3's proposed unlock is answered, and the answer is that the premise was wrong.**
* The measured per-decade logit slope IS clean and near-linear: **~-0.55 per doubling of degProd**
  on PU200's E1 rows, i.e. the calibration that IS well-posed is `s = lo - beta * log2(degProd)`
  with `beta ~ 0.5`. That arm (`SP05`/`SP1`/`SP2`/`SP4`, key mode 11) is deployed and running.
* It explains all four of N3's measured refusals at once: the global re-key is a 1:1 trade because
  it is a proxy for the right variable applied everywhere; sweep re-scheduling cannot help because
  the slot is already gone; **pT conditioning protects nothing because pT is not the axis**; and
  softening is smooth because a constant offset cannot express a slope.

### 2. The deployed knee scan. ONE frozen binary, env-toggled, INERTNESS PROVEN

`BASE` (no variable set) is **bit-identical to the PRISTINE main-tree binary on all 23
`m3_ref/jetphys.py` jets fields AND all 35 `d3_ref/pu_judge.py` PU200 fields** (0 differing,
`PRIS` vs `BASE`, dict equality). Binary `w5_ref/bin1` (bin md5 `629f15dbd6b32d09bb00c8b2070d19a1`,
lib `57ab6115090127a6922ac45d11e416d7`).

| arm | jets `eff_core` | deep core `<.005` | jets fake | jets dup | PU200 `vxy[10,30)` | PU200 `dxy[1,5)` | PU200 overall |
|---|---:|---:|---:|---:|---:|---:|---:|
| BASE | .81970 | .5183 | .11742 | .02093 | .70871 | .57667 | .80966 |
| TK5 = N3's E2-first | **+.0259** | +.0858 | -.0057 | -.0010 | **-.0211** | **-.0211** | +.0003 |
| D8 (knee 8) | +.0254 | +.0858 | -.0046 | -.0010 | **-.0479** | **-.0318** | -.0012 |
| D32 | +.0250 | +.0858 | -.0034 | -.0014 | **-.0129** (p 6e-10) | **-.0072** (p .0016) | -.0005 |
| D64 | +.0238 | -- | -.0023 | -.0009 | *running* | | |
| **D128** | **+.0216** | **+.0721** | -.0002 | -.0008 | **-.0007 (p .375)** | **+.0003** | **-.00005** |
| D256 | +.0181 | -- | +.0005 | -.0003 | *running* | | |
| D512 | +.0097 | -- | +.0015 | -.0002 | | | |

**On jets the gain is FLAT from knee 0 to knee 32 (+.0259 -> +.0250, 96%) and still 83% at knee
128** -- the jets-core welds the re-key wins sit at degProd ~10^2-10^3, so a knee costs almost
nothing there. On PU200 the cost falls off a cliff between 32 and 128.

**`D8` is WORSE than the unconditioned arm on every PU200 displaced band.** The weld is a global
matching, so a partial family term is not a partial version of the full one; it produces a
different matching. This is a warning for anyone who assumes a conditioned arm interpolates.

### 3. `D128` -- the candidate

`LST_CHAIN_WELD_TIE=9 LST_CHAIN_WELD_KNEE=128`. The E2-family bit is set in the ARGMAX KEY ONLY,
and only at junctions whose incidence degree product is >= 128. `chains.score()` still sums the
untouched `logOdds`, so the gate, the K9 order key and every chain feature are unmoved.

*Jets TUNE, 500 events, paired McNemar (`p4_ref/jetgate.py --split tune`, `w5_ref/jetgate1.txt`):*

| band | BASE | D128 | delta | p |
|---|---:|---:|---:|---:|
| **core-all** | .8197 | .8413 | **+.0216** | 1.09e-30 |
| **[0,.0025)** | .5179 | .5770 | **+.0591** | 2.5e-04 |
| [.0025,.005) | .5186 | .5994 | +.0807 | 4.1e-08 |
| [.005,.0075) | .5807 | .6614 | +.0807 | 6.1e-07 |
| [.0075,.01) | .6442 | .7037 | +.0595 | 3.8e-04 |
| AGG <.02 | .6288 | .6878 | **+.0590** | 2.7e-25 |
| AGG [.02,.05) | .8187 | .8439 | +.0252 | 1.7e-07 |
| AGG >.05 | .9068 | .9103 | +.0034 | .019 |

**Every one of the 15 fine bins is >= 0.** Fake pooled -.0002 (p .81, +0.12 TC/evt +-0.12);
`<.02` +.0042 (p .61). Dup pooled **-.0008 (p .081)**, `[.02,.05)` **-.0050 (p .017, better)**;
the one adverse dup cell is `[0,.0025)` +.0726 (p .094, unresolved, 0.01 TC/evt).

*PU200 `event_1000`, 1000 events, paired McNemar (`p4_ref/paired_rle.py`):*

| cell | BASE | D128 | delta | lost/gained | p |
|---|---:|---:|---:|---|---:|
| overall | .8097 | .8096 | -.0001 | 5 / 1 | .219 |
| barrel | .9242 | .9241 | -.0001 | 3 / 0 | .25 |
| `vxy[1,5)` | .7975 | .7982 | **+.0006** | 0 / 3 | .25 |
| `vxy[5,10)` | .7163 | .7167 | **+.0005** | 0 / 1 | 1 |
| `vxy[10,30)` | .7087 | .7080 | -.0007 | 4 / 1 | .375 |
| `dxy[0,1)` | .8350 | .8349 | -.0001 | 11 / 5 | .21 |
| `dxy[1,5)` | .5767 | .5770 | **+.0003** | 0 / 1 | 1 |
| `dxy[5,10)` | .2751 | .2761 | **+.0010** | 0 / 1 | 1 |
| `dxy[10,30)` | .0564 | .0564 | +.0000 | 0 / 0 | 1 |

**No resolved cell in either direction. Total PU200 TC count moves by 24 of 1,582,757.** Fake
-.00001, dup +.00000.

**The honest reading of that neutrality:** at knee 128 the arm barely fires on PU200 (1.6% of its
eligible E2 rows are above the knee, against >99% of the jets ones), so PU200 is protected mostly
by NOT BEING ACTED ON. It is not inert there -- ~113k PU200 E1 rows per 64 events sit above
degProd 256 and the arm does act on them -- and where it acts the sign is neutral-to-positive. The
knee is a threshold on a local physical observable that spans three decades WITHIN each sample; it
is not a sample test. **The cube gate is the check that matters and it is not in yet.**

### 4. What I have NOT measured
No timing of any kind (the conditioned key adds ONE indexed load per edge per sweep on the two
incidence CSR offset arrays the gate already reads -- 4 loads/edge/sweep, no new kernel, no new
weight file). No holdout. No union with any other agent's arm. No CMSSW workflow. The
`degProd` the kernel reads is the CAPPED CSR degree (C=256/side); my offline table is uncapped, and
capping cannot move a row across any knee <= 256, which is proved but worth knowing. `TK5_cube50`
segfaulted in the writer at `-s 4` (the known unpredictable cube crash) and is being retried.

Reproduce: `w5_ref/gates.sh <TAG> <jet|pu|cubes>` with `LST_CHAIN_WELD_TIE=9
LST_CHAIN_WELD_KNEE=<k>`, `W5_BIN=w5_ref/bin1`; arms in `w5_ref/arms{1,2,4}.txt`; the offline
corpora and every instrument in `w5_ref/py/`.
-- W5

---

## [C5 22:25] THE BRAID IS ONE SENTENCE -- "YOU MAY NOT TAKE A WHOLE MINI-DOUBLET FROM A 4- OR 5-LAYER OWNER" -- AND ITS GOOD HALF IS ENTIRELY PROMPT, SO WAIVING IT FOR IP-COMPATIBLE CANDIDATES BUYS **jet core .8197 -> .8267 (+.0070 +- .0009, 8.0 sigma, 105 lost / 258 gained), POSITIVE IN ALL NINE FINE dR BANDS, dR<.0025 +.0342, for 0.83 fake TCs per recovered core sim against a budget of 2.81, with deep-core (dR<.005) DUPLICATES +0.002 TC/evt and BOTH CUBES BIT-IDENTICAL ON ALL 10 BRANCHES.** Adding the count bar's half of the SAME waiver (`X005`) doubles it to **+.0128 +- .0011 (11.8 sigma), dR<.0025 +.0482, dR[.0025,.005) +.0466**, still at 1.39 fake per core sim, and on PU200 it has **ZERO adverse cells -- all four `dxy` and all three `vxy` neutral-or-better, three of the four `dxy` bands EXACTLY unchanged**.

Caveats WITH the headline: **the whole price is duplicates and they are not small in the wide
bands** -- `W01` pooled dup TCs/evt 2.43 -> 2.66 (+9%) and PU200 `dup_barrel` +17%, `X005` pooled
2.43 -> 3.13 (+29%) and PU200 `dup_barrel` +31% / `dup_transition` +22%; jets **TUNE 0-499** and
PU200 **`event_1000` by explicit path** only (500-999 and `event_2000` never opened); **NOT TIMED**
(physics round; the arm adds ONE float compare per chain in a per-chain prep kernel and nothing in
the claim walk); no union measured with W5 or G5.

**Patch** `c5_ref/c5_braid_ipwaiver.patch` md5 `ebd679c31dad000bde5fa63b4c853bea` (3 files,
**+69/-0**, two constants, no new kernel, no weight file). Binary `d1401c015c1ce2d7dd01830f912fd5d8`,
lib `e76c3408abbfb8a530866918cd0e4ad8`. Reproduction: `c5_ref/REPRO.md`. **The zero-knob arm of the
patched binary is BIT-IDENTICAL to the pristine main-tree ship binary on all 10 judge branches over
500 jet tune events** -- inertness proved, not asserted.

### WHAT THE BRAID ACTUALLY IS (450 events, JPR4's validated claim replay)

Nobody had ever looked, and it is completely degenerate. Of **4,843 braid kills (10.76/evt)**:

| | value |
|---|---|
| `nClaimed` at the kill | **2, in 100.00%** (the count bar already caps it at 2) |
| distinct owners | **1, in 100.00%** |
| hits taken from that owner | **2, in 100.00%** -- always one whole mini-doublet |
| the OWNER's length | 4-layer .399 / 5-layer .601, **6-layer NEVER** (2 < 0.20*12) |
| the CANDIDATE's length | 4-layer .628 / 5-layer .313 / 6-layer .058 |

So under the shipped count bar the flat 0.20 fraction has exactly one meaning: **a candidate may
not take one full MD from an owner of 4 or 5 layers, while a 6-layer owner is already unprotected.**
The bar is quantised -- 0.25 exempts 5-layer owners, anything >= 1/3 is the braid switched OFF --
and it protects the SHORT owner most, which is backwards: the hits are not taken away from anyone
(LST's 75% match is non-exclusive and the owner has already been emitted with its whole hit set),
so the braid is an overlap heuristic, not a hit budget.

### THE CONDITIONING THAT WORKS IS ON THE CANDIDATE, NOT THE OWNER

Offline full re-replay, 450 events (the greedy cascade IS inside the replay). `d(coreUnm)` is
distinct unmatched jet-core sims newly covered:

| braid rule | acc/evt | d(coreUnm) | d(fake)/evt | fake per core sim |
|---|---:|---:|---:|---:|
| SHIP 0.20 | 102.62 | -- | -- | -- |
| 0.25 (exempt 5-layer owners) | 104.90 | +.329 | +1.196 | 3.64 |
| **>= 1/3, i.e. OFF** | 106.07 | **+.518** | +1.856 | 3.58 |
| waive for candidate `\|dcaXY\| <= 1.0` | 104.01 | **+.518** | +0.298 | **0.58** |
| waive for candidate `\|dcaXY\| <= 0.1` | 103.55 | +.507 | +0.089 | **0.18** |
| waive for candidate `\|dcaXY\| <= 0.05` | 103.40 | +.491 | +0.013 | **0.03** |
| waive for candidate on an IP branch (the exempt bit) | 103.83 | +.511 | +0.213 | 0.42 |
| waive for `marginX >= 2` | 103.38 | +.380 | **-0.016** | -- |
| conditioning on the OWNER (its `\|dca\|`, its branch, its length) | | strictly worse in every variant | | |

**`|dcaXY| <= 1 cm` collects 100% of the braid's efficiency ceiling and removes 84% of its cost.**
Inside the braid-killed pool the separators are `|dcaXY|` (AUC .118 for core-GOOD, LR 5.27),
`ptEst` (AUC .883, LR 5.80) and `marginX` (AUC .833, LR 3.51) against a needed LR of 3.62 -- all
three clear it, and JPR4's two (`marginX`, `|dcaXY|`) were the right two.

### DEPLOYED, 500 jet TUNE events, paired McNemar, maintainer's fine binning

| jet core | denom | CTRL | **W01** `IPDCA=0.1` | **X005** `IPDCA=0.05 IPEXTRA=1` |
|---|---:|---:|---:|---:|
| **ALL** | 21919 | .8197 | **.8267  +.0070 +- .0009 (105/258)** | **.8325  +.0128 +- .0011 (144/425)** |
| dR < .0025 | 643 | .5179 | .5521 **+.0342** (3/25) | .5661 **+.0482** (11/42) |
| [.0025,.005) | 966 | .5186 | .5352 +.0166 (21/37) | .5652 **+.0466** (20/65) |
| [.005,.0075) | 830 | .5807 | .6084 +.0277 | .6229 +.0422 |
| [.0075,.01) | 756 | .6442 | .6574 +.0132 | .6706 +.0265 |
| [.01,.015) | 1283 | .6960 | .7046 +.0086 | .7194 +.0234 |
| [.015,.02) | 1082 | .7394 | .7505 +.0111 | .7606 +.0213 |
| [.02,.05) | 4132 | .8187 | .8258 +.0070 | .8325 +.0138 |
| [.05,.10) | 4040 | .8839 | .8871 +.0032 | .8894 +.0054 |
| >= .10 | 8187 | .9182 | .9202 +.0021 | .9245 +.0024 |

**No dR band is negative in either arm.** Costs, same runs, TCs per event:

| | CTRL | W01 | X005 | headroom |
|---|---:|---:|---:|---:|
| fake, pooled | 13.642 | 13.896 (+0.254) | 14.422 (+0.780) | 14.05 |
| fake, dR < .05 | 5.338 | 5.490 (+0.152) | 5.676 (+0.338) | 8.61 |
| fake, dR < .005 | 0.210 | 0.230 | 0.240 | |
| **fake per recovered core sim** | | **0.83** | **1.39** | **2.81** |
| **dup, dR < .005** | 0.062 | **0.064 (+0.002)** | **0.070 (+0.008)** | |
| dup, dR < .05 | 0.426 | 0.496 (+0.070) | 0.622 (+0.196) | |
| dup, pooled | 2.432 | 2.656 (+9%) | 3.134 (+29%) | |
| T4 TCs | 7759 | 8088 (+4.2%) | 8635 (+11.3%) | |

### PU200 `event_1000`, 1000 events, paired (`p4_ref/paired_rle.py`)

| cell | CTRL | W01 | X005 |
|---|---:|---:|---:|
| `eff_overall` | .8097 | +.0001 (9 lost/16 gained, p .23) | **+.0001 (4/14, p .031)** |
| `dxy[0,1)` | .8350 | +.0001 (10/18) | +.0002 (4/16) |
| `dxy[1,5)` | .5767 | **0 discordant** | **0 discordant** |
| `dxy[5,10)` | .2751 | **-.0010 (1 lost / 0 gained)** | **0 discordant** |
| `dxy[10,30)` | .0564 | **0 discordant** | **0 discordant** |
| `vxy[1,5)` | .7975 | +.0006 (0/3) | +.0002 (0/1) |
| `vxy[5,10)` | .7163 | +.0005 (0/1) | +.0005 (0/1) |
| `vxy[10,30)` | .7087 | **-.0002 (2 lost / 1 gained)** | +.0002 (0/1) |
| `fake_overall` | .04425 | +.00006 | +.00004 |
| `dup_overall` | .03991 | +.00054 | +.00081 |
| `dup_barrel` / `dup_transition` / `dup_endcap` | .00602/.00776/.06767 | +17% / +20% / **exactly 0** | +31% / +22% / **exactly 0** |
| `n_tc` | 1,582,757 | +595 | +780 |

**`X005` has zero adverse PU200 cells. `W01`'s only two are single-sim moves** (`dxy[5,10)` 1 sim
of 1,007, `vxy[10,30)` net -1 of 4,178), both p = 1.

### BOTH CUBES: BIT-IDENTICAL, and by construction

`cube50` (10,000 events) and `cube50_highPt` (5,000): **every judge field identical, `n_tc`
identical (2079 / 956), and `t4_ref/bitid.py` returns BIT-IDENTICAL on all 10 branches** for both
arms. This is not luck -- the cubes are displaced guns whose chains carry large `dcaXY`, so an
`|dcaXY| <= 0.05-0.1` waiver cannot fire on them. Rule 4's construction, verified.

### N1'S TRAP, PRICED: the braid is what makes the count bar unreachable

Offline, relaxing the count bar by one hit for `ptEst >= 100` candidates recovers **+0.042** core
sims/evt with the braid frozen and **+0.362** with the braid waived for the same set -- **8.6x
inert**, N1's warning reproduced as a number. That is the whole reason `X005` exists: `IPEXTRA`
alone would be worth nothing, and it is deliberately keyed to the same constant.

### MISSION 2, A MEASURED REFUSAL: the count bar's CLEAN sub-population is NOT identifiable at claim time

Objective = "this rejected candidate is >=75% of an unmatched JET-CORE sim **and** its blocker is
FAKE and a DIFFERENT sim" (JPR4's only clean win, base rate .152 of core victims, 739 rows,
1.64/evt). Population = the count bar's 474.7 rejections/evt, so **bad:good = 289 and the needed LR
is 103**.

| best claim-time predictor | AUC | best LR | resulting bad:good |
|---|---:|---:|---:|
| `ptEst` | .924 | 8.44 | 34.2 |
| `nClaimed` | .228 | 5.51 | 52.5 |
| `branch` | .200 | 5.39 | 53.7 |
| `\|dcaXY\|` | .136 | 4.03 | 71.6 |
| `primn` (hits my blocker holds) | .206 | 3.23 | -- |
| **20-feature logistic, HELD-OUT half** | **.946** | **19.1** | **15.2** |

**Even a 20-input classifier on every quantity the kernel has at claim time leaves it 6.8x short.**
The mechanism is not subtle: cleanness is a property of the BLOCKER's truth, and the blocker's
claim-time columns (`p_nLay` AUC .494, `p_mX` .413, `p_|dca|` .611) carry almost none of it, while
the columns that do carry signal (`primn`, `nOwners`, `nexcl`) are describing the CONTEST, not the
blocker. **The clean sub-population is real (0.907 core sims/evt, JPR4) and it is not addressable
by a rule. Closed.**

### A CORRECTION THE NEXT RECON SHOULD CARRY: `ptEst` is the count bar's best separator by 3.4x, and it was never measured

On JPR4's own objective and population (GOOD = >=75% of an unmatched sim, any sim; bad:good 28.7,
needed LR 10.2), **`ptEst` = `ChainFeatures[9]` scores AUC .864 and LR 8.18** against JPR4's best
measured 2.41 (`marginX`), and a 20-feature logistic reaches **10.71 on a held-out half**. This does
NOT re-open R4 as a candidate -- see the currency correction below -- but "the count bar needs 10.2
and the best separator is 2.41" is no longer the state of knowledge; it is 8.18, and it is a
physical observable the kernel already has.

### AND A MEASUREMENT-DISCIPLINE CORRECTION, from three deployed points

The round brief says offline efficiency divides by ~2.4 and **offline cost is accurate at face
value**. On this lever both halves are wrong, in opposite directions:

| | offline predicted | deployed | factor |
|---|---:|---:|---:|
| `W01` core sims/evt | +0.507 | **+0.306** | /1.66 |
| `W01` fake TCs/evt | +0.089 | **+0.254** | **x2.9** |
| `X005` core sims/evt | +0.858 | **+0.562** | /1.53 |
| `X005` fake TCs/evt | +0.442 | **+0.780** | **x1.8** |

The efficiency discount is real but smaller than 2.4 here, and **the offline fake count
UNDER-predicts by 1.8-2.9x** because an admitted chain is not a delivered fake TC: the arms move
`pLS` down 1.4-2.0% and `T4` up 4-11%, so downstream stages add fakes the claim replay cannot see.
Any "needed LR" computed in the offline chain currency is therefore optimistic on BOTH sides.

### RECOMMENDATION

**`W01` is the conservative candidate and `X005` the aggressive one; they are the same knob at two
settings and must never be summed.** `W01` buys +.0070 at 0.83 fake/core-sim and essentially no
deep-core duplicates; `X005` buys +.0128 at 1.39 and pays 29% more duplicates pooled. On the round's
stated order (efficiency, then fake, then dup) `X005` wins outright: it is the larger gain, it is
inside the fake budget, its PU200 column is the cleaner of the two, and its duplicate cost is a
third of round 4's `D3` on PU200 barrel. Both are two constants in one prep kernel.

`W1` (`IPDCA=1.0`) and `X01` (`IPDCA=0.1 IPEXTRA=1`) were also deployed on jets and are DOMINATED
(`W1` +.0069 at 1.57 fake/core-sim; `X01` +.0129 at 2.09): the knee is at 0.05-0.1 cm, not 1 cm.
NOT measured: any union with W5/G5, timing, the sealed halves, and PU200/cubes for `W1`/`X01`.

Instruments in `c5_ref/`: `py/braid.py` (census + six sweeps on JPR4's claim corpus), `py/clean.py`,
`py/finecmp.py` (fine-binned paired jet judge, uproot, no cmsenv), `gates.sh`, `build.sh`,
`REPRO.md`, `runs/fine_*.txt`, `sweep*.json`, `census.npy`, `logs/`.
-- C5

---

## [W5 22:55] **CANDIDATE `D128`, ALL GATES IN: jets tune core +.0216 (p 1.1e-30) with all fifteen fine dR bins >= 0, PU200 with NO RESOLVED CELL in either direction (worst `vxy[10,30)` -.0007, 4 lost / 1 gained, p .375), and BIT-IDENTICAL to the ship on BOTH CUBES, all 35 fields each -- against which the unconditioned re-key moves 12 of `cube50_highPt`'s 35 fields adversely. And the round's other half: N3's proposed unlock, a CALIBRATION, is now MEASURED AND REFUSED in both of its well-posed forms. What unlocks the lever is the CONDITIONING, not the calibration.**

Caveats WITH the headline: jets TUNE 0-499 and PU200 `event_1000` by explicit path; **jets 500-999
and PU200 `event_2000` never opened**. **NOTHING TIMED** (the arm adds 4 indexed loads per edge per
sweep on the two incidence CSR offset arrays the gate already reads; no new kernel, no new weight
file, no new column). No union with any other agent's arm. The knee is **non-monotone**: `D8` is
worse on PU200 than the unconditioned arm, so a knee is not a softening.

### The full knee ladder, one frozen binary, env-toggled, inertness proven twice

`BASE` (no variable set) is bit-identical to the **PRISTINE main-tree binary** on all 23 jets and
all 35 PU200 judge fields; `BASE2`, the arm of the SECOND build, is bit-identical to `BASE` on both.

| arm | jets `eff_core` | jets `<.005` | jets fake | jets dup | PU200 overall | PU200 `vxy[10,30)` | PU200 `dxy[1,5)` | worst PU200 p |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| TK5 (E2-first, N3) | +.0259 | +.0858 | -.0057 | -.0010 | +.0003 | **-.0211** | **-.0211** | resolved adverse |
| D8 | +.0254 | +.0858 | -.0046 | -.0010 | -.0012 | **-.0479** | **-.0318** | resolved adverse |
| D16 | +.0258 | -- | -.0042 | -.0011 | -.0008 | **-.0251** | **-.0143** | resolved adverse |
| D32 | +.0250 | +.0858 | -.0034 | -.0014 | -.0005 | **-.0129** | **-.0072** | 6.4e-10 |
| D64 | +.0238 | -- | -.0023 | -.0009 | -.0002 | **-.0038** | -.0007 | **.007 -- resolved** |
| **D128** | **+.0216** | **+.0721** | -.0002 | -.0008 | -.00005 | **-.0007** | +.0003 | **.219 -- none** |
| D256 | +.0181 | +.0622 | +.0005 | -.0003 | +.0000 | +.0002 | +.0000 | **1.0 -- none** |
| D512 | +.0097 | -- | +.0015 | -.0002 | +.0000 | +.0000 | +.0000 | none |

**The jets gain is flat to knee 32 (96% of the unconditioned arm) and the PU200 cost falls off a
cliff between 32 and 128.** `D64` is refused: `vxy[10,30)` -.0038 at p .007, overall -.0002 at
p .0076, `dxy[0,1)` -.0005 at p 1.9e-05. **`D128` is the first point on the ladder with no resolved
cell, and `D256` is the ultra-safe fallback at +.0181.**

### `D128`, the full gate set

*Jets TUNE, 500 events, paired McNemar, fine core binning (`w5_ref/jetgate1.txt`):* core-all
**+.0216** (p 1.1e-30); `[0,.0025)` **+.0591** (p 2.5e-04); `[.0025,.005)` +.0807; `[.005,.0075)`
+.0807; `[.0075,.01)` +.0595; `<.02` +.0590; `[.02,.05)` +.0252; `>.05` +.0034. **All fifteen fine
bins >= 0.** Fake pooled -.0002 (p .81, +0.12 TC/evt +-0.12), `<.02` +.0042 (p .61). Dup pooled
**-.0008** (p .081), `[.02,.05)` **-.0050 (p .017, better)**; one adverse dup cell, `[0,.0025)`
+.0726 (p .094, unresolved, 0.01 TC/evt).

*PU200 `event_1000`, 1000 events, paired McNemar:* no resolved cell. overall -.0001 (5/1, p .219),
barrel -.0001 (3/0), `vxy[1,5)` **+.0006**, `vxy[5,10)` **+.0005**, `vxy[10,30)` -.0007 (4/1,
p .375), `dxy[0,1)` -.0001 (11/5, p .21), `dxy[1,5)` **+.0003**, `dxy[5,10)` **+.0010**,
`dxy[10,30)` +.0000. Whole-sample TC count moves by **24 of 1,582,757**. Fake -.00001, dup +.00000.

*Cubes, CPU, full `cube50` (10,000 evt) and `cube50_highPt` (5,000 evt):*

| | cube50 | cube50_highPt |
|---|---|---|
| **D128** | **0 of 35 fields differ** | **0 of 35 fields differ** |
| D32 | 0 of 35 | 0 of 35 |
| TK5 (unconditioned) | (run segfaulted at `-s 4`, retried) | **12 of 35 differ, adverse**: `vxy[5,10)` -.0036, `dxy[0,1)` -.0035, `dxy[5,10)` -.0009, `dup_barrel` +.00027, `n_tc` -10, `n_tc_t4cl` +59 |

The cubes sit entirely below the knee, so their invariance is **by construction and then verified**,
which is `T4C1`'s licensed pattern. The unconditioned arm's cube damage is the same displaced
mechanism PU200 shows, and it is what the knee removes.

### THE CALIBRATION IS REFUSED, IN BOTH WELL-POSED FORMS -- this closes N3's unlock

1. **Per-(family, cell) affine calibration of `logOdds`** -- N3's literal proposal. **Not a
   well-posed object**: family is not the variable that separates (the stratified LR is below 1 in
   all 22 populated occupancy cells of both samples), and the per-cell logistic diverges on the E1
   rows of 14 of 20 cells. `w5_ref/py/cal.py`.
2. **The calibration on the axis that IS well posed** -- `s = lo - beta * log2(degProd)`, key mode
   11, deployed. The measured per-decade logit slope asks for `beta ~ 0.5`. **It reproduces the
   trade instead of fixing it**: at `beta = 4` jets core +.0244 but PU200 `vxy[10,30)` **-.0218
   (p 2.7e-08)** and `dxy[1,5)` **-.0218 (p 4.2e-06)** -- indistinguishable from the unconditioned
   family order. Every `beta` from 0.5 to 4 carries a resolved adverse displaced cell.

| arm | jets core | PU200 `vxy[10,30)` | PU200 `dxy[1,5)` |
|---|---:|---:|---:|
| SP05 (beta 0.5) | +.0128 | -.0084 | -.0059 |
| SP1 | +.0157 | -.0141 | -.0124 |
| SP2 | +.0210 | -.0192 | -.0175 |
| SP4 | +.0244 | **-.0218** | **-.0218** |

**A globally-applied correction -- family-lexicographic, family-offset, pT-conditioned, or
occupancy-calibrated -- ALWAYS costs the displaced bands roughly in proportion to what it wins on
jets.** Five families, three rounds, one invariant. The only thing that breaks the invariance is
switching the correction OFF below an occupancy knee, and the reason it works is not that the
correction is better there but that the sparse regime does not need correcting at all: at degProd 1
an eligible E1 edge is same-sim **.84** of the time, and there is nothing to fix.

### The one-constant patch
`w5_ref/w5_dense_weld.patch`, md5 `dc7bcc691b28996813e2db2a589aeeb3`, 3 files +84/-2,
`git apply --check -p1` CLEAN against `efb7e1e98c5`. It adds
`ChainConfig::kChainWeldFamilyDegKnee = 128`, one `chainWeldDegProd` helper reading the two
incidence CSRs the gate already reads, one `chainWeldKeyDense`, and the two incidence views on
K6a/K6b. `chains.score()` still sums the untouched `logOdds`, so the gate, the K9 order key and
every chain feature are bit-for-bit unchanged. Setting the constant to 0 restores the ship exactly.
**Compile-and-reproduce validation of the patch is running; until it lands, the numbers above are
from the env arm of the instrument** (`w5_ref/bin1`, bin `629f15dbd6b32d09bb00c8b2070d19a1`,
`LST_CHAIN_WELD_TIE=9 LST_CHAIN_WELD_KNEE=128`), whose key expression is character-for-character
the patch's.

Reproduce: `W5_BIN=w5_ref/bin1 bash w5_ref/gates.sh D128 all` with
`LST_CHAIN_WELD_TIE=9 LST_CHAIN_WELD_KNEE=128`; arms in `w5_ref/arms{1,2,4,6}.txt`; offline
instruments in `w5_ref/py/` (`wr.py` the validated replay, `fam.py` the stratified prior, `knee.py`
the exchange table, `ab.py` the offline key A/B, `cal.py` the calibration fit, `comp.py` the chain
composition, `cmp.py` the judge diff).
-- W5

## [W5 23:05] **THE ONE-CONSTANT PATCH IS BUILT AND IT REPRODUCES THE ENV ARM EXACTLY: `w5_dense_weld.patch` (md5 `dc7bcc691b28996813e2db2a589aeeb3`, 3 files +84/-2) compiles clean at `efb7e1e98c5` and its `SHIP128` arm is BIT-IDENTICAL to the `D128` env arm on all 23 jets fields AND all 35 PU200 fields.** Cube gates for the patched binary are running; the env arm they replicate was already bit-identical to the ship on both cubes.

Build: `gpu_wt/g3` at `efb7e1e98c5` + the patch, `lst_make_tracklooper -mcC`, 0 `error:` in the
fresh make log; bin md5 `f17aa4fb7040170b98e3e8a3cd9dbe0e`, lib `cc3d82874b9c6e177bcbdf162d227996`.
The instrument that produced every scan arm is preserved separately as `w5_ref/w5_instrument.patch`
(md5 `fb08b736d112091aaba322b8535c34a2`), and the frozen instrument binary is `w5_ref/bin1`.

What the patch is, in full: one constant `ChainConfig::kChainWeldFamilyDegKnee = 128`; one helper
`chainWeldDegProd` reading the two incidence CSR offset arrays `ChainGate` feature 14 already
reads; one `chainWeldKeyDense` that sets a single order bit ahead of `logOdds` for the E2 family
at junctions above the knee; and the two incidence views threaded onto K6a/K6b. Setting the
constant to 0 restores the shipped weld bit for bit (`if constexpr`, so the loads are not even
emitted). No new kernel, no new weight file, no new SoA column, no new network.
-- W5

## [W5 23:20] CUBE GATE CLOSED, AND IT IS THE STRONGEST SINGLE PIECE OF EVIDENCE FOR THE KNEE: **the patched binary `SHIP128` is BIT-IDENTICAL to the ship on `cube50` (10,000 evt, 35/35 fields) and on `cube50_highPt` (5,000 evt, 35/35), while the UNCONDITIONED E2-first arm moves 14 of `cube50`'s 35 fields and 12 of `cube50_highPt`'s, adverse on EVERY displaced efficiency band of both guns.** The cubes sit entirely below the knee, so the candidate's invariance there is by construction and now verified; the unconditioned arm's cube damage is the same displaced mechanism PU200 shows, and the knee removes all of it.

Caveats WITH the headline: `cube50` at `-s 4` segfaulted in the writer for two arms (the known
unpredictable crash), so the cube50 numbers below are the `-s 8` rerun -- and **`BASE` at `-s 4` vs
`-s 8` is bit-identical on all 35 fields**, which is the check that makes the stream change free.

| cube50 (10,000 evt), vs BASE | fields differing | worst displaced cells |
|---|---:|---|
| **SHIP128 (the patch)** | **0 of 35** | -- |
| D128 (env arm, `-s 4`) | **0 of 35** | -- |
| D32 (env arm, `-s 4`) | 0 of 35 | -- |
| TK5 (unconditioned) | **14 of 35** | `vxy[5,10)` **-.0140**, `vxy[1,5)` **-.0118**, `dxy[1,5)` -.0045, `dxy[5,10)` -.0025, `n_tc` -22, `dup_overall` +.00099 |

| cube50_highPt (5,000 evt), vs BASE | fields differing | worst displaced cells |
|---|---:|---|
| **SHIP128 (the patch)** | **0 of 35** | -- |
| D128 / D32 (env arms) | 0 of 35 | -- |
| TK5 (unconditioned) | **12 of 35** | `vxy[5,10)` -.0036, `dxy[0,1)` -.0035, `dxy[5,10)` -.0009, `n_tc_t4cl` +59 |

### `D128` / `SHIP128` -- the candidate, complete

| gate | result |
|---|---|
| jets TUNE core (paired) | **+.0216, p 1.1e-30**; deep core `[0,.0025)` **+.0591**; all 15 fine bins >= 0 |
| jets fake | -.0002 pooled (p .81); `<.02` +.0042 (p .61) |
| jets dup | **-.0008 pooled** (p .081); `[.02,.05)` -.0050 (p .017, better); one unresolved adverse cell `[0,.0025)` +.0726 (p .094) |
| PU200 `event_1000` (paired) | **no resolved cell in either direction**; worst `vxy[10,30)` -.0007 (4/1, p .375); `vxy[1,5)` +.0006, `dxy[1,5)` +.0003, `dxy[5,10)` +.0010; TC count moves 24 of 1,582,757 |
| `cube50` | **bit-identical, 35/35** |
| `cube50_highPt` | **bit-identical, 35/35** |
| patch | `w5_ref/w5_dense_weld.patch` md5 `dc7bcc691b28996813e2db2a589aeeb3`, 3 files +84/-2, builds clean, arm bit-identical to the env arm on all 23 jets + 35 PU200 fields |
| cost | 4 indexed loads per edge per sweep on arrays the gate already reads; **NOT TIMED** |
| never opened | jets 500-999, PU200 `event_2000..7000` |

**The round in one line: the lever that was refused for two rounds is not a calibration problem at
all -- it is an occupancy-conditioning problem, and conditioned at degProd >= 128 it delivers 83%
of its jets-core gain with every crown jewel and both cubes untouched.**
-- W5

---

## [G5 23:20] R2 IS TWO-SIDED BUT ONLY ONE SIDE EXISTS: **the loosening half is real and the tightening half is REFUSED on PU200 displaced at every conditioning I could build. The candidate `G5U` (TWO constants: branch-0 bar -1.0, far-dca free pass withdrawn above density 100) is a STRICT TWO-SIDED WIN on jets -- core .8197 -> .8209 (+.0012, 19 lost / 45 gained, p .0016), dR<.02 +.0016, dR<.05 +.0019, and fake DOWN 13.64 -> 12.68 TC/evt (-7.0%, core dR<.05 -8.8%) -- with PU200 GAINING on all three vxy crown jewels (+.0034 / +.0069 / +.0026, dxy[5,10) and [10,30) EXACTLY 0/0) and cube50_highPt gaining in 8 of 11 cells with ZERO sims lost in any of them.**

**Caveats WITH the headline.** (1) `G5U`'s debits are PU200 `fake_overall` +.00087 (.04425 ->
.04512, still under LST master's .0454) and PU200 `dup_overall` +.00056 (+887 dup TCs / 1000
events) -- **6x the debit that UN4's attach half was priced at**; on jets, dup TCs/evt 2.43 -> 2.49
(p 7e-4). (2) **Neither cube is bit-identical** -- branch 0's delta is unconditioned by
construction, so the cubes MOVE; every cell they move is a GAIN and it is priced below. (3) The
loosening half ALONE is a **bad trade at the round's own currency: 8.6 delivered fake TCs per
recovered core sim against a 2.81 budget**. It is affordable only because the far-cell knee pays 3x
that back in the same patch. (4) Jets tune 0-499 and PU200 `event_1000` by explicit path; sealed
halves never opened. Nothing timed, CPU only, no GPU run.

### Provenance and the inertness proof
Worktree `gpu_wt/g5` at `efb7e1e98c5` + `g5_ref/g5_gatebars.patch` (md5
`4c4c5034d3988719c8a7f4af21bfdd40`, 160 lines, 2 files) = N4's `t4_fardens.patch` re-applied
(cleanly, `git apply --3way`) plus the G5 branch-bar deltas. CPU build, **0 `error:` and 0 `failed
to compile`** in the fresh `.make.log.1786671801`; `bin/lst_cpu` md5
`2d5ed6a70b72106d9d229db410bb20c0`, lib `4a40c8df2c3123fcc9e73f679c946d35`, snapshot `g5_ref/snap/`.
**The knob-free arm is BIT-IDENTICAL to the pristine main-tree ship binary on 500 jet events (all
ten `tc_*` / `sim_tcIdx` branches) and identical on all 35 PU200 judge fields.** Full recipe:
`g5_ref/REPRODUCE.md`.

### The four bars, one delta each, DEPLOYED (jets tune 500 events, paired vs the control)

| arm | what it moves | core-all | lost/gained | fake TC/evt | core fake (dR<.05) |
|---|---|---:|---:|---:|---:|
| `B0F05` | b0 -0.5 | +.0005 | 13 / 24 | **+0.19** | +0.08 |
| **`B0F10`** | **b0 -1.0** | **+.0012** (p .0013) | 18 / 44 | **+0.45** | +0.20 |
| `B0F20` | b0 -2.0 | +.0009 | 38 / 58 | +1.23 | +0.55 |
| `B0D10` | b0 -1.0, ramped knee 100 | +.0002 | 2 / 7 | +0.20 | +0.10 |
| `T4D25`/`T4D30` | T4C1 relax 2 -> 2.5 / 3.0 | +.0001 / +.0005 | 6/9, 6/17 | +0.19 / +0.34 | +0.08 / +0.17 |
| `B1P2` | b1 +2.0 | +.0001 | 2 / 5 | **-0.66** | -0.18 |
| `B2P2` | b2 +2.0 | **-.0009** | **68 / 48** | -0.15 | -0.04 |
| `B3P2` | b3 +2.0 | +.0006 | 16 / 30 | **-0.57** | -0.13 |
| `B1D2`/`B3D2` | b1/b3 +2, ramped knee 100 | +.0000 / +.0003 | 0/0, 7/13 | -0.09 / -0.32 | -0.04 / -0.12 |
| `B1R30`/`B3R30` | the same, knee 30 | +.0000 / +.0004 | 0/1, 8/17 | -0.21 / -0.42 | -0.07 / -0.14 |
| **`F100`** | **far free pass, knee 100** | **+.0000** | **3 / 3** | **-1.63** | **-0.75** |

`F100` reproduces N4's `T4F100` on this head almost exactly (-1.63 vs -1.80 TC/evt, PU200 -45 TCs
in both). **It is still the single largest clean fake object in the pipeline.**

### THE REFUSAL, and its mechanism: the tightening side dies on PU200 displaced

| PU200 `event_1000`, 1000 events | dxy[1,5) | dxy[5,10) | dxy[10,30) | vxy[10,30) | verdict |
|---|---:|---:|---:|---:|---|
| `B1P2` b1 +2 flat | **-.0227** | **-.0566** | -.0051 | -.0115 | REFUSED |
| `B3P2` b3 +2 flat | **-.0435** | **-.0616** | -.0063 | **-.0419** | REFUSED |
| `B3D2` b3 +2, knee 100 | -.0020 | -.0020 | .0000 | -.0022 | REFUSED (small, but adverse) |
| `B3R30` b3 +2, knee 30 | -.0039 | -.0079 | .0000 | -.0050 | REFUSED |
| `B1R30` b1 +2, knee 30 | -.0007 | -.0010 | .0000 | .0000 | REFUSED |
| **`B1D2` b1 +2, knee 100** | **.0000** | **.0000** | **.0000** | **.0000** | the ONLY clean one -- and worth only -0.09 fake TC/evt |

**The mechanism, named:** branches 1 and 3 are the EXEMPT (large-dcaXY) branches -- they are not
"the jet fake branches", they are **the displaced branches**, and the same bar carries PU200's
displaced tracks and the jet core's fake 4-layer chains. JPR4's 8-9-fakes-per-real is a **jet-only
census**; on PU200 the same +2 deletes 57 of 1,007 `dxy[5,10)` sims. Density conditioning does NOT
separate them the way it does for the far cell: PU200's displaced tracks in the exempt-5+ branch
live ABOVE the knee, unlike the far-dca 4-layer cell where PU200 sits at density p50 5 and jets at
p50 780. **That asymmetry is why R7's far half works and R2's tightening half does not.**

And `B2P2` is a third, separate refusal with its own mechanism: JPR4 measured branch 2 as **too
loose (local LR 0.11, the marginal admit 9:1 against being real)**, yet a +2 tightening costs
**20 net core sims and buys only -0.15 fake TC/evt**. Killing a chain does not delete its fake TC --
**the claim re-fills the freed hits with another fake**. The gate's marginal exchange rate is not
the delivered exchange rate.

### The same correction applies to the LOOSENING side, in the opposite direction
JPR4 predicted b0 -1.0 = +45.7 gate-alive chains/evt at 2.7 bad per good, and warned the chain-level
"bad" column **over**-states the delivered fake cost by ~6x. Deployed, it is the other way round:
of the +12.3 extra REAL chains/evt only **0.052/evt become newly matched sims (0.4%)**, while of the
+33.4 extra fake chains/evt **0.45/evt become fake TCs (1.3%)**. **The delivered exchange rate is
~3x WORSE than the chain-level one, not 6x better** -- because a newly admitted real chain usually
belongs to a sim we already deliver. Recoverability tables built on gate-alive chains should be
divided, not multiplied.

### `G5U` = `LST_G5_B0=-1.0` + `LST_T4_FARDENS_RHO0=100` -- the candidate

| jets tune, 500 events, paired | control | **G5U** | delta | discordant | p |
|---|---:|---:|---:|---|---:|
| core-all | .8197 | **.8209** | **+.0012** | 19 lost / 45 gained | .0016 |
| core dR<.005 | .5183 | .5190 | +.0006 | 2 / 3 | 1 |
| core dR<.02 | .6288 | **.6304** | +.0016 | 4 / 13 | .049 |
| core dR<.05 | .7098 | **.7116** | +.0019 | 9 / 27 | .0039 |
| **fake TC/evt (pooled)** | 13.64 | **12.68** | **-0.96 (-7.0%)** | | 1.4e-24 |
| fake TC/evt dR<.05 | 5.34 | **4.87** | -0.47 (-8.8%) | | 1.3e-17 |
| fake TC/evt dR<.02 | 1.91 | **1.80** | -0.10 | | 1.8e-04 |
| dup TC/evt (COST) | 2.43 | 2.49 | **+0.06** | | 7e-04 |
| dup rate dR<.005 / dR<.05 | .1006 / .0263 | .1044 / .0274 | +.0038 / +.0011 | | .42 / .018 |
| T4-class TCs/evt (non-fake frac) | 15.5 (.548) | 15.5 (**.599**) | same count, **+9% purity** | | |

| PU200 `event_1000`, paired sims | control | **G5U** | delta | lost / gained | p |
|---|---:|---:|---:|---|---:|
| eff_overall | .80966 | .80978 | +.00012 | 47 / 56 | .43 |
| **vxy[1,5)** | .7975 | **.8009** | **+.0034** | **1 / 17** | 1.4e-04 |
| **vxy[5,10)** | .7163 | **.7232** | **+.0069** | **4 / 18** | .0043 |
| **vxy[10,30)** | .7087 | **.7113** | **+.0026** | **5 / 16** | .027 |
| dxy[0,1) | .8350 | .8356 | +.0006 | 51 / 97 | 2e-04 |
| dxy[1,5) | .5767 | .5786 | +.0019 | 5 / 11 | .21 |
| **dxy[5,10) / dxy[10,30)** | .2751 / .0564 | same | **+.0000** | **0 / 0 both** | 1 |
| fake_overall (DEBIT) | .04425 | .04512 | **+.00087** | | |
| dup_overall (DEBIT) | .03991 | .04047 | **+.00056** | | |

| cube (a GATE: movement must be a gain) | control | **G5U** |
|---|---|---|
| **cube50_highPt** (5000 evt, paired) | | **0 sims LOST in all 11 cells**; +12 `dxy[1,5)`, +7 `dxy[0,1)`, +6 `vxy[10,30)`, +4 `vxy[1,5)`, +2 `vxy[5,10)`, +2 `dxy[5,10)`; fake .00105 -> .00102, dup .01778 -> .01728 |
| **cube50** (full, `-s 32`, paired) | | **0 sims LOST in all 12 cells**; +5 `dxy[0,1)`, +2 `vxy[1,5)`, +2 `vxy[10,30)`, +1 `dxy[1,5)`; fake .00289 -> .00288, dup unchanged; `n_tc` +7 of 2,079 |

The cube50 control had to be re-run at `-s 32`: the shipped `-s 4` config **segfaulted the writer on
the CONTROL arm and not on either candidate arm** (the known unpredictable-in-arm-and-stream-count
bug). `-s 32` vs `-s 4` stream invariance was then VERIFIED on this binary, not assumed: the
`cube50_highPt` control is identical in all 35 judge fields between the two stream counts.

### The conservative variant `G5H` = `LST_G5_B0=-0.5` + `LST_T4_FARDENS_RHO0=100`
Half the gain and half the debit, measured not interpolated: jets core **+.0005** (14 / 26,
p .081), fake **-1.37 TC/evt (-10.1%)**, core fake -0.65; PU200 vxy **+.0019 / +.0049 / +.0010**,
`dxy[5,10)`/`[10,30)` 0/0, fake **+.00034**, dup **+.00031**; cube50_highPt all-gain, no cell lost
(cube50 for this variant is the one gate still on the queue at the time of writing; its `-s 4` run
against the `-s 32` control shows the same all-gain pattern at half the size).
**If the coordinator prices the PU200 duplicate debit above 26 jet-core sims, `G5H` is the arm to
take -- it keeps 71% of the fake win and 42% of the efficiency win for 55% of the dup debit.**

### WHAT I DID NOT MEASURE
Jets 500-999 and PU200 `event_2000` (sealed). **No timing at all** -- the arm adds one `float`
comparison and one add per chain in K7c and no kernel, but it is not timed and I make no ms claim.
**GPU never run** (CPU only; the arm has no new atomics and no new memory, but that is an argument,
not a measurement). The union with W5's weld work and C5's arbitration work -- **not measured, and
it must be measured as a union: `G5U` moves the same 4-layer population W5's weld sweeps feed and
C5's claim consumes.** The `B1D2` knee-100 b1 tightening is PU200-clean and worth -0.09 fake TC/evt;
I did NOT fold it into `G5U` (one more constant for 0.7% of the fake budget) and I did not run it
on the cubes.
