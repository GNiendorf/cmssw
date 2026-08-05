#!/usr/bin/env python3
"""M22 CEIL scoreboard + dedup ledger. Usage: ta_board.py <tag> [<tag> ...]"""
import json, sys, os, re
T = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/ceil_ref/'
KEYS = [('eff_overall_incut', 'eff'), ('eff_vxy_0_1', 'vxy01'), ('eff_vxy_1_5', 'v15'),
        ('eff_vxy_5_10', 'v510'), ('eff_vxy_10_30', 'v1030'), ('eff_dxy_1_5', 'd15'),
        ('eff_dxy_5_10', 'd510'), ('dup_overall_incut', 'dup'),
        ('fake_overall_incut', 'fake'), ('n_tc', 'nTC')]
BASE = dict(eff=.81303, vxy01=.84610, v15=.80283, v510=.72713, v1030=.71732,
            d15=.56009, d510=.24912, dup=.05188, fake=.04636, nTC=614277)

def ledger(tag):
    """pixel-side revokes, OT-side revokes, deliveries, and the M22 -CE target-universe
    reduction, straight out of the run log."""
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
    ce = None
    if os.path.exists(p):
        for ln in open(p, errors='ignore'):
            m = re.search(r'M22 -CE cond.*universe=\d+ \(([\d.]+)/evt\)', ln)
            if m:
                ce = float(m.group(1))
    return px, ot, dl, ce

rows = []
for t in sys.argv[1:]:
    f = T + 't_%s.json' % t
    if not os.path.exists(f):
        print('%-12s  (no json yet)' % t); continue
    m = json.load(open(f))['metrics']
    rows.append((t, m, ledger(t)))

hdr = "%-12s" % "tag" + "".join("%8s" % n for _, n in KEYS[:-1]) + "%9s" % "nTC"
hdr += "%8s%8s%8s%9s" % ("pxRev", "otRev", "deliv", "ceVeto")
print(hdr)
print("%-12s" % "P25BASE" + "".join("%8.5f" % BASE[n] for _, n in KEYS[:-1]) +
      "%9d" % BASE['nTC'] + "%8s%8s%8s%9s" % ("-", "-", "-", "-"))
for t, m, (px, ot, dl, ce) in rows:
    line = "%-12s" % t
    for k, n in KEYS[:-1]:
        line += "%8.5f" % m[k]['proto']
    line += "%9d" % m['n_tc']['proto']
    line += "".join("%8s" % ('-' if v is None else '%.1f' % v) for v in (px, ot, dl))
    line += "%9s" % ('-' if ce is None else '%.0f' % ce)
    print(line)
if rows:
    m = rows[0][1]
    print("%-12s" % "LST(base)" + "".join("%8.5f" % m[k]['base'] for k, _ in KEYS[:-1]) +
          "%9d" % m['n_tc']['base'])
