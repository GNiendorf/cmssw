#!/usr/bin/env python3
"""gpumem_table.py <timing out dir>...: device memory of the benchmark's own cmsRun processes (gpumem.sh samples, every 2 s):
peak and median of the summed MiB while all 4 jobs held memory (D7-f: target <= stock + 10% at the CI shape)."""
import sys, os, statistics
print("%-28s %8s %10s %12s %8s" % ("run", "samples", "peak MiB", "median MiB*", "procs"))
for d in sys.argv[1:]:
    f = os.path.join(d, "gpumem.txt")
    if not os.path.exists(f): print("%-28s missing" % os.path.basename(d)); continue
    rows = [l.split() for l in open(f) if l.strip()]
    v = [(int(r[2]), int(r[4])) for r in rows if len(r) > 4]
    full = [m for m, n in v if n == max(n for _, n in v)] if v else []
    print("%-28s %8d %10d %12d %8d" % (os.path.basename(d), len(v), max((m for m, _ in v), default=0), statistics.median(full) if full else 0, max((n for _, n in v), default=0)))
print("* median over the samples where the maximum number of the benchmark's processes held device memory")
