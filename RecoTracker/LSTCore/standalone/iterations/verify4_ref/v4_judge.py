#!/usr/bin/env python3
"""Verifier-owned re-implementation of the matched-duplicate-rate judge.
Metrics are recomputed FROM THE HISTS, not read from any agent's json."""
import sys, os
sys.path.insert(0,"/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype")
import ROOT
ROOT.gROOT.SetBatch(True); ROOT.gErrorIgnoreLevel = ROOT.kError
import compare_ab as C
S="/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/"
# global -XCT frontier at -AT3 6 -CCN 1 -CCR 2, RE-DERIVED from raw hists
GF=[(3.0,"fin_ref/r_F_A6X3_hists.root"),(3.5,"fin_ref/r_E_A6X35_hists.root"),
    (4.0,"fin_ref/r_F_A6X4_hists.root"),(4.5,"fin_ref/r_F_A6X45_hists.root")]
G=[]
for x,p in GF:
    m=C.compute_metrics(S+p,"g")
    G.append((m["dup_overall_incut"],m["eff_overall_incut"],m["fake_overall_incut"],x))
G.sort()
print("GLOBAL FRONTIER (re-derived): "+" | ".join("XCT %.2f dup %.5f eff %.5f fake %.5f"%(g[3],g[0],g[1],g[2]) for g in G))
def interp(dup,col):
    pts=[(g[0],g[col]) for g in G]
    if dup<=pts[0][0]: a,b=pts[0],pts[1]
    elif dup>=pts[-1][0]: a,b=pts[-2],pts[-1]
    else:
        for i in range(len(pts)-1):
            if pts[i][0]<=dup<=pts[i+1][0]: a,b=pts[i],pts[i+1]; break
    return a[1] if b[0]==a[0] else a[1]+(b[1]-a[1])*(dup-a[0])/(b[0]-a[0])
print("%-14s %8s %8s %8s %9s %9s %9s"%("tag","eff","dup","fake","gEff","EFFGAIN","FAKEGAIN"))
for a in sys.argv[1:]:
    lab,p=a.split("=",1)
    if not os.path.exists(p): print(lab,"MISSING"); continue
    m=C.compute_metrics(p,lab)
    e,d,f=m["eff_overall_incut"],m["dup_overall_incut"],m["fake_overall_incut"]
    ge,gf=interp(d,1),interp(d,2)
    ex="  EXTRAP" if (d<G[0][0] or d>G[-1][0]) else ""
    print("%-14s %8.5f %8.5f %8.5f %9.5f %+9.5f %+9.5f%s"%(lab,e,d,f,ge,e-ge,gf-f,ex))
