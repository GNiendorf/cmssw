#!/usr/bin/env python3
"""One pass over the 977 for the two remaining displaced questions:
(1) the vxy > 30 tail, finely binned (the region the round scoreboard drops as overflow);
(2) the d1030 loss profile under the EXACT createPerfNumDenHists selection.
"""
import math
from collections import Counter
import ROOT
ROOT.gROOT.SetBatch(True); ROOT.gErrorIgnoreLevel = ROOT.kError
S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
fa = ROOT.TFile.Open(S + 'fin_ref/r_W_X4.root'); ta = fa.Get('tree')
fi = ROOT.TFile.Open(S + 'rebase_ref/LSTNtuple_instr_977evt.root'); ti = fi.Get('tree')
def idx(t):
    t.SetBranchStatus('*', 0); t.SetBranchStatus('evt', 1); m = {}
    for i in range(t.GetEntries()):
        t.GetEntry(i); m[int(t.evt)] = i
    t.SetBranchStatus('*', 1); return m
ea, ei = idx(ta), idx(ti)
TAIL = [(30, 40), (40, 50), (50, 60), (60, 80), (80, 1e18)]
acc = {b: [0, 0, 0] for b in TAIL}
TN = {4: 'T5', 9: 'T4', 7: 'pT5', 5: 'pT3', 8: 'pLS'}
lost_t, lost_len, lost_pdg, lost_reg = Counter(), Counter(), Counter(), Counter()
lv, ld, lpt, leta = [], [], [], []
nlost = ngain = 0
for evt in ea:
    if evt not in ei: continue
    ta.GetEntry(ea[evt]); ti.GetEntry(ei[evt])
    sa = list(ta.sim_tcIdx); sl = list(ti.sim_tcIdx)
    if len(sa) != len(sl): continue
    pt = list(ti.sim_pt); eta = list(ti.sim_eta); dxy = list(ti.sim_pca_dxy)
    vxy = list(ti.sim_vtxperp); vz = list(ti.sim_vz); q = list(ti.sim_q)
    pdg = list(ti.sim_pdgId); lt = list(ti.tc_type); ll = list(ti.tc_nhitOT)
    for s in range(len(sl)):
        ae = abs(eta[s])
        if not (pt[s] > 0.9 and ae < 4.5 and abs(vz[s]) < 30. and q[s] != 0): continue
        for b in TAIL:
            if b[0] <= vxy[s] < b[1]:
                a = acc[b]; a[0] += sa[s] >= 0; a[1] += sl[s] >= 0; a[2] += 1; break
        if 10.0 <= abs(dxy[s]) < 30.0:
            if sl[s] >= 0 and sa[s] < 0:
                nlost += 1; lost_t[lt[sl[s]]] += 1; lost_len[ll[sl[s]]] += 1
                lost_pdg[abs(pdg[s])] += 1
                lost_reg['barrel' if ae < 1.1 else ('trans' if ae < 1.7 else 'endcap')] += 1
                lv.append(vxy[s]); ld.append(abs(dxy[s])); lpt.append(pt[s]); leta.append(ae)
            elif sa[s] >= 0 and sl[s] < 0:
                ngain += 1
print("977 evts -- vxy TAIL (histogram overflow region, exact eff selection)")
print("%-14s %8s %8s %8s %9s %9s %8s" % ("band", "proto", "LST", "denom", "dTracks", "drate", "n_sigma"))
tp = tl = td = 0
for b in TAIL:
    p, l, d = acc[b]
    if d == 0: continue
    pr, lr = p / d, l / d
    sg = math.sqrt(pr * (1 - pr) / d + lr * (1 - lr) / d)
    print("vxy[%g,%s) %8.5f %8.5f %8d %+9d %+9.5f %+8.2f" % (b[0], 'inf' if b[1] > 1e17 else '%g' % b[1],
          pr, lr, d, p - l, pr - lr, (pr - lr) / sg if sg > 0 else 0))
    if b[1] <= 60: tp += p; tl += l; td += d
pr, lr = tp / td, tl / td
sg = math.sqrt(pr * (1 - pr) / td + lr * (1 - lr) / td)
print("vxy[30,60)  %8.5f %8.5f %8d %+9d %+9.5f %+8.2f  <- the reconstructable tail" %
      (pr, lr, td, tp - tl, pr - lr, (pr - lr) / sg))
def qt(v):
    v = sorted(v); n = len(v)
    return "min=%.2f q25=%.2f med=%.2f q75=%.2f max=%.2f" % (v[0], v[n//4], v[n//2], v[3*n//4], v[-1]) if v else "n/a"
print("\n977 evts -- dxy[10,30) LOSSES, exact selection: LOST=%d GAINED=%d net=%+d" % (nlost, ngain, ngain - nlost))
print("  LST matched-TC type: " + " ".join("%s=%d" % (TN.get(k, k), v) for k, v in lost_t.most_common()))
print("  LST matched nhitOT : " + " ".join("%d:%d" % (k, v) for k, v in sorted(lost_len.items())))
print("  eta region         : " + " ".join("%s=%d" % (k, v) for k, v in lost_reg.most_common()))
print("  |pdgId|            : " + " ".join("%d:%d" % (k, v) for k, v in lost_pdg.most_common(6)))
print("  pt   : " + qt(lpt)); print("  |eta|: " + qt(leta))
print("  vxy  : " + qt(lv)); print("  |dxy|: " + qt(ld))
