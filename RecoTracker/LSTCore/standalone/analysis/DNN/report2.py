#!/usr/bin/env python3
"""[DISP2] Choose the epsilon point and emit the delivered 18-bar table.

Adds the one thing the raw scan cannot see: a WP quantile is an ORDER STATISTIC, and at
eps >= 1 - 1/n_cell the "quantile" degenerates to the single lowest true pair in the cell. Such a
bar is an outlier, not a working point, and it is exactly the overfitting the brief forbids. So the
recommendation is taken on the ROBUST frontier -- every one of the 18 cells must have
k = floor((1-eps)*n) >= KMIN -- with the unconstrained optimum reported beside it for transparency.

Usage: report2.py <extract.npz> <fit.json> <outprefix>
"""
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import census_disp as C          # noqa: E402
from fitbars import Replay, wp_bars, PT_EDGES, N_PT, N_ETA, ETA_NAMES, PT_NAMES  # noqa: E402

KMIN = 2          # every cell's bar must be at least the 3rd-lowest true pair of that cell
GUARD = 2.0       # wrong-seed deliveries at most this multiple of the shipped count
ROBUST = [0.95, 0.96, 0.97, 0.975, 0.98, 0.985, 0.99]


def main():
    z = np.load(sys.argv[1])
    fit = json.load(open(sys.argv[2]))
    pref = sys.argv[3]
    R = Replay(z)
    fitmask = R.istrue & ~R.disp
    tl, tc = R.logit[fitmask], R.cell[fitmask]

    s_ev = z["s_ev"].astype(np.int64)
    s_chain = z["s_chain"].astype(np.int64)
    s_pt = z["s_pt"].astype(np.float64)
    s_eta = np.abs(z["s_eta"].astype(np.float64))
    s_our = z["s_our"].astype(np.int64)
    s_mast = z["s_mast"].astype(np.int64)
    s_gt = s_ev * (1 << 17) + s_chain
    fam = np.isin(s_our, (4, 7)) & (s_chain >= 0)
    mfam = np.isin(s_mast, (4, 7))
    s_eb = C.seed_eta_band(s_eta)
    s_cell = s_eb * N_PT + np.digitize(s_pt, PT_EDGES)
    ncell = np.array(fit["scan1"][0]["n_cell"])

    mast_eta = np.array([(s_mast[mfam & (s_eb == e)] == 7).mean() for e in range(N_ETA)])
    mast_cell = np.array([(s_mast[mfam & (s_cell == c)] == 7).mean()
                          if (mfam & (s_cell == c)).any() else np.nan
                          for c in range(N_ETA * N_PT)])
    mast_barrel, mast_out = mast_eta[0], (s_mast[mfam & (s_eb > 0)] == 7).mean()

    def evaluate(bars):
        dv = R.run(np.where(np.isnan(bars), np.inf, bars))
        gtu = np.unique(R.gt[dv])
        d = np.isin(s_gt[fam], gtu)
        eb, cl = s_eb[fam], s_cell[fam]
        return dict(
            wrong=int((~R.istrue[dv]).sum()),
            wrong_eta=np.bincount(R.ecell[dv][~R.istrue[dv]], minlength=3).tolist(),
            true=int(R.istrue[dv].sum()),
            share_eta=[float(d[eb == e].mean()) for e in range(N_ETA)],
            share_cell=[float(d[cl == c].mean()) if (cl == c).any() else float("nan")
                        for c in range(N_ETA * N_PT)],
            share_barrel=float(d[eb == 0].mean()), share_out=float(d[eb > 0].mean()),
            n_fam_eta=[int((eb == e).sum()) for e in range(N_ETA)])

    ship = np.array(fit["base"]["bars"])
    base = evaluate(ship)
    bw = base["wrong"]
    score = lambda m: abs(m["share_barrel"] - mast_barrel) + abs(m["share_out"] - mast_out)

    def kmin(eps_by_eta):
        return min(int(np.floor((1 - eps_by_eta[c // N_PT]) * ncell[c]))
                   for c in range(N_ETA * N_PT))

    rows = []
    for nz, combos in ((1, [(a, a, a) for a in ROBUST]),
                       (2, [(a, b, b) for a in ROBUST for b in ROBUST]),
                       (3, [(a, b, c) for a in ROBUST for b in ROBUST for c in ROBUST])):
        for cb in combos:
            if kmin(cb) < KMIN:
                continue
            bars, ns = wp_bars(tl, tc, list(cb))
            m = evaluate(bars)
            if m["wrong"] / bw > GUARD:
                continue
            rows.append(dict(nz=nz, eps=list(cb), bars=bars.tolist(), kmin=kmin(cb),
                             score=score(m), wrong_mult=m["wrong"] / bw, **m))
    best = {nz: min([r for r in rows if r["nz"] == nz], key=lambda r: r["score"]) for nz in (1, 2, 3)}
    for nz in (1, 2, 3):
        b = best[nz]
        print("%d-zone ROBUST best: eps %-24s barrel %.4f out %.4f  wrong x%.2f  kmin %d  score %.4f"
              % (nz, str(b["eps"]), b["share_barrel"], b["share_out"], b["wrong_mult"],
                 b["kmin"], b["score"]))

    # recommendation: the smallest zone count within 0.002 of the best score available
    bestscore = min(best[nz]["score"] for nz in (1, 2, 3))
    rec = next(nz for nz in (1, 2, 3) if best[nz]["score"] <= bestscore + 0.002)
    out = dict(master=dict(share_eta=mast_eta.tolist(), share_cell=mast_cell.tolist(),
                           share_barrel=float(mast_barrel), share_out=float(mast_out)),
               base=dict(bars=ship.tolist(), **base),
               n_cell=ncell.tolist(), robust_best={str(k): v for k, v in best.items()},
               recommend_nz=rec, kmin=KMIN, guard=GUARD,
               unconstrained=dict(one=fit["best1"], two=fit["best2"], three=fit["best3"]),
               scan1=fit["scan1"])
    json.dump(out, open(pref + ".json", "w"), indent=1)
    print("recommend %d-zone; wrote %s.json" % (rec, pref))


if __name__ == "__main__":
    main()
