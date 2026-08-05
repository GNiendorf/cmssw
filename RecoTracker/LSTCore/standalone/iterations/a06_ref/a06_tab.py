#!/usr/bin/env python3
"""A06 (TRACK LENGTH) scoreboard. Usage: a06_tab.py [-r] <tag> [<tag> ...]
   -r : per-region table instead of the overall + displaced-band table.
Falls back a06_ref -> fin_ref -> xc_ref -> rebase_ref so older tags can be quoted."""
import json
import os
import sys

B = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [B + 'a06_ref/', B + 'fin_ref/', B + 'xc_ref/', B + 'rebase_ref/']

KEYS = [('eff_overall_incut', 'eff'), ('eff_vxy_0_1', 'vxy01'), ('eff_vxy_1_5', 'v15'),
        ('eff_vxy_5_10', 'v510'), ('eff_vxy_10_30', 'v1030'), ('eff_dxy_1_5', 'd15'),
        ('eff_dxy_5_10', 'd510'), ('eff_dxy_10_30', 'd1030'),
        ('dup_overall_incut', 'dup'), ('fake_overall_incut', 'fake'),
        ('mean_nhitOT_barrel', 'nhB'), ('mean_nhitOT_transition', 'nhT'),
        ('mean_nhitOT_endcap', 'nhE'), ('n_tc', 'nTC')]
RKEYS = [('eff_barrel', 'effB'), ('eff_transition', 'effT'), ('eff_endcap', 'effE'),
         ('dup_barrel', 'dupB'), ('dup_transition', 'dupT'), ('dup_endcap', 'dupE'),
         ('fake_barrel', 'fakB'), ('fake_transition', 'fakT'), ('fake_endcap', 'fakE'),
         ('mean_nhitOT_barrel', 'nhB'), ('mean_nhitOT_transition', 'nhT'),
         ('mean_nhitOT_endcap', 'nhE')]


def load(tag):
    for d in DIRS:
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            return json.load(open(p))['metrics']
    raise SystemExit('no json for tag ' + tag)


args = sys.argv[1:]
keys = KEYS
if args and args[0] == '-r':
    keys = RKEYS
    args = args[1:]
rows = [(t, load(t)) for t in args]
print("%-18s" % "tag" + "".join("%9s" % n for _, n in keys))
for t, m in rows:
    line = "%-18s" % t
    for k, _ in keys:
        v = m[k]['proto']
        line += "%9s" % "n/a" if v is None else (("%9d" % v) if k == 'n_tc' else ("%9.5f" % v))
    print(line)
if rows:
    m = rows[0][1]
    line = "%-18s" % "LST(base)"
    for k, _ in keys:
        v = m[k]['base']
        line += "%9s" % "n/a" if v is None else (("%9d" % v) if k == 'n_tc' else ("%9.5f" % v))
    print(line)
