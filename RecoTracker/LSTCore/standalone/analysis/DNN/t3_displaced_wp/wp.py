#!/usr/bin/env python3
"""Lane T3-DESIGN: displaced-aware T3 DNN working points.  Method = the training notebook's (per pT x eta bin, a
percentile of the displaced score of true displaced objects), but on the UNBIASED population of the probe (true
consecutive segment pairs incl. the ones the DNN refuses), and required per vxy bin.
usage: wp.py <derive.npz> <test.npz> [retention=0.996] [mincell=300]   READ-ONLY."""
import sys
import numpy as np
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from anat import load, BINS
from need import WPP, WPD, wp_bins

VB = [(2.5, 10), (10, 25), (25, 37.2), (37.2, 52.4)]


def derive(d, ret, mincell):
    r = d["r"]; pi, ei = wp_bins(r)
    wp = WPD.copy(); info = {}
    for p in range(2):
        for e in range(10):
            best = WPD[p, e]
            for lo, hi in VB:
                m = d["ok"] & (d["vxy"] >= lo) & (d["vxy"] < hi) & (pi == p) & (ei == e)
                if m.sum() < mincell:
                    continue
                need = m & ~(r["score"][:, 1] > WPP[p, e])      # only pairs the prompt working point does not already pass
                k = int(np.floor((1 - ret) * m.sum()))           # pairs allowed to fail
                s = np.sort(r["score"][need, 2])                 # numpy on a local table, not an LST kernel
                if len(s) > k:
                    best = min(best, s[k] * 0.999)
            wp[p, e] = best
    return wp


def evaluate(d, wp, label):
    r = d["r"]; pi, ei = wp_bins(r)
    pas = (r["score"][:, 1] > WPP[pi, ei]) | (r["score"][:, 2] > wp[pi, ei])
    print("\n%s" % label)
    for b, lo, hi in BINS:
        m = d["ok"] & (d["vxy"] >= lo) & (d["vxy"] < hi)
        print("- %s: %.4f (%d)" % (b, pas[m].mean(), m.sum()))
    nt = ~d["true"]
    print("- not-true records: %.4f (%d)" % (pas[nt].mean(), nt.sum()))


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    ret = float(sys.argv[3]) if len(sys.argv) > 3 else 0.996
    mincell = int(sys.argv[4]) if len(sys.argv) > 4 else 300
    wp = derive(a, ret, mincell)
    print("retention %.4f per (pT, eta, vxy) cell with >= %d pairs; derived on %s" % (ret, mincell, sys.argv[1].rsplit("/", 1)[1]))
    for p in range(2):
        print("pt bin %d stock  : " % p + ", ".join("%.4f" % x for x in WPD[p]))
        print("pt bin %d derived: " % p + ", ".join("%.4g" % x for x in wp[p]))
    evaluate(a, WPD, "stock working point, derive sample"); evaluate(a, wp, "derived, derive sample"); evaluate(b, wp, "derived, TEST sample")
    for f in (0.05, 0.02):
        evaluate(b, WPD * f, "uniform scale f = %g, TEST sample" % f)
    wp0 = WPD.copy(); wp0[:, :] = -1.0
    evaluate(b, wp0, "displaced working point removed (always pass), TEST sample")


if __name__ == "__main__":
    main()
