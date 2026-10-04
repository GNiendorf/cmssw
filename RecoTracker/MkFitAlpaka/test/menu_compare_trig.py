#!/usr/bin/env python3
"""compare_trig.py stock.log port.log: TrigReport path summaries (wantSummary) of two cmsRun logs; paths whose
passed count differs, and the totals."""
import sys, re
def paths(fn):
    d = {}; on = False
    op = open
    if fn.endswith('.gz'):
        import gzip; op = lambda f, **k: gzip.open(f, 'rt', **k)
    for line in op(fn, errors='replace'):
        if 'TrigReport ---------- Path   Summary' in line: on = True; continue
        if on and ('TrigReport -------End-Path' in line or 'TrigReport ---------- Modules' in line): on = False
        m = re.match(r'TrigReport\s+\d+\s+\d+\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\S+)', line)
        if on and m: d[m[5]] = (int(m[1]), int(m[2]))
    return d
a, b = paths(sys.argv[1]), paths(sys.argv[2])
diff = [(k, a[k], b.get(k)) for k in a if b.get(k) != a[k]]
print('paths: stock %d port %d; events visited %s; paths with a different passed count: %d' %
      (len(a), len(b), sorted({v[0] for v in a.values()}), len(diff)))
for k, x, y in diff: print('  %-60s stock passed %6d   port passed %s' % (k, x[1], y[1] if y else 'MISSING'))
print('sum of passed over paths: stock %d port %d' % (sum(v[1] for v in a.values()), sum(v[1] for v in b.values() if v)))
