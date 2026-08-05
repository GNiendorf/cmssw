#!/usr/bin/env python3
"""GEN-A scoreboard. Usage: ga_board.py <tag> [<tag> ...]"""
import json
import os
import re
import sys

T = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/gen_a_ref/'
KEYS = [('eff_overall_incut', 'eff'), ('eff_vxy_0_1', 'vxy01'), ('eff_vxy_1_5', 'v15'),
        ('eff_vxy_5_10', 'v510'), ('eff_vxy_10_30', 'v1030'), ('eff_dxy_1_5', 'd15'),
        ('eff_dxy_5_10', 'd510'), ('dup_overall_incut', 'dup'),
        ('fake_overall_incut', 'fake')]
BASE = dict(eff=.81303, vxy01=.84610, v15=.80283, v510=.72713, v1030=.71732,
            d15=.56009, d510=.24912, dup=.05188, fake=.04636, nTC=614277)
ROFF = dict(eff=.77432, dup=.05254, fake=.04905, nTC=579968)


def deliv(tag):
    p = T + 't_%s.log' % tag
    d = w = None
    if os.path.exists(p):
        for ln in open(p, errors='ignore'):
            m = re.search(r'DELIVERED=(\d+) \(([\d.]+)/evt\)', ln)
            if m:
                d = float(m.group(2))
            m = re.search(r'WELD side \(-CCW \d\): revoked=(\d+) \(([\d.]+)/evt\)', ln)
            if m:
                w = float(m.group(2))
    return d, w


print('%-10s' % 'tag' + ''.join('%8s' % n for _, n in KEYS) + '%9s%8s%6s' % ('nTC', 'deliv', 'gate'))
print('%-10s' % 'P25BASE' + ''.join('%8.5f' % BASE[n] for _, n in KEYS) +
      '%9d%8s%6s' % (BASE['nTC'], '-', 'ref'))
print('%-10s' % 'r_off' + '%8.5f' % ROFF['eff'] + ' ' * (8 * 6) +
      '%8.5f%8.5f%9d%8.1f%6s' % (ROFF['dup'], ROFF['fake'], ROFF['nTC'], 0.0, 'noPT3'))
for t in sys.argv[1:]:
    f = T + 't_%s.json' % t
    if not os.path.exists(f):
        print('%-10s  (no json yet)' % t)
        continue
    m = json.load(open(f))['metrics']
    line = '%-10s' % t
    for k, _ in KEYS:
        line += '%8.5f' % m[k]['proto']
    d, w = deliv(t)
    g = ('E' if m['eff_overall_incut']['proto'] >= .81303 else '.') + \
        ('D' if m['dup_overall_incut']['proto'] <= .052 else '.') + \
        ('F' if m['fake_overall_incut']['proto'] <= .047 else '.')
    line += '%9d%8s%6s' % (m['n_tc']['proto'], '-' if d is None else '%.1f' % d, g)
    print(line)
