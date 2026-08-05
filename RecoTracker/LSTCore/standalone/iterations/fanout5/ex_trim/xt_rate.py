#!/usr/bin/env python3
"""Exchange rate: milli-hits of nhitOT recovered per milli-eff spent, vs a reference tag.

Usage: xt_rate.py <ref_tag> <tag> [tag...]
Reports d(nhB), d(nhT), d(nhE), d(eff), and the rate in milli-hits per milli-eff,
plus the fraction of the LST gap closed in each region.
"""
import json
import os
import sys

D = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_trim/"
LST = dict(nhB=10.151, nhT=10.010, nhE=3.559)
FLOOR_EFF = .8127


def load(t):
    p = D + t + ".json"
    if not os.path.exists(p):
        return None
    m = json.load(open(p))
    return {k: v["proto"] for k, v in m["metrics"].items()}


ref = load(sys.argv[1])
if ref is None:
    sys.exit("no ref " + sys.argv[1])
rb, rt, re_, rf = (ref["mean_nhitOT_barrel"], ref["mean_nhitOT_transition"],
                   ref["mean_nhitOT_endcap"], ref["eff_overall_incut"])
gapB, gapT, gapE = LST["nhB"] - rb, LST["nhT"] - rt, LST["nhE"] - re_

print("ref=%s  nhB=%.3f nhT=%.3f nhE=%.3f eff=%.5f" % (sys.argv[1], rb, rt, re_, rf))
print("LST gap from ref: B=%.3f T=%.3f E=%.3f" % (gapB, gapT, gapE))
print()
hdr = "%-18s %8s %8s %8s | %9s | %9s | %s" % (
    "tag", "dnhB", "dnhT", "dnhE", "d_eff(m)", "hits/meff", "gap closed B/T/E")
print(hdr)
print("-" * len(hdr))
for t in sys.argv[2:]:
    m = load(t)
    if m is None:
        continue
    db = m["mean_nhitOT_barrel"] - rb
    dt = m["mean_nhitOT_transition"] - rt
    de = m["mean_nhitOT_endcap"] - re_
    deff = (m["eff_overall_incut"] - rf) * 1000.0  # milli-eff
    tot = db + dt + de
    rate = (tot * 1000.0 / -deff) if deff < -1e-9 else float("inf")
    print("%-18s %+8.3f %+8.3f %+8.3f | %+9.3f | %9.1f | %4.1f%% %4.1f%% %4.1f%%  %s"
          % (t, db, dt, de, deff, rate,
             100 * db / gapB if gapB else 0, 100 * dt / gapT if gapT else 0,
             100 * de / gapE if gapE else 0,
             "" if m["eff_overall_incut"] >= FLOOR_EFF else "EFF-FLOOR-FAIL"))
