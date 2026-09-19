#!/usr/bin/env python3
"""Rung 4: which inputs make the T5 / T4 DNN refuse true displaced objects.  For TRUE pairs with true |d0| >= 4 cm the
inputs of one feature group are replaced by those of a random TRUE pair with |d0| < 0.5 cm of the SAME layer combination;
the pass rate that comes back says how much of the refusal that group carries.    usage: dnn_why.py <t4.npz> <t5.npz>"""
import sys

import numpy as np

import dnnrep
from d0true import true_d0


def run(kind, d):
    r, tk = d["rec"], d["tk"]
    n = 4 if kind == "t4" else 5
    tm = (tk >= 0) & np.all(d["lp"] >= 0.8, axis=1) & np.all(d["prim"], axis=1) & np.isfinite(r["radii"]).all(1)
    d0 = true_d0(d, np.maximum(tk, 0))
    if kind == "t4":
        F = dnnrep.t4_features(r["xyz"].reshape(-1, 4, 3), r["radii"], r["t3Scores"])
        groups = {"first hit (eta, phi, z, r)": [0, 1, 2, 3], "delta eta": [4, 8, 12], "delta phi": [5, 9, 13], "delta z": [6, 10, 14], "delta r": [7, 11, 15],
                  "radii (5)": [16, 17, 18, 19, 20], "T3 DNN scores (9)": list(range(21, 30))}
        score = lambda X: dnnrep.t4_scores(X)
        passed = lambda S, rr: (S[:, 2] > rr["wpDisp"]) & (S[:, 0] < rr["wpFake"])
    else:
        F = dnnrep.t5_features(r["xyz"].reshape(-1, 5, 3), r["radii"])
        groups = {"first hit (eta, phi, z, r)": [0, 1, 2, 3], "delta eta": [4, 8, 12, 16], "delta phi": [5, 9, 13, 17], "delta z": [6, 10, 14, 18],
                  "delta r": [7, 11, 15, 19], "radii (3)": [20, 21, 22]}
        score = lambda X: dnnrep.t5_score(X)
        passed = lambda S, rr: S > rr["wp98"]
    L = r["lstLayers"].astype(np.int64)
    code = np.zeros(len(r), np.int64)
    for k in range(n):
        code = code * 20 + L[:, k]
    big = np.nonzero(tm & (d0 >= 4))[0]
    small = tm & (d0 < 0.5)
    rng = np.random.default_rng(7)
    donor = np.full(len(big), -1)
    for c in np.unique(code[big]):
        pool = np.nonzero(small & (code == c))[0]
        m = code[big] == c
        if len(pool) >= 20:
            donor[m] = rng.choice(pool, m.sum())
    keep = donor >= 0
    big, donor = big[keep], donor[keep]
    base = passed(score(F[big]), r[big])
    print("\n## %s DNN: %d true all-primary pairs with true |d0| >= 4 cm (with a donor pool); pass rate as is %.4f; of the small-d0 donors %.4f"
          % (kind.upper(), len(big), base.mean(), passed(score(F[donor]), r[donor]).mean()))
    print("| inputs replaced by a small-d0 true pair's (same layers) | pass rate | refusals recovered |")
    print("|---|---|---|")
    for g, idx in groups.items():
        X = F[big].copy(); X[:, idx] = F[donor][:, idx]
        p = passed(score(X), r[big])
        print("| %s | %.4f | %.3f |" % (g, p.mean(), (p.mean() - base.mean()) / max(1 - base.mean(), 1e-9)))
    # how the inputs themselves move with d0
    print("| mean of |input| by true d0: | < 0.5 cm | 4-8 cm | 8-16 cm |")
    for g, idx in groups.items():
        print("| %s | %s |" % (g, " | ".join("%.4f" % np.abs(F[tm & (d0 >= lo) & (d0 < hi)][:, idx]).mean() for lo, hi in ((0, 0.5), (4, 8), (8, 16)))))


def main():
    for kind, p in (("t4", sys.argv[1]), ("t5", sys.argv[2])):
        d = np.load(p, allow_pickle=True)
        run(kind, {k: d[k] for k in d.files})


if __name__ == "__main__":
    main()
