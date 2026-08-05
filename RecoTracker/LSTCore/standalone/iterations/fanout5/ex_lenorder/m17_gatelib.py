#!/usr/bin/env python3
"""M17 (third gate retrain) shared helpers: score a chain dump with a 3-class gate model
and replicate the prototype's -G 6 kill rules + the K9 pre-claim staging funnel.

Kept in one place so the funnel verification (m17_verify_funnel.py), the equal-kill
threshold calibration (m17_calibrate.py) and any offline ledger use IDENTICAL code.
"""
import json

import numpy as np


def score_dump(pt, normjson, dump, extra_meta=()):
    """Returns (margins, meta). margins = dict(mP, mD, mX). Mirrors b3_calibrate.py."""
    import torch
    import uproot

    norm = json.load(open(normjson))
    names = norm["feature_names"]
    mu = np.asarray(norm["mean"], dtype=np.float32)
    sd = np.asarray(norm["std"], dtype=np.float32)
    f = uproot.open(dump)
    cf_names = f["feature_spec"].member("fTitle")[3:].split(",")
    branches = ["dcaXY" if nm[3:] == "dcaXY" else f"cf_{cf_names.index(nm[3:]):02d}" for nm in names]
    tree = f["chains"]
    arr = tree.arrays(sorted(set(branches)), library="np")
    n = tree.num_entries
    X = np.empty((n, len(names)), dtype=np.float32)
    for j, b in enumerate(branches):
        X[:, j] = arr[b]
    del arr
    for c in norm.get("conditioning") or []:
        if c["feature"] not in names:
            continue
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        elif c["op"] == "log10_1p":
            X[:, j] = np.log10(1.0 + X[:, j])
    X = (X - mu) / sd
    blob = torch.load(pt, map_location="cpu", weights_only=False)
    a = blob["arch"]
    model = torch.nn.Sequential(torch.nn.Linear(a[0], a[1]), torch.nn.ReLU(),
                                torch.nn.Linear(a[1], a[2]), torch.nn.ReLU(),
                                torch.nn.Linear(a[2], a[3]))
    model.load_state_dict(blob["state_dict"])
    model.eval()
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model.to(dev)
    # This torch build has no numpy interop; alias the numpy buffers with torch.frombuffer
    # in both directions (zero copy, no np<->torch bridge involved).
    out = np.empty((n, a[3]), dtype=np.float32)
    X = np.ascontiguousarray(X, dtype=np.float32)
    Xt = torch.frombuffer(memoryview(X.reshape(-1)), dtype=torch.float32).reshape(n, len(names))
    out_t = torch.frombuffer(memoryview(out.reshape(-1)), dtype=torch.float32).reshape(n, a[3])
    with torch.no_grad():
        for i in range(0, n, 1 << 19):
            out_t[i:i + (1 << 19)] = model(Xt[i:i + (1 << 19)].to(dev)).cpu()
    m = {"mP": out[:, 1] - out[:, 0], "mD": out[:, 2] - out[:, 0],
         "mX": np.maximum(out[:, 1], out[:, 2]) - out[:, 0]}
    want = ["evt", "label", "simVxy", "simPt", "nLayers", "dcaXY", "score", "pixPT5", "pixPT3"]
    want += [w for w in extra_meta if w not in want]
    have = set(k.split(";")[0] for k in tree.keys())
    meta = tree.arrays([w for w in want if w in have], library="np")
    meta["nNodes"] = np.rint(tree["cf_00"].array(library="np")).astype(np.int32)
    return m, meta


# ---- prototype constants (main.cc) ----
K_GATE_KILL = 1e9
K_NOCUT_THETA = -1e5


def gate_kill(m, meta, cfg):
    """Replicate main.cc's -G 6 branch rules. Returns (killed, exempt) bool arrays.

    cfg keys (prototype flag names): X, M4, M4D, M5, M6, MD, MR, MRI, Z, C25, C25D.
    """
    mP, mD, mX = m["mP"], m["mD"], m["mX"]
    nL = meta["nLayers"]
    dca = meta["dcaXY"]
    X = cfg["X"]
    Z = cfg.get("Z", 0.0)
    t4ex = max(X, Z)

    killed = np.zeros(len(nL), dtype=bool)
    exempt = np.zeros(len(nL), dtype=bool)

    t4 = nL <= 4
    t4e = t4 & (dca >= t4ex)
    t4i = t4 & ~t4e
    exempt |= t4e
    killed |= t4e & (mD < cfg["M4D"])
    killed |= t4i & (mX < cfg["M4"])

    p5 = nL >= 5
    ip5 = p5 & (dca < X)
    ex5 = p5 & ~ip5
    exempt |= ex5
    thr5 = np.where(nL >= 6, cfg["M6"], cfg["M5"])
    killed |= ip5 & (mP < thr5) & (mX < cfg["MRI"])
    killed |= ex5 & (mD < cfg["MD"]) & (mX < cfg["MR"])

    # C1 cell (nNodes == 2, nLayers == 5), applied to branch survivors only.
    if cfg.get("C25", -1e9) > -1e9:
        cell = (nL == 5) & (meta["nNodes"] == 2)
        killed |= cell & ~killed & (mP < cfg["C25"]) & (mD < cfg["C25D"])
    return killed, exempt


def funnel(m, meta, cfg, drop_pt5=True, drop_pt3=True, u4=0.0, u5=0.0, u6=0.0):
    """Replicate the hybrid chain funnel: in -> theta -> pixdrop. Returns dict of masks."""
    killed, exempt = gate_kill(m, meta, cfg)
    nL = meta["nLayers"]
    score = meta["score"].astype(np.float64) - np.where(killed, K_GATE_KILL, 0.0)
    thr_alt = np.where(nL >= 6, u6, np.where(nL == 5, u5, u4))
    thr = np.where(exempt, thr_alt, K_NOCUT_THETA)
    theta = score >= thr
    pix = np.ones(len(nL), dtype=bool)
    if drop_pt5:
        pix &= meta["pixPT5"] == 0
    if drop_pt3:
        pix &= meta["pixPT3"] == 0
    return {"killed": killed, "exempt": exempt, "theta": theta, "pixdrop": theta & pix,
            "stage": pix}


# f2 / ctl_noatt anchor configuration (the M15 f2 flag set).
CFG_F2 = dict(X=0.5, M4=3.5, M4D=-0.75, M5=1e9, M6=1e9, MD=1e9, MR=-1.8, MRI=0.5, Z=0.0,
              C25=2.0, C25D=-2.0)
