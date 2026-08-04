# Sample inventory

Short reference for which ntuple is which. All are PU200RelVal, `--allobj`, `-d` build
(CUT_VALUE_DEBUG), `-p 0.8`. Truth side for all of them:
`/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/`

| file | evts | size | notes |
|---|---|---|---|
| `LSTNtuple_PU200RelVal_300evt.root` | 300 | 6.1 GB | The M0-M19 frozen benchmark. Every pre-2026-08-03 number is measured on this. |
| `LSTNtuple_PU200RelVal_1000evt.root` | 498 | 10.1 GB | Salvage from a 1000-evt run that hung. **349 of its events do NOT overlap the 300** -> 649 unique across both, which is what the M8-onward retrains used ("M8 combination rule"), and the 349 are the true out-of-sample set used at M18b. |
| `rebase_ref/LSTNtuple_instr_977evt.root` | 977 | 19.9 GB | **2026-08-03, the current sample.** Same config PLUS the dup-flag instrumentation. 23 events missing (`rebase_ref/missing_events.txt`). A frozen 300-evt subset is carved from it for fast iteration (`rebase_ref/frozen300_keys.txt`). |

## Why the new sample exists (do not substitute an older one)

Only the 977 carries LST's **algorithmic** dup flags, so it is **the only sample from which
the post-deletion state can be reconstructed**. The older ones have just the truth-level
`pLS_isDuplicate`, which is a different quantity. Anything measured or trained against the
post-deletion baseline must use the 977.

New branches in it: `pLS_isDupAlgSelf` / `Pass2` / `Final` (three snapshots because
`CrossCleanpLS` overwrites the self-cleaning bitmask), `pLS_score`, `pT3_isDupAlgSelf` /
`Final`, `pT5_isDupAlg`, `tc_hitIdx`, `tc_hitType`, and `tc_plsIdx` extended to types 5 and 7.
Verified by three closure identities that reproduce LST's own TC counts exactly.

## Derived scoreboard references

`prototype/base300_hists.root` (LST side of the 300), `prototype/base60_hists.root` (the
frozen test-60 subset), `prototype/freeze_verify_hists.root` (the M19 freeze),
`fanout4/compose_attach/base_oos349_hists.root` (the 349 true-OOS events).

## Known issues

- **`-d` builds emit garbage in ~12 `ls_*` cut-value branches on `ls_isPLS` rows** - those
  columns are never written for pixel pseudo-segments, so the ntuple dumps uninitialised
  memory (`nan`, denormals). Pre-existing, reproduces when the same binary is run twice.
  Ignore those branches; they are not physics.
- **Generation stalls** on large runs: `matchedSimTrkIdxsAndFracs`
  (`code/core/trkCore.cc:461-473`) builds the full cartesian product of per-hit sim
  candidates inside an `omp critical`, so nearly all threads park on the lock. This is the
  historical "two-hour hang". Workaround: many chunks (`-j N -I i`, which partitions by
  read index modulo N - verified disjoint), not many streams. `-s 64` buys almost no
  parallelism for `--allobj` (load ~1.1 per job). Real fix: the wanted quantity is an
  O(nHits x nCandidates) count, not a product.
