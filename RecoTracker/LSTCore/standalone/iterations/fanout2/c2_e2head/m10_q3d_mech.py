#!/usr/bin/env python3
"""M10 Mission A Q5: candidate fake-rejection MECHANISMS scored on the reconstructed
accepted 5+-layer set (anchor arbitration F=0.3), with the displaced cost of each."""
import pickle
from collections import defaultdict
import numpy as np

P = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad/m10_cand.pkl"
rows = pickle.load(open(P, "rb"))
NEV = len(set(r["evt"] for r in rows))
byevt = defaultdict(list)
for i, r in enumerate(rows):
    byevt[r["evt"]].append(i)

def arbitrate(F=0.3, keep=None):
    acc = []
    for e, idxs in byevt.items():
        nMDmax = max(max(rows[i]["mds"]) for i in idxs) + 1
        claimed = np.zeros(nMDmax, bool)
        cand = [i for i in idxs if keep is None or keep(rows[i])]
        cand.sort(key=lambda i: (-rows[i]["score"], i))
        for i in cand:
            mds = rows[i]["mds"]
            if claimed[mds].sum() / len(mds) > F:
                continue
            claimed[mds] = True
            acc.append(i)
    return acc

def summarize(acc, tag):
    r = [rows[i] for i in acc]
    pt = np.array([x["tcpt"] for x in r]); bf = np.array([x["bestfrac"] for x in r])
    bs = np.array([x["bestsim"] for x in r]); vxy = np.array([x["bsVxy"] for x in r])
    spt = np.array([x["bsPt"] for x in r]); ev = np.array([x["evt"] for x in r])
    m = pt > 0.9; fake = bf <= 0.75; good = m & ~fake
    okin = good & (spt > 0.9) & (vxy > -900)
    S = lambda msk: len(set(zip(ev[msk].tolist(), bs[msk].tolist())))
    print("  %-42s n=%6.1f/evt FR=%.4f  simsAcc=%5.2f  vxy>=1=%5.2f vxy>=5=%5.2f vxy>=10=%5.2f"
          % (tag, m.sum()/NEV, fake[m].mean(), S(okin)/NEV, S(okin & (vxy >= 1))/NEV,
             S(okin & (vxy >= 5))/NEV, S(okin & (vxy >= 10))/NEV))
    return dict(fr=fake[m].mean(), n=m.sum()/NEV, s=S(okin)/NEV, d1=S(okin & (vxy >= 1))/NEV,
                d5=S(okin & (vxy >= 5))/NEV, d10=S(okin & (vxy >= 10))/NEV)

print("=== BASELINE and ORACLE gates (all at F=0.3) ===")
b = summarize(arbitrate(), "anchor (h4b 5+ branch)")
summarize(arbitrate(keep=lambda r: r["nInter"] > 0),
          "ORACLE chain-LABEL gate (all members share sim)")
summarize(arbitrate(keep=lambda r: r["bestfrac"] > 0.75), "ORACLE TC-truth gate (ceiling)")
summarize(arbitrate(keep=lambda r: r["nEmpty"] == 0),
          "ORACLE 'no unmatched member T3' gate")
summarize(arbitrate(keep=lambda r: r["nFakeT3"] == 0),
          "ORACLE 'no LST-fake member T3' gate (t3_isFake)")

acc = arbitrate()
R = [rows[i] for i in acc]
pt = np.array([x["tcpt"] for x in R]); bf = np.array([x["bestfrac"] for x in R])
inC = pt > 0.9; fake = bf <= 0.75
nN = np.array([x["nNodes"] for x in R]); nInter = np.array([x["nInter"] for x in R])
nEmpty = np.array([x["nEmpty"] for x in R]); nL = np.array([x["nLayers"] for x in R])
vxy = np.array([x["bsVxy"] for x in R]); spt = np.array([x["bsPt"] for x in R])
dca = np.array([x["dca"] for x in R]); nMD = np.array([x["nMD"] for x in R])

cls = {
    "pureFake": inC & fake & (nEmpty == nN),
    "mixedRealFake": inC & fake & (nEmpty > 0) & (nEmpty < nN),
    "crossSim": inC & fake & (nEmpty == 0) & (nInter == 0),
    "contaminated": inC & fake & (nInter > 0),
    "TRUE": inC & ~fake,
    "TRUE-disp(vxy>=1)": inC & ~fake & (vxy >= 1),
    "TRUE-disp(vxy>=5)": inC & ~fake & (vxy >= 5),
}

print("\n=== transverse DCA of the full-chain circle fit (the -G 3/-A 3 axis) ===")
print("  %-20s %7s %8s %8s %8s %8s" % ("class", "n", "dca<0.5", "dca<1", "med dca", "p90 dca"))
for k, v in cls.items():
    print("  %-20s %7d %8.3f %8.3f %8.3f %8.2f"
          % (k, v.sum(), (dca[v] < 0.5).mean(), (dca[v] < 1.0).mean(),
             np.median(dca[v]), np.percentile(dca[v], 90)))

print("\n=== WHERE the wrong MDs sit (fake chains, position 0 = innermost) ===")
def poscat(r):
    mp = r["missPos"]; n = r["nMD"]
    if not mp:
        return "none"
    ends = set([0, 1, n - 2, n - 1])
    if all(p in (0, n - 1) for p in mp):
        return "extreme ends only"
    if all(p in ends for p in mp):
        return "within 2 of an end"
    return "interior"
for cname in ["contaminated", "mixedRealFake", "pureFake", "crossSim"]:
    v = cls[cname]
    idx = np.nonzero(v)[0]
    cnt = defaultdict(int)
    nmiss = []
    for i in idx:
        cnt[poscat(R[i])] += 1
        nmiss.append(len(R[i]["missPos"]))
    tot = max(len(idx), 1)
    print("  %-16s n=%6d  mean #unmatched MDs %.2f of %.2f | %s"
          % (cname, len(idx), np.mean(nmiss) if nmiss else 0, nMD[v].mean(),
             "  ".join("%s %.2f" % (k, c/tot) for k, c in sorted(cnt.items()))))

print("\n  contaminated chains: #unmatched MDs distribution")
v = cls["contaminated"]; idx = np.nonzero(v)[0]
h = defaultdict(int)
for i in idx:
    h[len(R[i]["missPos"])] += 1
for k in sorted(h):
    print("    %d unmatched MD(s): %6d (%.3f)" % (k, h[k], h[k]/len(idx)))

print("\n  'trim one end MD -> would the rest be >75%% matched?' (contaminated only)")
ok = 0
for i in idx:
    r = R[i]; n = r["nMD"]; mp = r["missPos"]
    if len(mp) == 1 and mp[0] in (0, n - 1) and (n - 1) >= 4:
        ok += 1
print("    exactly 1 unmatched MD, at an extreme end, >=4 MDs remain: %d (%.3f of contaminated)"
      % (ok, ok/len(idx)))

print("\n=== member-T3 position of the unmatched member (mixedRealFake, nNodes==3) ===")
v = cls["mixedRealFake"]
h = defaultdict(int); tot = 0
for i in np.nonzero(v)[0]:
    r = R[i]
    if r["nNodes"] != 3 or len(r["emptyPos"]) != 1:
        continue
    h[["inner", "middle", "outer"][r["emptyPos"][0]]] += 1
    tot += 1
print("  n=%d  %s" % (tot, "  ".join("%s %.3f" % (k, c/max(tot,1)) for k, c in h.items())))

print("\n=== nNodes structure per class (welder path length) ===")
print("  %-20s %s" % ("class", "  ".join("n%d" % k for k in [2, 3, 4, 5])))
for k, v in cls.items():
    print("  %-20s %s" % (k, "  ".join("%.3f" % (nN[v] == j).mean() for j in [2, 3, 4, 5])))

print("\n=== MECHANISM sweeps: fake rejection vs displaced cost (applied as an acceptance cut) ===")
def sweep(name, fn, vals):
    for t in vals:
        s = summarize(arbitrate(keep=lambda r: fn(r, t)), "%s %s" % (name, t))
        print("     dFR=%+.4f  dSims=%+.2f  dVxy>=1=%+.2f dVxy>=5=%+.2f dVxy>=10=%+.2f"
              % (s["fr"] - b["fr"], s["s"] - b["s"], s["d1"] - b["d1"],
                 s["d5"] - b["d5"], s["d10"] - b["d10"]))

sweep("minEdge >=", lambda r, t: r["minEdge"] >= t, [1.0, 2.0, 3.0])
sweep("meanEdge >=", lambda r, t: r["meanEdge"] >= t, [1.5, 2.5, 3.5])
sweep("chi2PerHit <=", lambda r, t: r["chi2"] <= t, [0.3, 0.1, 0.03])
sweep("maxJuncDeg <=", lambda r, t: r["maxDeg"] <= t, [64, 32, 16])
sweep("nLayers>=6 only", lambda r, t: r["nLayers"] >= 6, ["(kill 5-layer)"])
sweep("chi2<=0.05 AND minEdge>=1.5",
      lambda r, t: (r["chi2"] <= 0.05) and (r["minEdge"] >= 1.5), ["combo"])
