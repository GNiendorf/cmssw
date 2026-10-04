#!/usr/bin/env python3
"""trig_sum.py <tags file> <runs dir A> <runs dir B> [labelA labelB]: TrigReport path summaries (wantSummary) of the per-file job logs
(<runs>/ttbar/<tag>/hlt.log.gz), summed over the common files; per path: passed A, passed B, B-A, and the number of files in which
the passed count differs (paired). Also the visited-event totals (must be equal: same events)."""
import sys, re, gzip, os
tags = open(sys.argv[1]).read().split(); ra, rb = sys.argv[2], sys.argv[3]
la, lb = (sys.argv[4], sys.argv[5]) if len(sys.argv) > 5 else ("A", "B")
def paths(fn):
    d = {}; on = False
    for line in gzip.open(fn, 'rt', errors='replace'):
        if 'TrigReport ---------- Path   Summary' in line: on = True; continue
        if on and ('TrigReport -------End-Path' in line or 'TrigReport ---------- Modules' in line): on = False
        m = re.match(r'TrigReport\s+\d+\s+\d+\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\S+)', line)
        if on and m: d[m[5]] = (int(m[1]), int(m[2]))
    return d
SA, SB, ND, VIS = {}, {}, {}, [0, 0]
for t in tags:
    a = paths(f"{ra}/ttbar/{t}/hlt.log.gz"); b = paths(f"{rb}/ttbar/{t}/hlt.log.gz")
    VIS[0] += max((v[0] for v in a.values()), default=0); VIS[1] += max((v[0] for v in b.values()), default=0)
    for k in set(a) | set(b):
        pa, pb = a.get(k, (0, 0))[1], b.get(k, (0, 0))[1]
        SA[k] = SA.get(k, 0) + pa; SB[k] = SB.get(k, 0) + pb; ND[k] = ND.get(k, 0) + (pa != pb)
diff = sorted(k for k in SA if ND[k])
print(f"TriggerResults over {len(tags)} files: events visited {la} {VIS[0]} {lb} {VIS[1]}; {len(SA)} paths; "
      f"paths with a different passed count in >= 1 file: {len(diff)}; summed passed differs in {sum(1 for k in SA if SA[k] != SB[k])}")
for k in diff: print(f"  {k:62s} {la} {SA[k]:7d} {lb} {SB[k]:7d} diff {SB[k]-SA[k]:+5d} files differing {ND[k]}")
print(f"sum of passed over paths: {la} {sum(SA.values())} {lb} {sum(SB.values())}")
