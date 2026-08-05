#!/usr/bin/env python3
"""A12 scoreboard table. Usage: a12_tab.py [--region] <tag> [<tag> ...]"""
import json
import os
import sys

DIRS = ['/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a12_ref/',
        '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fin_ref/',
        '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/xc_ref/',
        '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/rebase_ref/']

MAIN = [('eff_overall_incut', 'eff'), ('eff_vxy_0_1', 'vxy01'), ('eff_vxy_1_5', 'v15'),
        ('eff_vxy_5_10', 'v510'), ('eff_vxy_10_30', 'v1030'), ('eff_dxy_1_5', 'd15'),
        ('eff_dxy_5_10', 'd510'), ('eff_dxy_10_30', 'd1030'),
        ('dup_overall_incut', 'dup'), ('fake_overall_incut', 'fake'),
        ('mean_nhitOT_barrel', 'nhB'), ('mean_nhitOT_transition', 'nhT'),
        ('mean_nhitOT_endcap', 'nhE'), ('n_tc', 'nTC')]

REGION = [('eff_barrel', 'effB'), ('eff_transition', 'effT'), ('eff_endcap', 'effE'),
          ('dup_barrel', 'dupB'), ('dup_transition', 'dupT'), ('dup_endcap', 'dupE'),
          ('fake_barrel', 'fakB'), ('fake_transition', 'fakT'), ('fake_endcap', 'fakE'),
          ('mean_nhitOT_barrel', 'nhB'), ('mean_nhitOT_transition', 'nhT'),
          ('mean_nhitOT_endcap', 'nhE')]


def load(tag):
    for d in DIRS:
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            return json.load(open(p))['metrics']
    return None


def main():
    args = sys.argv[1:]
    keys = MAIN
    if args and args[0] == '--region':
        keys = REGION
        args = args[1:]
    rows = []
    for t in args:
        m = load(t)
        if m is None:
            print('# no json for tag ' + t)
            continue
        rows.append((t, m))
    print("%-18s" % "tag" + "".join("%9s" % n for _, n in keys))
    for t, m in rows:
        line = "%-18s" % t
        for k, _ in keys:
            v = m.get(k, {}).get('proto')
            line += ("%9s" % "-") if v is None else (("%9d" % v) if k == 'n_tc' else ("%9.5f" % v))
        print(line)
    if rows:
        m = rows[0][1]
        line = "%-18s" % "LST(base)"
        for k, _ in keys:
            v = m.get(k, {}).get('base')
            line += ("%9s" % "-") if v is None else (("%9d" % v) if k == 'n_tc' else ("%9.5f" % v))
        print(line)


main()
