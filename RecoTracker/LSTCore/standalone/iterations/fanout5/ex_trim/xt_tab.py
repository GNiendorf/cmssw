#!/usr/bin/env python3
"""EXPLOIT trim scan table: M19 floors + length + d510 count + trim volume + timing."""
import glob
import json
import os
import re
import sys

D = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_trim/"
FLOOR = dict(eff_overall_incut=.8127, eff_vxy_1_5=.8022, eff_vxy_5_10=.7267,
             eff_vxy_10_30=.7170, eff_dxy_1_5=.5613, fake_overall_incut=.0480)
LST = dict(nhB=10.151, nhT=10.010, nhE=3.559, dup=.0513, eff=.8136)
FL = dict(eff=.8132, nhB=9.794, nhT=9.698, nhE=3.417, dup=.05708)


def g(m, k):
    v = m.get(k)
    return v if v is not None else float("nan")


_d510cache = {}


def d510(tag):
    if tag in _d510cache:
        return _d510cache[tag]
    import ROOT
    ROOT.gROOT.SetBatch(True)
    p = D + tag + "_hists.root"
    if not os.path.exists(p):
        return (float("nan"), float("nan"))
    f = ROOT.TFile.Open(p)
    n = f.Get("Root__TC_base_0_0_ef_numer_dxy")
    de = f.Get("Root__TC_base_0_0_ef_denom_dxy")

    def band(h, lo, hi):
        t = 0.0
        ax = h.GetXaxis()
        for b in range(1, h.GetNbinsX() + 1):
            c = abs(ax.GetBinCenter(b))
            if lo <= c < hi:
                t += h.GetBinContent(b)
        return t
    r = (band(n, 5, 10), band(de, 5, 10))
    f.Close()
    _d510cache[tag] = r
    return r


def d15cnt(tag):
    import ROOT
    ROOT.gROOT.SetBatch(True)
    p = D + tag + "_hists.root"
    if not os.path.exists(p):
        return (float("nan"), float("nan"))
    f = ROOT.TFile.Open(p)
    n = f.Get("Root__TC_base_0_0_ef_numer_dxy")
    de = f.Get("Root__TC_base_0_0_ef_denom_dxy")

    def band(h, lo, hi):
        t = 0.0
        ax = h.GetXaxis()
        for b in range(1, h.GetNbinsX() + 1):
            c = abs(ax.GetBinCenter(b))
            if lo <= c < hi:
                t += h.GetBinContent(b)
        return t
    r = (band(n, 1, 5), band(de, 1, 5))
    f.Close()
    return r


def loginfo(tag):
    """returns (ms/evt, nTrimTotal, nTrimInner, nTrimOuter, nExamined)"""
    p = D + tag + ".log"
    ms = float("nan")
    tr = (float("nan"),) * 4
    if not os.path.exists(p):
        return (ms,) + tr
    for line in open(p, errors="ignore"):
        s = line.strip()
        if s.startswith("time mean/evt"):
            tot = 0.0
            for tok in s.split():
                if "=" in tok:
                    try:
                        tot += float(tok.split("=")[1])
                    except ValueError:
                        pass
            ms = tot
        if "terminal trim examined=" in s:
            mm = re.search(r"examined=(\d+).*inner=(\d+) outer=(\d+) total=(\d+)", s)
            if mm:
                tr = (int(mm.group(4)), int(mm.group(2)), int(mm.group(3)), int(mm.group(1)))
    return (ms,) + tr


def row(t):
    p = D + t + ".json"
    if not os.path.exists(p):
        return None
    m = json.load(open(p))
    m = {k: v["proto"] for k, v in m["metrics"].items()}
    dn, dd = d510(t)
    ms, ntr, nin, nout, nex = loginfo(t)
    fl = []
    if g(m, "eff_overall_incut") < FLOOR["eff_overall_incut"]:
        fl.append("EFF")
    if g(m, "eff_vxy_1_5") < FLOOR["eff_vxy_1_5"]:
        fl.append("V15")
    if g(m, "eff_vxy_5_10") < FLOOR["eff_vxy_5_10"]:
        fl.append("V510")
    if g(m, "eff_vxy_10_30") < FLOOR["eff_vxy_10_30"]:
        fl.append("V1030")
    if g(m, "eff_dxy_1_5") < FLOOR["eff_dxy_1_5"]:
        fl.append("D15")
    if dn < 71:
        fl.append("D510")
    if g(m, "fake_overall_incut") > FLOOR["fake_overall_incut"]:
        fl.append("FAKE")
    return dict(tag=t, m=m, dn=dn, dd=dd, ms=ms, ntr=ntr, fl=fl)


HDR = ("%-18s %7s %6s %6s %6s %6s %6s | %6s %6s %6s | %6s %6s %6s | %6s %6s %6s | %6s %5s %s" %
       ("tag", "eff", "v15", "v510", "v1030", "d15", "d510", "nhB", "nhT", "nhE",
        "dupB", "dupT", "dupE", "fake", "dup", "nTC", "trims", "ms", "status"))


def emit(t):
    r = row(t)
    if r is None:
        return
    m = r["m"]
    print("%-18s %7.5f %6.4f %6.4f %6.4f %6.4f %3.0f/%-3.0f| %6.3f %6.3f %6.3f | %6.4f %6.4f %6.4f | %6.4f %6.5f %6.0f | %6.0f %5.0f %s"
          % (r["tag"], g(m, "eff_overall_incut"), g(m, "eff_vxy_1_5"), g(m, "eff_vxy_5_10"),
             g(m, "eff_vxy_10_30"), g(m, "eff_dxy_1_5"), r["dn"], r["dd"],
             g(m, "mean_nhitOT_barrel"), g(m, "mean_nhitOT_transition"),
             g(m, "mean_nhitOT_endcap"),
             g(m, "dup_barrel"), g(m, "dup_transition"), g(m, "dup_endcap"),
             g(m, "fake_overall_incut"), g(m, "dup_overall_incut"), g(m, "n_tc"),
             r["ntr"], r["ms"],
             ("FAIL:" + ",".join(r["fl"])) if r["fl"] else "pass"))


if __name__ == "__main__":
    tags = sys.argv[1:]
    if not tags:
        tags = sorted(os.path.basename(p)[:-5] for p in glob.glob(D + "*.json"))
    print(HDR)
    print("-" * len(HDR))
    for t in tags:
        emit(t)
    print("-" * len(HDR))
    print("%-18s %7.5f %6s %6s %6s %6s %3s/%-3s| %6.3f %6.3f %6.3f | %6s %6s %6s | %6s %6.5f"
          % ("FLAGSHIP(ref)", FL["eff"], "-", "-", "-", "-", "-", "-",
             FL["nhB"], FL["nhT"], FL["nhE"], "-", "-", "-", "-", FL["dup"]))
    print("%-18s %7.5f %6s %6s %6s %6s %3s/%-3s| %6.3f %6.3f %6.3f | %6s %6s %6s | %6s %6.5f"
          % ("LST TARGET", LST["eff"], "-", "-", "-", "-", "-", "-",
             LST["nhB"], LST["nhT"], LST["nhE"], "-", "-", "-", "-", LST["dup"]))
    print("%-18s %7.5f %6.4f %6.4f %6.4f %6.4f %3d/%-3s|" %
          ("HARD FLOOR", FLOOR["eff_overall_incut"], FLOOR["eff_vxy_1_5"],
           FLOOR["eff_vxy_5_10"], FLOOR["eff_vxy_10_30"], FLOOR["eff_dxy_1_5"], 71, "285"))
