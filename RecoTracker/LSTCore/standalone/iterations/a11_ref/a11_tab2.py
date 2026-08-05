#!/usr/bin/env python3
"""Per-region scoreboard (eff / dup / fake / mean nhitOT by detector region).
Usage: xc_tab2.py <tag> [<tag> ...]"""
import json, os, sys
T = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a11_ref/'
R = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/rebase_ref/'
KEYS = [('eff_barrel', 'effB'), ('eff_transition', 'effT'), ('eff_endcap', 'effE'),
        ('dup_barrel', 'dupB'), ('dup_transition', 'dupT'), ('dup_endcap', 'dupE'),
        ('fake_barrel', 'fakB'), ('fake_transition', 'fakT'), ('fake_endcap', 'fakE'),
        ('mean_nhitOT', 'nhAll'), ('mean_nhitOT_barrel', 'nhB'),
        ('mean_nhitOT_transition', 'nhT'), ('mean_nhitOT_endcap', 'nhE'),
        ('n_sim_denom', 'nSim'), ('n_tc', 'nTC')]


X = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fin_ref/'


def load(tag):
    for d in (T, X, R):
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
        line += ("%9d" % v) if k in ('n_tc', 'n_sim_denom') else ("%9.5f" % v)
    print(line)
if rows:
    m = rows[0][1]
    line = "%-18s" % "LST(base)"
    for k, _ in KEYS:
        v = m[k]['base']
        line += ("%9d" % v) if k in ('n_tc', 'n_sim_denom') else ("%9.5f" % v)
    print(line)
