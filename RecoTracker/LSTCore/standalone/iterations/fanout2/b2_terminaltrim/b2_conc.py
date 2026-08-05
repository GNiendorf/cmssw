#!/usr/bin/env python3
"""ANGLE B2: is there a CONCENTRATING selector? TT only scales volume (F->T per trim is
flat ~5%), and every wasted trim is pure track-length cost. Test absolute chi2 guards."""
import sys
import numpy as np

d = np.loadtxt(sys.argv[1] if len(sys.argv) > 1 else "trimstudy30.txt")
nLI, nLO = d[:, 4].astype(int), d[:, 5].astype(int)
cF, cI, cO = d[:, 6] + d[:, 7], d[:, 8] + d[:, 9], d[:, 10] + d[:, 11]
mfF, mfI, mfO = d[:, 12], d[:, 13], d[:, 14]
trueF = mfF > 0.75
FLOOR = 1e-9
TL = 5


def ledger(name, sel, mfB):
    ft = int((sel & ~trueF & (mfB > 0.75)).sum())
    tf = int((sel & trueF & (mfB <= 0.75)).sum())
    tt = int((sel & trueF & (mfB > 0.75)).sum())
    n = int(sel.sum())
    print("%-34s ntrim=%6d (%6.1f/evt)  F->T=%4d  T->F=%3d  purity=%.3f  waste(T->T)=%5d  yield/waste=%.4f"
          % (name, n, n / 30.0, ft, tf, ft / max(n, 1), tt, ft / max(tt, 1)))


rI = np.where(nLI >= TL, cF / np.maximum(cI, FLOOR), -1.0)
rO = np.where(nLO >= TL, cF / np.maximum(cO, FLOOR), -1.0)
best = np.where(rI >= rO, rI, rO)
mfB = np.where(rI >= rO, mfI, mfO)
cRem = np.where(rI >= rO, cI, cO)

print("=== baseline ratio-only ladder (TL=5) ===")
for TT in (1.0, 3.0, 10.0, 30.0):
    ledger("TT=%g" % TT, best > TT, mfB)

print("\n=== + ABSOLUTE full-chain chi2 floor (only mis-fitting chains may be trimmed) ===")
for C in (0.01, 0.05, 0.1, 0.3, 1.0, 3.0):
    for TT in (1.0, 3.0):
        ledger("TT=%g & chi2Full>%g" % (TT, C), (best > TT) & (cF > C), mfB)

print("\n=== + ABSOLUTE remainder chi2 ceiling (remainder must be genuinely clean) ===")
for R in (0.3, 0.1, 0.03, 0.01, 0.003):
    for TT in (1.0, 3.0):
        ledger("TT=%g & chi2Rem<%g" % (TT, R), (best > TT) & (cRem < R), mfB)

print("\n=== both ===")
for C in (0.05, 0.1, 0.3):
    for R in (0.03, 0.01):
        ledger("chi2Full>%g & chi2Rem<%g (TT>1)" % (C, R), (best > 1.0) & (cF > C) & (cRem < R), mfB)
