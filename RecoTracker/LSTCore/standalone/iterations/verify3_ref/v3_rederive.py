#!/usr/bin/env python3
"""VERIFIER-3: re-derive every metric straight from the raw *_hists.root files,
using the standard tooling's own compute_metrics(), then compare to the .json the
explorer shipped. Any table doctoring or stale json shows up here.

Usage: v3_rederive.py <hists.root> [<hists.root> ...]
Prints a wide table; also writes verify3_ref/rederived.json.
"""
import json
import os
import sys

sys.path.insert(0, '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype')
import ROOT  # noqa: E402
ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError
import compare_ab as CA  # noqa: E402

KEYS = ['eff_overall_incut', 'eff_barrel', 'eff_transition', 'eff_endcap',
        'eff_vxy_0_1', 'eff_vxy_1_5', 'eff_vxy_5_10', 'eff_vxy_10_30',
        'eff_dxy_0_1', 'eff_dxy_1_5', 'eff_dxy_5_10', 'eff_dxy_10_30',
        'dup_overall_incut', 'dup_barrel', 'dup_transition', 'dup_endcap',
        'fake_overall_incut', 'fake_barrel', 'fake_transition', 'fake_endcap',
        'mean_nhitOT', 'mean_nhitOT_barrel', 'mean_nhitOT_transition',
        'mean_nhitOT_endcap', 'n_tc', 'n_sim_denom',
        'eff_overall', 'dup_overall', 'fake_overall']

out = {}
for p in sys.argv[1:]:
    tag = os.path.basename(p).replace('_hists.root', '')
    d = os.path.dirname(p)
    m = CA.compute_metrics(p, tag)
    out[p] = {k: m.get(k) for k in KEYS}
    # cross-check against the shipped json, if there is one
    jp = os.path.join(d, tag + '.json')
    mism = []
    if os.path.exists(jp):
        shipped = json.load(open(jp))['metrics']
        for k in KEYS:
            a, b = m.get(k), shipped.get(k, {}).get('proto')
            if a is None or b is None:
                if a is not b:
                    mism.append('%s: rederived=%s shipped=%s' % (k, a, b))
                continue
            if abs(a - b) > 1e-9 * max(1.0, abs(a)):
                mism.append('%s: rederived=%.8g shipped=%.8g' % (k, a, b))
    else:
        mism.append('NO SHIPPED JSON')
    out[p]['_json_mismatch'] = mism

print('%-26s' % 'tag' + ''.join('%10s' % k for k in
      ['eff', 'effB', 'effT', 'effE', 'dup', 'dupB', 'dupE',
       'fake', 'fakB', 'fakE', 'nhAll', 'nhB', 'nhT', 'nhE', 'nTC']))
for p, m in out.items():
    tag = os.path.basename(p).replace('_hists.root', '')
    vals = [m['eff_overall_incut'], m['eff_barrel'], m['eff_transition'], m['eff_endcap'],
            m['dup_overall_incut'], m['dup_barrel'], m['dup_endcap'],
            m['fake_overall_incut'], m['fake_barrel'], m['fake_endcap'],
            m['mean_nhitOT'], m['mean_nhitOT_barrel'], m['mean_nhitOT_transition'],
            m['mean_nhitOT_endcap']]
    line = '%-26s' % tag + ''.join(('%10.5f' % v) if v is not None else '%10s' % 'n/a' for v in vals)
    line += '%10d' % (m['n_tc'] or 0)
    print(line)

print()
bad = 0
for p, m in out.items():
    if m['_json_mismatch']:
        bad += 1
        print('MISMATCH %s' % p)
        for x in m['_json_mismatch']:
            print('    ' + x)
if not bad:
    print('ALL SHIPPED JSONs MATCH RE-DERIVATION FROM RAW HISTS (%d files)' % len(out))

json.dump({k: v for k, v in out.items()}, open(
    '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/verify3_ref/rederived.json', 'w'), indent=1)
