#!/usr/bin/env python3
"""Numerical parity check between the Python edge head and its generated C++ header.

The deployed edge scorer (src/alpaka/EdgeNetworkWeights.h, used by the edge inference in
ChainEdges.h) is a transcription of a trained PyTorch model, so the two implementations can drift
apart without anything failing to compile. This script produces the Python side of the comparison:
it loads the checkpoint and its norm json, applies the same conditioning and standardization the
trainer applied, runs the forward pass, and writes the resulting LOGITS (pre-sigmoid, the quantity
the C++ stores as ChainEdgesSoA::logOdds) for the first --n-edges rows of an edge feature dump.
The C++ side scores the same rows and compares them one by one. A disagreement larger than float32
rounding means the two are not the same function -- a wrong literal in the header, a different
feature column order, or conditioning applied in a different order -- and not a physics
disagreement.

Feature columns are read in the order the trainer assembles its input matrix: ni_00..ni_12 (inner
node), no_00..no_12 (outer node), ef_00..ef_13 (edge). That width, 2 * N_NODE_FEAT + N_EDGE_FEAT,
is asserted against the norm json.

Inputs -- NONE of these live in this repository, they have to be supplied from wherever training
and the instrumented run wrote them:
  --model  a PyTorch checkpoint (.pt) holding "state_dict" for an n_in -> 32 -> 32 -> 1 Sequential
           and optionally "feature_names", which is asserted to match the norm json.
  --norm   a JSON file with "feature_names", "mean", "std", and an optional "conditioning" list of
           {"feature", "op"} entries; the supported ops are "clip" (with "lo"/"hi") and "log10_1p",
           and an unrecognised op raises rather than being skipped.
  --input  a ROOT file with an "edges" tree carrying the branches above. The edge rows come from a
           run of the instrumented binary, which appends its per-event edge sidecars when
           LST_CHAIN_EDGE_DUMP and LST_CHAIN_FEAT_DUMP name output files (single stream only: the
           sidecars key on event order). Converting those binary sidecars into this ROOT tree is
           not part of this repository either.

Output: --out, a flat JSON object mapping edge index (tree row, as a string) to logit, plus a
summary line of the logit distribution on stdout.

Run:
  python3 edge_parity.py --model <head>.pt --norm <norm>.json --input <edges>.root \
      --out parity_ref.json [--n-edges 2000]
"""

import argparse
import json
import os
import sys

import numpy as np

PROTO_DIR = os.path.dirname(os.path.abspath(__file__))

N_NODE_FEAT = 13
N_EDGE_FEAT = 14


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", default=os.path.join(PROTO_DIR, "edge_mlp_v1.pt"))
    p.add_argument("--norm", default=os.path.join(PROTO_DIR, "edge_norm_v1.json"))
    p.add_argument("--input", default=os.path.join(PROTO_DIR, "edges_smoke.root"))
    p.add_argument("--out", default=os.path.join(PROTO_DIR, "parity_ref.json"))
    p.add_argument("--n-edges", type=int, default=2000)
    return p.parse_args()


def main():
    args = parse_args()
    import torch
    import uproot

    with open(args.norm) as fh:
        norm = json.load(fh)
    names = norm["feature_names"]
    mu = np.asarray(norm["mean"], dtype=np.float32)
    sd = np.asarray(norm["std"], dtype=np.float32)
    n_in = len(names)
    assert n_in == 2 * N_NODE_FEAT + N_EDGE_FEAT == len(mu) == len(sd)

    # Same branch order the trainer uses to build its input matrix.
    feat_branches = ([f"ni_{i:02d}" for i in range(N_NODE_FEAT)]
                     + [f"no_{i:02d}" for i in range(N_NODE_FEAT)]
                     + [f"ef_{i:02d}" for i in range(N_EDGE_FEAT)])

    tree = uproot.open(args.input)["edges"]
    n = min(args.n_edges, tree.num_entries)
    arr = tree.arrays(feat_branches, entry_stop=n, library="np")
    X = np.empty((n, n_in), dtype=np.float32)
    for j, b in enumerate(feat_branches):
        X[:, j] = arr[b]
    print(f"loaded {n} edges x {n_in} features from {args.input}")

    # Conditioning as the norm json specifies it, then standardization: the same order the trainer
    # applies and the same order baked into EdgeNetworkWeights.h. The order is not interchangeable,
    # since for a log-scaled input the clip bounds and the mean/std are measured in log space.
    for c in norm.get("conditioning") or []:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        elif c["op"] == "log10_1p":
            X[:, j] = np.log10(1.0 + X[:, j])
        else:
            raise ValueError(f"unknown conditioning op {c['op']}")
        print(f"conditioned {c['feature']}: {c['op']}")
    Xs = (X - mu) / sd

    try:
        blob = torch.load(args.model, map_location="cpu")
    except Exception:
        blob = torch.load(args.model, map_location="cpu", weights_only=False)
    assert blob.get("feature_names", names) == names, "model/norm feature_names disagree"
    model = torch.nn.Sequential(torch.nn.Linear(n_in, 32), torch.nn.ReLU(),
                                torch.nn.Linear(32, 32), torch.nn.ReLU(),
                                torch.nn.Linear(32, 1))
    model.load_state_dict(blob["state_dict"])
    model.eval()

    with torch.no_grad():
        logits = model(torch.tensor(np.ascontiguousarray(Xs))).squeeze(1)
    # CMSSW torch build lacks numpy interop (.numpy() raises); tolist is fine here.
    vals = logits.tolist()

    ref = {str(i): float(v) for i, v in enumerate(vals)}
    with open(args.out, "w") as fh:
        json.dump(ref, fh, indent=0)
    v = np.asarray(vals)
    print(f"wrote {args.out}: {n} logits, min={v.min():.4f} max={v.max():.4f} "
          f"mean={v.mean():.4f} frac(logit>0)={float((v > 0).mean()):.4f}")
    print(f"first 5 logits: {[round(x, 6) for x in vals[:5]]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
