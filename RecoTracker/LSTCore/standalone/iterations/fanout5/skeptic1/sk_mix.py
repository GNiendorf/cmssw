#!/usr/bin/env python3
"""SKEPTIC-1 length-metric MIX DECOMPOSITION.

mean_nhitOT = sum(nhitOT) / nTC.  A lever that DELETES short TCs raises the mean
without lengthening a single track.  This splits the observed delta per region:
   delta_mean = (dSum / nTC_ref)          <- REAL: hits actually added
              + (mean_new * (nTC_ref/nTC_new - 1)) ... exact split below.
Exactly:  mean_new - mean_ref = (Sum_new - Sum_ref)/N_ref  +  Sum_new*(1/N_new - 1/N_ref)
          = REAL term                     +  MIX term.
Usage: sk_mix.py <ref_label>=<ref_hists> <label>=<hists> [...]
"""
import sys

import ROOT

ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError

OL = "Root__TC_ol_"
REG = (("all", 0, None), ("B", 0, 1.1), ("T", 1.1, 1.7), ("E", 1.7, None),
       ("w1.5-3", 1.5, 3.0), ("w1.5-2.5", 1.5, 2.5))


def band(h, lo, hi):
    if hi is None and lo == 0:
        return h.Integral(0, h.GetNbinsX() + 1)
    t = 0.0
    ax = h.GetXaxis()
    for b in range(1, h.GetNbinsX() + 1):
        c = abs(ax.GetBinCenter(b))
        if lo <= c < (hi if hi is not None else 1e9):
            t += h.GetBinContent(b)
    return t


def read(p):
    f = ROOT.TFile.Open(p)
    n, d = f.Get(OL + "numer_eta"), f.Get(OL + "denom_eta")
    o = {}
    for lbl, lo, hi in REG:
        o[lbl] = (band(n, lo, hi), band(d, lo, hi))
    f.Close()
    return o


args = [a.partition("=") for a in sys.argv[1:]]
ref_lbl, refp = args[0][0], args[0][2]
ref = read(refp)
print("MIX DECOMPOSITION of mean tc_nhitOT, reference = %s" % ref_lbl)
hdr = "%-18s %-10s %9s %9s %9s %9s %9s %9s" % (
    "tag", "region", "meanRef", "meanNew", "dMean", "REAL", "MIX", "dNTC")
print(hdr)
print("-" * len(hdr))
for lbl, _, p in args[1:]:
    o = read(p)
    for r, _lo, _hi in REG:
        sR, nR = ref[r]
        sN, nN = o[r]
        mR, mN = sR / nR, sN / nN
        real = (sN - sR) / nR
        mix = sN * (1.0 / nN - 1.0 / nR)
        print("%-18s %-10s %9.4f %9.4f %+9.4f %+9.4f %+9.4f %+9.0f" %
              (lbl, r, mR, mN, mN - mR, real, mix, nN - nR))
    print()
