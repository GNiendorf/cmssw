#!/usr/bin/env python3
"""Aggregate a per-event-isolated jet sweep directory (the pe1000 pattern).

usage: agg_pe.py <dir> [first] [last]

Reads <dir>/evt<i>.log (+ evt<i>.err for peak RSS) and reports, over the events that
COMPLETED, the per-stage mean / median / p90 / max in ms, plus the crash census and the
chain-graph statistics ([CHAIN], [MEM], [CHAIN TOPC]).  This is the exact aggregation the
[COORDINATOR 12:50] baseline quotes, so arms can be differenced against it directly.
"""
import os
import re
import sys
import statistics as st

# The 11 columns of the per-event "Evt" row.  Use THAT row, not the "avg" row: the
# [COORDINATOR 12:50] baseline (mean 3621 / median 1396 / p90 9075 / max 33630 over the 98
# completed of the first 100) is reproduced digit for digit from the Evt row's Total column,
# while the avg row's Total runs ~1% higher.  Verified against jetrecon_ref/pe1000.
STAGES = ["Hits", "MD", "LS", "T3", "Graph", "pLS", "Chain", "TC", "Reset", "Total", "Total(short)"]

d = sys.argv[1]
first = int(sys.argv[2]) if len(sys.argv) > 2 else 0
last = int(sys.argv[3]) if len(sys.argv) > 3 else 99

avg = re.compile(r"^\s+\d+\s+" + r"\s+".join([r"([-\d.]+)"] * 11) + r"\s*$")
rss = re.compile(r"Maximum resident set size \(kbytes\):\s+(\d+)")
chain = re.compile(r"\[CHAIN\] nodes\(nT3\)=(\d+) E1=(\d+) E2=(\d+) E=(\d+)")
topc = re.compile(r"\[CHAIN TOPC\] C=(\d+) enumerated=(\d+) eligible=(\d+) kept=(\d+)")
memtot = re.compile(r"\[MEM\] Total: ([\d.]+) MB")

rows, peak, crash, ch, tc, mem = [], [], [], [], [], []
for i in range(first, last + 1):
    p = os.path.join(d, "evt%d.log" % i)
    if not os.path.exists(p):
        crash.append((i, "nolog"))
        continue
    txt = open(p, errors="ignore").read()
    m = avg.search(txt, 0) or None
    got = None
    for line in txt.splitlines():
        mm = avg.match(line)
        if mm:
            got = [float(x) for x in mm.groups()]
    if got is None:
        crash.append((i, "noavg"))
        continue
    rows.append((i, got))
    m = chain.search(txt)
    if m:
        ch.append((i,) + tuple(int(x) for x in m.groups()))
    m = topc.search(txt)
    if m:
        tc.append((i,) + tuple(int(x) for x in m.groups()))
    m = memtot.search(txt)
    if m:
        mem.append((i, float(m.group(1))))
    e = os.path.join(d, "evt%d.err" % i)
    if os.path.exists(e):
        m = rss.search(open(e, errors="ignore").read())
        if m:
            peak.append((i, int(m.group(1)) / 1024.0))


def q(v, f):
    v = sorted(v)
    return v[min(len(v) - 1, int(f * len(v)))]


print("dir=%s  events %d..%d : completed %d, missing/crashed %d %s"
      % (d, first, last, len(rows), len(crash), [c[0] for c in crash]))
print("%-8s %10s %10s %10s %10s" % ("stage", "mean", "median", "p90", "max"))
for k, name in enumerate(STAGES):
    v = [r[1][k] for r in rows]
    print("%-8s %10.1f %10.1f %10.1f %10.1f" % (name, sum(v) / len(v), st.median(v), q(v, 0.90), max(v)))
if peak:
    v = [p[1] for p in peak]
    print("%-8s %10.1f %10.1f %10.1f %10.1f   MB (peak RSS)"
          % ("RSS", sum(v) / len(v), st.median(v), q(v, 0.90), max(v)))
if mem:
    v = [m[1] for m in mem]
    print("%-8s %10.1f %10.1f %10.1f %10.1f   MB ([MEM] Total, LST buffers)"
          % ("MEMtot", sum(v) / len(v), st.median(v), q(v, 0.90), max(v)))
if ch:
    E = [c[4] for c in ch]
    print("enumerated E: n=%d mean %.0f median %.0f max %.0f (evt %d)"
          % (len(E), sum(E) / len(E), st.median(E), max(E), ch[E.index(max(E))][0]))
if tc:
    K = [t[4] for t in tc]
    En = [t[2] for t in tc]
    print("kept rows   : n=%d mean %.0f median %.0f max %.0f ; kept/enumerated pooled %.4f%%"
          % (len(K), sum(K) / len(K), st.median(K), max(K), 100.0 * sum(K) / max(1, sum(En))))
    print("C=%d" % tc[0][1])
