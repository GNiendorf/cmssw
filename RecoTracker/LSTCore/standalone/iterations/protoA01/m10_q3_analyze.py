#!/usr/bin/env python3
"""M10 Mission A Q3/Q4 analysis of the reconstructed accepted 5+-layer chains."""
import pickle
import numpy as np

P = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad/m10_comp.pkl"
rows = pickle.load(open(P, "rb"))
nev = len(set(r["evt"] for r in rows))
print("accepted 5+-layer chains: %d over %d events (%.1f/evt)" % (len(rows), nev, len(rows)/nev))

bf = np.array([r["bestfrac"] for r in rows])
nInter = np.array([r["nInter"] for r in rows])
nEmpty = np.array([r["nEmpty"] for r in rows])
nNodes = np.array([r["nNodes"] for r in rows])
nMD = np.array([r["nMD"] for r in rows])
nL = np.array([r["nLayers"] for r in rows])
nFakeT3 = np.array([r["nFakeT3"] for r in rows])
nDist = np.array([r["nDistinctSims"] for r in rows])
shT = np.array([r["shareMDwithTrue"] for r in rows])
shA = np.array([r["shareMDany"] for r in rows])
pt = np.array([r["tcpt"] for r in rows])
sc = np.array([r["score"] for r in rows])

incut = pt > 0.9
tcfake = bf <= 0.75           # MD-level proxy for the harness >75% hit-match rule
print("\n--- TC-fake proxy validation (harness chain-slice numbers for reference) ---")
print("  MD-coverage proxy fake rate (all)      : %.4f" % tcfake.mean())
print("  MD-coverage proxy fake rate (pt>0.9)   : %.4f  [anchor T5-class FR = 0.3461]"
      % tcfake[incut].mean())
print("  chain LABEL (all members share a sim) fake rate, pt>0.9: %.4f"
      % (nInter[incut] == 0).mean())

print("\n=== Q3 COMPOSITION of accepted 5+-layer chains (pt>0.9) ===")
m = incut
cats = {
    "TRUE  (>=75% MD coverage)":              m & ~tcfake,
    "FAKE, pure-fake members (all T3 sim-sets empty)": m & tcfake & (nEmpty == nNodes),
    "FAKE, some member has NO sim (mixed real+fake)":  m & tcfake & (nEmpty > 0) & (nEmpty < nNodes),
    "FAKE, all members real but NO common sim (cross-sim weld)":
        m & tcfake & (nEmpty == 0) & (nInter == 0),
    "FAKE, common sim exists but coverage<=75% (contaminated real track)":
        m & tcfake & (nInter > 0),
}
tot = int(m.sum()); nf = int((m & tcfake).sum())
for k, v in cats.items():
    print("  %-62s %6d  %6.2f%% of all  %6.2f%% of FAKES" %
          (k, v.sum(), 100*v.sum()/tot, 100*v.sum()/nf))

print("\n  fake breakdown by member-T3 quality (LST t3_isFake):")
for k, v in [("0 fake member T3s", nFakeT3 == 0), ("1", nFakeT3 == 1),
             ("2", nFakeT3 == 2), (">=3", nFakeT3 >= 3)]:
    a = m & tcfake & v; b = m & ~tcfake & v
    print("    %-20s fakes %6d (%5.1f%%)   trues %6d (%5.1f%%)"
          % (k, a.sum(), 100*a.sum()/nf, b.sum(), 100*b.sum()/(tot-nf)))
print("    mean #fake member T3s: fake chains %.2f  true chains %.2f"
      % (nFakeT3[m & tcfake].mean(), nFakeT3[m & ~tcfake].mean()))
print("    mean #member T3s     : fake chains %.2f  true chains %.2f"
      % (nNodes[m & tcfake].mean(), nNodes[m & ~tcfake].mean()))

print("\n  best-sim MD coverage (bestfrac) distribution of FAKE chains:")
fb = bf[m & tcfake]
for lo, hi in [(0.0, 0.2), (0.2, 0.4), (0.4, 0.55), (0.55, 0.7), (0.7, 0.76)]:
    s = (fb >= lo) & (fb < hi)
    print("    bestfrac [%.2f,%.2f): %6d (%5.1f%%)" % (lo, hi, s.sum(), 100*s.sum()/len(fb)))
print("    median bestfrac of fakes %.3f ; of trues %.3f" % (np.median(fb), np.median(bf[m & ~tcfake])))

print("\n=== Q3 BRAID REMNANTS: MD sharing with a TC-TRUE accepted chain ===")
for tag, v in [("FAKE chains", m & tcfake), ("TRUE chains", m & ~tcfake)]:
    print("  %-12s share >=1 MD with a TRUE accepted chain: %.4f ; with ANY other accepted: %.4f"
          % (tag, (shT[v] > 0).mean(), (shA[v] > 0).mean()))
    print("               mean #shared MDs with a true chain %.2f (of %.2f MDs)"
          % (shT[v].mean(), nMD[v].mean()))

print("\n=== Q4 per-length split (accepted, pt>0.9) ===")
print("  %-10s %8s %8s %9s %9s %9s %9s %9s" % ("nLayers", "n", "FR", "pureFake",
                                                "mixedRF", "crossSim", "contam", "braidfrac"))
for lab, v in [("5", nL == 5), ("6", nL == 6), (">=7", nL >= 7)]:
    q = m & v
    if q.sum() == 0:
        continue
    f = q & tcfake
    n = int(q.sum()); nfk = int(f.sum())
    print("  %-10s %8d %8.4f %9.3f %9.3f %9.3f %9.3f %9.3f" %
          (lab, n, nfk/n,
           (f & (nEmpty == nNodes)).sum()/max(nfk,1),
           (f & (nEmpty > 0) & (nEmpty < nNodes)).sum()/max(nfk,1),
           (f & (nEmpty == 0) & (nInter == 0)).sum()/max(nfk,1),
           (f & (nInter > 0)).sum()/max(nfk,1),
           (shT[f] > 0).mean()))

print("\n=== score separation of the composition classes (legacy sum-logit + 0.5*nLayers) ===")
for k, v in cats.items():
    if v.sum() < 20:
        continue
    q = np.percentile(sc[v], [10, 50, 90])
    print("  %-62s n=%6d  score p10/p50/p90 = %6.2f %6.2f %6.2f" % (k, v.sum(), *q))

print("\n=== nMD vs nLayers of accepted chains (extra-MD = same-layer duplicate hit) ===")
for lab, v in [("TRUE", m & ~tcfake), ("FAKE", m & tcfake)]:
    extra = nMD[v] - nL[v]
    print("  %s: nMD-nLayers 0/1/2+ = %.3f / %.3f / %.3f ; mean nMD %.2f nLayers %.2f"
          % (lab, (extra == 0).mean(), (extra == 1).mean(), (extra >= 2).mean(),
             nMD[v].mean(), nL[v].mean()))
