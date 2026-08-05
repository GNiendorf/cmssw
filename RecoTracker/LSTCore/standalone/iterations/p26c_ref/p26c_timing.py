#!/usr/bin/env python3
"""Per-stage mean of the [CHAIN TIMING] and [CHAIN K8] lines in a run log.

usage: p26b_timing.py <log> [<log2> ...]   one column per log, plus the delta vs the first.
"""
import re
import sys
from collections import defaultdict

PAT = re.compile(r"([A-Za-z0-9+]+(?: [A-Za-z0-9+]+)*?)\s+([0-9]+\.[0-9]+) ms")
CNT = re.compile(r"([A-Za-z0-9]+)=([0-9]+)")


def parse(path):
    per = defaultdict(list)
    for line in open(path):
        for marker in ("[CHAIN TIMING]", "[CHAIN K8]"):
            if marker not in line:
                continue
            body = line.split(marker, 1)[1]
            parts = [p.strip() for p in body.split("|")]
            tag = "K8" if marker == "[CHAIN K8]" else None
            for p in parts:
                m = PAT.fullmatch(p)
                if not m:
                    if tag == "K8":
                        for c in CNT.finditer(p):
                            per["K8#" + c.group(1)].append(float(c.group(2)))
                    continue
                name, val = m.group(1), float(m.group(2))
                if tag is None:
                    tag = name.split()[0]
                per[f"{tag}::{name}"].append(val)
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
    hdr = f"{'stage':<{w}}" + "".join(f"{p.split('/')[-1]:>26}" for p in logs)
    print(hdr)
    print("-" * len(hdr))
    tot = [0.0] * len(logs)
    for k in keys:
        row = f"{k:<{w}}"
        base = None
        for i, t in enumerate(tabs):
            v = t.get(k)
            if not v:
                row += f"{'-':>26}"
                continue
            m = sum(v) / len(v)
            if not k.startswith("K8#") and not k.startswith("K8::") and not k.endswith("::total"):
                tot[i] += m
            if base is None:
                base = m
                row += f"{m:>12.3f} (n={len(v)})".rjust(26)
            else:
                row += f"{m:>12.3f} ({m - base:+8.3f})".rjust(26)
        print(row)
    print("-" * len(hdr))
    row = f"{'CHAIN BLOCK TOTAL':<{w}}"
    for i, v in enumerate(tot):
        row += (f"{v:>12.3f} (n=-)" if i == 0 else f"{v:>12.3f} ({v - tot[0]:+8.3f})").rjust(26)
    print(row)


if __name__ == "__main__":
    main()
