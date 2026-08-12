#!/usr/bin/env python3
"""S1: fit the per-cell weld working-point table, and report the head-quality verdict.

THE PROTOCOL (maintainer's standard, FINDINGS_NN "THE CALIBRATION PROTOCOL"):
each cell's bar is set so the NEW head's TRUE-edge acceptance in that cell equals the
SHIPPED head's measured acceptance there, cell = (family, ptbin, etabin) on LST's T3-DNN
binning, E1 and E2 tables SEPARATE.  Signal efficiency is then pinned by construction and
only the FALSE-edge rate is free to move -- that is the verdict.

  FIT   on the VAL events (the events model selection already used)
  JUDGE on the TEST events (never trained on, never calibrated on, never selected on)

Usage: wp_table.py --model models/edge_<tag>.pt --out wp_<tag>.json
"""
import argparse
import json
import os
import sys

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from train_s1 import (CACHE, NBIN_PT, NBIN_ETA, SHIP_BAR, build_model, event_split, gather,
                      row_mask, scores, fast_auc)  # noqa: E402

MIN_TRUE = 20


def cells(s_new, s_ship, y, fam, ptb, etb, bars=None, vxy=None, strat=False):
    """If bars is None: FIT (bar = matched-efficiency quantile). Else: APPLY the given table."""
    rep = []
    tab = np.zeros((2, NBIN_PT * NBIN_ETA), dtype=np.float64)
    for f in (1, 2):
        for a in range(NBIN_PT):
            for j in range(NBIN_ETA):
                b = (fam == f) & (ptb == a) & (etb == j)
                t = b & (y == 1)
                fk = b & (y == 0)
                nT, nF = int(t.sum()), int(fk.sum())
                eff_s = float((s_ship[t] >= SHIP_BAR[f]).mean()) if nT else -1.0
                if bars is None:
                    if nT < MIN_TRUE or nF == 0:
                        nb = SHIP_BAR[f]
                        note = "nTrue<%d -> shipped scalar kept" % MIN_TRUE
                    elif eff_s >= 1.0:
                        # the shipped bar accepts EVERY true edge in this cell. Matching that
                        # exactly means accepting them all -- but with the TIGHTEST bar that does,
                        # not a -1e9 free pass, so the cell's false-edge rate stays as low as
                        # 100% signal efficiency permits.
                        nb = float(np.min(s_new[t]))
                        note = "shipped eff == 1 -> bar = min true logit"
                    elif eff_s <= 0.0:
                        nb = 1e9
                        note = "shipped eff == 0 -> cell closed"
                    else:
                        nb = float(np.quantile(s_new[t], 1.0 - eff_s))
                        note = ""
                        if strat:
                            # The golden gate study's convention (analysis/DNN/train_edge.py
                            # gate_study): set the threshold SEPARATELY in the simVxy < 1 and
                            # >= 1 strata and let the LOOSER one bind, so BOTH keep their shipped
                            # acceptance. A cell matched on the pooled population is matched on a
                            # ~97%-prompt mixture, and in the high-acceptance cells the bar then
                            # sits in the tail where the DISPLACED true edges live.
                            td = t & (vxy >= 1.0)
                            if int(td.sum()) >= MIN_TRUE:
                                ed = float((s_ship[td] >= SHIP_BAR[f]).mean())
                                bd = (float(np.min(s_new[td])) if ed >= 1.0
                                      else float(np.quantile(s_new[td], 1.0 - ed)))
                                if bd < nb:
                                    note = "displaced stratum binds (%.4f < %.4f)" % (bd, nb)
                                    nb = bd
                else:
                    nb = float(bars[f - 1][a * NBIN_ETA + j])
                    note = ""
                tab[f - 1][a * NBIN_ETA + j] = nb
                rep.append({"fam": f, "pt": a, "eta": j, "nTrue": nT, "nFake": nF,
                            "ship_bar": SHIP_BAR[f], "bar": nb, "note": note,
                            "eff_shipped": eff_s,
                            "eff_new": float((s_new[t] >= nb).mean()) if nT else -1.0,
                            "fpr_shipped": float((s_ship[fk] >= SHIP_BAR[f]).mean()) if nF else -1.0,
                            "fpr_new": float((s_new[fk] >= nb).mean()) if nF else -1.0})
    return rep, tab


def summarize(rep, tag):
    ok = [r for r in rep if r["nFake"] > 0 and r["nTrue"] >= MIN_TRUE]
    out = {}
    print("%s" % tag)
    print("  %-24s %4s %10s %10s %10s %10s %8s" %
          ("subset", "cells", "nFake", "fpr_ship", "fpr_new", "ratio", "d_eff"))
    def sub(sel, name):
        s = [r for r in ok if sel(r)]
        if not s:
            return None
        nF = sum(r["nFake"] for r in s)
        nT = sum(r["nTrue"] for r in s)
        ws = sum(r["fpr_shipped"] * r["nFake"] for r in s) / nF
        wn = sum(r["fpr_new"] * r["nFake"] for r in s) / nF
        es = sum(r["eff_shipped"] * r["nTrue"] for r in s) / nT
        en = sum(r["eff_new"] * r["nTrue"] for r in s) / nT
        print("  %-24s %4d %10d %10.6f %10.6f %10.4f %+8.5f" %
              (name, len(s), nF, ws, wn, wn / max(ws, 1e-12), en - es))
        out[name] = {"cells": len(s), "nFake": nF, "fpr_ship": ws, "fpr_new": wn,
                     "ratio": wn / max(ws, 1e-12), "eff_ship": es, "eff_new": en}
        return out[name]
    sub(lambda r: True, "ALL (headline)")
    sub(lambda r: r["fam"] == 1, "E1 only")
    sub(lambda r: r["fam"] == 2, "E2 only")
    sub(lambda r: r["pt"] == 0, "pt<5 only")
    sub(lambda r: r["nTrue"] >= 1000, "nTrue>=1000")
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--model", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--hidden", type=int, default=32)
    p.add_argument("--strat", action="store_true",
                   help="stratify the efficiency match on simVxy (prompt / >=1cm) and let the "
                        "LOOSER bar bind, the golden gate study's own convention")
    a = p.parse_args()

    ck = torch.load(a.model, map_location="cpu", weights_only=False)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = build_model(40, a.hidden).to(dev)
    model.load_state_dict({k: v.to(dev) for k, v in ck["state_dict"].items()})
    mu = torch.tensor(ck["mu"], device=dev)
    sd = torch.tensor(ck["sd"], device=dev)

    M = np.load(os.path.join(CACHE, "meta.npz"))
    off = M["evt_off"]
    ntot = int(off[-1])
    X = np.load(os.path.join(CACHE, "X.npy"), mmap_mode="r")
    tr_ev, va_ev, te_ev = event_split(len(off) - 1, ck["args"]["seed"])
    assert list(va_ev) == ck["split"]["val"], "split drift"

    res = {"model": os.path.abspath(a.model), "best_epoch": ck["best_epoch"],
           "selected_on": ck.get("selected_on", "val_auc")}
    tab = None
    for name, evs in (("val(FIT)", va_ev), ("test(JUDGE)", te_ev)):
        m = row_mask(off, evs, ntot)
        Xs = gather(X, off, evs, dev)
        Xs.sub_(mu).div_(sd)
        sn = scores(model, Xs).cpu().numpy()
        del Xs
        torch.cuda.empty_cache()
        y = M["label"][m]
        rep, t = cells(sn, M["logit"][m], y, M["type"][m], M["ptbin"][m], M["etabin"][m],
                       bars=None if tab is None else tab, vxy=M["simVxy"][m], strat=a.strat)
        if tab is None:
            tab = t
            res["fit_cells"] = rep
        else:
            res["judge_cells"] = rep
        res[name] = summarize(rep, "\n=== %s : %d events, %d rows (%.4f true)" %
                              (name, len(evs), len(y), y.mean()))
        from sklearn.metrics import roc_auc_score
        res[name]["auc_new"] = float(roc_auc_score(y, sn))
        res[name]["auc_ship"] = float(roc_auc_score(y, M["logit"][m]))
        print("  AUC: shipped %.6f  new %.6f" % (res[name]["auc_ship"], res[name]["auc_new"]))
    res["table"] = tab.tolist()
    res["strat"] = a.strat
    res["note"] = ("fitted on the 200 VAL events of the seed-42 60/20/20 event split of "
                   "nnloop_ref/round1 (1000 evt, on-policy dump of the shipped binary); each cell "
                   "reproduces the shipped head's true-edge acceptance in that cell. Cells with "
                   "fewer than %d true edges keep the shipped scalar." % MIN_TRUE)
    json.dump(res, open(a.out, "w"), indent=1)
    print("\nwrote %s" % a.out)
    print("--- kWpBar (C++) ---")
    for f in range(2):
        print("      {" + ", ".join("%.4ff" % v for v in tab[f]) + "},")


if __name__ == "__main__":
    main()
