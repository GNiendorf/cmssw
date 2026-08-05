#!/usr/bin/env python3
"""EX-EXTEND scoreboard: M19 floors + length + per-band dup + d510 count + timing."""
import glob
import json
import os
import sys

import ROOT

D = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_extend/"
FLOOR = dict(eff_overall_incut=.8127, eff_vxy_1_5=.8022, eff_vxy_5_10=.7267,
             eff_vxy_10_30=.7170, eff_dxy_1_5=.5613, fake_overall_incut=.0480)
LST = dict(mean_nhitOT_barrel=10.151, mean_nhitOT_transition=10.010,
           mean_nhitOT_endcap=3.559, dup_overall_incut=.0513, eff_overall_incut=.8136)


def g(m, k):
    v = m.get(k)
    return v if v is not None else float("nan")


def band(h, lo, hi):
    t = 0.0
    ax = h.GetXaxis()
    for b in range(1, h.GetNbinsX() + 1):
        c = abs(ax.GetBinCenter(b))
        if lo <= c < hi:
            t += h.GetBinContent(b)
    return t


def hists(tag):
    """d510 numer/denom, and dup/meanOT in the maintainer |eta| 1.5-3.0 window."""
    p = D + tag + "_hists.root"
    out = dict(d510n=float("nan"), d510d=float("nan"), dupw=float("nan"),
               ntcw=float("nan"), meanOTw=float("nan"))
    if not os.path.exists(p):
        return out
    f = ROOT.TFile.Open(p)
    if not f or f.IsZombie():
        return out
    n = f.Get("Root__TC_base_0_0_ef_numer_dxy")
    de = f.Get("Root__TC_base_0_0_ef_denom_dxy")
    out["d510n"] = band(n, 5, 10)
    out["d510d"] = band(de, 5, 10)
    dn = f.Get("Root__TC_dr_numer_eta")
    dd = f.Get("Root__TC_dr_denom_eta")
    out["dupw"] = band(dn, 1.5, 3.0) / max(1e-9, band(dd, 1.5, 3.0))
    out["ntcw"] = band(dd, 1.5, 3.0)
    on = f.Get("Root__TC_ol_numer_eta")
    od = f.Get("Root__TC_ol_denom_eta")
    out["meanOTw"] = band(on, 1.5, 3.0) / max(1e-9, band(od, 1.5, 3.0))
    f.Close()
    return out


def timing(tag):
    p = D + tag + ".log"
    if not os.path.exists(p):
        return float("nan")
    for line in open(p, errors="ignore"):
        if line.strip().startswith("time mean/evt"):
            tot = 0.0
            for tok in line.split():
                if "=" in tok:
                    try:
                        tot += float(tok.split("=")[1])
                    except ValueError:
                        pass
            return tot
    return float("nan")


def chaintc(tag):
    p = D + tag + ".log"
    if not os.path.exists(p):
        return float("nan")
    s = 0
    for line in open(p, errors="ignore"):
        if line.startswith("evt "):
            for tok in line.split():
                if tok.startswith("chainTC="):
                    s += int(tok.split("=")[1])
    return s


import re

EXRE = re.compile(r"EX chain extend chains=(\d+).*?extended chains=(\d+).*?"
                  r"outer=(\d+) inner=(\d+) \| chi2-rejected=(\d+)")


def exstat(tag):
    """(examined, extended, outer, inner, chi2rej) from the -EX report line."""
    p = D + tag + ".log"
    if not os.path.exists(p):
        return (0, 0, 0, 0, 0)
    for line in open(p, errors="ignore"):
        m = EXRE.search(line)
        if m:
            return tuple(int(x) for x in m.groups())
    return (0, 0, 0, 0, 0)


tags = sys.argv[1:]
if not tags:
    tags = sorted(os.path.basename(p)[:-5] for p in glob.glob(D + "ex_*.json"))

hdr = ("%-14s %7s %7s %7s %7s %7s %7s | %6s %6s %6s | %6s %6s %6s %6s | %7s %7s %6s %5s %6s"
       % ("tag", "eff", "v15", "v510", "v1030", "d15", "d510", "nhB", "nhT", "nhE",
          "fake", "dup", "dupB", "dupE", "dupWin", "otWin", "chTC", "ms", "ext%"))
print(hdr)
print("-" * len(hdr))
for t in tags:
    p = D + t + ".json"
    if not os.path.exists(p):
        continue
    m = json.load(open(p))
    m = {k: v["proto"] for k, v in m["metrics"].items()}
    h = hists(t)
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
    if h["d510n"] < 71:
        fl.append("D510")
    if g(m, "fake_overall_incut") > FLOOR["fake_overall_incut"]:
        fl.append("FAKE")
    ex = exstat(t)
    exp = (100.0 * ex[1] / ex[0]) if ex[0] else 0.0
    print("%-14s %7.4f %7.4f %7.4f %7.4f %7.4f %3.0f/%-3.0f| %6.3f %6.3f %6.3f | %6.4f %6.4f %6.4f %6.4f | %7.4f %7.3f %6.0f %5.0f %5.1f%% %s"
          % (t, g(m, "eff_overall_incut"), g(m, "eff_vxy_1_5"), g(m, "eff_vxy_5_10"),
             g(m, "eff_vxy_10_30"), g(m, "eff_dxy_1_5"), h["d510n"], h["d510d"],
             g(m, "mean_nhitOT_barrel"), g(m, "mean_nhitOT_transition"),
             g(m, "mean_nhitOT_endcap"), g(m, "fake_overall_incut"),
             g(m, "dup_overall_incut"), g(m, "dup_barrel"), g(m, "dup_endcap"),
             h["dupw"], h["meanOTw"], chaintc(t), timing(t), exp,
             ("FAIL:" + ",".join(fl)) if fl else "pass"))
print("-" * len(hdr))
print("%-14s %7.4f %7s %7s %7s %7s %3s/%-3s| %6.3f %6.3f %6.3f | %6s %6.4f"
      % ("LST TARGET", LST["eff_overall_incut"], "-", "-", "-", "-", "-", "-",
         LST["mean_nhitOT_barrel"], LST["mean_nhitOT_transition"],
         LST["mean_nhitOT_endcap"], "-", LST["dup_overall_incut"]))
