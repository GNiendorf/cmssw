#!/usr/bin/env python3
"""W5 PHASE 1: CROSS-FAMILY CALIBRATION of the weld argmax key.

The defect (N3, jet round 4): the edge head's eligibility bar is fitted PER FAMILY and per
(pT,|eta|) cell -- the 80 weld WPs -- so `logOdds` is only calibrated WITHIN a family, and then
ChainWeldArgmax puts both families into ONE atomicMax and compares the two scales directly.

The principled fix is a per-(family, cell) calibration that makes the two comparable, i.e. a map
    logOdds -> logit P(this edge joins two T3s of the SAME SIM | family, cell, logOdds)
fitted so that equal calibrated score means equal evidence.  A flat constant cannot do this and
was scanned and closed (N3's FAMOFF family); an affine map per (family, cell) can, because it lets
the two families' curves CROSS -- a strongly-supported E1 edge keeps its slot while a mediocre one
loses it.

Corpora: BOTH samples, per rule 3 and per N3's own proposal that PU200's displaced bands be in the
fit.  Cube rows are NOT in the fit.

usage: cal.py <out.npz> [jetfrac]
"""
import glob
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import edgew  # noqa: E402

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"


def load_jets_sub(W, maxev=450):
    """JPR4's free 450-event edge sample: subP/subD/subT/subB/subTrue."""
    lo, fam, bn, y = [], [], [], []
    for p in sorted(glob.glob(SA + "/jpr4_ref/fun/e*.npz"),
                    key=lambda s: int(os.path.basename(s)[1:-4]))[:maxev]:
        z = np.load(p)
        if "subP" not in z.files or len(z["subP"]) == 0:
            continue
        mP, mD = z["subP"].astype(np.float64), z["subD"].astype(np.float64)
        f, b = z["subT"].astype(np.int64), z["subB"].astype(np.int64)
        e = edgew.eligible(mP, mD, f, b, W)
        lo.append(np.maximum(mP, mD)[e])
        fam.append(f[e])
        bn.append(b[e])
        y.append(z["subTrue"].astype(np.int64)[e])
    return (np.concatenate(lo), np.concatenate(fam), np.concatenate(bn), np.concatenate(y))


def load_dir(d, disp_only=False):
    lo, fam, bn, y, wt = [], [], [], [], []
    for p in sorted(glob.glob(os.path.join(d, "e*.npz")),
                    key=lambda s: int(os.path.basename(s)[1:-4])):
        z = np.load(p, allow_pickle=True)
        if int(z["replay_mismatch"]) != 0:
            continue
        e = z["elig"]
        lo.append(z["lo"].astype(np.float64)[e])
        fam.append(z["et"].astype(np.int64)[e])
        bn.append(z["wpb"].astype(np.int64)[e])
        yy = z["trueEdge"][e].astype(np.int64)
        y.append(yy)
        if disp_only:
            s = z["esim"].astype(np.int64)[e]
            vxy = np.where(s >= 0, z["sim_vxy"][np.maximum(s, 0)], 0.0)
            dxy = np.where(s >= 0, z["sim_dxy"][np.maximum(s, 0)], 0.0)
            wt.append(np.where(yy & ((vxy >= 1.0) | (dxy >= 1.0)), 1.0, 0.0))
    return (np.concatenate(lo), np.concatenate(fam), np.concatenate(bn), np.concatenate(y),
            np.concatenate(wt) if wt else None)


def fit_logistic(x, y, w, iters=60, l2=1e-3):
    """Newton/IRLS on a 2-parameter logistic, weighted."""
    a, b = 1.0, -5.0
    for _ in range(iters):
        z = a * x + b
        p = 1.0 / (1.0 + np.exp(-np.clip(z, -40, 40)))
        r = w * (y - p)
        s = w * p * (1 - p)
        g = np.array([np.dot(r, x), r.sum()]) - l2 * np.array([a, b])
        H = np.array([[np.dot(s, x * x), np.dot(s, x)],
                      [np.dot(s, x), s.sum()]]) + l2 * np.eye(2)
        try:
            step = np.linalg.solve(H, g)
        except np.linalg.LinAlgError:
            break
        a += step[0]
        b += step[1]
        if np.abs(step).max() < 1e-9:
            break
    return a, b


def main(outnpz, jetw=1.0, dispw=1.0):
    W = edgew.load()
    print("loading jets sub-corpus (jpr4_ref/fun) ...")
    jl, jf, jb, jy = load_jets_sub(W)
    print("  jets eligible rows %d, same-sim rate %.5f (E1 %.5f  E2 %.5f)"
          % (len(jl), jy.mean(), jy[jf == 1].mean(), jy[jf == 2].mean()))
    print("loading PU200 (w5_ref/dp) ...")
    pl, pf, pb, py, pw = load_dir(SA + "/w5_ref/dp", disp_only=True)
    print("  PU200 eligible rows %d, same-sim rate %.5f (E1 %.5f  E2 %.5f)"
          % (len(pl), py.mean(), py[pf == 1].mean(), py[pf == 2].mean()))

    x = np.concatenate([jl, pl])
    f = np.concatenate([jf, pf])
    b = np.concatenate([jb, pb])
    y = np.concatenate([jy, py])
    w = np.concatenate([np.full(len(jl), jetw),
                        np.ones(len(pl)) + (dispw - 1.0) * (pw if pw is not None else 0.0)])

    A = np.zeros(40)
    B = np.zeros(40)
    print("\n cell-by-cell affine calibration  a*lo + b  (logit of P(same-sim | elig, fam, cell))")
    print(" %-6s %8s %8s %10s %10s | %8s %8s %10s %10s | %10s"
          % ("bin", "nE1", "rateE1", "aE1", "bE1", "nE2", "rateE2", "aE2", "bE2", "cross lo"))
    for bi in range(20):
        row = []
        for fa in (1, 2):
            m = (f == fa) & (b == bi)
            n = int(m.sum())
            if n < 500 or y[m].sum() < 5:
                a_, b_ = 1.0, -8.0
            else:
                a_, b_ = fit_logistic(x[m], y[m].astype(np.float64), w[m])
            A[(fa - 1) * 20 + bi] = a_
            B[(fa - 1) * 20 + bi] = b_
            row.append((n, y[m].mean() if n else np.nan, a_, b_))
        (n1, r1, a1, b1), (n2, r2, a2, b2) = row
        cross = (b2 - b1) / (a1 - a2) if abs(a1 - a2) > 1e-9 else np.nan
        print(" %-6d %8d %8.5f %10.4f %10.4f | %8d %8.5f %10.4f %10.4f | %10.3f"
              % (bi, n1, r1, a1, b1, n2, r2, a2, b2, cross))

    np.savez(outnpz, A=A, B=B)
    print("\nsaved %s" % outnpz)


if __name__ == "__main__":
    main(sys.argv[1],
         float(sys.argv[2]) if len(sys.argv) > 2 else 1.0,
         float(sys.argv[3]) if len(sys.argv) > 3 else 1.0)
