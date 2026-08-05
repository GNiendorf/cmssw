#!/usr/bin/env python3
"""dca-window refinement of the transition exempt mX floor."""
import sys
import numpy as np
import uproot
t = uproot.open(sys.argv[1])["tree"]
a = t.arrays(["tc_pt", "tc_eta", "tc_isFake", "tc_isChain", "tc_dbgBr", "tc_dbgNL",
              "tc_dbgMP", "tc_dbgMD", "tc_dbgDca"], library="np")
d = {k: np.concatenate([np.asarray(x) for x in v]) for k, v in a.items()}
pt9 = d["tc_pt"] > 0.9
ae = np.abs(d["tc_eta"])
fk = d["tc_isFake"].astype(bool)
ch = d["tc_isChain"] > 0
mX = np.maximum(d["tc_dbgMP"], d["tc_dbgMD"])
band = pt9 & (ae >= 1.1) & (ae < 1.7)
ex = band & ch & (d["tc_dbgBr"] == 3)
print("== transition 5+exempt: dca distribution, fake vs true ==")
for lab, s in (("FAKE", ex & fk), ("TRUE", ex & ~fk)):
    q = np.percentile(d["tc_dbgDca"][s], [10, 25, 50, 75, 90, 99])
    print("  %s n=%6d dca p10/25/50/75/90/99 = %s" % (lab, s.sum(), " ".join("%6.2f" % x for x in q)))
print("\n== transition 5+exempt population in dca windows ==")
edges = [0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 5.0, 1e9]
for i in range(len(edges) - 1):
    s = ex & (d["tc_dbgDca"] >= edges[i]) & (d["tc_dbgDca"] < edges[i + 1])
    if s.sum() == 0:
        continue
    lowmx = s & (mX < 0.0)
    print("  dca [%5.2f,%6.2f) n=%6d fake=%.4f | mX<0 : n=%5d fakefrac=%.3f (F=%4d T=%4d)"
          % (edges[i], min(edges[i + 1], 999), s.sum(), fk[s].mean(), lowmx.sum(),
             fk[lowmx].mean() if lowmx.sum() else 0, (lowmx & fk).sum(), (lowmx & ~fk).sum()))
b0f, b0n = fk[band].sum(), band.sum()
print("\n== SHADOW: band exempt mX floor restricted to dca < dcaMax ==")
print("   dcaMax  floor   killF   killT  ratio | band fake")
for dm in (1.0, 1.5, 2.0, 3.0, 1e9):
    for f in (0.0, 0.25, 0.5, 0.75):
        kill = ex & (d["tc_dbgDca"] < dm) & (mX < f)
        kf, kt = (kill & fk).sum(), (kill & ~fk).sum()
        print("  %7.1f %6.2f %7d %7d %6.2f | %.4f"
              % (dm, f, kf, kt, kf / max(1, kt), (b0f - kf) / (b0n - kill.sum())))
print("\n== SHADOW: nLayers split of the exempt floor (nL=5 only vs all) ==")
for nlsel, lab in ((d["tc_dbgNL"] == 5, "nL=5"), (d["tc_dbgNL"] >= 6, "nL>=6"), (d["tc_dbgNL"] >= 0, "all")):
    for f in (0.0, 0.5):
        kill = ex & nlsel & (mX < f)
        kf, kt = (kill & fk).sum(), (kill & ~fk).sum()
        print("  %-6s floor=%.2f killF=%5d killT=%5d ratio=%.2f | band fake=%.4f"
              % (lab, f, kf, kt, kf / max(1, kt), (b0f - kf) / (b0n - kill.sum())))
