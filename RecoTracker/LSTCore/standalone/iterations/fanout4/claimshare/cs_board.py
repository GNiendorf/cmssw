#!/usr/bin/env python3
"""claimshare scoreboard: cs_*.json -> Pareto table with bar verdicts.

Usage: cs_board.py [tag ...]   (default: every cs_*.json in this directory)
"""
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# Bars from the fan-out brief (ctl_noatt anchor / LST baseline).
CTL = {
    "eff_overall_incut": 0.8017,
    "eff_vxy_0_1": 0.8344,
    "eff_vxy_1_5": 0.7852,
    "eff_vxy_5_10": 0.7129,
    "eff_vxy_10_30": 0.6961,
    "eff_dxy_1_5": 0.5418,
    "eff_dxy_5_10": 0.2491,
    "dup_overall_incut": 0.0643,
    "fake_overall_incut": 0.0536,
}
FLOORS = {  # displaced floors that must hold
    "eff_vxy_1_5": 0.7832,
    "eff_vxy_5_10": 0.7109,
    "eff_vxy_10_30": 0.6941,
    "eff_dxy_1_5": 0.5398,
    "eff_dxy_5_10": 0.2471,
}
CEIL = {"fake_overall_incut": 0.0536, "dup_overall_incut": 0.0643}

COLS = [
    ("eff_overall_incut", "effAll"),
    ("eff_vxy_0_1", "vxy01"),
    ("eff_vxy_1_5", "vxy15"),
    ("eff_vxy_5_10", "vxy510"),
    ("eff_vxy_10_30", "vxy1030"),
    ("eff_dxy_1_5", "dxy15"),
    ("eff_dxy_5_10", "dxy510"),
    ("dup_overall_incut", "dup"),
    ("fake_overall_incut", "fake"),
]


def killed(tag):
    log = os.path.join(HERE, "cs_%s.log" % tag)
    if not os.path.exists(log):
        return "", ""
    n, tc = "", ""
    for line in open(log, errors="ignore"):
        if "post-arb dedup" in line:
            m = re.search(r"total=(\d+)", line)
            if m:
                n = m.group(1)
        elif line.strip().startswith("chain TCs"):
            m = re.search(r"total=(\d+)", line)
            if m:
                tc = m.group(1)
    return n, tc


def main():
    tags = sys.argv[1:]
    if not tags:
        tags = sorted(os.path.basename(p)[3:-5] for p in glob.glob(os.path.join(HERE, "cs_*.json")))
    hdr = "%-14s %8s %7s" % ("tag", "chainTC", "ddKill")
    for _, short in COLS:
        hdr += " %8s" % short
    hdr += "  verdict"
    print(hdr)
    print("-" * len(hdr))
    for t in tags:
        p = os.path.join(HERE, "cs_%s.json" % t)
        if not os.path.exists(p):
            continue
        m = json.load(open(p))["metrics"]
        nk, ntc = killed(t)
        row = "%-14s %8s %7s" % (t, ntc, nk)
        bad = []
        for key, short in COLS:
            v = m[key]["proto"]
            row += " %8.4f" % v
            if key in FLOORS and v < FLOORS[key] - 5e-5:
                bad.append("%s<%.4f" % (short, FLOORS[key]))
            if key in CEIL and v > CEIL[key] + 5e-5:
                bad.append("%s>%.4f" % (short, CEIL[key]))
        row += "  " + ("PASS" if not bad else "FAIL:" + ",".join(bad))
        print(row)
    print()
    print("bars: %s" % " ".join("%s=%.4f" % (s, CTL[k]) for k, s in COLS))


if __name__ == "__main__":
    main()
