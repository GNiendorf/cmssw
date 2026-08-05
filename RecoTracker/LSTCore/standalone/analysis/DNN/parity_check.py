#!/usr/bin/env python3
"""Python-side parity reference for the C++ edge-MLP inference port.

Loads the trained model + norm json, recomputes LOGITS (pre-sigmoid, the quantity
EdgeInference.cc writes into EdgeScores.logOdds) for the first N edges of the M2 smoke
dump, reading the feature columns exactly as train_edge.py assembles its input matrix
(ni_00..ni_12, no_00..no_12, ef_00..ef_13), applying any "conditioning" spec from the
norm json (v1: none) then standardization.

Output: parity_ref.json — a flat JSON object mapping edge index (tree row, as string)
to logit. The C++ side runs the same events and compares runEdgeInference output.

Re-run against v2 (one command):
  python3 parity_check.py --model edge_mlp_v2.pt --norm edge_norm_v2.json
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

    # Same branch order as train_edge.py's load_dump.
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

    # Conditioning (norm json spec; v1 has none) then standardization — the exact
    # train_edge.py pipeline and the exact math baked into edge_mlp_weights.h.
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
