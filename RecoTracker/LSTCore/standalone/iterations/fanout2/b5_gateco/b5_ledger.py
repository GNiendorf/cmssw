#!/usr/bin/env python3
"""ANGLE B5: turn the AUC ladder into the campaign's currency.

For each of the three m14_j1 kill cells, at the EXACT j1 threshold under the M12 gate,
measure the kill ledger (fakes / trues / displaced-trues killed); then for every
candidate model find the threshold that kills the SAME NUMBER OF TRUES and report how
many more fakes it removes. An AUC delta only matters if it converts here.

Cells (m14_j1 = -M4 2.5 -MRI 1.5 -MR -0.800 -MD 1e9 -X 0.5, all on mX):
  T4-IP    : nLayers<=4 & dca<0.5   kill mX <  2.5     (-M4)
  IP 5+    : nLayers>=5 & dca<0.5   kill mX <  1.5     (-MRI, pure-mX OR-rescue)
  exempt5+ : nLayers>=5 & dca>=0.5  kill mX < -0.800   (-MR, owns 74-85% of residual fake)

Evaluated on the FROZEN TEST-60 events only (out of sample for every model).
"""
import json
import os
import sys
import time

import numpy as np

T0 = time.time()
PROTO = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, PROTO)
import train_chain3 as tc3  # noqa: E402
import b5_gateco_train as b5  # noqa: E402


def log(m):
    print(f"[{time.time()-T0:8.1f}s] {m}", flush=True)


def mlp_margins(pt_path, X, names, dev):
    """Score every row with a saved b5 / m12 model, gathering its own feature subset."""
    import torch
    blob = torch.load(pt_path, map_location="cpu", weights_only=False)
    fn = blob["feature_names"]
    cols = [names.index(n) for n in fn]
    norm = json.load(open(pt_path.replace(".pt", "_norm.json")))
    mu = np.asarray(norm["mean"], np.float32)
    sd = np.asarray(norm["std"], np.float32)
    Xc = (X[:, cols] - mu) / sd
    a = blob["arch"]
    m = torch.nn.Sequential(torch.nn.Linear(a[0], a[1]), torch.nn.ReLU(),
                            torch.nn.Linear(a[1], a[2]), torch.nn.ReLU(),
                            torch.nn.Linear(a[2], a[3]))
    m.load_state_dict(blob["state_dict"])
    m.eval().to(dev)
    out = np.empty((len(Xc), 3), np.float32)
    Xt = torch.tensor(np.ascontiguousarray(Xc))
    with torch.no_grad():
        for i in range(0, len(Xc), 1 << 19):
            out[i:i + (1 << 19)] = np.asarray(m(Xt[i:i + (1 << 19)].to(dev)).cpu().tolist(), dtype=np.float32)
    return np.maximum(out[:, 1], out[:, 2]) - out[:, 0]


def main():
    import torch
    from sklearn.ensemble import HistGradientBoostingClassifier
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    rng = np.random.default_rng(42)
    inputs = [f"{b5.SHARED}/chains_m12_300evt.root", f"{b5.SHARED}/chains_m12_498evt.root"]
    meta, X, src, names = b5.load_all(inputs, set())
    np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
    dca = meta["dcaXY"].copy()
    cond = list(tc3.CONDITIONING_SPEC)
    for nm, _, _, ops in b5.DERIVED:
        for o in ops:
            cond.append(dict(o, feature=nm))
    tc3.apply_conditioning(X, names, cond)

    class A:
        train_frac, val_frac, pool_train_frac = 0.6, 0.2, 0.75
    tr, va, te = tc3.combined_event_split(meta, src, A, rng)
    is_true = meta["label"] == 1
    vxy = meta["simVxy"]
    nl = meta["nLayers"]
    y3 = np.zeros(len(X), np.int64)
    y3[is_true & (vxy < 1)] = 1
    y3[is_true & (vxy >= 1)] = 2

    # --- models -> mX over every row ---
    mods = {}
    # M12 production gate: feature names live in chain3_norm_m12.json
    norm = json.load(open(f"{PROTO}/chain3_norm_m12.json"))
    cols = [names.index(n) for n in norm["feature_names"]]
    blob = torch.load(f"{PROTO}/chain3_mlp_m12.pt", map_location="cpu", weights_only=False)
    a = blob["arch"]
    m = torch.nn.Sequential(torch.nn.Linear(a[0], a[1]), torch.nn.ReLU(),
                            torch.nn.Linear(a[1], a[2]), torch.nn.ReLU(),
                            torch.nn.Linear(a[2], a[3]))
    m.load_state_dict(blob["state_dict"])
    m.eval().to(dev)
    Xc = (X[:, cols] - np.asarray(norm["mean"], np.float32)) / np.asarray(norm["std"], np.float32)
    out = np.empty((len(Xc), 3), np.float32)
    Xt = torch.tensor(np.ascontiguousarray(Xc))
    with torch.no_grad():
        for i in range(0, len(Xc), 1 << 19):
            out[i:i + (1 << 19)] = np.asarray(m(Xt[i:i + (1 << 19)].to(dev)).cpu().tolist(), dtype=np.float32)
    mods["m12_prod"] = np.maximum(out[:, 1], out[:, 2]) - out[:, 0]
    log("scored m12 production gate")
    for v in ("v1_wide", "v3_w64f"):
        mods[v] = mlp_margins(f"{PROTO}/b5_{v}.pt", X, names, dev)
        log(f"scored {v}")

    # GBDT ceiling model (retrained here; same recipe as b5_ceiling gbdt_big)
    n0 = int((y3[tr] == 0).sum())
    pw = n0 / max(int((y3[tr] > 0).sum()), 1)
    w = np.ones(len(X), np.float32)
    w[is_true] = pw
    w[is_true & (vxy >= 1) & (vxy < 5)] = pw * 8
    w[is_true & (vxy >= 5)] = pw * 16
    t = time.time()
    clf = HistGradientBoostingClassifier(max_iter=600, learning_rate=0.06,
                                         max_leaf_nodes=255, min_samples_leaf=40,
                                         l2_regularization=1.0, max_bins=255,
                                         early_stopping=False, random_state=42)
    clf.fit(X[tr], y3[tr], sample_weight=w[tr])
    lz = np.log(np.clip(clf.predict_proba(X), 1e-12, None))
    mods["gbdt_big"] = (np.maximum(lz[:, 1], lz[:, 2]) - lz[:, 0]).astype(np.float32)
    log(f"scored gbdt_big ({time.time()-t:.0f}s)")

    CELLS = {"T4-IP  (-M4 2.5)": ((nl <= 4) & (dca < 0.5), 2.5),
             "IP5+   (-MRI 1.5)": ((nl >= 5) & (dca < 0.5), 1.5),
             "exempt5+(-MR -0.8)": ((nl >= 5) & (dca >= 0.5), -0.800)}
    res = {}
    print("\n=== KILL LEDGER on frozen TEST-60, equal-TRUE-kill matching ===")
    for cname, (cell, thr) in CELLS.items():
        m_te = cell & te
        s0 = mods["m12_prod"][m_te]
        yy = y3[m_te]
        vv = vxy[m_te]
        k0 = s0 < thr
        nk0, nf0, nt0 = int(k0.sum()), int((k0 & (yy == 0)).sum()), int((k0 & (yy > 0)).sum())
        nd0 = int((k0 & (yy == 2)).sum())
        print(f"\n{cname}   branch n={int(m_te.sum())}  "
              f"(fake {int((yy==0).sum())} / true {int((yy>0).sum())} / disp {int((yy==2).sum())})")
        print(f"  {'m12_prod':<12} thr={thr:+7.3f} kill={nk0:6d} fakes={nf0:6d} "
              f"trues={nt0:6d} disp={nd0:4d}  f/t={nf0/max(nt0,1):6.2f}")
        res[cname] = {"n": int(m_te.sum()), "m12": {"kill": nk0, "fakes": nf0,
                                                    "trues": nt0, "disp": nd0}}
        for mn, s in mods.items():
            if mn == "m12_prod":
                continue
            ss = s[m_te]
            # threshold that kills exactly nt0 trues (equal TRUE cost)
            st = np.sort(ss[yy > 0])
            thr_eq = np.nextafter(st[nt0 - 1], np.inf) if nt0 > 0 else -np.inf
            k = ss < thr_eq
            nf, nt = int((k & (yy == 0)).sum()), int((k & (yy > 0)).sum())
            nd = int((k & (yy == 2)).sum())
            # and the threshold that kills exactly nk0 rows (equal kill RATE)
            sr = np.sort(ss)
            thr_r = np.nextafter(sr[nk0 - 1], np.inf) if nk0 > 0 else -np.inf
            kr = ss < thr_r
            nfr, ntr_ = int((kr & (yy == 0)).sum()), int((kr & (yy > 0)).sum())
            ndr = int((kr & (yy == 2)).sum())
            print(f"  {mn:<12} EQUAL-TRUE  kill={int(k.sum()):6d} fakes={nf:6d} "
                  f"({nf-nf0:+5d}, {100*(nf-nf0)/max(nf0,1):+5.1f}%) trues={nt:6d} disp={nd:4d}")
            print(f"  {'':<12} EQUAL-RATE  kill={int(kr.sum()):6d} fakes={nfr:6d} "
                  f"({nfr-nf0:+5d}) trues={ntr_:6d} ({ntr_-nt0:+5d}) disp={ndr:4d} ({ndr-nd0:+4d})")
            res[cname][mn] = {"eq_true": {"fakes": nf, "trues": nt, "disp": nd},
                              "eq_rate": {"fakes": nfr, "trues": ntr_, "disp": ndr}}
    json.dump(res, open(f"{PROTO}/b5_ledger.json", "w"), indent=1)
    log("wrote b5_ledger.json")


if __name__ == "__main__":
    main()
