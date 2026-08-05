#!/usr/bin/env python3
"""m13_report5.py -- MISSION 3 part 5: band-resolved efficiency collateral vs the
measured w7-over-baseline band margins (the winner-rule test)."""
import numpy as np

SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-"
           "RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")
S = np.load(f"{SCRATCH}/m13_shadow.npy")
F = S["isFake"] == 1
T = ~F
LOSS = T & (S["simPt"] > 0.9) & (S["uniqueDeliverer"] == 1)
V = S["simVxy"]
BANDS = [(0, 1, "vxy[0,1)"), (1, 5, "vxy[1,5)"), (5, 10, "vxy[5,10)"),
         (10, 30, "vxy[10,30)"), (30, 1e9, "vxy>=30")]
# w7 vs LST-baseline margins in SIMS per 300 events (exact ratios recovered from
# ab_m12_w7.json: value * denominator is integral for these denominators)
MARGIN = {"vxy[1,5)": "+? (denom n/a)", "vxy[5,10)": "+45 of 634", "vxy[10,30)": "n/a"}

rules = [("A shadow dR<0.02 & mX<0", (S["sX02"] == 1) & (S["mX"] < 0)),
         ("A shadow dR<0.05 & mX<0", (S["sX05"] == 1) & (S["mX"] < 0)),
         ("A shadow dR<0.10 & mX<0", (S["sX10"] == 1) & (S["mX"] < 0)),
         ("A shadow dR<0.10 & mX<1", (S["sX10"] == 1) & (S["mX"] < 1)),
         ("A' dR<0.02 gap4 & mX<0", (S["bX02"] > -90) & (S["bX02"] - S["mX"] > 4) & (S["mX"] < 0)),
         ("A' dR<0.10 gap4 & mX<0", (S["bX10"] > -90) & (S["bX10"] - S["mX"] > 4) & (S["mX"] < 0)),
         ("B absolute mX<-0.5", S["mX"] < -0.5),
         ("B absolute mX<0", S["mX"] < 0.0)]
print(f"{'rule':>24} {'fakeKill':>8} | " + " ".join(f"{b[2]:>11}" for b in BANDS))
print(f"{'(sims lost per band)':>24} {'':>8} | " +
      " ".join(f"{int((LOSS & (V >= lo) & (V < hi)).sum()):>11}" for lo, hi, _ in BANDS) +
      "   <- total irreplaceable pool")
for nm, k in rules:
    line = f"{nm:>24} {int((k & F).sum()):>8d} | "
    for lo, hi, _ in BANDS:
        line += f"{int((k & LOSS & (V >= lo) & (V < hi)).sum()):>11d} "
    print(line)
print()
print("w7-over-baseline band margins (sims / 300 evt, exact from ab_m12_w7.json):")
print("   dxy[1,5)  480 vs 465 of 932   -> +15 sims")
print("   dxy[5,10)  66 vs  65 of 285   -> +1 sim")
print("   vxy[5,10) 459 vs 414 of 634   -> +45 sims")
print("   dxy[10,30) FAILS already (formation-side)")
