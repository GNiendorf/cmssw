#!/usr/bin/env python3
"""A09 frontier comparison. Each -RPS mode has its own eff-vs-dup curve parameterised by
-XCT; the only fair comparison is at MATCHED duplicate rate. Reads the run jsons, groups
tags into named curves, linearly interpolates each curve to a target duplicate rate and
reports the efficiency (and fake rate) there.

Usage: a09_front.py <target_dup> <curve>=<tag,tag,...> [<curve>=<tags> ...]
"""
import json
import os
import sys

DIRS = ['/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a09_ref/',
        '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fin_ref/',
        '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/xc_ref/']


def load(tag):
    for d in DIRS:
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            m = json.load(open(p))['metrics']
            return (m['dup_overall_incut']['proto'], m['eff_overall_incut']['proto'],
                    m['fake_overall_incut']['proto'])
    return None


def interp(pts, target):
    """pts sorted by dup; linear interpolation of (eff, fake) at target dup."""
    pts = sorted(pts)
    for i in range(len(pts) - 1):
        d0, e0, f0 = pts[i]
        d1, e1, f1 = pts[i + 1]
        if d0 <= target <= d1 and d1 > d0:
            t = (target - d0) / (d1 - d0)
            return e0 + t * (e1 - e0), f0 + t * (f1 - f0), 'interp'
    # outside the measured range: report the nearest endpoint, flagged
    if target < pts[0][0]:
        return pts[0][1], pts[0][2], 'BELOW range (dup>=%.5f)' % pts[0][0]
    return pts[-1][1], pts[-1][2], 'ABOVE range (dup<=%.5f)' % pts[-1][0]


target = float(sys.argv[1])
print('target duplicate rate = %.5f' % target)
print('%-14s %-42s %9s %9s   %s' % ('curve', 'measured points (dup:eff)', 'eff@tgt', 'fake@tgt', 'note'))
for spec in sys.argv[2:]:
    name, tags = spec.split('=', 1)
    pts = []
    miss = []
    for t in tags.split(','):
        v = load(t)
        if v is None:
            miss.append(t)
        else:
            pts.append(v)
    if not pts:
        print('%-14s NO DATA (%s)' % (name, ','.join(miss)))
        continue
    e, f, note = interp(pts, target)
    shown = ' '.join('%.5f:%.5f' % (d, ee) for d, ee, _ in sorted(pts))
    if miss:
        note += ' [missing %s]' % ','.join(miss)
    print('%-14s %-42s %9.5f %9.5f   %s' % (name, shown[:42], e, f, note))
