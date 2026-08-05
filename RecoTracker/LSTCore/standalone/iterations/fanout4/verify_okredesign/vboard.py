#!/usr/bin/env python3
"""Adversarial board over ALL okredesign ok_*.json, including the metrics their
ok_board.py omits (dxy[10,30), eta-region eff, per-region mean nhitOT)."""
import glob
import json
import os

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/okredesign"
CTL = {"eff": .8017, "vxy01": .8344, "vxy15": .7852, "vxy510": .7129, "vxy1030": .6961,
       "dxy15": .5418, "dxy510": .2491, "fake": .0536, "dup": .0643}
FLOOR = {"vxy15": .7832, "vxy510": .7109, "vxy1030": .6941, "dxy15": .5398, "dxy510": .2471}
KEYS = [("eff", "eff_overall_incut"), ("vxy01", "eff_vxy_0_1"), ("vxy15", "eff_vxy_1_5"),
        ("vxy510", "eff_vxy_5_10"), ("vxy1030", "eff_vxy_10_30"), ("dxy15", "eff_dxy_1_5"),
        ("dxy510", "eff_dxy_5_10"), ("dxy1030", "eff_dxy_10_30"), ("fake", "fake_overall_incut"),
        ("dup", "dup_overall_incut"), ("nhB", "mean_nhitOT_barrel"),
        ("nhT", "mean_nhitOT_transition"), ("nhE", "mean_nhitOT_endcap")]

rows = []
lst = None
for f in sorted(glob.glob(os.path.join(P, "ok_*.json"))):
    tag = os.path.basename(f)[3:-5]
    m = json.load(open(f))["metrics"]
    v = {k: m[j]["proto"] for k, j in KEYS}
    lst = {k: m[j]["base"] for k, j in KEYS}
    v["tag"] = tag
    v["bad"] = [k for k in FLOOR if v[k] < FLOOR[k]]
    rows.append(v)

hdr = f"{'tag':<18}" + "".join(f"{k:>9}" for k, _ in KEYS) + "  floors"
print(hdr)
print("-" * len(hdr))
# rank by maintainer priority: eff first, then dup, then fake
rows.sort(key=lambda r: (-r["eff"], r["dup"], r["fake"]))
for r in rows:
    ok = "PASS" if not r["bad"] else "FAIL:" + ",".join(r["bad"])
    print(f"{r['tag']:<18}" + "".join(f"{r[k]:>9.4f}" for k, _ in KEYS) + f"  {ok}")
print("-" * len(hdr))
print(f"{'CTL(anchor)':<18}" + "".join(f"{CTL.get(k, float('nan')):>9.4f}" for k, _ in KEYS))
print(f"{'LST(base300)':<18}" + "".join(f"{lst[k]:>9.4f}" for k, _ in KEYS))
print(f"{'FLOOR':<18}" + "".join(f"{FLOOR.get(k, float('nan')):>9.4f}" for k, _ in KEYS))
