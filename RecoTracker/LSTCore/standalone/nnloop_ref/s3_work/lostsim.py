#!/usr/bin/env python3
"""S3: WHICH SIMS an arm loses relative to a baseline, and WHAT CLASS of TC used to match them.

Efficiency is per sim, so a delta is only interpretable once you know which sims moved and which TC
class was carrying them.  The writer emits events in STREAM-COMPLETION order, so the two runs are
paired on the sim-collection key exactly as a5_ref/paired.py does it, never entry-wise.
usage: lostsim.py <base.root> <arm.root>
"""
import sys
import awkward as ak
import numpy as np
import uproot

def load(p):
    t = uproot.open(p)["tree"]
    d = t.arrays(["sim_pt", "sim_eta", "sim_vx", "sim_vy", "sim_pca_dxy", "sim_tcIdx", "tc_type"])
    key = np.asarray(ak.num(d.sim_pt, axis=1)) * 1.0e9 + np.asarray(ak.sum(d.sim_pt, axis=1))
    o = np.argsort(key, kind="stable")
    return d[o], key[o]

A, ka = load(sys.argv[1])
B, kb = load(sys.argv[2])
assert np.array_equal(ka, kb), "event keys differ -- the two runs are not the same events"
n = len(ka)
lost_ty, gain_ty = {}, {}
lost_eta, lost_vxy = [], []
nl = ng = 0
for i in range(n):
    ta = np.asarray(A.sim_tcIdx[i]); tb = np.asarray(B.sim_tcIdx[i])
    tya = np.asarray(A.tc_type[i]);  tyb = np.asarray(B.tc_type[i])
    ea = np.asarray(A.sim_eta[i]);   pa = np.asarray(A.sim_pt[i])
    vx = np.asarray(A.sim_vx[i]);    vy = np.asarray(A.sim_vy[i])
    cut = pa > 0.9
    la = (ta >= 0) & cut; lb = (tb >= 0) & cut
    L = la & ~lb; G = lb & ~la
    nl += int(L.sum()); ng += int(G.sum())
    for j in np.flatnonzero(L):
        t = int(ta[j]); ty = int(tya[t]) if 0 <= t < len(tya) else -1
        lost_ty[ty] = lost_ty.get(ty, 0) + 1
        lost_eta.append(abs(ea[j])); lost_vxy.append(np.hypot(vx[j], vy[j]))
    for j in np.flatnonzero(G):
        t = int(tb[j]); ty = int(tyb[t]) if 0 <= t < len(tyb) else -1
        gain_ty[ty] = gain_ty.get(ty, 0) + 1
NAME = {4: "T5", 5: "pT3", 7: "pT5", 8: "pLS", 9: "T4", -1: "?"}
print("sims matched in BASE only (lost) = %d ; in ARM only (gained) = %d ; net %+d" % (nl, ng, ng - nl))
print("  lost, by the TC CLASS that matched them in the BASE run:")
for k in sorted(lost_ty, key=lambda z: -lost_ty[z]):
    print("     %-4s %6d" % (NAME.get(k, k), lost_ty[k]))
print("  gained, by the TC class that matches them in the ARM run:")
for k in sorted(gain_ty, key=lambda z: -gain_ty[z]):
    print("     %-4s %6d" % (NAME.get(k, k), gain_ty[k]))
le = np.array(lost_eta); lv = np.array(lost_vxy)
print("  lost by |eta|: barrel<1.1 %d  transition %d  endcap>=1.7 %d"
      % ((le < 1.1).sum(), ((le >= 1.1) & (le < 1.7)).sum(), (le >= 1.7).sum()))
print("  lost by vxy: <1 %d  [1,5) %d  >=5 %d" % ((lv < 1).sum(), ((lv >= 1) & (lv < 5)).sum(), (lv >= 5).sum()))
