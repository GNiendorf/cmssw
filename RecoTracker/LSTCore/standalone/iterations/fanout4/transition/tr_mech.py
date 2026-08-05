#!/usr/bin/env python3
"""Mechanism: chain dcaXY resolution vs |eta| for TRUE PROMPT chains (the misrouting proof)."""
import sys, numpy as np, uproot
t = uproot.open(sys.argv[1])["tree"]
a = t.arrays(["tc_pt","tc_eta","tc_isFake","tc_isChain","tc_dbgBr","tc_dbgNL","tc_dbgNB",
              "tc_dbgNMD","tc_dbgNPS","tc_dbgDca","tc_simIdxAll","sim_vx","sim_vy","sim_pca_dxy"],
             library="np")
E=[]
for i in range(len(a["tc_pt"])):
    vxy=np.hypot(np.asarray(a["sim_vx"][i]),np.asarray(a["sim_vy"][i]))
    dxy=np.abs(np.asarray(a["sim_pca_dxy"][i])); ns=len(vxy); n=len(a["tc_pt"][i])
    sl=a["tc_simIdxAll"][i]; mv=np.full(n,-1.0); md=np.full(n,-1.0)
    for k in range(n):
        s=[x for x in sl[k] if 0<=x<ns]
        if s: mv[k]=vxy[s].min(); md[k]=dxy[s].min()
    E.append((np.asarray(a["tc_pt"][i]),np.asarray(a["tc_eta"][i]),np.asarray(a["tc_isFake"][i]),
              np.asarray(a["tc_isChain"][i]),np.asarray(a["tc_dbgBr"][i]),np.asarray(a["tc_dbgNL"][i]),
              np.asarray(a["tc_dbgNB"][i]),np.asarray(a["tc_dbgNMD"][i]),np.asarray(a["tc_dbgNPS"][i]),
              np.asarray(a["tc_dbgDca"][i]),mv,md))
pt,eta,fk,ich,br,nl,nb,nmd,nps,dca,mv,md=[np.concatenate(x) for x in zip(*E)]
fk=fk.astype(bool); ch=ich>0; ae=np.abs(eta)
prompt = ch & (pt>0.9) & ~fk & (mv>=0) & (mv<1) & (md<0.2) & (nl>=5)
print("== chain dcaXY for TRUE PROMPT (sim vxy<1, |dxy|<0.2) 5+-layer chains, vs |eta| ==")
print("   |eta| bin      n    dca p50   p75   p90    frac(dca>=0.5) [-> exempt branch]  meanPSfrac  meanBarrelFrac")
for lo in np.arange(0.0, 2.4, 0.2):
    m = prompt & (ae>=lo) & (ae<lo+0.2)
    if m.sum()<50: continue
    print("   [%.1f,%.1f) %7d   %5.3f %5.3f %5.3f      %5.1f%%                 %.2f        %.2f"
          % (lo, lo+0.2, m.sum(), np.median(dca[m]), np.percentile(dca[m],75),
             np.percentile(dca[m],90), 100.0*(dca[m]>=0.5).mean(),
             (nps[m]/np.maximum(1,nmd[m])).mean(), (nb[m]/np.maximum(1,nmd[m])).mean()))
print("\n== same, restricted to chains with a MIXED barrel+endcap MD set ==")
mix = (nb>0)&(nb<nmd)
for lo in np.arange(0.8, 2.0, 0.2):
    m = prompt & mix & (ae>=lo) & (ae<lo+0.2)
    if m.sum()<50: continue
    print("   [%.1f,%.1f) n=%6d dca p50=%5.3f frac(dca>=0.5)=%5.1f%%" % (lo,lo+0.2,m.sum(),
          np.median(dca[m]), 100.0*(dca[m]>=0.5).mean()))
print("\n== pure-barrel vs pure-endcap vs mixed inside the band ==")
band = prompt & (ae>=1.1)&(ae<1.7)
for lab, s in (("pure barrel", band&(nb==nmd)), ("mixed", band&mix), ("pure endcap", band&(nb==0))):
    if s.sum()<20: continue
    print("   %-12s n=%6d dca p50=%5.3f frac(dca>=0.5)=%5.1f%%" % (lab, s.sum(),
          np.median(dca[s]), 100.0*(dca[s]>=0.5).mean()))
