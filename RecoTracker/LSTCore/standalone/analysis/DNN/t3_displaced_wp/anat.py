#!/usr/bin/env python3
"""Lane T3-DESIGN (rung 3): closure of the probe and anatomy of refused true consecutive segment pairs.

usage: anat.py <join.npz> [label]      READ-ONLY, prints markdown.
"""
import sys
from math import sqrt

import numpy as np

BINS = [("2.5-10", 2.5, 10), ("10-25", 10, 25), ("band 2.5-30", 2.5, 30), ("25-37.2", 25, 37.2), ("37.2-52.4", 37.2, 52.4),
        ("0.1-2.5", 0.1, 2.5)]


def load(path):
    z = np.load(path, allow_pickle=False)
    r = z["rec"]
    d = dict(r=r, tk=z["tk"], lp3=z["lp3"], lay3=z["lay3"])
    for k in z.files:
        if k.startswith("trk_"):
            d[k] = z[k]
    true = d["tk"] >= 0
    t = np.where(true, d["tk"], 0)
    d["true"] = true
    d["vxy"] = np.where(true, d["trk_vxy"][t], np.nan)
    d["pt"] = np.where(true, d["trk_pt"][t], np.nan)
    d["pdg"] = np.where(true, d["trk_pdg"][t], 0)
    d["C4"] = true & d["trk_C4"][t]
    d["ok"] = d["C4"] & np.all(d["lp3"] >= 0.8, axis=1)
    d["made"] = r["t3Idx"] >= 0
    d["anyLoose"] = r["mdLoose"].any(axis=1)
    d["gateDir"] = d["anyLoose"] & (r["dirFail"] != 0)
    return d


def frac(k, n):
    if n == 0:
        return "-"
    p = k / n
    return "%.4f +- %.4f (%d)" % (p, sqrt(max(p * (1 - p), 1e-12) / n), n)


def main():
    d = load(sys.argv[1])
    label = sys.argv[2] if len(sys.argv) > 2 else ""
    r = d["r"]
    n = len(r)
    print("# T3 probe anatomy %s" % label)
    print("records %d, true consecutive segment pairs %d, on C4 tracks %d, of which local pT >= 0.8 on all three MDs %d"
          % (n, int(d["true"].sum()), int(d["C4"].sum()), int(d["ok"].sum())))
    auth = r["verdictAuth"] != 0
    print("\n## closure (all records, true or not)")
    print("unmodified function == object exists in the SoA: %d / %d = %.6f" % (int((auth == d["made"]).sum()), n, (auth == d["made"]).mean()))
    comp = (r["pointing"] != 0) & ~d["gateDir"] & (r["rzPass"] != 0) & (r["dnnPass"] != 0)
    print("product of the probe-mode components == unmodified function: %d / %d = %.6f" % (int((comp == auth).sum()), n, (comp == auth).mean()))
    print("made but unmodified function says no: %d ; function says yes but not made: %d" % (int((d["made"] & ~auth).sum()), int((auth & ~d["made"]).sum())))
    fl = r["t3Flags"][d["made"]]
    print("made T3s: loose-pointing flag %d, direction-fail flag %d, of %d" % (int((fl & 1 > 0).sum()), int((fl & 2 > 0).sum()), len(fl)))

    cuts = [("pointing (beyond the widened bound)", r["pointing"] == 0), ("direction test, gated by mdLoose", d["gateDir"]),
            ("r-z: no region", (r["rzPass"] == 0) & (r["rzRegion"] < 0)), ("r-z: region cut", (r["rzPass"] == 0) & (r["rzRegion"] >= 0)),
            ("DNN", r["dnnPass"] == 0)]
    for nm, den in (("local pT >= 0.8 (criterion)", d["ok"]), ("ALL", d["C4"])):
        print("\n## true pairs on C4 tracks -> T3, %s" % nm)
        print("| vxy bin | survival | refused | " + " | ".join(c[0] for c in cuts) + " | sole cause: " + " / ".join(c[0].split()[0] for c in cuts) + " |")
        print("|---|---|---|" + "---|" * (len(cuts) + 1))
        for b, lo, hi in BINS:
            m = den & (d["vxy"] >= lo) & (d["vxy"] < hi)
            rej = m & ~d["made"]
            nfail = sum((c[1] & rej).astype(int) for c in cuts)
            sole = [int((c[1] & rej & (nfail == 1)).sum()) for c in cuts]
            print("| %s | %s | %d | %s | %s |" % (b, frac(int((m & d["made"]).sum()), int(m.sum())), int(rej.sum()),
                                                 " | ".join(str(int((c[1] & rej).sum())) for c in cuts), " / ".join(map(str, sole))))
    print("\n## by lstLayers combination, criterion denominator, vxy 2.5-52.4")
    m0 = d["ok"] & (d["vxy"] >= 2.5) & (d["vxy"] < 52.4)
    combos, cnt = np.unique(r["lstLayer"][m0], axis=0, return_counts=True)
    print("| layers | pairs | survival | refused | " + " | ".join(c[0].split()[0] + ("-" + c[0].split()[1] if c[0].startswith("r-z") else "") for c in cuts) + " | rz region |")
    print("|---|---|---|---|" + "---|" * (len(cuts) + 1))
    for cb, c_ in zip(combos, cnt):
        m = m0 & np.all(r["lstLayer"] == cb, axis=1)
        rej = m & ~d["made"]
        regs = np.unique(r["rzRegion"][m])
        print("| %s | %d | %.4f | %d | %s | %s |" % (tuple(int(x) for x in cb), c_, (m & d["made"]).sum() / c_, int(rej.sum()),
                                                   " | ".join(str(int((c[1] & rej).sum())) for c in cuts), ",".join(map(str, regs))))
    print("\n## the direction-test gate: true pairs with any loose MD, criterion denominator")
    print("| vxy bin | pairs | any MD loose | loose and direction-fail (refused by the gate) | direction-fail among NOT loose (flag only) | refused by the gate ONLY |")
    print("|---|---|---|---|---|---|")
    for b, lo, hi in BINS:
        m = d["ok"] & (d["vxy"] >= lo) & (d["vxy"] < hi)
        others = (r["pointing"] == 0) | (r["rzPass"] == 0) | (r["dnnPass"] == 0)
        print("| %s | %d | %d | %d | %d | %d |" % (b, int(m.sum()), int((m & d["anyLoose"]).sum()), int((m & d["gateDir"]).sum()),
                                                  int((m & ~d["anyLoose"] & (r["dirFail"] != 0)).sum()), int((m & d["gateDir"] & ~others).sum())))
    print("\n## track level (C4 tracks with >= 1 true pair at local pT >= 0.8): owns >= 1 T3 / >= 2 T3s")
    print("| vxy bin | tracks | >= 1 true T3 | >= 2 true T3s |")
    print("|---|---|---|---|")
    tkk = d["tk"][d["ok"]]
    mk = d["made"][d["ok"]]
    nt = np.bincount(tkk, minlength=len(d["trk_vxy"]))
    nm_ = np.bincount(tkk, weights=mk.astype(float), minlength=len(d["trk_vxy"]))
    for b, lo, hi in BINS:
        m = (nt > 0) & (d["trk_vxy"] >= lo) & (d["trk_vxy"] < hi)
        print("| %s | %d | %.4f | %.4f |" % (b, int(m.sum()), (nm_[m] >= 1).mean() if m.sum() else 0, (nm_[m] >= 2).mean() if m.sum() else 0))


if __name__ == "__main__":
    main()
