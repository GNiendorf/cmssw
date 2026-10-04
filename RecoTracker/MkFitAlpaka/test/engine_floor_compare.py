#!/usr/bin/env python3
# D-M4 noise floor of the clone-engine stage: stock(x86-64-v2) vs stock(x86-64-v3) step dumps of the SAME events,
# both written with 1 thread (same record order), compared seed by seed with the engine test's "structural" rule
# (seed state, candidate count/order, hit/hole/overlap counters, overlap pair, hit chains (hit, layer), best-short).
# Usage: engine_floor_compare.py <dumpdir A> <dumpdir B>     (format: test/engine_step_format.h)
import glob
import os
import struct
import sys

CAND_WORDS = 47


def read_cand(b, o):
    w = struct.unpack_from('<2f9i4i2f6f21f2iI', b, o)
    c = dict(lastHitIdx=w[2], counters=w[3:9], hasCC=w[10], ovHit=w[11:13], ovModule=w[13:15])
    return c, o + 4 * CAND_WORDS


def read_seed(b, o):
    seedIdx, state, pickup, lb, nib, ntb = struct.unpack_from('<6i', b, o)
    o += 24
    best, o = read_cand(b, o)
    (nc,) = struct.unpack_from('<i', b, o)
    o += 4
    cands = []
    for _ in range(nc):
        c, o = read_cand(b, o)
        cands.append(c)
    (nh,) = struct.unpack_from('<i', b, o)
    o += 4
    hots = [struct.unpack_from('<iifi', b, o + 16 * k) for k in range(nh)]
    o += 16 * nh
    return dict(seedIdx=seedIdx, state=state, best=best, cands=cands, hots=hots), o


def read_records(fn):
    b = open(fn, 'rb').read()
    o = 0
    recs = []
    while o < len(b):
        magic, kind, call, region, iterDir, startSeed, nSeeds = struct.unpack_from('<I6i', b, o)
        assert magic == 0x31445345, fn
        o += 28
        nrec = nSeeds
        if kind == 1:
            o += 80
            (nl,) = struct.unpack_from('<i', b, o)
            o += 4
            for _ in range(nl):
                nh = struct.unpack_from('<5i', b, o)[4]
                o += 132 + 84 * max(nh, 0)
            (nrec,) = struct.unpack_from('<i', b, o)
            o += 4
        seeds = []
        for _ in range(nrec):
            s, o = read_seed(b, o)
            seeds.append(s)
        recs.append(dict(kind=kind, key=(kind, region, iterDir, startSeed, nSeeds), iterDir=iterDir, seeds=seeds))
    return recs


def chain(hots, idx):
    out = []
    while idx >= 0 and len(out) < 1000:
        h = hots[idx]
        out.append((h[0], h[1]))
        idx = h[3]
    return out


def cand_eq(a, ha, b, hb):
    return (a['counters'] == b['counters'] and a['ovHit'] == b['ovHit'] and a['ovModule'] == b['ovModule'] and
            chain(ha, a['lastHitIdx']) == chain(hb, b['lastHitIdx']))


def seed_eq(a, b):
    if a['state'] != b['state'] or len(a['cands']) != len(b['cands']):
        return False
    for x, y in zip(a['cands'], b['cands']):
        if not cand_eq(x, a['hots'], y, b['hots']):
            return False
    if (a['best']['hasCC'] != 0) != (b['best']['hasCC'] != 0):
        return False
    if a['best']['hasCC'] and not cand_eq(a['best'], a['hots'], b['best'], b['hots']):
        return False
    return True


def main():
    da, db = sys.argv[1], sys.argv[2]
    fa = sorted(glob.glob(os.path.join(da, 'step_*.bin')))
    fb = sorted(glob.glob(os.path.join(db, 'step_*.bin')))
    ra = [r for f in fa for r in read_records(f)]
    rb = [r for f in fb for r in read_records(f)]
    stats = {}
    unpaired = 0
    for x, y in zip(ra, rb):
        if x['key'] != y['key']:
            unpaired += 1
            continue
        if x['kind'] == 0:
            continue
        name = ('step ' if x['kind'] == 1 else 'FINAL ') + ('forward' if x['iterDir'] == 0 else 'backward')
        ya = {s['seedIdx']: s for s in y['seeds']}
        st = stats.setdefault(name, [0, 0])
        for s in x['seeds']:
            t = ya.get(s['seedIdx'])
            st[0] += 1
            st[1] += 1 if (t is not None and seed_eq(s, t)) else 0
    print('records A %d, B %d, key mismatches %d' % (len(ra), len(rb), unpaired + abs(len(ra) - len(rb))))
    for k in sorted(stats):
        n, ok = stats[k]
        print('%-18s seeds %8d  structurally identical %8d (%.4f%%)  differing %d' % (k, n, ok, 100.0 * ok / max(n, 1),
                                                                                   n - ok))


if __name__ == '__main__':
    main()
