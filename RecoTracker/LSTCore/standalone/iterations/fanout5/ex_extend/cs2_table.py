#!/usr/bin/env python3
"""SCANNER 2 scoreboard. Includes the metrics the phase-1 verifiers found missing:
mean nhitOT (all three regions), dxy[10,30), per-eta dup/fake/eff. Floors are the
task's HARD FLOORS; targets are the LST base300 values."""
import glob
import json
import os
import sys

FLOORS = {
    "eff_vxy_1_5": 0.7832,
    "eff_vxy_5_10": 0.7109,
    "eff_vxy_10_30": 0.6941,
    "eff_dxy_1_5": 0.5398,
    "eff_dxy_5_10": 0.2471,
}
LST = {
    "eff_overall_incut": 0.8136, "eff_vxy_0_1": 0.8473, "eff_vxy_1_5": 0.7731,
    "eff_vxy_5_10": 0.6530, "eff_vxy_10_30": 0.6291, "eff_dxy_1_5": 0.4989,
    "eff_dxy_5_10": 0.2281, "fake_overall_incut": 0.0455, "dup_overall_incut": 0.0513,
    "mean_nhitOT_barrel": 10.151, "mean_nhitOT_transition": 10.010,
    "mean_nhitOT_endcap": 3.559,
}
CTL = {
    "eff_overall_incut": 0.8017, "eff_vxy_0_1": 0.8344, "eff_vxy_1_5": 0.7852,
    "eff_vxy_5_10": 0.7129, "eff_vxy_10_30": 0.6961, "eff_dxy_1_5": 0.5418,
    "eff_dxy_5_10": 0.2491, "fake_overall_incut": 0.0536, "dup_overall_incut": 0.0643,
}

COLS = [
    ("eff", "eff_overall_incut", 4), ("vxy01", "eff_vxy_0_1", 4),
    ("v15", "eff_vxy_1_5", 4), ("v510", "eff_vxy_5_10", 4),
    ("v1030", "eff_vxy_10_30", 4), ("dxy01", "eff_dxy_0_1", 4),
    ("d15", "eff_dxy_1_5", 4), ("d510", "eff_dxy_5_10", 4),
    ("d1030", "eff_dxy_10_30", 4),
    ("fake", "fake_overall_incut", 4), ("dup", "dup_overall_incut", 4),
    ("fkTr", "fake_transition", 4), ("dupTr", "dup_transition", 4),
    ("nhB", "mean_nhitOT_barrel", 3), ("nhT", "mean_nhitOT_transition", 3),
    ("nhE", "mean_nhitOT_endcap", 3),
]


def load(tag):
    p = "cs2_%s.json" % tag
    if not os.path.exists(p):
        return None
    return json.load(open(p))["metrics"]


def verdict(m):
    bad = []
    for k, v in FLOORS.items():
        if m[k]["proto"] < v - 1e-9:
            bad.append("%s %.4f<%.4f" % (k, m[k]["proto"], v))
    return "PASS" if not bad else "FAIL: " + " ".join(bad)


def main():
    tags = sys.argv[1:]
    if not tags:
        tags = sorted(os.path.basename(p)[4:-5] for p in glob.glob("cs2_*.json"))
    hdr = "| %-12s | " % "tag" + " | ".join("%-6s" % c[0] for c in COLS) + " | verdict |"
    sep = "|" + "-" * 14 + "|" + "|".join("-" * 8 for _ in COLS) + "|---------|"
    print(hdr)
    print(sep)
    for t in tags:
        m = load(t)
        if m is None:
            print("| %-12s | MISSING" % t)
            continue
        cells = []
        for name, key, prec in COLS:
            v = m[key]["proto"]
            cells.append(("%-6." + str(prec) + "f") % v)
        print("| %-12s | " % t + " | ".join(cells) + " | %s |" % verdict(m))
    print()
    print("LST target : " + " ".join("%s=%s" % (c[0], LST.get(c[1], "-")) for c in COLS))
    print("ctl_noatt  : " + " ".join("%s=%s" % (c[0], CTL.get(c[1], "-")) for c in COLS))


if __name__ == "__main__":
    main()
