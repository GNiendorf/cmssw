#!/usr/bin/env python3
"""C2 diagnostic: WHY E2 (shared-LS) edges cap out ~0.92 AUC.

Per-type single-feature AUCs on the frozen TEST rows (subsampled), plus the
inner/outer feature-redundancy measure that explains the deficit: for E2 the two
T3s share 2 of 3 MDs, so the inner/outer node blocks are nearly the same object and
the "consistency" features (dKappa, centerDist, kinks) carry almost no independent
information.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import train_edge as te

CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "edgecache.npz")


class A:  # arg shim for combined_event_split
    train_frac = 0.6
    val_frac = 0.2
    pool_train_frac = 0.75


def main():
    from sklearn.metrics import roc_auc_score
    z = np.load(CACHE, allow_pickle=True)
    X = z["X"]
    names = [str(n) for n in z["names"]]
    meta = {k: z["m_" + k] for k in te.META_BRANCHES}
    src = z["src"]
    rng = np.random.default_rng(42)
    tr, va, te_m = te.combined_event_split(meta, src, A(), rng)
    idx = np.flatnonzero(te_m)
    sub = rng.choice(idx, size=min(1500000, len(idx)), replace=False)
    Xt = X[sub]
    lab = (meta["label"][sub] == 1)
    ety = meta["etype"][sub]
    print(f"test subsample {len(sub)} rows: E1 {int((ety==1).sum())} E2 {int((ety==2).sum())}")

    rows = []
    for j, nm in enumerate(names):
        r = {"feat": nm}
        for et, tag in ((1, "E1"), (2, "E2")):
            m = ety == et
            v = Xt[m, j].astype(np.float64)
            y = lab[m]
            if v.std() < 1e-9 or y.all() or not y.any():
                r[tag] = 0.5
            else:
                a = roc_auc_score(y, v)
                r[tag] = max(a, 1 - a)  # direction-free separation power
        rows.append(r)
    rows.sort(key=lambda r: -r["E2"])
    print(f"\n{'feature':>20} {'E2 |AUC|':>9} {'E1 |AUC|':>9} {'E2-E1':>8}")
    for r in rows:
        print(f"{r['feat']:>20} {r['E2']:>9.4f} {r['E1']:>9.4f} {r['E2'] - r['E1']:>8.4f}")

    # inner/outer node-block redundancy: |corr(ni_f, no_f)| per node feature
    print(f"\n{'node feature':>20} {'|corr| E1':>10} {'|corr| E2':>10}")
    nnode = 13
    for j in range(nnode):
        nm = names[j][3:]
        out = []
        for et in (1, 2):
            m = ety == et
            a = Xt[m, j].astype(np.float64)
            b = Xt[m, nnode + j].astype(np.float64)
            if a.std() < 1e-9 or b.std() < 1e-9:
                out.append(float("nan"))
            else:
                out.append(abs(float(np.corrcoef(a, b)[0, 1])))
        print(f"{nm:>20} {out[0]:>10.4f} {out[1]:>10.4f}")

    # class-prior / population context
    for et, tag in ((1, "E1"), (2, "E2")):
        m = ety == et
        print(f"{tag}: n={int(m.sum())} true_frac={lab[m].mean():.4f} "
              f"displaced(vxy>=1) true frac={(meta['simVxy'][sub][m & lab] >= 1).mean():.4f}")


if __name__ == "__main__":
    main()
