#!/usr/bin/env python3
"""D-M4 acceptance check (harness lane): is a port-vs-stock comparison no worse than the stock noise floor
(stock x86-64-v2 vs stock x86-64-v3, test/replay_floor.sh) of the same comparator on the same sample?

  python3 test/replay_floor_check.py FLOOR PORT [--label floorCand] [--port-label cmpMine] [--k 3]

FLOOR / PORT: a comparator summary JSON (summaryFile=...) or a cmsRun log with the "[compare <label>]" lines.
Metrics (rates per paired track; unpaired per reference track):
  hits     paired tracks whose hit lists differ               (1 - sameHits / matched)
  unpaired reference + target tracks without a partner       ((refOnly + tgtOnly) / nRef)
  pull<p>  paired tracks with |pull| >= 1e-2 and >= 1 per parameter
  truth one-side    TPs found by only one side / TPs found by either   (MkFitAlpakaPairedTruthCompare "[paired X]"
  truth transitions seed-paired true->fake + fake->true / seed-paired   lines, from a log: FLOOR/PORT or --*-paired)
A metric of the floor that the port does not report FAILS (review M3 a).
Rule per metric: n_port <= r_floor * N_port + k * sqrt(r_floor * N_port) + 1  (Poisson slack, k = 3 by default).
Exit status 0 if every metric passes."""
import argparse, json, math, re, sys

EDGES = ['==0', '<1e-6', '<1e-5', '<1e-4', '<1e-3', '<1e-2', '<0.1', '<1', '>=1']


def from_log(fn, label):
    d = {'pullBuckets': [], 'parNames': []}
    pre = '[compare %s]' % label
    inPull = False
    for line in open(fn, errors='replace'):
        if not line.startswith(pre):
            continue
        t = line[len(pre):].strip()
        m = re.match(r'tracks ref (\d+) tgt (\d+) matched-by-seed (\d+) ref-only (\d+) tgt-only (\d+)', t)
        if m:
            d.update(nRef=int(m[1]), nTgt=int(m[2]), matched=int(m[3]), refOnly=int(m[4]), tgtOnly=int(m[5]))
        m = re.match(r'matched pairs: identical hit lists (\d+)', t)
        if m:
            d['sameHits'] = int(m[1])
        m = re.match(r'events (\d+) ', t)
        if m and 'events' not in d:
            d['events'] = int(m[1])
        if t.startswith('|pull| buckets'):
            inPull = True
            continue
        if t.startswith('|sigma') or t.startswith('|dchi2'):
            inPull = False
            continue
        m = re.match(r'(\S+): ((?:\d+ ){8}\d+)', t)
        if inPull and m:
            d['parNames'].append(m[1])
            d['pullBuckets'].append([int(x) for x in m[2].split()])
    if 'matched' not in d:
        sys.exit('no "%s" summary in %s' % (pre, fn))
    return d


def paired_from_log(fn):
    """Paired-truth numbers of the (single) MkFitAlpakaPairedTruthCompare in a log, or None."""
    d = {}
    for line in open(fn, errors='replace'):
        if not line.startswith('[paired '):
            continue
        m = re.search(r'TPs found by both (\d+), ref only (\d+), tgt only (\d+)', line)
        if m:
            d.update(both=int(m[1]), refOnly=int(m[2]), tgtOnly=int(m[3]))
        m = re.search(r'true/true (\d+), true->fake (\d+), fake->true (\d+), fake/fake (\d+)', line)
        if m:
            d.update(tt=int(m[1]), tf=int(m[2]), ft=int(m[3]), ff=int(m[4]))
    return d if len(d) == 7 else None


def load(fn, label):
    if fn.endswith('.json'):
        return json.load(open(fn))
    return from_log(fn, label)


def paired_metrics(d):
    if d is None:
        return {}
    return {'truth one-side': (d['refOnly'] + d['tgtOnly'], d['both'] + d['refOnly'] + d['tgtOnly']),
            'truth transitions': (d['tf'] + d['ft'], d['tt'] + d['tf'] + d['ft'] + d['ff'])}


def metrics(d):
    m = {'hits': (d['matched'] - d['sameHits'], d['matched']),
         'unpaired': (d['refOnly'] + d['tgtOnly'], d['nRef'])}
    for name, b in zip(d.get('parNames', []), d.get('pullBuckets', [])):
        m['pull %s >=1e-2' % name] = (sum(b[6:]), d['matched'])
        m['pull %s >=1' % name] = (b[8], d['matched'])
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('floor')
    ap.add_argument('port')
    ap.add_argument('--label', default='floorCand', help='comparator label in the floor log')
    ap.add_argument('--port-label', default=None, help='comparator label in the port log (default: --label)')
    ap.add_argument('--k', type=float, default=3.)
    ap.add_argument('--floor-paired', default=None, help='log with the floor "[paired X]" lines (default: FLOOR if a log)')
    ap.add_argument('--port-paired', default=None, help='log with the port "[paired X]" lines (default: PORT if a log)')
    ap.add_argument('--no-paired', action='store_true', help='ignore paired truth (hit/pull metrics only)')
    a = ap.parse_args()
    f = metrics(load(a.floor, a.label))
    p = metrics(load(a.port, a.port_label or a.label))
    if not a.no_paired:
        fp = a.floor_paired or (a.floor if not a.floor.endswith('.json') else None)
        pp = a.port_paired or (a.port if not a.port.endswith('.json') else None)
        f.update(paired_metrics(paired_from_log(fp)) if fp else {})
        p.update(paired_metrics(paired_from_log(pp)) if pp else {})
    ok = True
    print('%-22s %22s %22s %10s  %s' % ('metric', 'floor n/N (rate)', 'port n/N (rate)', 'allowed', 'verdict'))
    for k, (nf, Nf) in f.items():
        if k not in p:
            ok = False
            print('%-22s %10d/%-8d %33s  MISSING in the port summary: FAIL' % (k, nf, Nf, ''))
            continue
        np_, Np = p[k]
        r = nf / Nf if Nf else 0.
        allowed = r * Np + a.k * math.sqrt(r * Np) + 1
        v = np_ <= allowed
        ok &= v
        print('%-22s %10d/%-8d(%.2e) %10d/%-8d(%.2e) %10.1f  %s' % (k, nf, Nf, r, np_, Np, np_ / Np if Np else 0,
                                                                    allowed, 'ok' if v else 'WORSE'))
    print('D-M4 verdict: %s' % ('PASS (no worse than the stock v2/v3 floor)' if ok else 'FAIL'))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
