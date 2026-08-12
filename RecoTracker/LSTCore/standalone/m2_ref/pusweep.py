#!/usr/bin/env python3
"""Aggregate the PU200 C-sweep logs written by m2a1.sh (one log per arm, 100 events each).

usage: pusweep.py m2_ref/pu_C0.log m2_ref/pu_C4.log ...

Per arm: the `avg` row of the timing table (the batch average LST prints itself), plus the pooled
enumerated / eligible / kept edge counts from the [CHAIN TOPC] lines and the largest [MEM] Total.
PU200 is the sample the cap must not hurt, so the columns that matter here are Graph and Chain.
"""
import re
import sys

avg = re.compile(r"^\s+avg\s+" + r"\s+".join([r"([-\d.]+)"] * 10))
topc = re.compile(r"\[CHAIN TOPC\] C=(\d+) enumerated=(\d+) eligible=(\d+) kept=(\d+)")
memtot = re.compile(r"\[MEM\] Total: ([\d.]+) MB")
chains = re.compile(r"\[MEM\] Chains: (\d+) chains / (\d+) member nodes")

print("%-14s %7s %7s %7s %8s %9s %7s %7s %7s %9s %8s"
      % ("arm", "Hits", "MD", "LS", "T3", "Graph", "pLS", "Chain", "TC", "Total", "MEMmax"))
rows = {}
for p in sys.argv[1:]:
    txt = open(p, errors="ignore").read()
    m = None
    for line in txt.splitlines():
        mm = avg.match(line)
        if mm:
            m = [float(x) for x in mm.groups()]
    tc = [tuple(int(x) for x in q.groups()) for q in topc.finditer(txt)]
    mem = [float(q.group(1)) for q in memtot.finditer(txt)]
    ch = [(int(a), int(b)) for a, b in (q.groups() for q in chains.finditer(txt))]
    tag = p.split("/")[-1].replace(".log", "")
    if m is None:
        print("%-14s no avg row" % tag)
        continue
    print("%-14s %7.2f %7.2f %7.2f %8.2f %9.2f %7.2f %7.2f %7.2f %9.2f %8.1f"
          % (tag, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[9], max(mem) if mem else -1))
    rows[tag] = (tc, ch)

print()
print("%-14s %6s %14s %14s %14s %9s %12s %12s"
      % ("arm", "C", "pooled enum", "pooled eligible", "pooled kept", "kept%", "sum chains", "sum members"))
for tag, (tc, ch) in rows.items():
    if not tc:
        print("%-14s %6s (no [CHAIN TOPC] lines: cap off or -v < 2)   sum chains %s, members %s"
              % (tag, "-", sum(c for c, _ in ch), sum(n for _, n in ch)))
        continue
    en = sum(t[1] for t in tc)
    el = sum(t[2] for t in tc)
    kp = sum(t[3] for t in tc)
    print("%-14s %6d %14d %14d %14d %8.3f%% %12d %12d"
          % (tag, tc[0][0], en, el, kp, 100.0 * kp / max(1, en),
             sum(c for c, _ in ch), sum(n for _, n in ch)))
