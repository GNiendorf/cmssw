# P1 INSTRUMENT + RE-BASELINE -- HANDOFF (2026-08-03)

Everything below is DONE unless marked OPEN. Artifacts live in `standalone/rebase_ref/`.

---
## 0. THE FILES YOU NEED

| what | path |
|---|---|
| merged instrumented sample, 977 evts, 19.9 GB | `standalone/rebase_ref/LSTNtuple_instr_977evt.root` |
| FROZEN 300-evt iteration subset (first 300 entries of the merged file) | `standalone/rebase_ref/LSTNtuple_instr_300evt.root` |
| its 300 `(run lumi evt)` keys, in order | `standalone/rebase_ref/frozen300_keys.txt` |
| the 22 chunk files it was merged from | `standalone/rebase_ref/gen/` |
| the 1000-event reference read order `(idx run lumi evt)` | `standalone/rebase_ref/read_order_1000_keys.txt` |
| the 23 events MISSING from the sample | `standalone/rebase_ref/missing_events.txt` |
| prototype copy used for the measurement | `standalone/protoBASE/` (binary `protoBASE/bin/chainproto`) |
| scoreboard | `standalone/rebase_ref/rb_scoreboard.txt`, per-run JSON `r_<TAG>.json` |
| seed accounting | `standalone/rebase_ref/seed_stats_300.txt` |
| pinned control binaries + libs | `rebase_ref/bin_pristine/`, `rebase_ref/bin_instr/` (each has MD5) |

## 1. COMMANDS THAT REPRODUCE THE NUMBERS

```bash
# environment (standing rule)
pushd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone \
  && source setup.sh >/dev/null 2>&1 && cmsenv && source setup.sh >/dev/null 2>&1

# the whole measurement set (identity + 6 hybrid runs + table), ~15 min
bash rebase_ref/rb_all.sh

# one configuration by hand (frozen P25BASE line is inside rb_run.sh)
bash rebase_ref/rb_run.sh POSTDEL    -ZPF 3 -ZP5 1 -RT3 1 -T3E 0 -ZP8 5   # MEASURED pass-1
bash rebase_ref/rb_run.sh POSTDELP2  -ZPF 3 -ZP5 1 -RT3 1 -T3E 0 -ZP8 6   # MEASURED pass-1+2

# current LST on the same events (this is the `base` column of every compare_ab.py call)
protoBASE/bin/chainproto -m identity -i rebase_ref/LSTNtuple_instr_300evt.root \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -n -1 -o rebase_ref/rb_base300.root
createPerfNumDenHists -i rebase_ref/rb_base300.root -o rebase_ref/rb_base300_hists.root

# scoreboard table
python3 rebase_ref/rb_tab.py RBBASE NOZP8 MODELF3 MODELP2 POSTDEL POSTDELP2

# seed accounting + the three closure identities
python3 rebase_ref/seed_stats.py rebase_ref/LSTNtuple_instr_300evt.root

# regenerate more events (see section 6 for the stall)
bash rebase_ref/gen_round.sh 0 1        # watchdog included
python3 rebase_ref/verify_chunks.py     # disjointness + coverage, writes missing_read_indices.txt
python3 rebase_ref/carve300.py          # re-carve the frozen subset
```

## 2. WHAT WAS CHANGED, FILE BY FILE

### `src/alpaka/LSTEvent.h`
Six public `std::vector<char>` members, bookkeeping only, nothing reads them back:
`plsIsDupSelf_`, `plsIsDupPass2_`, `plsIsDupFinal_`, `pt3IsDupSelf_`, `pt3IsDupFinal_`,
`pt5IsDup_`.

### `src/alpaka/LSTEvent.dev.cc`
* `dupSnapshotsEnabled()` -- env gate `LST_DUP_SNAPSHOTS`, DEFAULT OFF (anonymous namespace,
  next to `chainSkipDoomedEnabled`).
* `snapshotByteColumn(queue, column, n, out)` -- one device->host copy of a 1-byte SoA column.
* Six call sites, each `if (dupSnapshotsEnabled())`:
  | member | taken right after | why it cannot be recovered later |
  |---|---|---|
  | `plsIsDupSelf_`  | `CheckHitspLS` pass 1, end of `pixelLineSegmentCleaning()` | pass 2 ORs bit 2 |
  | `plsIsDupPass2_` | `CheckHitspLS` pass 2, in `createTrackCandidates()` | `CrossCleanpLS` writes `= 1` |
  | `plsIsDupFinal_` | `CrossCleanpLS`, before `AddpLSasTrackCandidate` | admission state |
  | `pt3IsDupSelf_`  | `RemoveDupPixelTripletsFromMap` | `CrossCleanpT3` overwrites |
  | `pt3IsDupFinal_` | `CrossCleanpT3` | admission state |
  | `pt5IsDup_`      | `RemoveDupPixelQuintupletsFromMap` | (already final) |

### `standalone/bin/lst.cc`
After the `--t4` block: `if (ana.pls_branches || ana.pt3_branches || ana.pt5_branches)
setenv("LST_DUP_SNAPSHOTS", "1", 1);` -- so `--allobj` turns the snapshots on and nothing
else does. CMSSW never sets it.

### `standalone/code/core/write_lst_ntuple.cc` -- 9 NEW branches, 1 CHANGED
NEW: `pLS_isDupAlgSelf`, `pLS_isDupAlgPass2`, `pLS_isDupAlgFinal` (raw `char` bitmask as
`int`; `-999` means "not recorded"), `pLS_score`, `pT3_isDupAlgSelf`, `pT3_isDupAlgFinal`,
`pT5_isDupAlg`, `tc_hitIdx`, `tc_hitType` (`vector<vector<int>>`, from
`getHitIdxsAndHitTypesFromTC`, i.e. `candsBase.hitIndices()`; HitType 0 = Pixel,
4 = Phase2OT).
CHANGED: `tc_plsIdx` is now filled for type 7 (via `objectIndices()[tc][0]`, which IS the
global pLS index) and type 5 (via `pixelTriplets.pixelSegmentIndices()[pT3row]`), not just
type 8. Chain rows are excluded.
`t3_partOfT5` needed NO work -- it already existed at writer:154/2806 and is populated.

### `standalone/protoBASE/` (COPY of `protoAUDIT` = `prototype` + the audit's flags)
* `EventData.h`: `pLS_isDupAlgSelf/Pass2/Final` (`vector<int>`), `pLS_score` (`vector<float>`).
* `NtupleReader.cc`: `bindVecOptional()` + `LST_OPT_VF` / `LST_OPT_VI` X-macro lists, so
  protoBASE STILL READS PRE-INSTRUMENT NTUPLES (fields just stay empty).
* `main.cc`: TWO NEW `-ZP8` modes.
  `-ZP8 5` = post-deletion bare-seed universe from MEASURED `pLS_isDupAlgSelf == 0`.
  `-ZP8 6` = same from MEASURED `pLS_isDupAlgPass2 == 0`.
  Both abort loudly if the branches are absent. Modes 1-4 (the audit's models) unchanged.

## 3. VERIFICATION (all passed)

**Library pinning.** `ldd bin/lst_cpu` shows `liblst_cpu.so => not found` without
`LD_LIBRARY_PATH`: there is NO RPATH, so an executable-only comparison is vacuous. Both
control runs were made with their own pinned `lst_cpu` + `liblst_cpu.so` + `librooutil.so`
(MD5s in `bin_pristine/MD5` and `bin_instr/MD5`; the two `liblst_cpu.so` differ,
`librooutil.so` is identical). A re-run out of `bin_pristine/` with `LD_LIBRARY_PATH`
prepended resolved the pinned library, confirmed by `ldd`.

**Bit-identity**, 5 evts, `--allobj -n 5 -s 1`, `rebase_ref/cmp_branches.py`:
* pristine vs instrumented: **460 IDENTICAL, 13 DIFFER, 0 MISSING, 9 ADDED**
* pristine vs pristine (same binary twice): **461 IDENTICAL, 12 DIFFER**
The same 12 `ls_*` cut-value branches differ in BOTH comparisons -> they are PRE-EXISTING
run-to-run nondeterminism (uninitialised `CUT_VALUE_DEBUG` fields; `ls_mdIdx*` and every
other LS branch are identical, so the LS collection itself is stable). Net: the instrument
changes exactly ONE pre-existing branch, `tc_plsIdx`, which is the intended change.

**Closure identities** -- these are what actually pin each snapshot to its kernel boundary.
Exact on all 300 frozen events:
```
isQuad && pLS_isDupAlgFinal == 0   ==  n(tc_type 8)     TRUE
pT5_isDupAlg == 0                  ==  n(tc_type 7)     TRUE
pT3_isDupAlgFinal == 0             ==  n(tc_type 5)     TRUE
monotone Self <= Pass2 <= Final; value hists {0,1} / {0,1,2} / {0,1,2}
```
**New-branch correctness**: `tc_plsIdx` type 7 == `pT5_plsIdx` (3846 rows, 0 mismatches);
type 5 == `pT3_plsIdx` (708, 0); type 8 always >= 0 and always an `isQuad` seed;
`len(tc_hitIdx) == tc_nhits` for every row.

**The brief's arithmetic did NOT close, and that is the headline.** See section 4.

## 4. THE NUMBERS (frozen 300-evt subset)

Seed universe, per event:
```
nPLS 22527  quad pLS 17095
admitted today (isQuad && Final==0)          894.5   == n(tc_type 8)
post-deletion, pass 1 only (Self==0)        1951.7   x2.18
post-deletion, pass 1 + pass 2 (Pass2==0)   1569.1   x1.75
```
The audit MODELLED 943/evt against 743 admitted (x1.27). The measurement is **x2.18**, i.e.
the union-find family model UNDERCOUNTS the exposed seed universe by about a factor 2.
WHY the model failed: pass 1 is a PAIRWISE `|=`, not a per-family election. In a connected
component every LOCALLY best seed survives, so a component contributes as many survivors as
it has local minima -- not one.

`Final - Pass2` = 674.6/evt of newly flagged seeds (brief expected ~958). This is mechanical,
not an error: `CrossCleanpLS` skips rows whose `isDup` is already nonzero, so it can only
newly flag seeds that survived BOTH self-cleaning passes. It is identically equal to
`pass2KeepQ - finalKeepQ`.

pT3 side, per event: 585.8 built -> 151.7 survive `RemoveDupPixelTripletsFromMap` -> 151.5
survive `CrossCleanpT3`. **`CrossCleanpT3` removes 0.26 pT3/evt -- it is essentially inert.**
So the 586 -> 152 reduction is pT3-vs-pT3 self dedup, which SURVIVES deletion. The
pT5-dependence of the carried pT3 set is therefore entirely in the BUILDER skips
(`PixelTriplet.h:721/:770`), which no flag can measure -- the second ntuple with those skips
disabled is still needed.

## 5. OPEN / NEXT

* The frozen 300-evt subset and the 977-evt merged sample are ready for the P2 fan-out.
* Still needed per the plan: the SECOND 1000-evt ntuple from a build with the
  `PixelTriplet.h:721/:770` pT5 skips disabled.
* `prototype/base300_hists.root` is the OLD sample's LST reference; the equivalent for the
  new subset is `rebase_ref/rb_base300_hists.root`.

## 6. THE GENERATION STALL -- ROOT CAUSED, ACT ON THIS

`matchedSimTrkIdxsAndFracs`, `standalone/code/core/trkCore.cc:461-473`. The `perm()` lambda
materialises the FULL CARTESIAN PRODUCT of the per-hit sim-candidate lists (`k^nHits`
`vector<int>`s) purely to find, for each combination, the largest count of a single sim
index. On dense events this is unbounded work: gdb showed 63 of 64 threads parked in
`gomp_mutex_lock_slow` at `bin/lst.cc:565` (the `omp critical` ntuple-write block) with the
lock holder inside that recursion, output file untouched for 35 min. THIS IS THE "two hour
hang" OF THE EARLIER 1000-EVENT ATTEMPT. It is pre-existing writer code, untouched here.

**It is removable.** `max over permutations of (count of sim u)` is just "how many hit
positions have `u` in their candidate list" -- an O(nHits x nCandidates) loop, no enumeration.
Fixing that makes 1000-event generation routine. NOT done here (it would change writer
behaviour right before a baseline freeze).

Two further facts for whoever regenerates:
* `-s 64` buys almost NO parallelism for an `--allobj` write: the fill sits in
  `omp critical`, so ~1 thread runs and load per job is ~1.1. Run MANY chunks concurrently,
  not many streams.
* Killing a stalled job loses its ENTIRE file (ROOT never writes the TTree). Use SMALL
  `-j` shares so one bad event costs little.

`-j`/`-I` VERIFIED EMPIRICALLY (n=20, j=2): disjoint, union == full set, counts sum.
`-n N` caps events READ, so `-n 1000 -j 4` gives 4 x 250 distinct events.
Partition rule (measured): `-I k` of `-j N` selects read indices `n` with `n % N == k-1`.

### Missing events: 23 of 1000 (sample is 977)
`rebase_ref/missing_events.txt` (read_index run lumi evt). They are exactly the shares of
the three sub-chunks killed last:
* `-j 64 -I 59`  -> read idx 58, 122, 186, 250, 314, 378, 442, 506, 570, 634, 698, 762, 826, 890, 954
* `-j 256 -I 176` -> read idx 175, 431, 687, 943
* `-j 256 -I 120` -> read idx 119, 375, 631, 887

### The pathological events
Chunk c0 (`-j 4 -I 0`, read idx `n % 4 == 3`) wedged after 101 events. Two independent
re-splits of its share both stalled again, localising the culprits to:
* **read index 119 = run 1 lumi 88 evt 8719**
* **read index 175 = run 1 lumi 88 evt 8776**
Both are in c0's share and both are the FIRST members of their (repeatedly stalling)
sub-partitions, so c0 wedged on one of these two. A third pathological event sits in the
`-j 64 -I 59` share (first dispatched members: read idx 58, 122, 186, 250). Reproduce a
single event with `-j 1000 -I <read_index+1>`.

---
## 7. THE SCOREBOARD -- frozen 300-evt subset of the merged 977-evt sample

```
tag                   eff    vxy01      v15     v510    v1030      d15     d510    d1030      dup     fake      nhB      nhT      nhE      nTC
RBBASE            0.80917  0.84225  0.79167  0.73524  0.74219  0.61873  0.20161  0.03073  0.05193  0.04585  9.89358  9.88903  3.46317   611645
NOZP8             0.77109  0.80207  0.76608  0.72850  0.74139  0.61650  0.20161  0.03073  0.05285  0.04846 10.47580 10.24738  3.47026   577342
MODELF3           0.80732  0.83978  0.79678  0.73187  0.74139  0.61650  0.20161  0.03073  0.21127  0.04985  8.46070  8.73038  3.17906   664158
MODELP2           0.80648  0.83906  0.79094  0.73187  0.74139  0.61650  0.20161  0.03073  0.20366  0.04822  8.46398  8.73985  3.22031   656012
POSTDEL           0.80882  0.84125  0.80044  0.73356  0.74139  0.61650  0.20161  0.03073  0.39326  0.04504  8.46403  8.70414  2.52806   758249
POSTDELP2         0.80626  0.83887  0.79167  0.73187  0.74139  0.61650  0.20161  0.03073  0.20553  0.04630  8.46885  8.74966  3.23588   655866
LST(base)         0.80988  0.84296  0.77193  0.65430  0.66453  0.55407  0.20968  0.05674  0.05179  0.04476 10.14804 10.01546  3.56248   608190
```
RBBASE   = frozen P25BASE line, no deletion flags (calibration)
LST(base)= CURRENT LST on the same 300 events (`-m identity`)
NOZP8    = deletion WITHOUT any bare-seed change (isolates the pT3-class loss)
MODELF3  = the audit's union-find family model, `-ZP8 3`, on THESE events
MODELP2  = the audit's pass-2 model,             `-ZP8 4`, on THESE events
POSTDEL  = MEASURED pass-1 universe,             `-ZP8 5`   <-- the brief's definition
POSTDELP2= MEASURED pass-1 + pass-2 universe,    `-ZP8 6`

Bare-seed accounting per event (quad pLS 17095.5, already delivered 887.4):
```
-ZP8 5 (measured pass1)     blocked by our contention 461.2   ADDED type-8 TCs 603.0
-ZP8 6 (measured pass1+2)   blocked 419.9                     ADDED 261.7
-ZP8 3 (family model)       blocked 417.3                     ADDED 289.4
-ZP8 4 (pass-2 model)       blocked 414.5                     ADDED 262.2
```

## 8. POSTDEL vs THE AUDIT'S MODELLED .21093 -- THE VERDICT

The sample change is NOT the story: the audit's own model re-run on these events
(MODELF3) gives dup **.21127** against its published **.21093** -- a .0003 shift. So the
model and the measurement are directly comparable, and:

| quantity | audit MODEL | MEASURED | ratio |
|---|---|---|---|
| post-deletion bare-seed universe | ~943 /evt | **1951.7 /evt** | **x2.07** |
| post-deletion duplicate rate (pass 1 only) | .21093 | **.39326** | **x1.86** |

**The model under-predicted the damage by nearly a factor two.** Post-deletion duplicate
rate is 7.6x today's .05193, not 4.1x. Efficiency is still flat (.80882 vs .80917) and
fake actually improves (.04504 vs .04585) -- the added rows are almost purely duplicates,
exactly the shape the audit described, just twice as large.

**THE ONE THING THAT CHANGES THE PLAN.** The audit assumed "post-deletion only CheckHitspLS
PASS 1 survives (pass 2 and CrossCleanpLS both die)". Pass 2 does NOT have to die. Its
kernel (`Kernels.h:786`, `secondpass=true`, launched at `LSTEvent.dev.cc` in
`createTrackCandidates`) takes only `modules`, `segmentsOccupancy`, `pixelSeeds` and
`pixelSegments` -- NOTHING in the deletion set. Keeping it is free, and it is worth:
```
dup  .39326 (pass 1 only)  ->  .20553 (pass 1 + pass 2)     -.188
nTC  758249                ->  655866
eff  .80882                ->  .80626                       -.0026
```
i.e. keeping pass 2 removes HALF the post-deletion duplicate damage for 0.0026 of
efficiency. **POSTDELP2 (.20553), not POSTDEL (.39326), should be the reference the next
round works against, and "keep CheckHitspLS pass 2" should be an explicit design decision
rather than an assumed casualty.** Note MODELP2 (.20366) was accidentally close to the
truth -- the audit's mode-4 "any shared pixel hit" edge happens to approximate pass 2 well;
the family model for pass 1 does not.

Secondary: NOZP8 shows the pT3-class deletion alone costs eff .80917 -> .77109 (-.0381),
reproducing the audit's ROFF (-.0387) on a different sample.
