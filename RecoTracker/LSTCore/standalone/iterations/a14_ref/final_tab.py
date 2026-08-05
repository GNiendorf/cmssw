#!/usr/bin/env python3
"""One-shot deliverable table: headline + displaced bands + per region + track length.

Usage: final_tab.py [--base977] <tag> [<tag> ...]
"""
import json
import os
import sys

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [S + d + '/' for d in ('a14_ref', 'fin_ref', 'xc_ref', 'rebase_ref')]

BLOCKS = [
    ('HEADLINE + DISPLACED BANDS',
     [('eff_overall_incut', 'eff'), ('dup_overall_incut', 'dup'), ('fake_overall_incut', 'fake'),
      ('eff_vxy_0_1', 'vxy01'), ('eff_vxy_1_5', 'v15'), ('eff_vxy_5_10', 'v510'),
      ('eff_vxy_10_30', 'v1030'), ('eff_dxy_1_5', 'd15'), ('eff_dxy_5_10', 'd510'),
      ('eff_dxy_10_30', 'd1030'), ('n_tc', 'nTC')]),
    ('PER REGION + TRACK LENGTH',
     [('eff_barrel', 'effB'), ('eff_transition', 'effT'), ('eff_endcap', 'effE'),
      ('dup_barrel', 'dupB'), ('dup_transition', 'dupT'), ('dup_endcap', 'dupE'),
      ('fake_barrel', 'fakB'), ('fake_transition', 'fakT'), ('fake_endcap', 'fakE'),
      ('mean_nhitOT_barrel', 'nhB'), ('mean_nhitOT_transition', 'nhT'),
      ('mean_nhitOT_endcap', 'nhE')]),
]


def load(tag):
    for d in DIRS:
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            return json.load(open(p))['metrics']
    return None


tags = [t for t in sys.argv[1:] if not t.startswith('--')]
rows = []
for t in tags:
    m = load(t)
    if m is None:
        print('SKIP (no json): %s' % t)
        continue
    rows.append((t, m))

for title, keys in BLOCKS:
    print('==== %s' % title)
    print('%-14s' % 'tag' + ''.join('%9s' % n for _, n in keys))
    for t, m in rows:
        line = '%-14s' % t
        for k, _ in keys:
            v = m[k]['proto']
            line += ('%9d' % v) if k == 'n_tc' else ('%9.5f' % v)
        print(line)
    if rows:
        m = rows[0][1]
        line = '%-14s' % 'LST'
        for k, _ in keys:
            v = m[k]['base']
            line += ('%9d' % v) if k == 'n_tc' else ('%9.5f' % v)
        print(line)
    print()
