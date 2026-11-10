#!/usr/bin/env python3
"""Working points of the otpull pair network: one threshold on sigmoid(logit) per (T3 pT, T3 |eta|) cell
(OTPullParams kNWpPt x kNWpEta; edges in common.py = LSTProducer otpullScoreWP* defaults), each the score that keeps a
target fraction q of the TRUE pairs of that cell (test split of every sample, pooled). Floor: a cell never keeps fewer
true pairs than the current decision (chi2 below 1 inside the one-sigma windows) does there. Cells with fewer than
--nmin true pairs take the threshold of their pT row pooled over |eta| (or of every pair at or above that pT row).
With --match-false-row R the pT rows from index R on take instead the score passing no more false pairs than the current
decision does in that cell (high-pT fake control).
Also emulates the arbitration (per-T3 best, per-cell mutual best) on the test split and prints true / false objects per
event against the current decision. The final q is chosen by the end-to-end MTV scan, never by these offline numbers.
  python3 wp_otpull.py models/otpull_v1.json --lab ttbar='rows/ttbar_*.npz' --lab qcd='rows/qcd_*.npz' \
      --q 0.6,0.7,0.8,0.9 --out wp/otpull_v1
"""
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import FEATURES, WP_ETA_EDGES, WP_PT_EDGES, cell_index, load_rows, split_events  # noqa: E402

NP, NE = len(WP_PT_EDGES) + 1, len(WP_ETA_EDGES) + 1


def logits(meta, X):
    sd = {k: np.array(v, np.float64) for k, v in meta["state_dict"].items()}
    lo, hi, mu, std = (np.array(meta[k]) for k in ("clip_lo", "clip_hi", "mean", "std"))
    out = np.empty(len(X))
    for i in range(0, len(X), 1 << 21):
        x = (np.clip(X[i:i + (1 << 21)].astype(np.float64), lo, hi) - mu) / std
        h = np.maximum(x @ sd["0.weight"].T + sd["0.bias"], 0)
        h = np.maximum(h @ sd["2.weight"].T + sd["2.bias"], 0)
        out[i:i + (1 << 21)] = (h @ sd["4.weight"].T + sd["4.bias"])[:, 0]
    return out


def arbitrate(ev, t3, cell, cost, ok):
    """per-T3 lowest cost, then per-cell lowest (cost, T3) among the T3 bests; returns accepted-row mask"""
    idx = np.nonzero(ok)[0]
    acc = np.zeros(len(ok), bool)
    if len(idx) == 0:
        return acc
    o = idx[np.lexsort((cost[idx], t3[idx], ev[idx]))]
    k = ev[o] * (1 << 32) + t3[o]
    bt = o[np.r_[True, k[1:] != k[:-1]]]
    o2 = bt[np.lexsort((t3[bt], cost[bt], cell[bt], ev[bt]))]
    k2 = ev[o2] * (1 << 32) + cell[o2]
    acc[o2[np.r_[True, k2[1:] != k2[:-1]]]] = True
    return acc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("--lab", action="append", required=True)
    ap.add_argument("--q", default="0.6,0.7,0.8,0.9", help="comma list; an item may give one q per |eta| column, a/b/c")
    ap.add_argument("--nmin", type=int, default=40)
    ap.add_argument("--no-floor", action="store_true")
    ap.add_argument("--match-false-row", type=int, default=99,
                    help="pT rows from this index on: the threshold that passes no more false pairs than the current decision")
    ap.add_argument("--chi2-cut", type=float, default=1.0, help="the current decision's chi2 cut (floor, comparison)")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    meta = json.load(open(a.model))
    assert meta["features"] == FEATURES
    seed = meta["args"]["seed"]
    T = {}
    for nm, pat in (x.split("=", 1) for x in a.lab):
        R, _ = load_rows(pat)
        part, _ = split_events(R["ev"], seed)
        m = part == 2
        R = {k: v[m] for k, v in R.items()}
        R["z"] = logits(meta, R["X"])
        R["ip"], R["ie"] = cell_index(R["X"])
        T[nm] = R
        print(f"{nm}: test rows {m.sum()}, true {int(R['y'].sum())}, events {len(np.unique(R['ev']))}")
    yt = np.concatenate([T[nm]["y"] > 0 for nm in T])
    s = 1.0 / (1.0 + np.exp(-np.concatenate([T[nm]["z"] for nm in T])))
    ip = np.concatenate([T[nm]["ip"] for nm in T])
    ie = np.concatenate([T[nm]["ie"] for nm in T])
    cur = np.concatenate([(T[nm]["gate1"] > 0) & (T[nm]["chi2"] < a.chi2_cut) for nm in T])
    yf = ~yt
    wall = np.concatenate([T[nm]["w"] for nm in T]).astype(np.float64)
    res = {"model": a.model, "pt_edges": WP_PT_EDGES, "eta_edges": WP_ETA_EDGES, "tables": {}}
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    lines = []
    for qs in a.q.split(","):
        qv = [float(x) for x in qs.split("/")]  # one q, or one per |eta| column ("0.5/0.7/0.5")
        qv = qv * NE if len(qv) == 1 else qv
        assert len(qv) == NE, qs
        wp = np.zeros((NP, NE))
        info = []
        for p in range(NP):
            rowt = yt & (ip == p)
            for e in range(NE):
                m = rowt & (ie == e)
                src, mm = "cell", m
                if m.sum() < a.nmin:
                    src, mm = "row", rowt
                if mm.sum() < a.nmin:
                    src, mm = "rows above", yt & (ip >= p)
                if mm.sum() < a.nmin:
                    src, mm = "all", yt
                fcur = float(cur[mm].mean())
                if p >= a.match_false_row:
                    # false pairs of the same pooled population, weighted (dump sampling)
                    fm = yf & (ip >= p) if src in ("rows above", "all") else (yf & (ip == p) & ((ie == e) if src == "cell" else True))
                    nfc = (wall[fm] * cur[fm]).sum()
                    o = np.argsort(-s[fm])
                    cw = np.cumsum(wall[fm][o])
                    k = np.searchsorted(cw, nfc, side="right")
                    wp[p, e] = float(s[fm][o][k]) if k < len(o) else 0.0
                    qq = float((s[mm] > wp[p, e]).mean())
                    src += ", matched false"
                else:
                    qq = qv[e] if a.no_floor else max(qv[e], fcur)
                    wp[p, e] = float(np.quantile(s[mm], 1.0 - qq))
                info.append((p, e, int(m.sum()), src, fcur, qq, wp[p, e]))
        res["tables"][qs] = {"wp": wp.tolist(), "cells": info}
        lines.append(f"q {qs}:")
        for p, e, n, src, fcur, qq, w in info:
            lines.append(f"  pT row {p} |eta| col {e}: true {n:7d} ({src}) current keep "
                         f"{'-' if fcur is None else f'{fcur:.3f}'} target {'-' if qq is None else f'{qq:.3f}'} wp {w:.6f}")
        lines.append("  otpullScoreWP = cms.vdouble(" + ", ".join(f"{w:.6g}" for w in wp.ravel()) + ")")
        # emulated arbitration on each sample's test split
        for nm, R in T.items():
            ev, t3, cell = R["ev"], R["t3"].astype(np.int64), R["cell"].astype(np.int64)
            y = R["y"] > 0
            ne = len(np.unique(ev))
            accC = arbitrate(ev, t3, cell, R["chi2"].astype(np.float64), (R["gate1"] > 0) & (R["chi2"] < a.chi2_cut))
            sc = 1.0 / (1.0 + np.exp(-R["z"]))
            w = wp[R["ip"], R["ie"]]
            cost = (1.0 - sc) / (1.0 - w)
            accN = arbitrate(ev, t3, cell, cost, cost < 1.0)
            wt = R["w"]
            hi = R["tppt"] > 5
            lines.append(f"  {nm}: objects/event current true {(wt * (accC & y)).sum() / ne:.2f} false "
                         f"{(wt * (accC & ~y)).sum() / ne:.2f} | network true {(wt * (accN & y)).sum() / ne:.2f} "
                         f"false {(wt * (accN & ~y)).sum() / ne:.2f} | true pT>5 current {(accC & y & hi).sum()} "
                         f"network {(accN & y & hi).sum()} | pairs passing {(wt * (cost < 1)).sum() / ne:.1f}/event")
    open(a.out + ".txt", "w").write("\n".join(lines) + "\n")
    json.dump(res, open(a.out + ".json", "w"), indent=1)
    print("\n".join(lines))


if __name__ == "__main__":
    main()
