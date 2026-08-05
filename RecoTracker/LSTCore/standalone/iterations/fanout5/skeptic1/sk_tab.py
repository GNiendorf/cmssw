#!/usr/bin/env python3
"""SKEPTIC-1 independent scoreboard, read straight from any *_hists.root.

Written from scratch against efficiency/src/performance.cc + compare_ab.py
semantics; it does NOT import or trust the exploit agents' tab scripts or their
cached .json files.  Every band is recomputed from the histogram bin contents.

Usage: sk_tab.py <label>=<hists.root> [...]
"""
import os
import sys

import ROOT

ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError

FLOOR = dict(eff=.8127, v15=.8022, v510=.7267, v1030=.7170, d15=.5613, fake=.0480)
EF = "Root__TC_base_0_0_ef_"
FR = "Root__TC_fr_"
DR = "Root__TC_dr_"
OL = "Root__TC_ol_"


def band(h, lo, hi):
    t = 0.0
    ax = h.GetXaxis()
    for b in range(1, h.GetNbinsX() + 1):
        c = abs(ax.GetBinCenter(b))
        if lo <= c < (hi if hi is not None else 1e9):
            t += h.GetBinContent(b)
    return t


def allb(h):
    return h.Integral(0, h.GetNbinsX() + 1)


def read(path):
    f = ROOT.TFile.Open(path)
    if not f or f.IsZombie():
        return None
    o = {}
    efn_pt, efd_pt = f.Get(EF + "numer_pt"), f.Get(EF + "denom_pt")
    o["eff_pt"] = allb(efn_pt) / allb(efd_pt)
    efn_e, efd_e = f.Get(EF + "numer_eta"), f.Get(EF + "denom_eta")
    o["eff"] = allb(efn_e) / allb(efd_e)
    o["effnum"] = allb(efn_e)
    o["effden"] = allb(efd_e)
    for var in ("vxy", "dxy"):
        n, d = f.Get(EF + "numer_" + var), f.Get(EF + "denom_" + var)
        for lo, hi in ((0, 1), (1, 5), (5, 10), (10, 30)):
            nn, dd = band(n, lo, hi), band(d, lo, hi)
            o["%s%g_%g" % (var, lo, hi)] = nn / dd if dd else float("nan")
            o["%s%g_%g_n" % (var, lo, hi)] = nn
            o["%s%g_%g_d" % (var, lo, hi)] = dd
    frn, frd = f.Get(FR + "numer_eta"), f.Get(FR + "denom_eta")
    o["fake"] = allb(frn) / allb(frd)
    o["nTC"] = allb(frd)
    drn, drd = f.Get(DR + "numer_eta"), f.Get(DR + "denom_eta")
    o["dup"] = allb(drn) / allb(drd)
    o["dupnum"] = allb(drn)
    for lbl, lo, hi in (("B", 0, 1.1), ("T", 1.1, 1.7), ("E", 1.7, None),
                        ("w30", 1.5, 3.0), ("w25", 1.5, 2.5), ("lo15", 0, 1.5),
                        ("gt25", 2.5, None)):
        dd = band(drd, lo, hi)
        o["dup" + lbl] = band(drn, lo, hi) / dd if dd else float("nan")
        o["n" + lbl] = dd
        fd = band(frd, lo, hi)
        o["fake" + lbl] = band(frn, lo, hi) / fd if fd else float("nan")
        en, ed = band(efn_e, lo, hi), band(efd_e, lo, hi)
        o["eff" + lbl] = en / ed if ed else float("nan")
    oln, old = f.Get(OL + "numer_eta"), f.Get(OL + "denom_eta")
    o["otSum"] = allb(oln)
    o["ot"] = allb(oln) / allb(old)
    for lbl, lo, hi in (("B", 0, 1.1), ("T", 1.1, 1.7), ("E", 1.7, None), ("w30", 1.5, 3.0),
                        ("w25", 1.5, 2.5), ("gt25", 2.5, None), ("lo15", 0, 1.5)):
        d = band(old, lo, hi)
        o["ot" + lbl] = band(oln, lo, hi) / d if d else float("nan")
        o["otS" + lbl] = band(oln, lo, hi)
        o["otD" + lbl] = d
    f.Close()
    return o


def verdict(o):
    fl = []
    if o["eff"] < FLOOR["eff"]:
        fl.append("EFF")
    if o["vxy1_5"] < FLOOR["v15"]:
        fl.append("V15")
    if o["vxy5_10"] < FLOOR["v510"]:
        fl.append("V510")
    if o["vxy10_30"] < FLOOR["v1030"]:
        fl.append("V1030")
    if o["dxy1_5"] < FLOOR["d15"]:
        fl.append("D15")
    if o["dxy5_10_n"] < 71:
        fl.append("D510")
    if o["fake"] > FLOOR["fake"]:
        fl.append("FAKE")
    return "FAIL:" + ",".join(fl) if fl else "pass"


rows = []
for a in sys.argv[1:]:
    lbl, _, p = a.partition("=")
    if not p:
        p, lbl = a, os.path.basename(a).replace("_hists.root", "")
    o = read(p)
    if o is None:
        print("MISSING %s" % p)
        continue
    rows.append((lbl, o))

h = ("%-16s %7s %7s %7s %7s %7s %7s %8s %8s | %7s %7s %7s %7s %8s %8s %8s %8s | %7s %7s %7s"
     " | %6s %6s %6s %6s %8s %8s %s" %
     ("tag", "eff", "vxy01", "v15", "v510", "v1030", "d15", "d15cnt", "d510",
      "dup", "dupB", "dupT", "dupE", "dup1.5-3", "dup1.5-2.5", "dup<1.5", "dup>2.5",
      "fake", "fakeT", "fakeE", "nhB", "nhT", "nhE", "nhW", "nTC", "otSum", "verdict"))
print(h)
print("-" * len(h))
for lbl, o in rows:
    print("%-16s %7.5f %7.5f %7.5f %7.5f %7.5f %7.5f %8.1f %4.0f/%-3.0f| %7.5f %7.5f %7.5f %7.5f"
          " %8.5f %10.5f %8.5f %8.5f | %7.5f %7.5f %7.5f | %6.3f %6.3f %6.3f %6.3f %8.0f %8.0f %s" %
          (lbl, o["eff"], o["vxy0_1"], o["vxy1_5"], o["vxy5_10"], o["vxy10_30"],
           o["dxy1_5"], o["dxy1_5_n"], o["dxy5_10_n"], o["dxy5_10_d"],
           o["dup"], o["dupB"], o["dupT"], o["dupE"], o["dupw30"], o["dupw25"],
           o["duplo15"], o["dupgt25"], o["fake"], o["fakeT"], o["fakeE"],
           o["otB"], o["otT"], o["otE"], o["otw30"], o["nTC"], o["otSum"], verdict(o)))
