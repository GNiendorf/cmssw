#!/usr/bin/env python3
"""GEN-C Task 1 extraction. Builds ONE pickle holding, per event, the three candidate
sets to be compared, all in the SAME sim-row space and with the SAME truth rule.

SIM SPACE. Every *_simIdxAll branch (LST's and ours alike) indexes the FULL tracking
ntuple sim rows, not the LST ntuple's sim_* arrays. sim_trkNtupIdx maps LST sim row ->
full row, so it is inverted once per event and every match list is translated into LOCAL
sim rows. Sims outside the LST list are pileup rows that are not in the efficiency
denominator anyway, so they are dropped -- identically on both sides.

TRUTH RULE. A candidate matches a sim iff that sim is in the candidate's simIdxAll, i.e.
strictly more than 75% of the candidate's hits come from that sim -- the production
matcher on LST's side (write_lst_ntuple) and its verbatim port on ours (Matching.cc).

IN-CUT SIMS. The harness ef_denom_eta selection: pt > 0.9, |vz| < 30, |vperp| < 2.5,
q != 0 (pdgid == 0 set). Same on both sides.

Sets written:
  lst_pre   LST's COMPLETE pre-cleaning pT3 candidate set (pT3_* branches; LST's dup
            cleaning only sets flags, so nothing is missing here)
  lst_post  LST's DELIVERED pT3 rows (tc_type == 5 in the same input ntuple)
  ours      our bare-T3 deliveries with the stage-B logit, from a low-threshold run

Usage: gc_extract.py <our-run-tag> [out.pkl]
"""
import pickle
import sys
import ROOT

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
G = S + 'gen_c_ref/'

tag = sys.argv[1]
out = G + (sys.argv[2] if len(sys.argv) > 2 else 'task1_%s.pkl' % tag)

fin = ROOT.TFile.Open(S + 'LSTNtuple_PU200RelVal_300evt.root')
tin = fin.Get('tree')
fpr = ROOT.TFile.Open(G + 't_%s.root' % tag)
tpr = fpr.Get('tree')
nev = tin.GetEntries()
assert tpr.GetEntries() == nev, 'event count mismatch'

evts = []
for i in range(nev):
    tin.GetEntry(i)
    tpr.GetEntry(i)
    ntup = list(tin.sim_trkNtupIdx)
    g2l = {}
    for k, gidx in enumerate(ntup):
        g2l[gidx] = k
    spt = list(tin.sim_pt)
    seta = list(tin.sim_eta)
    sq = list(tin.sim_q)
    svx = list(tin.sim_vx)
    svy = list(tin.sim_vy)
    svz = list(tin.sim_vz)
    incut = [1 if (spt[k] > 0.9 and abs(svz[k]) < 30.0 and
                   (svx[k] * svx[k] + svy[k] * svy[k]) ** 0.5 < 2.5 and sq[k] != 0) else 0
             for k in range(len(spt))]

    def loc(v):
        r = []
        for s in v:
            k = g2l.get(s, -1)
            if k >= 0:
                r.append(k)
        return r

    # --- LST pre-cleaning pT3 candidates -------------------------------------------
    lst_pre = [(pt, eta, 1 if len(sa) == 0 else 0, loc(sa))
               for pt, eta, sa in zip(tin.pT3_pt, tin.pT3_eta, tin.pT3_simIdxAll)]
    # --- LST delivered pT3 rows (tc_type == 5) + everything else LST delivers ------
    ty = list(tin.tc_type)
    lst_post, lst_other = [], []
    for j in range(len(ty)):
        raw = list(tin.tc_simIdxAll[j])
        sa = loc(raw)
        if ty[j] == 5:
            lst_post.append((tin.tc_pt[j], tin.tc_eta[j], 1 if len(raw) == 0 else 0, sa))
        else:
            lst_other.append(sa)
    # --- our deliveries -------------------------------------------------------------
    ch = list(tpr.tc_isChain)
    ours, our_other = [], []
    for j in range(len(ch)):
        raw = list(tpr.tc_simIdxAll[j])
        sa = loc(raw)
        if ch[j] == 3:
            ours.append((tpr.tc_pt[j], tpr.tc_eta[j], tpr.tc_attLogit[j],
                         tpr.tc_t3Row[j], tpr.tc_plsRow[j], 1 if len(raw) == 0 else 0, sa))
        else:
            our_other.append(sa)
    evts.append(dict(sim_pt=spt, sim_eta=seta, incut=incut,
                     lst_pre=lst_pre, lst_post=lst_post, lst_other=lst_other,
                     ours=ours, our_other=our_other))
fin.Close()
fpr.Close()

n1 = sum(len(e['lst_pre']) for e in evts)
n2 = sum(len(e['lst_post']) for e in evts)
n3 = sum(len(e['ours']) for e in evts)
print('evts %d | LST pre %d (%.1f/evt) | LST post %d (%.1f/evt) | ours %d (%.1f/evt)'
      % (nev, n1, n1 / nev, n2, n2 / nev, n3, n3 / nev))
with open(out, 'wb') as fh:
    pickle.dump(dict(nev=nev, evts=evts), fh)
print('wrote', out)
