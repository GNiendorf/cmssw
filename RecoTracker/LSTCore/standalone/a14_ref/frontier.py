#!/usr/bin/env python3
"""Does a per-region calibration point beat the GLOBAL -XCT frontier?

The global frontier (fin_ref, assembled baseline, -AT3 6, 300 evts) is the curve a single
threshold traces. A regional point only EARNS its extra constants if it lands above that
curve: more efficiency at the same duplicate rate. Prints, per tag, the efficiency the
global frontier would give at the SAME duplicate rate (piecewise-linear in dup) and the
excess.

Usage: frontier.py <tag> [<tag> ...]
"""
import json, os, sys

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [S + d + '/' for d in ('a14_ref', 'fin_ref', 'xc_ref', 'rebase_ref')]

# (-XCT, eff, dup, fake) at -AT3 6 -CCN 1 -CCR 2, 300 evts (fin_ref STATUS section D).
GLOBAL = [(4.5, 0.81037, 0.06675, 0.05539),
          (4.0, 0.80992, 0.06230, 0.05551),
          (3.5, 0.80904, 0.05925, 0.05558),
          (3.0, 0.80776, 0.05689, 0.05562)]


def load(tag):
    for d in DIRS:
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            return json.load(open(p))['metrics']
    raise SystemExit('no json for tag ' + tag)


def frontier_eff(dup):
    pts = sorted([(g[2], g[1]) for g in GLOBAL])
    if dup <= pts[0][0]:
        (d0, e0), (d1, e1) = pts[0], pts[1]
    elif dup >= pts[-1][0]:
        (d0, e0), (d1, e1) = pts[-2], pts[-1]
    else:
        for i in range(len(pts) - 1):
            if pts[i][0] <= dup <= pts[i + 1][0]:
                (d0, e0), (d1, e1) = pts[i], pts[i + 1]
                break
    if d1 == d0:
        return e0
    return e0 + (e1 - e0) * (dup - d0) / (d1 - d0)


print('%-20s %9s %9s %9s %11s %10s' % ('tag', 'eff', 'dup', 'fake', 'globalEff', 'EXCESS'))
for t in sys.argv[1:]:
    m = load(t)
    eff = m['eff_overall_incut']['proto']
    dup = m['dup_overall_incut']['proto']
    fake = m['fake_overall_incut']['proto']
    ge = frontier_eff(dup)
    print('%-20s %9.5f %9.5f %9.5f %11.5f %+10.5f' % (t, eff, dup, fake, ge, eff - ge))
