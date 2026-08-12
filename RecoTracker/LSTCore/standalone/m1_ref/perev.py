#!/usr/bin/env python3
"""Per-event stage-timing distribution out of a `-v 1` standalone log, and the CSV behind it.

    perev.py <log> [<log2> ...]

The point of parsing per-event rows instead of reading the `avg` line: the two jet arms do not have
the same denominator (the guard SKIPS the events it cannot allocate, and those are the LARGEST
events), so comparing two `avg` lines compares two different event sets. This writes <log>.csv per
input and, when given two logs, the paired comparison over the events BOTH completed.
"""
import sys
import os

COLS = ["Evt", "Hits", "MD", "LS", "T3", "Graph", "pLS", "Chain", "TC", "Reset", "Total", "TotalShort"]


def parse(path):
    rows = {}
    for line in open(path, errors="replace"):
        f = line.split()
        if len(f) != 12:
            continue
        try:
            vals = [float(x) for x in f]
        except ValueError:
            continue
        if vals[0] != int(vals[0]):
            continue
        rows[int(vals[0])] = vals[1:]
    return rows


def pct(v, q):
    if not v:
        return float("nan")
    s = sorted(v)
    i = min(len(s) - 1, max(0, int(round(q * (len(s) - 1)))))
    return s[i]


def describe(tag, rows, keys=None):
    keys = sorted(rows) if keys is None else sorted(keys)
    print(f"  {tag}: n={len(keys)}")
    print(f"    {'stage':<7} {'mean':>10} {'median':>10} {'p90':>10} {'p99':>10} {'max':>10} {'sum_s':>9}")
    for i, name in enumerate(COLS[1:]):
        v = [rows[k][i] for k in keys if k in rows]
        if not v:
            continue
        print(f"    {name:<7} {sum(v)/len(v):10.2f} {pct(v,0.5):10.2f} {pct(v,0.9):10.2f} "
              f"{pct(v,0.99):10.2f} {max(v):10.2f} {sum(v)/1000.0:9.1f}")


def main():
    logs = sys.argv[1:]
    parsed = []
    for p in logs:
        rows = parse(p)
        parsed.append(rows)
        with open(p + ".csv", "w") as f:
            f.write(",".join(COLS) + "\n")
            for k in sorted(rows):
                f.write(",".join([str(k)] + [f"{x:.3f}" for x in rows[k]]) + "\n")
        print(f"{os.path.basename(p)}  ({len(rows)} events, csv written)")
        describe("all events", rows)
    if len(parsed) == 2:
        common = set(parsed[0]) & set(parsed[1])
        print(f"\n  PAIRED over the {len(common)} events BOTH arms completed "
              f"(A only: {len(set(parsed[0]) - common)}, B only: {len(set(parsed[1]) - common)}):")
        for tag, rows in zip(logs, parsed):
            describe(f"paired {os.path.basename(tag)}", rows, common)
        for i, name in enumerate(COLS[1:]):
            a = sum(parsed[0][k][i] for k in common)
            b = sum(parsed[1][k][i] for k in common)
            r = (a / b) if b else float("inf")
            print(f"    {name:<7} sum {a/1000.0:9.1f} s -> {b/1000.0:9.1f} s   speedup {r:6.3f}x")


main()
