#!/usr/bin/env python3
"""W5: side-by-side of the deployed arms' judge JSONs.

usage: cmp.py <jet|pu|cube50|cubehi5> BASE ARM [ARM ...]
"""
import json
import os
import sys

R = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/w5_ref/runs"

JETF = ["eff_core", "eff_all", "fake", "dup"]
PUF = ["eff_overall_incut", "eff_barrel", "eff_vxy_1_5", "eff_vxy_5_10", "eff_vxy_10_30",
       "eff_dxy_0_1", "eff_dxy_1_5", "eff_dxy_5_10", "eff_dxy_10_30",
       "fake_overall", "dup_overall"]


def load(tag, which):
    p = os.path.join(R, "%s_%s.json" % (tag, which))
    if not os.path.exists(p):
        return None
    d = json.load(open(p))
    v = list(d.values())[0] if len(d) == 1 and isinstance(list(d.values())[0], dict) else d
    return v


def main(which, tags):
    arms = {t: load(t, which) for t in tags}
    miss = [t for t, v in arms.items() if v is None]
    if miss:
        print("MISSING: %s" % miss)
    tags = [t for t in tags if arms[t] is not None]
    if not tags:
        return 1
    base = arms[tags[0]]
    if which == "jet":
        fields = JETF
        extra = ("band", "band_n", "band_d")
    else:
        fields = [f for f in PUF if f in base]
        extra = None
    print("%-10s " % "arm" + " ".join("%14s" % f for f in fields))
    for t in tags:
        a = arms[t]
        row = "%-10s " % t
        for f in fields:
            if t == tags[0]:
                row += "%14.5f" % a[f]
            else:
                row += "%14s" % ("%+.5f" % (a[f] - base[f]))
        print(row)
    if extra and "band_n" in base:
        print("\njet dR bands (band_n/band_d):")
        for t in tags:
            a = arms[t]
            r = [n / d for n, d in zip(a["band_n"], a["band_d"])]
            rb = [n / d for n, d in zip(base["band_n"], base["band_d"])]
            print("%-10s " % t + " ".join(
                ("%8.4f" % x) if t == tags[0] else ("%+8.4f" % (x - y)) for x, y in zip(r, rb)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2:]))
