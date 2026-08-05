#!/usr/bin/env python3
"""GEN-C Task 1, LST side: characterise LST's COMPLETE pre-cleaning pT3 candidate set
straight out of the input ntuple (pT3_* branches; dup cleaning only sets flags there).

Emits a pickle with, per event:
  * per-pT3-row: isFake, pt, eta, list of matched sim rows (simIdxAll)
  * per-sim: pt, eta, in-cut flag (harness ef_denom_eta selection:
    pt > 0.9, |vz| < 30, |vperp| < 2.5, q != 0)
so the same offline machinery can score LST and us identically.
"""
import pickle
import sys
import ROOT

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
OUT = S + 'gen_c_ref/lst_pt3_set.pkl'

f = ROOT.TFile.Open(S + 'LSTNtuple_PU200RelVal_300evt.root')
t = f.Get('tree')
nev = t.GetEntries()

evts = []
tot = 0
fake = 0
nomatch = 0
for i in range(nev):
    t.GetEntry(i)
    spt = list(t.sim_pt)
    seta = list(t.sim_eta)
    sq = list(t.sim_q)
    svx = list(t.sim_vx)
    svy = list(t.sim_vy)
    svz = list(t.sim_vz)
    incut = [1 if (spt[k] > 0.9 and abs(svz[k]) < 30.0 and
                   (svx[k] ** 2 + svy[k] ** 2) ** 0.5 < 2.5 and sq[k] != 0) else 0
             for k in range(len(spt))]
    pt = list(t.pT3_pt)
    eta = list(t.pT3_eta)
    isf = list(t.pT3_isFake)
    sall = [list(v) for v in t.pT3_simIdxAll]
    tot += len(pt)
    fake += sum(1 for x in isf if x)
    nomatch += sum(1 for v in sall if len(v) == 0)
    evts.append(dict(pt=pt, eta=eta, isFake=isf, simAll=sall,
                     sim_pt=spt, sim_eta=seta, incut=incut))
f.Close()

print('LST pT3 rows  total %d  per evt %.1f' % (tot, tot / float(nev)))
print('  isFake==1     %d (%.4f)' % (fake, fake / float(tot)))
print('  simIdxAll==[] %d (%.4f)   <- consistency check vs isFake' % (nomatch, nomatch / float(tot)))
with open(OUT, 'wb') as fh:
    pickle.dump(dict(nev=nev, evts=evts), fh)
print('wrote', OUT)
