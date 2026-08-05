#!/usr/bin/env python3
"""Which (branch, nLayers, mX) chain TCs deliver the DISPLACED sims -- globally and in band."""
import sys
import numpy as np
import uproot
t = uproot.open(sys.argv[1])["tree"]
a = t.arrays(["tc_pt", "tc_eta", "tc_isFake", "tc_isChain", "tc_dbgBr", "tc_dbgNL",
              "tc_dbgMP", "tc_dbgMD", "tc_simIdxAll", "sim_vx", "sim_vy", "sim_pca_dxy"],
             library="np")
rows = []
for i in range(len(a["tc_pt"])):
    vxy = np.hypot(np.asarray(a["sim_vx"][i]), np.asarray(a["sim_vy"][i]))
    dxy = np.abs(np.asarray(a["sim_pca_dxy"][i]))
    ns = len(vxy)
    sl = a["tc_simIdxAll"][i]
    n = len(a["tc_pt"][i])
    mv = np.zeros(n); md = np.zeros(n)
    for k in range(n):
        s = [x for x in sl[k] if 0 <= x < ns]
        if s:
            mv[k] = vxy[s].max(); md[k] = dxy[s].max()
    rows.append((np.asarray(a["tc_pt"][i]), np.asarray(a["tc_eta"][i]),
                 np.asarray(a["tc_isFake"][i]), np.asarray(a["tc_isChain"][i]),
                 np.asarray(a["tc_dbgBr"][i]), np.asarray(a["tc_dbgNL"][i]),
                 np.asarray(a["tc_dbgMP"][i]), np.asarray(a["tc_dbgMD"][i]), mv, md))
pt, eta, fk, ich, br, nl, mp, mdm, mvxy, mdxy = [np.concatenate(x) for x in zip(*rows)]
fk = fk.astype(bool); ch = ich > 0
mX = np.maximum(mp, mdm)
sel = (pt > 0.9) & ch & ~fk
band = sel & (np.abs(eta) >= 1.1) & (np.abs(eta) < 1.7)
print("== TRUE chain TCs matched to a DISPLACED sim: where do they live? ==")
for lab, dm in (("vxy>=5", mvxy >= 5), ("vxy>=10", mvxy >= 10), ("dxy>=5", mdxy >= 5),
                ("dxy>=1", mdxy >= 1), ("prompt vxy<1", mvxy < 1)):
    m = sel & dm
    mb = band & dm
    print("\n  %-13s global n=%6d | in-band n=%5d (%.1f%%)" % (lab, m.sum(), mb.sum(),
          100.0 * mb.sum() / max(1, m.sum())))
    for b, bn in ((1, "T4-exempt"), (2, "5+IP"), (3, "5+exempt"), (0, "T4-IP")):
        for nlsel, nls in (((nl == 5), "nL=5"), ((nl >= 6), "nL>=6"), ((nl <= 4), "nL<=4")):
            s = mb & (br == b) & nlsel
            if s.sum() < 5:
                continue
            print("      band %-10s %-6s n=%5d | would die at band mX floor 0.0: %4d  0.5: %4d"
                  % (bn, nls, s.sum(), (s & (mX < 0.0)).sum(), (s & (mX < 0.5)).sum()))
