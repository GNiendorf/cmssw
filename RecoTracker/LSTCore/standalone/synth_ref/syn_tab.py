#!/usr/bin/env python3
"""Synthesis scoreboard. Falls back synth_ref -> fin_ref -> a*_ref -> rebase_ref/xc_ref
so any explorer's tag can be quoted in the same table.
  syn_tab.py [-r] [-b] TAG [TAG...]     -r = per region, -b = displaced bands
The 'base' side of every compare_ab json IS LST, so LST is printed from any run's json."""
import json, os, sys

S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
DIRS = ["synth_ref", "fin_ref", "xc_ref", "rebase_ref"] + ["a%02d_ref" % i for i in range(1, 16)]

def load(tag):
    for d in DIRS:
        p = os.path.join(S, d, "r_%s.json" % tag)
        if os.path.exists(p):
            with open(p) as f:
                return json.load(f)["metrics"]
    return None

HEAD = [("eff", "eff_overall_incut"), ("dup", "dup_overall_incut"), ("fake", "fake_overall_incut"),
        ("nhB", "mean_nhitOT_barrel"), ("nhT", "mean_nhitOT_transition"),
        ("nhE", "mean_nhitOT_endcap"), ("nh", "mean_nhitOT"), ("nTC", "n_tc")]
REG = [("effB", "eff_barrel"), ("effT", "eff_transition"), ("effE", "eff_endcap"),
       ("dupB", "dup_barrel"), ("dupT", "dup_transition"), ("dupE", "dup_endcap"),
       ("fakB", "fake_barrel"), ("fakT", "fake_transition"), ("fakE", "fake_endcap")]
BAND = [("v01", "eff_vxy_0_1"), ("v15", "eff_vxy_1_5"), ("v510", "eff_vxy_5_10"),
        ("v1030", "eff_vxy_10_30"), ("d01", "eff_dxy_0_1"), ("d15", "eff_dxy_1_5"),
        ("d510", "eff_dxy_5_10"), ("d1030", "eff_dxy_10_30")]

def fmt(k, v):
    if k == "n_tc":
        return "%8d" % int(round(v))
    if k.startswith("nh"):
        return "%8.4f" % v
    return "%8.5f" % v

def main():
    args = sys.argv[1:]
    cols = HEAD
    if "-r" in args:
        args.remove("-r"); cols = REG
    if "-b" in args:
        args.remove("-b"); cols = BAND
    lst = None
    rows = []
    for t in args:
        m = load(t)
        if m is None:
            print("MISSING %s" % t); continue
        if lst is None:
            lst = m
        rows.append((t, m))
    if not rows:
        return
    print("%-22s" % "tag" + "".join("%8s" % n for n, _ in cols))
    for t, m in rows:
        print("%-22s" % t + "".join(fmt(k, m[k]["proto"]) for _, k in cols))
    print("%-22s" % "LST (target)" + "".join(fmt(k, lst[k]["base"]) for _, k in cols))
    print("-- deltas vs LST --")
    for t, m in rows:
        print("%-22s" % t + "".join(
            ("%+8d" % int(round(m[k]["proto"] - lst[k]["base"])) if k == "n_tc"
             else "%+8.5f" % (m[k]["proto"] - lst[k]["base"])) for _, k in cols))

main()
