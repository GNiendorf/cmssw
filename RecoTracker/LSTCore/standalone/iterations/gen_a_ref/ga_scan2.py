#!/usr/bin/env python3
"""GEN-A scan 2: (attach threshold) x (per-object T3 quality gate) x (ownership rule)."""
import sys, os, itertools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ga_lab import load, run_rule

EFF_OFF, FAKE_OFF = -0.009, +0.0015
GATE_EFF, GATE_FAKE = 0.81303, 0.047

nev = int(sys.argv[1]) if len(sys.argv) > 1 else None
evs = load(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'audit300c.txt'), nev)
print('events %d' % len(evs))
res = []
for th in [3.0, 4.0, 5.0, 6.0, 7.0]:
    for fsc in [1.0, 0.2, 0.1, 0.05, 0.02, 0.01]:
        for cc in [(False, 0, 'md'), (True, 3, 'md'), (True, 2, 'md'), (True, 1, 'md')]:
            for weld in [False, True]:
                kw = dict(theta=th, t3fakeMax=fsc, ccOn=cc[0], ccN=cc[1], ccGran=cc[2],
                          rdt=True, weld=weld)
                r = run_rule(evs, **kw)
                name = 'th%.1f fs%.3g %s%s%s' % (
                    th, fsc, ('ccN%d' % cc[1]) if cc[0] else 'nocc', ' W' if weld else '', '')
                res.append((r['effEst'] + EFF_OFF, r['fakeEst'] + FAKE_OFF, r, name))
res.sort(key=lambda x: -x[0])
print('%-24s %8s %8s %9s %9s %9s %8s' %
      ('config', 'cand/e', 'deliv/e', 'effReal~', 'fakeReal~', 'keptFake', 'dupAdd'))
shown = 0
for eff, fk, r, name in res:
    ok = 'PASS' if (eff >= GATE_EFF and fk <= GATE_FAKE) else ''
    if eff < 0.805 and not ok:
        continue
    print('%-24s %8.1f %8.1f %9.5f %9.5f %9.5f %8.5f %s'
          % (name, r['cand'], r['deliv'], eff, fk, r['keptFakeFrac'], r['dupAdd'], ok))
    shown += 1
    if shown > 60:
        break
