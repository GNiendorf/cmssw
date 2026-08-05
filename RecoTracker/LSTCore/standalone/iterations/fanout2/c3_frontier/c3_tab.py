#!/usr/bin/env python3
"""c3_frontier result table + constraint judge."""
import json, glob, os, sys

DIR = os.path.dirname(os.path.abspath(__file__))
PROTO = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype"

EFFB = ["eff_overall_incut", "eff_vxy_0_1", "eff_vxy_1_5", "eff_vxy_5_10", "eff_vxy_10_30",
        "eff_dxy_0_1", "eff_dxy_1_5", "eff_dxy_5_10", "eff_barrel", "eff_transition", "eff_endcap"]
LEN = ["mean_nhitOT_barrel", "mean_nhitOT_transition", "mean_nhitOT_endcap"]
VXY = ["eff_vxy_0_1", "eff_vxy_1_5", "eff_vxy_5_10", "eff_vxy_10_30"]

# j1 anchor dxy values (floor = j1 - 0.003)
J1 = json.load(open(os.path.join(PROTO, "ab_m14_j1.json")))["metrics"]
DXYFLOOR = {k: J1[k]["proto"] - 0.003 for k in
            ["eff_dxy_0_1", "eff_dxy_1_5", "eff_dxy_5_10", "eff_dxy_10_30"]}
FAKECAP = 0.0575


def load(path):
    return json.load(open(path))["metrics"]


def judge(m):
    fails = []
    for k in EFFB:
        if m[k]["proto"] < m[k]["base"]:
            fails.append("%s(%.4f<%.4f)" % (k.replace("eff_", "e"), m[k]["proto"], m[k]["base"]))
    for k in LEN:
        if m[k]["delta"] < 0:
            fails.append("%s(%+.3f)" % (k.replace("mean_nhitOT_", "len_"), m[k]["delta"]))
    for k, f in DXYFLOOR.items():
        if m[k]["proto"] < f - 1e-9:
            fails.append("j1%s(%.4f<%.4f)" % (k.replace("eff_dxy", "d"), m[k]["proto"], f))
    if m["fake_overall_incut"]["proto"] > FAKECAP + 1e-9:
        fails.append("fake(%.4f)" % m["fake_overall_incut"]["proto"])
    return fails


def vsum(m):
    return sum(m[k]["delta"] for k in VXY)


rows = []
files = sorted(glob.glob(os.path.join(DIR, "ab_c3_*.json")))
extra = [os.path.join(PROTO, "ab_m14_j1.json"), os.path.join(PROTO, "ab_m14_j4.json"),
         os.path.join(PROTO, "ab_m12_w7.json")]
for f in files + extra:
    tag = os.path.basename(f)[3:-5]
    try:
        m = load(f)
    except Exception as e:
        print("skip %s: %s" % (tag, e)); continue
    rows.append((tag, m))

hdr = ("%-14s %6s %6s %6s %6s %6s | %6s %6s %6s %6s | %6s %6s | %6s %6s %6s | %7s  %s" %
       ("tag", "eff", "v01", "v15", "v510", "v1030", "d01", "d15", "d510", "d1030",
        "fake", "dup", "lb", "lt", "le", "vxySum", "FAILS"))
print(hdr)
print("-" * len(hdr))
rows.sort(key=lambda r: -vsum(r[1]))
for tag, m in rows:
    fails = judge(m)
    print("%-14s %6.4f %6.4f %6.4f %6.4f %6.4f | %6.4f %6.4f %6.4f %6.4f | %6.4f %6.4f | %+6.3f %+6.3f %+6.3f | %+7.4f  %s" % (
        tag, m["eff_overall_incut"]["proto"], m["eff_vxy_0_1"]["proto"], m["eff_vxy_1_5"]["proto"],
        m["eff_vxy_5_10"]["proto"], m["eff_vxy_10_30"]["proto"],
        m["eff_dxy_0_1"]["proto"], m["eff_dxy_1_5"]["proto"], m["eff_dxy_5_10"]["proto"], m["eff_dxy_10_30"]["proto"],
        m["fake_overall_incut"]["proto"], m["dup_overall_incut"]["proto"],
        m["mean_nhitOT_barrel"]["delta"], m["mean_nhitOT_transition"]["delta"], m["mean_nhitOT_endcap"]["delta"],
        vsum(m), "PASS" if not fails else " ".join(fails)))
m = rows[0][1]
print("-" * len(hdr))
print("%-14s %6.4f %6.4f %6.4f %6.4f %6.4f | %6.4f %6.4f %6.4f %6.4f | %6.4f %6.4f | %6.3f %6.3f %6.3f |" % (
    "BASELINE", m["eff_overall_incut"]["base"], m["eff_vxy_0_1"]["base"], m["eff_vxy_1_5"]["base"],
    m["eff_vxy_5_10"]["base"], m["eff_vxy_10_30"]["base"], m["eff_dxy_0_1"]["base"], m["eff_dxy_1_5"]["base"],
    m["eff_dxy_5_10"]["base"], m["eff_dxy_10_30"]["base"], m["fake_overall_incut"]["base"],
    m["dup_overall_incut"]["base"], m["mean_nhitOT_barrel"]["base"], m["mean_nhitOT_transition"]["base"],
    m["mean_nhitOT_endcap"]["base"]))
print("dxy floors (j1-0.003): " + " ".join("%s>=%.4f" % (k[-5:], v) for k, v in DXYFLOOR.items()))
