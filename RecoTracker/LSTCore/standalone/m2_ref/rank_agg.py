#!/usr/bin/env python3
"""Aggregate the M2 [CHAIN RANK] census lines.

R = max over every node, direction, sweep and EVENT of the rank (1-based) that the weld's
argmax reaches inside that node's own descending list of eligible incident edges. C >= R is
the sufficient condition for the per-node top-C cap to be bit-identical (see the K4 block in
src/alpaka/ChainEdges.h).
"""
import re
import sys
from collections import defaultdict

pat = re.compile(
    r"\[CHAIN RANK\] sweep=(\d+) maxRankOut=(\d+) maxRankIn=(\d+) slotsDeeperThan1=(\d+) "
    r"over8=(\d+) over16=(\d+) over32=(\d+) over64=(\d+)")

per_sweep = defaultdict(lambda: dict(mo=0, mi=0, live=0, o8=0, o16=0, o32=0, o64=0, n=0))
gmax = 0
nlines = 0
for path in sys.argv[1:]:
    with open(path, errors="ignore") as f:
        for line in f:
            m = pat.search(line)
            if not m:
                continue
            nlines += 1
            s = int(m.group(1))
            mo, mi, live, o8, o16, o32, o64 = (int(m.group(i)) for i in range(2, 9))
            d = per_sweep[s]
            d["mo"] = max(d["mo"], mo)
            d["mi"] = max(d["mi"], mi)
            d["live"] += live
            d["o8"] += o8
            d["o16"] += o16
            d["o32"] += o32
            d["o64"] += o64
            d["n"] += 1
            gmax = max(gmax, mo, mi)

if not nlines:
    print("no [CHAIN RANK] lines found")
    sys.exit(1)

print(f"records={nlines}  ({per_sweep[0]['n']} events x {len(per_sweep)} sweeps)")
print(f"{'sweep':>5} {'maxRankOut':>11} {'maxRankIn':>10} {'slots>rank1':>12} "
      f"{'>8':>8} {'>16':>8} {'>32':>8} {'>64':>8}")
for s in sorted(per_sweep):
    d = per_sweep[s]
    print(f"{s:>5} {d['mo']:>11} {d['mi']:>10} {d['live']:>12} "
          f"{d['o8']:>8} {d['o16']:>8} {d['o32']:>8} {d['o64']:>8}")
tot = sum(d["live"] for d in per_sweep.values())
o = {k: sum(d[k] for d in per_sweep.values()) for k in ("o8", "o16", "o32", "o64")}
print(f"\nR (max argmax rank over everything) = {gmax}")
print(f"slots deeper than rank 1: {tot}; "
      f"deeper than 8: {o['o8']}; 16: {o['o16']}; 32: {o['o32']}; 64: {o['o64']}")
print("=> C >= R is bit-identical by the induction argument; any C below the '>C' column above "
      "has at least that many chances to differ.")
