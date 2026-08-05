#!/usr/bin/env python3
"""Markdown Pareto table for the claimshare grid: cs_table.py <tag> [tag ...]

Emits value rows plus delta-vs-anchor (ctl_noatt = the gate1 run) and delta-vs-LST
(the base column of compare_ab.py) for the bar metrics.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
COLS = [("eff_overall_incut", "effAll"), ("eff_vxy_0_1", "vxy01"), ("eff_vxy_1_5", "vxy15"),
        ("eff_vxy_5_10", "vxy510"), ("eff_vxy_10_30", "vxy1030"), ("eff_dxy_1_5", "dxy15"),
        ("eff_dxy_5_10", "dxy510"), ("dup_overall_incut", "dup"), ("fake_overall_incut", "fake")]
FLOORS = {"eff_vxy_1_5": 0.7832, "eff_vxy_5_10": 0.7109, "eff_vxy_10_30": 0.6941,
          "eff_dxy_1_5": 0.5398, "eff_dxy_5_10": 0.2471}
CEIL = {"fake_overall_incut": 0.0536, "dup_overall_incut": 0.0643}


def load(tag):
    p = os.path.join(HERE, "cs_%s.json" % tag)
    return json.load(open(p))["metrics"] if os.path.exists(p) else None


def counts(tag):
    log = os.path.join(HERE, "cs_%s.log" % tag)
    nk, tc = "-", "-"
    if os.path.exists(log):
        for line in open(log, errors="ignore"):
            if "post-arb dedup" in line:
                m = re.search(r"total=(\d+)", line)
                nk = m.group(1) if m else nk
            elif line.strip().startswith("chain TCs"):
                m = re.search(r"total=(\d+)", line)
                tc = m.group(1) if m else tc
    return tc, nk


def main():
    tags = sys.argv[1:]
    anc = load("gate1")
    hdr = "| config | chainTC | ddKill | " + " | ".join(s for _, s in COLS) + " | verdict |"
    print(hdr)
    print("|" + "---|" * (len(COLS) + 4))
    for t in tags:
        m = load(t)
        if m is None:
            continue
        tc, nk = counts(t)
        cells, bad = [], []
        for k, s in COLS:
            v = m[k]["proto"]
            d = v - anc[k]["proto"]
            cells.append("%.4f (%+.4f)" % (v, d))
            if k in FLOORS and v < FLOORS[k] - 5e-5:
                bad.append(s)
            if k in CEIL and v > CEIL[k] + 5e-5:
                bad.append(s)
        print("| %s | %s | %s | %s | %s |" % (t, tc, nk, " | ".join(cells),
                                              "PASS" if not bad else "FAIL " + ",".join(bad)))
    print()
    print("LST baseline: " + " ".join("%s=%.4f" % (s, anc[k]["base"]) for k, s in COLS))


if __name__ == "__main__":
    main()
