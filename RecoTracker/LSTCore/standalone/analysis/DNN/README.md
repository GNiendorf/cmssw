# Training the chain-tracking networks

The chain pipeline uses **three** networks. They are not independent: they run in sequence, and
each one's decisions change the population the next one sees. Training them correctly is therefore
a *loop*, not three separate jobs, and the order matters. This file documents that process,
because it is the part that cannot be reconstructed from the code.

| network | shape | file | what it decides |
|---|---|---|---|
| edge head | 40 -> 32 -> 32 -> 3 | `src/alpaka/EdgeNetworkWeights.h` | which triplet pairs are weld candidates |
| chain gate | 25 -> 32 -> 32 -> 3 | `src/alpaka/ChainNetworkWeights.h` | whether a welded chain is fake / prompt-true / displaced-true |
| attach head | 22 -> 24 -> 24 -> 1 | `src/alpaka/AttachNetworkWeights.h` | which pixel seed, if any, belongs to a chain |

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

`barfit_attach.py` fits the attach delivery bars that live in `interface/ChainConfig.h`.
`pair_dump_io.py` and `join_dump_io.py` read the two attach sidecars; nothing outside this
directory is imported.

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
* Row widths come from the dump header (`nFeat`, `nProbe`). Do NOT reuse any splice that hardcodes
  a width: `kAttachFeatures` is 22, and a hardcoded 20-wide splice drops one column and shifts eight
  others **with no error**. `pair_dump_io.py` rejects a format version it does not know rather than
  guessing.

Each script's docstring repeats the specific inputs it needs.
