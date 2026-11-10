# fit_wp.py: working points of the pixel-track classifier per (pT, |eta|) cell, separately for tuples with three hits
# and with more, each cell the (1 - eps) quantile of the scores of its true prompt tuples (vxy < 1 cm) on the
# training + validation events. A cell with fewer than --min-true true prompt tuples takes the cut of the next lower pT
# cell of the same |eta| band (the lowest cell then takes the cut of all its hit class). Also fits the single
# high-purity cut for the published subset (tuples with at least four hits): the lowest cut whose passing-fake
# fraction is at most --hp-fake percent in both samples. Reports both on the test events, next to the deployed
# (in-menu) decision of the policy's own classifier.
#   python3 fit_wp.py --train <train dir> --dump <dump dir> --eps4 0.99 --eps3 0.95 --out wp.json
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pttlib  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('--train', required=True)
ap.add_argument('--dump', required=True)
ap.add_argument('--out', required=True)
ap.add_argument('--eps4', type=float, default=0.99)
ap.add_argument('--eps3', type=float, default=0.95)
ap.add_argument('--pt-edges', default='2,5,10,25,50')
ap.add_argument('--eta-edges', default='1.1,1.7,2.5')
ap.add_argument('--min-true', type=int, default=30)
ap.add_argument('--hp-fake', type=float, default=3.0)
args = ap.parse_args()
PT = [float(x) for x in args.pt_edges.split(',')]
ETA = [float(x) for x in args.eta_edges.split(',')]
nP, nE = len(PT) + 1, len(ETA) + 1

d = pttlib.load_dumps(args.dump)
ok = np.isfinite(d['f']).all(axis=1)
d = {k: v[ok] for k, v in d.items()}
S = np.load(os.path.join(args.train, 'scores.npz'))
score, trn, val, tst = S['score'], S['trn'], S['val'], S['tst']
assert len(score) == len(d['isTrue'])
fit = trn | val
y = d['isTrue'].astype(bool)
prompt = y & (d['tpVxy'] < 1.)
pt, aeta = d['f'][:, 7], np.abs(d['f'][:, 3])
ip, ie = pttlib.bin_index(pt, PT), pttlib.bin_index(aeta, ETA)
cell = ip * nE + ie
three = d['nHits'] == 3
classes = {'4hit': ~three, '3hit': three}
eps = {'4hit': args.eps4, '3hit': args.eps3}

tables, counts = {}, {}
for cn, cm in classes.items():
    allm = fit & cm & prompt
    pooled = float(np.quantile(score[allm], 1. - eps[cn])) if allm.any() else 0.5
    t = np.zeros(nP * nE)
    n = np.zeros(nP * nE, int)
    for e_ in range(nE):
        for p_ in range(nP):
            c = p_ * nE + e_
            m = allm & (cell == c)
            n[c] = int(m.sum())
            if n[c] >= args.min_true:
                t[c] = float(np.quantile(score[m], 1. - eps[cn]))
            else:
                t[c] = t[(p_ - 1) * nE + e_] if p_ > 0 else pooled
    tables[cn], counts[cn] = t, n


def selected(sc):
    thr = np.where(three, tables['3hit'][cell], tables['4hit'][cell])
    return sc >= thr


sel = selected(score)
# high-purity cut: tuples with four or more hits that pass the working point
grid = np.linspace(0., 1., 1001)
hp = 1.
for t in grid:
    good = True
    for k in (0, 1):
        m = fit & sel & ~three & (score >= t) & (d['sample'] == k)
        if m.sum() == 0 or 100. * np.mean(~y[m]) > args.hp_fake:
            good = False
            break
    if good:
        hp = float(t)
        break


def tp_eff(passm, m, sel_tp):
    """signal TPs (with a true tuple in rows m and TP selection sel_tp): fraction with a passing true tuple"""
    r = m & y & d['tpSig'] & sel_tp
    key = ((d['sample'][r].astype(np.int64) << 40) + d['evt'][r].astype(np.int64)) * 1000003 + d['tpKey'][r].astype(np.int64)
    uk, inv = np.unique(key, return_inverse=True)
    anyp = np.zeros(len(uk), bool)
    np.logical_or.at(anyp, inv, passm[r])
    return (100. * anyp.mean() if len(uk) else float('nan')), int(len(uk))


def report(passm, hpm, m):
    out = {}
    for k, sn in enumerate(pttlib.SAMPLES):
        mk = m & (d['sample'] == k)
        nev = len(set(zip(d['run'][mk], d['lumi'][mk], d['evt'][mk])))
        o = {'events': nev, 'tuples/ev': mk.sum() / nev}
        for cn, cm in classes.items():
            q = passm & mk & cm
            o['pass/ev_' + cn] = q.sum() / nev
            o['fake%_' + cn] = 100. * np.mean(~y[q]) if q.any() else float('nan')
            o['trueEff%_' + cn] = 100. * q[mk & cm & y].mean() if (mk & cm & y).any() else float('nan')
        q = passm & mk
        o['fake%_all'] = 100. * np.mean(~y[q])
        q = hpm & mk
        o['hp/ev'] = q.sum() / nev
        o['fake%_hp'] = 100. * np.mean(~y[q]) if q.any() else float('nan')
        for lo, hi in ((0.9, 2), (2, 5), (5, 10), (10, 30), (30, 100), (100, 1e9)):
            o['tp_pt%g-%g' % (lo, hi)] = tp_eff(passm, mk, (d['tpPt'] >= lo) & (d['tpPt'] < hi))
        for lo, hi in ((0, 0.9), (0.9, 1.5), (1.5, 2.5), (2.5, 4)):
            o['tp_eta%g-%g' % (lo, hi)] = tp_eff(passm, mk, (np.abs(d['tpEta']) >= lo) & (np.abs(d['tpEta']) < hi) & (d['tpPt'] >= 0.9))
        o['tp_vxy1-5'] = tp_eff(passm, mk, (d['tpVxy'] >= 1) & (d['tpVxy'] < 5) & (d['tpPt'] >= 0.9))
        o['tp_vxy5+'] = tp_eff(passm, mk, (d['tpVxy'] >= 5) & (d['tpPt'] >= 0.9))
        out[sn] = o
    return out


res = {'ptEdges': PT, 'absEtaEdges': ETA, 'thresholds': tables['4hit'].tolist(), 'thresholdsThreeHit': tables['3hit'].tolist(),
       'highPurityThreshold': hp, 'highPurityMinNumberOfHits': 4, 'eps4': args.eps4, 'eps3': args.eps3, 'hp_fake_target%': args.hp_fake,
       'counts_true_prompt': {k: v.tolist() for k, v in counts.items()},
       'test_new': report(sel, sel & ~three & (score >= hp), tst),
       'test_deployed': report(d['dep'][:, 0], d['dep'][:, 0] & ~three, tst),
       'nPre_max': {sn: int(d['nPre'][d['sample'] == k].max()) for k, sn in enumerate(pttlib.SAMPLES)}}
json.dump(res, open(args.out, 'w'), indent=1, default=float)
print('WP eps4 %.4f eps3 %.4f, high-purity cut %.3f' % (args.eps4, args.eps3, hp))
for cn in ('4hit', '3hit'):
    print(' %s cuts (pT rows x |eta| columns):' % cn)
    for p_ in range(nP):
        print('   ' + ' '.join('%.3f(%d)' % (tables[cn][p_ * nE + e_], counts[cn][p_ * nE + e_]) for e_ in range(nE)))
for lab in ('test_new', 'test_deployed'):
    for sn in pttlib.SAMPLES:
        o = res[lab][sn]
        print('%-13s %-5s pass/ev 4hit %.0f 3hit %.0f | fake%% 4hit %.2f 3hit %.2f all %.2f | hp/ev %.0f fake%% %.2f | TP pt %s | eta %s | vxy %s %s' % (
            lab, sn, o['pass/ev_4hit'], o['pass/ev_3hit'], o['fake%_4hit'], o['fake%_3hit'], o['fake%_all'], o['hp/ev'], o['fake%_hp'],
            ' '.join('%.2f' % o[k][0] for k in o if k.startswith('tp_pt')), ' '.join('%.2f' % o[k][0] for k in o if k.startswith('tp_eta')),
            '%.2f' % o['tp_vxy1-5'][0], '%.2f' % o['tp_vxy5+'][0]))
print('max preselected tuples per event', res['nPre_max'])
