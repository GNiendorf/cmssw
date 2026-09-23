#!/usr/bin/env python3
"""Compare summarize.py outputs on the events common to all arms (joined by sim fingerprint).
usage: compare.py <ctx> <ref arm> <arm> [<arm> ...]   (reads deploy/sum/<arm>_<ctx>.json)"""
import json
import math
import os
import sys

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "deploy/sum")
ctx, arms = sys.argv[1], sys.argv[2:]
S = {a: json.load(open(f"{D}/{a}_{ctx}.json")) for a in arms}
common = set.intersection(*(set(s) for s in S.values()))
print(f"{ctx}: {len(common)} common events (" + ", ".join(f"{a} {len(s)}" for a, s in S.items()) + ")")
tot = {a: {k: sum(S[a][e][k] for e in common) for k in S[a][next(iter(common))]} for a in arms}
bands = [k[4:] for k in tot[arms[0]] if k.startswith("den_")]


def ratio(n, d):
    p = n / max(d, 1)
    return p, math.sqrt(max(p * (1 - p), 0) / max(d, 1))


rows = [(f"eff vxy {b.replace('_', '-')} cm", f"num_{b}", f"den_{b}") for b in bands] + \
       [("fake rate", "fake", "tc"), ("dup rate", "dup", "tc")] + \
       [(f"fake rate {t}", f"fake_{t}", f"tc_{t}") for t in ("T5", "pT5", "pT3", "pLS", "T4")]
print(f"{'':22s}" + "".join(f"{a:>22s}" for a in arms))
for lab, n, d in rows:
    line = f"{lab:22s}"
    p0, _ = ratio(tot[arms[0]][n], tot[arms[0]][d])
    for i, a in enumerate(arms):
        p, e = ratio(tot[a][n], tot[a][d])
        line += f"{p:10.4f}+-{e:.4f}" + (f" {p - p0:+.4f}" if i else " " * 8)
    print(line)
print(f"{'TCs per event':22s}" + "".join(f"{tot[a]['tc'] / len(common):22.1f}" for a in arms))
for t in ("T5", "pT5", "pT3", "pLS", "T4"):
    print(f"{t + ' TCs per event':22s}" + "".join(f"{tot[a]['tc_' + t] / len(common):22.1f}" for a in arms))
