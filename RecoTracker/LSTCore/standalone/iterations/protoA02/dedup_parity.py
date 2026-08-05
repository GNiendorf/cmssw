#!/usr/bin/env python3
"""Parity check for the exported dedup head: re-implement the GENERATED header's
arithmetic (clip -> standardize -> 22x24x24x1 MLP with ReLU) in numpy straight from the
C++ literals, and compare it against the PyTorch model on real dump features.

This is the same discipline as attach_parity.py / chain_parity.py: it catches transpose,
ordering and rounding mistakes in the export, which are the failure mode that silently
produces a head that scores nonsense in the binary while looking fine in python.

Usage: dedup_parity.py --header dedup_mlp_weights.h --model m.pt --norm m_norm.json
                       --pairs pairs.txt [--n 20000]
"""
import argparse
import json
import re
import sys

import numpy as np

NFEAT = 22


def parse_header(path):
    txt = open(path).read()
    txt = re.sub(r"//[^\n]*", "", txt)

    def scalar(name):
        m = re.search(r"constexpr\s+(?:int|float)\s+%s\s*=\s*([-\w.+]+)f?;" % name, txt)
        return float(m.group(1).rstrip("f"))

    def arr(name):
        m = re.search(r"%s\s*(?:\[[^\]]*\])+\s*=\s*\{(.*?)\n\};" % name, txt, re.S)
        body = m.group(1)
        return [float(v) for v in re.findall(r"[-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?", body)]

    n_in, n_hid = int(scalar("kInput")), int(scalar("kHidden"))
    h = dict(kInput=n_in, kHidden=n_hid)
    for nm in ("kFeatMean", "kFeatStd", "kClipLo", "kClipHi", "bias_l1", "bias_l2", "wgt_out"):
        h[nm] = np.asarray(arr(nm), dtype=np.float32)
    h["wgt_l1"] = np.asarray(arr("wgt_l1"), dtype=np.float32).reshape(n_in, n_hid)
    h["wgt_l2"] = np.asarray(arr("wgt_l2"), dtype=np.float32).reshape(n_hid, n_hid)
    h["bias_out"] = np.float32(scalar("bias_out"))
    return h


def header_logit(h, X):
    x = np.clip(X, h["kClipLo"], h["kClipHi"])
    x = ((x - h["kFeatMean"]) / h["kFeatStd"]).astype(np.float32)
    a = np.maximum(x @ h["wgt_l1"] + h["bias_l1"], 0.0)
    b = np.maximum(a @ h["wgt_l2"] + h["bias_l2"], 0.0)
    return b @ h["wgt_out"] + h["bias_out"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--header", required=True)
    ap.add_argument("--model", required=True)
    ap.add_argument("--norm", required=True)
    ap.add_argument("--pairs", required=True)
    ap.add_argument("--n", type=int, default=20000)
    a = ap.parse_args()

    feats = []
    for line in open(a.pairs):
        if line.startswith("#"):
            continue
        t = line.split()
        if len(t) < 6 + NFEAT:  # -XCL 1 appends sharp-label columns after the features
            continue
        feats.append([float(v) for v in t[6:6 + NFEAT]])
        if len(feats) >= a.n:
            break
    X = np.asarray(feats, dtype=np.float64)

    h = parse_header(a.header)
    assert h["kInput"] == NFEAT, h["kInput"]
    lh = header_logit(h, X)

    import torch
    import torch.nn as nn
    blob = torch.load(a.model, map_location="cpu", weights_only=False)
    norm = json.load(open(a.norm))
    lo, hi = np.asarray(norm["clip_lo"]), np.asarray(norm["clip_hi"])
    mu, sd = np.asarray(norm["mean"]), np.asarray(norm["std"])
    Z = ((np.clip(X, lo, hi) - mu) / sd).astype(np.float32)
    n_hid = blob["arch"][1]
    m = nn.Sequential(nn.Linear(NFEAT, n_hid), nn.ReLU(), nn.Linear(n_hid, n_hid), nn.ReLU(),
                      nn.Linear(n_hid, 1))
    m.load_state_dict(blob["state_dict"])
    m.eval()
    with torch.no_grad():
        lt = np.asarray(m(torch.tensor(Z.tolist(), dtype=torch.float32)).squeeze(1).tolist())

    d = np.abs(lh - lt)
    print(f"parity over {len(X)} pairs: max|dlogit| = {d.max():.3e}, mean = {d.mean():.3e}")
    print(f"logit range header [{lh.min():.3f}, {lh.max():.3f}] torch [{lt.min():.3f}, {lt.max():.3f}]")
    ok = d.max() < 2e-3
    print("PARITY OK" if ok else "PARITY FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
