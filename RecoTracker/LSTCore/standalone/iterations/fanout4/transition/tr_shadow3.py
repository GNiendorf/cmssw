#!/usr/bin/env python3
"""joint (claim-frozen) shadow of the composed transition levers."""
import sys
import numpy as np
import uproot
t = uproot.open(sys.argv[1])["tree"]
a = t.arrays(["tc_pt", "tc_eta", "tc_isFake", "tc_isChain", "tc_dbgBr", "tc_dbgNL",
              "tc_dbgNN", "tc_dbgMP", "tc_dbgMD", "tc_dbgDca"], library="np")
d = {k: np.concatenate([np.asarray(x) for x in v]) for k, v in a.items()}
pt9 = d["tc_pt"] > 0.9
ae = np.abs(d["tc_eta"])
fk = d["tc_isFake"].astype(bool)
ch = d["tc_isChain"] > 0
mX = np.maximum(d["tc_dbgMP"], d["tc_dbgMD"])
band = pt9 & (ae >= 1.1) & (ae < 1.7)
b0f, b0n = fk[band].sum(), band.sum()
gf, gn = fk[pt9].sum(), pt9.sum()
ex5 = band & ch & (d["tc_dbgBr"] == 3) & (d["tc_dbgNL"] == 5)
ex6 = band & ch & (d["tc_dbgBr"] == 3) & (d["tc_dbgNL"] >= 6)
t4e = band & ch & (d["tc_dbgBr"] == 1)
cell = band & ch & (d["tc_dbgNN"] == 2) & (d["tc_dbgNL"] == 5)

def rep(name, kill):
    kf, kt = (kill & fk).sum(), (kill & ~fk).sum()
    print("  %-34s killF=%5d killT=%5d ratio=%4.2f | band fake=%.4f  glob fake=%.4f"
          % (name, kf, kt, kf / max(1, kt), (b0f - kf) / (b0n - kill.sum()),
             (gf - kf) / (gn - kill.sum())))

print("== single levers ==")
for f in (0.0, 0.25, 0.5, 0.75, 1.0):
    rep("ex5 mX floor %.2f (-ZR5 %.2f)" % (f, f + 1.8), ex5 & (mX < f))
for f in (0.0, 0.5):
    rep("ex6 mX floor %.2f (-ZR6 %.2f)" % (f, f + 1.8), ex6 & (mX < f))
for fd in (-1.0, 0.0, 1.0):
    rep("cell mD floor %.1f (-ZCD %.1f)" % (fd, fd + 2.0), cell & (d["tc_dbgMP"] < 2.0) & (d["tc_dbgMD"] < fd))
rep("T4-exempt full kill (-ZM4D 99)", t4e)
print("\n== compositions ==")
for f in (0.0, 0.25, 0.5):
    for fd in (-2.0, -1.0, 0.0):
        for t4 in (False, True):
            k = (ex5 & (mX < f)) | (cell & (d["tc_dbgMP"] < 2.0) & (d["tc_dbgMD"] < fd))
            if t4:
                k = k | t4e
            rep("ex5<%.2f + cellmD<%.1f%s" % (f, fd, " + T4Ekill" if t4 else ""), k)
