#!/usr/bin/env python3
"""Python-side parity reference for the C++ GENERAL attach-head inference port (M16).

Selects the FIRST --n-per-type rows of EACH target kind (ttype 0 = accepted chain,
ttype 1 = bare T3) from a general pair dump -- the identical, deterministic selection
tools/attach_parity.cc makes -- recomputes the LOGIT (pre-sigmoid, the quantity
attachLogit returns) from the torch checkpoint + norm json, and compares against the C++
JSON when --cpp is given.

Both kinds must be checked: the whole point of the 19th feature (targetType) is that the
two universes are scored by one head, and they populate different regions of every
target-side slot (a bare T3 has nLayers == 3 and chainGateLogit == 0 by construction).

  python3 attach_parity_gen.py --model attach_mlp_g1.pt --norm attach_norm_g1.json \
      --input pairs_gen_c0.root --cpp tools/attach_parity_cpp.json
"""

import argparse
import json
import os
import sys

import numpy as np

PROTO_DIR = os.path.dirname(os.path.abspath(__file__))


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", default=os.path.join(PROTO_DIR, "attach_mlp_g1.pt"))
    p.add_argument("--norm", default=os.path.join(PROTO_DIR, "attach_norm_g1.json"))
    p.add_argument("--input", default=os.path.join(PROTO_DIR, "pairs_gen_c0.root"))
    p.add_argument("--out", default=os.path.join(PROTO_DIR, "attach_parity_gen_ref.json"))
    p.add_argument("--cpp", default=None, help="C++ json to compare against")
    p.add_argument("--n-per-type", type=int, default=1000)
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

    feat_branches = [f"af_{i:02d}" for i in range(n_in)]
    tree = uproot.open(args.input)["pairs"]
    arr = tree.arrays(feat_branches + ["ttype"], library="np")
    tt = arr["ttype"]

    # Deterministic selection: first n-per-type rows of each kind (== attach_parity.cc).
    sel = []
    got = {0: 0, 1: 0}
    for i in range(len(tt)):
        k = 0 if tt[i] == 0 else 1
        if got[k] >= args.n_per_type:
            if got[0] >= args.n_per_type and got[1] >= args.n_per_type:
                break
            continue
        got[k] += 1
        sel.append(i)
    sel = np.asarray(sorted(sel), dtype=np.int64)
    print(f"selected {len(sel)} rows from {args.input} (ttype0 {got[0]}, ttype1 {got[1]})")

    X = np.empty((len(sel), n_in), dtype=np.float32)
    for j, b in enumerate(feat_branches):
        X[:, j] = arr[b][sel]

    for c in norm.get("conditioning") or []:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        elif c["op"] == "log10_1p":
            X[:, j] = np.log10(1.0 + X[:, j])
        else:
            raise ValueError(f"unknown conditioning op {c['op']}")
    Xs = (X - mu) / sd

    blob = torch.load(args.model, map_location="cpu", weights_only=False)
    arch = blob["arch"]
    n_hid, n_out = arch[1], arch[3]
    assert blob.get("feature_names", names) == names, "model/norm feature_names disagree"
    model = torch.nn.Sequential(torch.nn.Linear(n_in, n_hid), torch.nn.ReLU(),
                                torch.nn.Linear(n_hid, n_hid), torch.nn.ReLU(),
                                torch.nn.Linear(n_hid, n_out))
    model.load_state_dict(blob["state_dict"])
    model.eval()
    with torch.no_grad():
        z = np.asarray(model(torch.tensor(np.ascontiguousarray(Xs))).tolist(),
                       dtype=np.float64)
    if n_out > 1:
        vals = z[np.arange(len(sel)), tt[sel].astype(np.int64)]
    else:
        vals = z[:, 0]

    ref = {str(int(i)): float(v) for i, v in zip(sel, vals)}
    with open(args.out, "w") as fh:
        json.dump(ref, fh, indent=0)
    print(f"wrote {args.out}: {len(vals)} logits, min={vals.min():.4f} "
          f"max={vals.max():.4f} frac(>0)={float((vals > 0).mean()):.4f}")

    if args.cpp:
        with open(args.cpp) as fh:
            cpp = json.load(fh)
        assert set(cpp) == set(ref), (f"row sets differ: cpp {len(cpp)} vs py {len(ref)}")
        rc = np.asarray([cpp[k] for k in ref], dtype=np.float64)
        rp = np.asarray([ref[k] for k in ref], dtype=np.float64)
        ttv = tt[sel]
        d = np.abs(rc - rp)
        ok = True
        print(f"\n{'target kind':>16} {'n':>6} {'max|dLogit|':>13} {'mean|dLogit|':>13} "
              f"{'sign agree':>11}")
        for k, nm in ((0, "CHAIN (ttype 0)"), (1, "BARE T3 (ttype 1)")):
            m = ttv == k
            mx = float(d[m].max()) if m.any() else 0.0
            sg = float(((rc[m] > 0) == (rp[m] > 0)).mean()) if m.any() else 1.0
            print(f"{nm:>16} {int(m.sum()):>6d} {mx:>13.3e} {float(d[m].mean()):>13.3e} "
                  f"{sg:>11.4f}")
            if mx >= args.tol:
                ok = False
        print(f"{'ALL':>16} {len(d):>6d} {float(d.max()):>13.3e} {float(d.mean()):>13.3e} "
              f"{float(((rc > 0) == (rp > 0)).mean()):>11.4f}")
        print(f"\nPARITY {'PASS' if ok else 'FAIL'} (tolerance {args.tol:g} per kind)")
        return 0 if ok else 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
