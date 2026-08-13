# FINDINGS_SF.md -- high-pT working-point recalibration of the EDGE head (agent SF)

Mission: recover the pT > 5 GeV efficiency the integrated pipeline loses to LST master (symptom: a
~-.024 dip in 25-50 GeV), whose suspected cause is that the edge head's OR-rule working-point
tables are fitted per-cell on high-pT bins that are statistically too thin to support a per-cell
fit. Fix under test: coarser |eta| grouping in the pT > 5 rows, and then, if that is not enough,
looser bars there.

Baseline for EVERY number in this file is `04c6e122e68` (jet-core round 2 ship). PU200 `event_2000`
and jets 500-999 are SEALED and are never run here.

--------------------------------------------------------------------------------------------------

## [SF 00:05] DESIGN ON RECORD, before any build -- plus two facts that make this round cheap

**What the tables are.** `EdgeNetworkWeights.h` carries `kWpPrompt[40]` and `kWpDisp[40]`. The index
is `fam * 20 + ptbin * 10 + etabin`, `fam` in {E1, E2}, `ptbin = (pT > 5)`, `etabin = |eta|/0.25`
capped at 9, all read off the edge's INNER node (`ChainEdges.h:553`, applied at `:529-531`). An edge
is weld-eligible iff `mP >= kWpPrompt[cell] || mD >= kWpDisp[cell]` -- the T3-DNN OR-rule. So the
twenty numbers this round touches are indices 10-19 and 30-39: the pT > 5 rows, and nothing else.
The pT <= 5 rows, all 2707 weight/bias/preprocessing literals, and every kernel stay byte-identical.
That is the whole blast radius, and `setwp.py` proves it by re-parsing both headers and comparing
every literal of every other array.

**Fact 1 -- my parked headers need no rebasing.** `EdgeNetworkWeights.h` is md5
`05b7f3f379aa5fa285538eba5b8b4d8a` at `81a9afe2d00`, `ec08aba9e5b`, `eb9369fd452` AND
`04c6e122e68`. The jet rounds retrained the CHAIN gate, never the edge head, so the four headers I
fitted before being parked are still fitted against the deployed edge head exactly.

**Fact 2 -- the CONTROL is free, and that is a validation, not a shortcut.** My refitted per-cell
CONTROL header `hdr_SF_percell.h` differs from the shipped header on exactly TWO lines, both
comments (the table filename and the provenance note). Every one of the 40 + 40 working-point
literals it fits comes out BIT-IDENTICAL to what is shipped. The barfit protocol pins each group's
acceptance to the shipped head's own acceptance of that group, so refitting at the shipped grouping
must return the shipped table -- and it does, to the last float literal. Consequence: I do not build
or run a separate control arm. **The pristine `04c6e122e68` binary IS the control**, and the
per-cell/CONTROL row of every table below is by construction the shipped row. This also re-answers,
for the edge head, the question `ATCAL` is asking for the attach head: at the shipped grouping there
is no calibration drift to recover, so any gain this round produces comes from the GROUPING, not
from re-fitting.

**Arms, in the order they will be run.**

| arm | pT>5 abs(eta) grouping | worst per-cell acceptance deviation (offline, parked-era) | status |
|---|---|---|---|
| CONTROL | per-cell, 10 groups | E1 .0240 / E2 .0031 reference | = shipped binary, no build |
| `SF2W` | 2-way: [0-3] [4-9] | **E1 .0069 / E2 .0052 <- best offline** | lead arm |
| `SF3W` | 3-way: [0-3] [4-7] [8-9] | E1 .0209 / E2 .0056 | only if `SF2W` moves something |
| `SFPOOL` | fully pooled | E1 .0240 / E2 .0031 | only as the grouping-coarseness endpoint |

The deviation column is a SPLIT-HALF STABILITY measure (100 VAL events vs the other 100), not an
efficiency prediction: it says a per-cell high-pT fit is unstable at the .02-.08 level in E1 and a
2-way fit is stable at .007, i.e. the per-cell table's high-pT rows are largely fitting noise. It
does NOT say deployment will move. Offline proxies on this project have mispredicted deployment 4+
times and I am treating every number in that column as a hypothesis, not as evidence.

**Second half of the mission, deliberately sequenced second.** Relaxing the high-pT bars is a
separate lever from regrouping them, and it is only worth spending on once I know whether regrouping
alone moves the 25-50 GeV cell. It also runs straight into the project rule that a win in an
LST-carried cell is not a win: at pT > 5 both we and master are near their respective ceilings, so I
will report every efficiency-vs-pT band as OURS, MASTER, and the GAP, and will call a loosening a
win only if it closes the gap.

**Provenance.** Fresh worktree `gpu_wt/sf2`, detached at `04c6e122e68`, built as a full SCRAM area
(the main tree's plumbing `cp -a`'d, then `git worktree add --detach src`). Seeded with the main
tree's pristine standalone artifacts, which verify as binary `5293c48f2561a6968efecf4fd4b1270f`,
lib `e7a11d8115ca59a7d53a26db5ea8c7d5` -- the same two hashes AT froze at `at_ref/basebin/` as the
pristine ship binary, arrived at from an independent copy. Frozen again inside my own area at
`nnloop_ref/sf_work/basebin/`. Every run below prints the loader-RESOLVED `liblst_cpu.so` md5.

**Infrastructure note for whoever copies a SCRAM area next (benign here, but know it).** The `cp -a`
plumbing recipe leaves the donor area's absolute path baked into five files under `.SCRAM/<arch>/`,
so `cmsenv` inside the copy reports the DONOR's `CMSSW_BASE`. In `gpu_wt/at1` today that resolves to
`gpu_wt/g7`. It is harmless for standalone work -- the standalone `Makefile` puts
`-I${TRACKLOOPERDIR}/../../../` (the worktree's own `src`) BEFORE `-I${CMSSW_BASE}/src`, so a
worktree's own headers always win -- but it is not harmless for `scram b`. I rewrote the five files
in `sf2` so its `CMSSW_BASE` is `sf2`, and I am flagging it rather than editing anyone else's tree.

I am not touching `AttachNetworkWeights.h` or any `ChainConfig` bar -- agent AT owns those this
round -- and there is no kernel, cell or conditioning change, so the E1-B2 far-dca cell is untouched.

--------------------------------------------------------------------------------------------------

## [SF 00:45] THE OFFLINE BIN-POOLING WIN DOES NOT SURVIVE DEPLOYMENT. `SF2W` moves 2 sim tracks in 75,422, both losses, and ZERO above 5 GeV. Caveat riding with it: this is a clean negative on the REGROUPING lever only, measured on the 1000-event tune sample, and it does not yet acquit the loosening lever -- the ceiling arm that decides that is running.

**First, the provenance chain closed.** My `CTL` arm -- the pristine `04c6e122e68` binary run through my
own harness in my own worktree -- is **bit-identical on all 35 judge fields to AT's independently
produced `BASE_pu`**, down to `n_tc = 1583791`. Two agents, two worktrees, two copies of the binary,
same numbers. The control is real and the CPU determinism the round relies on is confirmed.

**And the control needed no run of its own anyway.** As predicted in the design post, `hdr_SF_percell.h`
(the refit at the shipped grouping) is comment-only different from the shipped header: all 80
working-point literals reproduce bit-for-bit. So CONTROL == shipped, by construction and in fact.

**`SF2W` deployed, PU200 tune (`event_1000`), paired McNemar against `CTL` on the same 1000 events:**

| cell | CTL | SF2W | delta | CTL-only | SF2W-only | p |
|---|---|---|---|---|---|---|
| eff overall | .8097 | .8097 | -.0000 | 1 | 0 | 1 |
| pt [5,7) | .9060 | .9060 | +.0000 | 0 | 0 | 1 |
| pt [7,10) | .9059 | .9056 | -.0003 | 1 | 0 | 1 |
| pt [10,25) | .9174 | .9174 | +.0000 | 0 | 0 | 1 |
| **pt [25,50)** | **.9032** | **.9032** | **+.0000** | **0** | **0** | **1** |
| pt [50,inf) | .8763 | .8763 | +.0000 | 0 | 0 | 1 |
| pt [5,inf) | .9087 | .9086 | -.0001 | 1 | 0 | 1 |
| dxy [1,5) | .5621 | .5614 | -.0006 | 2 | 0 | .5 |
| all four dxy / three vxy | -- | -- | neutral or -.0006 | -- | -- | -- |
| dup / fake overall | .0428 / .0454 | .0428 / .0454 | .0000 / .0000 | -- | -- | -- |

Whole-sample blast radius: **`n_tc` 1583791 -> 1583813, +22 track candidates in 1.58 million
(+0.0014%)**, and exactly **2 sim tracks change matched status in 75,422, both losses**. The
25-50 GeV band -- the band this round exists to fix -- has *zero* discordant sims. It is not that the
arm is a small win or a small loss; the arm is a no-op that costs 2 tracks.

**Why the proxy lied, stated mechanically so the next agent does not repeat it.** `wpsf.py`'s protocol
pins each group's bar to reproduce **the shipped head's own acceptance of that group**. Regrouping
therefore preserves aggregate acceptance *by construction* -- that is what made the per-cell refit
reproduce the shipped table bit-for-bit, and it is the same property that makes a REgrouped refit land
within a hair of it. What my parked split-half diagnostic measured was the instability of the
acceptance TARGET ESTIMATE on thin cells (E1 displaced pT>5 swinging .208 between event halves), not
instability of delivered efficiency. Those are different quantities and I conflated them. The bars
move by roughly .1-.3 logit units; almost no true high-pT edge sits in that sliver, because the
OR-rule admits an edge if EITHER class clears its bar and high-pT true edges clear both comfortably.
A stability argument about a fitted target is not an argument about supply.

**The gap I am chasing, now measured on `04c6e122e68` rather than inherited.** SHIPPED vs LST master,
same 1000 events, paired:

| ptband | nSim | OURS | MASTER | gap (master ahead) | ours-only | master-only | p |
|---|---|---|---|---|---|---|---|
| pt [0.9,5) | 59240 | .8278 | .8274 | **-.0004 (we lead)** | 531 | 507 | .46 |
| pt [5,7) | 4030 | .9060 | .9079 | +.0020 | 49 | 57 | .50 |
| pt [7,10) | 3283 | .9059 | .9071 | +.0012 | 49 | 53 | .77 |
| pt [10,25) | 4478 | .9174 | .9216 | +.0042 | 68 | 87 | .15 |
| **pt [25,50)** | **1054** | **.9032** | **.9175** | **+.0142** | **20** | **35** | **.058** |
| pt [50,inf) | 396 | .8763 | .8788 | +.0025 | 12 | 13 | 1 |
| pt [5,inf) | 13241 | .9087 | .9122 | +.0035 | 198 | 245 | **.029** |
| overall | 72481 | .8426 | .8429 | +.0003 | 729 | 752 | .55 |

Two headline caveats that belong WITH those numbers. **(1) The dip is smaller than the -.024 the
mission was opened on: it is +.0142 at 25-50 GeV on `04c6e122e68`, and the whole pT>5 deficit is
+.0035 (p=.029).** I cannot tell from here whether the jet round closed part of it or whether the
-.024 came from a different selection; what I can say is that the number to beat today is .0142, and
in absolute terms it is **15 net sim tracks out of 1054**. **(2) It is not localised in eta**, which
is the fact that most undercuts the mission's premise:

| pT | abs(eta) 0-.75 | .75-1.1 | 1.1-1.7 | 1.7-2.4 | 2.4-4.5 |
|---|---|---|---|---|---|
| 25-50 gap | +.0168 (5 vs 12) | -.0052 (3 vs 2) | +.0209 (8 vs 13) | +.0210 (4 vs 7) | +.0156 (0 vs 1) |

A deficit spread evenly over every eta region, single-digit counts per cell, is not the signature of
a per-cell |eta| fit going wrong in specific cells. An |eta| REgrouping cannot fix a defect that has
no |eta| structure, and deployment agrees that it does not.

**So the round now turns entirely on the second lever, and I am testing its CEILING rather than
guessing a delta.** `SFOPEN` sets all twenty pT>5 entries of both tables to `-1e30`: every pT>5 edge
becomes weld-eligible. It is a diagnostic, never a ship candidate. It **upper-bounds every possible
regrouping and every finite loosening of these twenty numbers**, so if `SFOPEN` does not move
pt[25,50), then the edge working-point table is not the mechanism of this gap and no arm in this round
can be, which is a result worth having and worth having cheaply. `SFL10` (a uniform -1.0 shift of the
same twenty numbers, the realistic version) runs alongside it.

**Provenance.** `SF2W` lib `1abd10b5d6fd51baf1ecd08789fe60ae`, `SFOPEN` lib
`35453bcc0e3cab1a4273b5551ea11bdd`, `SFL10` lib `c6a2354ece2fa871ab563598183d0038`, control lib
`e7a11d8115ca59a7d53a26db5ea8c7d5`; 0 `error:` in each fresh `.make.log.*`; `bin/lst_cpu` is
byte-identical across all four arms (the weights live in the library, so the LIBRARY md5 is the only
discriminator -- `gates.sh` verifies the loader-resolved one on every run and aborts on a mismatch).
Every arm is run from a FROZEN snapshot under `sf_work/arms/`, never from the live build tree.

**A TRAP I HIT, which invalidates any master comparison made with the standard judge.**
`p4_ref/paired_rle.py` keys OUR ntuple by `(run, lumi, evt)` and LST master's by its content-hash
fallback, then pairs the two sorted orders POSITIONALLY. Against `master_ref/master_rv1000.root` those
are two different orderings, so it silently compares unrelated events: run directly, it reports
master's `eff_dxy_0_1` as **.0903** when the correctly-keyed value is **.8297**. It is not wrong on
ours-vs-ours (both sides then have `run/lumi/evt`), which is why this has stayed invisible.
`sf_work/effpt2.py` forces the content key on BOTH sides whenever either side lacks `run/lumi/evt`,
and `--cells` reproduces paired_rle's own table on correctly-keyed rows; the honest displaced picture
against master is that **we lead master everywhere on displaced** (vxy[10,30) .6963 vs .6257,
dxy[1,5) .5621 vs .5097, overall neutral at +.0003). Anyone who has published a master-paired number
from `paired_rle.py` should re-derive it.

--------------------------------------------------------------------------------------------------

## [SF 01:05] MECHANISM: the pT>5 gap versus master is a PIXEL-SEEDED SUPPLY deficit, not a weld deficit. All 35 of the 25-50 GeV sims master finds and we miss are carried by master's pT5/pLS/pT3, ZERO by T5 or chain -- and we produce no overlapping track candidate for any of them. Caveat with the headline: 35 sims on 1000 events, so the DECOMPOSITION is what is solid here, not the .0142 to three digits.

This is the result that explains `SF2W`'s no-op and, I think, retires the round's premise. Three
measurements, each on the same paired 1000 tune events, `CTL` (= shipped) vs `master_rv1000`.

**1. The chain path barely carries the band the round is about.** TC-type composition of MATCHED core
sims, ours:

| ptband | nMatched | pT5 | T5 | pLS | pT3 | **T4 (chain)** |
|---|---|---|---|---|---|---|
| 0.9-5 | 49037 | .519 | .131 | .279 | .042 | .029 |
| 5-7 | 3651 | .640 | .185 | .141 | .020 | .015 |
| 7-10 | 2974 | .578 | .244 | .149 | .015 | .013 |
| 10-25 | 4108 | .420 | .417 | .132 | .011 | .019 |
| **25-50** | **952** | .168 | **.650** | .140 | .009 | **.033** |
| 50+ | 347 | .104 | .683 | .153 | .006 | .055 |

Chain-delivered TCs carry **3.3%** of matched sims at 25-50 GeV. The edge working-point table gates
weld eligibility and therefore gates only that 3.3% -- and most of those sims are also reachable by
T5/pT5, so extra chain supply there converts to duplicates before it converts to efficiency. A lever
with a 3.3% ceiling on the band cannot close a 1.6-percentage-point gap in it. That is the arithmetic
I should have done before fitting four tables.

**2. The deficit is entirely pixel-seeded.** For each sim in the discordant set, the TC type that
carried it in the pipeline that found it:

| ptband | MASTER-only (we miss): master's type | OURS-only (master misses): our type |
|---|---|---|
| 5-10 | 110: pT5 46, pT3 39, pLS 21, T5 3, chain 1 | 98: T5 39, pLS 30, pT5 14, chain 8, pT3 7 |
| 10-25 | 87: pT5 46, pT3 22, pLS 17, T5 2 | 68: T5 32, pT5 15, pLS 14, pT3 6, chain 1 |
| **25-50** | **35: pT5 26, pLS 5, pT3 4 -- T5 0, chain 0** | 20: T5 11, chain 3, pLS 3, pT5 2, pT3 1 |
| 50+ | 13: pT5 12, pT3 1 | 12: T5 8, chain 3, pT5 1 |

At 25-50 GeV **35 of 35** master-only sims are pixel-seeded (pT5 26 + pLS 5 + pT3 4); not one is a T5
or a chain TC. The pattern is consistent across every band above 5 GeV: we LOSE pixel-seeded tracks
and WIN outer-tracker ones. The net -15 sims at 25-50 is `20 won on T5/chain` minus `35 lost on
pixel`. So the gap is not "our welds are too tight", it is "our pixel-seeded supply is thinner than
master's at high pT, and our T5/chain supply is thicker".

**3. It is missing SUPPLY, not a spoiled match or an arbitration displacement.** For all **35** of
those sims, `sim_tcIdxAll` in our ntuple is **EMPTY**: we build no track candidate overlapping the sim
at all. Zero of 35 are cases where we built something that then failed the 0.75 matched-hit
threshold, and zero are cases where a chain TC or a retirement claimed the track away from a pixel
seed. Validated rather than assumed: on 200 events in the same band, `sim_tcIdxAll` is non-empty for
**193/193** matched sims and empty for **0/22** unmatched ones, so "empty" carries the meaning I am
giving it and the match threshold never bites in this band.

**What this means for the mission as written.** The mission is "reduce the number of WP bins at high
pT and relax the high-pT cuts". Both halves act on `kWpPrompt`/`kWpDisp`, which act on weld
eligibility, which acts on the chain path, which carries 3.3% of the band and none of the loss. I do
not believe either half can close this gap, and `SF2W` deploying to 2 tracks (both losses) is the
first confirmation. `SFOPEN` -- every pT>5 edge weld-eligible, the strict ceiling of both halves --
is the second and is running; I will report it whatever it says.

**Where I would point the next agent instead, stated as a lead and not as a result I have proven.**
The high-pT deficit lives in the pT5/pT3/pLS supply. That is pLS-side and therefore sits in exactly
the territory `feedback_no_lst_help_changes` warns about -- but the warning is against claiming a WIN
in an LST-carried cell, and this is a LOSS in one, which is a different thing and is a real gap
against master. Whether the cause is our pLS dedup/retirement machinery, the pT3 dedup work, or
something in the pixel-side plumbing, I have not established, and it is outside both halves of my
brief. It should be measured before another edge-table round is commissioned.

--------------------------------------------------------------------------------------------------

## [SF 01:25] VERDICT: BOTH HALVES OF THE MISSION ARE DEAD, and the ceiling arm says why. Opening EVERY pT>5 weld bar completely moves pt[25,50) by ZERO sim tracks while making fake and three displaced bands WORSE. Caveats with the headline: this bounds arms that touch only the pT>5 rows (my brief's scope), it is the 1000-event tune sample, and the cube and jet gates are still running -- but no gate can rescue an arm whose best case is +0 tracks in the target band.

`SFOPEN` sets all twenty pT>5 entries of both tables to `-1e30`: every pT>5 edge is weld-eligible.
It is the strict upper bound on every regrouping and every finite loosening of those numbers.

**PU200 tune (`event_1000`), all four arms, `d3_ref/pu_judge.py`:**

| field | CTL (= shipped) | SF2W | SFL10 (-1.0) | SFOPEN (ceiling) |
|---|---|---|---|---|
| eff overall | .8097 | .8097 | .8097 | .8097 |
| eff barrel / transition / endcap | .9242 / .8796 / .6795 | .9242 / .8796 / .6795 | .9241 / .8796 / .6795 | .9243 / .8796 / .6796 |
| eff vxy [1,5) / [5,10) / [10,30) | .7954 / .7128 / .6963 | .7954 / .7128 / .6963 | .7954 / .7128 / **.6960** | .7954 / .7128 / **.6958** |
| eff dxy [0,1) | .8345 | .8344 | .8344 | .8345 |
| eff dxy [1,5) | .5621 | **.5614** | **.5595** | **.5598** |
| eff dxy [5,10) | .2502 | .2502 | **.2483** | **.2473** |
| eff dxy [10,30) | .0538 | .0538 | **.0519** | **.0519** |
| dup overall | .0428 | .0428 | .0428 | .0428 |
| fake overall | .0454 | .0454 | **.0456** | **.0456** |
| fake barrel | .0519 | .0520 | **.0525** | **.0525** |
| n_tc | 1583791 | 1583813 | 1584108 | 1584035 |
| n_tc chain (t4cl) | 63851 | 63871 | 64041 | 63859 |

**Paired McNemar on pT bands, `CTL` vs `SFOPEN`, same 1000 events:**

| ptband | nSim | CTL | SFOPEN | delta | CTL-only | OPEN-only | p |
|---|---|---|---|---|---|---|---|
| pt [5,7) | 4030 | .9060 | .9062 | +.0002 | 1 | 2 | 1 |
| pt [7,10) | 3283 | .9059 | .9065 | +.0006 | 0 | 2 | .5 |
| pt [10,25) | 4478 | .9174 | .9172 | -.0002 | 1 | 0 | 1 |
| **pt [25,50)** | **1054** | **.9032** | **.9032** | **+.0000** | **0** | **0** | **1** |
| pt [50,inf) | 396 | .8763 | .8763 | +.0000 | 0 | 0 | 1 |
| pt [5,inf) | 13241 | .9087 | .9088 | +.0002 | 2 | 4 | .69 |
| overall | 72481 | .8426 | .8426 | +.0000 | 3 | 5 | .73 |

**The target band moves by zero sim tracks under the maximum possible intervention.** Six sims change
anywhere above 5 GeV, net +2, p=.69. Against master the gap stays exactly +.0142 at 25-50 GeV.

**Why: the pT>5 bars were already essentially non-binding.** Removing all twenty of them entirely adds
**8 chain TCs in 63851** (+0.013%) and 244 TCs in 1.58 million. There was no withheld high-pT supply
to release -- high-pT true edges clear the OR-rule comfortably already, which is the same fact that
made `SF2W` a no-op, seen from the other end. The mission was premised on the high-pT working point
being too tight. Measured, it is not tight at all.

**And the lever is not free even at zero benefit.** `SFOPEN` and `SFL10` both cost fake (+.0002
overall, +.0006 barrel) and three of the four dxy bands, including **dxy[10,30) .0538 -> .0519** and
**dxy[5,10) .2502 -> .2473** -- the displaced cells the project protects in every round. Loosening the
high-pT weld admission admits false edges that survive into displaced-band TCs. So the second half of
the mission is not merely ineffective, it trades protected crown jewels for nothing. **No arm from
this round should ship.**

**One honest oddity I will not paper over.** Chain TC count is NOT monotone in eligibility: `SFL10`
(bars down 1.0) yields 64041 chain TCs while `SFOPEN` (bars fully open) yields 63859, fewer. That is
not a bug in either arm -- welding is competitive, the weld key is an argmax over eligible edges, and
K10 emits a TC only for chains of >= 4 layers, so admitting more edges can merge what would have been
two short chains into one longer one and LOWER the count. It does mean "looser bar => more chain
supply" is false as stated, and it is a reason to distrust any future weld-side argument that assumes
monotonicity. It does not affect the verdict: both arms move the target band by zero.

**Scope limit on the ceiling claim, stated precisely.** `SFOPEN` bounds arms that modify only the
**pT>5** rows, which is what my brief scopes ("reduce the number of WP bins at high pT", "relax the
high-pT cuts"). It does NOT bound an arm that changes the pT<=5 rows -- e.g. pooling the two pT bins
to defend against a high-pT track being MISBINNED into the pT<=5 row, since `wpPt` is the noisy
T3-radius pT (`ChainEdges.h:414`) and the pT<=5 bars are the tighter ones (E1 ~-0.42..-0.64 vs
~-0.69..-1.05). I did not test that variant, and I recommend against commissioning it, because the
mechanism post above shows the 25-50 GeV losses have **no overlapping TC of any type** and are carried
by master's **pixel-seeded** pT5/pLS/pT3 -- objects no outer-tracker weld bar can produce.

--------------------------------------------------------------------------------------------------

## [SF 01:35] EXACT REPRODUCTION RECIPE, so nobody has to trust my prose

**Worktree.** `gpu_wt/sf2`, a FULL SCRAM area: the main tree's plumbing (`biglib bin cfipython config
doc external include objs poison python static test tmp lib .SCRAM`, ~750 MB) `cp -a`'d, then
`git worktree add --detach src 04c6e122e68`. Seeded with the main tree's pristine standalone build
products (`LST/*.o`, `LST/liblst_cpu.so`, `bin/lst_cpu`, `code/rooutil/*.o`), which is what let every
arm be an 86-second incremental relink instead of a cold build. Then five files under
`.SCRAM/el9_amd64_gcc13/` (`RuntimeCache.json`, `MakeData/Tools.mk`, `MakeData/variables.mk`,
`tools/self`, `MakeData/Tools/self.mk`) had the donor path rewritten to `gpu_wt/sf2` so `cmsenv`
resolves to sf2. Env: `gpu_wt/sf2/env.sh`. Build: `gpu_wt/sf2/build.sh -mC` (prints the edge-header
md5, the fresh-log `error:` count, and the loader-RESOLVED library md5 every time).

**Arms are built by header substitution only.** `cp nnloop_ref/sf_work/hdr_SF_<ARM>.h
src/alpaka/EdgeNetworkWeights.h`, rebuild, then freeze `bin/lst_cpu` + `LST/liblst_cpu.so` into
`sf_work/arms/<ARM>/`. Runs execute from the frozen snapshot with `LD_LIBRARY_PATH` pointed at it, and
`gates.sh` aborts unless the loader-resolved library md5 equals the snapshot's -- `bin/lst_cpu` is
byte-identical across all four arms (weights live in the library), so the library md5 is the ONLY
discriminator and checking it is not optional.

**Header generators** (all verify by re-parsing both headers and comparing every literal of every
array, and all assert the pT<=5 rows untouched):
`sf_work/wpsf.py --groups {percell,2way,3way,pooled}` + `sf_work/setwp.py` for the regrouping arms;
`sf_work/loosen.py --delta D` / `--open` for the loosening arms (new this round).

**Judges** -- reused, not rebuilt: `d3_ref/pu_judge.py` (35 fields), `m3_ref/jetphys.py`,
`p4_ref/jetgate.py`, `p4_ref/paired_rle.py`. New: `sf_work/effpt2.py`, which supersedes my
`sf_work/effpt.py` (that one imported `a5_ref/paired.py`, now archived). `effpt2.py` reuses
paired_rle's `load`/`cells`/`mcnemar_p` verbatim by exec'ing its source with the trailing bare
`main()` call stripped -- a plain `import` of paired_rle RUNS its report against the importer's argv,
which is how I first noticed the keying bug.

**Run recipes.** `sf_work/gates.sh <BINDIR> <TAG> [all|pu|jet|cubes|cubehi|cube50]`, adapted from
`at_ref/gates.sh` with the paths moved: PU200 tune `-i PU200RelVal -n 1000 -s 8 -p 0.8 -w 1`; jets
`-i jet_ref/trackingNtuple_jets_1000.root -n 500 -s 16 -p 0.8 -J -w 1` (events 0-499);
`-i cube50 -n -1 -s 4`; `-i cube50_highPt -n -1 -s {4,2,1}` with automatic retry at lower `-s` on
`RUN_EXIT=139`. `sf_work/holdout.sh <BINDIR> <TAG>` runs `event_3000..7000` (5000 events) and contains
a hard `case` guard that exits 9 if `event_2000` ever appears in the file list -- the SEALED sample is
excluded structurally, not by my remembering to.

**Artifact inventory** (all under `standalone/nnloop_ref/sf_work/`, nothing in `/tmp`):
`hdr_SHIPPED.h` + `hdr_SF_{percell,2way,3way,pooled,OPEN,LOOSE10}.h`; `patch/SF_{2way,OPEN,LOOSE10}.patch`;
`arms/{SF2W,SFOPEN,SFLOOSE10}/` and `basebin/` (frozen binaries); `runs/` (every ntuple, log, judge,
json, and the `GAP_*` / `CMP_*` tables); `logs/` (build logs and the fresh make logs).

**Patches, `git apply --check` run inside a tree literally detached at `04c6e122e68`:**

| arm | patch | md5 | check |
|---|---|---|---|
| `SF2W` | `patch/SF_2way.patch` | `fd18902e5693eacb020a2695d4c1440a` | CLEAN |
| `SFOPEN` (diagnostic) | `patch/SF_OPEN.patch` | `ab05edc34024b2110c5ba2ddbb4b48fe` | CLEAN |
| `SFL10` | `patch/SF_LOOSE10.patch` | `eb0bbb2aa574b44b8205d617921cea0a` | CLEAN |

`SF_2way.patch` is **byte-identical to my parked `SF_PARKED_g1_state.patch`** (same md5), regenerated
independently from the shipped blob -- the parked state and the rebuilt state are the same patch.
Each touches only `RecoTracker/LSTCore/src/alpaka/EdgeNetworkWeights.h`, 10 changed value lines plus
the provenance comment. **I am recommending that NONE of them ship** (see the verdict post); they are
banked so the measurement is reproducible, not because any is a candidate.

The main tree was never built in and its tracked files are clean; `gpu_wt/sf2` is restored to a
pristine `04c6e122e68` (edge header back to `05b7f3f379aa5fa285538eba5b8b4d8a`, no modified or deleted
tracked files). I committed nothing and published nothing.

--------------------------------------------------------------------------------------------------

## [SF 02:05] CUBES: `SF2W` neutral-or-better on both, control again bit-identical to AT's. Plus the fact that most changes how this round's target should be read: on the high-pT DISPLACED gun we beat master 2.6x at 25-50 GeV (.0224 vs .0074, 228 ours-only vs 0). The PU200 high-pT dip is a PROMPT, pixel-seeded deficit sitting next to a displaced high-pT reach we already dominate.

**`cube50_highPt`, 10000 events, `-s 4` (no segfault, `CTL cubehi ok at -s 4` / `SF2W cubehi ok at -s 4`):**

| field | CTL | SF2W | delta |
|---|---|---|---|
| eff overall | .3678 | .3678 | +.0000 |
| eff vxy [1,5) / [5,10) / [10,30) | .3097 / .2162 / .0421 | .3097 / **.2176** / **.0423** | .0000 / **+.0014** / **+.0002** |
| eff dxy [0,1) / [1,5) / [5,10) / [10,30) | .1554 / .1120 / .0630 / .0085 | .1554 / **.1124** / **.0634** / .0085 | .0000 / **+.0004** / **+.0004** / .0000 |
| dup / fake overall | .0186 / .0016 | .0186 / .0016 | .0000 / .0000 |
| n_tc / n_tc chain | 1880 / 828 | 1885 / 834 | +5 / +6 |

Neutral-or-better in every cell, four cells a hair positive. `cube50` (10000 events) is **bit-identical**
between `CTL` and `SF2W` on all 19 fields. Both cube gates pass -- but with `SF2W` moving 2 PU200
tracks and 0 above 5 GeV, passing them is not an argument for it.

**THE `cube50_highPt` dxy[1,5) GATE NUMBER IS EVENT-COUNT DEPENDENT, and .1209 vs .1120 is NOT a
regression.** My brief says "cube50_highPt dxy[1,5) is now .1209 ... do not give it back". My pristine
control measures **.1120**. Both are right and AT's runs contain both, independently:

| run | n_evt | n in dxy[1,5) | eff dxy[1,5) |
|---|---|---|---|
| `at_ref/runs/BASE_cubehi5` (AT, pristine) | 5000 | 2705 | **.1209** |
| `at_ref/runs/BASE_cubehi` (AT, pristine) | 10000 | 5312 | **.1120** |
| `sf_work/runs/CTL_cubehi` (mine, pristine) | 10000 | 5312 | **.1120** |

The round's headline `.1209` is defined on a **5000**-event cube50_highPt run; the second 5000 events of
that gun are harder, so the same binary gives `.1120` over 10000. **Anyone comparing a 10000-event arm
against the 5000-event .1209 will see a phantom -.0089 regression and may kill a good arm for it.**
The gate needs its event count stated with it. My control at 10000 reproduces AT's at 10000 to four
decimals -- the second bit-identical cross-agent agreement this round.

**And the result that reframes the mission.** `cube50_highPt` is the high-pT DISPLACED gun (median pT
22.4 GeV, median vperp 41.8 cm), which needs the band selection rather than the prompt core cut -- the
core cut passes 30 of its 55923 sims, which is why pT bands on the cubes have never been read before.
Banded, CTL vs LST master on 5000 common events:

| ptband | nSim | OURS | MASTER | delta | ours-only | master-only | p |
|---|---|---|---|---|---|---|---|
| pt [3,5) | 1159 | .0233 | .0164 | **-.0069** | 8 | 0 | .0078 |
| pt [5,7) | 1192 | .0294 | .0134 | **-.0159** | 20 | 1 | 2.1e-05 |
| pt [7,10) | 1780 | .0348 | .0157 | **-.0191** | 34 | 0 | 1.2e-10 |
| pt [10,25) | 9191 | .0248 | .0111 | **-.0137** | 128 | 2 | 1.3e-35 |
| **pt [25,50)** | **15248** | **.0224** | **.0074** | **-.0150 (3.0x master)** | **228** | **0** | **4.6e-69** |
| pt [5,inf) | 27411 | .0243 | .0094 | **-.0148 (2.6x)** | 410 | 3 | 1.1e-117 |

(negative delta = WE lead). At 25-50 GeV on displaced tracks we find **228 sims master misses and lose
zero**. So "we lose high pT to master" is true only for PROMPT tracks and only through the pixel-seeded
path; on displaced high pT we are 3x master with no losses at all.

**Why that matters for the decision, not just as a nice number.** `SFOPEN`/`SFL10` bought exactly zero
prompt high-pT efficiency while costing PU200 dxy[5,10) (.2502 -> .2473) and dxy[10,30)
(.0538 -> .0519). That is the trade the second half of the mission actually offers: give up displaced
cells we dominate to chase a prompt cell LST owns and that the weld bar cannot reach anyway. Under
`feedback_tuning_priority` (displaced bands are crown jewels) and `feedback_no_lst_help_changes`, that
trade should be refused even if it had worked -- and it did not work.

--------------------------------------------------------------------------------------------------

## [SF 02:35] REVERSAL, AND I WAS ABOUT TO CLOSE THE ROUND ON A WRONG NEGATIVE. The high-pT weld bars ARE binding -- for DISPLACED tracks. `SFL10` gains 110 / 106 / 88 / 52 sims in the four displaced cube cells with 0-1 losses each (p to 1.5e-33) and LOWERS cube dup. Caveats WITH the headline: it buys ZERO of the prompt 25-50 GeV gap the mission was opened on, it costs PU200 fake +.0002 and a coherent (though individually insignificant) ~-.002 across all four PU200 dxy bands, so it FAILS two hard gates as it stands and I am not proposing it for ship yet.

My previous post said the pT>5 bars were "essentially non-binding" because opening them added only 8
chain TCs on PU200. **That inference was wrong, and the cube gate caught it.** On PU200 the high-pT
population is prompt and tiny; on `cube50_highPt` -- the high-pT DISPLACED gun, median pT 22.4 GeV,
median vperp 41.8 cm -- opening the same twenty numbers adds **269 chain TCs (828 -> 1097, +32%)**.
The bars were binding hard the whole time, on a population PU200 barely contains. Had I stopped at
the PU200 tune sample and the ceiling arm's PU200 numbers, I would have filed a false negative.

**`cube50_highPt`, 10000 events, all four arms:**

| field | CTL | SF2W | **SFL10 (-1.0)** | SFOPEN (ceiling) |
|---|---|---|---|---|
| eff overall | .3678 | .3678 | **.3793** | .3793 |
| eff vxy [1,5) | .3097 | .3097 | **.3226** | .3226 |
| eff vxy [5,10) | .2162 | .2176 | **.2351** | .2358 |
| eff vxy [10,30) | .0421 | .0423 | **.0497** | .0499 |
| eff dxy [0,1) | .1554 | .1554 | **.1597** | .1597 |
| eff dxy [1,5) | .1120 | .1124 | **.1216** | .1212 |
| eff dxy [5,10) | .0630 | .0634 | **.0762** | .0765 |
| eff dxy [10,30) | .0085 | .0085 | **.0128** | .0129 |
| dup overall | .0186 | .0186 | **.0152** | .0152 |
| fake overall | .0016 | .0016 | **.0028** | .0032 |
| n_tc / n_tc chain | 1880 / 828 | 1885 / 834 | **2168 / 1095** | 2172 / 1097 |

**`SFL10` captures essentially all of the ceiling's gain at a lower fake cost** (dxy[1,5) .1216 vs the
ceiling's .1212 -- slightly BETTER; fake .0028 vs .0032). A delta of 1.0 saturates the lever, so there
is no reason to go further out.

**Paired McNemar, `cube50_highPt`, CTL vs SFL10, 10000 common events -- the gains are enormous and
one-directional:**

| cell | CTL | SFL10 | delta | CTL-only | SFL10-only | p |
|---|---|---|---|---|---|---|
| vxy [1,5) | .3097 | .3226 | +.0129 | 0 | 6 | .031 |
| vxy [5,10) | .2162 | .2351 | +.0189 | **0** | **27** | 1.5e-08 |
| vxy [10,30) | .0421 | .0497 | +.0075 | **0** | **110** | **1.5e-33** |
| dxy [0,1) | .1554 | .1597 | +.0043 | 0 | 5 | .063 |
| dxy [1,5) | .1120 | .1216 | +.0096 | 1 | **52** | 1.2e-14 |
| dxy [5,10) | .0630 | .0762 | +.0132 | **0** | **88** | 6.5e-27 |
| dxy [10,30) | .0085 | .0128 | +.0042 | 1 | **106** | **1.3e-30** |

`CTL-only` is 0 or 1 in every cell: it recovers tracks and loses essentially none. **And cube dup
IMPROVES** (.0186 -> .0152, dup_barrel .0272 -> .0202) -- the extra chain TCs are replacing duplicate
pLS rows, not adding to them.

**The bill, stated as plainly as the win. `SFL10` fails two hard gates as it stands.**

| PU200 tune cell | CTL | SFL10 | delta | CTL-only | SFL10-only | p |
|---|---|---|---|---|---|---|
| overall | .8097 | .8097 | -.0000 | 4 | 1 | .375 |
| dxy [1,5) | .5621 | .5595 | **-.0026** | 12 | 4 | .077 |
| dxy [5,10) | .2502 | .2483 | **-.0020** | 4 | 2 | .69 |
| dxy [10,30) | .0538 | .0519 | **-.0019** | 4 | 1 | .375 |
| vxy [10,30) | .6963 | .6960 | -.0002 | 3 | 2 | 1 |
| **fake overall** | **.0454** | **.0456** | **+.0002** | -- | -- | -- |

1. **`fake_overall` +.0002** violates "PU200 dup/fake not worse" as written (dup is unchanged at .0428).
2. **All four PU200 dxy bands are negative.** No single one is significant (p .077 to .69), and each is
   1-8 net sims, so by the project's own ">.002 or it is noise" rule they are at the boundary. But the
   SIGN IS COHERENT across all four (CTL-only exceeds SFL10-only in every one: 12/4, 4/2, 4/1, 4/2),
   and a coherent small loss is not the same object as noise scatter. I am flagging it as a real,
   small, sub-significant cost rather than rounding it to zero in my own favour.

There is also a genuine tension I cannot resolve from here: **the same change helps displaced cells on
the gun and hurts displaced cells on PU200.** The plausible reading is that the gun's displaced tracks
are high-pT (so the pT>5 bars govern them directly) while PU200's displaced population is mostly low
pT (bars untouched), so PU200 sees only the second-order arbitration/fake cost of the extra supply and
none of the benefit. That is a hypothesis, not a measurement.

**Two targeted arms building now to find a cleaner point on that trade, both cheap:**
- **`SFD10`** -- loosen **only `kWpDisp`** at pT>5, holding `kWpPrompt` at shipped. Every gained cell is
  a displaced cell, so the displaced table is the plausible engine; holding the prompt bar should buy
  the gain at a lower fake cost. With `SFL10` this is also a decomposition, not a guess.
- **`SFL05`** -- delta 0.5, to map whether the PU200 fake/dxy cost falls faster than the cube gain.

If neither clears PU200 fake and the dxy signs, my recommendation stays "ship nothing", but the finding
that the high-pT weld admission is worth **+8% to +51% relative on displaced high-pT with ~zero losses**
belongs on the record either way -- it is a chain-side win in a cell we already lead master 3x in, i.e.
exactly the kind `feedback_no_lst_help_changes` wants, and it is not what this round was commissioned
to look for.

--------------------------------------------------------------------------------------------------

## [SF 03:10] THE DECOMPOSITION IS CLEAN: the entire effect is `kWpDisp`. `SFD10` touches TEN numbers instead of twenty and reproduces `SFL10` cell-for-cell on the cube INCLUDING its fake rate, so the prompt table contributes exactly nothing. Caveat WITH it: `SFD10` still carries the same PU200 bill -- fake +.0002 and a coherent negative across all four dxy bands -- so simplifying the arm did NOT buy the gates back, and my recommendation is still ship nothing.

**`cube50_highPt`, 10000 events, the full trade curve:**

| field | CTL | SFL05 (both, -0.5) | **SFD10 (disp only, -1.0)** | SFL10 (both, -1.0) | SFOPEN (ceiling) |
|---|---|---|---|---|---|
| eff vxy [5,10) | .2162 | .2295 | **.2344** | .2351 | .2358 |
| eff vxy [10,30) | .0421 | .0465 | **.0497** | .0497 | .0499 |
| eff dxy [1,5) | .1120 | .1194 | **.1214** | .1216 | .1212 |
| eff dxy [5,10) | .0630 | .0705 | **.0762** | .0762 | .0765 |
| eff dxy [10,30) | .0085 | .0110 | **.0128** | .0128 | .0129 |
| dup overall | .0186 | .0160 | **.0152** | .0152 | .0152 |
| fake overall | .0016 | .0019 | **.0028** | .0028 | .0032 |
| n_tc chain | 828 | 993 | **1095** | 1095 | 1097 |

`SFD10` equals `SFL10` on every displaced cell, on dup, on **fake**, and on chain TC count -- while
leaving `kWpPrompt` byte-identical to shipped. **The prompt table contributes neither gain nor cost.**
So the honest form of this lever is ten numbers (`kWpDisp[10..19]` and `[30..39]`), not twenty, which
also matters under `feedback_simplicity_over_cleverness`. `SFOPEN` adds nothing over `SFD10` except
fake (.0032 vs .0028), confirming delta 1.0 saturates.

**PU200 tune, the cost side, paired McNemar against CTL:**

| cell | CTL | SFL05 | SFD10 | SFL10 | SFD10 discordant (CTL-only/arm-only), p |
|---|---|---|---|---|---|
| eff overall | .8097 | .8096 | .8097 | .8097 | 4 / 1, p=.375 |
| eff dxy [1,5) | .5621 | .5601 | **.5598** | .5595 | 11 / 4, p=.118 |
| eff dxy [5,10) | .2502 | .2483 | **.2463** | .2483 | 4 / 0, p=.125 |
| eff dxy [10,30) | .0538 | .0526 | **.0519** | .0519 | 4 / 1, p=.375 |
| eff vxy [10,30) | .6963 | .6960 | **.6958** | .6960 | 4 / 2, p=.688 |
| fake overall | .0454 | .0455 | **.0456** | .0456 | -- |
| dup overall | .0428 | .0428 | .0428 | .0428 | -- |

**Every loosening arm fails the same two hard gates**, and simplifying to `kWpDisp` did not change that:
`fake_overall` is worse (+.0001 for `SFL05`, +.0002 for `SFD10`/`SFL10`), and all four PU200 dxy bands
are negative in every arm. `SFL05` is the mildest point (57% of the cube gain for roughly half the
PU200 cost), so the trade is roughly linear and there is no free corner on this curve.

**The one measurement that could still change the verdict, running now.** Every PU200 cost above is
2-11 discordant sims on 1000 events -- individually p=.12 to .69, i.e. AT the project's noise floor,
but with a coherent sign in all four bands, which is what a small real effect and a coincidence look
identically like at this sample size. I have `CTL` and `SFOPEN` running on the **5000-event holdout**
(`event_3000..7000`; `event_2000` is SEALED and structurally excluded by `holdout.sh`). `SFOPEN` is the
COST CEILING as well as the gain ceiling -- it is worse than `SFD10` on PU200 fake and no better on any
dxy band -- so if `SFOPEN`'s displaced bands come back neutral at 5x statistics, the tune-sample
regressions were noise and `SFD10` deserves a real ship discussion; if they come back negative and
significant, every arm in this round is correctly dead and I will say so.

**Provenance for the two new arms.** `SFD10` lib `5c366dc26ba08061ee82e5f3491ef0be`, `SFL05` lib
`53b4c81965c096ef51e2954bf1a9377c`, 0 `error:` in each fresh make log, `bin/lst_cpu` still
`fc06ec27b67b4d3442d382a0fd9eaa5e` for both (weights live in the library), loader-resolved library
verified against the frozen snapshot on all four gate launches. `loosen.py --only disp` asserts
`kWpPrompt` comes out with **0 of 40** literals changed and `kWpDisp` with exactly **20 of 40**, and
re-verified that my earlier `SFL10` header regenerates with every literal identical after the edit
(only the provenance comment differs).

--------------------------------------------------------------------------------------------------

## [SF 03:40] FINAL VERDICT -- SHIP NOTHING. The 5000-event holdout settles both open questions and it settles them against the arms: the prompt high-pT gain is confirmed EXACTLY ZERO (pt[25,50) moves 1 sim vs 1 on 5203), and the PU200 displaced regression is confirmed REAL AND SIGNIFICANT (dxy[5,10) p=.0025, dxy[10,30) p=.00082, vxy[10,30) p=.043). Caveat riding with it: the cube50_highPt displaced GAIN is equally real (p to 1.5e-33), so this lever is a genuine trade -- it moves displaced efficiency from PU200 to the high-pT gun -- and the reason to refuse it is that the hard gate protects PU200, not that the gun result was wrong.

`CTL` vs `SFOPEN` (the ceiling, and also the worst case of the cost) on `event_3000..7000`, 5000 events,
370,888 sims -- five times the tune sample. `event_2000` was never run.

**1. The mission's target is dead, now at 5x statistics.**

| ptband | nSim | CTL | SFOPEN | delta | CTL-only | OPEN-only | p |
|---|---|---|---|---|---|---|---|
| pt [5,7) | 20180 | .9079 | .9078 | -.0001 | 5 | 3 | .73 |
| pt [7,10) | 16077 | .9125 | .9125 | +.0000 | 2 | 2 | 1 |
| pt [10,25) | 21879 | .9084 | .9082 | -.0001 | 6 | 3 | .51 |
| **pt [25,50)** | **5203** | **.9041** | **.9041** | **+.0000** | **1** | **1** | **1** |
| pt [50,inf) | 1903 | .8544 | .8544 | +.0000 | 0 | 0 | 1 |
| pt [5,inf) | 65242 | .9073 | .9072 | -.0001 | 14 | 9 | .41 |
| overall | 356468 | .8447 | .8447 | -.0000 | 18 | 16 | .86 |

Opening every pT>5 weld bar changes the 25-50 GeV band by **one sim in each direction out of 5203**.
Combined with the tune sample that is 6257 sims in the target band and a net movement of zero. The
mission's premise -- that the high-pT working point is costing us prompt efficiency against master --
is now excluded, not merely unsupported.

**2. The PU200 displaced cost was NOT noise. My coherent-sign caution was right and my "individually
insignificant" framing was too generous to my own arms.**

| cell | CTL | SFOPEN | delta | CTL-only | OPEN-only | p (5000 evt) | p was (1000 evt) |
|---|---|---|---|---|---|---|---|
| eff dxy [5,10) | .2683 | .2652 | **-.0032** | **21** | **5** | **.0025** | .69 |
| eff dxy [10,30) | .0494 | .0468 | **-.0026** | **27** | **7** | **.00082** | .375 |
| eff vxy [10,30) | .7035 | .7030 | -.0005 | 18 | 7 | **.043** | 1 |
| eff dxy [1,5) | .5792 | .5790 | -.0001 | 37 | 35 | .91 | .077 |
| eff overall | .8118 | .8118 | -.0000 | 18 | 16 | .86 | .375 |
| fake overall | .0453 | .0455 | **+.0002** | -- | -- | -- | -- |

Three PU200 displaced cells regress significantly. `dxy[10,30)` loses 27 tracks and gains 7. These are
the crown jewels the project protects in every round, and `feedback_tuning_priority` puts displaced
efficiency above everything this arm could offer. **Two hard gates fail on the decisive sample, not at
the noise floor.** Note also that `dxy[1,5)` -- the cell that looked worst on the tune sample
(-.0026, p=.077) -- comes back neutral at 5x (37/35, p=.91), which is a clean reminder that a 1000-event
displaced band cannot rank these arms at all, in either direction.

**3. What the lever actually is, stated plainly.** It moves displaced efficiency BETWEEN SAMPLES: +8% to
+51% relative on `cube50_highPt` (p to 1.5e-33, 0-1 losses per cell) and -1.2% to -5.3% relative on
PU200 displaced (p to .00082). The mechanism consistent with every number I have: the gun's displaced
tracks are high-pT, so the pT>5 bars govern them directly and extra weld supply converts to found
tracks; PU200's displaced population is mostly LOW pT, so its bars never moved and it sees only the
second-order cost of the extra high-pT chain TCs -- which win arbitration and claim hits against
displaced TCs that were previously delivered. That is a hypothesis about arbitration, consistent with
`n_tc` rising while PU200 displaced efficiency falls, and I have not isolated it.

**FINAL RECOMMENDATION: none of the six arms ships.**

| arm | prompt high-pT (the mission) | PU200 displaced | PU200 fake | cube50_highPt displaced | verdict |
|---|---|---|---|---|---|
| `SF2W` (2-way regroup) | 0 (2 tracks lost total) | neutral | neutral | neutral/+.0004 | **no-op, reject** |
| `SF3W`, `SFPOOL` | not built | -- | -- | -- | **not worth building** (SF2W + ceiling exclude them) |
| `SFL05` (-0.5 both) | 0 | -.0013 to -.0020 | +.0001 | +8% to +29% rel | **reject: fails fake + displaced** |
| `SFD10` (-1.0 disp only) | 0 | -.0019 to -.0040 | +.0002 | +8% to +51% rel | **reject: same gates, best-formed of the arms** |
| `SFL10` (-1.0 both) | 0 | -.0019 to -.0026 | +.0002 | +8% to +51% rel | **reject: dominated by SFD10 (2x the numbers, same result)** |
| `SFOPEN` (ceiling) | **0 at 5x stats** | **-.0026 to -.0032, p<.003** | +.0002 | +8% to +52% rel | **diagnostic only, never a candidate** |

**What I would hand forward, and what I would not.** Do NOT commission another edge working-point round
for prompt high-pT: the ceiling arm excludes it at 5000 events, and the mechanism post shows the
25-50 GeV losses are pixel-seeded (35/35 carried by master's pT5/pLS/pT3) with **no overlapping TC of
any type** in our output -- objects no outer-tracker weld bar can create. The live lead is the
**pT5/pT3/pLS supply at high pT**, which is pLS-side and which I did not touch. The `kWpDisp` high-pT
finding is worth keeping as a characterised, banked result: it is a real, huge, chain-side displaced
gain on a gun where we already lead master 3x, currently priced at three significant PU200 displaced
cells. If anyone wants it, the thing to attack is that PU200 arbitration cost, not the bar.

--------------------------------------------------------------------------------------------------

## [SF 03:55] CLOSE-OUT: what I ran, what I did NOT run, and the five banked patches

**Every run in this round** (22 judged runs; all CPU, the deterministic backend; every one launched from
a frozen binary snapshot with the loader-resolved `liblst_cpu.so` md5 verified against that snapshot):

| arm | lib md5 | PU200 tune | jets 0-499 | cube50 | cube50_highPt | PU200 holdout 5000 |
|---|---|---|---|---|---|---|
| `CTL` (= shipped) | `e7a11d8115ca59a7d53a26db5ea8c7d5` | yes | yes | yes | yes (-s 4) | **yes** |
| `SF2W` | `1abd10b5d6fd51baf1ecd08789fe60ae` | yes | yes | yes | yes (-s 4) | no |
| `SFL10` | `c6a2354ece2fa871ab563598183d0038` | yes | yes | yes | yes (-s 4) | no |
| `SFOPEN` | `35453bcc0e3cab1a4273b5551ea11bdd` | yes | yes | yes | yes (-s 4) | **yes** |
| `SFD10` | `5c366dc26ba08061ee82e5f3491ef0be` | yes | no | no | yes (-s 4) | no |
| `SFL05` | `53b4c81965c096ef51e2954bf1a9377c` | yes | no | no | yes (**-s 2**) | no |

**Gaps I am declaring rather than hiding.** `SFD10` and `SFL05` did NOT get the jet or `cube50` gates,
and only `SFOPEN` joined `CTL` on the holdout. That is defensible only because `SFOPEN` is the CEILING
of both the gain and the cost of every loosening arm -- it is strictly more permissive than `SFD10`,
`SFL10` and `SFL05`, and it is the arm with the worst PU200 fake and no better dxy in any cell -- so its
holdout failure condemns them all without each needing its own 5000-event run. If anyone wants to
revive `SFD10` (the best-formed arm) it needs its own jet gate, `cube50` gate and holdout before any
ship discussion. `SF3W` and `SFPOOL`, the two regrouping arms in my design table, were never built:
`SF2W` deploying to 2 tracks plus the ceiling arm moving the target band by 0 excludes them, and
building them would have been spending compute to re-derive a known null.

**`SFL05`'s cube50_highPt run used `-s 2`, not `-s 4`.** It segfaulted at `-s 4` (`cube50_highPt` writer,
the known failure) and `gates.sh` retried down automatically: `SFL05 cubehi SEGFAULT at -s 4, retrying
lower` then `SFL05 cubehi ok at -s 2`. The completed run has `RUN_EXIT=0`, `n_evt = 10000` and
`n_sim_denom = 87` -- identical event and denominator counts to every other arm's `-s 4` run -- and
stream count is proven physics-bit-identical on this sample, so the arm is compared on the same events.
Flagging it because a differing `-s` in a comparison table is exactly the kind of thing that should
never be silent.

**Five patches banked, all `git apply --check` CLEAN against a tree literally detached at
`04c6e122e68`, each touching ONLY `RecoTracker/LSTCore/src/alpaka/EdgeNetworkWeights.h`:**

| patch | md5 | what it is |
|---|---|---|
| `patch/SF_2way.patch` | `fd18902e5693eacb020a2695d4c1440a` | 2-way |eta| regroup of the pT>5 rows (= my parked patch, byte-identical) |
| `patch/SF_L05.patch` | `ba5d1040dfedb2a1cdcd59bd7f775947` | both tables, pT>5 rows -0.5 |
| `patch/SF_D10.patch` | `15f3386d0401d0821ffaaff52e4dae4a` | **`kWpDisp` only, pT>5 rows -1.0 -- the best-formed arm, 10 numbers** |
| `patch/SF_LOOSE10.patch` | `eb0bbb2aa574b44b8205d617921cea0a` | both tables, pT>5 rows -1.0 (dominated by `SF_D10`) |
| `patch/SF_OPEN.patch` | `ab05edc34024b2110c5ba2ddbb4b48fe` | diagnostic ceiling, NOT a candidate |

**I recommend shipping none of them.** They are banked so the measurement is reproducible and so the
`kWpDisp` finding is not lost, not because any is a candidate.

**Housekeeping.** `gpu_wt/sf2` is restored to a pristine `04c6e122e68`: no modified or deleted tracked
files, edge header back to `05b7f3f379aa5fa285538eba5b8b4d8a`. The main tree was never built in and its
tracked files are clean. No process of mine is still running. I committed nothing, pushed nothing and
published nothing. Footprint 1.9 GB (`sf_work` 722 MB + worktree 1.2 GB); `/mnt/data1` has 200 GB free.

**Three traps found this round that outlive it**, repeated here so they are in one place:
1. `p4_ref/paired_rle.py` **cannot be used against LST master's ntuple** -- it keys our side by
   `(run,lumi,evt)` and master's by content-hash, then pairs positionally, reporting master's
   `eff_dxy_0_1` as .0903 instead of .8297. Use `sf_work/effpt2.py` (forces one key on both sides).
2. `cube50_highPt dxy[1,5) = .1209` is a **5000-event** number; the same binary gives **.1120** over
   10000. State the event count with the gate or arms will be killed for a phantom -.0089.
3. A 1000-event PU200 displaced band **cannot rank these arms in either direction**: `dxy[1,5)` looked
   like the worst cell at 1000 events (p=.077) and came back neutral at 5000 (p=.91), while
   `dxy[5,10)`/`dxy[10,30)` looked like noise (p=.69/.375) and came back significant (p=.0025/.00082).
