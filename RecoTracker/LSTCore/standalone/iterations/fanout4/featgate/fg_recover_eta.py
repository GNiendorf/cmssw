#!/usr/bin/env python3
"""Diagnostic: is the resident 25-input gate ACTUALLY blind to the barrel/endcap transition?

The featgate hypothesis assumes the gate cannot see eta because kChainFeat carries no |eta|
and no barrel FRACTION. But the contract does carry innermostLayer, layerSpan, nPS, nBarrel,
rzLineChi2PerHit, maxRzResid, ptEst and nNodes -- a longitudinal-geometry basis. If |eta|
and barrelMDFrac are recoverable from those columns, the "blind" premise is false and the
explicit features are redundant by construction, which would explain a null retrain result.

Regresses |chainEta| and barrelMDFrac on the resident's own 25 conditioned inputs
(train rows only for fitting, test rows for R^2) with (i) ordinary least squares and
(ii) a gradient-boosted tree on a subsample for the non-linear ceiling.
"""

import argparse
import os

import numpy as np

import train_chain3_fg as T


def r2(y, yhat):
    return 1.0 - float(np.sum((y - yhat) ** 2) / np.sum((y - y.mean()) ** 2))


def main():
    d = os.path.dirname(os.path.abspath(__file__))
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", nargs="+",
                   default=[f"{d}/chains_fg_300evt.root", f"{d}/chains_fg_498evt.root"])
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--sub", type=int, default=400000, help="rows for the GBT fit")
    args = p.parse_args()

    rng = np.random.default_rng(args.seed)
    metas, x_parts, src_parts = [], [], []
    names = prim = None
    for si, path in enumerate(args.input):
        m_i, X_i, n_i = T.load_dump(path, {"maxBridgeChi2"})
        if si == 0:
            names, prim = n_i, np.unique(m_i["evt"])
        else:
            keep = ~np.isin(m_i["evt"], prim)
            m_i = {k: v[keep] for k, v in m_i.items()}
            X_i = X_i[keep]
        metas.append(m_i)
        x_parts.append(X_i)
        src_parts.append(np.full(len(X_i), si, dtype=np.int8))
    keys = T.META_BRANCHES + T.OPT_BRANCHES + T.FG_BRANCHES
    meta = {k: np.concatenate([m[k] for m in metas]) for k in keys}
    X = np.concatenate(x_parts)
    src = np.concatenate(src_parts)
    del metas, x_parts, src_parts
    np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
    T.apply_conditioning(X, names, T.CONDITIONING_SPEC)

    class A:
        pass
    a = A()
    a.train_frac, a.val_frac, a.pool_train_frac = 0.6, 0.2, 0.75
    tr, va, te = T.combined_event_split(meta, src, a, rng)
    fit = tr | va

    mu = X[fit].mean(axis=0, dtype=np.float64)
    sd = X[fit].std(axis=0, dtype=np.float64)
    sd[sd < 1e-8] = 1.0
    Z = ((X - mu) / sd).astype(np.float32)
    Zb = np.concatenate([Z, np.ones((len(Z), 1), np.float32)], axis=1)

    targets = {"|chainEta|": np.abs(meta["chainEta"]).astype(np.float32),
               "barrelMDFrac": meta["barrelMDFrac"].astype(np.float32),
               "chainTanLambda": meta["chainTanLambda"].astype(np.float32)}
    print(f"basis = the resident's {len(names)} conditioned inputs; "
          f"fit rows {int(fit.sum())}, test rows {int(te.sum())}\n")
    from sklearn.ensemble import HistGradientBoostingRegressor
    idx = np.flatnonzero(fit)
    rng.shuffle(idx)
    idx = idx[:args.sub]
    for tag, y in targets.items():
        beta, *_ = np.linalg.lstsq(Zb[fit], y[fit], rcond=None)
        lin = r2(y[te], Zb[te] @ beta)
        g = HistGradientBoostingRegressor(max_iter=200, random_state=args.seed)
        g.fit(Z[idx], y[idx])
        pred = g.predict(Z[te])
        nl = r2(y[te], pred)
        rms = float(np.sqrt(np.mean((y[te] - pred) ** 2)))
        print(f"  {tag:>16}  R^2(OLS) = {lin:.4f}   R^2(GBT, {len(idx)} rows) = {nl:.4f}"
              f"   resid RMS = {rms:.4f}  (target sd = {float(y[te].std()):.4f})")

    # Can the resident basis tell "is this chain in the transition band" at all?
    from sklearn.ensemble import HistGradientBoostingClassifier
    from sklearn.metrics import roc_auc_score
    ae = np.abs(meta["chainEta"])
    inband = ((ae >= 1.1) & (ae < 1.7)).astype(np.int32)
    c = HistGradientBoostingClassifier(max_iter=200, random_state=args.seed)
    c.fit(Z[idx], inband[idx])
    pr = c.predict_proba(Z[te])[:, 1]
    print(f"\n  band-membership (|eta| in [1.1,1.7)) from the SAME 25 inputs: "
          f"AUC = {roc_auc_score(inband[te], pr):.4f}  "
          f"(prevalence {inband[te].mean():.4f})")


if __name__ == "__main__":
    main()
