#!/usr/bin/env python3
"""Python-side parity reference for the C++ K8 attach-head MLP inference port.

Loads the trained attach-head model + norm json, recomputes LOGITS (pre-sigmoid, the
quantity attachLogit returns) for the first N pairs of the pair dump, reading
af_00..af_17 exactly as train_attach.py assembles its input matrix, applying the
"conditioning" spec from the norm json then standardization.

Output: attach_parity_ref.json -- a flat JSON object mapping pair index (tree row,
as string) to logit. The C++ side reads the same rows and compares attachLogit.

  python3 attach_parity.py --model attach_mlp_v1.pt --norm attach_norm_v1.json
"""

import argparse
import json
import os
import sys

import numpy as np

PROTO_DIR = os.path.dirname(os.path.abspath(__file__))

N_ATTACH_FEAT = 18


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", default=os.path.join(PROTO_DIR, "attach_mlp_v1.pt"))
    p.add_argument("--norm", default=os.path.join(PROTO_DIR, "attach_norm_v1.json"))
    p.add_argument("--input", default=os.path.join(PROTO_DIR, "pairs_300evt.root"))
    p.add_argument("--out", default=os.path.join(PROTO_DIR, "attach_parity_ref.json"))
    p.add_argument("--n-pairs", type=int, default=1000)
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
    assert n_in == N_ATTACH_FEAT == len(mu) == len(sd)

    feat_branches = [f"af_{i:02d}" for i in range(N_ATTACH_FEAT)]
    tree = uproot.open(args.input)["pairs"]
    n = min(args.n_pairs, tree.num_entries)
    arr = tree.arrays(feat_branches, entry_stop=n, library="np")
    X = np.empty((n, n_in), dtype=np.float32)
    for j, b in enumerate(feat_branches):
        X[:, j] = arr[b]
    print(f"loaded {n} pairs x {n_in} features from {args.input}")

    # Conditioning (norm json spec) then standardization -- the exact train_attach.py
    # pipeline and the exact math baked into attach_mlp_weights.h.
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
    model = torch.nn.Sequential(torch.nn.Linear(n_in, 24), torch.nn.ReLU(),
                                torch.nn.Linear(24, 24), torch.nn.ReLU(),
                                torch.nn.Linear(24, 1))
    model.load_state_dict(blob["state_dict"])
    model.eval()

    with torch.no_grad():
        logits = model(torch.tensor(np.ascontiguousarray(Xs))).squeeze(1)
    vals = logits.tolist()  # CMSSW torch build lacks numpy interop

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
