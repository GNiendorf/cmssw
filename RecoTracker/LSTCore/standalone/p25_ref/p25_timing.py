#!/usr/bin/env python3
"""Per-stage mean of the LST_CHAIN_TIMING lines in a run log.

usage: p25_timing.py <log> [<log2> ...]      one column per log, plus the delta vs the first.
"""
import re
import sys
from collections import defaultdict

PAT = re.compile(r"([A-Za-z0-9+]+(?: [A-Za-z0-9+]+)*?)\s+([0-9]+\.[0-9]+) ms")


def parse(path):
    per = defaultdict(list)
    nlines = defaultdict(int)
    for line in open(path):
        if "[CHAIN TIMING]" not in line:
            continue
        body = line.split("[CHAIN TIMING]", 1)[1]
        # the "total" label repeats across the four timing lines: prefix it with the line's first
        # stage name so the four totals stay distinct.
        parts = [p.strip() for p in body.split("|")]
        tag = None
        for p in parts:
            m = PAT.fullmatch(p)
            if not m:
                continue
            name, val = m.group(1), float(m.group(2))
            if tag is None:
                tag = name.split()[0]
            key = f"{tag}::{name}"
            per[key].append(val)
            nlines[key] += 1
    return per


def main():
    logs = sys.argv[1:]
    tabs = [parse(p) for p in logs]
    keys = []
    for t in tabs:
        for k in t:
            if k not in keys:
                keys.append(k)
    w = max(len(k) for k in keys) + 2
    hdr = f"{'stage':<{w}}" + "".join(f"{p.split('/')[-1]:>22}" for p in logs)
    print(hdr)
    print("-" * len(hdr))
    for k in keys:
        row = f"{k:<{w}}"
        base = None
        for t in tabs:
            v = t.get(k)
            if not v:
                row += f"{'-':>22}"
                continue
            m = sum(v) / len(v)
            if base is None:
                base = m
                row += f"{m:>13.3f} ms(n={len(v)})"[:22].rjust(22)
            else:
                row += f"{m:>10.3f} ms ({m-base:+.3f})".rjust(22)
        print(row)


if __name__ == "__main__":
    main()
