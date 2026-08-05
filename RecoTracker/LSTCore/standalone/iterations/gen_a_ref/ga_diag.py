#!/usr/bin/env python3
"""GEN-A diagnostics on the candidate pool: where do the NEW sims live, where do the
fakes live, and what per-object quantity separates them."""
import sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ga_lab import load

evs = load(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'audit300c.txt'),
           int(sys.argv[1]) if len(sys.argv) > 1 else None)
print('events %d' % len(evs))

# per-candidate: does it cover an in-cut sim that the pre-stage-B universe misses?
rows = []
for e in evs:
    for o in e.O:
        lg, pt, eta, isFake, sims, mds, pix, nClMd, nClHit, grp = o[:10]
        fs, ps, ds = o[10], o[11], o[12]
        newsim = any(s < len(e.incut) and e.incut[s] and s not in e.already for s in sims)
        anyinc = any(s < len(e.incut) and e.incut[s] for s in sims)
        rows.append((lg, isFake, newsim, anyinc, nClMd, nClHit, fs, ps + ds, grp >= 0, pt))
R = np.array([(r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9]) for r in rows],
             dtype=np.float64)
lg, isF, isNew, isInc, nClMd, nClHit, fs, good, hasG, pt = R.T
print('candidates %d  fake %.4f  covers-new %.4f  covers-incut %.4f'
      % (len(lg), isF.mean(), isNew.mean(), isInc.mean()))

print('\n-- nClMd distribution (pre-claimed MDs of 3) --')
print('%6s %9s %9s %9s %9s' % ('nClMd', 'frac', 'fakeFrac', 'newFrac', 'nNew'))
for k in range(4):
    m = nClMd == k
    if m.sum() == 0: continue
    print('%6d %9.4f %9.4f %9.4f %9d' % (k, m.mean(), isF[m].mean(), isNew[m].mean(), isNew[m].sum()))

print('\n-- t3_fakeScore separation (over ALL candidates) --')
print('%8s %9s %9s %9s %9s %9s' % ('cut', 'keepFrac', 'fakeFrac', 'newKept', 'newFrac', 'candFake'))
tot_new = isNew.sum()
for c in [1.0, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01, 0.005, 0.002, 0.001]:
    m = fs <= c
    if m.sum() == 0: continue
    print('%8.4g %9.4f %9.4f %9d %9.4f %9.4f'
          % (c, m.mean(), isF[m].mean(), int(isNew[m].sum()), isNew[m].sum() / tot_new, isF[m].mean()))

print('\n-- t3 prompt+displaced separation --')
for c in [0.0, 0.2, 0.5, 0.8, 0.9, 0.95, 0.99]:
    m = good >= c
    if m.sum() == 0: continue
    print('good>=%.3g keep %.4f fake %.4f newKept %d (%.4f)'
          % (c, m.mean(), isF[m].mean(), int(isNew[m].sum()), isNew[m].sum() / tot_new))

print('\n-- joint: logit threshold x fakeScore cut, on NEW-sim yield and purity --')
print('%6s %8s %9s %9s %9s' % ('theta', 'fsCut', 'cand/e', 'fake', 'nNew'))
for th in [4.0, 5.0, 6.0, 7.0, 8.0]:
    for c in [1.0, 0.1, 0.02, 0.005]:
        m = (lg >= th) & (fs <= c)
        if m.sum() == 0: continue
        print('%6.1f %8.4g %9.1f %9.4f %9d' % (th, c, m.sum() / len(evs), isF[m].mean(), int(isNew[m].sum())))

print('\n-- weld group coverage --')
print('candidates with a welded-chain group: %.4f' % hasG.mean())
