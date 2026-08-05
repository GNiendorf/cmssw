#!/usr/bin/env python3
"""GEN-C -- the DUPLICATE diagnostic the gate actually depends on.

The harness duplicate flag is computed against the FULL sim list (pileup included), not
the in-cut denominator: a TC is a duplicate when some OTHER TC matches one of the same
sims. That is why LST can deliver 151.7 pT3 rows/evt that cover only 4.3 IN-CUT sims and
still LOWER the duplicate rate -- its rows land on ~150 DISTINCT (mostly pileup) sims,
one row per track.

This script measures, as the stage-B margin is scanned, how many DISTINCT full sims our
delivered set lands on and how many of its rows are duplicates in the harness sense
(sharing a full sim with another delivered TC of the same event, ours or not).

Usage: gc_dupdiag.py <t_TAG.root> [logit-branch-name]
"""
import sys
from collections import defaultdict

import ROOT

path = sys.argv[1]
f = ROOT.TFile.Open(path)
t = f.Get('tree')
NEV = t.GetEntries()

THR = [2, 3, 4, 5, 6, 6.5, 7, 7.5, 8, 8.5, 9, 9.5, 10, 11, 12]
rows = defaultdict(int)
dups = defaultdict(int)
fakes = defaultdict(int)
distinct = defaultdict(int)
oth_rows = 0
oth_dups = 0

for i in range(NEV):
    t.GetEntry(i)
    ch = list(t.tc_isChain)
    lg = list(t.tc_attLogit)
    sims = [list(v) for v in t.tc_simIdxAll]
    ours = [j for j in range(len(ch)) if ch[j] == 3]
    other = [j for j in range(len(ch)) if ch[j] != 3]
    base = defaultdict(int)
    for j in other:
        for s in sims[j]:
            base[s] += 1
    oth_rows += len(other)
    for j in other:
        if any(base[s] > 1 for s in sims[j]):
            oth_dups += 1
    for T in THR:
        sel = [j for j in ours if lg[j] >= T]
        cnt = dict(base)
        for j in sel:
            for s in sims[j]:
                cnt[s] = cnt.get(s, 0) + 1
        rows[T] += len(sel)
        distinct[T] += len(set(s for j in sel for s in sims[j]))
        for j in sel:
            if not sims[j]:
                fakes[T] += 1
            elif any(cnt[s] > 1 for s in sims[j]):
                dups[T] += 1
f.Close()

print('%s  (%d evts)' % (path.split("/")[-1], NEV))
print('non-pT3-class rows/evt %.1f of which harness-duplicate %.1f (%.4f)'
      % (oth_rows / float(NEV), oth_dups / float(NEV), oth_dups / float(oth_rows)))
print()
print('%-6s %9s %10s %9s %9s %9s %9s' %
      ('T', 'rows/ev', 'distinct', 'rows/dist', 'dupRows', 'dupFrac', 'fakeFrac'))
for T in THR:
    r = rows[T] / float(NEV)
    d = distinct[T] / float(NEV)
    print('%-6.1f %9.1f %10.1f %9.2f %9.1f %9.4f %9.4f' %
          (T, r, d, (r / d if d else 0), dups[T] / float(NEV),
           dups[T] / float(rows[T]) if rows[T] else 0,
           fakes[T] / float(rows[T]) if rows[T] else 0))
print()
print('LST reference (same measure on its own delivered pT3 rows) is printed by')
print('gc_dupdiag_lst.py; the target is rows/distinct near 1.0.')
