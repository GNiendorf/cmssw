# Training the chain-tracking networks

The chain pipeline uses **three** networks. They are not independent: they run in sequence, and
each one's decisions change the population the next one sees. Training them correctly is therefore
a *loop*, not three separate jobs, and the order matters. This file documents that process,
because it is the part that cannot be reconstructed from the code.

| network | shape | file | what it decides |
|---|---|---|---|
| edge head | 40 -> 32 -> 32 -> 3 | `src/alpaka/EdgeNetworkWeights.h` | which triplet pairs are weld candidates |
| chain gate | 25 -> 32 -> 32 -> 3 | `src/alpaka/ChainNetworkWeights.h` | whether a welded chain is fake / prompt-true / displaced-true |
| attach head | 23 -> 24 -> 24 -> 1 | `src/alpaka/AttachNetworkWeights.h` | which pixel seed, if any, belongs to a chain (input 22 = master's tracklet closure dBeta) |

## Why the order matters

Each head has a **working point** -- a bar its score must clear. That bar decides which objects
survive to the next stage. So:

* Retrain the edge head, and a different set of edges becomes weld-eligible.
* Different edges mean different chains get built, so the chain gate now sees a **different input
  distribution** than the one it was trained on.
* The gate's own bars then decide which chains are accepted, which changes the set of targets the
  attach head is offered.

A head trained on rows produced by a *previous* version of an upstream head is **off-policy**: it
has been fitted to a distribution that no longer occurs. This has bitten this project before --
a gate was once trained on rows dumped by a binary whose edge head was not the one it shipped
alongside.

## The loop

All the training rows can be emitted from **one run of one binary**. The dump sidecars are
env-gated and inert unless set:

    LST_CHAIN_EDGE_DUMP     edge rows        (with LST_CHAIN_FEAT_DUMP for the raw edge features)
    LST_CHAIN_NODE_DUMP     node rows
    LST_CHAIN_CHAIN_DUMP    chain rows
    LST_CHAIN_TC_DUMP       track-candidate rows
    LST_CHAIN_PAIR_DUMP     attach (target, seed) rows -- the attach head's training rows
    LST_CHAIN_JOIN_DUMP     the truth-join keys the pair rows deliberately do not carry

The pair dump takes two more knobs: `LST_CHAIN_PAIR_CAP` (device rows per event, default 1e6; a
non-zero `nDrop` in the header means the event was truncated, so raise it) and
`LST_CHAIN_PAIR_DSB` (stage-B keep factor, default 16 -- stage A is always kept whole). Its byte
layout is `analysis/DNN/PAIRDUMP_FORMAT.md`; take the row width from the header, never
from a constant in a script.

Per round, in **topological order, re-dumping between every stage**:

    1. dump edges  with the current binary   -> retrain the EDGE head
       -> re-derive the edge working point at fixed signal efficiency   -> REBUILD
    2. dump chains with THAT binary          -> retrain the CHAIN GATE
       -> re-derive the gate working points, prompt and displaced SEPARATELY -> REBUILD
    3. dump pairs  with THAT binary          -> retrain the ATTACH head
       -> re-derive the delivery bars at fixed signal efficiency        -> REBUILD

**Re-dumping between stages is required, not an optimisation.** Skipping a rebuild means the next
head is trained against the old working point, which is exactly the off-policy failure above.

Iterate rounds until a round is a no-op -- the retrain barely moves the metrics. Two or three
rounds is typical. If the metrics oscillate instead of settling, stop: that is a result worth
reporting, not something to average away.

## What each shipped head was actually trained on

**The three heads were fitted on THREE DIFFERENT sample mixes.** This is recorded here because it
is not discoverable from the scripts: the trainers take arbitrary corpora via `--lab NAME=dir`, so
the sample choice lives in the invocation, and the only durable record is the saved `args` inside
the `.pt`.

| head | model | corpora | jet share |
|---|---|---|---|
| edge | `edge_G_pinned.pt` | **PU200 only** | -- |
| chain gate | `chain3_A02.pt` | **PU200 + jets + cube5_highPt** | .25 jets, .02 gun (flat per-row) |
| attach | `ATJ25.pt` | **PU200 + jets** | .25 |

`cube50_highPt` is **never** trained on. It is a pure gate, which is what makes "the cube samples
are bit-identical" a meaningful statement when it appears in a findings file. The enrichment gun,
where one is used, is `cube5_highPt` -- a corpus that is 99.8% true, which is why a 2% loss share
is worth anything against 7M PU200 rows.

To check any model for yourself, rather than trusting this table:

```python
import torch
d = torch.load("models/<head>.pt", map_location="cpu", weights_only=False)
print(d["args"]["lab"], d["args"].get("share"))
```

### KNOWN ISSUE: the corpora are inconsistent and nobody has justified it

The heads run in sequence and each one's working point changes the population the next one sees --
that is the whole reason the on-policy loop above exists. Yet the **edge** head, which runs FIRST
and whose bar decides which edges exist for everything downstream, was trained with **no jet rows
and no gun rows at all**, while the gate immediately downstream of it saw both.

That asymmetry looks like drift across rounds rather than a decision. It is an open item: either
normalise the corpora across the three heads, or write down why each one differs. **Until that is
resolved, retraining a single head will silently fit it to a different population than its
neighbours, and the resulting weights will differ from the shipped ones for reasons that have
nothing to do with your change.** If you retrain, reproduce the corpus in the table above first
and confirm you can regenerate the shipped weights before changing anything.

## Working points

Calibrate at **fixed per-bin signal efficiency**, on the same pt x eta binning LST's T5 DNN uses
(2 pt bins split at 5 GeV, 10 bins of 0.25 in |eta| with the last absorbing everything above 2.5).

Holding signal efficiency fixed per bin is what isolates head quality: it pins what the head keeps
and lets only the background rate move. Matching a *mixed* quantity instead -- pair acceptance
rate, or per-target conversion rate -- has failed twice here, once costing 7.8 points of conversion
and once doubling the barrel duplicate rate.

The gate's prompt and displaced working points are **separate tables**. Do not collapse the three
gate logits into one hand-derived scalar for calibration: `max(zPrompt, zDisp)` discards which
class won, and that quantity measures *worse than random* at separating displaced from prompt.

## Provenance

Every dump records the git hash of the binary that produced it **and** the hash of every weight
header in that build. A training run whose dump provenance does not match the binary it will be
deployed in is invalid and its result must be discarded. This single rule is what prevents the
off-policy bug class described above.

## The scripts here

Per network, a trainer, an exporter and a parity check:

| network | train | export to header | parity |
|---|---|---|---|
| edge | `train_edge.py` | `export_edge_weights.py` | `edge_parity.py` |
| chain gate | `train_chain.py` (on `train_chain_base.py`) | `export_chain_weights.py` | `chain_parity.py` |
| attach | `train_attach.py` (rows from `label_attach.py`) | `export_attach_weights.py` | `attach_parity.py` |

`barfit_attach.py` re-derives the attach bars at matched per-cell SIGNAL ACCEPTANCE;
`barfit_attach_fpr.py` re-derives the four DELIVERY bars at matched per-cell FAKE rate instead --
the tool that fitted the < 5 GeV delivery row (it spends head improvements on acceptance, the axis
this project ranks first). `attach_bar_cells.py` produces the per-cell (2 pt rows x 10 |eta| bins)
occupancy, current acceptance/FPR and true-pair quantile ladders over the labelled corpora -- the
groundwork behind the 3-row delivery bar TABLE. All write values for `interface/ChainConfig.h`.

### How the 3-row delivery bar table was derived (exact provenance)

The table in ChainConfig.h (low / high / displaced rows x 3 eta bands, split at seed ptIn = 5 GeV
and target dcaXY = dcaSplit) was set as follows; every number is reproducible from these steps:

1. **Per-cell groundwork**: `python3 attach_bar_cells.py` over the labelled corpora (the pair-dump
   pipeline above), giving cell occupancy and the bar-for-acceptance ladders. This showed the
   >= 5 GeV cells sitting at .55-.78 true-pair acceptance in barrel/transition under the low-row
   bars (endcap already .90+), and 4-layer margins ABOVE 5-layer ones (no per-length row needed).
2. **High row**: a deployed env sweep (`LST_D_AHI_DELTA` at 0.5/1/2/3/5, edge 5) on sealed PU200
   holdout, recon HP, FINDINGS_GAPS.md `# [HP]`: the pT5 share saturates at a relaxation of 3
   logits, long before any fake price (nothing degrades until 8), and the endcap is the one cell a
   uniform relaxation harms -- hence barrel/transition -3, endcap unchanged.
3. **Low row**: the same sweep form (`LST_D_A_DELTA=x` with `LST_D_AHI_DELTA=-x` cancelling the
   high row) at x = 0.5/1/1.5/2/2.5/3: share rises .749 -> .853 with fake flat and dup falling;
   1.5 chosen as the knee with the displaced guard in place.
4. **Displaced row**: frozen at the matched-FPR (`barfit_attach_fpr.py`) values. Every relaxation
   was measured to leak vxy-displaced sims one-directionally via wrong-seed dilution of the 75%
   matcher; routing pairs whose TARGET has reconstructed dcaXY >= dcaSplit to the unrelaxed bars
   closed every dxy band completely. The residual (radially-emitted displaced, small fitted dca)
   is not taggable by any sweep-time observable tested and is priced in the ship record.

No simulation information enters any of these decisions at runtime: the rows are selected on the
seed's reconstructed ptIn and the chain's reconstructed dcaXY only.
`pair_dump_io.py` and `join_dump_io.py` read the two attach sidecars; nothing outside this
directory is imported.

### How the 6 x 3 delivery WP table was derived (exact provenance)

`attachThetaTable` in ChainConfig.h (6 seed-ptIn rows, edges 2/5/10/25/50, x the 3 seed-|eta|
bands) REPLACED the previous 2-row prompt structure. One-sentence scheme: **every cell's bar is
the logit at which 99 percent of that cell's true stage-A prompt pairs still deliver** -- fixed
per-cell signal efficiency, one global target, raw quantiles, no floors and no inheritance from
the previous constants. The displaced-target row is a separate mechanism and was not refit.
Exact reproduction:

1. **On-policy dump** from the deployed binary (single stream, provenance-hashed):
   `bash chain_clean/dumprun.sh pu1000B -i .../event_1000.root -n 1000 -p 0.8 -w 1`
2. **Extract + truth-join** the stage-A pairs (`extract2.py <dumpdir> <out.npz>`): per pair the
   deployed logit, seed ptIn/|eta|, target-chain dcaXY (prompt selection), and the truth label
   through the `label_attach.py` join. `validate.py` re-checks the offline delivery replay
   against the run's own TC output (must reproduce type-7 vs type-4 at ~1.0000) before any fit
   is trusted.
3. **Fit** (`fitbars.py <extract.npz> <outprefix>`): scans the efficiency target over a grid,
   replays delivery with each candidate table, and reports share vs master, the wrong-seed
   delivery count (the dilution proxy), and the per-cell order-statistic index k. The chosen
   point (0.99) is the loosest target at which every cell's bar is still a genuine order
   statistic (k >= 2) -- beyond it the thin high-pT cells' bars become the single lowest true
   pair, which is overfitting. 1/2/3-zone targets were scanned; all optimise to the same single
   value, so ONE global target is shipped. `census_disp.py` is the disposition census
   ([DISP]/[DISP2] in FINDINGS_GAPS.md) that motivated the table; `report2.py` renders the
   final tables.

The bars are quantiles of the DEPLOYED head's own logits: retraining the attach head invalidates
all 18 and this chain must be rerun from step 1.

The **parity checks are the important ones for a reviewer**: each loads the trained model, recomputes
the logits in Python, and compares against the hand-written C++ inference in `src/alpaka/`. They are
what makes the committed weight headers verifiable rather than trusted.

## Retraining the attach head, end to end

The commands below are the whole path. `$S` is the standalone directory, `$W` a scratch directory
for the dumps and corpora. Budget roughly 25 GB of dump and 27 GB of labelled columns per 1000
PU200 events, and note the dumps can be deleted once labelled.

**1. Dump, SINGLE STREAM.** `-s 1` is not a performance choice: the sidecars use a sequential
per-event counter, so record *i* is entry *i* only for one stream. Dump from a binary whose weight
headers are the ones that will be deployed, or the rows are off-policy.

```bash
export LST_CHAIN_PAIR_DUMP=$W/pu/pairs.bin LST_CHAIN_JOIN_DUMP=$W/pu/join.bin \
       LST_CHAIN_CHAIN_DUMP=$W/pu/chains.bin LST_CHAIN_PAIR_CAP=2000000
lst_cpu -i <ntuple>.root -n 1000 -p 0.8 -s 1 -w 1 -o $W/pu/run.root
```

**2. Count the rows, then label.** The labeller memory-maps its output at fixed length, so the
total must be known first. `--rows` fails loudly if any event dropped rows.

```bash
N=$(python3 pair_dump_io.py --rows $W/pu/pairs.bin)
python3 label_attach.py $W/pu/chains.bin $W/pu/join.bin $W/pu/pairs.bin $W/lab/pu 1000 $N <ntuple>.root
```

Repeat both steps per corpus (the shipped attach head used PU200 plus jets; see the table above).
The labeller reads truth from the ORIGINAL tracking ntuple, not from `run.root` -- no `--allobj`
and no `-d` build flag are needed for any of this.

**3. Train**, with the corpora mixed by loss share:

```bash
python3 train_attach.py --lab pu=$W/lab/pu --lab jet=$W/lab/jet --share jet=0.25 \
    --arm scalar --epochs 200 --hidden 24 --lr 3e-3 --sched cos --out $W/models/NEW.pt
```

**4. Export, check parity, refit the bars, rebuild:**

```bash
python3 export_attach_weights.py $W/models/NEW.pt > ../../src/alpaka/AttachNetworkWeights.h
python3 attach_parity.py $W/models/NEW.pt      # must pass before the header is trusted
python3 barfit_attach.py $W/lab/pu $W/lab/jet  # delivery bars -> interface/ChainConfig.h
```

Then rebuild and measure **paired, on a sealed holdout**, never on the events the head was tuned on.

## What is not in this repository

The **attach** path above is runnable from a fresh checkout given a tracking ntuple. The rest is
not, and these are the gaps:

* The **dumps** are produced by running the binary with the env vars above. They are large and are
  not committed.
* The **trained checkpoints** (`.pt`) and their **normalisation JSONs** are not committed, so the
  exporters need `MODEL_DIR` pointed at wherever those live, and the export self-tests skip
  without it.
* `train_edge.py` builds a single-logit head while `export_edge_weights.py` expects the deployed
  3-output form and a working-point table that nothing here produces -- the two are **not currently
  plug-compatible**, and reconciling them is outstanding work.
* **The WP-TABLE FITTERS for the edge and gate heads are NOT in this directory.** The 2x10 tables
  deployed in `EdgeNetworkWeights.h` (weld bars) and `ChainNetworkWeights.h` (kWpPrompt/kWpDisp)
  were fitted by round-era scripts that never moved here. Until they do, retraining either head
  leaves you without an in-tree way to re-derive its tables -- the same failure class the attach
  pair dump had before it was restored. Porting them is owed.
* Row widths come from the dump header (`nFeat`, `nProbe`). Do NOT reuse any splice that hardcodes
  a width: `kAttachFeatures` is 22, and a hardcoded 20-wide splice drops one column and shifts eight
  others **with no error**. `pair_dump_io.py` rejects a format version it does not know rather than
  guessing.

Each script's docstring repeats the specific inputs it needs.

## Retraining the attach head ON THE CURRENT (23-input, rescue-aware) STACK -- [ARM-RETRAIN]

Four things in the sections above went stale between rounds. They are corrected here, and every
correction is a change to a script in this directory, not a note.

**1. The head is 23 inputs wide and the pair dump carries NO probe columns.** `af_dBeta` was
promoted out of the dump's probe array into `kAttachFeatures`, so a current dump reports
`nFeat 23 nProbe 0` and its `x` already carries dBeta standardized with the deployed constants,
exactly like every other verbatim slot. Consequences: `--use-probe` (the flag the shipped DBETA
head was trained with, from a 22+1 dump) must NOT be used on a current corpus; `train_attach.py`
now takes the width **from the corpus** and asserts the corpora agree; the norm-json template for
a 23-wide fit is `attach_norm23_template.json`, IN this directory, so the path has no external
dependency. Check any dump with `python3 ../../at_ref/nrows.py <pairs.bin>` before trusting a
width.

**2. Stage tag 3 exists and it is a CHAIN target.** The claim-rescue arm tags its claim-rejected
chain targets `3` (`ChainAttach.h`: `row.stage = is4L ? 2u : (isRescue ? 3u : 0u)`). Both
`label_attach.py` and `mk_tdca.py` selected chain targets with `(st == 0) | (st == 2)`, which does
not raise on a stage-3 row -- it falls through with an empty sim set, i.e. **a silent label of 0 on
every rescue pair, true ones included**. Both are fixed here and both now assert on an unknown
tag. On a PU200 dump with `LST_CHAIN_RESCUE=2` the stage-3 rows are 5.5% of the corpus and 2% of
them are true, so the bug was worth about a hundred thousand mislabelled positives per 1000 events.

**3. Which upstream state a pair dump is on-policy against, measured rather than assumed.** The
attach DELIVERY BARS do not change the pair rows at all: comparing a dump taken under the 3-row
bar table against one taken under the 6x3 table on the same 1000 events, the stage-0 and stage-1
row counts are equal **per event** and the rows are bit-identical -- same (stage, target, pLS)
keys and the same logits to the last bit, over the events checked. The reason is structural: the
dump is written pre-verdict, and both attach stages enumerate and score their pairs before any
seed ownership is resolved. So a bar retune does NOT invalidate a pair corpus; only a change to
the head (the logit column), to anything upstream of the chains, or to the TARGET SET does. The
rescue arm is the last of those: it adds stage-3 rows and changes nothing else. A dump for a
retrain should therefore be taken with the rescue setting the arm will deploy with.

**4. `attach_parity.py` cannot pass and cannot gate anything.** It hardcodes `N_ATTACH_FEAT = 18`
and reads a ROOT `pairs` tree that nothing writes. `attach_parity2.py` replaces it with three
checks over artefacts the pipeline actually produces: generated header vs norm json (the
preprocessing constants), generated header vs checkpoint (the weights, honouring the transposed
storage), and -- on a dump taken with that header DEPLOYED -- the compiled C++ logit column against
a numpy forward pass on the dump's own standardized rows. The third check is the one that tests
the deployed inference rather than Python against itself; the first is what stops it passing
vacuously, since the dump's `x` is standardized by the C++ before it is written.

### The per-cell k floor on the 6x3 WP fit

`fitbars.py` gained `wp_bars_kfloor` and a `scanK` alongside the global-epsilon `scan1`. The
shipped rule -- "the loosest global epsilon at which every one of the 18 cells still has order
statistic index k >= 2" -- is a statement about the THINNEST cell (204 true pairs), and it holds
back cells with four orders of magnitude more statistics. Since k indexes a cell's OWN sorted
true-pair logits, the guard belongs per cell: `k[cell] = max(2, floor((1 - eps_max) * n[cell]))`.
Every bar remains a raw order statistic of its own cell, no cell is set by an extremum, and the
cells that carry the deficit (low pT, outside the barrel -- the fattest in the table) are free to
run at the target. `fitbars.py` also takes an optional third argument: a JSON of the 18 bars the
DUMPING BINARY ACTUALLY HAD, so the "x shipped" dilution ratios are measured against the deployed
table rather than against the pre-table constants it still reconstructs by default.

### Runners (in `standalone/retrain_ref/`, outside this package)

`lockbuild.sh` / `deploy_rt.sh` build under the round's `BUILD.lock` and stash the binary, and
`deploy_rt.sh` installs an arm's header and bar table, builds, stashes, then restores the shared
tree byte for byte inside the same lock, so a shared-tree round cannot inherit one arm's weights.
`dumprun_rt.sh` dumps from a stashed binary and records the HEAD hash plus the sha1 of the whole
`git diff` (the round's tree is dirty by design, so a bare hash would be a lie). `settable.py`
rewrites `attachThetaTable` from the fit's own JSON, transposing eta-major to the header's
pt-major ONCE in one place -- the mis-transcribed `attachThetaHiE` constant [DISP2] found is
exactly what hand-transposing 18 numbers costs.

## THE SHIPPED STATE (endcap-share round, 2026-08-16): head CTRL + the pure eps-0.99 table

What is deployed in this commit, and the one-line rule for each piece:

* **Head**: `AttachNetworkWeights.h` = the CTRL retrain (23 inputs, control arm -- the low-pT
  weighted variant won offline and LOST deployed, the fifth recorded offline-proxy
  misprediction). Model + norm: `chain_clean/models/CTRL.pt`, `CTRL_norm.json` (models are not
  committed; the header is, and `attach_parity2.py` ties them together).
* **Delivery WP table**: `attachThetaTable` = raw per-cell quantiles of THIS head's logits at a
  single global signal efficiency of 0.99, fitted on the pooled PU+jets corpus at the 0.25 jets
  loss share. NO cap, NO floor, NO inheritance: every value is an order statistic of its own
  cell (thinnest cell k = 28). A "2x-background cap" variant was measured and REJECTED as
  indefensible (it inherits 18 per-cell anchors from the previous operating point); fit rules
  must be self-contained.
* **Seed evidence in the claim** (`ChainAfirst.h` + hook): gate-alive >= 5-layer chains are
  scored against the pixel seeds BEFORE the greedy claim; "own argmax clears own delivery bar"
  is one term in the claim's order key. Master's pT5-before-exclusivity ordering in our
  architecture.
* **Claim rescue + same-seed handover** (`ChainAttach.h`/`ChainArbitrate.h`): a claim-rejected
  strictly-longer chain whose best seed IS its shorter sibling's granted seed takes the hits and
  the seed. Share-neutral under the evidence ordering, kept for track length (+~30k 15-hit
  tracks / 1000 evt).

Exact reproduction of the table (after any head retrain, ALL 18 values must be re-derived):

    # 1. dump on-policy, rescue on, from the deployed binary (single stream):
    bash chain_clean/dumprun.sh <TAG> -i .../event_1000.root -n 1000 -p 0.8 -w 1     # PU
    bash chain_clean/dumprun.sh <TAGJ> -i .../trackingNtuple_jets_1000.root -n 500 -p 0.8 -w 1 -J
    # 2. label + extract (label_attach.py / extract2.py as documented above)
    # 3. fit: python3 fitbars.py <extract_pooled.npz> <out>   -> pure eps scan; take eps=0.99
    # 4. bake: python3 ../../retrain_ref/settable.py <fit.json>  (single transposition point)
    # 5. verify: attach_parity2.py (all three checks) BEFORE any physics run.

Deployed result (event_2000 @0.8 + jets, vs shipped B and master): outside-barrel pT5 share
.8847 -> .9186 (57% of the B->master gap), barrel closed, eff at/above B and master, cubes 0/0,
pT5 mean nhits preserved, jet-core dR<.005 share .47 -> .76. Priced costs (headline, accepted at
ship time): PU vxy[10,30) -33/3000evt and dxy[1,5) -13 (leads over master remain >= +250); jets
eff -.0066 and jets vxy[10,30) -444 (lead remains large); CPU +4.8% single-stream (evidence-pass
score reuse is the named, unbuilt optimization). Full round record: standalone/FINDINGS_GAPS.md
sections [EC-COORD] .. [ARM-RETRAIN2] in the measurement tree.
