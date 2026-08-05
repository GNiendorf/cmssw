#!/usr/bin/env python3
"""Denominator sizes for every metric, so a delta can be read as tracks, not decimals."""
import sys
from a07_sim import load, prep, band, ETA_CUT

ev = prep(load(sys.argv[1]))
effD = 0
regED = [0, 0, 0]
vD = [0] * 4
dD = [0] * 4
nTC = 0
regN = [0, 0, 0]
for e in ev:
    for i in range(len(e["type"])):
        if e["incut"][i]:
            nTC += 1
            regN[e["treg"][i]] += 1
    for s in range(e["nsim"]):
        if e["sden"][s]:
            effD += 1
            if e["sreg"][s] is not None:
                regED[e["sreg"][s]] += 1
        if e["sbden"][s]:
            b = band(e["svxy"][s])
            if b is not None:
                vD[b] += 1
            b = band(abs(e["sdxy"][s]))
            if b is not None:
                dD[b] += 1
print("300-event denominators (one unit of the rate = 1/denominator)")
print("  eff overall (in-cut sims)      %8d   -> 1 track = %.6f" % (effD, 1.0 / effD))
for j, n in enumerate(("barrel", "transition", "endcap")):
    print("  eff %-26s %8d   -> 1 track = %.6f" % (n, regED[j], 1.0 / regED[j]))
for j, n in enumerate(("vxy[0,1)", "vxy[1,5)", "vxy[5,10)", "vxy[10,30)")):
    print("  eff %-26s %8d   -> 1 track = %.6f" % (n, vD[j], 1.0 / vD[j] if vD[j] else 0))
for j, n in enumerate(("dxy[0,1)", "dxy[1,5)", "dxy[5,10)", "dxy[10,30)")):
    print("  eff %-26s %8d   -> 1 track = %.6f" % (n, dD[j], 1.0 / dD[j] if dD[j] else 0))
print("  fake/dup denominator (in-cut TC) %6d   -> 1 TC    = %.7f" % (nTC, 1.0 / nTC))
for j, n in enumerate(("barrel", "transition", "endcap")):
    print("  fake/dup %-21s %8d   -> 1 TC    = %.7f" % (n, regN[j], 1.0 / regN[j]))
