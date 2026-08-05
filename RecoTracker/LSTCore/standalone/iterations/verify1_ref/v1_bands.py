#!/usr/bin/env python3
"""Track-level (numerator) counts in every displacement band, so that 'bit-identical'
claims can be checked as integers rather than as 5-decimal rates.

Usage: v1_bands.py <ref.root> <lab>=<file> [...]
"""
import sys
import ROOT

ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError
EF = "Root__TC_base_0_0_ef_"
VXY = [(0.0, 1.0), (1.0, 5.0), (5.0, 10.0), (10.0, 30.0)]


def band(h, lo, hi):
    t = 0.0
    ax = h.GetXaxis()
    for b in range(1, h.GetNbinsX() + 1):
        c = abs(ax.GetBinCenter(b))
        if c >= lo and (hi is None or c < hi):
            t += h.GetBinContent(b)
    return t


def counts(path):
    f = ROOT.TFile.Open(path)
    out = {}
    for var in ("vxy", "dxy"):
        hn = f.Get(EF + "numer_" + var)
        hd = f.Get(EF + "denom_" + var)
        for lo, hi in VXY:
            out["%s%g-%g" % (var, lo, hi)] = (band(hn, lo, hi), band(hd, lo, hi))
    f.Close()
    return out


ref = counts(sys.argv[1])
keys = list(ref.keys())
print("%-22s" % "band" + "".join("%16s" % k for k in keys))
print("%-22s" % "REF(numer/denom)" + "".join("%16s" % ("%.0f/%.0f" % ref[k]) for k in keys))
for a in sys.argv[2:]:
    lab, path = a.split("=", 1)
    c = counts(path)
    print("%-22s" % (lab + " numer") + "".join("%16s" % ("%.0f" % c[k][0]) for k in keys))
    print("%-22s" % (lab + " D(trk)") + "".join("%16s" % ("%+.0f" % (c[k][0] - ref[k][0])) for k in keys))
