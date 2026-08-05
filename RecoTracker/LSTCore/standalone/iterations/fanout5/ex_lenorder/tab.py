#!/usr/bin/env python3
"""RECON B ablation table: floors + length columns for every run in this dir."""
import glob
import json
import os
import sys

D = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_lenorder/"
FLOOR = dict(eff_overall_incut=.8127, eff_vxy_1_5=.8022, eff_vxy_5_10=.7267,
             eff_vxy_10_30=.7170, eff_dxy_1_5=.5613, fake_overall_incut=.0480)
LST = dict(mean_nhitOT_barrel=10.151, mean_nhitOT_transition=10.010,
           mean_nhitOT_endcap=3.559, dup_overall_incut=.0513, eff_overall_incut=.8136)


def g(m, k):
    v = m.get(k)
    return v if v is not None else float("nan")


def d510(tag):
    import ROOT
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
    return r


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


tags = sys.argv[1:]
if not tags:
    tags = sorted(os.path.basename(p)[:-5] for p in glob.glob(D + "*.json"))

hdr = ("%-16s %6s %6s %6s %6s %6s %6s | %7s %7s %7s | %6s %6s %8s %5s" %
       ("tag", "eff", "v15", "v510", "v1030", "d15", "d510", "nhB", "nhT", "nhE",
        "fake", "dup", "nTC", "ms"))
print(hdr)
print("-" * len(hdr))
for t in tags:
    p = D + t + ".json"
    if not os.path.exists(p):
        continue
    m = json.load(open(p))
    m = {k: v["proto"] for k, v in m["metrics"].items()}
    dn, dd = d510(t)
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
    print("%-16s %6.4f %6.4f %6.4f %6.4f %6.4f %3.0f/%-3.0f| %7.3f %7.3f %7.3f | %6.4f %6.4f %8.0f %5.0f %s"
          % (t, g(m, "eff_overall_incut"), g(m, "eff_vxy_1_5"), g(m, "eff_vxy_5_10"),
             g(m, "eff_vxy_10_30"), g(m, "eff_dxy_1_5"), dn, dd,
             g(m, "mean_nhitOT_barrel"), g(m, "mean_nhitOT_transition"),
             g(m, "mean_nhitOT_endcap"), g(m, "fake_overall_incut"),
             g(m, "dup_overall_incut"), g(m, "n_tc"), timing(t),
             ("FAIL:" + ",".join(fl)) if fl else "pass"))
print("-" * len(hdr))
print("%-16s %6.4f %6s %6s %6s %6s %3s/%-3s| %7.3f %7.3f %7.3f | %6s %6.4f"
      % ("LST TARGET", LST["eff_overall_incut"], "-", "-", "-", "-", "-", "-",
         LST["mean_nhitOT_barrel"], LST["mean_nhitOT_transition"],
         LST["mean_nhitOT_endcap"], "-", LST["dup_overall_incut"]))
