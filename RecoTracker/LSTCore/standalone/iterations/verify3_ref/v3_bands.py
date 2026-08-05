#!/usr/bin/env python3
"""VERIFIER-3: every band, quoted or not, straight from the raw hists, WITH the
numerator and denominator so a "regression" can be read as tracks rather than rates.
Usage: v3_bands.py <ref_hists.root> <hists.root> [<hists.root> ...]
The first file is the reference every later file is differenced against.
"""
import os
import sys

sys.path.insert(0, '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype')
import ROOT  # noqa: E402
ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError
import compare_ab as CA  # noqa: E402

EF = "Root__TC_base_0_0_ef_"
FR = "Root__TC_fr_"
DR = "Root__TC_dr_"
OL = "Root__TC_ol_"
BANDS = [(0, 1), (1, 5), (5, 10), (10, 30), (30, 60), (60, None)]
ETA = [("B", 0.0, 1.1), ("T", 1.1, 1.7), ("E", 1.7, None)]


def harvest(path):
    f = ROOT.TFile.Open(path)
    r = {}
    for var in ("vxy", "dxy"):
        hn = f.Get(EF + "numer_" + var)
        hd = f.Get(EF + "denom_" + var)
        for lo, hi in BANDS:
            k = "%s_%g_%s" % (var, lo, hi if hi is not None else 'inf')
            r[k] = (CA.sum_band(hn, lo, hi), CA.sum_band(hd, lo, hi))
    # eta-binned eff / dup / fake with numerators and denominators
    for pre, nm in ((EF, 'eff'), (DR, 'dup'), (FR, 'fak')):
        hn = f.Get(pre + "numer_eta")
        hd = f.Get(pre + "denom_eta")
        for reg, lo, hi in ETA:
            r["%s%s" % (nm, reg)] = (CA.sum_band(hn, lo, hi), CA.sum_band(hd, lo, hi))
        r["%sALL" % nm] = (CA.sum_all(hn), CA.sum_all(hd))
    on = f.Get(OL + "numer_eta")
    od = f.Get(OL + "denom_eta")
    for reg, lo, hi in ETA:
        r["nh%s" % reg] = (CA.sum_band(on, lo, hi), CA.sum_band(od, lo, hi))
    r["nhALL"] = (CA.sum_all(on), CA.sum_all(od))
    f.Close()
    return r


files = sys.argv[1:]
data = [(os.path.basename(p).replace('_hists.root', ''), harvest(p)) for p in files]
ref_name, ref = data[0]
keys = list(ref.keys())

print("REFERENCE = %s" % ref_name)
hdr = "%-24s" % "band" + "%12s" % "ref rate" + "%10s" % "num" + "%9s" % "den"
for nm, _ in data[1:]:
    hdr += "%12s%9s" % (nm[:11], "dNum")
print(hdr)
print("-" * len(hdr))
for k in keys:
    n, d = ref[k]
    rate = (n / d) if d else float('nan')
    line = "%-24s%12.5f%10.0f%9.0f" % (k, rate, n, d)
    for nm, m in data[1:]:
        n2, d2 = m[k]
        r2 = (n2 / d2) if d2 else float('nan')
        line += "%12.5f%+9.0f" % (r2, n2 - n)
    print(line)
