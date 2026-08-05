#!/usr/bin/env python3
"""Offline simulator of OUTPUT-ONLY TC row removal.

Because dropping a TC row at the writer changes nothing upstream, the whole scoreboard
can be recomputed exactly from one ntuple.  Validated by reproducing the stored
tc_isFake / tc_isDuplicate / sim_tcIdx bookkeeping with an empty drop set.
"""
import sys, math, json, ROOT
from collections import defaultdict

PTCUT, ETACUT = 0.9, 4.5
VXY_BANDS = [(0.0,1.0),(1.0,5.0),(5.0,10.0),(10.0,30.0)]

def region(eta):
    a = abs(eta)
    return 'barrel' if a < 1.1 else ('transition' if a < 1.7 else 'endcap')

class Ev:
    __slots__=('spt','seta','svz','svx','svy','sq','sdxy','ty','et','pt','isf','isd',
               'sims','nh','nl','nb','nps','nn','inlay','nmd','dca','ch','br')

def load(path, nmax=-1):
    f = ROOT.TFile.Open(path); t = f.Get('tree')
    n = t.GetEntries() if nmax < 0 else min(nmax, t.GetEntries())
    evs=[]
    has = lambda b: t.GetBranch(b) is not None
    for i in range(n):
        t.GetEntry(i)
        e=Ev()
        e.spt=list(t.sim_pt); e.seta=list(t.sim_eta); e.svz=list(t.sim_vz)
        e.svx=list(t.sim_vx); e.svy=list(t.sim_vy); e.sq=list(t.sim_q)
        e.sdxy=list(t.sim_pca_dxy)
        e.ty=list(t.tc_type); e.et=list(t.tc_eta); e.pt=list(t.tc_pt)
        e.isf=list(t.tc_isFake); e.isd=list(t.tc_isDuplicate); e.nh=list(t.tc_nhitOT)
        e.sims=[list(x) for x in t.tc_simIdxAll]
        e.ch=list(t.tc_isChain) if has('tc_isChain') else [0]*len(e.ty)
        if has('tc_dbgNL'):
            e.br=list(t.tc_dbgBr)
            e.nl=list(t.tc_dbgNL); e.nb=list(t.tc_dbgNB); e.nps=list(t.tc_dbgNPS)
            e.nn=list(t.tc_dbgNN); e.inlay=list(t.tc_dbgInLay); e.nmd=list(t.tc_dbgNMD)
            e.dca=list(t.tc_dbgDca)
        else:
            z=[0]*len(e.ty); e.br=[-1]*len(e.ty); e.nl=z; e.nb=z; e.nps=z; e.nn=z; e.inlay=z; e.nmd=z; e.dca=[0.0]*len(e.ty)
        evs.append(e)
    return evs

def score(evs, drop=None):
    """drop(e, j) -> True to remove TC row j of event e."""
    efn=0; efd=0
    efn_r=defaultdict(int); efd_r=defaultdict(int)
    vn=defaultdict(int); vd=defaultdict(int)
    dn=defaultdict(int); dd=defaultdict(int)
    frn_pt=0; frd_pt=0; drn_pt=0; drd_pt=0
    frn_r=defaultdict(int); frd_r=defaultdict(int)
    drn_r=defaultdict(int); drd_r=defaultdict(int)
    olnum=defaultdict(float); olden=defaultdict(int); olnA=0.0; oldA=0
    ntc=0
    for e in evs:
        keep=[True]*len(e.ty)
        if drop is not None:
            for j in range(len(e.ty)):
                if drop(e,j): keep[j]=False
        cover=defaultdict(int)
        for j in range(len(e.ty)):
            if not keep[j]: continue
            for s in set(e.sims[j]): cover[s]+=1
        # efficiency
        for k in range(len(e.spt)):
            if e.sq[k]==0: continue
            if not (abs(e.svz[k])<30.0 and math.hypot(e.svx[k],e.svy[k])<2.5): continue
            ok = cover[k] > 0
            if e.spt[k]>PTCUT:
                efd+=1; efn+= 1 if ok else 0   # sum_all includes over/underflow: NO eta cut
                if abs(e.seta[k])<ETACUT:
                    r=region(e.seta[k]); efd_r[r]+=1; efn_r[r]+= 1 if ok else 0
                vxy=math.hypot(e.svx[k],e.svy[k])
                # note: vxy<2.5 already; bands beyond that are empty here
        # displaced bands need the full vtx selection RELAXED as compare_ab does
        for k in range(len(e.spt)):
            if e.sq[k]==0: continue
            if not (abs(e.seta[k])<ETACUT and e.spt[k]>PTCUT and abs(e.svz[k])<30.0): continue
            ok = cover[k] > 0
            vxy=math.hypot(e.svx[k],e.svy[k]); dxy=abs(e.sdxy[k])
            for lo,hi in VXY_BANDS:
                if lo<=vxy<hi: vd[(lo,hi)]+=1; vn[(lo,hi)]+= 1 if ok else 0
                if lo<=dxy<hi: dd[(lo,hi)]+=1; dn[(lo,hi)]+= 1 if ok else 0
        # fake / dup / length
        for j in range(len(e.ty)):
            if not keep[j]: continue
            ntc+=1
            fake = (len(e.sims[j])==0)
            dup = any(cover[s]>1 for s in set(e.sims[j]))
            if abs(e.et[j])<ETACUT:
                frd_pt+=1; frn_pt+= 1 if fake else 0
                drd_pt+=1; drn_pt+= 1 if dup else 0
            if e.pt[j]>PTCUT:
                r=region(e.et[j])
                frd_r[r]+=1; frn_r[r]+= 1 if fake else 0
                drd_r[r]+=1; drn_r[r]+= 1 if dup else 0
                olden[r]+=1; olnum[r]+=e.nh[j]; olnA+=e.nh[j]; oldA+=1
    m={}
    m['eff']=efn/efd
    for r in ('barrel','transition','endcap'):
        m['eff_'+r]=efn_r[r]/max(efd_r[r],1)
        m['fake_'+r]=frn_r[r]/max(frd_r[r],1)
        m['dup_'+r]=drn_r[r]/max(drd_r[r],1)
        m['nh_'+r]=olnum[r]/max(olden[r],1)
    for lo,hi in VXY_BANDS:
        m['vxy_%g_%g'%(lo,hi)]=vn[(lo,hi)]/max(vd[(lo,hi)],1)
        m['dxy_%g_%g'%(lo,hi)]=dn[(lo,hi)]/max(dd[(lo,hi)],1)
    m['fake']= (frn_r['barrel']+frn_r['transition']+frn_r['endcap'])/max(frd_r['barrel']+frd_r['transition']+frd_r['endcap'],1)
    m['dup_pt']=drn_pt/max(drd_pt,1)
    m['fake_pt']=frn_pt/max(frd_pt,1)
    m['dup']=(drn_r['barrel']+drn_r['transition']+drn_r['endcap'])/max(drd_r['barrel']+drd_r['transition']+drd_r['endcap'],1)
    m['nTC']=ntc
    m['nh']=olnA/max(oldA,1)
    return m

HDR=('eff','vxy_0_1','vxy_1_5','vxy_5_10','vxy_10_30','dxy_1_5','dxy_5_10','dxy_10_30',
     'dup_pt','fake_pt','dup','fake','nh_barrel','nh_transition','nh_endcap','nTC')
def line(tag,m):
    s="%-18s"%tag
    for k in HDR:
        s += ("%9d"%m[k]) if k=='nTC' else ("%9.5f"%m[k])
    return s
def header():
    s="%-18s"%"tag"
    for k in HDR: s+="%9s"%k.replace('vxy_','v').replace('dxy_','d').replace('_','')
    return s

if __name__=='__main__':
    evs=load(sys.argv[1])
    m=score(evs)
    print(header()); print(line('NODROP',m))
