#!/usr/bin/env python3
"""Parity check for the OKR matchFrac regressor export.

Re-implements, in numpy, EXACTLY what the generated header + ChainInference.cc do
(gather via kSrcCol -> log10_1p -> clip -> standardize -> 2 hidden ReLU layers -> linear
output), reading every constant straight out of mf_mlp_weights.h, and compares against
the torch checkpoint evaluated on real chain-dump rows. A mismatch means the export or
the conditioning order drifted; the C++ arithmetic itself is the byte-for-byte chain3
code path.

  python3 mf_parity.py --header mf_mlp_weights.h --model mf_mlp_v1.pt --norm mf_norm_v1.json
"""
import argparse
import json
import os
import re
import sys

import numpy as np

PROTO_DIR = os.path.dirname(os.path.abspath(__file__))


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--header", default=os.path.join(PROTO_DIR, "mf_mlp_weights.h"))
    p.add_argument("--model", default=os.path.join(PROTO_DIR, "mf_mlp_v1.pt"))
    p.add_argument("--norm", default=os.path.join(PROTO_DIR, "mf_norm_v1.json"))
    p.add_argument("--input", default="/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/"
                                      "LSTCore/standalone/fanout3/m16/chains_m17_300evt.root")
    p.add_argument("--n-chains", type=int, default=5000)
    p.add_argument("--tol", type=float, default=2e-4)
    return p.parse_args()


def hdr_scalar(txt, name):
    m = re.search(r"constexpr int %s = (\d+);" % name, txt)
    return int(m.group(1))


def hdr_block(txt, decl):
    """Text between the '=' after `decl` and the matching ';'."""
    i = txt.index(decl)
    j = txt.index("=", i)
    k = txt.index(";", j)
    return txt[j + 1:k]


def hdr_floats(txt, decl):
    return np.asarray([float(v) for v in re.findall(r"(-?[\d.]+(?:e[+-]?\d+)?)f", hdr_block(txt, decl))],
                      dtype=np.float32)


def hdr_bools(txt, decl):
    return np.asarray([v == "true" for v in re.findall(r"\b(true|false)\b", hdr_block(txt, decl))])


def hdr_ints(txt, decl):
    return np.asarray([int(v) for v in re.findall(r"-?\d+", hdr_block(txt, decl))], dtype=int)


def main():
    args = parse_args()
    import torch
    import uproot

    txt = open(args.header).read()
    n_in = hdr_scalar(txt, "kInput")
    n_hid = hdr_scalar(txt, "kHidden")
    n_out = hdr_scalar(txt, "kOutput")
    mean = hdr_floats(txt, "kFeatMean[kInput]")
    std = hdr_floats(txt, "kFeatStd[kInput]")
    clip_lo = hdr_floats(txt, "kClipLo[kInput]")
    clip_hi = hdr_floats(txt, "kClipHi[kInput]")
    log10p1 = hdr_bools(txt, "kLog10p1[kInput]")
    src_col = hdr_ints(txt, "kSrcCol[kInput]")
    w1 = hdr_floats(txt, "wgt_l1[kInput][kHidden]").reshape(n_in, n_hid)
    b1 = hdr_floats(txt, "bias_l1[kHidden]")
    w2 = hdr_floats(txt, "wgt_l2[kHidden][kHidden]").reshape(n_hid, n_hid)
    b2 = hdr_floats(txt, "bias_l2[kHidden]")
    wo = hdr_floats(txt, "wgt_out[kHidden][kOutput]").reshape(n_hid, n_out)
    bo = hdr_floats(txt, "bias_out[kOutput]")
    assert len(mean) == len(std) == len(clip_lo) == len(clip_hi) == len(log10p1) == len(src_col) == n_in
    print(f"header: kInput={n_in} kHidden={n_hid} kOutput={n_out}")

    # Raw ChainFeatures rows + dcaXY straight from the dump, in dump (unconditioned) units.
    f = uproot.open(args.input)
    cf_names = f["feature_spec"].member("fTitle")[3:].split(",")
    n_cf = len(cf_names)
    branches = [f"cf_{i:02d}" for i in range(n_cf)] + ["dcaXY"]
    arr = f["chains"].arrays(branches, entry_stop=args.n_chains, library="np")
    n = len(arr["dcaXY"])
    raw = np.empty((n, n_cf + 1), dtype=np.float32)
    for i in range(n_cf):
        raw[:, i] = arr[f"cf_{i:02d}"]
    raw[:, n_cf] = arr["dcaXY"]
    print(f"loaded {n} dump rows x {n_cf} ChainFeatures columns")

    # ---- the header's own arithmetic ----
    x = np.empty((n, n_in), dtype=np.float32)
    for i in range(n_in):
        col = src_col[i]
        v = raw[:, n_cf] if col < 0 else raw[:, col]
        v = v.astype(np.float32).copy()
        if log10p1[i]:
            v = np.log10(np.float32(1.0) + v).astype(np.float32)
        v = np.clip(v, clip_lo[i], clip_hi[i])
        x[:, i] = (v - mean[i]) / std[i]
    h1 = np.maximum(x @ w1 + b1, 0.0)
    h2 = np.maximum(h1 @ w2 + b2, 0.0)
    z_hdr = (h2 @ wo + bo).reshape(-1)

    # ---- the torch checkpoint, fed through the norm json the trainer wrote ----
    with open(args.norm) as fh:
        norm = json.load(fh)
    names = norm["feature_names"]
    assert len(names) == n_in
    mu_j = np.asarray(norm["mean"], dtype=np.float32)
    sd_j = np.asarray(norm["std"], dtype=np.float32)
    xt = np.empty((n, n_in), dtype=np.float32)
    name_to_raw = {f"cf_{nm}": i for i, nm in enumerate(cf_names)}
    name_to_raw["cf_dcaXY"] = n_cf
    for i, nm in enumerate(names):
        xt[:, i] = raw[:, name_to_raw[nm]]
    for c in norm.get("conditioning") or []:
        j = names.index(c["feature"])
        if c["op"] == "log10_1p":
            xt[:, j] = np.log10(1.0 + xt[:, j])
        else:
            np.clip(xt[:, j], c["lo"], c["hi"], out=xt[:, j])
    xt = (xt - mu_j) / sd_j

    blob = torch.load(args.model, map_location="cpu", weights_only=False)
    sd_t = blob["state_dict"]
    import torch.nn as nn
    model = nn.Sequential(nn.Linear(n_in, n_hid), nn.ReLU(),
                          nn.Linear(n_hid, n_hid), nn.ReLU(), nn.Linear(n_hid, 1))
    model.load_state_dict(sd_t)
    model.eval()
    with torch.no_grad():
        z_t = np.asarray(model(torch.tensor(xt)).reshape(-1).tolist(), dtype=np.float32)

    d = np.abs(z_hdr - z_t)
    print(f"max|dlogit| = {d.max():.3e}  mean = {d.mean():.3e}  (tol {args.tol:g})")
    print(f"logit range header [{z_hdr.min():.3f}, {z_hdr.max():.3f}]  "
          f"pred frac range [{1/(1+np.exp(-z_hdr.max())):.4f} .. {1/(1+np.exp(-z_hdr.min())):.4f}]")
    ok = d.max() < args.tol
    print("PARITY OK" if ok else "PARITY FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
