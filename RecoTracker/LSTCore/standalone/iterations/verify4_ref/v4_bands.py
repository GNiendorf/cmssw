#!/usr/bin/env python3
"""Per-band NUMERATOR and DENOMINATOR (sim counts) for the vxy/dxy displaced bands and
the three eta regions, straight out of createPerfNumDenHists output. Verifier-owned."""
import sys, os
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype")
import ROOT
ROOT.gROOT.SetBatch(True); ROOT.gErrorIgnoreLevel = ROOT.kError
import compare_ab as C
EF = "Root__TC_base_0_0_ef_"
BANDS = [("v01","vxy",0,1),("v15","vxy",1,5),("v510","vxy",5,10),("v1030","vxy",10,30),
         ("d01","dxy",0,1),("d15","dxy",1,5),("d510","dxy",5,10),("d1030","dxy",10,30)]
ETA = [("B",0.0,1.1),("T",1.1,1.7),("E",1.7,None)]
ref = None
args = sys.argv[1:]
if args[0].startswith("REF="):
    ref = args[0][4:]; args = args[1:]
def counts(path):
    f = ROOT.TFile.Open(path)
    out = {}
    for lab, var, lo, hi in BANDS:
        hn = f.Get(EF+"numer_"+var); hd = f.Get(EF+"denom_"+var)
        out[lab] = (C.sum_band(hn,lo,hi), C.sum_band(hd,lo,hi))
    hn = f.Get(EF+"numer_eta"); hd = f.Get(EF+"denom_eta")
    for lab, lo, hi in ETA:
        out["eta"+lab] = (C.sum_band(hn,lo,hi), C.sum_band(hd,lo,hi))
    out["ALL"] = (C.sum_all(hn), C.sum_all(hd))
    f.Close(); return out
rc = counts(ref) if ref else None
for a in args:
    lab, p = a.split("=",1)
    if not os.path.exists(p): print("MISSING",lab,p); continue
    c = counts(p)
    print("== %s" % lab)
    for k in [b[0] for b in BANDS] + ["etaB","etaT","etaE","ALL"]:
        n, d = c[k]
        s = "  %-6s numer %7.0f / denom %7.0f = %.5f" % (k, n, d, (n/d if d else 0))
        if rc: s += "   dNUMER %+.0f  drate %+.5f" % (n-rc[k][0], (n/d if d else 0)-(rc[k][0]/rc[k][1] if rc[k][1] else 0))
        print(s)
