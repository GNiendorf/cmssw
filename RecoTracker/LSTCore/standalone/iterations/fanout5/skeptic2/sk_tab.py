#!/usr/bin/env python3
"""Independent scoreboard: everything recomputed from <tag>_hists.root directly.
Does NOT read any agent's .json."""
import os, sys
import ROOT
ROOT.gROOT.SetBatch(True)

FLOOR = dict(eff=.8127, v15=.8022, v510=.7267, v1030=.7170, d15=.5613, fake=.0480)

def band(h, lo, hi):
    t = 0.0; ax = h.GetXaxis()
    for b in range(1, h.GetNbinsX()+1):
        c = abs(ax.GetBinCenter(b))
        if c >= lo and (hi is None or c < hi):
            t += h.GetBinContent(b)
    return t

def metrics(path):
    f = ROOT.TFile.Open(path)
    if not f or f.IsZombie():
        return None
    EF = "Root__TC_base_0_0_ef_"; FR = "Root__TC_fr_"; DR = "Root__TC_dr_"; OL = "Root__TC_ol_"
    G = lambda n: f.Get(n)
    m = {}
    sa = lambda h: h.Integral(0, h.GetNbinsX()+1)
    efn_e, efd_e = G(EF+"numer_eta"), G(EF+"denom_eta")
    m["eff"]  = sa(efn_e)/sa(efd_e)          # compare_ab eff_overall_incut (incl under/overflow)
    m["effN"] = sa(efn_e); m["effD"] = sa(efd_e)
    for var in ("vxy","dxy"):
        hn, hd = G(EF+"numer_"+var), G(EF+"denom_"+var)
        for lo,hi,nm in ((0,1,"01"),(1,5,"15"),(5,10,"510"),(10,30,"1030")):
            n_, d_ = band(hn,lo,hi), band(hd,lo,hi)
            m["%s%s"%(var,nm)] = n_/d_ if d_>0 else float("nan")
            m["%s%s_n"%(var,nm)] = n_; m["%s%s_d"%(var,nm)] = d_
    frn,frd = G(FR+"numer_eta"), G(FR+"denom_eta")
    drn,drd = G(DR+"numer_eta"), G(DR+"denom_eta")
    m["fake"] = sa(frn)/sa(frd)
    m["dup"]  = sa(drn)/sa(drd)
    m["nTC"]  = sa(frd)
    for nm,lo,hi in (("B",0,1.1),("T",1.1,1.7),("E",1.7,None)):
        m["dup"+nm] = band(drn,lo,hi)/max(1e-9,band(drd,lo,hi))
        m["fake"+nm] = band(frn,lo,hi)/max(1e-9,band(frd,lo,hi))
    m["dupWin"] = band(drn,1.5,3.0)/max(1e-9,band(drd,1.5,3.0))
    m["dupWin25"] = band(drn,1.5,2.5)/max(1e-9,band(drd,1.5,2.5))
    oln, old = G(OL+"numer_eta"), G(OL+"denom_eta")
    for nm,lo,hi in (("B",0,1.1),("T",1.1,1.7),("E",1.7,None)):
        m["nh"+nm] = band(oln,lo,hi)/max(1e-9,band(old,lo,hi))
    m["nhWin"] = band(oln,1.5,3.0)/max(1e-9,band(old,1.5,3.0))
    f.Close()
    return m

def fails(m):
    fl = []
    if m["eff"] < FLOOR["eff"]: fl.append("EFF")
    if m["vxy15"] < FLOOR["v15"]: fl.append("V15")
    if m["vxy510"] < FLOOR["v510"]: fl.append("V510")
    if m["vxy1030"] < FLOOR["v1030"]: fl.append("V1030")
    if m["dxy15"] < FLOOR["d15"]: fl.append("D15")
    if m["dxy510_n"] < 71: fl.append("D510")
    if m["fake"] > FLOOR["fake"]: fl.append("FAKE")
    return fl

H = ("%-26s %8s %7s %7s %7s %7s %9s %8s | %6s %6s %6s | %7s %7s | %7s %7s %6s %s" %
     ("hists", "eff", "v15", "v510", "v1030", "d15", "d15 cnt", "d510", "nhB","nhT","nhE",
      "fake","dup","dupWin","nhWin","nTC","status"))

if __name__ == "__main__":
    print(H); print("-"*len(H))
    for p in sys.argv[1:]:
        m = metrics(p)
        if m is None:
            print("%-26s  UNREADABLE" % p); continue
        print("%-26s %8.5f %7.5f %7.5f %7.5f %7.5f %5.0f/%-3.0f %4.0f/%-3.0f | %6.3f %6.3f %6.3f | %7.5f %7.5f | %7.5f %7.4f %6.0f %s"
              % (os.path.basename(p).replace("_hists.root",""), m["eff"], m["vxy15"], m["vxy510"],
                 m["vxy1030"], m["dxy15"], m["dxy15_n"], m["dxy15_d"], m["dxy510_n"], m["dxy510_d"],
                 m["nhB"], m["nhT"], m["nhE"], m["fake"], m["dup"], m["dupWin"], m["nhWin"], m["nTC"],
                 ("FAIL:"+",".join(fails(m))) if fails(m) else "pass"))
