#!/usr/bin/env python3
"""DESCENT scoreboard. Same columns as cs2_table.py plus the trim counters and the
raw band numer/denom track counts, so every 1-track claim is auditable."""
import glob
import json
import os
import re
import sys

FLOORS = {
    "eff_vxy_1_5": 0.7832,
    "eff_vxy_5_10": 0.7109,
    "eff_vxy_10_30": 0.6941,
    "eff_dxy_1_5": 0.5398,
    "eff_dxy_5_10": 0.2471,
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
    ("nhE", "mean_nhitOT_endcap", 3), ("nTC", "n_tc", 0),
]
PFX = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].startswith("--pfx=") else None
PREFIX = PFX.split("=", 1)[1] if PFX else "ds_"
ARGS = sys.argv[2:] if PFX else sys.argv[1:]


def load(tag):
    p = "%s%s.json" % (PREFIX, tag)
    if not os.path.exists(p):
        return None
    return json.load(open(p))["metrics"]


def trim_counts(tag):
    p = "%s%s.log" % (PREFIX, tag)
    if not os.path.exists(p):
        return ""
    for ln in open(p, errors="ignore"):
        if "terminal trim" in ln:
            m = re.search(r"total=(\d+) \(([\d.]+)/evt, ([\d.]+) of examined\).*?\| (-TT.*?) \|", ln)
            if m:
                return "trim %s/evt (%.2f%%) [%s]" % (m.group(2), 100 * float(m.group(3)), m.group(4))
    return "trim OFF"


def verdict(m):
    bad = [
        "%s %.4f<%.4f" % (k, m[k]["proto"], v)
        for k, v in FLOORS.items()
        if m[k]["proto"] < v - 1e-9
    ]
    return "PASS" if not bad else "FAIL: " + " ".join(bad)


def main():
    tags = ARGS or sorted(
        os.path.basename(p)[len(PREFIX):-5] for p in glob.glob("%s*.json" % PREFIX))
    print("| %-12s | " % "tag" + " | ".join("%-6s" % c[0] for c in COLS) + " | verdict |")
    print("|" + "-" * 14 + "|" + "|".join("-" * 8 for _ in COLS) + "|---------|")
    for t in tags:
        m = load(t)
        if m is None:
            print("| %-12s | MISSING" % t)
            continue
        cells = [("%-6." + str(p) + "f") % m[k]["proto"] for _, k, p in COLS]
        print("| %-12s | " % t + " | ".join(cells) + " | %s |" % verdict(m))
    print()
    for t in tags:
        print("%-12s %s" % (t, trim_counts(t)))


if __name__ == "__main__":
    main()
