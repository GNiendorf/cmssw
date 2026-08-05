#!/usr/bin/env python3
"""Fake decomposition by TC type x region for a chainproto output ntuple.
Reproduces compare_ab's fake_overall (denom |eta|<4.5, all pt) and the per-region
numbers (denom |eta| in band AND pt>0.9)."""
import sys, ROOT, array
from collections import defaultdict

def region(eta):
    a = abs(eta)
    if a < 1.1: return 'B'
    if a < 1.7: return 'T'
    return 'E'

def run(path, label, typebr='tc_type'):
    f = ROOT.TFile.Open(path)
    t = f.Get('tree')
    t.SetBranchStatus('*', 0)
    for b in ('tc_type','tc_eta','tc_pt','tc_isFake','tc_isDuplicate','tc_nhitOT'):
        if t.GetBranch(b): t.SetBranchStatus(b, 1)
    nev = t.GetEntries()
    # overall: denom |eta|<4.5
    den = defaultdict(float); num = defaultdict(float)
    # region: pt>0.9
    rden = defaultdict(float); rnum = defaultdict(float)
    dupn = defaultdict(float)
    for i in range(nev):
        t.GetEntry(i)
        ty = list(t.tc_type); et = list(t.tc_eta); pt = list(t.tc_pt)
        fk = list(t.tc_isFake); dp = list(t.tc_isDuplicate)
        for j in range(len(ty)):
            if abs(et[j]) < 4.5:
                den[ty[j]] += 1; den['ALL'] += 1
                if fk[j]: num[ty[j]] += 1; num['ALL'] += 1
                if dp[j]: dupn[ty[j]] += 1; dupn['ALL'] += 1
            if pt[j] > 0.9:
                r = region(et[j])
                rden[(ty[j], r)] += 1; rden[('ALL', r)] += 1
                if fk[j]: rnum[(ty[j], r)] += 1; rnum[('ALL', r)] += 1
    print("=== %s (%d evts) ===" % (label, nev))
    print("OVERALL (denom |eta|<4.5, all pt)  nTC=%d  fakeRate=%.5f" % (den['ALL'], num['ALL']/den['ALL']))
    print("%-6s %10s %10s %8s %10s %10s" % ("type","nTC","nFake","fakeFrac","perEvtTC","perEvtFk"))
    for k in sorted([k for k in den if k != 'ALL']):
        print("%-6s %10d %10d %8.5f %10.2f %10.2f" % (k, den[k], num[k], num[k]/max(den[k],1), den[k]/nev, num[k]/nev))
    print("%-6s %10d %10d %8.5f %10.2f %10.2f" % ('ALL', den['ALL'], num['ALL'], num['ALL']/den['ALL'], den['ALL']/nev, num['ALL']/nev))
    print()
    print("PER REGION (pt>0.9): fake counts / denom / rate")
    types = sorted([k for k in den if k != 'ALL']) + ['ALL']
    hdr = "%-6s" % "type"
    for r in ('B','T','E'): hdr += "%12s" % ("den_"+r) + "%12s" % ("num_"+r) + "%9s" % ("fr_"+r)
    print(hdr)
    for k in types:
        line = "%-6s" % k
        for r in ('B','T','E'):
            d = rden[(k,r)]; n = rnum[(k,r)]
            line += "%12d%12d%9.5f" % (d, n, n/max(d,1))
        print(line)
    print()
    return den, num, rden, rnum, nev

if __name__ == '__main__':
    for p in sys.argv[1:]:
        lbl = p.split('/')[-1]
        run(p, lbl)
