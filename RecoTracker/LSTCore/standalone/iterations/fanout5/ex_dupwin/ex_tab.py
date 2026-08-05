#!/usr/bin/env python3
"""M19 exploit scoreboard: hard floors + per-band dup + length + d510 count + timing."""
import glob
import json
import os
import sys

import ROOT

ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError

D = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_dupwin/"
BASE = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/base300_hists.root"
FLOOR = dict(eff_overall_incut=.8127, eff_vxy_1_5=.8022, eff_vxy_5_10=.7267,
             eff_vxy_10_30=.7170, eff_dxy_1_5=.5613, fake_overall_incut=.0480)


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


def hists(path):
    f = ROOT.TFile.Open(path)
    if not f or f.IsZombie():
        return None
    o = {}
    n = f.Get("Root__TC_base_0_0_ef_numer_dxy")
    de = f.Get("Root__TC_base_0_0_ef_denom_dxy")
    o["d510n"], o["d510d"] = band(n, 5, 10), band(de, 5, 10)
    dn = f.Get("Root__TC_dr_numer_eta")
    dd = f.Get("Root__TC_dr_denom_eta")
    for lbl, lo, hi in (("w", 1.5, 3.0), ("w25", 1.5, 2.5), ("lo", 0.0, 1.5)):
        o["dup_" + lbl] = band(dn, lo, hi) / max(1e-9, band(dd, lo, hi))
        o["n_" + lbl] = band(dd, lo, hi)
    ol = f.Get("Root__TC_ol_numer_eta")
    od = f.Get("Root__TC_ol_denom_eta")
    o["ot_w"] = band(ol, 1.5, 3.0) / max(1e-9, band(od, 1.5, 3.0))
    f.Close()
    return o


def timing(tag):
    p = D + "x_" + tag + ".log"
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


tags = sys.argv[1:]
if not tags:
    tags = sorted(os.path.basename(p)[2:-5] for p in glob.glob(D + "x_*.json"))

hdr = ("%-12s %6s %6s %6s %6s %6s %6s %7s | %6s %6s %6s %6s %6s %6s %6s | %6s %6s %6s %6s %5s %8s %5s %s" %
       ("tag", "eff", "vxy01", "v15", "v510", "v1030", "d15", "d510",
        "dup", "dupB", "dupT", "dupE", "dup1.5-3", "dp1.5-2.5", "dup<1.5",
        "fake", "nhB", "nhT", "nhE", "otW", "nTC", "ms", "verdict"))
print(hdr)
print("-" * len(hdr))
for t in tags:
    p = D + "x_" + t + ".json"
    hp = D + "x_" + t + "_hists.root"
    if not os.path.exists(p) or not os.path.exists(hp):
        continue
    m = json.load(open(p))
    m = {k: v["proto"] for k, v in m["metrics"].items()}
    H = hists(hp)
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
    if H["d510n"] < 71:
        fl.append("D510")
    if g(m, "fake_overall_incut") > FLOOR["fake_overall_incut"]:
        fl.append("FAKE")
    print("%-12s %6.4f %6.4f %6.4f %6.4f %6.4f %6.4f %3.0f/%-3.0f| %6.4f %6.4f %6.4f %6.4f %8.4f %9.4f %7.4f | %6.4f %6.3f %6.3f %6.3f %5.3f %8.0f %5.0f %s"
          % (t, g(m, "eff_overall_incut"), g(m, "eff_vxy_0_1"), g(m, "eff_vxy_1_5"),
             g(m, "eff_vxy_5_10"), g(m, "eff_vxy_10_30"), g(m, "eff_dxy_1_5"),
             H["d510n"], H["d510d"],
             g(m, "dup_overall_incut"), g(m, "dup_barrel"), g(m, "dup_transition"),
             g(m, "dup_endcap"), H["dup_w"], H["dup_w25"], H["dup_lo"],
             g(m, "fake_overall_incut"),
             g(m, "mean_nhitOT_barrel"), g(m, "mean_nhitOT_transition"),
             g(m, "mean_nhitOT_endcap"), H["ot_w"], g(m, "n_tc"), timing(t),
             ("FAIL:" + ",".join(fl)) if fl else "pass"))
