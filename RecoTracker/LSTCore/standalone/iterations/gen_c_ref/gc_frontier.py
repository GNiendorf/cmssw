#!/usr/bin/env python3
"""GEN-C -- does the LAYER-CONTAINMENT veto escape the N-shared-unit frontier?

Sibling A established an ORACLE ceiling for "veto when >= N units are already claimed"
rules: with perfect truth-based selection, N>=1 caps efficiency at ~.8069, N>=2 at ~.8110,
N>=3 at ~.8147 (dup .28). -CCL is NOT a point on that spectrum -- it vetoes on layer
CONTAINMENT (one owner already covers every layer the T3 occupies), so it is the one rule
that could in principle sit off that curve.

Test: build the (dup, eff) frontier from the N-rule points at a fixed head and margin,
then check whether each -CCL point sits ABOVE the linear interpolation of that frontier at
its own duplicate rate. Positive residual = escapes; ~0 = on it; negative = worse.
"""
import json
import os
import sys

G = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/gen_c_ref/'


def m(tag):
    p = G + 't_%s.json' % tag
    if not os.path.exists(p):
        return None
    d = json.load(open(p))['metrics']
    return (d['eff_overall_incut']['proto'], d['dup_overall_incut']['proto'],
            d['fake_overall_incut']['proto'], d['n_tc']['proto'])


# N-rule reference points, all: M21 head, -RDT 1, MD granularity, no -CCL, no -RPO.
NRULE = [('x_a75_n1', 'N=1 @7.5'), ('y_a75_n2', 'N=2 @7.5'),
         ('x_a9_n1', 'N=1 @9'), ('y_a9_n2', 'N=2 @9'),
         ('x_a6_n1', 'N=1 @6'), ('f_a7_n1', 'N=1 @7'), ('f_a85_n1', 'N=1 @8.5')]
CCL = [('q_a75_L', 'CCL only @7.5'), ('q_a75_L_n2', 'CCL+N=2 @7.5'),
       ('q_a6_L', 'CCL only @6'), ('q_a5_L', 'CCL only @5'),
       ('f_a75_Ln1', 'CCL+N=1 @7.5'), ('f_a7_Ln2', 'CCL+N=2 @7'),
       ('f_a8_Ln2', 'CCL+N=2 @8')]

pts = []
for t, nm in NRULE:
    r = m(t)
    if r:
        pts.append((r[1], r[0], nm))
pts.sort()
print('N-SHARED-UNIT FRONTIER (M21 head, MD granularity, no -CCL, no -RPO)')
for d, e, nm in pts:
    print('   dup %.5f  eff %.5f   %s' % (d, e, nm))


def interp(dup):
    """Upper envelope of the N-rule points, linearly interpolated in dup."""
    best = None
    for i in range(len(pts) - 1):
        d0, e0, _ = pts[i]
        d1, e1, _ = pts[i + 1]
        if d0 <= dup <= d1 and d1 > d0:
            v = e0 + (e1 - e0) * (dup - d0) / (d1 - d0)
            best = v if best is None else max(best, v)
    if best is None:
        best = pts[-1][1] if dup > pts[-1][0] else pts[0][1]
    return best


print()
print('LAYER-CONTAINMENT POINTS vs THAT FRONTIER AT THE SAME DUPLICATE RATE')
print('%-18s %9s %9s %9s %11s' % ('config', 'eff', 'dup', 'fake', 'eff-frontier'))
for t, nm in CCL:
    r = m(t)
    if not r:
        print('%-18s   (not available)' % nm)
        continue
    e, d, f, _ = r
    print('%-18s %9.5f %9.5f %9.5f %+11.5f' % (nm, e, d, f, e - interp(d)))
print()
print('A positive last column means the layer rule delivers more efficiency at the same')
print('duplicate rate than any mixture of the N-shared-unit rules can.')
