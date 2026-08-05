#!/usr/bin/env python3
"""ANGLE B5 ceiling probe: how much displaced-vs-fake separation is LEFT in the current
25/30-column chain representation, independent of the MLP trunk?

Three probes, all on the SAME frozen split as b5_gateco_train.py:
  1. big 3-class HistGBDT (600 iters, 255 leaves) -- shared-trunk ceiling
  2. BRANCH-SPECIALIST binary HistGBDTs trained ONLY on the target pool
     (exempt dca>=0.5 & nL>=5 displaced-vs-fake; nL=4 displaced-vs-fake;
      IP dca<0.5 & nL>=5 alltrue-vs-fake) -- removes any shared-trunk dilution
  3. same specialists with the M12 feature set, to separate "features" from "trunk"

A specialist that cannot beat the M12 gate on its own branch proves the deficit is
REPRESENTATIONAL (features), not capacity/architecture.
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


def auc(pos, neg):
    return tc3.auc_of(pos, neg)


def main():
    from sklearn.ensemble import HistGradientBoostingClassifier
    rng = np.random.default_rng(42)
    inputs = [f"{b5.SHARED}/chains_m12_300evt.root", f"{b5.SHARED}/chains_m12_498evt.root"]
    meta, X, src, names = b5.load_all(inputs, set())
    np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
    dca_raw = meta["dcaXY"].copy()
    cond = list(tc3.CONDITIONING_SPEC)
    for nm, _, _, ops in b5.DERIVED:
        for o in ops:
            cond.append(dict(o, feature=nm))
    tc3.apply_conditioning(X, names, cond)

    class A:
        train_frac, val_frac, pool_train_frac = 0.6, 0.2, 0.75
    tr, va, te = tc3.combined_event_split(meta, src, A, rng)
    tr = tr | va          # specialists get the full non-test pool (more displaced rows)
    is_true = meta["label"] == 1
    vxy = meta["simVxy"]
    y3 = np.zeros(len(X), dtype=np.int64)
    y3[is_true & (vxy < 1.0)] = 1
    y3[is_true & (vxy >= 1.0)] = 2
    nl = meta["nLayers"]

    m12_cols = [j for j, n in enumerate(names)
                if n != "cf_maxBridgeChi2" and not n.startswith("d_")]
    all_cols = list(range(len(names)))

    out = {}
    if os.path.exists(f"{PROTO}/b5_ceiling.json"):
        out = json.load(open(f"{PROTO}/b5_ceiling.json"))

    # ---- probe 1: big shared 3-class GBDT ----
    n0 = int((y3[tr] == 0).sum())
    pw = n0 / max(int((y3[tr] > 0).sum()), 1)
    w = np.ones(len(X), dtype=np.float32)
    w[is_true] = pw
    w[is_true & (vxy >= 1) & (vxy < 5)] = pw * 8
    w[is_true & (vxy >= 5)] = pw * 16
    t = time.time()
    clf = HistGradientBoostingClassifier(max_iter=600, learning_rate=0.06,
                                         max_leaf_nodes=255, min_samples_leaf=40,
                                         l2_regularization=1.0, max_bins=255,
                                         early_stopping=False, random_state=42)
    clf.fit(X[np.ix_(tr, all_cols)], y3[tr], sample_weight=w[tr])
    lz = np.log(np.clip(clf.predict_proba(X[np.ix_(te, all_cols)]), 1e-12, None))
    mP, mD = lz[:, 1] - lz[:, 0], lz[:, 2] - lz[:, 0]
    mX = np.maximum(lz[:, 1], lz[:, 2]) - lz[:, 0]
    res = b5.eval_ladder(mP, mD, mX, y3[te], nl[te], dca_raw[te], vxy[te])
    out["gbdt_big"] = {"train_sec": round(time.time() - t, 1), "auc": res}
    log(f"gbdt_big: mP={res['prompt-vs-fake (mP)']['auc']:.5f} "
        f"mD={res['displaced-vs-fake (mD)']['auc']:.5f} "
        f"ex5+D={res['exempt 5+ disp']['auc']:.5f} nL4D={res['nL=4 disp']['auc']:.5f} "
        f"IP5+all={res['IP 5+ all']['auc']:.5f}")
    json.dump(out, open(f"{PROTO}/b5_ceiling.json", "w"), indent=1)

    # ---- probe 2/3: branch specialists ----
    ip = (dca_raw < 0.5) & (nl >= 5)
    ex = (dca_raw >= 0.5) & (nl >= 5)
    t4 = nl <= 4
    BRANCHES = {
        "exempt5+_dispVfake": (ex, y3 == 2, y3 == 0),
        "nL4_dispVfake": (t4, y3 == 2, y3 == 0),
        "IP5+_alltrueVfake": (ip, y3 > 0, y3 == 0),
        "exempt5+_alltrueVfake": (ex, y3 > 0, y3 == 0),
    }
    for bname, (sel, pos, neg) in BRANCHES.items():
        for fs, fcols in (("m12feat", m12_cols), ("extfeat", all_cols)):
            m_tr = sel & tr & (pos | neg)
            m_te = sel & te & (pos | neg)
            ytr = pos[m_tr].astype(np.int64)
            yte = pos[m_te].astype(np.int64)
            if ytr.sum() < 50 or yte.sum() < 20:
                continue
            # inverse-frequency balance inside the branch
            wp = (~pos[m_tr]).sum() / max(pos[m_tr].sum(), 1)
            ww = np.where(ytr == 1, wp, 1.0).astype(np.float64)
            t = time.time()
            c = HistGradientBoostingClassifier(max_iter=400, learning_rate=0.06,
                                               max_leaf_nodes=127, min_samples_leaf=20,
                                               l2_regularization=1.0, max_bins=255,
                                               early_stopping=False, random_state=42)
            c.fit(X[np.ix_(m_tr, fcols)], ytr, sample_weight=ww)
            s = c.predict_proba(X[np.ix_(m_te, fcols)])[:, 1]
            a = auc(s[yte == 1], s[yte == 0])
            key = f"spec_{bname}_{fs}"
            out[key] = {"auc": a, "n_train_pos": int(ytr.sum()),
                        "n_train_neg": int((ytr == 0).sum()),
                        "n_test_pos": int(yte.sum()), "n_test_neg": int((yte == 0).sum()),
                        "train_sec": round(time.time() - t, 1)}
            log(f"{key}: AUC={a:.5f}  (train {int(ytr.sum())}+/{int((ytr==0).sum())}-, "
                f"test {int(yte.sum())}+/{int((yte==0).sum())}-)")
            json.dump(out, open(f"{PROTO}/b5_ceiling.json", "w"), indent=1)

    # ---- feature importance on the decisive branch (permutation, exempt5+ disp) ----
    sel, pos, neg = BRANCHES["exempt5+_dispVfake"]
    m_tr = sel & tr & (pos | neg)
    m_te = sel & te & (pos | neg)
    ytr = pos[m_tr].astype(np.int64)
    yte = pos[m_te].astype(np.int64)
    wp = (~pos[m_tr]).sum() / max(pos[m_tr].sum(), 1)
    c = HistGradientBoostingClassifier(max_iter=400, learning_rate=0.06,
                                       max_leaf_nodes=127, min_samples_leaf=20,
                                       l2_regularization=1.0, max_bins=255,
                                       early_stopping=False, random_state=42)
    c.fit(X[np.ix_(m_tr, all_cols)], ytr,
          sample_weight=np.where(ytr == 1, wp, 1.0).astype(np.float64))
    Xte_b = X[np.ix_(m_te, all_cols)]
    base = auc(c.predict_proba(Xte_b)[:, 1][yte == 1], c.predict_proba(Xte_b)[:, 1][yte == 0])
    imp = {}
    r2 = np.random.default_rng(7)
    for j, nm in enumerate(names):
        Xp = Xte_b.copy()
        Xp[:, j] = Xp[r2.permutation(len(Xp)), j]
        s = c.predict_proba(Xp)[:, 1]
        imp[nm] = round(base - auc(s[yte == 1], s[yte == 0]), 5)
    out["exempt5+_disp_perm_importance"] = {"base_auc": base, "drop": imp}
    log("exempt5+ disp permutation importance (AUC drop), top 10:")
    for nm, v in sorted(imp.items(), key=lambda kv: -kv[1])[:10]:
        log(f"   {nm:28s} {v:+.5f}")
    json.dump(out, open(f"{PROTO}/b5_ceiling.json", "w"), indent=1)
    log("done")


if __name__ == "__main__":
    main()
