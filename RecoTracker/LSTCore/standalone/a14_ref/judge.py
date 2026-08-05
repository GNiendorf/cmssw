#!/usr/bin/env python3
"""Does a per-region calibration point BEAT the global one-knob frontier?

A regional setting only earns its extra constants if, at MATCHED duplicate rate, it gives
more efficiency than a single global -XCT would have. This interpolates the measured
global -XCT frontier (fin_ref, -AT3 6 -CCN 1 -CCR 2, 300 evts) in duplicate rate and
prints the excess, plus the fake rate the global point would have had.

Usage: judge.py <tag> [<tag> ...]
"""
import json
import os
import sys

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [S + d + '/' for d in ('a14_ref', 'fin_ref', 'xc_ref', 'rebase_ref')]

# (-XCT, eff, dup, fake) at -AT3 6 -CCN 1 -CCR 2, frozen 300. fin_ref STATUS section D.
GLOBAL = [(4.5, 0.81037, 0.06675, 0.05539),
          (4.0, 0.80992, 0.06230, 0.05551),
          (3.5, 0.80904, 0.05925, 0.05558),
          (3.0, 0.80776, 0.05689, 0.05562)]


def load(tag):
    for d in DIRS:
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            return json.load(open(p))['metrics']
    return None


def interp(dup, col):
    pts = sorted([(g[2], g[col]) for g in GLOBAL])
    if dup <= pts[0][0]:
        (d0, e0), (d1, e1) = pts[0], pts[1]
    elif dup >= pts[-1][0]:
        (d0, e0), (d1, e1) = pts[-2], pts[-1]
    else:
        for i in range(len(pts) - 1):
            if pts[i][0] <= dup <= pts[i + 1][0]:
                (d0, e0), (d1, e1) = pts[i], pts[i + 1]
                break
    return e0 if d1 == d0 else e0 + (e1 - e0) * (dup - d0) / (d1 - d0)


hdr = ('tag', 'eff', 'dup', 'fake', 'effB', 'effT', 'effE', 'gEff', 'EFFGAIN', 'gFake', 'FAKEGAIN')
print('%-12s %8s %8s %8s %8s %8s %8s %9s %9s %9s %9s' % hdr)
for t in sys.argv[1:]:
    m = load(t)
    if m is None:
        print('%-12s  (no json yet)' % t)
        continue
    eff = m['eff_overall_incut']['proto']
    dup = m['dup_overall_incut']['proto']
    fak = m['fake_overall_incut']['proto']
    ge, gf = interp(dup, 1), interp(dup, 3)
    print('%-12s %8.5f %8.5f %8.5f %8.5f %8.5f %8.5f %9.5f %+9.5f %9.5f %+9.5f'
          % (t, eff, dup, fak, m['eff_barrel']['proto'], m['eff_transition']['proto'],
             m['eff_endcap']['proto'], ge, eff - ge, gf, gf - fak))
print()
print('EFFGAIN  = efficiency above what a single global -XCT would give at this dup rate.')
print('FAKEGAIN = fake rate BELOW what that global point would give (positive is better).')
print('Extrapolation warning: the global frontier is only measured over dup .0569-.0668;')
print('points outside that range are linearly extrapolated from its end segment.')
