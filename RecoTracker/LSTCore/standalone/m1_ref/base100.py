#!/usr/bin/env python3
"""Rebuild the [COORDINATOR 12:50] OURS-100 baseline from jetrecon_ref/pe1000, and emit the
per-event table the capped arms are compared against.

Offline only: it parses logs that already exist (broker-exempt). Two things come out of it:
  * the baseline aggregate over the FIRST 100 jet events, per-event-isolated, cap-off, at
    81a9afe2d00 -- the numbers the coordinator quoted, reproduced from the raw logs so the
    comparison is on a table and not on four remembered figures;
  * per-event E1/E2/nT3, which is what says which of the 100 events are over each ceiling
    (the CPU 4 GiB extent, the GPU 1 GiB allocator bin) BEFORE any run is submitted.
Writes m1_ref/base100.csv and prints the summary.
"""
import os
import re
import sys

J = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/jetrecon_ref/pe1000"
R = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/m1_ref"
N = int(sys.argv[1]) if len(sys.argv) > 1 else 100

STAGES = ["Hits", "MD", "LS", "T3", "Graph", "pLS", "Chain", "TC", "Reset", "Total", "TotalShort"]
ROW_BYTES = 21  # ChainEdgesSoA
IDX_WALL = 2 ** 32
BIN_WALL = 1 << 30

reChain = re.compile(r"\[CHAIN\] nodes\(nT3\)=(\d+) E1=(\d+) E2=(\d+) E=(\d+)")


def pct(v, q):
    if not v:
        return float("nan")
    s = sorted(v)
    return s[min(len(s) - 1, max(0, int(round(q * (len(s) - 1)))))]


rows = {}
sizes = {}
for i in range(N):
    log = os.path.join(J, f"evt{i}.log")
    err = os.path.join(J, f"evt{i}.err")
    if not os.path.exists(log):
        continue
    txt = open(log, errors="replace").read()
    if os.path.exists(err):
        txt += open(err, errors="replace").read()
    m = reChain.search(txt)
    if m:
        sizes[i] = tuple(int(x) for x in m.groups())
    for line in txt.splitlines():
        f = line.split()
        if len(f) != 12:
            continue
        try:
            vals = [float(x) for x in f]
        except ValueError:
            continue
        if vals[0] != int(vals[0]):
            continue
        rows[i] = vals[1:]  # the per-event row is keyed by stream slot, not event index

done = sorted(rows)
crashed = [i for i in range(N) if i not in rows]
print(f"# OURS-100 BASELINE rebuilt from pe1000 (81a9afe2d00, per-event isolated, cap off)")
print(f"# events {N}  completed {len(done)}  CRASHED {len(crashed)}: {crashed}")
print(f"\n{'stage':<11} {'mean':>10} {'median':>10} {'p90':>10} {'max':>10} {'sum_s':>9}")
for k, name in enumerate(STAGES):
    v = [rows[i][k] for i in done]
    print(f"{name:<11} {sum(v)/len(v):10.1f} {pct(v,0.5):10.1f} {pct(v,0.9):10.1f} {max(v):10.1f} {sum(v)/1000:9.1f}")

overIdx = [i for i, s in sizes.items() if s[3] * ROW_BYTES >= IDX_WALL]
overBin = [i for i, s in sizes.items() if s[3] * ROW_BYTES > BIN_WALL]
E = [s[3] for s in sizes.values()]
print(f"\n# graph size over the {len(sizes)} events with a [CHAIN] line")
print(f"#   E    mean {sum(E)//len(E)}  median {pct(E,0.5)}  p90 {pct(E,0.9)}  max {max(E)}")
print(f"#   sum of E over the 100 events: {sum(E)}")
print(f"# CPU 4 GiB alpaka-Idx extent (E >= {IDX_WALL//ROW_BYTES} rows): {len(overIdx)} events {sorted(overIdx)}")
print(f"# GPU 1 GiB allocator bin     (E >  {BIN_WALL//ROW_BYTES} rows): {len(overBin)} events {sorted(overBin)}")
print("#   (the crashers have no [CHAIN] line on CPU only if they die before it; check both lists)")

with open(os.path.join(R, "base100.csv"), "w") as f:
    f.write("Evt," + ",".join(STAGES) + ",nT3,E1,E2,E,edgeMB\n")
    for i in range(N):
        t = rows.get(i)
        s = sizes.get(i)
        f.write(str(i) + "," + (",".join(f"{x:.3f}" for x in t) if t else ",".join([""] * len(STAGES))))
        f.write("," + (f"{s[0]},{s[1]},{s[2]},{s[3]},{s[3]*ROW_BYTES/1e6:.1f}" if s else ",,,,") + "\n")
print(f"\n# wrote {R}/base100.csv")
