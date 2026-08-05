#!/usr/bin/env python3
"""M10 Q5 end-to-end lever test: a TC-truth-trained chain gate applied BEFORE K9
arbitration on the 5+-layer path (so freed MDs can be re-claimed by true chains).

Train on all 5+ candidates of events 0-69; evaluate the full K9 on events 70-99.
"""
import pickle
from collections import defaultdict
import numpy as np
from sklearn.ensemble import HistGradientBoostingClassifier

P = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad/m10_cand.pkl"
rows = pickle.load(open(P, "rb"))
ev = np.array([r["evt"] for r in rows])
NEVTE = len(set(ev[ev >= 70].tolist()))

def col(k):
    return np.array([r[k] for r in rows], float)

feat = {
    "sumEdge": col("sumEdge"), "minEdge": col("minEdge"), "meanEdge": col("meanEdge"),
    "logDeg": np.log10(1 + col("maxDeg")), "logChi2": np.log10(1 + col("chi2")),
    "logRz": np.log10(1 + col("rz")), "nLayers": col("nLayers"), "nNodes": col("nNodes"),
    "nPS": col("nPS"), "nBarrel": col("nBarrel"), "innerLayer": col("innerLayer"),
    "layerSpan": col("layerSpan"), "ptEst": col("tcpt"),
    "invR": 1 / np.maximum(col("R"), 1e-6), "radSpread": col("radSpread"),
    "dca": col("dca"), "absEta": np.abs(col("tceta")),
    "t3fkMax": col("t3fkMax"), "t3fkMean": col("t3fkMean"),
    "t3pmMin": col("t3pmMin"), "t3dsMin": col("t3dsMin"),
}
FS = list(feat.keys())
X = np.stack([feat[k] for k in FS], axis=1)
y = (col("bestfrac") > 0.75).astype(int)
tr = ev < 70
m = HistGradientBoostingClassifier(max_iter=300, learning_rate=0.08, max_depth=6,
                                   random_state=0).fit(X[tr], y[tr])
s = np.zeros(len(rows))
s[~tr] = m.predict_proba(X[~tr])[:, 1]
print("gate trained on %d candidates (evts 0-69); evaluating %d test events" % (tr.sum(), NEVTE))

byevt = defaultdict(list)
for i in np.nonzero(~tr)[0]:
    byevt[rows[i]["evt"]].append(i)

pt = col("tcpt"); bf = col("bestfrac"); vxy = col("bsVxy"); spt = col("bsPt")
bs = np.array([r["bestsim"] for r in rows])

def run(thr, tag, F=0.3):
    acc = []
    for e, idxs in byevt.items():
        nmax = max(max(rows[i]["mds"]) for i in idxs) + 1
        cl = np.zeros(nmax, bool)
        cand = [i for i in idxs if s[i] >= thr]
        cand.sort(key=lambda i: (-rows[i]["score"], i))
        for i in cand:
            mds = rows[i]["mds"]
            if cl[mds].sum() / len(mds) > F:
                continue
            cl[mds] = True
            acc.append(i)
    a = np.array(acc, dtype=np.int64)
    mm = pt[a] > 0.9
    fk = bf[a] <= 0.75
    good = mm & ~fk
    okin = good & (spt[a] > 0.9) & (vxy[a] > -900)
    S = lambda msk: len(set(zip(ev[a][msk].tolist(), bs[a][msk].tolist())))
    print("  %-22s n=%6.1f/evt  FR=%.4f | simsAcc=%5.2f  vxy>=1=%5.2f  vxy>=5=%5.2f  vxy>=10=%5.2f"
          % (tag, mm.sum()/NEVTE, fk[mm].mean(), S(okin)/NEVTE, S(okin & (vxy[a] >= 1))/NEVTE,
             S(okin & (vxy[a] >= 5))/NEVTE, S(okin & (vxy[a] >= 10))/NEVTE))
    return fk[mm].mean(), S(okin)/NEVTE, S(okin & (vxy[a] >= 1))/NEVTE, S(okin & (vxy[a] >= 5))/NEVTE

print("\n=== gate applied BEFORE the greedy MD claim (freed MDs re-claimed) ===")
b = run(-1.0, "anchor (no gate)")
for thr in [0.05, 0.1, 0.2, 0.3, 0.5]:
    r = run(thr, "gate p>=%.2f" % thr)
    print("      dFR=%+.4f  dSims=%+.2f/evt (%+.1f%%)  dVxy>=1=%+.2f (%+.1f%%)  dVxy>=5=%+.2f (%+.1f%%)"
          % (r[0]-b[0], r[1]-b[1], 100*(r[1]-b[1])/b[1], r[2]-b[2], 100*(r[2]-b[2])/b[2],
             r[3]-b[3], 100*(r[3]-b[3])/b[3]))
