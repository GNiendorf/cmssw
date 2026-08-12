#!/usr/bin/env python3
"""Aggregate per-event-isolated jet arms and compare them to the OURS-100 baseline.

    pe100.py <dir> [<dir> ...]        # each dir holds evt<i>.log / evt<i>.err for i in 0..99

Prints, per arm: the stage table (mean / median / p90 / max / sum), the completion census, and the
edge census (E under the cap vs Euncapped, straight out of the `[CHAIN]` lines the binary now
prints). Then the delta against m1_ref/base100.csv on the events the BASELINE completed and on the
events BOTH completed -- two denominators, because the whole point of the guard is that the arms
complete events the baseline did not, and a mean over a larger event set that now includes the two
most expensive events in the sample is not comparable to a mean that excluded them.
"""
import csv
import os
import re
import sys

R = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/m1_ref"
STAGES = ["Hits", "MD", "LS", "T3", "Graph", "pLS", "Chain", "TC", "Reset", "Total", "TotalShort"]
reFam = re.compile(r"\[CHAIN\] (MD/E1|LS/E2):.* E=(\d+) Euncapped=(\d+) cap=(\d+)")
reNodes = re.compile(r"\[CHAIN\] nodes\(nT3\)=(\d+) E1=(\d+) E2=(\d+) E=(\d+)")


def pct(v, q):
    if not v:
        return float("nan")
    s = sorted(v)
    return s[min(len(s) - 1, max(0, int(round(q * (len(s) - 1)))))]


def table(tag, rows, keys):
    keys = [k for k in sorted(keys) if k in rows]
    print(f"  {tag}: n={len(keys)}")
    print(f"    {'stage':<11} {'mean':>10} {'median':>10} {'p90':>10} {'max':>10} {'sum_s':>9}")
    for i, name in enumerate(STAGES):
        v = [rows[k][i] for k in keys]
        if not v:
            continue
        print(f"    {name:<11} {sum(v)/len(v):10.1f} {pct(v,0.5):10.1f} {pct(v,0.9):10.1f} "
              f"{max(v):10.1f} {sum(v)/1000:9.1f}")


def readarm(d):
    rows, edges, skips, sizes = {}, {}, set(), {}
    for i in range(100):
        p = os.path.join(d, f"evt{i}.log")
        if not os.path.exists(p):
            continue
        txt = open(p, errors="replace").read()
        e = os.path.join(d, f"evt{i}.err")
        if os.path.exists(e):
            txt += open(e, errors="replace").read()
        if "CHAIN OVERFLOW" in txt:
            skips.add(i)
        cap = tot = unc = 0
        for m in reFam.finditer(txt):
            cap = int(m.group(4))
            tot += int(m.group(2))
            unc += int(m.group(3))
        if tot or unc:
            edges[i] = (tot, unc, cap)
        m = reNodes.search(txt)
        if m:
            sizes[i] = int(m.group(1))
        for line in txt.splitlines():
            f = line.split()
            if len(f) != 12:
                continue
            try:
                vals = [float(x) for x in f]
            except ValueError:
                continue
            if vals[0] == int(vals[0]):
                rows[i] = vals[1:]
    return rows, edges, skips, sizes


base = {}
with open(os.path.join(R, "base100.csv")) as f:
    for r in csv.DictReader(f):
        if r["Total"]:
            base[int(r["Evt"])] = [float(r[s]) for s in STAGES]
print(f"BASELINE (81a9afe2d00, isolated, cap off): {len(base)} of 100 events completed; "
      f"missing = {sorted(set(range(100)) - set(base))} (SIGSEGV)")
table("baseline", base, base)

arms = {}
for d in sys.argv[1:]:
    rows, edges, skips, sizes = readarm(d)
    arms[os.path.basename(d)] = rows
    print()
    print(f"ARM {os.path.basename(d)}: {len(rows)} of 100 events produced a timing row; "
          f"[CHAIN OVERFLOW] skipped = {sorted(skips)}")
    if edges:
        cap = {e[2] for e in edges.values()}
        se, su = sum(e[0] for e in edges.values()), sum(e[1] for e in edges.values())
        print(f"  edge census over {len(edges)} events (cap={cap}): "
              f"sum E = {se:,} vs Euncapped = {su:,}  -> {su/se if se else 0:.2f}x fewer edges, "
              f"{100*(1-se/su) if su else 0:.3f}% of E removed")
        worst = max(edges.items(), key=lambda kv: kv[1][1])
        print(f"  largest uncapped event: evt {worst[0]}  E {worst[1][1]:,} -> {worst[1][0]:,} "
              f"({worst[1][1]/max(1,worst[1][0]):.1f}x), edge rows {worst[1][0]*21/1e6:.0f} MB vs "
              f"{worst[1][1]*21/1e6:.0f} MB")
    table("all completed", rows, rows)
    both = set(base) & set(rows)
    table("on the 98 the BASELINE completed", rows, both)
    print(f"  DELTA on those {len(both)} events (baseline -> arm):")
    for i, name in enumerate(STAGES):
        a = sum(base[k][i] for k in both)
        b = sum(rows[k][i] for k in both)
        print(f"    {name:<11} {a/len(both):10.1f} -> {b/len(both):10.1f} ms/evt   "
              f"speedup {a/b if b else float('inf'):6.3f}x")

if len(arms) == 2:
    (na, ra), (nb, rb) = arms.items()
    common = set(ra) & set(rb)
    print(f"\nARM-vs-ARM ({na} -> {nb}) on the {len(common)} events both completed:")
    for i, name in enumerate(STAGES):
        a = sum(ra[k][i] for k in common)
        b = sum(rb[k][i] for k in common)
        print(f"    {name:<11} {a/len(common):10.1f} -> {b/len(common):10.1f} ms/evt   "
              f"speedup {a/b if b else float('inf'):6.3f}x")
