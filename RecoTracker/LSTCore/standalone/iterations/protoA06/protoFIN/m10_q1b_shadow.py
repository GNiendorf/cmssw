#!/usr/bin/env python3
"""M10 Q1/Q3 addendum: are fake chain TCs kinematic shadows of real tracks? + dup flags."""
import numpy as np
import uproot, awkward as ak

F = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/ab_m8_h4b.root"
t = uproot.open(F + ":tree")
a = t.arrays(["tc_pt", "tc_eta", "tc_phi", "tc_isFake", "tc_isDuplicate", "tc_isChain",
              "tc_type", "tc_nhitOT"])
nev = len(a["tc_pt"])
inc = (a["tc_pt"] > 0.9) & (abs(a["tc_eta"]) < 4.5)

fl = lambda x, m: ak.to_numpy(ak.flatten(x[m]))
mC = inc & (a["tc_isChain"] == 1)
f = fl(a["tc_isFake"], mC); d = fl(a["tc_isDuplicate"], mC)
print("chain TCs in cut: %d ; fake %d ; fake AND flagged duplicate: %d (%.4f of fakes)"
      % (len(f), f.sum(), ((f == 1) & (d == 1)).sum(), ((f == 1) & (d == 1)).mean() / max(f.mean(), 1e-9)))

# nearest TRUE TC in (eta,phi,pt) for each fake chain TC
res = {k: [] for k in ["fake_shadow", "true_shadow"]}
for ie in range(nev):
    e = np.asarray(ak.to_list(a["tc_eta"][ie][inc[ie]]), float)
    p = np.asarray(ak.to_list(a["tc_phi"][ie][inc[ie]]), float)
    q = np.asarray(ak.to_list(a["tc_pt"][ie][inc[ie]]), float)
    fk = np.asarray(ak.to_list(a["tc_isFake"][ie][inc[ie]]), int)
    ch = np.asarray(ak.to_list(a["tc_isChain"][ie][inc[ie]]), int)
    if len(e) == 0:
        continue
    de = e[:, None] - e[None, :]
    dp = (p[:, None] - p[None, :] + np.pi) % (2 * np.pi) - np.pi
    dr = np.sqrt(de * de + dp * dp)
    ptr = np.maximum(q[:, None] / q[None, :], q[None, :] / q[:, None])
    ok = (dr < 0.02) & (ptr < 2.0) & (fk[None, :] == 0)
    np.fill_diagonal(ok, False)
    has = ok.any(axis=1)
    res["fake_shadow"].append(has[(ch == 1) & (fk == 1)])
    res["true_shadow"].append(has[(ch == 1) & (fk == 0)])
for k in res:
    v = np.concatenate(res[k])
    print("  %-14s has a TRUE TC within dR<0.02 and ptratio<2 : %.4f  (n=%d)"
          % (k, v.mean(), len(v)))
