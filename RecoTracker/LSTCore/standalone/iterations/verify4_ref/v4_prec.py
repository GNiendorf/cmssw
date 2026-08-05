#!/usr/bin/env python3
"""Precise (5-decimal) headline re-derivation + deltas vs a reference hists file."""
import sys, os
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype")
import ROOT
ROOT.gROOT.SetBatch(True); ROOT.gErrorIgnoreLevel = ROOT.kError
import compare_ab as C
KEYS = ["eff_overall_incut","dup_overall_incut","fake_overall_incut",
        "eff_vxy_1_5","eff_vxy_5_10","eff_vxy_10_30","eff_dxy_1_5","eff_dxy_5_10","eff_dxy_10_30",
        "eff_barrel","eff_transition","eff_endcap","dup_barrel","dup_transition","dup_endcap",
        "fake_barrel","fake_transition","fake_endcap",
        "mean_nhitOT","mean_nhitOT_barrel","mean_nhitOT_transition","mean_nhitOT_endcap","n_tc"]
ref = None
if sys.argv[1].startswith("REF="):
    ref = C.compute_metrics(sys.argv[1][4:], "ref"); rest = sys.argv[2:]
else:
    rest = sys.argv[1:]
for a in rest:
    lab, p = a.split("=",1)
    if not os.path.exists(p):
        print("MISSING", lab, p); continue
    m = C.compute_metrics(p, lab)
    out = [lab]
    for k in KEYS:
        v = m.get(k)
        s = "n/a" if v is None else ("%d" % v if k=="n_tc" else "%.5f" % v)
        if ref is not None and v is not None and ref.get(k) is not None:
            d = v - ref[k]
            s += ("(%+d)" % d) if k=="n_tc" else ("(%+.5f)" % d)
        out.append("%s=%s" % (k, s))
    print("  ".join(out))
