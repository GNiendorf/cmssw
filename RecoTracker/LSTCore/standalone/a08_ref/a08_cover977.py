#!/usr/bin/env python3
"""Cover multiplicity and pixel-cover fraction per displacement band, full 977."""
from collections import defaultdict
import ROOT
ROOT.gROOT.SetBatch(True); ROOT.gErrorIgnoreLevel = ROOT.kError
S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
fi = ROOT.TFile.Open(S + 'rebase_ref/LSTNtuple_instr_977evt.root'); ti = fi.Get('tree')
def idx(t):
    t.SetBranchStatus('*', 0); t.SetBranchStatus('evt', 1); m = {}
    for i in range(t.GetEntries()):
        t.GetEntry(i); m[int(t.evt)] = i
    t.SetBranchStatus('*', 1); return m
ei = idx(ti)
VB = [(0, 1), (1, 5), (5, 10), (10, 30), (30, 60)]
PIX = {7, 5, 8}
print("977 evts -- cover multiplicity per displacement band (cover = TC with >0.75 match)")
print("%-9s %-12s %10s %10s %12s" % ("tag", "band", "matchedSim", "meanCovers", "fracWithPix"))
for tag, path in [('W_X4', 'fin_ref/r_W_X4.root'), ('LST', 'rebase_ref/LSTNtuple_instr_977evt.root')]:
    f = ROOT.TFile.Open(S + path); t = f.Get('tree'); e = idx(t)
    acc = {b: [0, 0, 0] for b in VB}
    for evt in e:
        if evt not in ei: continue
        t.GetEntry(e[evt]); ti.GetEntry(ei[evt])
        pt = list(ti.sim_pt); eta = list(ti.sim_eta); vxy = list(ti.sim_vtxperp)
        vz = list(ti.sim_vz); q = list(ti.sim_q); n = len(pt)
        ok = [pt[s] > 0.9 and abs(eta[s]) < 4.5 and abs(vz[s]) < 30. and q[s] != 0 for s in range(n)]
        ty = list(t.tc_type)
        cov = defaultdict(list)
        for k in range(len(ty)):
            for x in t.tc_simIdxAll[k]:
                cov[int(x)].append(ty[k])
        for s, tl in cov.items():
            if s >= n or not ok[s]: continue
            for b in VB:
                if b[0] <= vxy[s] < b[1]:
                    a = acc[b]; a[0] += 1; a[1] += len(tl); a[2] += int(any(x in PIX for x in tl)); break
    for b in VB:
        c = acc[b]
        if not c[0]: continue
        print("%-9s %-12s %10d %10.3f %12.4f" % (tag, "vxy[%g,%g)" % b, c[0], c[1] / c[0], c[2] / c[0]))
    f.Close()
