#!/usr/bin/env python3
"""GEN-B scoreboard. Usage: gb_board.py <tag> [<tag> ...]   (t_<tag>.json in gen_b_ref/)"""
import json
import os
import re
import sys

G = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/gen_b_ref/'
T = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/t3attach_ref/'
KEYS = [('eff_overall_incut', 'eff'), ('eff_vxy_0_1', 'vxy01'), ('eff_vxy_1_5', 'v15'),
        ('eff_vxy_5_10', 'v510'), ('eff_vxy_10_30', 'v1030'), ('eff_dxy_1_5', 'd15'),
        ('eff_dxy_5_10', 'd510'), ('dup_overall_incut', 'dup'),
        ('fake_overall_incut', 'fake')]
BASE = dict(eff=.81303, vxy01=.84610, v15=.80283, v510=.72713, v1030=.71732,
            d15=.56009, d510=.24912, dup=.05188, fake=.04636, nTC=614277)
GATE = "PASS: eff>=.81303 dup<=.052 fake<=.047"


def deliv(tag, d):
    p = d + 't_%s.log' % tag
    if not os.path.exists(p):
        return None
    for ln in open(p, errors='ignore'):
        if 'M20 pT3 dedup' in ln:
            m = re.search(r'DELIVERED=(\d+) \(([\d.]+)/evt\)', ln)
            if m:
                return float(m.group(2))
    return None


print('%-14s' % 'tag' + ''.join('%8s' % n for _, n in KEYS) + '%9s%8s   %s' % ('nTC', 'deliv', 'verdict'))
print('%-14s' % 'P25BASE' + ''.join('%8.5f' % BASE[n] for _, n in KEYS) + '%9d%8s' % (BASE['nTC'], '-'))
for tag in sys.argv[1:]:
    d = G if os.path.exists(G + 't_%s.json' % tag) else T
    f = d + 't_%s.json' % tag
    if not os.path.exists(f):
        print('%-14s (no json)' % tag)
        continue
    m = json.load(open(f))['metrics']
    line = '%-14s' % tag
    v = {}
    for k, n in KEYS:
        v[n] = m[k]['proto']
        line += '%8.5f' % v[n]
    line += '%9d' % m['n_tc']['proto']
    dl = deliv(tag, d)
    line += '%8s' % ('-' if dl is None else '%.1f' % dl)
    ok = (v['eff'] >= BASE['eff'] and v['dup'] <= 0.052 and v['fake'] <= 0.047)
    bad = []
    if v['eff'] < BASE['eff']:
        bad.append('eff%+.5f' % (v['eff'] - BASE['eff']))
    if v['dup'] > 0.052:
        bad.append('dup%+.5f' % (v['dup'] - 0.052))
    if v['fake'] > 0.047:
        bad.append('fake%+.5f' % (v['fake'] - 0.047))
    line += '   ' + ('PASS' if ok else 'fail ' + ' '.join(bad))
    print(line)
print(GATE)
