#!/usr/bin/env python3
"""tr_shadow.py -- first-order (claim-frozen) scan of band-aware kill thresholds."""
import sys
import numpy as np
import uproot

P = sys.argv[1]
t = uproot.open(P)["tree"]
a = t.arrays(["tc_pt", "tc_eta", "tc_isFake", "tc_isDuplicate", "tc_isChain", "tc_dbgBr",
              "tc_dbgNL", "tc_dbgNN", "tc_dbgMP", "tc_dbgMD", "tc_dbgDca", "tc_dbgInLay"],
             library="np")
d = {k: np.concatenate([np.asarray(x) for x in v]) for k, v in a.items()}
pt9 = d["tc_pt"] > 0.9
ae = np.abs(d["tc_eta"])
fk = d["tc_isFake"].astype(bool)
dup = d["tc_isDuplicate"].astype(bool)
ch = d["tc_isChain"] > 0
mX = np.maximum(d["tc_dbgMP"], d["tc_dbgMD"])
band = pt9 & (ae >= 1.1) & (ae < 1.7)
allsel = pt9

print("== dca distribution of chain TCs per band (why the exempt branch is over-populated) ==")
for nm, lo, hi in (("barrel", 0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 4.5)):
    m = pt9 & (ae >= lo) & (ae < hi) & ch & (d["tc_dbgNL"] >= 5)
    ex = d["tc_dbgBr"] == 3
    print("  %-11s n5+=%7d  exempt(dca>=0.5)=%6.1f%%  | TRUE-only exempt=%5.1f%%  dca p50=%.3f p90=%.3f"
          % (nm, m.sum(), 100.0 * (m & ex).sum() / m.sum(), 100.0 * (m & ex & ~fk).sum() / (m & ~fk).sum(),
             np.median(d["tc_dbgDca"][m]), np.percentile(d["tc_dbgDca"][m], 90)))

print("\n== mX quantiles, 5+exempt branch, per band ==")
for nm, lo, hi in (("barrel", 0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 4.5)):
    m = pt9 & (ae >= lo) & (ae < hi) & ch & (d["tc_dbgBr"] == 3)
    for lab, s in (("FAKE", m & fk), ("TRUE", m & ~fk)):
        q = np.percentile(mX[s], [5, 10, 25, 50, 75, 90])
        print("  %-11s %s n=%6d mX p5/10/25/50/75/90 = %s" % (nm, lab, s.sum(),
              " ".join("%6.2f" % x for x in q)))

print("\n== SHADOW SCAN: transition-band 5+exempt mX floor (claim frozen) ==")
print("   floor   killF   killT  killDupT | band n    band fake  (glob fake)")
b0f, b0n = fk[band].sum(), band.sum()
g0f, g0n = fk[allsel].sum(), allsel.sum()
for f in (-1.8, -1.0, -0.5, 0.0, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0):
    kill = band & ch & (d["tc_dbgBr"] == 3) & (mX < f)
    kf, kt, kdt = (kill & fk).sum(), (kill & ~fk).sum(), (kill & ~fk & dup).sum()
    gkill = allsel & ch & (d["tc_dbgBr"] == 3) & (mX < f) & (ae >= 1.1) & (ae < 1.7)
    print("  %6.2f %7d %7d %8d | %7d   %.4f     (%.4f)"
          % (f, kf, kt, kdt, b0n - kill.sum(), (b0f - kf) / (b0n - kill.sum()),
             (g0f - (gkill & fk).sum()) / (g0n - gkill.sum())))

print("\n== SHADOW SCAN: transition-band T4-exempt mD floor ==")
print("   floor   killF   killT | band fake")
for f in (-0.75, -0.5, -0.25, 0.0, 0.25, 0.5, 1.0, 99.0):
    kill = band & ch & (d["tc_dbgBr"] == 1) & (d["tc_dbgMD"] < f)
    kf, kt = (kill & fk).sum(), (kill & ~fk).sum()
    print("  %6.2f %7d %7d | %.4f" % (f, kf, kt, (b0f - kf) / (b0n - kill.sum())))

print("\n== SHADOW SCAN: transition-band cell (nNodes=2, nL=5) extra mP floor (mD floor fixed) ==")
cell = band & ch & (d["tc_dbgNN"] == 2) & (d["tc_dbgNL"] == 5)
print("   cell n=%d fake=%.4f (%.1f%% of band fakes)" % (cell.sum(), fk[cell].mean(),
      100.0 * fk[cell].sum() / b0f))
print("   mPfloor mDfloor   killF   killT | band fake")
for fp in (2.0, 3.0, 4.0):
    for fd in (-2.0, -1.0, 0.0, 1.0):
        kill = cell & (d["tc_dbgMP"] < fp) & (d["tc_dbgMD"] < fd)
        kf, kt = (kill & fk).sum(), (kill & ~fk).sum()
        print("   %6.1f %6.1f %7d %7d | %.4f" % (fp, fd, kf, kt, (b0f - kf) / (b0n - kill.sum())))

print("\n== SHADOW SCAN: transition-band 5+IP mX floor (-MRI 0.5 anchor) ==")
print("   floor   killF   killT | band fake")
for f in (0.5, 1.0, 1.5, 2.0, 2.5):
    kill = band & ch & (d["tc_dbgBr"] == 2) & (mX < f)
    kf, kt = (kill & fk).sum(), (kill & ~fk).sum()
    print("  %6.2f %7d %7d | %.4f" % (f, kf, kt, (b0f - kf) / (b0n - kill.sum())))

print("\n== inner-layer >= 2 chains in the band (a composition signature) ==")
s = band & ch & (d["tc_dbgInLay"] >= 2)
print("   n=%d fake=%.4f  fakeshare=%.1f%%  -- by branch:" % (s.sum(), fk[s].mean(),
      100.0 * fk[s].sum() / b0f))
for b in (0, 1, 2, 3):
    ss = s & (d["tc_dbgBr"] == b)
    if ss.sum() > 20:
        print("      br=%d n=%6d fake=%.4f" % (b, ss.sum(), fk[ss].mean()))
