#!/usr/bin/env python3
"""Absolute OT-hit budget per region (harness ol_ population: every TC with pt>0.9),
split by fake/real, for the LST baseline and any prototype output."""
import sys

import numpy as np
import uproot
import awkward as ak

S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/"
BASE = S + "LSTNtuple_PU200RelVal_300evt.root"
REG = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 99.0)]


def budget(path, tag):
    br = ["tc_pt", "tc_eta", "tc_nhitOT", "tc_isFake", "tc_isDuplicate"]
    a = uproot.open(path)["tree"].arrays(br, library="ak")
    d = {k: ak.to_numpy(ak.flatten(a[k])) for k in br}
    sel = d["tc_pt"] > 0.9
    ae = np.abs(d["tc_eta"])
    print("%-26s %-11s %8s %11s %8s | %8s %8s %8s" %
          (tag, "region", "nTC", "sumOThits", "mean", "n_real", "n_fake", "n_dup"))
    out = {}
    for r, lo, hi in REG:
        m = sel & (ae >= lo) & (ae < hi)
        n = int(m.sum())
        h = float(d["tc_nhitOT"][m].sum())
        nf = int((d["tc_isFake"][m] > 0).sum())
        nd = int((d["tc_isDuplicate"][m] > 0).sum())
        out[r] = (n, h)
        print("%-26s %-11s %8d %11.0f %8.3f | %8d %8d %8d" %
              ("", r, n, h, h / n if n else 0, n - nf, nf, nd))
    return out


if __name__ == "__main__":
    b = budget(BASE, "LST BASELINE")
    for p in sys.argv[1:]:
        o = budget(p, p.split("/")[-1])
        print("   %-23s %-11s %8s %11s %8s" % ("", "vs LST:", "dnTC", "dHits", "dMean"))
        for r, _, _ in REG:
            print("   %-23s %-11s %+8d %+11.0f %+8.3f  (hits/evt %+7.1f)" %
                  ("", r, o[r][0] - b[r][0], o[r][1] - b[r][1],
                   o[r][1] / o[r][0] - b[r][1] / b[r][0], (o[r][1] - b[r][1]) / 300.0))
