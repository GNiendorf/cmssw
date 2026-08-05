#!/usr/bin/env python3
"""a08_tab.py -- full scoreboard row (headline + displaced bands + per region + length)
for any tag produced in a08_ref, with fallback to fin_ref / xc_ref / rebase_ref so the
round's own tags can be quoted in the same table. Usage: a08_tab.py <tag> [<tag> ...]"""
import json
import os
import sys

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [S + d for d in ('a08_ref/', 'fin_ref/', 'xc_ref/', 'rebase_ref/')]

HEAD = [('eff_overall_incut', 'eff'), ('eff_vxy_0_1', 'vxy01'), ('eff_vxy_1_5', 'v15'),
        ('eff_vxy_5_10', 'v510'), ('eff_vxy_10_30', 'v1030'), ('eff_dxy_1_5', 'd15'),
        ('eff_dxy_5_10', 'd510'), ('eff_dxy_10_30', 'd1030'),
        ('dup_overall_incut', 'dup'), ('fake_overall_incut', 'fake'),
        ('mean_nhitOT_barrel', 'nhB'), ('mean_nhitOT_transition', 'nhT'),
        ('mean_nhitOT_endcap', 'nhE'), ('n_tc', 'nTC')]
REG = [('eff_barrel', 'effB'), ('eff_transition', 'effT'), ('eff_endcap', 'effE'),
       ('dup_barrel', 'dupB'), ('dup_transition', 'dupT'), ('dup_endcap', 'dupE'),
       ('fake_barrel', 'fakB'), ('fake_transition', 'fakT'), ('fake_endcap', 'fakE')]


def load(tag):
    for d in DIRS:
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            return json.load(open(p))['metrics']
    raise SystemExit('no json for tag ' + tag)


def table(keys, rows, title):
    print("\n" + title)
    print("%-12s" % "tag" + "".join("%9s" % n for _, n in keys))
    for t, m in rows:
        line = "%-12s" % t
        for k, _ in keys:
            v = m[k]['proto']
            line += ("%9d" % v) if k == 'n_tc' else ("%9.5f" % v)
        print(line)
    m = rows[0][1]
    line = "%-12s" % "LST(base)"
    for k, _ in keys:
        v = m[k]['base']
        line += ("%9d" % v) if k == 'n_tc' else ("%9.5f" % v)
    print(line)


rows = [(t, load(t)) for t in sys.argv[1:]]
table(HEAD, rows, "HEADLINE + DISPLACED BANDS + TRACK LENGTH")
table(REG, rows, "PER REGION")
