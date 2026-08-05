#!/usr/bin/env python3
"""M19 EXPLOIT length-aware order key: full scoreboard (floors + length + per-band dup)."""
import glob
import json
import os
import sys

D = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_lenorder/"
FLOOR = dict(eff_overall_incut=.8127, eff_vxy_1_5=.8022, eff_vxy_5_10=.7267,
             eff_vxy_10_30=.7170, eff_dxy_1_5=.5613, fake_overall_incut=.0480)
LST = dict(mean_nhitOT_barrel=10.151, mean_nhitOT_transition=10.010,
           mean_nhitOT_endcap=3.559, dup_overall_incut=.0513, eff_overall_incut=.8136)
_c = {}


def g(m, k):
    v = m.get(k)
    return v if v is not None else float("nan")


def d510(tag):
    if tag in _c:
        return _c[tag]
    import ROOT
    p = D + tag + "_hists.root"
    if not os.path.exists(p):
        _c[tag] = (float("nan"), float("nan"))
        return _c[tag]
    f = ROOT.TFile.Open(p)
    n = f.Get("Root__TC_base_0_0_ef_numer_dxy")
    de = f.Get("Root__TC_base_0_0_ef_denom_dxy")

    def band(h, lo, hi):
        t, ax = 0.0, h.GetXaxis()
        for b in range(1, h.GetNbinsX() + 1):
            c = abs(ax.GetBinCenter(b))
            if lo <= c < hi:
                t += h.GetBinContent(b)
        return t
    r = (band(n, 5, 10), band(de, 5, 10))
    f.Close()
    _c[tag] = r
    return r


def win(tag):
    """dup / fake / eff / meanOT restricted to |eta| 1.5-3.0 (the maintainer window)."""
    import ROOT
    p = D + tag + "_hists.root"
    if not os.path.exists(p):
        return (float("nan"),) * 3
    f = ROOT.TFile.Open(p)

    def band(h, lo, hi):
        t, ax = 0.0, h.GetXaxis()
        for b in range(1, h.GetNbinsX() + 1):
            c = abs(ax.GetBinCenter(b))
            if lo <= c < hi:
                t += h.GetBinContent(b)
        return t
    o = []
    for pre in ("Root__TC_dr_", "Root__TC_ol_"):
        n = f.Get(pre + "numer_eta")
        d = f.Get(pre + "denom_eta")
        o.append(band(n, 1.5, 3.0) / max(1e-9, band(d, 1.5, 3.0)))
    f.Close()
    return (o[0], o[1])


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
    tags = ["r_flag"] + sorted(os.path.basename(p)[:-5] for p in glob.glob(D + "*.json")
                               if os.path.basename(p)[:-5] not in ("r_flag", "r_def"))
ref = None
hdr = ("%-8s %-22s %7s %6s %6s %6s %6s %6s | %6s %6s %6s | %6s %6s %6s %6s %6s | %6s %6s | %7s %4s %s" %
       ("tag", "overrides", "eff", "v15", "v510", "v1030", "d15", "d510",
        "nhB", "nhT", "nhE", "fake", "dup", "dupB", "dupT", "dupE",
        "wdup", "wOT", "nTC", "ms", "verdict"))
print(hdr)
print("-" * len(hdr))
for t in tags:
    p = D + t + ".json"
    if not os.path.exists(p):
        continue
    m = {k: v["proto"] for k, v in json.load(open(p))["metrics"].items()}
    dn, dd = d510(t)
    wd, wo = win(t)
    cmd = ""
    if os.path.exists(D + t + ".cmd"):
        cmd = open(D + t + ".cmd").read().strip().split("OVERRIDES:")[-1].strip()
    fl = []
    for k, lbl in (("eff_overall_incut", "EFF"), ("eff_vxy_1_5", "V15"), ("eff_vxy_5_10", "V510"),
                   ("eff_vxy_10_30", "V1030"), ("eff_dxy_1_5", "D15")):
        if g(m, k) < FLOOR[k]:
            fl.append(lbl)
    if dn < 71:
        fl.append("D510")
    if g(m, "fake_overall_incut") > FLOOR["fake_overall_incut"]:
        fl.append("FAKE")
    if t == "r_flag":
        ref = m
    print("%-8s %-22s %7.5f %6.4f %6.4f %6.4f %6.4f %3.0f/%-2.0f| %6.3f %6.3f %6.3f | "
          "%6.4f %6.4f %6.4f %6.4f %6.4f | %6.4f %6.3f | %7.0f %4.0f %s"
          % (t, cmd[:22], g(m, "eff_overall_incut"), g(m, "eff_vxy_1_5"), g(m, "eff_vxy_5_10"),
             g(m, "eff_vxy_10_30"), g(m, "eff_dxy_1_5"), dn, dd,
             g(m, "mean_nhitOT_barrel"), g(m, "mean_nhitOT_transition"), g(m, "mean_nhitOT_endcap"),
             g(m, "fake_overall_incut"), g(m, "dup_overall_incut"), g(m, "dup_barrel"),
             g(m, "dup_transition"), g(m, "dup_endcap"), wd, wo, g(m, "n_tc"), timing(t),
             ("FAIL:" + ",".join(fl)) if fl else "pass"))
print("-" * len(hdr))
if ref is not None:
    print("\nDELTA vs FLAGSHIP (r_flag)  [+ = toward LST for nh*, - = toward LST for dup]")
    h2 = ("%-8s %-22s %8s %8s %8s %8s %8s %5s | %7s %7s %7s | %8s %8s" %
          ("tag", "overrides", "d_eff", "d_v15", "d_v510", "d_v1030", "d_d15", "d510",
           "d_nhB", "d_nhT", "d_nhE", "d_fake", "d_dup"))
    print(h2)
    print("-" * len(h2))
    for t in tags:
        p = D + t + ".json"
        if not os.path.exists(p) or t == "r_flag":
            continue
        m = {k: v["proto"] for k, v in json.load(open(p))["metrics"].items()}
        dn, _ = d510(t)
        cmd = open(D + t + ".cmd").read().strip().split("OVERRIDES:")[-1].strip() if os.path.exists(D + t + ".cmd") else ""
        print("%-8s %-22s %+8.5f %+8.5f %+8.5f %+8.5f %+8.5f %5.0f | %+7.3f %+7.3f %+7.3f | %+8.5f %+8.5f"
              % (t, cmd[:22],
                 g(m, "eff_overall_incut") - g(ref, "eff_overall_incut"),
                 g(m, "eff_vxy_1_5") - g(ref, "eff_vxy_1_5"),
                 g(m, "eff_vxy_5_10") - g(ref, "eff_vxy_5_10"),
                 g(m, "eff_vxy_10_30") - g(ref, "eff_vxy_10_30"),
                 g(m, "eff_dxy_1_5") - g(ref, "eff_dxy_1_5"), dn,
                 g(m, "mean_nhitOT_barrel") - g(ref, "mean_nhitOT_barrel"),
                 g(m, "mean_nhitOT_transition") - g(ref, "mean_nhitOT_transition"),
                 g(m, "mean_nhitOT_endcap") - g(ref, "mean_nhitOT_endcap"),
                 g(m, "fake_overall_incut") - g(ref, "fake_overall_incut"),
                 g(m, "dup_overall_incut") - g(ref, "dup_overall_incut")))
print("\nFLOORS: eff>=.8127 v15>=.8022 v510>=.7267 v1030>=.7170 d15>=.5613 d510>=71 fake<=.0480")
print("LST   : nhB 10.151 nhT 10.010 nhE 3.559 | dup .0513 | eff .8136")
