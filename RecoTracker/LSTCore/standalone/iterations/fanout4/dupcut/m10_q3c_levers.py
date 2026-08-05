#!/usr/bin/env python3
"""M10 Mission A Q2/Q3/Q4/Q5: arbitration levers + per-feature separability vs TC-LEVEL truth."""
import pickle
from collections import defaultdict
import numpy as np
from scipy.stats import ks_2samp

P = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad/m10_cand.pkl"
rows = pickle.load(open(P, "rb"))
evts = sorted(set(r["evt"] for r in rows))
NEV = len(evts)
print("5+-layer post-pixdrop candidates: %d over %d events (%.0f/evt)" % (len(rows), NEV, len(rows)/NEV))

byevt = defaultdict(list)
for i, r in enumerate(rows):
    byevt[r["evt"]].append(i)

def arbitrate(F, scorekey="score", extra_cut=None):
    acc = []
    for e, idxs in byevt.items():
        nMDmax = max(max(r["mds"]) for r in (rows[i] for i in idxs)) + 1
        claimed = np.zeros(nMDmax, bool)
        cand = [i for i in idxs if extra_cut is None or extra_cut(rows[i])]
        cand.sort(key=lambda i: (-rows[i][scorekey], i))
        for i in cand:
            mds = rows[i]["mds"]
            if claimed[mds].sum() / len(mds) > F:
                continue
            claimed[mds] = True
            acc.append(i)
    return np.array(acc, dtype=np.int64)

def summarize(acc, tag):
    r = [rows[i] for i in acc]
    pt = np.array([x["tcpt"] for x in r]); bf = np.array([x["bestfrac"] for x in r])
    bs = np.array([x["bestsim"] for x in r]); vxy = np.array([x["bsVxy"] for x in r])
    spt = np.array([x["bsPt"] for x in r])
    m = pt > 0.9
    fake = bf <= 0.75
    good = m & ~fake
    # delivered accepted-sim tracks (efficiency proxy): distinct (evt,bestsim) with kinematics
    ev = np.array([x["evt"] for x in r])
    key = set(zip(ev[good].tolist(), bs[good].tolist()))
    keyacc = set((e, s) for e, s in key if s >= 0)
    okin = good & (spt > 0.9) & (vxy > -900)
    dk = set(zip(ev[okin].tolist(), bs[okin].tolist()))
    dd = set((e, s) for e, s in zip(ev[okin & (vxy >= 1)].tolist(), bs[okin & (vxy >= 1)].tolist()))
    dd5 = set((e, s) for e, s in zip(ev[okin & (vxy >= 5)].tolist(), bs[okin & (vxy >= 5)].tolist()))
    print("  %-26s acc=%6.1f/evt  FR=%.4f  true=%6.1f/evt  distinct-sims=%6.1f/evt"
          "  accepted-sim(pt>0.9)=%5.1f/evt  vxy>=1: %5.2f  vxy>=5: %5.2f"
          % (tag, m.sum()/NEV, fake[m].mean(), good.sum()/NEV, len(key)/NEV,
             len(dk)/NEV, len(dd)/NEV, len(dd5)/NEV))
    return dict(n=m.sum(), fr=fake[m].mean(), ndist=len(key), ndisp=len(dd), ndisp5=len(dd5),
                nacc=len(dk))

print("\n=== ARBITRATION LEVER: maxClaimedFrac (anchor F=0.3) ===")
base = None
for F in [1.0, 0.5, 0.3, 0.2, 0.19, 0.0]:
    s = summarize(arbitrate(F), "F=%.2f" % F)
    if F == 0.3:
        base = s
print("  (deltas vs F=0.30) ")
for F in [0.2, 0.19, 0.0]:
    s = summarize(arbitrate(F), "  recheck F=%.2f" % F)
    print("     dFR=%+.4f  d(distinct sims)=%+.1f/evt  d(disp vxy>=1)=%+.2f/evt d(vxy>=5)=%+.2f/evt"
          % (s["fr"]-base["fr"], (s["ndist"]-base["ndist"])/NEV,
             (s["ndisp"]-base["ndisp"])/NEV, (s["ndisp5"]-base["ndisp5"])/NEV))

acc = arbitrate(0.3)
R = [rows[i] for i in acc]
pt = np.array([x["tcpt"] for x in R]); bf = np.array([x["bestfrac"] for x in R])
nL = np.array([x["nLayers"] for x in R]); nN = np.array([x["nNodes"] for x in R])
inC = pt > 0.9
fake = bf <= 0.75
nInter = np.array([x["nInter"] for x in R]); nEmpty = np.array([x["nEmpty"] for x in R])
vxy = np.array([x["bsVxy"] for x in R])
F = {k: np.array([x[k] for x in R], float) for k in
     ["sumEdge", "minEdge", "meanEdge", "maxDeg", "score", "chi2", "rz", "R", "nLayers",
      "nNodes", "nMD", "tcpt", "tceta"]}
F["logChi2"] = np.log10(1 + F["chi2"]); F["logRz"] = np.log10(1 + F["rz"])
F["logDeg"] = np.log10(1 + F["maxDeg"]); F["absEta"] = np.abs(F["tceta"])
F["invR"] = 1.0 / np.maximum(F["R"], 1e-6)

def auc(a, b):
    if len(a) == 0 or len(b) == 0:
        return float("nan")
    x = np.concatenate([a, b]); order = np.argsort(x, kind="stable")
    rr = np.empty(len(x)); rr[order] = np.arange(1, len(x) + 1)
    xs = x[order]; i = 0
    while i < len(xs):
        j = i
        while j + 1 < len(xs) and xs[j + 1] == xs[i]:
            j += 1
        if j > i:
            rr[order[i:j + 1]] = 0.5 * (i + j) + 1
        i = j + 1
    return (rr[:len(a)].sum() - len(a) * (len(a) + 1) / 2.0) / (len(a) * len(b))

print("\n=== Q2 SEPARABILITY vs TC-LEVEL TRUTH on the ACCEPTED set (pt>0.9) ===")
def sep(mask, tag):
    t = mask & ~fake; f = mask & fake
    print("\n--- %s : true %d  fake %d (FR %.4f) ---" % (tag, t.sum(), f.sum(), f.sum()/mask.sum()))
    out = []
    for k, v in F.items():
        A = auc(v[t], v[f]); ks = ks_2samp(v[t], v[f], method="asymp").statistic
        out.append((abs(A-0.5), A, ks, k, np.median(v[t]), np.median(v[f])))
    out.sort(reverse=True)
    print("   %-12s %7s %7s %11s %11s" % ("feature", "AUC", "KS", "med_true", "med_fake"))
    for _, A, ks, k, mt, mf in out:
        print("   %-12s %7.4f %7.4f %11.4f %11.4f" % (k, A, ks, mt, mf))

sep(inC, "ALL accepted 5+ (pt>0.9)")
sep(inC & (nL == 5), "nLayers==5")
sep(inC & (nL >= 6), "nLayers>=6")

print("\n=== Q2/Q5: what a cut on each feature buys at fixed TRUE-chain efficiency ===")
print("(TRUE = accepted chain with >75%% MD coverage; displaced = its sim has vxy>=1)")
tt = inC & ~fake; ff = inC & fake
disp = tt & (vxy >= 1)
for k in ["score", "minEdge", "meanEdge", "logChi2", "logDeg", "sumEdge"]:
    v = F[k]
    sgn = 1.0 if auc(v[tt], v[ff]) > 0.5 else -1.0
    x = sgn * v
    print("  %-10s" % k, end="")
    for eff in [0.99, 0.98, 0.95]:
        th = np.quantile(x[tt], 1 - eff)
        print("  eff%.2f: fakerej %.3f (disp-eff %.3f)" % (eff, (x[ff] < th).mean(),
                                                           (x[disp] >= th).mean()), end="")
    print()

print("\n=== Q3 composition x feature: which class does each feature catch? ===")
cls = {
    "pureFake": inC & fake & (nEmpty == nN),
    "mixedRealFake": inC & fake & (nEmpty > 0) & (nEmpty < nN),
    "crossSim": inC & fake & (nEmpty == 0) & (nInter == 0),
    "contaminated": inC & fake & (nInter > 0),
    "TRUE": tt,
    "TRUE-displaced": disp,
}
print("  %-16s %7s %8s %8s %8s %8s %8s %8s" % ("class", "n", "score", "minEdge", "meanEdge",
                                                "logChi2", "maxDeg", "nNodes"))
for k, v in cls.items():
    print("  %-16s %7d %8.2f %8.2f %8.2f %8.3f %8.1f %8.2f"
          % (k, v.sum(), np.median(F["score"][v]), np.median(F["minEdge"][v]),
             np.median(F["meanEdge"][v]), np.median(F["logChi2"][v]),
             np.median(F["maxDeg"][v]), np.median(F["nNodes"][v])))

print("\n=== Q4 per-length composition (accepted, pt>0.9) ===")
print("  %-8s %7s %7s %9s %9s %9s %9s" % ("nLayers", "n", "FR", "pureFake", "mixedRF",
                                           "crossSim", "contam"))
for lab, v in [("5", nL == 5), ("6", nL == 6), (">=7", nL >= 7)]:
    q = inC & v; f = q & fake; n = max(int(f.sum()), 1)
    print("  %-8s %7d %7.4f %9.3f %9.3f %9.3f %9.3f"
          % (lab, q.sum(), f.sum()/max(q.sum(),1),
             (f & (nEmpty == nN)).sum()/n, (f & (nEmpty > 0) & (nEmpty < nN)).sum()/n,
             (f & (nEmpty == 0) & (nInter == 0)).sum()/n, (f & (nInter > 0)).sum()/n))

print("\n=== Q4 per-length separability of the score/minEdge (AUC vs TC truth) ===")
for lab, v in [("5", nL == 5), ("6", nL == 6), (">=7", nL >= 7)]:
    q = inC & v
    print("  nLayers %-4s n=%6d  AUC score %.4f  minEdge %.4f  logChi2 %.4f  logDeg %.4f"
          % (lab, q.sum(), auc(F["score"][q & ~fake], F["score"][q & fake]),
             auc(F["minEdge"][q & ~fake], F["minEdge"][q & fake]),
             auc(F["logChi2"][q & ~fake], F["logChi2"][q & fake]),
             auc(F["logDeg"][q & ~fake], F["logDeg"][q & fake])))

print("\n=== Q1 cross-check: FR vs |eta| on the reconstructed accepted set ===")
for lo, hi in [(0, 1.1), (1.1, 1.7), (1.7, 4.5)]:
    q = inC & (F["absEta"] >= lo) & (F["absEta"] < hi)
    print("  |eta| [%.1f,%.1f): n=%6d FR=%.4f" % (lo, hi, q.sum(), fake[q].mean()))
