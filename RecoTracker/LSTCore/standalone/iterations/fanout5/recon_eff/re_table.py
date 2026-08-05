#!/usr/bin/env python3
"""re_table.py -- RECON C scoreboard. Reads re_<tag>.json (compare_ab.py) + re_<tag>_hists.root
(for the exact d510 numerator/denominator) + re_<tag>.log (funnel, timing) for each tag.
READ-ONLY on everything outside fanout5/recon_eff.
"""
import json
import os
import re
import sys

import numpy as np
import uproot

P = os.path.dirname(os.path.abspath(__file__))

BANDS = {"d510": ("dxy", 5, 10), "d15": ("dxy", 1, 5), "v01": ("vxy", 0, 1),
         "v15": ("vxy", 1, 5), "v510": ("vxy", 5, 10), "v1030": ("vxy", 10, 30)}


def band_counts(path, var, lo, hi):
    f = uproot.open(path)
    n = f["Root__TC_base_0_0_ef_numer_" + var]
    d = f["Root__TC_base_0_0_ef_denom_" + var]
    hn, e = n.to_numpy()
    hd, _ = d.to_numpy()
    ctr = np.abs(0.5 * (e[:-1] + e[1:]))
    sel = (ctr >= lo) & (ctr < hi)
    return float(hn[sel].sum()), float(hd[sel].sum())


def load(tag):
    j = json.load(open(f"{P}/re_{tag}.json"))["metrics"]
    g = lambda k: j[k]["proto"]
    r = dict(tag=tag, eff=g("eff_overall_incut"), v01=g("eff_vxy_0_1"), v15=g("eff_vxy_1_5"),
             v510=g("eff_vxy_5_10"), v1030=g("eff_vxy_10_30"), d15=g("eff_dxy_1_5"),
             d510=g("eff_dxy_5_10"), fake=g("fake_overall_incut"), dup=g("dup_overall_incut"),
             nb=g("mean_nhitOT_barrel"), nt=g("mean_nhitOT_transition"),
             ne=g("mean_nhitOT_endcap"), ntc=g("n_tc") / 300.0)
    n, d = band_counts(f"{P}/re_{tag}_hists.root", "dxy", 5, 10)
    r["d510n"], r["d510d"] = n, d
    txt = open(f"{P}/re_{tag}.log", errors="ignore").read()
    m = re.search(r"chain funnel\s+in=(\d+) -> theta=(\d+) -> pixdrop=(\d+) -> claim=(\d+)", txt)
    if m:
        r["fun"] = tuple(int(x) for x in m.groups())
    m = re.search(r"time mean/evt\s+infer=([\d.]+) weld=([\d.]+) attach=([\d.]+) arb\+asm=([\d.]+) fill=([\d.]+)", txt)
    if m:
        r["ms"] = sum(float(x) for x in m.groups())
    m = re.search(r"chain TCs\s+total=(\d+)", txt)
    if m:
        r["nchain"] = int(m.group(1))
    return r


HDR = ("tag", "eff", "v01", "v15", "v510", "v1030", "d15", "d510", "d510n", "fake", "dup",
       "nb", "nt", "ne", "ntc", "nchain", "ms")


def main():
    rows = [load(t) for t in sys.argv[1:]]
    print("| " + " | ".join(HDR) + " |")
    print("|" + "---|" * len(HDR))
    for r in rows:
        cells = [r["tag"]]
        for k in HDR[1:]:
            v = r.get(k)
            if v is None:
                cells.append("n/a")
            elif k in ("d510n", "nchain"):
                cells.append("%d" % v)
            elif k in ("nb", "nt", "ne"):
                cells.append("%.3f" % v)
            elif k in ("ntc", "ms"):
                cells.append("%.1f" % v)
            else:
                cells.append("%.4f" % v)
        print("| " + " | ".join(cells) + " |")


if __name__ == "__main__":
    main()
