#!/usr/bin/env python3
"""cs1_table.py -- SCANNER 1 scoreboard: markdown table over all cs1_*.json runs.

Floors are the maintainer's five HARD displaced floors; the target row is LST base300.
Also carries the ctl_noatt reference row so ladder deltas can be read directly.
"""
import json
import os
import sys

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose"

TAGS = ["bk", "bkt5", "bkt5b30", "bkt5g3", "bkt5g3f", "bkg3", "t5g3",
        "full_bt4", "full_bt3", "fsdd",
        # decomposition isolates (not part of the 10 requested configs)
        "t5only", "bkt5f", "bkt5dd", "bkt5fs"]

# (json key, column label, fmt)
COLS = [
    ("eff_overall_incut", "eff", "%.4f"),
    ("eff_vxy_0_1", "vxy01", "%.4f"),
    ("eff_vxy_1_5", "v15", "%.4f"),
    ("eff_vxy_5_10", "v510", "%.4f"),
    ("eff_vxy_10_30", "v1030", "%.4f"),
    ("eff_dxy_0_1", "dxy01", "%.4f"),
    ("eff_dxy_1_5", "d15", "%.4f"),
    ("eff_dxy_5_10", "d510", "%.4f"),
    ("eff_dxy_10_30", "d1030", "%.4f"),
    ("fake_overall_incut", "fake", "%.4f"),
    ("dup_overall_incut", "dup", "%.4f"),
    ("fake_transition", "fakeTr", "%.4f"),
    ("dup_transition", "dupTr", "%.4f"),
    ("mean_nhitOT", "nhAll", "%.3f"),
    ("mean_nhitOT_barrel", "nhB", "%.3f"),
    ("mean_nhitOT_transition", "nhT", "%.3f"),
    ("mean_nhitOT_endcap", "nhE", "%.3f"),
    ("eff_barrel", "effB", "%.4f"),
    ("eff_transition", "effT", "%.4f"),
    ("eff_endcap", "effE", "%.4f"),
    ("n_tc", "nTC", "%.0f"),
]

FLOORS = [("eff_vxy_1_5", 0.7832, "v15"), ("eff_vxy_5_10", 0.7109, "v510"),
          ("eff_vxy_10_30", 0.6941, "v1030"), ("eff_dxy_1_5", 0.5398, "d15"),
          ("eff_dxy_5_10", 0.2471, "d510")]

LST = {"eff_overall_incut": .8136, "eff_vxy_0_1": .8473, "eff_vxy_1_5": .7731,
       "eff_vxy_5_10": .6530, "eff_vxy_10_30": .6291, "eff_dxy_1_5": .4989,
       "eff_dxy_5_10": .2281, "fake_overall_incut": .0455, "dup_overall_incut": .0513,
       "mean_nhitOT": 6.514, "mean_nhitOT_barrel": 10.151, "mean_nhitOT_transition": 10.010,
       "mean_nhitOT_endcap": 3.559}
CTL = {"eff_overall_incut": .8017, "eff_vxy_0_1": .8344, "eff_vxy_1_5": .7852,
       "eff_vxy_5_10": .7129, "eff_vxy_10_30": .6961, "eff_dxy_1_5": .5418,
       "eff_dxy_5_10": .2491, "fake_overall_incut": .0536, "dup_overall_incut": .0643,
       "mean_nhitOT": 6.521, "mean_nhitOT_barrel": 10.044, "mean_nhitOT_transition": 9.986,
       "mean_nhitOT_endcap": 3.679}


def load(tag):
    p = os.path.join(P, "cs1_%s.json" % tag)
    if not os.path.exists(p):
        return None
    m = json.load(open(p))["metrics"]
    return {k: v["proto"] for k, v in m.items()}


def main():
    rows = []
    for t in TAGS:
        d = load(t)
        if d is None:
            print("MISSING: %s" % t, file=sys.stderr)
            continue
        rows.append((t, d))

    hdr = ["config"] + [c[1] for c in COLS] + ["floors"]
    print("| " + " | ".join(hdr) + " |")
    print("|" + "|".join(["---"] * len(hdr)) + "|")

    def refrow(name, ref):
        cells = [name]
        for k, _lab, f in COLS:
            cells.append(f % ref[k] if k in ref else "-")
        cells.append("-")
        print("| " + " | ".join(cells) + " |")

    refrow("**LST target**", LST)
    refrow("**ctl_noatt**", CTL)

    for t, d in rows:
        cells = [t]
        for k, _lab, f in COLS:
            v = d.get(k)
            cells.append(f % v if v is not None else "n/a")
        fails = ["%s %.4f<%.4f" % (lab, d[k], thr) for k, thr, lab in FLOORS
                 if d.get(k) is not None and d[k] < thr - 1e-9]
        cells.append("PASS" if not fails else "FAIL: " + "; ".join(fails))
        print("| " + " | ".join(cells) + " |")


if __name__ == "__main__":
    main()
