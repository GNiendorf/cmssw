#!/usr/bin/env python3
"""GEN-A scan 3 (full 300 evts): attach threshold x per-object T3 quality gate x
ownership rule x weld group. Lab->real calibration from ga_cal.py on the same 300 events:
  real_eff  = effEst  - 0.00266 (sd .00122)
  real_fake = fakeEst + 0.00290 (sd .00277)
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ga_lab import load, run_rule

EFF_OFF, FAKE_OFF = -0.00266, +0.00290
GATE_EFF, GATE_FAKE = 0.81303, 0.047
A = sys.argv[1] if len(sys.argv) > 1 else 'audit300c.txt'
evs = load(os.path.join(os.path.dirname(os.path.abspath(__file__)), A))
print('events %d  audit %s' % (len(evs), A))
res = []
for th in [2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0]:
    for fsc in [1.0, 0.2, 0.1, 0.05, 0.02, 0.01, 0.005]:
        for cc in [(False, 0), (True, 3), (True, 2), (True, 1)]:
            for weld in [False, True]:
                r = run_rule(evs, theta=th, t3fakeMax=fsc, ccOn=cc[0], ccN=cc[1],
                             ccGran='md', rdt=True, weld=weld)
                nm = 'th%-4.1f fs%-6.4g %-5s%s' % (th, fsc, ('ccN%d' % cc[1]) if cc[0] else 'nocc',
                                                   ' W' if weld else '')
                res.append((r['effEst'] + EFF_OFF, r['fakeEst'] + FAKE_OFF, r, nm))
res.sort(key=lambda x: -x[0])
print('%-24s %8s %8s %9s %9s %9s %8s' %
      ('config', 'cand/e', 'deliv/e', 'effReal~', 'fakeReal~', 'keptFake', 'dupAdd'))
n = 0
for eff, fk, r, nm in res:
    ok = 'PASS' if (eff >= GATE_EFF and fk <= GATE_FAKE) else ('effOK' if eff >= GATE_EFF else '')
    if not ok and n > 25:
        continue
    print('%-24s %8.1f %8.1f %9.5f %9.5f %9.5f %8.5f %s'
          % (nm, r['cand'], r['deliv'], eff, fk, r['keptFakeFrac'], r['dupAdd'], ok))
    n += 1
