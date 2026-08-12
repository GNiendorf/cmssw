#!/usr/bin/env python3
"""S1 ARM F calibration: the DUAL per-cell working-point tables of the T3-DNN OR-rule, plus the
affine pinning of the stored scalar.

Per cell (family, ptbin, etabin), SEPARATELY for the two true classes:
    target_P = fraction of that cell's PROMPT-true edges the SHIPPED head accepted
    target_D = fraction of that cell's DISPLACED-true edges the SHIPPED head accepted
    barP     = quantile of (zPrompt - zFake) over prompt trues reproducing target_P
    barD     = quantile of (zDisp   - zFake) over displaced trues reproducing target_D
An edge is eligible iff mP >= barP OR mD >= barD, so each class keeps AT LEAST its shipped
acceptance (the OR can only add) -- the surplus is exactly the displaced admission a single
scalar bar could not express, and it is reported, not hidden.

Then the stored scalar mX = max(zP, zD) - zF is affine-pinned to the shipped logit's scale (mean
and std over the eligible population), because that scalar is the weld rank key, the chain score's
summand and chain features 2/3/4/18. The map is baked into the output layer: rows scaled by a,
bias_out[1..2] += b (max(a*zP + b, a*zD + b) - a*zF == a*mX + b), then the tables are REFITTED on
the mapped model so eligibility is unaffected by the pinning.

  FIT on VAL events, JUDGE on TEST events (never trained, never calibrated on).

Usage: wp3.py --model models/edge_F_3cls.pt --out wp_F.json --out-model models/edge_F_pinned.pt
"""
import argparse
import json
import os
import sys

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from train_s1 import CACHE, SHIP_BAR, event_split, gather, row_mask  # noqa: E402
from train3 import build_model, scores3  # noqa: E402

NBIN = 20
MIN_N = 20
JOINT = False


def fit_or_apply(z, ss, y, disp, fam, cell, bars=None):
    """z: (N,3) logits. Returns (report, tabP, tabD) fitting per cell, or applying `bars`."""
    mP = z[:, 1] - z[:, 0]
    mD = z[:, 2] - z[:, 0]
    tabP = np.zeros(2 * NBIN)
    tabD = np.zeros(2 * NBIN)
    rep = []
    for f in (1, 2):
        for c in range(NBIN):
            b = (fam == f) & (cell == c)
            k = (f - 1) * NBIN + c
            tP = b & (y == 1) & ~disp
            tD = b & (y == 1) & disp
            fk = b & (y == 0)
            nP, nD, nF = int(tP.sum()), int(tD.sum()), int(fk.sum())
            effP = float((ss[tP] >= SHIP_BAR[f]).mean()) if nP else -1.0
            effD = float((ss[tD] >= SHIP_BAR[f]).mean()) if nD else -1.0
            if bars is None:
                def q(v, eff, n):
                    if n < MIN_N or eff < 0:
                        return -1e30  # cell has no statistics: leave this class's bar open
                    if eff >= 1.0:
                        return float(np.min(v))
                    if eff <= 0.0:
                        return 1e30
                    return float(np.quantile(v, 1.0 - eff))
                bP = q(mP[tP], effP, nP)
                bD = q(mD[tD], effD, nD)
                if JOINT and nP >= MIN_N and nD >= MIN_N and 0 < effP < 1 and 0 < effD < 1:
                    # EXACT match of the OR-rule: solve the 2x2 system so that the UNION accepts
                    # exactly effP of the cell's prompt trues and effD of its displaced trues. The
                    # marginal quantiles above are the starting point and are always LOOSER (the
                    # union of two sets that each reach the target overshoots both).
                    aP, bPv = mP[tP], mD[tP]
                    aD, bDv = mD[tD], mP[tD]
                    for _ in range(40):
                        # prompt equation: those not already taken by mD >= bD must come from mP
                        rest = bPv < bD
                        need = effP * nP - (nP - int(rest.sum()))
                        bP = (1e30 if need <= 0 else
                              (-1e30 if need >= int(rest.sum()) else
                               float(np.quantile(aP[rest], 1.0 - need / max(int(rest.sum()), 1)))))
                        rest2 = bDv < bP
                        need2 = effD * nD - (nD - int(rest2.sum()))
                        bD = (1e30 if need2 <= 0 else
                              (-1e30 if need2 >= int(rest2.sum()) else
                               float(np.quantile(aD[rest2], 1.0 - need2 / max(int(rest2.sum()), 1)))))
            else:
                bP, bD = float(bars[0][k]), float(bars[1][k])
            tabP[k], tabD[k] = bP, bD
            el = (mP >= bP) | (mD >= bD)
            rep.append({"fam": f, "cell": c, "nP": nP, "nD": nD, "nF": nF,
                        "target_effP": effP, "target_effD": effD, "barP": bP, "barD": bD,
                        "effP_new": float(el[tP].mean()) if nP else -1.0,
                        "effD_new": float(el[tD].mean()) if nD else -1.0,
                        "fpr_shipped": float((ss[fk] >= SHIP_BAR[f]).mean()) if nF else -1.0,
                        "fpr_new": float(el[fk].mean()) if nF else -1.0})
    return rep, tabP, tabD


def summ(rep, tag):
    ok = [r for r in rep if r["nF"] > 0 and (r["nP"] >= MIN_N or r["nD"] >= MIN_N)]
    nF = sum(r["nF"] for r in ok)
    nP = sum(r["nP"] for r in ok)
    nD = sum(r["nD"] for r in ok)
    ws = sum(r["fpr_shipped"] * r["nF"] for r in ok) / nF
    wn = sum(r["fpr_new"] * r["nF"] for r in ok) / nF
    eP = sum(r["target_effP"] * r["nP"] for r in ok if r["nP"]) / nP
    eP2 = sum(r["effP_new"] * r["nP"] for r in ok if r["nP"]) / nP
    eD = sum(r["target_effD"] * r["nD"] for r in ok if r["nD"]) / nD
    eD2 = sum(r["effD_new"] * r["nD"] for r in ok if r["nD"]) / nD
    print("%s: cells %d  nFake %d" % (tag, len(ok), nF))
    print("   prompt acceptance  shipped %.5f -> new %.5f  (%+.5f)" % (eP, eP2, eP2 - eP))
    print("   displaced acceptance shipped %.5f -> new %.5f  (%+.5f)" % (eD, eD2, eD2 - eD))
    print("   FAKE acceptance    shipped %.5f -> new %.5f   ratio %.4f" % (ws, wn, wn / max(ws, 1e-12)))
    return {"cells": len(ok), "nFake": nF, "effP_ship": eP, "effP_new": eP2,
            "effD_ship": eD, "effD_new": eD2, "fpr_ship": ws, "fpr_new": wn,
            "ratio": wn / max(ws, 1e-12)}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--model", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--out-model", required=True)
    p.add_argument("--joint", action="store_true",
                   help="solve the two bars JOINTLY per cell so the OR-rule matches each class's "
                        "shipped acceptance EXACTLY instead of overshooting it")
    a = p.parse_args()
    global JOINT
    JOINT = a.joint

    ck = torch.load(a.model, map_location="cpu", weights_only=False)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = build_model(40, 32, 3).to(dev)
    model.load_state_dict({k: v.to(dev) for k, v in ck["state_dict"].items()})
    M = np.load(os.path.join(CACHE, "meta.npz"))
    off = M["evt_off"]
    tr, va, te = event_split(len(off) - 1, ck["args"]["seed"])
    X = np.load(os.path.join(CACHE, "X.npy"), mmap_mode="r")

    def get(evs):
        m = row_mask(off, evs, int(off[-1]))
        Xs = gather(X, off, evs, dev)
        Xs.sub_(torch.tensor(ck["mu"], device=dev)).div_(torch.tensor(ck["sd"], device=dev))
        z = scores3(model, Xs).cpu().numpy()
        del Xs
        torch.cuda.empty_cache()
        cell = M["ptbin"][m].astype(int) * 10 + M["etabin"][m].astype(int)
        return (z, M["logit"][m], M["label"][m], M["simVxy"][m] >= 1.0, M["type"][m], cell)

    zv, ssv, yv, dv, fv, cv = get(va)
    print("--- pass 1: fit the dual tables on VAL")
    rep1, tP, tD = fit_or_apply(zv, ssv, yv, dv, fv, cv)
    s1 = summ(rep1, "VAL (pass 1)")

    # ---- affine pinning of the stored scalar, on the eligible population of pass 1
    mX = np.maximum(zv[:, 1], zv[:, 2]) - zv[:, 0]
    k = (fv - 1) * NBIN + cv
    el = (zv[:, 1] - zv[:, 0] >= tP[k]) | (zv[:, 2] - zv[:, 0] >= tD[k])
    elS = ssv >= np.where(fv == 1, SHIP_BAR[1], SHIP_BAR[2])
    A = float(ssv[elS].std() / mX[el].std())
    B = float(ssv[elS].mean() - A * mX[el].mean())
    print("--- affine pinning of mX: a = %.6f  b = %.6f  (shipped eligible mean %.4f std %.4f, "
          "arm %.4f %.4f)" % (A, B, ssv[elS].mean(), ssv[elS].std(), mX[el].mean(), mX[el].std()))
    sd = {kk: v.clone() for kk, v in ck["state_dict"].items()}
    sd["4.weight"] = sd["4.weight"] * A
    sd["4.bias"] = sd["4.bias"] * A
    sd["4.bias"][1] += B
    sd["4.bias"][2] += B
    model.load_state_dict({kk: v.to(dev) for kk, v in sd.items()})
    out = dict(ck)
    out["state_dict"] = {kk: v.cpu() for kk, v in sd.items()}
    out["affine"] = {"a": A, "b": B, "fit": "mean+std of mX over the eligible VAL population"}
    torch.save(out, a.out_model)

    print("--- pass 2: refit the tables on the PINNED model (VAL), then judge on TEST")
    zv2, ssv, yv, dv, fv, cv = get(va)
    rep2, tP2, tD2 = fit_or_apply(zv2, ssv, yv, dv, fv, cv)
    s2 = summ(rep2, "VAL (pass 2, pinned)")
    zt, sst, yt, dt, ft, ct = get(te)
    rep3, _, _ = fit_or_apply(zt, sst, yt, dt, ft, ct, bars=(tP2, tD2))
    s3 = summ(rep3, "TEST (JUDGE, pinned)")
    mX2 = np.maximum(zv2[:, 1], zv2[:, 2]) - zv2[:, 0]
    k2 = (fv - 1) * NBIN + cv
    el2 = (zv2[:, 1] - zv2[:, 0] >= tP2[k2]) | (zv2[:, 2] - zv2[:, 0] >= tD2[k2])
    print("   pinned mX on eligible: mean %.4f std %.4f  (shipped %.4f %.4f)" %
          (mX2[el2].mean(), mX2[el2].std(), ssv[elS].mean(), ssv[elS].std()))
    json.dump({"table_prompt": tP2.tolist(), "table_disp": tD2.tolist(),
               "affine": {"a": A, "b": B}, "val_pass1": s1, "val_pass2": s2, "test": s3,
               "cells_val": rep2, "cells_test": rep3,
               "note": "dual OR-rule tables, fitted on the 200 VAL events of the seed-42 60/20/20 "
                       "split of nnloop_ref/round1; per-class per-cell targets are the SHIPPED "
                       "head's own acceptance of that cell's prompt / displaced true edges."},
              open(a.out, "w"), indent=1)
    print("wrote %s and %s" % (a.out, a.out_model))


if __name__ == "__main__":
    main()
