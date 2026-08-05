#!/usr/bin/env python3
"""Sim-track COUNTS behind every ratio, read straight out of the hists file so no
denominator is assumed. Usage: ds_counts.py <hists.root> [<hists.root> ...]
Prints numer/denom per band for the five floor bands + overall + vxy01."""
import sys

import ROOT

ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError

EF = "Root__TC_base_0_0_ef_"
BANDS = [("v15", "vxy", 1.0, 5.0), ("v510", "vxy", 5.0, 10.0),
         ("v1030", "vxy", 10.0, 30.0), ("d15", "dxy", 1.0, 5.0),
         ("d510", "dxy", 5.0, 10.0), ("d1030", "dxy", 10.0, 30.0),
         ("vxy01", "vxy", 0.0, 1.0)]


def band(h, lo, hi):
    t = 0.0
    ax = h.GetXaxis()
    for b in range(1, h.GetNbinsX() + 1):
        c = abs(ax.GetBinCenter(b))
        if c >= lo and c < hi:
            t += h.GetBinContent(b)
    return t


for path in sys.argv[1:]:
    f = ROOT.TFile.Open(path)
    n = f.Get(EF + "numer_eta")
    d = f.Get(EF + "denom_eta")
    out = ["eff %d/%d" % (round(n.Integral(0, n.GetNbinsX() + 1)),
                          round(d.Integral(0, d.GetNbinsX() + 1)))]
    for name, var, lo, hi in BANDS:
        hn = f.Get(EF + "numer_" + var)
        hd = f.Get(EF + "denom_" + var)
        out.append("%s %d/%d" % (name, round(band(hn, lo, hi)), round(band(hd, lo, hi))))
    print("%-34s %s" % (path.split("/")[-1], "  ".join(out)))
    f.Close()
