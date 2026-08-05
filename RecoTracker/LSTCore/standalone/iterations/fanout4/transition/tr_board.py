#!/usr/bin/env python3
"""Scoreboard over all ab_<tag>.json in this directory, with pass/fail against the bars."""
import glob, json, os, sys
P = os.path.dirname(os.path.abspath(__file__))
BARS = {  # metric: (bar, ">=" or "<=")
    "fake_transition": (0.055, "<="), "eff_transition": (0.8603, ">="),
    "eff_overall_incut": (0.8017, ">="), "eff_vxy_1_5": (0.7832, ">="),
    "eff_vxy_5_10": (0.7109, ">="), "eff_vxy_10_30": (0.6941, ">="),
    "eff_dxy_1_5": (0.5398, ">="), "eff_dxy_5_10": (0.2471, ">="),
    "fake_overall_incut": (0.0536, "<="), "dup_overall_incut": (0.0643, "<="),
}
COLS = ["eff_transition", "fake_transition", "dup_transition", "eff_overall_incut",
        "fake_overall_incut", "dup_overall_incut", "eff_vxy_1_5", "eff_vxy_5_10",
        "eff_vxy_10_30", "eff_dxy_1_5", "eff_dxy_5_10", "eff_barrel", "eff_endcap",
        "fake_barrel", "fake_endcap"]
SHORT = {"eff_transition": "effTr", "fake_transition": "fkTr", "dup_transition": "dupTr",
         "eff_overall_incut": "eff", "fake_overall_incut": "fake", "dup_overall_incut": "dup",
         "eff_vxy_1_5": "vxy1_5", "eff_vxy_5_10": "vxy5_10", "eff_vxy_10_30": "vxy10_30",
         "eff_dxy_1_5": "dxy1_5", "eff_dxy_5_10": "dxy5_10", "eff_barrel": "effBar",
         "eff_endcap": "effEnd", "fake_barrel": "fkBar", "fake_endcap": "fkEnd"}
tags = sys.argv[1:] if len(sys.argv) > 1 else sorted(
    os.path.basename(f)[3:-5] for f in glob.glob(P + "/ab_*.json"))
base = None
print("%-8s " % "tag" + " ".join("%8s" % SHORT[c] for c in COLS) + "  verdict")
for tg in ["base"] + [t for t in tags if t != "base"]:
    fn = "%s/ab_%s.json" % (P, tg)
    if not os.path.exists(fn):
        continue
    m = json.load(open(fn))["metrics"]
    v = {c: m[c]["proto"] for c in COLS if c in m}
    if base is None:
        base = v
    fails = []
    for k, (bar, op) in BARS.items():
        if k not in m:
            continue
        x = round(m[k]["proto"], 4)   # bars are quoted to 4 decimals
        if (op == ">=" and x < bar) or (op == "<=" and x > bar):
            fails.append("%s%s%.4f(%.4f)" % (SHORT.get(k, k), op, bar, x))
    print("%-8s " % tg + " ".join("%8.4f" % v.get(c, float("nan")) for c in COLS) +
          ("  PASS" if not fails else "  FAIL: " + " ".join(fails)))
