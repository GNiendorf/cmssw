#!/usr/bin/env python3
"""Duplicate rate per displacement band on the full 977 (see displaced_dup_300.txt)."""
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
VB = [(0, 1), (1, 5), (5, 10), (10, 30), (30, 60), (60, 1e18)]
acc = {}
for v in ('vxy', 'dxy'):
    for b in VB: acc[(v,) + b] = [0, 0, 0, 0]
for evt in ea:
    if evt not in ei: continue
    ta.GetEntry(ea[evt]); ti.GetEntry(ei[evt])
    pt = list(ti.sim_pt); eta = list(ti.sim_eta); dxy = list(ti.sim_pca_dxy)
    vxy = list(ti.sim_vtxperp); vz = list(ti.sim_vz); q = list(ti.sim_q)
    n = len(pt)
    ok = [pt[s] > 0.9 and abs(eta[s]) < 4.5 and abs(vz[s]) < 30. and q[s] != 0 for s in range(n)]
    def bandof(s, v):
        val = abs(dxy[s]) if v == 'dxy' else vxy[s]
        for b in VB:
            if b[0] <= val < b[1]: return (v,) + b
        return None
    for tag, tree in (('p', ta), ('l', ti)):
        dup = list(tree.tc_isDuplicate)
        for k in range(len(dup)):
            sims = [int(x) for x in tree.tc_simIdxAll[k]]
            if not sims: continue
            s = sims[0]
            if s >= n or not ok[s]: continue
            for v in ('vxy', 'dxy'):
                b = bandof(s, v)
                if b is None: continue
                a = acc[b]
                if tag == 'p': a[0] += 1; a[1] += int(dup[k] != 0)
                else:          a[2] += 1; a[3] += int(dup[k] != 0)
print("DUPLICATE RATE AMONG MATCHED TCs PER DISPLACEMENT BAND -- FULL 977")
print("%-14s %9s %9s %9s   %9s %9s %9s   %9s" % ("band", "pTC", "pDup", "pDupRate", "lTC", "lDup", "lDupRate", "dRate"))
for k in acc:
    p, pd, l, ld = acc[k]
    if p == 0 and l == 0: continue
    pr = pd / p if p else 0.; lr = ld / l if l else 0.
    print("%-14s %9d %9d %9.5f   %9d %9d %9.5f   %+9.5f" % (
        "%s[%g,%s)" % (k[0], k[1], 'inf' if k[2] > 1e17 else '%g' % k[2]), p, pd, pr, l, ld, lr, pr - lr))
