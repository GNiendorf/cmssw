#!/usr/bin/env python3
"""Rung 4: PREDICTED admission of the rule (replay on probe records of an arm that does not carry the rule), track level
and object level, per vxy bin.   usage: predict.py <t4.npz> <t5.npz> <parts csv> [regions]      READ-ONLY"""
import sys

import numpy as np

import rules
from scan45 import Tracks, load, BINS


def main():
    d4, d5 = load(sys.argv[1]), load(sys.argv[2])
    parts = tuple(sys.argv[3].split(","))
    regions = len(sys.argv) > 4 and sys.argv[4] == "regions"
    T = Tracks(d4, d5)
    r4, r5 = d4["rec"], d5["rec"]
    cur4 = rules.allpass(rules.t4_parts(r4)); cur5 = rules.allpass(rules.t5_parts(r5))
    new4 = rules.allpass(rules.t4_new(r4, parts, regions)); new5 = rules.allpass(rules.t5_new(r5, parts, regions))
    print("rule parts: %s%s" % (",".join(parts), " + regions" if regions else ""))
    print("replay of the CURRENT rule == existence: T4 %.6f (%d), T5 %.6f (%d)" % (np.mean(cur4 == (r4["objIdx"] >= 0)), len(r4), np.mean(cur5 == (r5["objIdx"] >= 0)), len(r5)))
    print("current objects the rule would lose: T4 %d, T5 %d" % ((cur4 & ~new4).sum(), (cur5 & ~new5).sum()))
    for crit in (True, False):
        print("\n### track level, %s; cell = T4 or T5 (T4 / T5)" % ("criterion denominator" if crit else "ALL"))
        print("| | " + " | ".join(b for b, _, _ in BINS) + " |\n|---|" + "---|" * len(BINS))
        print(T.row("current", cur4, cur5, crit))
        print(T.row("PREDICTED", new4, new5, crit))
    ok4 = np.all(d4["lp"] >= 0.8, axis=1) & T.true4
    vx = d4["trk_vxy"][np.maximum(d4["tk"], 0)]
    ok5 = np.all(d5["lp"] >= 0.8, axis=1) & (d5["tk"] >= 0) & (r5["startValid"] == 1)
    vx5 = d5["trk_vxy"][np.maximum(d5["tk"], 0)]
    print("\n### object level, criterion denominator (true pairs -> object)")
    print("| | " + " | ".join(b for b, _, _ in BINS) + " |\n|---|" + "---|" * len(BINS))
    for lab, a, m, v in (("T4 current", cur4, ok4, vx), ("T4 PREDICTED", new4, ok4, vx), ("T5 current", cur5, ok5, vx5), ("T5 PREDICTED", new5, ok5, vx5)):
        print("| %s | " % lab + " | ".join("%.4f (%d)" % (a[m & (v >= lo) & (v < hi)].mean(), (m & (v >= lo) & (v < hi)).sum()) for _, lo, hi in BINS) + " |")
    nt4 = ~T.true4; nt5 = d5["tk"] < 0
    print("\nproxy cost on the probe's not-true records: T4 x%.2f, T5 x%.2f (rung 3: such proxies were wrong by x3)" % ((new4 & nt4).sum() / max((cur4 & nt4).sum(), 1), (new5 & nt5).sum() / max((cur5 & nt5).sum(), 1)))


if __name__ == "__main__":
    main()
