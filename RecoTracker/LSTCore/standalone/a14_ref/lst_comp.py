#!/usr/bin/env python3
"""LST's own per-eta-band TC composition from an input ntuple (the -RGD reference)."""
import sys
import numpy as np
import uproot

path = sys.argv[1] if len(sys.argv) > 1 else (
    '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
    'rebase_ref/LSTNtuple_instr_300evt.root')
t = uproot.open(path)['tree']
ty = t['tc_type'].array(library='np')
eta = t['tc_eta'].array(library='np')
n = len(ty)
ty = np.concatenate([np.asarray(a) for a in ty])
eta = np.concatenate([np.asarray(a) for a in eta])
ae = np.abs(eta)
band = np.where(ae < 1.1, 0, np.where(ae < 1.7, 1, 2))
names = {4: 'T5', 5: 'pT3', 7: 'pT5', 8: 'pLS', 9: 'T4'}
print('LST carried TC rows per event over %d events, %s' % (n, path.split('/')[-1]))
print('%-12s %9s %9s %9s %9s %9s %9s' % ('band', 't4 T5', 't5 pT3', 't7 pT5', 't8 pLS', 't9 T4', 'total'))
for b, bn in enumerate(('barrel', 'transition', 'endcap')):
    row = [np.sum((band == b) & (ty == k)) / n for k in (4, 5, 7, 8, 9)]
    print('%-12s %9.1f %9.1f %9.1f %9.1f %9.1f %9.1f' % (bn, row[0], row[1], row[2], row[3], row[4], sum(row)))
row = [np.sum(ty == k) / n for k in (4, 5, 7, 8, 9)]
print('%-12s %9.1f %9.1f %9.1f %9.1f %9.1f %9.1f' % ('ALL', row[0], row[1], row[2], row[3], row[4], sum(row)))
# pT3 rows by band as a fraction, the calibration reference
tot5 = np.sum(ty == 5)
print('\nLST pT3 (type 5) split by band: ' + ', '.join(
    '%s %.1f%%' % (bn, 100.0 * np.sum((band == b) & (ty == 5)) / max(tot5, 1))
    for b, bn in enumerate(('barrel', 'transition', 'endcap'))))
