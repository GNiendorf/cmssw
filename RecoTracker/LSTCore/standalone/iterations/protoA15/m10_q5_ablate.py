#!/usr/bin/env python3
"""M10 Q5: feature-set ablation for a chain-level gate on the 5+-layer path.
All models: HistGBDT, TC-level target, train evts 0-69 / test 70-99, accepted set F=0.3."""
import pickle
from collections import defaultdict
import numpy as np
from sklearn.ensemble import HistGradientBoostingClassifier

P = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad/m10_cand.pkl"
rows = pickle.load(open(P, "rb"))
byevt = defaultdict(list)
for i, r in enumerate(rows):
    byevt[r["evt"]].append(i)
acc = []
for e, idxs in byevt.items():
    nmax = max(max(rows[i]["mds"]) for i in idxs) + 1
    cl = np.zeros(nmax, bool)
    for i in sorted(idxs, key=lambda i: (-rows[i]["score"], i)):
        mds = rows[i]["mds"]
        if cl[mds].sum() / len(mds) > 0.3:
            continue
        cl[mds] = True
        acc.append(i)
R = [rows[i] for i in acc]

def col(k):
    return np.array([r[k] for r in R], float)

feat = {
    "sumEdge": col("sumEdge"), "minEdge": col("minEdge"), "meanEdge": col("meanEdge"),
    "logDeg": np.log10(1 + col("maxDeg")), "logChi2": np.log10(1 + col("chi2")),
    "logRz": np.log10(1 + col("rz")), "nLayers": col("nLayers"), "nNodes": col("nNodes"),
    "nPS": col("nPS"), "nBarrel": col("nBarrel"), "innerLayer": col("innerLayer"),
    "layerSpan": col("layerSpan"), "ptEst": col("tcpt"), "invR": 1 / np.maximum(col("R"), 1e-6),
    "radSpread": col("radSpread"),
    "dca": col("dca"), "absEta": np.abs(col("tceta")),
    "t3fkMax": col("t3fkMax"), "t3fkMean": col("t3fkMean"),
    "t3pmMin": col("t3pmMin"), "t3dsMin": col("t3dsMin"),
}
ev = col("evt"); pt = col("tcpt"); bf = col("bestfrac"); vxy = col("bsVxy")
y = (bf > 0.75).astype(int)
inC = pt > 0.9
tr = inC & (ev < 70); te = inC & (ev >= 70)

CUR = ["sumEdge", "minEdge", "meanEdge", "logChi2", "logRz", "invR", "nLayers", "nNodes",
       "nPS", "nBarrel", "innerLayer", "layerSpan", "ptEst", "logDeg"]
SETS = [
    ("A current-gate-like (14f)", CUR),
    ("B  + dca, |eta|", CUR + ["dca", "absEta"]),
    ("C  + T3 DNN aggregates", CUR + ["t3fkMax", "t3fkMean", "t3pmMin", "t3dsMin"]),
    ("D  + both (+radSpread)", CUR + ["dca", "absEta", "t3fkMax", "t3fkMean", "t3pmMin",
                                      "t3dsMin", "radSpread"]),
    ("E  T3 aggregates ONLY", ["t3fkMax", "t3fkMean", "t3pmMin", "t3dsMin", "nLayers"]),
]
t = y[te] == 1; f = y[te] == 0
d1 = t & (vxy[te] >= 1); d5 = t & (vxy[te] >= 5)
print("test: true %d fake %d (FR %.4f) ; displaced-true vxy>=1 %d vxy>=5 %d"
      % (t.sum(), f.sum(), f.mean(), d1.sum(), d5.sum()))
print("\n%-28s %7s | %s" % ("feature set", "AUC", "fake-rej @ true-eff (disp>=1 / disp>=5 eff)"))
for name, fs in SETS:
    X = np.stack([feat[k] for k in fs], axis=1)
    m = HistGradientBoostingClassifier(max_iter=300, learning_rate=0.08, max_depth=6,
                                       random_state=0).fit(X[tr], y[tr])
    s = m.predict_proba(X[te])[:, 1]
    x = np.concatenate([s[t], s[f]]); o = np.argsort(x, kind="stable")
    rr = np.empty(len(x)); rr[o] = np.arange(1, len(x) + 1)
    A = (rr[:t.sum()].sum() - t.sum() * (t.sum() + 1) / 2) / (t.sum() * f.sum())
    out = []
    for eff in [0.99, 0.98, 0.95]:
        th = np.quantile(s[t], 1 - eff)
        out.append("eff%.2f %.3f (%.3f/%.3f)" % (eff, (s[f] < th).mean(),
                                                 (s[d1] >= th).mean(), (s[d5] >= th).mean()))
    print("%-28s %7.4f | %s" % (name, A, "  ".join(out)))

# single-feature AUCs of the new candidates
print("\nsingle-feature AUC (true vs fake, test events):")
for k in ["t3fkMax", "t3fkMean", "t3pmMin", "t3dsMin", "dca", "absEta", "radSpread",
          "meanEdge", "minEdge", "logChi2"]:
    v = feat[k][te]
    x = np.concatenate([v[t], v[f]]); o = np.argsort(x, kind="stable")
    rr = np.empty(len(x)); rr[o] = np.arange(1, len(x) + 1)
    A = (rr[:t.sum()].sum() - t.sum() * (t.sum() + 1) / 2) / (t.sum() * f.sum())
    print("   %-12s %.4f   med true %8.4f  med fake %8.4f"
          % (k, A, np.median(feat[k][te][t]), np.median(feat[k][te][f])))
