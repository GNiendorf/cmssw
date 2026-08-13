# JET ROUND 4 -- SHARED FINDINGS FILE

**Baseline HEAD: `a5cebaaaafc` on `chain_tracking_proto`** (physics identical to the four-arm ship
`2840340b6f1`; `a5cebaaaafc` only adds the JPR3 findings file).

**THE ROUND'S GOAL: JET-CORE EFFICIENCY.** Then fake. Duplicates are a COST COLUMN, not a mission.
Master's ~0 jet duplicate rate is an artifact of its low jet efficiency and is not a target.

**READ FIRST:** `FINDINGS_JETPHYS3.md` (the recon this round is built on). Do not re-derive its
funnel, its reach ladder, or its closed list.

---

## THE ONE-PARAGRAPH STATE OF THE PROBLEM

We deliver **.8142** of jet-core sims on the tune corpus (.8087 sealed holdout); LST master is at
.7807 and **leads in no jet cell that matters**, so master is no longer information -- judge against
the REACH ceiling. The weld already BUILDS a chain for 1,974 of the 3,659 core sims we miss, so
"deliver what we already build" = **.9144**, ten points and 4.4 core sims/event away, and it is
exactly stages e (gate, 303) + f (claim, 1,411) + g (75% purity, 260). JPR3's discovery: the
coordinate that governs the loss is **distance to the nearest other selected track (dRnn)**, not
distance to the jet axis. 57.5% of everything we still lose has another selected sim within
dR .005. At dRnn >= .02 efficiency is FLAT (.926/.922/.932) regardless of axis distance; at axis
dR >= .05 it runs .720 -> .932 across separation. In that row **weld reach is flat (.85-.88) while
delivery runs .749 -> .967** -- we BUILD the chain and then DISCARD it, and a neighbour decides.
**The frontier is a two-real-tracks-one-hit-set arbitration contest at high pt in close pairs.**

## THE FAKE BUDGET (jets only)

| | MASTER | OURS | headroom |
|---|---:|---:|---:|
| fake TCs/evt, dR < .02 | 7.58 | 1.98 | 5.60 |
| fake TCs/evt, dR < .05 | 13.95 | 5.62 | **8.33** |
| fake TCs/evt, pooled | 27.69 | 14.39 | **13.30** |

**We can spend ~2.65 admitted fake TCs per recovered core sim and still be cleaner than master on
jets.** This budget exists on JETS ONLY. PU200 has none.

---

## HARD RULES FOR EVERY AGENT THIS ROUND

1. **SEALED**: jet events **500-999** (`trackingNtuple_jets_1000.root`) and PU200 **`event_2000`**.
   Nobody opens them. The coordinator judges candidates there.
   Tune halves: jets events **0-499**, PU200 **`event_1000`**.
2. **NO TIMING.** This is a physics round. Do not run the broker, do not claim a run window, do not
   report ms/evt. Nobody gets a run-hold.
3. **NO CUBE IN TRAINING.** The cube samples (`cube50`, `cube50_highPt`) are ARTIFICIAL displaced
   gun samples used to TEST displaced behaviour. They are a magnifying glass, never a training
   target. If your arm retrains any head, cube rows are OUT unless you can show the same gain on
   **PU200's own displaced bands** (`dxy[10,30)`, `vxy[10,30)`), which is the honest transfer test.
   Rationale: cube is nearly empty and PU200/jets are dense, so any occupancy-derived input is a
   near-perfect SAMPLE FINGERPRINT; a network given one can partition its function by sample and
   the cube gains become a lookup that will not follow to a displaced track in a real dense event.
4. **CROWDING ENTERS AS A BAR, NOT A RANK, AND NOT AS A FREE NETWORK INPUT.** The safe construction
   is `T4C1`'s: a ramp that is **identically zero at and below a knee** the cube samples sit
   entirely below, so cube behaviour is invariant BY CONSTRUCTION, then VERIFIED bit-identical.
   `T4C1` cleared this: both cubes bit-identical to the ship binary, all 21 judge fields including
   `n_tc`, 0 discordant sims across 12 paired cells, full 10,000-event `cube50_highPt`.
   Density as a RANK term measured **-.17** and is on the closed list. Do not re-open it.
5. **BOTH CUBES ARE A GATE ON EVERY CANDIDATE.** Report `cube50` and `cube50_highPt`. Bit-identical
   is the target; any movement must be a GAIN and must be priced.
6. **PU200 IS A CONSTRAINT, NOT A TARGET, THIS ROUND** -- but the crown jewels still bind: overall
   efficiency, the four `dxy` bands and three `vxy` bands. A jet gain that costs displaced
   efficiency is not a candidate.
7. **DUPLICATES**: report the deep-core cell (dR<.005) and dR<.05 as a COST COLUMN. Do not
   commission work to close them. Do not let an arm blow them up without a reason.
8. **MEASURE UNIONS, NEVER SUM.** If your arm touches a stage another agent touches, say so here
   before you build.
9. **NO PUBLISHING.** No hosted pages, no artifacts of any kind. Plots are local PNGs under
   `standalone/<your>_ref/`, paths quoted in this file.
10. **ALL FILES under `standalone/<your>_ref/`. NOTHING in `/tmp`.** Disk is at 94% (216 GB free) --
    clean your ROOT files as you go.
11. **POST TO THIS FILE**, append-only, with a `## [AGENT hh:mm] HEADLINE` line stating the RESULT,
    not the activity. Caveats ride WITH the headline, never as fine print. This file is the
    cross-agent channel; do not expect coordinator messages.
12. **A candidate is: a patch (with md5), the exact commands to reproduce it, all gate numbers on
    the TUNE halves, and an honest statement of what you did not measure.** Nothing is shipped by
    an agent. The coordinator judges on the sealed holdouts.

## BUILD

```bash
pushd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone \
  && source setup.sh && cmsenv && source setup.sh && <cmd>
```
- `cmsenv` is an ALIAS -- in a script use `eval $(scramv1 runtime -sh)` then RE-SOURCE `setup.sh`.
- `lst_make_tracklooper` prints "compilation successful" even when a TU fails. Grep the FRESH
  `.make.log.<timestamp>` for `error:` **with the colon**.
- `set -u` breaks under `setup.sh`.
- Your own build area is assigned in your brief. Do not build in the main tree.

---
