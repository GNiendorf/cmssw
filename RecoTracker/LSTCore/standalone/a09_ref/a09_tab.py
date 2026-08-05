#!/usr/bin/env python3
"""SEED-CROSSCLEAN scoreboard table. Usage: xc_tab.py <tag> [<tag> ...]"""
import json, os, sys
T = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a09_ref/'
R = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/rebase_ref/'
KEYS = [('eff_overall_incut', 'eff'), ('eff_vxy_0_1', 'vxy01'), ('eff_vxy_1_5', 'v15'),
        ('eff_vxy_5_10', 'v510'), ('eff_vxy_10_30', 'v1030'), ('eff_dxy_1_5', 'd15'),
        ('eff_dxy_5_10', 'd510'), ('eff_dxy_10_30', 'd1030'),
        ('dup_overall_incut', 'dup'), ('fake_overall_incut', 'fake'),
        ('mean_nhitOT_barrel', 'nhB'), ('mean_nhitOT_transition', 'nhT'),
        ('mean_nhitOT_endcap', 'nhE'), ('n_tc', 'nTC')]


X = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/xc_ref/'
F = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fin_ref/'


def load(tag):
    for d in (T, F, X, R):
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            return json.load(open(p))['metrics']
    raise SystemExit('no json for tag ' + tag)


rows = [(t, load(t)) for t in sys.argv[1:]]
print("%-18s" % "tag" + "".join("%9s" % n for _, n in KEYS))
for t, m in rows:
    line = "%-18s" % t
    for k, _ in KEYS:
        v = m[k]['proto']
        line += ("%9d" % v) if k == 'n_tc' else ("%9.5f" % v)
    print(line)
if rows:
    m = rows[0][1]
    line = "%-18s" % "LST(base)"
    for k, _ in KEYS:
        v = m[k]['base']
        line += ("%9d" % v) if k == 'n_tc' else ("%9.5f" % v)
    print(line)
