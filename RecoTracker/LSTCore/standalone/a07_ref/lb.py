#!/usr/bin/env python3
"""Load-bearing fake decomposition.

For every TC row classify:
  FAKE      tc_isFake                    -> removing it strictly helps fake rate
  SOLE      true AND is the ONLY TC covering some in-denominator sim
                                          -> removing it costs efficiency
  REDUND    true but every in-denom sim it touches has another TC
                                          -> removing it is FREE for eff (and helps dup)
Cells are (tc_type, region) and optionally a structural key.
"""
import sys, math, ROOT
from collections import defaultdict

def region(eta):
    a = abs(eta)
    return 'B' if a < 1.1 else ('T' if a < 1.7 else 'E')

PTCUT, ETACUT = 0.9, 4.5

def analyze(path, label, keyfn=None, keyname='key'):
    f = ROOT.TFile.Open(path); t = f.Get('tree')
    nev = t.GetEntries()
    den = defaultdict(int); fk = defaultdict(int); sole = defaultdict(int); red = defaultdict(int)
    totfk = 0; totden = 0; totsole = 0; totred = 0
    for i in range(nev):
        t.GetEntry(i)
        spt = list(t.sim_pt); seta = list(t.sim_eta); svz = list(t.sim_vz)
        svx = list(t.sim_vx); svy = list(t.sim_vy); sq = list(t.sim_q)
        indenom = [ (spt[k] > PTCUT and abs(seta[k]) < ETACUT and abs(svz[k]) < 30.0
                     and math.hypot(svx[k], svy[k]) < 2.5 and sq[k] != 0)
                    for k in range(len(spt)) ]
        ty = list(t.tc_type); et = list(t.tc_eta); pt = list(t.tc_pt); isf = list(t.tc_isFake)
        sall = t.tc_simIdxAll
        nrows = len(ty)
        # count TCs per in-denom sim
        cover = defaultdict(int)
        rowsims = []
        for j in range(nrows):
            ss = [s for s in sall[j] if 0 <= s < len(spt) and indenom[s]]
            rowsims.append(ss)
            for s in set(ss): cover[s] += 1
        extra = None
        if keyfn is not None:
            extra = keyfn(t)
        for j in range(nrows):
            if pt[j] <= PTCUT: continue
            k = (ty[j], region(et[j])) if extra is None else (ty[j], region(et[j]), extra[j])
            den[k] += 1; totden += 1
            if isf[j]:
                fk[k] += 1; totfk += 1
            else:
                if any(cover[s] == 1 for s in set(rowsims[j])):
                    sole[k] += 1; totsole += 1
                else:
                    red[k] += 1; totred += 1
    return dict(nev=nev, den=den, fk=fk, sole=sole, red=red,
                totden=totden, totfk=totfk, totsole=totsole, totred=totred)

def report(r, label):
    nev = r['nev']; D = r['totden']; F = r['totfk']
    print("=== %s : %d evts  nTC(pt>0.9)=%d  fake=%d (%.5f)  sole=%d  redundant=%d ===" %
          (label, nev, D, F, F/D, r['totsole'], r['totred']))
    print("%-16s %9s %9s %9s %9s %9s %9s %10s" %
          ("cell", "den", "fake", "sole", "redund", "fakeFrac", "freeFrac", "dFakeIfCut"))
    keys = sorted(r['den'].keys(), key=lambda k: -(r['fk'][k]+r['red'][k]))
    for k in keys:
        d = r['den'][k]; a = r['fk'][k]; s = r['sole'][k]; e = r['red'][k]
        # removing all FAKE+REDUNDANT rows of this cell
        nf = F - a; nd = D - a - e
        print("%-16s %9d %9d %9d %9d %9.4f %9.4f %+10.5f" %
              (str(k), d, a, s, e, a/max(d,1), (a+e)/max(d,1), nf/nd - F/D))

if __name__ == '__main__':
    r = analyze(sys.argv[1], sys.argv[1])
    report(r, sys.argv[1])
