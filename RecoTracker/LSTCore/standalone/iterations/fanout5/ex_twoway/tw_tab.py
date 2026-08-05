#!/usr/bin/env python3
"""EXPLOIT (capped n-way ownership) scoreboard: M19 floors + primary targets + sizing.

Columns: the six hard floors, the d510 TRACK COUNT, per-band dup, nhitOT b/t/e, the
maintainer's |eta| 1.5-3.0 dup window, TC count, timing, and the PROMPT (vxy < 1 mm)
efficiency NUMERATOR in sims -- the sizing number the exploit is gated on.
"""
import glob
import json
import os
import sys

import ROOT

ROOT.gROOT.SetBatch(True)
D = os.path.dirname(os.path.abspath(__file__)) + "/"
FLOOR = dict(eff_overall_incut=.8127, eff_vxy_1_5=.8022, eff_vxy_5_10=.7267,
             eff_vxy_10_30=.7170, eff_dxy_1_5=.5613, fake_overall_incut=.0480,
             dup_overall_incut=.0571)
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
    """(d510 num, d510 den, prompt vxy<1 num, prompt den, dup window num, den)."""
    p = D + tag + "_hists.root"
    nan = float("nan")
    if not os.path.exists(p):
        return (nan,) * 6
    f = ROOT.TFile.Open(p)
    nd = f.Get("Root__TC_base_0_0_ef_numer_dxy")
    dd = f.Get("Root__TC_base_0_0_ef_denom_dxy")
    nv = f.Get("Root__TC_base_0_0_ef_numer_vxy")
    dv = f.Get("Root__TC_base_0_0_ef_denom_vxy")
    ndr = f.Get("Root__TC_dr_numer_eta")
    ddr = f.Get("Root__TC_dr_denom_eta")
    out = (band(nd, 5, 10), band(dd, 5, 10), band(nv, 0, 1), band(dv, 0, 1),
           band(ndr, 1.5, 3.0), band(ddr, 1.5, 3.0))
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


tags = sys.argv[1:]
if not tags:
    tags = sorted(os.path.basename(p)[3:-5] for p in glob.glob(D + "tw_*.json"))

hdr = ("%-12s %6s %6s %6s %6s %6s %6s %7s | %6s %6s | %6s %6s %6s %6s | %6s %6s %6s | %7s %4s %5s"
       % ("tag", "eff", "vxy01", "v15", "v510", "v1030", "d15", "d510",
          "fake", "dup", "dupB", "dupT", "dupE", "dW1530", "nhB", "nhT", "nhE",
          "nTC", "ms", "nPrm"))
print(hdr)
print("-" * len(hdr))
for t in tags:
    p = D + "tw_" + t + ".json"
    if not os.path.exists(p):
        continue
    m = json.load(open(p))
    m = {k: v["proto"] for k, v in m["metrics"].items()}
    dn, dd, pn, pd, wn, wd = hists("tw_" + t)
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
    if g(m, "dup_overall_incut") > FLOOR["dup_overall_incut"] + 1e-9:
        fl.append("DUP")
    print("%-12s %6.4f %6.4f %6.4f %6.4f %6.4f %6.4f %3.0f/%-3.0f| %6.4f %6.4f | %6.4f %6.4f %6.4f %6.4f | %6.3f %6.3f %6.3f | %7.0f %4.0f %5.0f %s"
          % (t, g(m, "eff_overall_incut"), g(m, "eff_vxy_0_1"), g(m, "eff_vxy_1_5"),
             g(m, "eff_vxy_5_10"), g(m, "eff_vxy_10_30"), g(m, "eff_dxy_1_5"), dn, dd,
             g(m, "fake_overall_incut"), g(m, "dup_overall_incut"),
             g(m, "dup_barrel"), g(m, "dup_transition"), g(m, "dup_endcap"),
             (wn / wd) if wd else float("nan"),
             g(m, "mean_nhitOT_barrel"), g(m, "mean_nhitOT_transition"),
             g(m, "mean_nhitOT_endcap"), g(m, "n_tc"), timing("tw_" + t), pn,
             ("FAIL:" + ",".join(fl)) if fl else "pass"))
print("-" * len(hdr))
print("prompt denom (vxy<1) = %.0f ; d510 denom = %.0f" % (pd, dd))
print("%-12s %6.4f %6s %6s %6s %6s %6s %3s/%-3s| %6s %6.4f | %6s %6s %6s %6s | %6.3f %6.3f %6.3f"
      % ("LST TARGET", LST["eff_overall_incut"], "-", "-", "-", "-", "-", "-", "-",
         "-", LST["dup_overall_incut"], "-", "-", "-", "-",
         LST["mean_nhitOT_barrel"], LST["mean_nhitOT_transition"], LST["mean_nhitOT_endcap"]))
