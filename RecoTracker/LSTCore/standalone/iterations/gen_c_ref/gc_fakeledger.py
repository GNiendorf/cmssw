#!/usr/bin/env python3
"""GEN-C -- FAKE / DUPLICATE ledger of a prototype run, decomposed by provenance.

The scoreboard reports one fake rate and one duplicate rate. To tune a class replacement
you need to know which rows carry them, so this splits every delivered TC by tc_isChain
(0 carried baseline row, 1 bare chain TC, 2 attach pT5-class, 3 attach pT3-class) and by
tc_type, and reports rows/evt, fake fraction and harness-duplicate fraction for each.
The comparison of two runs' ledgers says exactly where a fake-rate move came from.

Usage: gc_fakeledger.py <t_TAG.root> [<t_TAG.root> ...]
"""
import sys
from collections import defaultdict

import ROOT

NAMES = {0: 'carried', 1: 'chainTC', 2: 'attach-pT5', 3: 'attach-pT3'}

for path in sys.argv[1:]:
    f = ROOT.TFile.Open(path)
    t = f.Get('tree')
    n = t.GetEntries()
    rows = defaultdict(int)
    fake = defaultdict(int)
    dup = defaultdict(int)
    for i in range(n):
        t.GetEntry(i)
        ch = list(t.tc_isChain)
        ty = list(t.tc_type)
        fk = list(t.tc_isFake)
        dp = list(t.tc_isDuplicate)
        pt = list(t.tc_pt)
        for j in range(len(ch)):
            if pt[j] <= 0.9:
                continue
            k = (ch[j], ty[j])
            rows[k] += 1
            fake[k] += 1 if fk[j] else 0
            dup[k] += 1 if dp[j] else 0
    f.Close()
    tot = sum(rows.values())
    tf = sum(fake.values())
    td = sum(dup.values())
    print('=== %s  (%d evts, TCs with pt > 0.9) ===' % (path.split('/')[-1], n))
    print('%-16s %10s %10s %9s %10s %9s' % ('class', 'rows/ev', 'fakes/ev', 'fakefrc',
                                            'dups/ev', 'dupfrc'))
    for k in sorted(rows, key=lambda k: -rows[k]):
        print('%-16s %10.1f %10.2f %9.4f %10.2f %9.4f' %
              ('%s/type%d' % (NAMES.get(k[0], '?'), k[1]), rows[k] / float(n),
               fake[k] / float(n), fake[k] / float(rows[k]),
               dup[k] / float(n), dup[k] / float(rows[k])))
    print('%-16s %10.1f %10.2f %9.4f %10.2f %9.4f' %
          ('TOTAL', tot / float(n), tf / float(n), tf / float(tot),
           td / float(n), td / float(tot)))
    print()
