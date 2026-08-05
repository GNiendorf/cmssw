#!/usr/bin/env python3
"""okretrain: markdown scoreboard table over the ab_*.json files.

  python3 ok_table.py tag1 tag2 ...        (or no args = every ab_*.json)
Deltas are vs the ctl_noatt anchor (tag 'base' here) and vs LST (compare_ab base).
"""
import glob
import json
import os
import sys

D = os.path.dirname(os.path.abspath(__file__))

COLS = [("eff", "eff_overall_incut"), ("vxy01", "eff_vxy_0_1"), ("vxy15", "eff_vxy_1_5"),
        ("vxy510", "eff_vxy_5_10"), ("vxy1030", "eff_vxy_10_30"),
        ("dxy15", "eff_dxy_1_5"), ("dxy510", "eff_dxy_5_10"),
        ("fake", "fake_overall_incut"), ("dup", "dup_overall_incut")]
FLOORS = {"eff_vxy_1_5": 0.7832, "eff_vxy_5_10": 0.7109, "eff_vxy_10_30": 0.6941,
          "eff_dxy_1_5": 0.5398, "eff_dxy_5_10": 0.2471}
# Ceilings are the ctl_noatt anchor's OWN exact values (the rounded .0536/.0643 in the
# brief); a run may not exceed them.
CEIL = {"fake_overall_incut": 0.053635, "dup_overall_incut": 0.064277}


def load(tag):
    p = f"{D}/ab_{tag}.json"
    if not os.path.exists(p):
        return None
    return json.load(open(p))["metrics"]


def main():
    tags = sys.argv[1:]
    if not tags:
        tags = sorted(os.path.basename(p)[3:-5] for p in glob.glob(f"{D}/ab_*.json"))
    base = load("base")
    hdr = "| config | " + " | ".join(c[0] for c in COLS) + " | TC/evt | pass |"
    print(hdr)
    print("|" + "---|" * (len(COLS) + 3))
    for t in tags:
        m = load(t)
        if m is None:
            continue
        cells, ok = [], True
        for _, k in COLS:
            v = m[k]["proto"]
            s = f"{v:.4f}"
            if base is not None and t != "base":
                s += f" ({v - base[k]['proto']:+.4f})"
            if k in FLOORS and v < FLOORS[k]:
                ok = False
                s += " !"
            if k in CEIL and v > CEIL[k] + 1e-6:
                ok = False
                s += " !"
            cells.append(s)
        print(f"| {t} | " + " | ".join(cells) + f" | {m['n_tc']['proto'] / 300.0:.1f} | "
              + ("OK" if ok else "FAIL") + " |")
    print()
    print("floors: vxy[1,5)>=.7832 vxy[5,10)>=.7109 vxy[10,30)>=.6941 dxy[1,5)>=.5398 "
          "dxy[5,10)>=.2471 ; ceilings fake<=.0536 dup<=.0643")
    if base is not None:
        print("deltas in parentheses are vs the ctl_noatt anchor (tag 'base').")
    # LST reference row
    if base is not None:
        lst = " | ".join(f"{base[k]['base']:.4f}" for _, k in COLS)
        print(f"\nLST baseline: | {lst} |")


if __name__ == "__main__":
    main()
