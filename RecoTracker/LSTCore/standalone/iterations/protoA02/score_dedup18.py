#!/usr/bin/env python3
"""Score a -XCP dump with a trained dedup head and write per-SEED max-pooled scores.

    score_dedup18.py --model M.pt --norm M_norm.json --pairs P.txt --out S.txt
    -> lines "run lumi evt seed score", which a02_ref/price18.py reads via --scores.
"""
import argparse
import json

import numpy as np

NFEAT = 22


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True)
    ap.add_argument("--norm", required=True)
    ap.add_argument("--pairs", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    import torch
    import torch.nn as nn

    ck = torch.load(a.model, map_location="cpu", weights_only=False)
    arch = ck["arch"]
    model = nn.Sequential(nn.Linear(arch[0], arch[1]), nn.ReLU(),
                          nn.Linear(arch[1], arch[2]), nn.ReLU(),
                          nn.Linear(arch[2], arch[3]))
    model.load_state_dict(ck["state_dict"])
    model.eval()
    nz = json.load(open(a.norm))
    lo = np.asarray(nz["clip_lo"])
    hi = np.asarray(nz["clip_hi"])
    mean = np.asarray(nz["mean"])
    std = np.asarray(nz["std"])

    keys, feats = [], []
    with open(a.pairs) as fh:
        hdr = fh.readline().split()[1:]
        for line in fh:
            if line.startswith("#") or not line.endswith("\n"):
                continue
            t = line.split()
            if len(t) != len(hdr):
                continue
            keys.append((t[0], t[1], t[2], t[3]))
            feats.append([float(v) for v in t[6:6 + NFEAT]])
    X = np.asarray(feats)
    Z = ((np.clip(X, lo, hi) - mean) / std).astype(np.float32)
    with torch.no_grad():
        s = np.asarray(model(torch.tensor(Z.tolist(), dtype=torch.float32)).squeeze(1).tolist())
    best = {}
    for k, v in zip(keys, s):
        if k not in best or v > best[k]:
            best[k] = v
    with open(a.out, "w") as fh:
        for k, v in best.items():
            fh.write("%s %s %s %s %.6f\n" % (k[0], k[1], k[2], k[3], v))
    print("wrote %s (%d seeds)" % (a.out, len(best)))


if __name__ == "__main__":
    main()
