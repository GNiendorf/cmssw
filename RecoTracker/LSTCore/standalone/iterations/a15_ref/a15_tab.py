#!/usr/bin/env python3
"""a15_tab.py -- scoreboard table for a15_ref runs (falls back to fin_ref/rebase_ref)."""
import json
import os
import sys

DIRS = ["/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a15_ref",
        "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fin_ref",
        "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/rebase_ref"]

ROWS = [("eff_overall_incut", "eff"), ("eff_vxy_0_1", "vxy01"), ("eff_vxy_1_5", "v15"),
        ("eff_vxy_5_10", "v510"), ("eff_vxy_10_30", "v1030"), ("eff_dxy_1_5", "d15"),
        ("eff_dxy_5_10", "d510"), ("eff_dxy_10_30", "d1030"),
        ("dup_overall_incut", "dup"), ("fake_overall_incut", "fake"),
        ("mean_nhitOT_barrel", "nhB"), ("mean_nhitOT_transition", "nhT"),
        ("mean_nhitOT_endcap", "nhE"), ("n_tc", "nTC")]
REG = [("eff_barrel", "effB"), ("eff_transition", "effT"), ("eff_endcap", "effE"),
       ("dup_barrel", "dupB"), ("dup_transition", "dupT"), ("dup_endcap", "dupE"),
       ("fake_barrel", "fakB"), ("fake_transition", "fakT"), ("fake_endcap", "fakE")]


def load(tag):
    for d in DIRS:
        p = os.path.join(d, "r_%s.json" % tag)
        if os.path.exists(p):
            with open(p) as f:
                return json.load(f)["metrics"]
    return None


def show(tags, rows, title):
    print(title)
    hdr = "%-20s" % "tag" + "".join("%9s" % lab for _, lab in rows)
    print(hdr)
    base = None
    for t in tags:
        m = load(t)
        if m is None:
            print("%-20s  (missing)" % t)
            continue
        if base is None:
            base = m
        vals = []
        for k, _ in rows:
            v = m.get(k, {}).get("proto")
            vals.append("%9.5f" % v if v is not None and abs(v) < 100 else
                        ("%9.0f" % v if v is not None else "%9s" % "n/a"))
        print("%-20s" % t + "".join(vals))
        lst = [m.get(k, {}).get("base") for k, _ in rows]
    if base is not None:
        vals = []
        for k, _ in rows:
            v = base.get(k, {}).get("base")
            vals.append("%9.5f" % v if v is not None and abs(v) < 100 else
                        ("%9.0f" % v if v is not None else "%9s" % "n/a"))
        print("%-20s" % "LST(base)" + "".join(vals))


if __name__ == "__main__":
    tags = sys.argv[1:]
    show(tags, ROWS, "A. HEADLINE")
    print()
    show(tags, REG, "B. PER REGION")
