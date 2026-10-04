#!/usr/bin/env python3
"""Compare two mkFit-level track dumps (plugins/ReplayStageDump.cc) per stage (harness lane).

  python3 test/replay_stage_compare.py REF_PREFIX TGT_PREFIX [--stages seeds,loaded,...] [--json OUT_PREFIX]
  files: <prefix><stage>.bin. Default stages: the stockStages instances (final = '').
Pairing: (run, event, label, k-th occurrence of that label in the event): several candidates per seed pair in order.
Per stage: counts, unpaired, hit-list identity (nTot and the first 32 (layer, index)), same position, pull buckets of
the 6 mkFit parameters in reference sigmas, score / chi2 changes. --json writes per-stage summaries in the
comparator JSON format, usable by test/replay_floor_check.py (floor = v3 vs v2 dumps; port = stock vs port dumps)."""
import argparse, json, math, os, sys
import numpy as np

KMAX = 32
DT = np.dtype([('event', '<u8'), ('run', '<u4'), ('lumi', '<u4'), ('label', '<i4'), ('pos', '<i4'), ('nTot', '<i2'),
               ('nFound', '<i2'), ('par', '<f4', 6), ('err', '<f4', 6), ('chi2', '<f4'), ('score', '<f4'),
               ('layer', '<i2', KMAX), ('pad', '<i2'), ('index', '<i4', KMAX)])
STAGES = ['seeds', 'loaded', 'fwd', 'fwdFiltered', 'bkfit', 'bkwsearch', 'postFilter', 'export', 'final']
PAR = ['x', 'y', 'z', '1/pT', 'phi', 'theta']
EDGES = [1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 0.1, 1.]


def keys(a):
    occ = {}
    out = np.empty(len(a), dtype=object)
    ev, lab = a['event'], a['label']
    for i in range(len(a)):
        k = (int(ev[i]), int(lab[i]))
        n = occ.get(k, 0)
        occ[k] = n + 1
        out[i] = (k[0], k[1], n)
    return out


def buckets(x):
    b = [int(np.sum(x == 0))]
    lo = 0.
    for e in EDGES:
        b.append(int(np.sum((x > lo) & (x < e)) if lo > 0 else np.sum((x > 0) & (x < e))))
        lo = e
    b.append(int(np.sum(~(x < 1.))))  # >= 1 and NaN
    return b


def compare(r, t):
    kr, kt = keys(r), keys(t)
    it = {k: i for i, k in enumerate(kt)}
    ir, jt = [], []
    for i, k in enumerate(kr):
        j = it.get(k)
        if j is not None:
            ir.append(i)
            jt.append(j)
    ir, jt = np.array(ir, dtype=np.int64), np.array(jt, dtype=np.int64)
    R, T = r[ir], t[jt]
    n = len(ir)
    nh = np.minimum(R['nTot'], KMAX)
    mask = np.arange(KMAX)[None, :] < nh[:, None]
    same = (R['nTot'] == T['nTot']) & np.all(~mask | ((R['layer'] == T['layer']) & (R['index'] == T['index'])), axis=1)
    d = {'nRef': len(r), 'nTgt': len(t), 'matched': n, 'refOnly': len(r) - n, 'tgtOnly': len(t) - n,
         'sameHits': int(same.sum()), 'sameIndex': int(np.sum(R['pos'] == T['pos'])), 'parNames': PAR,
         'pullBuckets': [], 'maxPull': []}
    for k in range(6):
        dp = (T['par'][:, k] - R['par'][:, k]).astype(np.float64)
        if k == 4:
            dp = (dp + math.pi) % (2 * math.pi) - math.pi
        s = np.sqrt(np.maximum(R['err'][:, k].astype(np.float64), 0))
        with np.errstate(divide='ignore', invalid='ignore'):
            p = np.where(s > 0, np.abs(dp) / s, np.where(dp == 0, 0., np.inf))
        d['pullBuckets'].append(buckets(p))
        d['maxPull'].append(float(np.nanmax(p)) if n else 0.)
    d['scoreChanged'] = int(np.sum(R['score'] != T['score']))
    d['chi2RelGt1e-3'] = int(np.sum(np.abs(T['chi2'] - R['chi2']) > 1e-3 * np.maximum(1, np.abs(R['chi2']))))
    return d


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('ref')
    ap.add_argument('tgt')
    ap.add_argument('--stages', default=','.join(STAGES))
    ap.add_argument('--json', default='')
    a = ap.parse_args()
    print('%-12s %8s %8s %8s %7s %7s %9s %10s %9s  |pull|>=1e-2 per par (x y z 1/pT phi theta)  >=1' % (
        'stage', 'nRef', 'nTgt', 'paired', 'refOnl', 'tgtOnl', 'hitsDiff', 'rate', 'scoreChg'))
    for st in a.stages.split(','):
        fr, ft = a.ref + st + '.bin', a.tgt + st + '.bin'
        if not (os.path.exists(fr) and os.path.exists(ft)):
            print('%-12s missing' % st)
            continue
        r, t = np.fromfile(fr, dtype=DT), np.fromfile(ft, dtype=DT)
        d = compare(r, t)
        d['label'] = st
        hd = d['matched'] - d['sameHits']
        t2 = ' '.join('%d' % sum(b[6:]) for b in d['pullBuckets'])
        t1 = ' '.join('%d' % b[8] for b in d['pullBuckets'])
        print('%-12s %8d %8d %8d %7d %7d %9d %10.2e %9d  %-44s %s' % (
            st, d['nRef'], d['nTgt'], d['matched'], d['refOnly'], d['tgtOnly'], hd, hd / max(1, d['matched']),
            d['scoreChanged'], t2, t1))
        if a.json:
            json.dump(d, open(a.json + st + '.json', 'w'))


if __name__ == '__main__':
    main()
