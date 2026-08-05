#!/usr/bin/env python3
"""dup / fake / eff and mean OT length in the maintainer window |eta| 1.5-3.0."""
import os, sys, ROOT
D = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_len/"
BASE = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/base300_hists.root"
def band(h, lo, hi):
    t = 0.0; ax = h.GetXaxis()
    for b in range(1, h.GetNbinsX() + 1):
        c = abs(ax.GetBinCenter(b))
        if lo <= c < hi: t += h.GetBinContent(b)
    return t
def row(path, tag):
    f = ROOT.TFile.Open(path)
    if not f or f.IsZombie(): return
    o = []
    for pre in ("Root__TC_dr_", "Root__TC_fr_", "Root__TC_base_0_0_ef_", "Root__TC_ol_"):
        n = f.Get(pre + "numer_eta"); d = f.Get(pre + "denom_eta")
        o.append(band(n, 1.5, 3.0) / max(1e-9, band(d, 1.5, 3.0)))
        o.append(band(d, 1.5, 3.0))
    print("%-16s dup=%.4f (nTC %6.0f) fake=%.4f eff=%.4f (nsim %5.0f) meanOT=%.3f"
          % (tag, o[0], o[1], o[2], o[4], o[5], o[6]))
    f.Close()
row(BASE, "LST BASE")
for t in sys.argv[1:]:
    p = D + t + "_hists.root"
    if os.path.exists(p): row(p, t)
