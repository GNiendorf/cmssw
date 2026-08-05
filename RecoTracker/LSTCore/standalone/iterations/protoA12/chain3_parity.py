#!/usr/bin/env python3
"""M12 python-side golden-value reference for the 3-class chain gate.

Recomputes the three raw logits for the first N rows of a chain dump exactly the way
train_chain3.py builds its input matrix (drop-features -> conditioning -> standardize)
and compares them against the C++ output of tools/chain3_parity.

  python3 chain3_parity.py --model chain3_mlp_m12.pt --norm chain3_norm_m12.json \
      --input chains_m12_300evt.root --cpp tools/chain3_parity_out.json
"""
import argparse
import json
import os
import sys

import numpy as np

PROTO_DIR = os.path.dirname(os.path.abspath(__file__))


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", default=os.path.join(PROTO_DIR, "chain3_mlp_m12.pt"))
    p.add_argument("--norm", default=os.path.join(PROTO_DIR, "chain3_norm_m12.json"))
    p.add_argument("--input", default=os.path.join(PROTO_DIR, "chains_m12_300evt.root"))
    p.add_argument("--cpp", default="")
    p.add_argument("--out", default=os.path.join(PROTO_DIR, "chain3_parity_ref.json"))
    p.add_argument("--n-chains", type=int, default=1000)
    p.add_argument("--tol", type=float, default=1e-3)
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
    assert n_in == len(mu) == len(sd)

    f = uproot.open(args.input)
    spec = f["feature_spec"].member("fTitle")
    cf_names = spec[3:].split(",")
    # Map each model input to its dump branch (cf_<NN>, or dcaXY).
    branches, cols = [], []
    for nm in names:
        base = nm[3:]
        if base == "dcaXY":
            branches.append("dcaXY")
        else:
            branches.append(f"cf_{cf_names.index(base):02d}")
    tree = f["chains"]
    n = min(args.n_chains, tree.num_entries)
    arr = tree.arrays(sorted(set(branches)), entry_stop=n, library="np")
    X = np.empty((n, n_in), dtype=np.float32)
    for j, b in enumerate(branches):
        X[:, j] = arr[b]
    print(f"loaded {n} chains x {n_in} inputs from {args.input}")
    print("input->branch: " + ", ".join(f"{nm}={b}" for nm, b in zip(names, branches)))

    for c in norm.get("conditioning") or []:
        if c["feature"] not in names:
            continue
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        elif c["op"] == "log10_1p":
            X[:, j] = np.log10(1.0 + X[:, j])
        else:
            raise ValueError(f"unknown conditioning op {c['op']}")
    Xs = (X - mu) / sd

    try:
        blob = torch.load(args.model, map_location="cpu")
    except Exception:
        blob = torch.load(args.model, map_location="cpu", weights_only=False)
    assert blob.get("feature_names", names) == names, "model/norm feature_names disagree"
    arch = blob["arch"]
    model = torch.nn.Sequential(torch.nn.Linear(arch[0], arch[1]), torch.nn.ReLU(),
                                torch.nn.Linear(arch[1], arch[2]), torch.nn.ReLU(),
                                torch.nn.Linear(arch[2], arch[3]))
    model.load_state_dict(blob["state_dict"])
    model.eval()
    with torch.no_grad():
        z = model(torch.tensor(np.ascontiguousarray(Xs)))
    vals = np.asarray(z.tolist(), dtype=np.float64)

    ref = {str(i): [float(v) for v in vals[i]] for i in range(n)}
    with open(args.out, "w") as fh:
        json.dump(ref, fh, indent=0)
    print(f"wrote {args.out}")
    print(f"first row logits: {[round(v, 6) for v in vals[0]]}")

    if not args.cpp:
        return 0
    with open(args.cpp) as fh:
        cpp = json.load(fh)
    m = min(n, len(cpp))
    cv = np.asarray([cpp[str(i)] for i in range(m)], dtype=np.float64)
    d = np.abs(cv - vals[:m])
    # Decision quantities: the margins are what -G 6 thresholds on.
    mP_p, mD_p = vals[:m, 1] - vals[:m, 0], vals[:m, 2] - vals[:m, 0]
    mP_c, mD_c = cv[:, 1] - cv[:, 0], cv[:, 2] - cv[:, 0]
    print(f"\n=== PARITY over {m} chains ===")
    print(f"  max |dLogit|  = {d.max():.3e}   (per class: "
          f"{d[:, 0].max():.3e} / {d[:, 1].max():.3e} / {d[:, 2].max():.3e})")
    print(f"  max |dmP|     = {np.abs(mP_c - mP_p).max():.3e}")
    print(f"  max |dmD|     = {np.abs(mD_c - mD_p).max():.3e}")
    print(f"  argmax agreement = {float((cv.argmax(1) == vals[:m].argmax(1)).mean()):.6f}")
    ok = d.max() < args.tol
    print(f"  RESULT: {'PASS' if ok else 'FAIL'} (tol {args.tol:g})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
