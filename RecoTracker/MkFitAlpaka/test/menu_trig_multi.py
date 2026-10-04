#!/usr/bin/env python3
"""trig_multi.py A_logs(comma-separated) B_logs(comma-separated) [labelA labelB]: TrigReport path summaries (wantSummary) summed over
several cmsRun logs (.gz ok) per side; paths whose summed passed count differs, visited-event totals."""
import sys, re, gzip
def paths(fn):
    d = {}; on = False; op = (lambda f: gzip.open(f, 'rt', errors='replace')) if fn.endswith('.gz') else (lambda f: open(f, errors='replace'))
    for line in op(fn):
        if 'TrigReport ---------- Path   Summary' in line: on = True; continue
        if on and ('TrigReport -------End-Path' in line or 'TrigReport ---------- Modules' in line): on = False
        m = re.match(r'TrigReport\s+\d+\s+\d+\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\S+)', line)
        if on and m: d[m[5]] = (int(m[1]), int(m[2]))
    return d
def summed(lst):
    s, vis = {}, 0
    for fn in lst.split(','):
        d = paths(fn); vis += max((v[0] for v in d.values()), default=0)
        for k, v in d.items(): s[k] = s.get(k, 0) + v[1]
    return s, vis
(a, va), (b, vb) = summed(sys.argv[1]), summed(sys.argv[2]); la, lb = (sys.argv[3], sys.argv[4]) if len(sys.argv) > 4 else ('A', 'B')
diff = sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))
print(f'TriggerResults: events visited {la} {va} {lb} {vb}; paths {len(a)}/{len(b)}; paths with a different passed count: {len(diff)}')
for k in diff: print(f'  {k:62s} {la} {a.get(k)} {lb} {b.get(k)}')
print(f'sum of passed over paths: {la} {sum(a.values())} {lb} {sum(b.values())}')
