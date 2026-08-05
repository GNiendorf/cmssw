#!/usr/bin/env python3
"""Displaced bands expressed in TRACKS lost/gained vs the assembled baseline (GATE).
The band efficiencies are quantised: every measured value in a band is an integer multiple
of 1/denominator, so the honest unit is tracks, not efficiency points. Quanta measured
empirically from the 300-evt spectrum of values seen this round.
Usage: a10_disp.py <tag> [tag ...]"""
import json, os, sys
B = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [B + d for d in ('a10_ref/', 'fin_ref/', 'xc_ref/', 'rebase_ref/')]
# key, label, 1-track quantum on the frozen 300 (= 1/denominator)
BANDS = [('eff_vxy_1_5', 'v15', 0.000730), ('eff_vxy_5_10', 'v510', 0.001686),
         ('eff_vxy_10_30', 'v1030', 0.000800), ('eff_dxy_1_5', 'd15', 0.001114),
         ('eff_dxy_5_10', 'd510', 0.004032), ('eff_dxy_10_30', 'd1030', 0.002364)]


def load(tag):
    for d in DIRS:
        p = d + 'r_%s.json' % tag
        if os.path.exists(p):
            return json.load(open(p))['metrics']
    raise SystemExit('no json ' + tag)


base = load('GATE')
print('%-22s' % 'tag' + ''.join('%8s' % l for _, l, _ in BANDS) + '%9s' % 'worst')
print('%-22s' % '(tracks vs FINBASE)' + ''.join('%8s' % '' for _ in BANDS))
for t in sys.argv[1:]:
    m = load(t)
    row, worst = '%-22s' % t, 0
    for k, _, q in BANDS:
        n = (m[k]['proto'] - base[k]['proto']) / q
        worst = min(worst, n)
        row += '%8.1f' % n
    print(row + '%9.1f' % worst)
