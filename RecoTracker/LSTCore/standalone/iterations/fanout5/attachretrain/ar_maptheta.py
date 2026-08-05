#!/usr/bin/env python3
"""ar_maptheta.py --from <v> --from-cut C --to <v> [...] -- translate an -a value
between two heads by MATCHED SELECTIVITY on the chain universe.

Each head has its own logit scale, so a numeric -a is not portable. What IS portable is
"how many chain pairs does this cut accept". This reads a pair-dump chunk, scores its
CHAIN-target rows (ttype 0) with both checkpoints, finds the acceptance fraction the
source cut produces, and reports the target head's cut at the same acceptance -- so a
scan can be centred correctly on the first try instead of after a wasted batch.
"""
import argparse
import json
import os

import numpy as np

P = os.path.dirname(os.path.abspath(__file__))


def load_head(v):
    import torch
    with open(f"{P}/attach_norm_{v}.json") as fh:
        norm = json.load(fh)
    blob = torch.load(f"{P}/attach_mlp_{v}.pt", map_location="cpu", weights_only=False)
    return norm, blob


def score(v, X_raw, names):
    import torch
    norm, blob = load_head(v)
    assert norm["feature_names"] == names
    X = X_raw.copy()
    for c in norm.get("conditioning") or []:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        elif c["op"] == "log10_1p":
            X[:, j] = np.log10(1.0 + X[:, j])
    X = (X - np.asarray(norm["mean"], np.float32)) / np.asarray(norm["std"], np.float32)
    a = blob["arch"]
    m = torch.nn.Sequential(torch.nn.Linear(a[0], a[1]), torch.nn.ReLU(),
                            torch.nn.Linear(a[1], a[2]), torch.nn.ReLU(),
                            torch.nn.Linear(a[2], a[3]))
    m.load_state_dict(blob["state_dict"])
    m.eval()
    out = np.empty(len(X), np.float32)
    with torch.no_grad():
        for i in range(0, len(X), 1 << 19):
            z = m(torch.tensor(np.ascontiguousarray(X[i:i + (1 << 19)])))
            out[i:i + (1 << 19)] = np.asarray(z.reshape(-1).tolist(), np.float32)
    return out


ap = argparse.ArgumentParser()
ap.add_argument("--from", dest="src", required=True)
ap.add_argument("--from-cut", type=float, nargs="+", required=True)
ap.add_argument("--to", dest="dst", required=True)
ap.add_argument("--input", default=f"{P}/dump/pr_c00.root")
args = ap.parse_args()

import uproot

t = uproot.open(args.input)["pairs"]
names = [f"af_{n}" for n in
         uproot.open(args.input)["feature_spec"].member("fTitle")[3:].split(",")]
br = [f"af_{i:02d}" for i in range(len(names))]
arr = t.arrays(br + ["ttype"], library="np")
ch = arr["ttype"] == 0
X = np.empty((int(ch.sum()), len(names)), np.float32)
for j, b in enumerate(br):
    X[:, j] = arr[b][ch]
print(f"{len(X)} chain-target pairs from {os.path.basename(args.input)}")

s_src = score(args.src, X, names)
s_dst = score(args.dst, X, names)
print(f"\n{'src cut':>9} {'accept frac':>12} {'-> equivalent ' + args.dst + ' cut':>28}")
for c in args.src_cut if hasattr(args, "src_cut") else args.from_cut:
    frac = float((s_src >= c).mean())
    eq = float(np.quantile(s_dst, 1.0 - frac)) if 0 < frac < 1 else float("nan")
    print(f"{c:>9.2f} {frac:>12.6f} {eq:>28.2f}")
