#!/usr/bin/env python3
import sys, numpy as np, uproot
t = uproot.open(sys.argv[1])["tree"]
a = t.arrays(["tc_pt","tc_eta","tc_isFake","tc_isChain","tc_dbgBr","tc_dbgNL","tc_dbgNN",
              "tc_dbgInLay","tc_dbgMP","tc_dbgMD","tc_dbgDca","tc_simIdxAll","sim_vx","sim_vy",
              "sim_pca_dxy"],library="np")
E=[]
for i in range(len(a["tc_pt"])):
    vxy=np.hypot(np.asarray(a["sim_vx"][i]),np.asarray(a["sim_vy"][i]));dxy=np.abs(np.asarray(a["sim_pca_dxy"][i]))
    ns=len(vxy);n=len(a["tc_pt"][i]);sl=a["tc_simIdxAll"][i]
    sk=np.full(n,-1,dtype=np.int64);mv=np.zeros(n);md=np.zeros(n)
    for k in range(n):
        s=[x for x in sl[k] if 0<=x<ns]
        if s: sk[k]=i*100000+s[0];mv[k]=vxy[s].max();md[k]=dxy[s].max()
    E.append((np.asarray(a["tc_pt"][i]),np.asarray(a["tc_eta"][i]),np.asarray(a["tc_isFake"][i]),
              np.asarray(a["tc_isChain"][i]),np.asarray(a["tc_dbgBr"][i]),np.asarray(a["tc_dbgNL"][i]),
              np.asarray(a["tc_dbgNN"][i]),np.asarray(a["tc_dbgInLay"][i]),np.asarray(a["tc_dbgMP"][i]),
              np.asarray(a["tc_dbgMD"][i]),np.asarray(a["tc_dbgDca"][i]),sk,mv,md))
pt,eta,fk,ich,br,nl,nn,il,mp,mdm,dca,sk,mv,md=[np.concatenate(x) for x in zip(*E)]
fk=fk.astype(bool);ch=ich>0;mX=np.maximum(mp,mdm);pt9=pt>0.9
band=pt9&ch&(np.abs(eta)>=1.1)&(np.abs(eta)<1.7)
ball=pt9&(np.abs(eta)>=1.1)&(np.abs(eta)<1.7)
bf,bn=fk[ball].sum(),ball.sum();gf,gn=fk[pt9].sum(),pt9.sum()
DS={"vxy>=5":mv>=5,"vxy>=10":mv>=10,"dxy>=1":md>=1,"dxy>=5":md>=5}
def risk(kill,d):
    v=set(sk[kill&~fk&d].tolist())-{-1};s=set(sk[pt9&~fk&~kill&d].tolist());return len(v-s)
def rep(name,kill):
    kf,kt=(kill&fk).sum(),(kill&~fk).sum()
    print("  %-42s %5d %5d | %.4f %.4f | %s"%(name,kf,kt,(bf-kf)/(bn-kill.sum()),
          (gf-kf)/(gn-kill.sum())," ".join("%7d"%risk(kill,d) for d in DS.values())))
print("  %-42s killF killT | bandFk globFk | %s"%("predicate"," ".join("%7s"%k for k in DS)))
ex=band&(br==3)&(il==1)
print("-- inLay1 exempt, with a dca CAP (tighten only the mis-routed window)")
for dm in (1.0,1.5,2.0,3.0,1e9):
    for f in (1.0,1.5,2.0,3.0):
        rep("inLay1 ex dca<%.1f mX<%.1f"%(dm,f), ex&(dca<dm)&(mX<f))
print("-- inLay1 exempt, dxy-safe variant: also require nNodes>=3? / nL split")
for dm in (2.0,1e9):
    for f in (1.0,1.5,2.0):
        rep("inLay1 ex nL5 dca<%.1f mX<%.1f"%(dm,f), ex&(nl==5)&(dca<dm)&(mX<f))
