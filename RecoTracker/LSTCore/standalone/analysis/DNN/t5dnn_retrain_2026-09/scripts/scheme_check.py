#!/usr/bin/env python3
"""Held-out PU200 + jets (T5 level, orientation only): at the old creation (kWp98) and promotion (kWp93) PU200 fake
retention, compare kept fractions of (a) old score + old tables, (b) 3-class OR scheme with per-bin prompt/displaced
tables, (c) one per-bin table on 1-P(fake) derived on all fully matched trues. For (b),(c) the per-bin retention r
is bisected so the PU200 held-out fake retention equals that of (a)."""
import importlib.util
import os
import sys

import numpy as np
import torch

DNN = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/displaced_ref/t5dnn/wt_train/RecoTracker/LSTCore/standalone/analysis/DNN"
spec = importlib.util.spec_from_file_location("wp", os.path.join(DNN, "wp_T5_DNN.py"))
wp = importlib.util.module_from_spec(spec)
sys.argv = [sys.argv[0]] + sys.argv[1:]
spec.loader.exec_module(wp)
tr = wp.tr

model_path = sys.argv[1]
ck = torch.load(model_path, map_location="cpu", weights_only=False)
targs, names = ck["args"], ck["feature_names"]
mu, sd = np.array(ck["mean"]), np.array(ck["std"])
model = torch.nn.Sequential(torch.nn.Linear(len(names), 32), torch.nn.ReLU(), torch.nn.Linear(32, 32),
                            torch.nn.ReLU(), torch.nn.Linear(32, 3))
model.load_state_dict(ck["state_dict"])
model.eval()
old = wp.old_tables(os.path.join(DNN, "../../../interface/alpaka/Common.h"))
labs = dict(x.split("=", 1) for x in targs["lab"])
rng = np.random.default_rng(targs["seed"])
D = {}
for nm, pat in labs.items():
    H, f, evt, _ = tr.load_sample(pat)
    ev = np.unique(evt)
    perm = rng.permutation(ev)
    if nm == "gun":
        continue
    held = np.isin(evt, perm[int(0.6 * len(ev)):])
    X = tr.build_features(H, f)
    Xm = np.stack([X[n] for n in names], axis=1)
    g = held & np.isfinite(Xm).all(axis=1)
    with torch.no_grad():
        p = np.array(torch.softmax(model(torch.tensor(np.ascontiguousarray(((Xm[g] - mu) / sd).astype(np.float32)))),
                                   dim=1).tolist())
    pt = f["t5_innerRadius"][g] * wp.K2RINV1GEV2
    ptb, eb1 = wp.bins(pt, H[(0, "eta")][g])
    l2 = np.where(np.isin(H[(0, "layer")][g], [1, 7]), H[(1, "eta")][g], H[(0, "eta")][g])
    _, eb2 = wp.bins(pt, l2)
    vxy, fake, pm = f["t5_sim_vxy"][g], f["t5_isFake"][g] == 1, f["t5_pMatched"][g]
    D[nm] = dict(p=p, ptb=ptb, eb={1: eb1, 2: eb2}, vxy=vxy, fake=fake, full=pm > 0.95, old=f["t5_dnnScore"][g])
    print(nm, "held-out rows", g.sum(), flush=True)

pu = D["pu"]
split = targs["vxy_split"]


def classes(d):
    t = ~d["fake"]
    c = {"fake": d["fake"], "prompt": t & (d["vxy"] < split)}
    for lo, hi in wp.VXY_BANDS:
        c[f"d{lo:g}-{hi:g}"] = t & (d["vxy"] >= lo) & (d["vxy"] < hi)
    return c


def tables(r, which, eb):
    if which == "or":
        wpP, _ = wp.table(pu["p"][:, 1], pu["full"] & (pu["vxy"] < split), pu["ptb"], pu["eb"][eb], r)
        wpD, _ = wp.table(pu["p"][:, 2], pu["full"] & (pu["vxy"] >= split), pu["ptb"], pu["eb"][eb], r)
        return lambda d: (d["p"][:, 1] > wpP[d["ptb"], d["eb"][eb]]) | (d["p"][:, 2] > wpD[d["ptb"], d["eb"][eb]])
    w, _ = wp.table(1 - pu["p"][:, 0], pu["full"], pu["ptb"], pu["eb"][eb], r)
    return lambda d: (1 - d["p"][:, 0]) > w[d["ptb"], d["eb"][eb]]


for lab, key, eb, cmp in (("creation", "kWp98", 1, np.greater), ("promotion", "kWp93", 2, np.greater_equal)):
    rules = {"old": lambda d, key=key, eb=eb, cmp=cmp: cmp(d["old"], old[key][d["ptb"], d["eb"][eb]])}
    target = rules["old"](pu)[pu["fake"]].mean()
    for which in ("or", "pfake"):
        lo, hi = 0.3, 0.9999
        for _ in range(25):
            mid = 0.5 * (lo + hi)
            fr = tables(mid, which, eb)(pu)[pu["fake"]].mean()
            lo, hi = (mid, hi) if fr < target else (lo, mid)
        rules[f"{which}@r={mid:.3f}"] = tables(mid, which, eb)
    print(f"== {lab}: PU200 fake retention of the old rule {target:.4f}")
    for nm, d in D.items():
        c = classes(d)
        print(f"  {nm:4s} {'':18s}" + "".join(f"{k:>10s}" for k in c))
        for rn, rule in rules.items():
            ps = rule(d)
            print(f"  {nm:4s} {rn:18s}" + "".join(f"{ps[m].mean():10.4f}" for m in c.values()))
