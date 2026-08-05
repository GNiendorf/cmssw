#!/usr/bin/env python3
"""okretrain export parity: the GENERATED chain_mlp_weights.h must reproduce the torch
model + norm json logit for real chain rows.

Parses the header's constexpr arrays and replays EXACTLY the arithmetic
ChainInference.cc performs (log10_1p -> clip -> standardize -> 3 linear layers with
relu), then compares against torch(model) on the same rows built from the norm json.
This is the C++/python parity of the chain_parity.py pattern with the header itself as
the C++ side, so the check covers the only thing the export can get wrong.

  python3 ok_parity.py --header chain_mlp_weights.h --model chain_mlp_vb.pt \
      --norm chain_norm_vb.json --dump chains_m18.root --n 20000
"""
import argparse
import json
import os
import re
import sys

import numpy as np

D = os.path.dirname(os.path.abspath(__file__))


def parse_header(path):
    src = open(path).read()
    src = re.sub(r"//[^\n]*", "", src)

    def scalar(name):
        m = re.search(rf"constexpr\s+\w+\s+{name}\s*=\s*([^;]+);", src)
        return m.group(1).strip()

    def arr(name):
        m = re.search(rf"constexpr\s+\w+\s+{name}\s*\[[^=]*=\s*\{{(.*?)\}};", src, re.S)
        body = m.group(1)
        body = body.replace("{", " ").replace("}", " ")
        toks = [t.strip() for t in body.split(",") if t.strip()]
        return toks

    h = {}
    h["kInput"] = int(scalar("kInput"))
    h["kHidden"] = int(scalar("kHidden"))
    h["bias_out"] = np.float32(scalar("bias_out").rstrip("f"))
    for nm in ("kFeatMean", "kFeatStd", "kClipLo", "kClipHi", "bias_l1", "bias_l2", "wgt_out"):
        h[nm] = np.array([np.float32(t.rstrip("f")) for t in arr(nm)], dtype=np.float32)
    h["kLog10p1"] = np.array([t == "true" for t in arr("kLog10p1")], dtype=bool)
    h["wgt_l1"] = np.array([np.float32(t.rstrip("f")) for t in arr("wgt_l1")],
                           dtype=np.float32).reshape(h["kInput"], h["kHidden"])
    h["wgt_l2"] = np.array([np.float32(t.rstrip("f")) for t in arr("wgt_l2")],
                           dtype=np.float32).reshape(h["kHidden"], h["kHidden"])
    return h


def header_forward(h, Xraw):
    x = Xraw.astype(np.float32).copy()
    lg = h["kLog10p1"]
    x[:, lg] = np.log10(1.0 + x[:, lg]).astype(np.float32)
    x = np.clip(x, h["kClipLo"], h["kClipHi"]).astype(np.float32)
    x = ((x - h["kFeatMean"]) / h["kFeatStd"]).astype(np.float32)
    a = np.maximum(x @ h["wgt_l1"] + h["bias_l1"], 0.0).astype(np.float32)
    b = np.maximum(a @ h["wgt_l2"] + h["bias_l2"], 0.0).astype(np.float32)
    return (b @ h["wgt_out"] + h["bias_out"]).astype(np.float32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--header", default=f"{D}/chain_mlp_weights.h")
    ap.add_argument("--model", required=True)
    ap.add_argument("--norm", required=True)
    ap.add_argument("--dump", default=f"{D}/chains_m18.root")
    ap.add_argument("--n", type=int, default=20000)
    ap.add_argument("--tol", type=float, default=2e-4)
    a = ap.parse_args()

    import torch
    import uproot

    h = parse_header(a.header)
    n_in = h["kInput"]
    f = uproot.open(a.dump)
    cf_names = f["feature_spec"].member("fTitle")[3:].split(",")
    tree = f["chains"]
    n = min(a.n, tree.num_entries)
    arr = tree.arrays([f"cf_{i:02d}" for i in range(len(cf_names))], entry_stop=n, library="np")
    Xraw = np.empty((n, len(cf_names)), dtype=np.float32)
    for j in range(len(cf_names)):
        Xraw[:, j] = arr[f"cf_{j:02d}"]

    # header side (== ChainInference.cc arithmetic, full 25-column ChainFeatures order)
    assert n_in == len(cf_names), f"header kInput {n_in} != dump feature count {len(cf_names)}"
    s_h = header_forward(h, Xraw)

    # torch side (norm json conditioning + standardization, chain_parity.py pattern)
    norm = json.load(open(a.norm))
    names = norm["feature_names"]
    full = [f"cf_{c}" for c in cf_names]
    Xc = Xraw.copy()
    for c in norm.get("conditioning") or []:
        j = full.index(c["feature"])
        if c["op"] == "clip":
            np.clip(Xc[:, j], c["lo"], c["hi"], out=Xc[:, j])
        elif c["op"] == "log10_1p":
            Xc[:, j] = np.log10(1.0 + Xc[:, j])
    Xc = np.ascontiguousarray(Xc[:, [full.index(nm) for nm in names]])
    Xc = (Xc - np.asarray(norm["mean"], dtype=np.float32)) / np.asarray(norm["std"], dtype=np.float32)
    blob = torch.load(a.model, map_location="cpu", weights_only=False)
    ar = blob["arch"]
    model = torch.nn.Sequential(torch.nn.Linear(ar[0], ar[1]), torch.nn.ReLU(),
                                torch.nn.Linear(ar[1], ar[2]), torch.nn.ReLU(),
                                torch.nn.Linear(ar[2], ar[3]))
    model.load_state_dict(blob["state_dict"])
    model.eval()
    with torch.no_grad():
        s_t = np.asarray(model(torch.tensor(np.ascontiguousarray(Xc))).squeeze(1).tolist(),
                         dtype=np.float32)

    d = np.abs(s_h - s_t)
    print(f"rows={n} header={os.path.basename(a.header)} model={os.path.basename(a.model)}")
    print(f"  logit range header [{s_h.min():+.4f},{s_h.max():+.4f}] "
          f"torch [{s_t.min():+.4f},{s_t.max():+.4f}]")
    print(f"  max|diff|={d.max():.3e} mean|diff|={d.mean():.3e} "
          f"n(|diff|>{a.tol:g})={int((d > a.tol).sum())}")
    print(f"  sign agreement: {float((np.sign(s_h) == np.sign(s_t)).mean()):.6f}")
    ok = d.max() <= a.tol
    print("PARITY OK" if ok else "PARITY FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
