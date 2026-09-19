#!/usr/bin/env python3
"""Lane T3-DESIGN (rung 3): what each part of the T3 selection would have to admit, from LST's own
variables (probe sidecar), true consecutive segment pairs on C4 tracks at local pT >= 0.8.

usage: need.py <join.npz> [<join2.npz>] [label]      READ-ONLY, prints markdown.
"""
import sys

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from anat import load, BINS  # noqa: E402

WPP = np.array([[0.4957, 0.5052, 0.5201, 0.5340, 0.4275, 0.4708, 0.4890, 0.4932, 0.5400, 0.5449],
                [0.0302, 0.0415, 0.0994, 0.1791, 0.1960, 0.2467, 0.3227, 0.3242, 0.2367, 0.2187]])
WPD = np.array([[0.0334, 0.0504, 0.0748, 0.0994, 0.1128, 0.1123, 0.1118, 0.1525, 0.1867, 0.1847],
                [0.0091, 0.0075, 0.0350, 0.0213, 0.0435, 0.0676, 0.1957, 0.1649, 0.1080, 0.1046]])
K2R = (2.99792458e-3 * 3.8) / 2


def wp_bins(r):
    pt = r["radius"] * K2R * 2
    pi = (pt > 5).astype(int)
    eta = np.abs(r["eta1"])
    ei = np.where(eta > 2.5, 9, np.minimum((eta / 0.25).astype(int), 9))
    return pi, ei


def q(x, ps):
    return " / ".join("%.3g" % v for v in np.quantile(x, ps)) if len(x) else "-"


def main():
    paths = [a for a in sys.argv[1:] if a.endswith(".npz")]
    label = " ".join(a for a in sys.argv[1:] if not a.endswith(".npz"))
    ds = [load(p) for p in paths]
    cat = lambda k: np.concatenate([d[k] for d in ds])
    r = np.concatenate([d["r"] for d in ds])
    ok, vxy, made, anyL = cat("ok"), cat("vxy"), cat("made"), cat("anyLoose")
    true = cat("true")
    print("# need tables %s (%d sidecars)" % (label, len(ds)))

    # ---- DNN
    pi, ei = wp_bins(r)
    replay = (r["score"][:, 1] > WPP[pi, ei]) | (r["score"][:, 2] > WPD[pi, ei])
    print("\n## DNN\nworking-point replay from the dumped scores == LST's verdict: %d / %d = %.6f"
          % (int((replay == (r["dnnPass"] != 0)).sum()), len(r), (replay == (r["dnnPass"] != 0)).mean()))
    print("\n| vxy bin | true pairs | DNN pass | with a loose MD: pairs, pass | without: pairs, pass | fake score of the refused q50/q90 | displaced score of the refused q10/q50 |")
    print("|---|---|---|---|---|---|---|")
    for b, lo, hi in BINS:
        m = ok & (vxy >= lo) & (vxy < hi)
        rf = m & (r["dnnPass"] == 0)
        a, c = m & anyL, m & ~anyL
        print("| %s | %d | %.4f | %d, %.4f | %d, %.4f | %s | %s |" % (b, m.sum(), (r["dnnPass"][m] != 0).mean(), a.sum(),
              (r["dnnPass"][a] != 0).mean() if a.sum() else np.nan, c.sum(), (r["dnnPass"][c] != 0).mean() if c.sum() else np.nan,
              q(r["score"][rf, 0], [.5, .9]), q(r["score"][rf, 2], [.1, .5])))
    print("\nDisplaced working point scaled by f (kWp_displaced x f), prompt one unchanged: true-pair DNN pass per vxy bin, and the pass rate of the"
          " NOT-true records of the sidecar (combinatorial pairs among flagged hits: a biased proxy of the fake side, the deployed count is the referee)")
    fs = [1.0, 0.5, 0.25, 0.1, 0.05, 0.02, 0.01, 0.0]
    print("| vxy bin | " + " | ".join("f=%g" % f for f in fs) + " |")
    print("|---|" + "---|" * len(fs))
    for b, lo, hi in BINS:
        m = ok & (vxy >= lo) & (vxy < hi)
        print("| %s | " % b + " | ".join("%.4f" % ((r["score"][m, 1] > WPP[pi[m], ei[m]]) | (r["score"][m, 2] > f * WPD[pi[m], ei[m]])).mean() for f in fs) + " |")
    m = ~true
    print("| not true (%d) | " % m.sum() + " | ".join("%.4f" % ((r["score"][m, 1] > WPP[pi[m], ei[m]]) | (r["score"][m, 2] > f * WPD[pi[m], ei[m]])).mean() for f in fs) + " |")

    # ---- r-z
    print("\n## r-z: value / cut of true pairs per region (criterion denominator, vxy 2.5-52.4): quantiles 50 / 90 / 99 / 99.5 / 99.9 / max; >1 fails")
    m0 = ok & (vxy >= 2.5) & (vxy < 52.4)
    print("| region | layers | pairs | fallback | pass | value/cut q50 / q90 / q99 / q99.5 / q99.9 / max | scale on the cut for 99% / 99.5% / 99.9% |")
    print("|---|---|---|---|---|---|---|")
    for reg in np.unique(r["rzRegion"][m0]):
        m = m0 & (r["rzRegion"] == reg)
        lay = np.unique(r["lstLayer"][m], axis=0)
        if reg < 0:
            v = r["rzChi2"][m]
            print("| none | %s | %d | %d | 0.0000 | rzChi2 itself: %s | - |" % (";".join(str(tuple(int(x) for x in l)) for l in lay), m.sum(),
                  int((r["rzFallback"][m] != 0).sum()), q(v[np.isfinite(v)], [.5, .9, .99, .995, .999, 1.])))
            continue
        ratio = r["rzValue"][m] / r["rzCut"][m]
        ratio = np.where(np.isfinite(ratio), ratio, np.inf)
        print("| %d | %s | %d | %d | %.4f | %s | %s |" % (reg, ";".join(str(tuple(int(x) for x in l)) for l in lay), m.sum(),
              int((r["rzFallback"][m] != 0).sum()), (r["rzPass"][m] != 0).mean(), q(ratio, [.5, .9, .99, .995, .999, 1.]), q(ratio, [.99, .995, .999])))
    print("\nr-z pass per vxy bin (regions that exist only):")
    for b, lo, hi in BINS:
        m = ok & (vxy >= lo) & (vxy < hi) & (r["rzRegion"] >= 0)
        print("- %s: %.4f (%d)" % (b, (r["rzPass"][m] != 0).mean(), m.sum()))

    # ---- pointing
    print("\n## pointing: |sin betaIn| / sin(betaInCut) of true pairs (criterion denominator); 1 = stock bound, sin(1.7 cut)/sin(cut) = widened bound")
    ratio = np.sqrt(r["sinBetaInSqOverR2"]) / np.sin(r["betaInCut"])
    for b, lo, hi in BINS:
        m = ok & (vxy >= lo) & (vxy < hi)
        print("- %s: q50/q90/q99/q99.9/max %s ; verdict 0/1/2 = %d / %d / %d ; cos<=0: %d" % (b, q(ratio[m], [.5, .9, .99, .999, 1.]),
              int((r["pointing"][m] == 0).sum()), int((r["pointing"][m] == 1).sum()), int((r["pointing"][m] == 2).sum()), int((r["cosPositive"][m] == 0).sum())))

    # ---- direction test
    print("\n## direction test on true pairs (criterion denominator): max pull^2 and mean chi2, by whether the triplet has a loose MD")
    print("| vxy bin | class | pairs | tested (nTested>0) | fail | max pull^2 q50/q90/q99 | mean chi2 q50/q90/q99 |")
    print("|---|---|---|---|---|---|---|")
    for b, lo, hi in BINS:
        for nm, mm in (("any loose MD", anyL), ("no loose MD", ~anyL)):
            m = ok & (vxy >= lo) & (vxy < hi) & mm
            t = m & (r["dirNTested"] > 0)
            print("| %s | %s | %d | %d | %.4f | %s | %s |" % (b, nm, m.sum(), t.sum(), (r["dirFail"][m] != 0).mean() if m.sum() else np.nan,
                  q(r["dirMaxPull2"][t], [.5, .9, .99]), q(r["dirSumChi2"][t] / r["dirNTested"][t], [.5, .9, .99])))


if __name__ == "__main__":
    main()
