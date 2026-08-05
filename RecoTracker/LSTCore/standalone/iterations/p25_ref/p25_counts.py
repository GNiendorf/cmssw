#!/usr/bin/env python3
"""Track counts behind every scoreboard row, for two createPerfNumDenHists files.

usage: p25_counts.py <ref_hists.root> <new_hists.root>

compare_ab.py reports ratios; a ratio delta of 1e-4 is unreadable without the population it is a
ratio of. This prints numerator and denominator INTEGRALS for the same bands compare_ab.py uses,
so every moved row can be quoted as "n tracks".
"""
import sys

import uproot
import numpy as np

EF = "Root__TC_base_0_0_ef_"
FR = "Root__TC_fr_"
DR = "Root__TC_dr_"
VXY_BANDS = [(0, 1), (1, 5), (5, 10), (10, 30)]
ETA_REGIONS = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 10.0)]


def h(f, name):
    return f[name]


def band(hist, lo, hi):
    v = hist.values()
    e = hist.axis().edges()
    c = 0.5 * (e[:-1] + e[1:])
    m = (c >= lo) & (c < hi)
    return float(v[m].sum())


def allsum(hist):
    return float(hist.values().sum())


def rows(path):
    f = uproot.open(path)
    out = {}
    out["eff_overall"] = (allsum(h(f, EF + "numer_pt")), allsum(h(f, EF + "denom_pt")))
    for var in ("vxy", "dxy"):
        hn, hd = h(f, EF + "numer_" + var), h(f, EF + "denom_" + var)
        for lo, hi in VXY_BANDS:
            out["eff_%s_%g_%g" % (var, lo, hi)] = (band(hn, lo, hi), band(hd, lo, hi))
    out["fake_overall"] = (allsum(h(f, FR + "numer_pt")), allsum(h(f, FR + "denom_pt")))
    out["dup_overall"] = (allsum(h(f, DR + "numer_pt")), allsum(h(f, DR + "denom_pt")))
    for reg, lo, hi in ETA_REGIONS:
        out["eff_" + reg] = (band(h(f, EF + "numer_eta"), lo, hi), band(h(f, EF + "denom_eta"), lo, hi))
        out["fake_" + reg] = (band(h(f, FR + "numer_eta"), lo, hi), band(h(f, FR + "denom_eta"), lo, hi))
        out["dup_" + reg] = (band(h(f, DR + "numer_eta"), lo, hi), band(h(f, DR + "denom_eta"), lo, hi))
    out["eff_overall_incut"] = (allsum(h(f, EF + "numer_eta")), allsum(h(f, EF + "denom_eta")))
    out["fake_overall_incut"] = (allsum(h(f, FR + "numer_eta")), allsum(h(f, FR + "denom_eta")))
    out["dup_overall_incut"] = (allsum(h(f, DR + "numer_eta")), allsum(h(f, DR + "denom_eta")))
    return out


def main():
    a, b = rows(sys.argv[1]), rows(sys.argv[2])
    print(f"{'row':<22} {'numer ref':>10} {'numer new':>10} {'dNumer':>8} "
          f"{'denom ref':>10} {'denom new':>10} {'dDenom':>8}")
    print("-" * 84)
    for k in a:
        (na, da), (nb, db) = a[k], b[k]
        mark = "" if (na == nb and da == db) else "   <-- MOVED"
        print(f"{k:<22} {na:>10.0f} {nb:>10.0f} {nb-na:>+8.0f} {da:>10.0f} {db:>10.0f} {db-da:>+8.0f}{mark}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
