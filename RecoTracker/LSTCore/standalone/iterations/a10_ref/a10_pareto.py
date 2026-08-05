#!/usr/bin/env python3
"""Pareto set over (eff up, dup down, fake down) for every a10_ref tag with a json.
Usage: a10_pareto.py [tag ...]   (no args = every r_*.json in a10_ref)"""
import glob, json, os, sys
B = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [B + d for d in ('a10_ref/', 'fin_ref/', 'xc_ref/', 'rebase_ref/')]


def load(tag):
    for d in DIRS:
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            return json.load(open(p))['metrics']
    return None


tags = sys.argv[1:]
if not tags:
    tags = sorted(os.path.basename(p)[2:-5] for p in glob.glob(B + 'a10_ref/r_*.json'))
pts = []
for t in tags:
    m = load(t)
    if m is None:
        continue
    pts.append((t, m['eff_overall_incut']['proto'], m['dup_overall_incut']['proto'],
                m['fake_overall_incut']['proto'], m['n_tc']['proto'],
                m['mean_nhitOT_barrel']['proto']))


def dominates(a, b):
    # a dominates b if a is >= on eff and <= on dup and fake, strictly better somewhere
    ge = a[1] >= b[1] - 1e-12 and a[2] <= b[2] + 1e-12 and a[3] <= b[3] + 1e-12
    st = a[1] > b[1] + 1e-12 or a[2] < b[2] - 1e-12 or a[3] < b[3] - 1e-12
    return ge and st


front = [p for p in pts if not any(dominates(q, p) for q in pts if q[0] != p[0])]
print("ALL POINTS (%d)" % len(pts))
print("%-20s %8s %8s %8s %9s %8s  %s" % ("tag", "eff", "dup", "fake", "nTC", "nhB", "P"))
for p in sorted(pts, key=lambda x: -x[1]):
    print("%-20s %8.5f %8.5f %8.5f %9d %8.4f  %s"
          % (p[0], p[1], p[2], p[3], p[4], p[5], "*" if p in front else ""))
print("\nPARETO SET (%d of %d)" % (len(front), len(pts)))
for p in sorted(front, key=lambda x: -x[1]):
    print("  %-20s eff %.5f  dup %.5f  fake %.5f" % (p[0], p[1], p[2], p[3]))
