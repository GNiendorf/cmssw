#!/usr/bin/env python3
"""One-line delivery ledger per tag, scraped from the run logs."""
import re, sys
P = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a05_ref/a_%s.log'
print('%-12s %10s %10s %10s %10s %10s' % ('tag', 'cand/evt', 'pixRev', 'otRev', 'DELIV', 'qualBlk'))
for t in sys.argv[1:]:
    try:
        s = open(P % t).read()
    except OSError:
        print('%-12s (no log)' % t); continue
    d = re.search(r'DELIVERED=\d+ \(([\d.]+)/evt\)', s)
    p = re.search(r'PIXEL side .*?revoked=\d+ \(([\d.]+)/evt\)', s)
    o = re.search(r'OT side .*?revoked=\d+ \(([\d.]+)/evt\)', s)
    c = re.search(r'before any dedup=\d+ \(([\d.]+)/evt\)', s)
    q = re.search(r'blocked=\d+ \(([\d.]+)/evt\)', s)
    g = lambda m: m.group(1) if m else '-'
    print('%-12s %10s %10s %10s %10s %10s' % (t, g(c), g(p), g(o), g(d), g(q)))
