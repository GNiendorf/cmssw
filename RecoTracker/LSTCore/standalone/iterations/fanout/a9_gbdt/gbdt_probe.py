#!/usr/bin/env python3
"""ANGLE 9 - REPRESENTATION CEILING PROBE.

Trains sklearn HistGradientBoostingClassifier on the EXISTING chain dumps
(M8 combination rule, frozen test-60 preserved bit-for-bit vs train_chain.py)
and compares AUCs against the MLP v3 chain gate, to answer:

  GBDT >> MLP  -> the MLP capacity/training is the limit (capacity angle wins)
  GBDT ~= MLP  -> the FEATURES are the limit (feature angles win)
  GBDT+extras still cannot separate displaced-true from fake (AUC < ~0.85)
               -> formation-side / new-information solutions win

No C++ changes; pure analysis on prototype/chains_*.root (read-only).

Split reproduction: train_chain.py combined_event_split with seed 42,
train_frac 0.6, val_frac 0.2, pool_train_frac 0.75, primary =
chains_300evt_simidx.root, extra = chains_498evt.root. Verified by matching the
v3 test row count (273549) from train_chain_v3.log.
"""

import argparse
import json
import os
import time

import numpy as np

T0 = time.time()
PROTO = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype"
MYDIR = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout/a9_gbdt"
N_CF = 16
META = ["evt", "label", "simVxy", "simPt", "nLayers"]


def log(m):
    print(f"[{time.time() - T0:7.1f}s] {m}", flush=True)


# ------------------------------------------------------------------ data
def load_dump(path):
    import uproot
    f = uproot.open(path)
    spec = f["feature_spec"].member("fTitle")
    assert spec.startswith("cf:")
    names = ["cf_" + n for n in spec[3:].split(",")]
    assert len(names) == N_CF
    tree = f["chains"]
    br = [f"cf_{i:02d}" for i in range(N_CF)]
    arr = tree.arrays(META + br, library="np")
    meta = {k: arr[k] for k in META}
    X = np.empty((len(meta["label"]), N_CF), dtype=np.float32)
    for j, b in enumerate(br):
        X[:, j] = arr[b]
        del arr[b]
    return meta, X, names


def build_cache(cache):
    prim = f"{PROTO}/chains_300evt_simidx.root"
    extra = f"{PROTO}/chains_498evt.root"
    m0, X0, names = load_dump(prim)
    prim_evts = np.unique(m0["evt"])
    log(f"primary {len(X0)} chains / {len(prim_evts)} evts")
    m1, X1, n1 = load_dump(extra)
    assert n1 == names
    keep = ~np.isin(m1["evt"], prim_evts)
    m1 = {k: v[keep] for k, v in m1.items()}
    X1 = X1[keep]
    log(f"extra kept {len(X1)} chains / {len(np.unique(m1['evt']))} evts")
    meta = {k: np.concatenate([m0[k], m1[k]]) for k in META}
    X = np.concatenate([X0, X1])
    src = np.concatenate([np.zeros(len(X0), np.int8), np.ones(len(X1), np.int8)])

    # ---- EXACT train_chain.py combined_event_split (seed 42) ----
    rng = np.random.default_rng(42)
    key = meta["evt"].astype(np.uint64)
    uniq0 = np.unique(key[src == 0])
    rng.shuffle(uniq0)
    n0 = len(uniq0)
    n_tr0 = int(round(0.6 * n0))
    n_va0 = int(round(0.2 * n0))
    te_keys = uniq0[n_tr0 + n_va0:]
    te = np.isin(key, te_keys)
    assert not (te & (src != 0)).any()
    pool_keys = np.unique(key[~te])
    rng.shuffle(pool_keys)
    n_ptr = int(round(0.75 * len(pool_keys)))
    tr = np.isin(key, pool_keys[:n_ptr])
    va = np.isin(key, pool_keys[n_ptr:])
    log(f"split rows train/val/test = {tr.sum()}/{va.sum()}/{te.sum()} "
        f"(v3 log says 273549 test)")
    np.savez_compressed(cache, X=X, names=np.array(names), tr=tr, va=va, te=te,
                        **{k: meta[k] for k in META})
    log(f"cached -> {cache}")


def load_cache(cache):
    z = np.load(cache, allow_pickle=False)
    names = [str(s) for s in z["names"]]
    meta = {k: z[k] for k in META}
    return meta, z["X"], names, z["tr"], z["va"], z["te"]


# ------------------------------------------------- engineered extras
def add_extras(X, names):
    """Ratios / abs / interactions that axis-aligned trees cannot form.
    (Monotone transforms - logs, clips - are NO-OPS for trees, so none here.)"""
    c = {n: X[:, j] for j, n in enumerate(names)}
    eps = 1e-6
    kap = c["cf_fitKappa"]
    akap = np.abs(kap)
    dkap = c["cf_dKappaFitVsMedianT3"]
    nN = c["cf_nNodes"]
    nL = np.maximum(c["cf_nLayers"], 1.0)
    span = np.maximum(c["cf_layerSpan"], 1.0)
    pt = np.maximum(c["cf_ptEst"], eps)
    chi2 = c["cf_fullFitChi2PerHit"]
    rz = c["cf_rzLineChi2PerHit"]
    ex = {
        "x_absFitKappa": akap,
        "x_absDKappa": np.abs(dkap),
        "x_dKappaRel": dkap / (akap + eps),
        "x_ptCurvRatio": pt * akap / 0.0114,          # ptEst / pt(fit radius)
        "x_chi2xPt2": chi2 * pt * pt,                 # MS-normalized xy residual
        "x_rzChi2xPt2": rz * pt * pt,
        "x_chi2Ratio": chi2 / (rz + eps),
        "x_edgeSpread": c["cf_meanEdgeLogit"] - c["cf_minEdgeLogit"],
        "x_nodesPerLayer": nN / nL,
        "x_layerFill": c["cf_nLayers"] / span,        # 1 = no missing layer
        "x_outerLayer": c["cf_innermostLayer"] + c["cf_layerSpan"],
        "x_psFrac": c["cf_nPS"] / nL,
        "x_barrelFrac": c["cf_nBarrel"] / nL,
        "x_degPerNode": c["cf_maxJunctionDegProduct"] / nN,
        "x_minLogitPerLayer": c["cf_minEdgeLogit"] / nL,
        "x_sumLogitMinusLen": c["cf_sumEdgeLogit"] - 0.5 * c["cf_nLayers"],  # legacy K9 score
    }
    Xe = np.column_stack([X] + [v.astype(np.float32) for v in ex.values()])
    return np.ascontiguousarray(Xe, dtype=np.float32), names + list(ex.keys())


# ------------------------------------------------- evaluation
def auc(y, s):
    from sklearn.metrics import roc_auc_score
    if y.sum() == 0 or y.sum() == len(y):
        return None
    return float(roc_auc_score(y, s))


def strat_auc(s_true, s_fake):
    if len(s_true) == 0 or len(s_fake) == 0:
        return None
    y = np.concatenate([np.ones(len(s_true)), np.zeros(len(s_fake))])
    return auc(y, np.concatenate([s_true, s_fake]))


def report(tag, s, meta, mask, extra_masks=None):
    """Same strata as train_chain.py eval_block, on the rows selected by mask."""
    lab = meta["label"][mask] == 1
    vxy = meta["simVxy"][mask]
    nl = meta["nLayers"][mask]
    st, sf = s[lab], s[~lab]
    r = {"all": strat_auc(st, sf),
         "nL4": strat_auc(s[lab & (nl == 4)], s[~lab & (nl == 4)]),
         "nL5": strat_auc(s[lab & (nl == 5)], s[~lab & (nl == 5)]),
         "nL6p": strat_auc(s[lab & (nl >= 6)], s[~lab & (nl >= 6)]),
         "prompt_vxy<1": strat_auc(s[lab & (vxy < 1)], sf),
         "disp_vxy>=1": strat_auc(s[lab & (vxy >= 1)], sf),
         "disp_vxy>=5": strat_auc(s[lab & (vxy >= 5)], sf),
         # branch-localized (M9 discovery (3)/(5)): the fake lives in 5+, the
         # displaced upside in T4-class
         "disp>=1_nL>=5": strat_auc(s[lab & (vxy >= 1) & (nl >= 5)], s[~lab & (nl >= 5)]),
         "disp>=5_nL>=5": strat_auc(s[lab & (vxy >= 5) & (nl >= 5)], s[~lab & (nl >= 5)]),
         "disp>=1_nL4": strat_auc(s[lab & (vxy >= 1) & (nl == 4)], s[~lab & (nl == 4)]),
         "prompt_nL>=5": strat_auc(s[lab & (vxy < 1) & (nl >= 5)], s[~lab & (nl >= 5)]),
         "n_true": int(lab.sum()), "n_fake": int((~lab).sum()),
         "n_disp1": int((lab & (vxy >= 1)).sum()),
         "n_disp5": int((lab & (vxy >= 5)).sum())}
    print(f"\n=== {tag} ===")
    for k, v in r.items():
        if k.startswith("n_"):
            print(f"  {k:>16} {v}")
        else:
            print(f"  {k:>16} {v if v is None else f'{v:.5f}'}")
    return r


# ------------------------------------------------- models
def fit_hgb(Xtr, ytr, wtr, max_iter=300, lr=0.1, leaves=31, l2=1.0, seed=42,
            classes=None):
    from sklearn.ensemble import HistGradientBoostingClassifier
    m = HistGradientBoostingClassifier(max_iter=max_iter, learning_rate=lr,
                                       max_leaf_nodes=leaves, l2_regularization=l2,
                                       early_stopping=False, random_state=seed)
    t = time.time()
    m.fit(Xtr, ytr, sample_weight=wtr)
    log(f"  fit {max_iter} iters on {Xtr.shape} in {time.time() - t:.1f}s")
    return m


def score_binary(m, X):
    return m.decision_function(X).astype(np.float32)


def tiered_weights(meta, mask, pos_weight=True):
    """v3 chain-gate weighting: pos_weight = n_fake/n_true, displaced true
    x8 ([1,5) cm) / x16 (>=5 cm)."""
    lab = meta["label"][mask] == 1
    vxy = meta["simVxy"][mask]
    w = np.ones(int(mask.sum()), np.float32)
    w[lab & (vxy >= 1) & (vxy < 5)] = 8.0
    w[lab & (vxy >= 5)] = 16.0
    if pos_weight:
        pw = (~lab).sum() / max(lab.sum(), 1)
        w[lab] *= pw
    return w


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", default=f"{MYDIR}/chains_cache.npz")
    ap.add_argument("--stage", default="all")
    ap.add_argument("--max-iter", type=int, default=300)
    ap.add_argument("--out", default=f"{MYDIR}/gbdt_results.json")
    a = ap.parse_args()

    if not os.path.exists(a.cache):
        build_cache(a.cache)
        if a.stage == "cache":
            return
    meta, X16, names, tr, va, te = load_cache(a.cache)
    log(f"loaded {X16.shape} rows={len(X16)} tr/va/te={tr.sum()}/{va.sum()}/{te.sum()}")
    Xex, names_ex = add_extras(X16, names)
    log(f"extras -> {Xex.shape[1]} features")

    y = (meta["label"] == 1).astype(np.int32)
    vxy = meta["simVxy"]
    nl = meta["nLayers"]
    res = {"split": {"n_train": int(tr.sum()), "n_val": int(va.sum()),
                     "n_test": int(te.sum())},
           "mlp_v3_reference": {"all": 0.90973, "nL4": 0.86612, "nL5": 0.90785,
                                "nL6p": 0.88255, "prompt_vxy<1": 0.91089,
                                "disp_vxy>=1": 0.82182, "disp_vxy>=5": 0.80329},
           "models": {}}

    def run(tag, Xall, ytr_sel, rows_tr, w_tr, rows_eval=None, **kw):
        Xtr = Xall[rows_tr]
        m = fit_hgb(Xtr, ytr_sel, w_tr, max_iter=a.max_iter, **kw)
        ev = te if rows_eval is None else (te & rows_eval)
        s = score_binary(m, Xall[ev])
        res["models"][tag] = report(tag, s, meta, ev)
        res["models"][tag]["n_train_rows"] = int(rows_tr.sum())
        res["models"][tag]["n_feat"] = int(Xall.shape[1])
        return m, ev, s

    # ---- A: binary true-vs-fake, cf16, v3-style tiered weights -------------
    if a.stage in ("all", "binary"):
        run("A_bin_cf16_w", X16, y[tr], tr, tiered_weights(meta, tr))
        run("B_bin_ext_w", Xex, y[tr], tr, tiered_weights(meta, tr))
        run("C_bin_cf16_unw", X16, y[tr], tr, None)
        run("D_bin_ext_unw", Xex, y[tr], tr, None)

    # ---- E/F: displaced-true-vs-fake only ---------------------------------
    if a.stage in ("all", "disp"):
        # positives = true & vxy>=1, negatives = ALL fakes; prompt trues dropped
        sel = tr & ((y == 1) & (vxy >= 1) | (y == 0))
        w = np.ones(int(sel.sum()), np.float32)
        lab = y[sel] == 1
        w[lab] = (~lab).sum() / max(lab.sum(), 1)
        run("E_disp_cf16", X16, y[sel], sel, w)
        run("F_disp_ext", Xex, y[sel], sel, w)
        # branch-localized: 5+ layer only (M9: 85% of residual fake)
        sel5 = sel & (nl >= 5)
        lab5 = y[sel5] == 1
        w5 = np.ones(int(sel5.sum()), np.float32)
        w5[lab5] = (~lab5).sum() / max(lab5.sum(), 1)
        run("G_disp_ext_nL5p", Xex, y[sel5], sel5, w5, rows_eval=(nl >= 5))
        # branch-localized: T4-class only (M9: displaced upside)
        sel4 = sel & (nl <= 4)
        lab4 = y[sel4] == 1
        w4 = np.ones(int(sel4.sum()), np.float32)
        w4[lab4] = (~lab4).sum() / max(lab4.sum(), 1)
        run("H_disp_ext_nL4", Xex, y[sel4], sel4, w4, rows_eval=(nl <= 4))

    # ---- I: 3-class (fake / prompt-true / displaced-true) ------------------
    if a.stage in ("all", "three"):
        cls = np.zeros(len(y), np.int32)
        cls[(y == 1) & (vxy < 1)] = 1
        cls[(y == 1) & (vxy >= 1)] = 2
        w3 = np.ones(int(tr.sum()), np.float32)
        c_tr = cls[tr]
        for k in (1, 2):
            n_k = max((c_tr == k).sum(), 1)
            w3[c_tr == k] = (c_tr == 0).sum() / n_k
        from sklearn.ensemble import HistGradientBoostingClassifier
        m3 = HistGradientBoostingClassifier(max_iter=a.max_iter, learning_rate=0.1,
                                            max_leaf_nodes=31, l2_regularization=1.0,
                                            early_stopping=False, random_state=42)
        t = time.time()
        m3.fit(Xex[tr], c_tr, sample_weight=w3)
        log(f"  3-class fit in {time.time() - t:.1f}s")
        P = m3.predict_proba(Xex[te])
        # displaced score: P(disp) / (P(disp) + P(fake)) - the 3-class gate's
        # displaced-vs-fake decision axis
        s_disp = (P[:, 2] / np.maximum(P[:, 2] + P[:, 0], 1e-9)).astype(np.float32)
        s_true = (1.0 - P[:, 0]).astype(np.float32)
        res["models"]["I_3class_ext_trueaxis"] = report("I_3class_ext (P(true)=1-P(fake))",
                                                       s_true, meta, te)
        res["models"]["I_3class_ext_dispaxis"] = report("I_3class_ext (disp axis)",
                                                       s_disp, meta, te)

    # ---- J: CAPACITY SWEEP (is the GBDT itself saturated?) ----------------
    if a.stage in ("all", "cap"):
        w_tr = tiered_weights(meta, tr)
        for tag, kw in (("J1_600x63_lr06", dict(max_iter=600, leaves=63, lr=0.06)),
                        ("J2_1200x127_lr04", dict(max_iter=1200, leaves=127, lr=0.04))):
            mi = kw.pop("max_iter")
            m = fit_hgb(Xex[tr], y[tr], w_tr, max_iter=mi,
                        lr=kw["lr"], leaves=kw["leaves"])
            s_te = score_binary(m, Xex[te])
            r = report(tag, s_te, meta, te)
            s_va = score_binary(m, Xex[va])
            r["val_all"] = strat_auc(s_va[meta["label"][va] == 1],
                                     s_va[meta["label"][va] != 1])
            r["val_disp>=1"] = strat_auc(
                s_va[(meta["label"][va] == 1) & (meta["simVxy"][va] >= 1)],
                s_va[meta["label"][va] != 1])
            print(f"  {'val_all':>16} {r['val_all']:.5f}\n"
                  f"  {'val_disp>=1':>16} {r['val_disp>=1']:.5f}")
            r["n_train_rows"], r["n_feat"] = int(tr.sum()), int(Xex.shape[1])
            res["models"][tag] = r

    # ---- K: PERMUTATION IMPORTANCE for displaced-vs-fake -------------------
    if a.stage in ("all", "imp"):
        sel = tr & (((y == 1) & (vxy >= 1)) | (y == 0))
        lab = y[sel] == 1
        w = np.ones(int(sel.sum()), np.float32)
        w[lab] = (~lab).sum() / max(lab.sum(), 1)
        rng = np.random.default_rng(7)
        for tag, brmask in (("disp_all", np.ones(len(y), bool)),
                            ("disp_nL5p", nl >= 5), ("disp_nL4", nl <= 4)):
            s_sel = sel & brmask
            l2 = y[s_sel] == 1
            w2 = np.ones(int(s_sel.sum()), np.float32)
            w2[l2] = (~l2).sum() / max(l2.sum(), 1)
            m = fit_hgb(Xex[s_sel], y[s_sel], w2, max_iter=a.max_iter)
            ev = te & brmask
            Xe = np.array(Xex[ev])
            labe = meta["label"][ev] == 1
            vxe = meta["simVxy"][ev]
            pos = labe & (vxe >= 1)
            neg = ~labe
            base_s = score_binary(m, Xe)
            base = strat_auc(base_s[pos], base_s[neg])
            imps = []
            for j, nm in enumerate(names_ex):
                col = Xe[:, j].copy()
                drops = []
                for rep in range(3):
                    Xe[:, j] = rng.permutation(col)
                    s = score_binary(m, Xe)
                    drops.append(base - strat_auc(s[pos], s[neg]))
                Xe[:, j] = col
                imps.append((nm, float(np.mean(drops)), float(np.std(drops))))
            imps.sort(key=lambda t: -t[1])
            print(f"\n=== permutation importance ({tag}) base AUC(disp>=1 vs fake) "
                  f"= {base:.5f}, n_pos={int(pos.sum())} n_neg={int(neg.sum())} ===")
            for nm, d, sd in imps:
                print(f"  {nm:>26} dAUC={d:+.5f} +-{sd:.5f}")
            res.setdefault("importance", {})[tag] = {
                "base_auc": base, "n_pos": int(pos.sum()), "n_neg": int(neg.sum()),
                "ranking": [{"feature": nm, "dAUC": d, "sd": sd} for nm, d, sd in imps]}

    with open(a.out, "w") as fh:
        json.dump(res, fh, indent=1)
    log(f"wrote {a.out}")


if __name__ == "__main__":
    main()
