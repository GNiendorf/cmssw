#!/usr/bin/env python3
"""Lane T3-DESIGN: the three families of DNN options on both samples pooled. READ-ONLY.
usage: dnn_options.py <join.npz> <join.npz>"""
import sys
import numpy as np
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from anat import load, BINS
from need import WPP, WPD, wp_bins
from geom import circle


def main():
    D = [load(p) for p in sys.argv[1:]]; Z = [np.load(p) for p in sys.argv[1:]]
    cat = lambda k: np.concatenate([d[k] for d in D])
    r = np.concatenate([d["r"] for d in D]); ok = cat("ok"); vxy = cat("vxy"); true = cat("true"); anyL = cat("anyLoose")
    xyz = np.concatenate([z["xyz"] for z in Z]); _, _, _, d0 = circle(xyz)
    pi, ei = wp_bins(r)
    stock = (r["score"][:, 1] > WPP[pi, ei]) | (r["score"][:, 2] > WPD[pi, ei])
    opts = [("stock", stock)]
    for f in (0.25, 0.1, 0.05, 0.02, 0.01):
        opts.append(("displaced WP x %g" % f, (r["score"][:, 1] > WPP[pi, ei]) | (r["score"][:, 2] > f * WPD[pi, ei])))
    opts.append(("bypass if any MD loose", stock | anyL))
    for x in (8, 4, 2, 1):
        opts.append(("bypass if circle d0 > %g cm" % x, stock | (d0 > x)))
    opts.append(("bypass if loose or circle d0 > 2", stock | anyL | (d0 > 2)))
    opts.append(("no DNN", np.ones(len(r), bool)))
    print("| option | " + " | ".join(b[0] for b in BINS) + " | not-true records pass |")
    print("|---|" + "---|" * (len(BINS) + 1))
    for nm, p in opts:
        print("| %s | " % nm + " | ".join("%.4f" % p[ok & (vxy >= lo) & (vxy < hi)].mean() for _, lo, hi in BINS) + " | %.4f |" % p[~true].mean())


if __name__ == "__main__":
    main()
