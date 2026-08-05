#!/usr/bin/env python3
"""A04 scoreboard table (overall + displaced bands + per region + length).
Usage: a04_tab.py <tag> [<tag> ...]   (falls back to fin_ref / xc_ref / rebase_ref)"""
import json
import os
import sys

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [S + 'a04_ref/', S + 'fin_ref/', S + 'xc_ref/', S + 'rebase_ref/']
KEYS = [('eff_overall_incut', 'eff'), ('eff_vxy_0_1', 'vxy01'), ('eff_vxy_1_5', 'v15'),
        ('eff_vxy_5_10', 'v510'), ('eff_vxy_10_30', 'v1030'), ('eff_dxy_1_5', 'd15'),
        ('eff_dxy_5_10', 'd510'), ('eff_dxy_10_30', 'd1030'),
        ('dup_overall_incut', 'dup'), ('fake_overall_incut', 'fake'),
        ('mean_nhitOT_barrel', 'nhB'), ('mean_nhitOT_transition', 'nhT'),
        ('mean_nhitOT_endcap', 'nhE'), ('n_tc', 'nTC')]
KEYS2 = [('eff_barrel', 'effB'), ('eff_transition', 'effT'), ('eff_endcap', 'effE'),
         ('dup_barrel', 'dupB'), ('dup_transition', 'dupT'), ('dup_endcap', 'dupE'),
         ('fake_barrel', 'fakB'), ('fake_transition', 'fakT'), ('fake_endcap', 'fakE')]


def load(tag):
    for d in DIRS:
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            return json.load(open(p))['metrics']
    raise SystemExit('no json for tag ' + tag)


rows = [(t, load(t)) for t in sys.argv[1:]]
for keyset in (KEYS, KEYS2):
    print("%-18s" % "tag" + "".join("%9s" % n for _, n in keyset))
    for t, m in rows:
        line = "%-18s" % t
        for k, _ in keyset:
            v = m[k]['proto']
            line += ("%9d" % v) if k == 'n_tc' else ("%9.5f" % v)
        print(line)
    if rows:
        m = rows[0][1]
        line = "%-18s" % "LST(base)"
        for k, _ in keyset:
            v = m[k]['base']
            line += ("%9d" % v) if k == 'n_tc' else ("%9.5f" % v)
        print(line)
    print()
