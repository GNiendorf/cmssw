#!/usr/bin/env python3
"""ANGLE B2: pick the -TT working point from the trimdump ledger.

Columns: evt chain nNodes nLayF nLayI nLayO xyF rzF xyI rzI xyO rzO mfF mfI mfO
Decision replica = Trim.cc: eligible end iff its remaining nLayers >= TL; ratio =
chi2Full / max(chi2Rem, 1e-9) with chi2 = xy + rz; larger ratio wins (inner on ties);
trim iff ratio > TT.
"""
import sys
import numpy as np

path = sys.argv[1] if len(sys.argv) > 1 else "trimstudy30.txt"
TL = 4
d = np.loadtxt(path)
nN = d[:, 2].astype(int)
nLF, nLI, nLO = d[:, 3].astype(int), d[:, 4].astype(int), d[:, 5].astype(int)
cF = d[:, 6] + d[:, 7]
cI = d[:, 8] + d[:, 9]
cO = d[:, 10] + d[:, 11]
mfF, mfI, mfO = d[:, 12], d[:, 13], d[:, 14]
FLOOR = 1e-9
rI = np.where(nLI >= TL, cF / np.maximum(cI, FLOOR), -1.0)
rO = np.where(nLO >= TL, cF / np.maximum(cO, FLOOR), -1.0)
best = np.where(rI >= rO, rI, rO)
mfBest = np.where(rI >= rO, mfI, mfO)
trueF = mfF > 0.75

print("candidates = %d  (nNodes: %s)" % (len(d), dict(zip(*np.unique(nN, return_counts=True)))))
print("  full-chain harness TRUE (mf>0.75) = %d (%.3f), FAKE = %d" % (trueF.sum(), trueF.mean(), (~trueF).sum()))
band = (mfF > 0.5) & (mfF <= 0.75)
print("  contaminated band mf in (0.5,0.75] = %d (%.3f of candidates)" % (band.sum(), band.mean()))
elig = (rI > 0) | (rO > 0)
conv = (~trueF) & ((mfI > 0.75) & (rI > 0) | (mfO > 0.75) & (rO > 0))
print("  ORACLE ceiling: fakes convertible by SOME eligible terminal trim = %d (%.3f of fakes, %.3f of band)"
      % (conv.sum(), conv.sum() / max((~trueF).sum(), 1), (conv & band).sum() / max(band.sum(), 1)))
print("  eligible (>=1 end keeps >=%d layers) = %d (%.3f)" % (TL, elig.sum(), elig.mean()))

print()
hdr = "%8s %8s %7s %7s %7s %7s %7s %8s %8s"
print(hdr % ("TT", "ntrim", "frac", "F->T", "T->F", "F->F", "T->T", "net", "ratio"))
for TT in [1.0, 1.5, 2.0, 3.0, 5.0, 10.0, 20.0, 50.0, 100.0, 1e3, 1e4]:
    sel = best > TT
    ft = (sel & ~trueF & (mfBest > 0.75)).sum()
    tf = (sel & trueF & (mfBest <= 0.75)).sum()
    ff = (sel & ~trueF & (mfBest <= 0.75)).sum()
    tt = (sel & trueF & (mfBest > 0.75)).sum()
    print(hdr % ("%.4g" % TT, sel.sum(), "%.4f" % sel.mean(), ft, tf, ff, tt, ft - tf,
                 "%.2f" % (ft / max(tf, 1))))

# Per-nNodes breakdown at a few working points
print()
for TT in [3.0, 5.0, 10.0]:
    sel = best > TT
    print("TT=%g by nNodes:" % TT)
    for n in sorted(set(nN.tolist())):
        m = sel & (nN == n)
        if m.sum() == 0:
            continue
        ft = (m & ~trueF & (mfBest > 0.75)).sum()
        tf = (m & trueF & (mfBest <= 0.75)).sum()
        print("   nNodes=%d ntrim=%6d  F->T=%5d  T->F=%5d  net=%+5d" % (n, m.sum(), ft, tf, ft - tf))
