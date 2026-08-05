#!/usr/bin/env python3
"""ANGLE B2: joint scan of the trim guards (min layers after, min nodes after, TT)."""
import sys
import numpy as np

path = sys.argv[1] if len(sys.argv) > 1 else "trimstudy30.txt"
d = np.loadtxt(path)
nN = d[:, 2].astype(int)
nLI, nLO = d[:, 4].astype(int), d[:, 5].astype(int)
cF = d[:, 6] + d[:, 7]
cI = d[:, 8] + d[:, 9]
cO = d[:, 10] + d[:, 11]
mfF, mfI, mfO = d[:, 12], d[:, 13], d[:, 14]
trueF = mfF > 0.75
FLOOR = 1e-9

print("%4s %5s %8s %8s %7s %7s %7s %8s %8s" % ("TL", "minN", "TT", "ntrim", "F->T", "T->F", "net", "TP:FP", "trim/evt"))
for minNodesAfter in (2, 3):
    for TL in (4, 5, 6):
        for TT in (1.0, 2.0, 3.0, 5.0, 10.0, 30.0):
            okI = (nLI >= TL) & (nN - 1 >= minNodesAfter)
            okO = (nLO >= TL) & (nN - 1 >= minNodesAfter)
            rI = np.where(okI, cF / np.maximum(cI, FLOOR), -1.0)
            rO = np.where(okO, cF / np.maximum(cO, FLOOR), -1.0)
            best = np.where(rI >= rO, rI, rO)
            mfB = np.where(rI >= rO, mfI, mfO)
            sel = best > TT
            ft = int((sel & ~trueF & (mfB > 0.75)).sum())
            tf = int((sel & trueF & (mfB <= 0.75)).sum())
            print("%4d %5d %8.4g %8d %7d %7d %+8d %8s %8.1f"
                  % (TL, minNodesAfter, TT, sel.sum(), ft, tf, ft - tf,
                     "%.1f" % (ft / max(tf, 1)), sel.sum() / 30.0))
        print()
