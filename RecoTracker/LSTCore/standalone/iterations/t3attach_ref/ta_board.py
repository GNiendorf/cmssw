#!/usr/bin/env python3
"""T3DEDUP-1 scoreboard + dedup ledger. Usage: ta_board.py <tag> [<tag> ...]"""
import json, sys, os, re
T = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/t3attach_ref/'
KEYS = [('eff_overall_incut', 'eff'), ('eff_vxy_0_1', 'vxy01'), ('eff_vxy_1_5', 'v15'),
        ('eff_vxy_5_10', 'v510'), ('eff_vxy_10_30', 'v1030'), ('eff_dxy_1_5', 'd15'),
        ('eff_dxy_5_10', 'd510'), ('dup_overall_incut', 'dup'),
        ('fake_overall_incut', 'fake'), ('n_tc', 'nTC')]
BASE = dict(eff=.81303, vxy01=.84610, v15=.80283, v510=.72713, v1030=.71732,
            d15=.56009, d510=.24912, dup=.05188, fake=.04636, nTC=614277)

def ledger(tag):
    p = T + 't_%s.log' % tag
    px = ot = dl = None
    if os.path.exists(p):
        for ln in open(p, errors='ignore'):
            if 'M20 pT3 dedup' in ln:
                m = re.search(r'PIXEL side \([^)]*\): revoked=(\d+) \(([\d.]+)/evt\)', ln)
                if m: px = float(m.group(2))
                m = re.search(r'OT side \([^)]*\): revoked=(\d+) \(([\d.]+)/evt\)', ln)
                if m: ot = float(m.group(2))
                m = re.search(r'DELIVERED=(\d+) \(([\d.]+)/evt\)', ln)
                if m: dl = float(m.group(2))
    return px, ot, dl

rows = []
for t in sys.argv[1:]:
    f = T + 't_%s.json' % t
    if not os.path.exists(f):
        print('%-12s  (no json yet)' % t); continue
    m = json.load(open(f))['metrics']
    rows.append((t, m, ledger(t)))

hdr = "%-12s" % "tag" + "".join("%8s" % n for _, n in KEYS[:-1]) + "%9s" % "nTC"
hdr += "%8s%8s%8s" % ("pxRev", "otRev", "deliv")
print(hdr)
print("%-12s" % "P25BASE" + "".join("%8.5f" % BASE[n] for _, n in KEYS[:-1]) +
      "%9d" % BASE['nTC'] + "%8s%8s%8s" % ("-", "-", "-"))
for t, m, (px, ot, dl) in rows:
    line = "%-12s" % t
    for k, n in KEYS[:-1]:
        line += "%8.5f" % m[k]['proto']
    line += "%9d" % m['n_tc']['proto']
    line += "".join("%8s" % ('-' if v is None else '%.1f' % v) for v in (px, ot, dl))
    print(line)
if rows:
    m = rows[0][1]
    print("%-12s" % "LST(base)" + "".join("%8.5f" % m[k]['base'] for k, _ in KEYS[:-1]) +
          "%9d" % m['n_tc']['base'])
