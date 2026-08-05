#!/usr/bin/env python3
"""GEN-A: re-derive the lab->real calibration on the FULL 300 events."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ga_lab import load, run_rule
A = sys.argv[1] if len(sys.argv) > 1 else 'audit300c.txt'
evs = load(os.path.join(os.path.dirname(os.path.abspath(__file__)), A))
base = sum(len(e.already) for e in evs)
print('events %d  base cov %d  effBase %.5f  nPreTC/evt %.1f  preFake %.5f'
      % (len(evs), base, base / 22784.0, sum(e.nPreTC for e in evs) / len(evs),
         sum(e.nPreFake for e in evs) / sum(e.nPreTC for e in evs)))
REAL = {  # (eff, dup, fake, deliv) measured in C++ by the prior agent, 300 evts
  'r_off':   (.77432, .05254, .04905, 0.0),
  'd_none':  (.81505, .53862, .06186, 1132.7),
  'd_px':    (.81404, .28287, .06672, 466.7),
  'd_ot':    (.80894, .13360, .06850, 257.8),
  'd_both':  (.80815, .07151, .06396, 181.5),
  'd_n1':    (.80456, .04682, .05649, 135.5),
  'd_otn1':  (.80504, .05241, .05906, 146.2),
  'd_nopre': (.81390, .27891, .06599, 455.8),
  'a8_n1':   (.79380, .05072, .04882, 56.9),
  'a8_n2':   (.79578, .06733, .04943, 75.9),
}
CFG = {
  'd_none':  dict(theta=6.0, ccOn=False, rdt=False),
  'd_px':    dict(theta=6.0, ccOn=False, rdt=True),
  'd_ot':    dict(theta=6.0, ccOn=True, ccN=2, rdt=False),
  'd_both':  dict(theta=6.0, ccOn=True, ccN=2, rdt=True),
  'd_n1':    dict(theta=6.0, ccOn=True, ccN=1, rdt=True),
  'd_otn1':  dict(theta=6.0, ccOn=True, ccN=1, rdt=False),
  'd_nopre': dict(theta=6.0, ccOn=True, ccN=2, rdt=True, preclaim=False),
  'a8_n1':   dict(theta=8.0, ccOn=True, ccN=1, rdt=True),
  'a8_n2':   dict(theta=8.0, ccOn=True, ccN=2, rdt=True),
}
print('%-10s %8s %8s %9s %9s %9s %9s' %
      ('tag', 'delivR', 'delivL', 'effReal', 'effEst', 'fakeReal', 'fakeEst'))
de, df = [], []
for k, kw in CFG.items():
    r = run_rule(evs, **kw)
    e0, d0, f0, dv = REAL[k]
    de.append(e0 - r['effEst']); df.append(f0 - r['fakeEst'])
    print('%-10s %8.1f %8.1f %9.5f %9.5f %9.5f %9.5f' %
          (k, dv, r['deliv'], e0, r['effEst'], f0, r['fakeEst']))
import statistics as st
print('EFF offset  mean %+.5f  sd %.5f  (real = est + offset)' % (st.mean(de), st.pstdev(de)))
print('FAKE offset mean %+.5f  sd %.5f' % (st.mean(df), st.pstdev(df)))
