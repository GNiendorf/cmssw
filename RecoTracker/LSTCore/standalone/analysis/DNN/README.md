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

The **parity checks are the important ones for a reviewer**: each loads the trained model, recomputes
the logits in Python, and compares against the hand-written C++ inference in `src/alpaka/`. They are
what makes the committed weight headers verifiable rather than trusted.

## What is not in this repository

These scripts document the method; they are **not runnable end to end from a fresh checkout**:

* The **dumps** they train on are produced by running the binary with the env vars above. They are
  large and are not committed.
* The **trained checkpoints** (`.pt`) and their **normalisation JSONs** are not committed, so the
  exporters need `MODEL_DIR` pointed at wherever those live, and the export self-tests skip
  without it.
* `train_edge.py` builds a single-logit head while `export_edge_weights.py` expects the deployed
  3-output form and a working-point table that nothing here produces -- the two are **not currently
  plug-compatible**, and reconciling them is outstanding work.
* No pair-dump writer exists in the committed C++, so `label_attach.py` -> `train_attach.py` has no
  in-tree way to produce its input.

Each script's docstring repeats the specific inputs it needs.
