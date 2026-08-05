#!/usr/bin/env python3
"""Compact results table for the C1 cell scan: values, baseline-floor verdict, anchor delta."""
import json
import os
import sys

D = os.path.dirname(os.path.abspath(__file__))

ANCH = {  # m14_j1 anchor (plan 10.4c)
    "eff_overall_incut": 0.8172,
    "eff_vxy_0_1": 0.8505, "eff_vxy_1_5": 0.7958, "eff_vxy_5_10": 0.7177, "eff_vxy_10_30": 0.6708,
    "eff_dxy_0_1": 0.8426, "eff_dxy_1_5": 0.5247, "eff_dxy_5_10": 0.2351, "eff_dxy_10_30": 0.0235,
    "fake_overall_incut": 0.0571, "dup_overall_incut": 0.3143,
}
EFF = ["eff_overall_incut", "eff_vxy_0_1", "eff_vxy_1_5", "eff_vxy_5_10", "eff_vxy_10_30",
       "eff_dxy_0_1", "eff_dxy_1_5", "eff_dxy_5_10", "eff_dxy_10_30",
       "eff_barrel", "eff_transition", "eff_endcap"]
LEN = ["mean_nhitOT_barrel", "mean_nhitOT_transition", "mean_nhitOT_endcap"]
OTHER = ["fake_overall_incut", "fake_barrel", "fake_transition", "fake_endcap", "dup_overall_incut"]
UNREACH = {"eff_dxy_10_30"}  # M13: demoted, formation-side, unreachable acceptance-side

tags = sys.argv[1:]
data = []
for t in tags:
    with open(os.path.join(D, "ab_%s.json" % t)) as f:
        data.append((t, json.load(f)["metrics"]))

base = {k: v["base"] for k, v in data[0][1].items()}

w = 11
print("%-24s %8s %s" % ("metric", "base", "".join("%*s" % (w, t) for t, _ in tags and [(t, 0) for t, _ in data])))
print("-" * (24 + 9 + w * len(data)))
for k in EFF + LEN + OTHER:
    print("%-24s %8.4f %s" % (k, base[k], "".join("%*.4f" % (w, m[k]["proto"]) for _, m in data)))
print()
print("%-16s %7s %7s  %-6s %s" % ("tag", "fake", "dup", "floors", "failing bands (proto-base) | anchor eff deltas"))
for t, m in data:
    fails = []
    for k in EFF:
        if k in UNREACH:
            continue
        if m[k]["proto"] < m[k]["base"] - 0.005:
            fails.append("%s %+.4f" % (k, m[k]["proto"] - m[k]["base"]))
    for k in LEN:
        if m[k]["proto"] < m[k]["base"]:
            fails.append("%s %+.3f" % (k, m[k]["proto"] - m[k]["base"]))
    anch = ["%s %+.4f" % (k, m[k]["proto"] - ANCH[k]) for k in EFF
            if k in ANCH and k not in UNREACH and abs(m[k]["proto"] - ANCH[k]) >= 0.001]
    print("%-16s %7.4f %7.4f  %-6s %s | %s" % (
        t, m["fake_overall_incut"]["proto"], m["dup_overall_incut"]["proto"],
        "PASS" if not fails else "FAIL", "; ".join(fails) or "-", "; ".join(anch) or "anchor-equal"))
