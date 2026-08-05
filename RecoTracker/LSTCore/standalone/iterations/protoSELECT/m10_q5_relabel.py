#!/usr/bin/env python3
"""M10 Q5 lever test: same chain features, TWO different training TARGETS.

target A = the current chain LABEL (all member T3s share a sim, Labels.h)
target B = TC-level truth (>75% of the chain's MDs belong to one sim = the harness rule)

Trained on the same accepted 5+-layer chains (events 0-69), evaluated on events 70-99,
scored ONLY by what the harness measures: TC fake rate at fixed true/displaced efficiency.
"""
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
    claimed = np.zeros(nmax, bool)
    for i in sorted(idxs, key=lambda i: (-rows[i]["score"], i)):
        mds = rows[i]["mds"]
        if claimed[mds].sum() / len(mds) > 0.3:
            continue
        claimed[mds] = True
        acc.append(i)
R = [rows[i] for i in acc]

FE = ["sumEdge", "minEdge", "meanEdge", "maxDeg", "score", "chi2", "rz", "R", "dca",
      "nLayers", "nNodes", "nMD", "tcpt", "tceta"]
X = np.array([[r[k] for k in FE] for r in R], float)
X[:, FE.index("chi2")] = np.log10(1 + X[:, FE.index("chi2")])
X[:, FE.index("rz")] = np.log10(1 + X[:, FE.index("rz")])
X[:, FE.index("maxDeg")] = np.log10(1 + X[:, FE.index("maxDeg")])
X[:, FE.index("tceta")] = np.abs(X[:, FE.index("tceta")])
ev = np.array([r["evt"] for r in R])
pt = np.array([r["tcpt"] for r in R])
bf = np.array([r["bestfrac"] for r in R])
vxy = np.array([r["bsVxy"] for r in R])
yA = (np.array([r["nInter"] for r in R]) > 0).astype(int)     # current chain label
yB = (bf > 0.75).astype(int)                                   # TC-level truth
inC = pt > 0.9
tr = inC & (ev < 70); te = inC & (ev >= 70)
print("train %d  test %d   (label-A pos frac %.3f, label-B pos frac %.3f)"
      % (tr.sum(), te.sum(), yA[tr].mean(), yB[tr].mean()))

def evaluate(s, tag):
    t = yB[te] == 1; f = yB[te] == 0
    d = t & (vxy[te] >= 1); d5 = t & (vxy[te] >= 5)
    print("  %-28s" % tag, end="")
    for eff in [0.99, 0.98, 0.95]:
        th = np.quantile(s[t], 1 - eff)
        print("  eff%.2f: FRrej %.3f dispEff %.3f/%.3f" %
              (eff, (s[f] < th).mean(), (s[d] >= th).mean(), (s[d5] >= th).mean()), end="")
    # AUC
    x = np.concatenate([s[t], s[f]]); o = np.argsort(x, kind="stable")
    rr = np.empty(len(x)); rr[o] = np.arange(1, len(x) + 1)
    A = (rr[:t.sum()].sum() - t.sum() * (t.sum() + 1) / 2) / (t.sum() * f.sum())
    print("   AUC %.4f" % A)

def fit(y, tag):
    m = HistGradientBoostingClassifier(max_iter=300, learning_rate=0.08, max_depth=6,
                                       random_state=0)
    m.fit(X[tr], y[tr])
    s = m.predict_proba(X[te])[:, 1]
    evaluate(s, tag)
    return s

print("\n=== fake rejection vs TC-TRUTH on held-out events 70-99 ===")
evaluate(X[te][:, FE.index("score")], "single feature: score")
evaluate(X[te][:, FE.index("meanEdge")], "single feature: meanEdge")
sA = fit(yA, "GBDT trained on chain LABEL")
sB = fit(yB, "GBDT trained on TC TRUTH")

# resulting fake rate if used as an acceptance cut at 99% true-chain efficiency
print("\n=== resulting chain-slice FR at a 99%/98% true-chain-efficiency working point ===")
t = yB[te] == 1; f = yB[te] == 0
for s, tag in [(X[te][:, FE.index("score")], "score (current legacy)"),
               (sA, "GBDT on chain LABEL"), (sB, "GBDT on TC TRUTH")]:
    for eff in [0.99, 0.98]:
        th = np.quantile(s[t], 1 - eff)
        keep = s >= th
        print("  %-24s eff%.2f -> FR %.4f (from %.4f), kept %.1f%% of accepted"
              % (tag, eff, (yB[te][keep] == 0).mean(), (yB[te] == 0).mean(),
                 100 * keep.mean()))
