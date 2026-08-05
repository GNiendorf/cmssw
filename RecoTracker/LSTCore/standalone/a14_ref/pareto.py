#!/usr/bin/env python3
"""Is a regional point DOMINATED by some GLOBAL two-knob setting?

judge.py compares against the one-dimensional -XCT frontier at fixed -AT3 6, which is the
right test for a point that only moves duplicate rate. A point that also moves the fake
rate has to clear a harder bar: the whole measured global (-AT3, -XCT) grid. This reports,
for each tag, whether any global grid point is at least as good on ALL THREE headline
metrics (eff >=, dup <=, fake <=) within a tolerance, and the nearest global competitors.

Global grid: fin_ref STATUS section D, frozen 300 evts, -CCN 1 -CCR 2.

Usage: pareto.py <tag> [<tag> ...]
"""
import json
import os
import sys

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [S + d + '/' for d in ('a14_ref', 'fin_ref', 'xc_ref', 'rebase_ref')]

# (label, eff, dup, fake)
GRID = [
    ('AT3 6  XCT 4.5', .81037, .06675, .05539), ('AT3 6  XCT 4', .80992, .06230, .05551),
    ('AT3 6  XCT 3.5', .80904, .05925, .05558), ('AT3 6  XCT 3', .80776, .05689, .05562),
    ('AT3 6.5 XCT 4.5', .81010, .06802, .05169), ('AT3 6.5 XCT 4', .80966, .06341, .05181),
    ('AT3 6.5 XCT 3.5', .80873, .06024, .05187),
    ('AT3 7  XCT 5.25', .81076, .08158, .04934), ('AT3 7  XCT 5', .81045, .07643, .04944),
    ('AT3 7  XCT 4.75', .80992, .07259, .04952), ('AT3 7  XCT 4.5', .80944, .06927, .04959),
    ('AT3 7  XCT 4', .80900, .06452, .04970), ('AT3 7  XCT 3.5', .80807, .06124, .04975),
    ('AT3 7  XCT 3', .80674, .05875, .04977), ('AT3 7  XCT 2', .80312, .05485, .04979),
    ('AT3 7.5 XCT 4', .80710, .06573, .04833), ('AT3 7.5 XCT 3.5', .80612, .06231, .04838),
    ('AT3 8  XCT 4', .80520, .06704, .04759), ('AT3 8  XCT 3.5', .80418, .06348, .04763),
    ('AT3 9  XCT 4', .80210, .06956, .04717),
]
TOL = 5e-5  # a fifth of the smallest move anyone quotes; below this call it a tie


def load(tag):
    for d in DIRS:
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            return json.load(open(p))['metrics']
    return None


for t in sys.argv[1:]:
    m = load(t)
    if m is None:
        print('%-12s (no json yet)' % t)
        continue
    e = m['eff_overall_incut']['proto']
    d = m['dup_overall_incut']['proto']
    k = m['fake_overall_incut']['proto']
    dom = [g for g in GRID if g[1] >= e - TOL and g[2] <= d + TOL and g[3] <= k + TOL]
    print('%-12s eff %.5f dup %.5f fake %.5f' % (t, e, d, k))
    if dom:
        print('   DOMINATED by %d global point(s):' % len(dom))
        for g in dom:
            print('     %-18s eff %+.5f  dup %+.5f  fake %+.5f' % (g[0], g[1] - e, g[2] - d, g[3] - k))
    else:
        print('   NOT DOMINATED by any measured global point. Closest global competitors')
        print('   (a global point beats this one on a metric where its delta is favourable):')
        near = sorted(GRID, key=lambda g: abs(g[2] - d))[:3]
        for g in near:
            print('     %-18s eff %+.5f  dup %+.5f  fake %+.5f' % (g[0], g[1] - e, g[2] - d, g[3] - k))
    print()
