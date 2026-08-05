#!/usr/bin/env python3
"""m13_bands.py - INTEGER sim counts behind every A/B efficiency band (READ-ONLY).

compare_ab.py reports ratios; for actionability we need the raw numerator/denominator
sim counts (how many sims a band deficit is actually worth). Same band definition and
the same histogram sets compare_ab.py uses.
"""
import ROOT

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype"
EF = "Root__TC_base_0_0_ef_"
BANDS = [(0.0, 1.0), (1.0, 5.0), (5.0, 10.0), (10.0, 30.0)]


def band(h, lo, hi):
    ax = h.GetXaxis()
    return sum(h.GetBinContent(b) for b in range(1, h.GetNbinsX() + 1)
               if lo <= abs(ax.GetBinCenter(b)) < hi)


def dump(tag, proto, base):
    fp, fb = ROOT.TFile.Open(proto), ROOT.TFile.Open(base)
    print(f"===== {tag} =====")
    for var in ("vxy", "dxy"):
        hnp, hdp = fp.Get(EF + "numer_" + var), fp.Get(EF + "denom_" + var)
        hnb, hdb = fb.Get(EF + "numer_" + var), fb.Get(EF + "denom_" + var)
        for lo, hi in BANDS:
            dp, db = band(hdp, lo, hi), band(hdb, lo, hi)
            np_, nb = band(hnp, lo, hi), band(hnb, lo, hi)
            print(f"  {var}[{lo:g},{hi:g})  N={dp:6.0f}  proto={np_:6.0f} ({np_/dp:.4f})"
                  f"  base={nb:6.0f} ({nb/db:.4f})  dSims={np_-nb:+.0f}")
    hn, hd = fp.Get(EF + "numer_pt"), fp.Get(EF + "denom_pt")
    hnb, hdb = fb.Get(EF + "numer_pt"), fb.Get(EF + "denom_pt")
    n = hn.Integral(0, hn.GetNbinsX() + 1); d = hd.Integral(0, hd.GetNbinsX() + 1)
    nb = hnb.Integral(0, hnb.GetNbinsX() + 1); db = hdb.Integral(0, hdb.GetNbinsX() + 1)
    print(f"  overall     N={d:6.0f}  proto={n:6.0f} ({n/d:.4f})  base={nb:6.0f} ({nb/db:.4f})"
          f"  dSims={n-nb:+.0f}")
    fp.Close(); fb.Close()


dump("300 evt: w7 vs LST baseline", f"{SA}/ab_m12_w7_hists.root", f"{SA}/base300_hists.root")
dump("300 evt: m8_h4b (previous anchor) vs LST baseline",
     f"{SA}/ab_m8_h4b_hists.root", f"{SA}/base300_hists.root")
dump("test-60 (frozen): w7 vs LST baseline",
     f"{SA}/ab_m12_w7_te60_hists.root", f"{SA}/base60_hists.root")
