#!/usr/bin/env python3
"""GEN-B -- pT3-CLASS VALUE INSIDE OUR OWN PIPELINE.

For every named run output, restrict to the pT3-class rows (tc_type == 5 -- in a
-RT3 1 run those are exactly OUR bare-T3 deliveries, in P25BASE exactly LST's carried
pT3 rows) and report, against the SAME output's other rows:
  rows/evt, fake fraction, in-cut sim coverage, and UNIQUE in-cut sim coverage
  (sims that ONLY the pT3 class delivers -- the class's actual efficiency contribution).
Usage: gb_class.py <label>=<path.root> ...
"""
import sys
import ROOT

PTCUT = 0.9


def scan(path):
    f = ROOT.TFile.Open(path)
    t = f.Get('tree')
    n = t.GetEntries()
    rows = fakes = 0
    cov = uniq = 0
    incut = 0
    allrows = allfake = 0
    for i in range(n):
        t.GetEntry(i)
        ty = list(t.tc_type)
        fk = list(t.tc_isFake)
        sa = t.tc_simIdxAll
        spt = list(t.sim_pt)
        inc = set(s for s in range(len(spt)) if spt[s] > PTCUT)
        incut += len(inc)
        a, b = set(), set()
        for j in range(len(ty)):
            mine = (ty[j] == 5)
            allrows += 1
            allfake += 1 if fk[j] else 0
            if mine:
                rows += 1
                fakes += 1 if fk[j] else 0
            tgt = a if mine else b
            for s in sa[j]:
                if s in inc:
                    tgt.add(s)
        cov += len(a)
        uniq += len(a - b)
    f.Close()
    return dict(nev=n, rows=rows / n, fake=fakes / max(rows, 1), cov=cov / n,
                uniq=uniq / n, incut=incut / n, nTC=allrows / n, tcfake=allfake / allrows)


print('%-16s%9s%9s%9s%10s%10s%9s%9s' %
      ('run', 'rows/evt', 'fakefrc', 'cov/evt', 'UNIQ/evt', 'UNIQ/sims', 'nTC/evt', 'TCfake'))
for arg in sys.argv[1:]:
    lab, path = arg.split('=', 1)
    d = scan(path)
    print('%-16s%9.1f%9.4f%9.2f%10.2f%10.5f%9.1f%9.5f' %
          (lab, d['rows'], d['fake'], d['cov'], d['uniq'], d['uniq'] / d['incut'],
           d['nTC'], d['tcfake']))
print('  (in-cut sims/evt = %.1f)' % d['incut'])
