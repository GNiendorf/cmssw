#!/usr/bin/env python3
"""ANGLE-1 scorer: compact A/B table vs the LST-BASELINE floors (plan 10.5).
Pass rule: every eff band >= base - 0.005 AND every length >= base per region;
among passers minimize fake, then dup. dxy[10,30) is flagged UNREACHABLE
acceptance-side (M9: ceiling 0.0256 at the thresholdless flood)."""
import json, sys, os
EFF = ["eff_overall_incut", "eff_vxy_0_1", "eff_vxy_1_5", "eff_vxy_5_10", "eff_vxy_10_30",
       "eff_dxy_0_1", "eff_dxy_1_5", "eff_dxy_5_10", "eff_dxy_10_30",
       "eff_barrel", "eff_transition", "eff_endcap"]
LEN = ["mean_nhitOT", "mean_nhitOT_barrel", "mean_nhitOT_transition", "mean_nhitOT_endcap"]
SHORT = {"eff_overall_incut": "eff", "eff_vxy_0_1": "vxy01", "eff_vxy_1_5": "vxy15",
         "eff_vxy_5_10": "vxy510", "eff_vxy_10_30": "vxy1030", "eff_dxy_0_1": "dxy01",
         "eff_dxy_1_5": "dxy15", "eff_dxy_5_10": "dxy510", "eff_dxy_10_30": "dxy1030",
         "eff_barrel": "eBar", "eff_transition": "eTra", "eff_endcap": "eEnd",
         "mean_nhitOT": "len", "mean_nhitOT_barrel": "lenB",
         "mean_nhitOT_transition": "lenT", "mean_nhitOT_endcap": "lenE"}
TOL = 0.005
rows = []
for p in sys.argv[1:]:
    m = json.load(open(p))["metrics"]
    tag = os.path.basename(p).replace("ab_", "").replace(".json", "")
    fails = []
    for k in EFF:
        if m[k]["proto"] < m[k]["base"] - TOL:
            fails.append(SHORT[k])
    for k in LEN:
        if m[k]["proto"] < m[k]["base"]:
            fails.append(SHORT[k])
    hard = [f for f in fails if f != "dxy1030"]
    rows.append((tag, m, fails, hard))
hdr = ["cfg", "eff", "vxy01", "vxy15", "vxy510", "vxy1030", "dxy01", "dxy15", "dxy510",
       "dxy1030", "eBar", "eTra", "eEnd", "fake", "dup", "len", "TC/evt", "FAIL(excl dxy1030)"]
print(" | ".join(f"{h:>8}" for h in hdr))
b = rows[0][1] if rows else None
if b:
    print(" | ".join([f"{'BASE':>8}"] + [f"{b[k]['base']:>8.4f}" for k in EFF] +
                     [f"{b['fake_overall_incut']['base']:>8.4f}",
                      f"{b['dup_overall_incut']['base']:>8.4f}",
                      f"{b['mean_nhitOT']['base']:>8.3f}",
                      f"{b['n_tc']['base']/300:>8.0f}", f"{'-':>8}"]))
for tag, m, fails, hard in rows:
    print(" | ".join([f"{tag:>8}"] + [f"{m[k]['proto']:>8.4f}" for k in EFF] +
                     [f"{m['fake_overall_incut']['proto']:>8.4f}",
                      f"{m['dup_overall_incut']['proto']:>8.4f}",
                      f"{m['mean_nhitOT']['proto']:>8.3f}",
                      f"{m['n_tc']['proto']/300:>8.0f}",
                      ("PASS" if not hard else ",".join(hard))]))
