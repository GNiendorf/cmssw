#!/usr/bin/env python3
"""Verify the regenerated chunks before merging.

Checks that the per-chunk (run, lumi, evt) key sets are pairwise DISJOINT and reports the
union against the reference read order (read_order_1000_keys.txt, built from the tracking
ntuple in looper order). Prints the missing read indices so a partial sample is never
shipped silently.

Usage: verify_chunks.py
"""
import glob
import os
import sys
import uproot

REF = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/rebase_ref'
GEN = os.path.join(REF, 'gen')

order = {}
with open(os.path.join(REF, 'read_order_1000_keys.txt')) as fh:
    for line in fh:
        i, r, l, e = line.split()
        order[(int(r), int(l), int(e))] = int(i)
print('reference read set: %d events' % len(order))

sets = {}
for p in sorted(glob.glob(os.path.join(GEN, '*.root'))):
    try:
        t = uproot.open(p)['tree']
    except Exception:
        print('  SKIP (stalled / still writing): %s' % os.path.basename(p))
        continue
    a = t.arrays(['run', 'lumi', 'evt'])
    ks = [(int(a['run'][i]), int(a['lumi'][i]), int(a['evt'][i])) for i in range(len(a))]
    if len(ks) != len(set(ks)):
        print('  !! %s has INTERNAL duplicates (%d rows, %d unique)' % (os.path.basename(p), len(ks), len(set(ks))))
    sets[os.path.basename(p)] = set(ks)

names = sorted(sets)
bad = 0
for i in range(len(names)):
    for j in range(i + 1, len(names)):
        ov = sets[names[i]] & sets[names[j]]
        if ov:
            bad += 1
            print('  !! OVERLAP %s & %s : %d keys' % (names[i], names[j], len(ov)))
union = set()
for n in names:
    union |= sets[n]
total_rows = sum(len(sets[n]) for n in names)
print()
for n in names:
    print('  %-42s %4d' % (n, len(sets[n])))
print()
print('rows %d, unique keys %d, pairwise overlaps %d' % (total_rows, len(union), bad))
unknown = union - set(order)
missing = set(order) - union
print('keys outside the reference read set: %d' % len(unknown))
print('MISSING from the reference read set: %d' % len(missing))
if missing:
    idx = sorted(order[k] for k in missing)
    print('missing read indices:', ' '.join(str(i) for i in idx))
    with open(os.path.join(REF, 'missing_read_indices.txt'), 'w') as fh:
        for i in idx:
            fh.write('%d\n' % i)
sys.exit(0 if (bad == 0 and not unknown) else 1)
