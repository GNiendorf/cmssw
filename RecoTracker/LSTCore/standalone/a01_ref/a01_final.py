#!/usr/bin/env python3
"""A01 final deliverable table: headline + per-region + ledger, for every tag, in one pass."""
import json
import os
import re
import sys

T = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a01_ref/'
F = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fin_ref/'

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
    for d in (T, F):
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            return json.load(open(p))['metrics']
    return None


def led(tag):
    for d in (T, F):
        p = d + 'r_%s.log' % tag
        if os.path.exists(p):
            txt = open(p, errors='replace').read()
            m = re.search(r'XC ownarm .*?seeds retired=\d+ mean=([0-9.]+)', txt)
            out = {'xco': float(m.group(1)) if m else 0.0}
            for cls, k in (('A true', 'A'), ('B true', 'B')):
                mm = re.findall(re.escape(cls) + r'[^\n]*?((?:\s+[0-9.]+){5})\s*\n', txt)
                if mm:
                    out[k] = [float(x) for x in mm[-1].split()]
            return out
    return {}


def table(tags, keys, title, label):
    print(title)
    print("%-14s" % label + "".join("%9s" % n for _, n in keys))
    base = None
    for t in tags:
        m = load(t)
        if m is None:
            print("%-14s  (pending)" % t)
            continue
        base = m
        line = "%-14s" % t
        for k, _ in keys:
            v = m[k]['proto']
            line += ("%9d" % v) if k == 'n_tc' else ("%9.5f" % v)
        print(line)
    if base:
        line = "%-14s" % "LST(target)"
        for k, _ in keys:
            v = base[k]['base']
            line += ("%9d" % v) if k == 'n_tc' else ("%9.5f" % v)
        print(line)
    print()


tags = sys.argv[1:]
table(tags, HEAD, "A. HEADLINE + DISPLACED BANDS + TRACK LENGTH", "tag")
table(tags, REG, "B. PER REGION", "tag")
print("C. SEED LEDGER (per event): ownership-arm retirements, class-A/B newly retired by the")
print("   whole crossclean, class-A survivors (the population this angle targets)")
print("%-14s %9s %9s %9s %9s %9s" % ("tag", "XCOret", "Aretire", "Bretire", "Asurv", "sel A:B"))
for t in tags:
    o = led(t)
    if 'A' not in o:
        print("%-14s  (pending)" % t)
        continue
    A, B = o['A'], o['B']
    sel = (A[3] / B[3]) if B[3] > 0 else float('inf')
    print("%-14s %9.1f %9.1f %9.1f %9.1f %9.1f" % (t, o['xco'], A[3], B[3], A[4], sel))
