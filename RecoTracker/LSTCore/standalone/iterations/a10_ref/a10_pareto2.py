#!/usr/bin/env python3
"""Displaced-CONSTRAINED Pareto. A point qualifies only if every displaced band is within
`tol` of the assembled baseline (FINBASE/GATE). Then Pareto on (eff up, dup down, fake down).
Usage: a10_pareto2.py [tol]   default tol = 0.004 (~1 track on the 285-denominator bands)."""
import glob, json, os, sys
B = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [B + d for d in ('a10_ref/', 'fin_ref/', 'xc_ref/', 'rebase_ref/')]
DISP = ['eff_vxy_1_5', 'eff_vxy_5_10', 'eff_vxy_10_30',
        'eff_dxy_1_5', 'eff_dxy_5_10', 'eff_dxy_10_30']
tol = float(sys.argv[1]) if len(sys.argv) > 1 else 0.004


def load(tag):
    for d in DIRS:
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            return json.load(open(p))['metrics']
    return None


base = load('GATE')
tags = sorted(os.path.basename(p)[2:-5] for p in glob.glob(B + 'a10_ref/r_*.json'))
tags = [t for t in tags if not t.startswith('W_')]
pts, rejected = [], []
for t in tags:
    m = load(t)
    if m is None or m['eff_overall_incut']['proto'] is None:
        continue
    worst = min(m[k]['proto'] - base[k]['proto'] for k in DISP)
    rec = (t, m['eff_overall_incut']['proto'], m['dup_overall_incut']['proto'],
           m['fake_overall_incut']['proto'], worst)
    (pts if worst >= -tol else rejected).append(rec)


def dom(a, b):
    return (a[1] >= b[1] and a[2] <= b[2] and a[3] <= b[3]
            and (a[1] > b[1] or a[2] < b[2] or a[3] < b[3]))


front = [a for a in pts if not any(dom(b, a) for b in pts)]
front.sort(key=lambda r: -r[1])
print('DISPLACED-SAFE PARETO (tol %.3f on every vxy/dxy band vs FINBASE): %d of %d'
      % (tol, len(front), len(pts)))
print('%-22s %8s %8s %8s %10s' % ('tag', 'eff', 'dup', 'fake', 'worstDisp'))
for r in front:
    print('%-22s %8.5f %8.5f %8.5f %+10.5f' % r)
print('\n(%d points excluded for a displaced band more than %.3f below FINBASE)'
      % (len(rejected), tol))
