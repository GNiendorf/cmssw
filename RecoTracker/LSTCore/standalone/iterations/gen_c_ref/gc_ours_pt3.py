#!/usr/bin/env python3
"""GEN-C Task 1, our side: extract OUR pT3-class delivery set (tc_isChain == 3) with the
stage-B attach logit attached, from one low-threshold run, into a pickle.

The stage-B decision is monotone in the margin, so re-thresholding this single dump on
tc_attLogit reproduces the delivery set of any higher -AT3 exactly (validated against the
real -AT3 6 row count in gc_task1.py).

Usage: gc_ours_pt3.py <tag> [outname]
"""
import pickle
import sys
import ROOT

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
G = S + 'gen_c_ref/'

tag = sys.argv[1]
out = G + (sys.argv[2] if len(sys.argv) > 2 else 'ours_pt3_%s.pkl' % tag)

f = ROOT.TFile.Open(G + 't_%s.root' % tag)
t = f.Get('tree')
nev = t.GetEntries()
evts = []
tot = 0
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
    ch = list(t.tc_isChain)
    pt = list(t.tc_pt)
    eta = list(t.tc_eta)
    lg = list(t.tc_attLogit)
    t3r = list(t.tc_t3Row)
    plr = list(t.tc_plsRow)
    sall = t.tc_simIdxAll
    rows = []
    other = []          # every NON-pT3-class row: (simsets) for the "unique to the class" test
    for j, c in enumerate(ch):
        sims = list(sall[j])
        if c == 3:
            rows.append((pt[j], eta[j], lg[j], t3r[j], plr[j], sims))
        else:
            other.append(sims)
    tot += len(rows)
    evts.append(dict(rows=rows, other=other, sim_pt=spt, sim_eta=seta, incut=incut))
f.Close()
print('%s: %d pT3-class rows, %.1f/evt over %d evts' % (tag, tot, tot / float(nev), nev))
with open(out, 'wb') as fh:
    pickle.dump(dict(nev=nev, evts=evts), fh)
print('wrote', out)
