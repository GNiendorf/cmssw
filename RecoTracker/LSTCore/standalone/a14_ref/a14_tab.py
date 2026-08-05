#!/usr/bin/env python3
"""A14 headline scoreboard. Usage: a14_tab.py <tag> [<tag> ...]"""
import json, os, sys
S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [S + d + '/' for d in ('a14_ref', 'fin_ref', 'xc_ref', 'rebase_ref')]
KEYS = [('eff_overall_incut', 'eff'), ('eff_vxy_0_1', 'vxy01'), ('eff_vxy_1_5', 'v15'),
        ('eff_vxy_5_10', 'v510'), ('eff_vxy_10_30', 'v1030'), ('eff_dxy_1_5', 'd15'),
        ('eff_dxy_5_10', 'd510'), ('eff_dxy_10_30', 'd1030'),
        ('dup_overall_incut', 'dup'), ('fake_overall_incut', 'fake'),
        ('mean_nhitOT_barrel', 'nhB'), ('mean_nhitOT_transition', 'nhT'),
        ('mean_nhitOT_endcap', 'nhE'), ('n_tc', 'nTC')]


def load(tag):
    for d in DIRS:
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            return json.load(open(p))['metrics']
    raise SystemExit('no json for tag ' + tag)


rows = [(t, load(t)) for t in sys.argv[1:]]
print("%-20s" % "tag" + "".join("%9s" % n for _, n in KEYS))
for t, m in rows:
    line = "%-20s" % t
    for k, _ in KEYS:
        v = m[k]['proto']
        line += ("%9d" % v) if k == 'n_tc' else ("%9.5f" % v)
    print(line)
if rows:
    m = rows[0][1]
    line = "%-20s" % "LST(base)"
    for k, _ in KEYS:
        v = m[k]['base']
        line += ("%9d" % v) if k == 'n_tc' else ("%9.5f" % v)
    print(line)
