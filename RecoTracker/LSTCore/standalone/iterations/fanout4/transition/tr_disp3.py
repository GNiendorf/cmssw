#!/usr/bin/env python3
import sys, numpy as np, uproot
t = uproot.open(sys.argv[1])["tree"]
a = t.arrays(["tc_pt","tc_eta","tc_isFake","tc_isChain","tc_dbgBr","tc_dbgNL","tc_dbgNN",
              "tc_dbgInLay","tc_dbgMP","tc_dbgMD","tc_simIdxAll","sim_vx","sim_vy","sim_pca_dxy"],library="np")
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
              np.asarray(a["tc_dbgMD"][i]),sk,mv,md))
pt,eta,fk,ich,br,nl,nn,il,mp,mdm,sk,mv,md=[np.concatenate(x) for x in zip(*E)]
fk=fk.astype(bool);ch=ich>0;mX=np.maximum(mp,mdm);pt9=pt>0.9
band=pt9&ch&(np.abs(eta)>=1.1)&(np.abs(eta)<1.7)
ball=pt9&(np.abs(eta)>=1.1)&(np.abs(eta)<1.7)
bf,bn=fk[ball].sum(),ball.sum();gf,gn=fk[pt9].sum(),pt9.sum()
DS={"vxy>=5":mv>=5,"vxy>=10":mv>=10,"dxy>=1":md>=1,"dxy>=5":md>=5}
def risk(kill,d):
    v=set(sk[kill&~fk&d].tolist())-{-1};s=set(sk[pt9&~fk&~kill&d].tolist());return len(v-s)
def rep(name,kill):
    kf,kt=(kill&fk).sum(),(kill&~fk).sum()
    print("  %-44s %5d %5d | %.4f %.4f | %s"%(name,kf,kt,(bf-kf)/(bn-kill.sum()),
          (gf-kf)/(gn-kill.sum())," ".join("%7d"%risk(kill,d) for d in DS.values())))
print("  %-44s killF killT | bandFk globFk | %s"%("predicate"," ".join("%7s"%k for k in DS)))
print("-- calibration: the z1 config (flat exempt mX < -0.5, both lengths)")
rep("exempt(5+) mX<-0.50   [= z1 real]", band&(br==3)&(mX<-0.5))
rep("exempt(5+) mX<0.00    [= z2 real]", band&(br==3)&(mX<0.0))
print("-- inLay==1 restricted, exempt nL=5")
for f in (0.5,1.0,1.5,2.0,2.5,3.0):
    rep("ex5 & inLay1 mX<%.2f"%f, band&(br==3)&(nl==5)&(il==1)&(mX<f))
print("-- inLay==1 restricted, exempt all lengths")
for f in (0.5,1.0,1.5,2.0,2.5):
    rep("ex5+6 & inLay1 mX<%.2f"%f, band&(br==3)&(il==1)&(mX<f))
print("-- inLay==1, exempt, per-length combos")
for f5 in (1.0,1.5,2.0):
    for f6 in (0.0,0.5,1.0):
        rep("inLay1 ex5 mX<%.1f + ex6 mX<%.1f"%(f5,f6),
            band&(br==3)&(il==1)&(((nl==5)&(mX<f5))|((nl>=6)&(mX<f6))))
print("-- add band cell (nNodes=2,nL=5) mD tightening on inLay1")
for f5 in (1.0,1.5):
    for cd in (-1.0,0.0):
        k=(band&(br==3)&(il==1)&(nl==5)&(mX<f5))|(band&(il==1)&(nn==2)&(nl==5)&(mp<2.0)&(mdm<cd))
        rep("inLay1 ex5 mX<%.1f + cell mD<%.1f"%(f5,cd),k)
