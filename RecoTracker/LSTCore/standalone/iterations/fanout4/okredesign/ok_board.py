#!/usr/bin/env python3
"""Scoreboard for the OKR order-key A/Bs.

Reads ok_<tag>.json (compare_ab.py output) and prints one row per tag with the bar
metrics, plus a PASS/FAIL against the ctl_noatt floors. All numbers are pt>0.9.
"""
import glob
import json
import os
import sys

P = os.path.dirname(os.path.abspath(__file__))

# ctl_noatt bars (the anchor this work must not regress) and the LST baseline.
CTL = {"eff": .8017, "vxy01": .8344, "vxy15": .7852, "vxy510": .7129, "vxy1030": .6961,
       "dxy15": .5418, "dxy510": .2491, "fake": .0536, "dup": .0643}
FLOOR = {"vxy15": .7832, "vxy510": .7109, "vxy1030": .6941, "dxy15": .5398, "dxy510": .2471}
KEYS = [("eff", "eff_overall_incut"), ("vxy01", "eff_vxy_0_1"), ("vxy15", "eff_vxy_1_5"),
        ("vxy510", "eff_vxy_5_10"), ("vxy1030", "eff_vxy_10_30"), ("dxy15", "eff_dxy_1_5"),
        ("dxy510", "eff_dxy_5_10"), ("fake", "fake_overall_incut"), ("dup", "dup_overall_incut")]
# MEASURED denominators of this 300-event sample (from ok_base_hists.root), not the
# recon's round numbers: vxy[0,1) has 21137 sims, the pt>0.9 overall set 22784.
N_SIM_PROMPT = 21137
N_SIM_INCUT = 22784


def load(tag):
    with open(os.path.join(P, f"ok_{tag}.json")) as fh:
        m = json.load(fh)["metrics"]
    return {k: m[j]["proto"] for k, j in KEYS}, {k: m[j]["base"] for k, j in KEYS}


def main():
    tags = sys.argv[1:]
    if not tags:
        tags = sorted(os.path.basename(f)[3:-5] for f in glob.glob(os.path.join(P, "ok_*.json")))
        tags = ["base"] + [t for t in tags if t != "base"]
    hdr = (f"{'tag':<16}" + "".join(f"{k:>9}" for k, _ in KEYS)
           + "   floors    d(vxy01)  +sims  +allsims")
    print(hdr)
    print("-" * len(hdr))
    lst = None
    for t in tags:
        try:
            v, base = load(t)
        except FileNotFoundError:
            continue
        lst = base
        bad = [k for k in FLOOR if v[k] < FLOOR[k]]
        ok = "PASS" if not bad else "FAIL:" + ",".join(bad)
        d01 = v["vxy01"] - CTL["vxy01"]
        deff = v["eff"] - CTL["eff"]
        row = f"{t:<16}" + "".join(f"{v[k]:>9.4f}" for k, _ in KEYS)
        print(f"{row}   {ok:<9} {d01:+.4f} {d01 * N_SIM_PROMPT:+6.0f}   {deff * N_SIM_INCUT:+6.0f}")
    print("-" * len(hdr))
    print(f"{'CTL(anchor)':<16}" + "".join(f"{CTL[k]:>9.4f}" for k, _ in KEYS))
    if lst:
        print(f"{'LST(base300)':<16}" + "".join(f"{lst[k]:>9.4f}" for k, _ in KEYS))
    print(f"{'FLOOR':<16}" + "".join(f"{FLOOR.get(k, float('nan')):>9.4f}" for k, _ in KEYS))


if __name__ == "__main__":
    main()
