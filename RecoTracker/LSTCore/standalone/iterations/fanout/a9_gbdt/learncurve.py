#!/usr/bin/env python3
"""ANGLE 9 part 3: LEARNING CURVE at GBDT capacity - is the displaced-vs-fake
ceiling a DATA limit or a REPRESENTATION limit? M8 measured saturation with the
MLP (2.5x data -> +0.005 AUC); this repeats it at higher capacity with the
3-class objective. Event-level subsampling of the TRAIN pool only; frozen
test-60 untouched."""
import json
import sys
import time

import numpy as np

sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout/a9_gbdt")
from gbdt_probe import load_cache, add_extras, strat_auc, MYDIR  # noqa: E402
from sklearn.ensemble import HistGradientBoostingClassifier  # noqa: E402

T0 = time.time()
meta, X16, names, tr, va, te = load_cache(f"{MYDIR}/chains_cache.npz")
Xex, _ = add_extras(X16, names)
y = (meta["label"] == 1).astype(np.int32)
vxy, nl = meta["simVxy"], meta["nLayers"]
cls = np.zeros(len(y), np.int32)
cls[(y == 1) & (vxy < 1)] = 1
cls[(y == 1) & (vxy >= 1)] = 2

evts_tr = np.unique(meta["evt"][tr])
rng = np.random.default_rng(11)
rng.shuffle(evts_tr)
lab_te, vx_te, nl_te = meta["label"][te] == 1, vxy[te], nl[te]
out = []
for frac in (0.125, 0.25, 0.5, 1.0):
    k = max(int(round(frac * len(evts_tr))), 1)
    sub = tr & np.isin(meta["evt"], evts_tr[:k])
    c = cls[sub]
    w = np.ones(int(sub.sum()), np.float32)
    n0 = (c == 0).sum()
    for kk in (1, 2):
        w[c == kk] = n0 / max((c == kk).sum(), 1)
    m = HistGradientBoostingClassifier(max_iter=300, learning_rate=0.1, max_leaf_nodes=31,
                                       l2_regularization=1.0, early_stopping=False,
                                       random_state=42)
    m.fit(Xex[sub], c, sample_weight=w)
    P = m.predict_proba(Xex[te])
    s_true = 1.0 - P[:, 0]
    s_disp = P[:, 2] / np.maximum(P[:, 2] + P[:, 0], 1e-9)
    b5 = nl_te >= 5
    r = {"frac": frac, "n_events": int(k), "n_rows": int(sub.sum()),
         "n_disp_true_train": int(((cls == 2) & sub).sum()),
         "auc_all_trueaxis": strat_auc(s_true[lab_te], s_true[~lab_te]),
         "auc_disp_trueaxis": strat_auc(s_true[lab_te & (vx_te >= 1)], s_true[~lab_te]),
         "auc_disp_dispaxis": strat_auc(s_disp[lab_te & (vx_te >= 1)], s_disp[~lab_te]),
         "auc_disp_dispaxis_nL5p": strat_auc(s_disp[lab_te & (vx_te >= 1) & b5],
                                             s_disp[(~lab_te) & b5])}
    print(f"[{time.time() - T0:6.1f}s] frac={frac:5.3f} evts={k:3d} rows={r['n_rows']:8d} "
          f"dispTrue_train={r['n_disp_true_train']:6d} | AUCall={r['auc_all_trueaxis']:.5f} "
          f"AUCdisp(true)={r['auc_disp_trueaxis']:.5f} AUCdisp(disp)={r['auc_disp_dispaxis']:.5f} "
          f"AUCdisp_nL5p={r['auc_disp_dispaxis_nL5p']:.5f}", flush=True)
    out.append(r)
json.dump(out, open(f"{MYDIR}/learncurve.json", "w"), indent=1)
print("wrote learncurve.json")
