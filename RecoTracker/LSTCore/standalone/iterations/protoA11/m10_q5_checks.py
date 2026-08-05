#!/usr/bin/env python3
"""M10: cross-checks -- anchor FR on the same 100 events; contaminated-class T3 anatomy."""
import pickle
from collections import Counter, defaultdict
import numpy as np
import uproot, awkward as ak

PROTO = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/"

# ---- (1) anchor chain-slice FR restricted to the first 100 events ----
t = uproot.open(PROTO + "ab_m8_h4b.root:tree")
a = t.arrays(["tc_pt", "tc_eta", "tc_type", "tc_isFake", "tc_isChain", "tc_nhitOT"],
             entry_stop=100)
m = (a["tc_pt"] > 0.9) & (abs(a["tc_eta"]) < 4.5) & (a["tc_isChain"] == 1)
f = ak.to_numpy(ak.flatten(a["tc_isFake"][m])); ty = ak.to_numpy(ak.flatten(a["tc_type"][m]))
print("anchor first-100-evt chain slice: n=%d FR=%.4f" % (len(f), f.mean()))
print("   type 4 (5+ layers): n=%d FR=%.4f   type 9: n=%d FR=%.4f"
      % ((ty == 4).sum(), f[ty == 4].mean(), (ty == 9).sum(), f[ty == 9].mean()))
print("   type-4 count/evt %.1f  [reconstruction: 552.9/evt, proxy FR 0.3585]"
      % ((ty == 4).sum() / 100))

# ---- (2) contaminated-class anatomy: per-member-T3 matched-MD counts ----
P = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad/m10_cand.pkl"
rows = pickle.load(open(P, "rb"))
byevt = defaultdict(list)
for i, r in enumerate(rows):
    byevt[r["evt"]].append(i)
acc = []
for e, idxs in byevt.items():
    nMDmax = max(max(rows[i]["mds"]) for i in idxs) + 1
    claimed = np.zeros(nMDmax, bool)
    cand = sorted(idxs, key=lambda i: (-rows[i]["score"], i))
    for i in cand:
        mds = rows[i]["mds"]
        if claimed[mds].sum() / len(mds) > 0.3:
            continue
        claimed[mds] = True
        acc.append(i)
R = [rows[i] for i in acc]
inC = np.array([x["tcpt"] for x in R]) > 0.9
bf = np.array([x["bestfrac"] for x in R]); nInter = np.array([x["nInter"] for x in R])
cont = np.nonzero(inC & (bf <= 0.75) & (nInter > 0))[0]
print("\ncontaminated accepted chains: %d" % len(cont))
h = Counter(); hm = Counter()
for i in cont:
    r = R[i]
    n = r["nMD"]; miss = set(r["missPos"])
    h[(n, len(miss))] += 1
    hm[tuple(sorted(0 if p == 0 else (1 if p == n - 1 else 2) for p in miss))] += 1
print("  (nMD, #unmatched):", dict(h.most_common(6)))
lbl = {0: "innermost", 1: "outermost", 2: "interior"}
print("  unmatched-MD position pattern (top):")
for k, v in hm.most_common(6):
    print("    %-30s %6d (%.3f)" % (str(tuple(lbl[x] for x in k)), v, v / len(cont)))
print("\n  interpretation: 'contaminated' = every member T3 keeps 2/3 sim-matched MDs")
print("  (the Labels.h >=2/3 T3 rule), so the chain LABEL is 1 while only %d%% of the"
      % 60)
print("  chain's MDs belong to the sim -> the harness calls the TC fake.")
