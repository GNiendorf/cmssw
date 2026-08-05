#!/usr/bin/env python3
"""tr_lost.py -- transition-band efficiency-loss anatomy vs the LST baseline."""
import sys
import numpy as np

SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-"
           "LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")
tag = sys.argv[1] if len(sys.argv) > 1 else "base"
a = np.load(f"{SCRATCH}/m16r_sims_{tag}.npy")
ok = (np.abs(a["vz"]) < 30) & (a["q"] != 0)
ae = np.abs(a["eta"])
BANDS = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 4.5)]
print("== per-band efficiency ==")
for nm, lo, hi in BANDS:
    m = ok & (ae >= lo) & (ae < hi)
    print("  %-11s n=%6d  proto=%.4f  LST=%.4f  delta=%+.4f | LOST=%4d GAINED=%4d net=%+d"
          % (nm, m.sum(), a["anyTC"][m].mean(), a["baseTC"][m].mean(),
             a["anyTC"][m].mean() - a["baseTC"][m].mean(),
             (m & a["baseTC"] & ~a["anyTC"]).sum(), (m & ~a["baseTC"] & a["anyTC"]).sum(),
             (m & ~a["baseTC"] & a["anyTC"]).sum() - (m & a["baseTC"] & ~a["anyTC"]).sum()))

band = ok & (ae >= 1.1) & (ae < 1.7)
lost = band & a["baseTC"] & ~a["anyTC"]
gain = band & ~a["baseTC"] & a["anyTC"]
print("\n== transition LOST sims (LST got them, we do not): n=%d ==" % lost.sum())
print("  by which BASELINE type delivered them (multi-flag, >0.75 frac):")
for f, lab in (("base7", "pT5 (type 7)"), ("base5", "pT3 (type 5)"), ("base8", "pLS (type 8)"),
               ("baseOT", "bare T5/T4 (4/9)")):
    print("     %-18s %4d (%5.1f%% of lost) | vs %5.1f%% among band sims LST got"
          % (lab, (lost & a[f]).sum(), 100.0 * (lost & a[f]).sum() / max(1, lost.sum()),
             100.0 * (band & a["baseTC"] & a[f]).sum() / max(1, (band & a["baseTC"]).sum())))
only7 = lost & a["base7"] & ~a["base5"] & ~a["base8"] & ~a["baseOT"]
print("     LOST delivered ONLY as pT5 by LST: %d (%.1f%%)" % (only7.sum(), 100.0 * only7.sum() / max(1, lost.sum())))
print("\n  pt spectrum of LOST vs all band sims:")
edges = [0.9, 1.1, 1.4, 1.7, 2.2, 3.0, 5.0, 10.0, 1e9]
for i in range(len(edges) - 1):
    s = lost & (a["pt"] >= edges[i]) & (a["pt"] < edges[i + 1])
    d = band & a["baseTC"] & (a["pt"] >= edges[i]) & (a["pt"] < edges[i + 1])
    print("     pt [%5.1f,%5.1f) lost=%4d (%5.1f%%)  base-den=%5d  loss-frac=%.4f"
          % (edges[i], min(edges[i + 1], 999), s.sum(), 100.0 * s.sum() / max(1, lost.sum()),
             d.sum(), s.sum() / max(1, d.sum())))
print("\n  vxy of LOST:")
for lo, hi, lab in ((0, 1, "prompt vxy<1"), (1, 5, "1-5"), (5, 10, "5-10"), (10, 30, "10-30"), (30, 1e9, ">30")):
    s = lost & (a["vxy"] >= lo) & (a["vxy"] < hi)
    print("     %-14s %4d (%5.1f%%)" % (lab, s.sum(), 100.0 * s.sum() / max(1, lost.sum())))
print("\n  |eta| sub-bins of LOST (band only):")
for lo in (1.1, 1.2, 1.3, 1.4, 1.5, 1.6):
    s = lost & (ae >= lo) & (ae < lo + 0.1)
    d = band & a["baseTC"] & (ae >= lo) & (ae < lo + 0.1)
    dn = ok & (ae >= lo) & (ae < lo + 0.1)
    print("     |eta| [%.1f,%.1f) lost=%4d  LSTden=%5d lossfrac=%.4f | eff proto=%.4f LST=%.4f d=%+.4f"
          % (lo, lo + 0.1, s.sum(), d.sum(), s.sum() / max(1, d.sum()),
             a["anyTC"][dn].mean(), a["baseTC"][dn].mean(),
             a["anyTC"][dn].mean() - a["baseTC"][dn].mean()))
print("\n== transition GAINED sims: n=%d ==" % gain.sum())
for lo, hi, lab in ((0, 1, "prompt vxy<1"), (1, 5, "1-5"), (5, 10, "5-10"), (10, 30, "10-30"), (30, 1e9, ">30")):
    s = gain & (a["vxy"] >= lo) & (a["vxy"] < hi)
    print("     %-14s %4d (%5.1f%%)" % (lab, s.sum(), 100.0 * s.sum() / max(1, gain.sum())))
print("\n== transition: chain-delivered vs pixel-delivered coverage of band sims LST gets ==")
m = band & a["baseTC"]
print("   our chainTC covers %.4f | our carried pixel covers %.4f | either %.4f"
      % (a["chainTC"][m].mean(), a["pixTC"][m].mean(), a["anyTC"][m].mean()))
mm = band & a["base7"]
print("   among band sims LST delivered as pT5 (n=%d): our chain covers %.4f, pixel %.4f, any %.4f"
      % (mm.sum(), a["chainTC"][mm].mean(), a["pixTC"][mm].mean(), a["anyTC"][mm].mean()))
