# wp_rows.py: derive a working-point json from a shipped one (fit_wp.py output) with
#   - the high-purity rows of the pixel-vertex subset: a cut per pT row for tuples with at least four hits
#     (highPurityThresholds) and one for three-hit tuples (highPurityThresholdsThreeHit; the subset then takes
#     selected three-hit tuples above it);
#   - optionally three-hit working-point cells re-fitted at another eps (the (1 - eps) quantile of the scores of true
#     prompt three-hit tuples on the training + validation events, as fit_wp.py), e.g. the barrel low-pT cells.
# With --train/--dump it reports, on the test events, the subset's tuples per event and fake fraction per reco pT row
# and its signal-TP efficiency per |eta| band.
#   python3 wp_rows.py --base shipped/v2/wp_eps4_0.99_fwd0.995_eps3_0.99.json --hp4-rows 0,0,0.5,0.8,0.8,0.8 --hp3 0.5 \
#       [--eps3-cells 0:0=0.80,1:0=0.80 --train <train dir> --dump <dump dir>] --out wp.json
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ap = argparse.ArgumentParser()
ap.add_argument('--base', required=True)
ap.add_argument('--out', required=True)
ap.add_argument('--hp4-rows', default=None, help='comma list, one high-purity cut per pT row (four or more hits)')
ap.add_argument('--hp3', type=float, default=None, help='high-purity cut of three-hit tuples (all cells)')
ap.add_argument('--eps3-cells', default=None, help='comma list ipt:ieta=eps of three-hit cells to re-fit')
ap.add_argument('--train', default=None)
ap.add_argument('--dump', default=None)
ap.add_argument('--min-true', type=int, default=30)
args = ap.parse_args()

wp = json.load(open(args.base))
nP, nE = len(wp['ptEdges']) + 1, len(wp['absEtaEdges']) + 1
if args.hp4_rows:
    rows = [float(x) for x in args.hp4_rows.split(',')]
    assert len(rows) == nP, 'one cut per pT row (%d)' % nP
    wp['highPurityThresholds'] = [r for r in rows for _ in range(nE)]
if args.hp3 is not None:
    wp['highPurityThresholdsThreeHit'] = [args.hp3] * (nP * nE)

d = score = None
if args.train and args.dump:
    import pttlib
    d = pttlib.load_dumps(args.dump)
    ok = np.isfinite(d['f']).all(axis=1)
    d = {k: v[ok] for k, v in d.items()}
    S = np.load(os.path.join(args.train, 'scores.npz'))
    score, fit, tst = S['score'], S['trn'] | S['val'], S['tst']
    assert len(score) == len(d['isTrue'])
    y = d['isTrue'].astype(bool)
    pt, aeta = d['f'][:, 7], np.abs(d['f'][:, 3])
    ipt, ieta = pttlib.bin_index(pt, wp['ptEdges']), pttlib.bin_index(aeta, wp['absEtaEdges'])
    cell = ipt * nE + ieta
    three = d['nHits'] == 3
    prompt = y & (d['tpVxy'] < 1.)

refit = {}
if args.eps3_cells:
    assert d is not None, '--eps3-cells needs --train and --dump'
    t3 = list(wp['thresholdsThreeHit'])
    for item in args.eps3_cells.split(','):
        c, eps = item.split('=')
        p_, e_ = (int(x) for x in c.split(':'))
        m = fit & three & prompt & (cell == p_ * nE + e_)
        assert m.sum() >= args.min_true, 'cell %s: %d true prompt three-hit tuples' % (c, m.sum())
        t3[p_ * nE + e_] = float(np.quantile(score[m], 1. - float(eps)))
        refit[c] = {'eps3': float(eps), 'cut': t3[p_ * nE + e_], 'was': wp['thresholdsThreeHit'][p_ * nE + e_]}
    wp['thresholdsThreeHit'] = t3
wp['wp_rows'] = {'base': os.path.basename(args.base), 'hp4_rows': args.hp4_rows, 'hp3': args.hp3, 'eps3_cells': refit}

if d is not None:
    thr = np.where(three, np.array(wp['thresholdsThreeHit'])[cell], np.array(wp['thresholds'])[cell])
    sel = score >= thr
    hp4 = np.array(wp.get('highPurityThresholds', [wp['highPurityThreshold']] * (nP * nE)))[cell]
    hp = sel & ~three & (score >= hp4)
    if 'highPurityThresholdsThreeHit' in wp:
        hp |= sel & three & (score >= np.array(wp['highPurityThresholdsThreeHit'])[cell])
    rep = {}
    for k, sn in enumerate(pttlib.SAMPLES):
        mk = tst & (d['sample'] == k)
        nev = len(set(zip(d['run'][mk], d['lumi'][mk], d['evt'][mk])))
        o = {'events': nev, 'hp/ev': float((hp & mk).sum() / nev), 'fake%_hp': float(100. * np.mean(~y[hp & mk]))}
        for p_ in range(nP):
            q = hp & mk & (ipt == p_)
            o['pTrow%d hp/ev, fake%%' % p_] = [float(q.sum() / nev), float(100. * np.mean(~y[q])) if q.any() else 0.]
        for lo, hi in ((0, 0.9), (0.9, 1.5), (1.5, 2.5), (2.5, 4)):
            r = mk & y & d['tpSig'] & (np.abs(d['tpEta']) >= lo) & (np.abs(d['tpEta']) < hi) & (d['tpPt'] >= 0.9)
            key = ((d['sample'][r].astype(np.int64) << 40) + d['evt'][r].astype(np.int64)) * 1000003 + d['tpKey'][r].astype(np.int64)
            uk, inv = np.unique(key, return_inverse=True)
            anyp = np.zeros(len(uk), bool)
            np.logical_or.at(anyp, inv, hp[r])
            o['tpEff%% hp |eta| %g-%g' % (lo, hi)] = float(100. * anyp.mean())
        o['three-hit passing/ev'] = float((sel & three & mk).sum() / nev)
        o['three-hit passing fake%'] = float(100. * np.mean(~y[sel & three & mk]))
        rep[sn] = o
    wp['wp_rows']['test'] = rep
    print(json.dumps(rep, indent=1))
json.dump(wp, open(args.out, 'w'), indent=1)
print('wrote', args.out)
