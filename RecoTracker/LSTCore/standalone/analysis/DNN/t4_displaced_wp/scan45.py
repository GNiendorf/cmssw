#!/usr/bin/env python3
"""Rung 4: track-level replay of candidate rules.  A track passes when any of its TRUE T4 pair records passes the T4 rule
or any of its TRUE T5 pair records passes the T5 rule.  Denominator: C4 tracks with >= 1 true T4 pair record
(criterion: >= 1 such record with all four MDs at local pT >= 0.8).
usage: scan45.py <t4.npz> <t5.npz>      READ-ONLY (stdout)"""
import sys

import numpy as np

import rules

BINS = [("2.5-10", 2.5, 10), ("10-25", 10, 25), ("band", 2.5, 30), ("25-37.2", 25, 37.2), ("37.2-52.4", 37.2, 52.4)]


def load(p):
    d = np.load(p, allow_pickle=True)
    return {k: d[k] for k in d.files}


class Tracks:
    def __init__(self, d4, d5):
        self.d4, self.d5 = d4, d5
        self.n = len(d4["trk_fp"])
        key5 = {(f, s): i for i, (f, s) in enumerate(zip(d5["trk_fp"], d5["trk_simidx"]))}
        self.map5 = np.full(len(key5), -1)
        for i, k in enumerate(zip(d4["trk_fp"], d4["trk_simidx"])):
            if k in key5:
                self.map5[key5[k]] = i
        self.true4 = d4["tk"] >= 0
        self.true5 = (d5["tk"] >= 0) & (self.map5[np.maximum(d5["tk"], 0)] >= 0)
        self.crit = np.zeros(self.n, bool)
        np.logical_or.at(self.crit, d4["tk"][self.true4 & np.all(d4["lp"] >= 0.8, axis=1)], True)
        self.vxy = d4["trk_vxy"]
        self.c4 = d4["trk_C4"].astype(bool)

    def own(self, pass4, pass5):
        a = np.zeros(self.n, bool); b = np.zeros(self.n, bool)
        np.logical_or.at(a, self.d4["tk"][self.true4 & pass4], True)
        np.logical_or.at(b, self.map5[self.d5["tk"][self.true5 & pass5]], True)
        return a, b

    def row(self, label, pass4, pass5, crit=True):
        a, b = self.own(pass4, pass5)
        cells = []
        for _, lo, hi in BINS:
            m = self.c4 & (self.vxy >= lo) & (self.vxy < hi) & (self.crit if crit else True)
            cells.append("%.4f (%.3f / %.3f)" % ((a | b)[m].mean(), a[m].mean(), b[m].mean()))
        return "| %s | %s |" % (label, " | ".join(cells))


def main():
    d4, d5 = load(sys.argv[1]), load(sys.argv[2])
    T = Tracks(d4, d5)
    r4, r5 = d4["rec"], d5["rec"]
    cur4 = rules.allpass(rules.t4_parts(r4)); cur5 = rules.allpass(rules.t5_parts(r5))
    print("criterion denominator; cell = T4 or T5 (T4 / T5)")
    print("| rule | " + " | ".join(b for b, _, _ in BINS) + " |")
    print("|---|" + "---|" * len(BINS))
    print(T.row("current (replay)", cur4, cur5))
    print(T.row("made (SoA)", r4["objIdx"] >= 0, r5["objIdx"] >= 0))
    # one component of the T4 rule removed at a time
    p4 = rules.t4_parts(r4)
    for k in p4:
        q = dict(p4); q[k] = np.ones(len(r4), bool)
        print(T.row("T4 without " + k, rules.allpass(q), cur5))
    p5 = rules.t5_parts(r5)
    for k in p5:
        if k == "start":
            continue
        q = dict(p5); q[k] = np.ones(len(r5), bool)
        print(T.row("T5 without " + k, cur4, rules.allpass(q)))
    q4 = dict(p4); q4["flag"] = np.ones(len(r4), bool); q4["dnn"] = np.ones(len(r4), bool)
    print(T.row("T4 without flag+dnn", rules.allpass(q4), cur5))
    q5 = dict(p5); q5["flag"] = np.ones(len(r5), bool); q5["dnn"] = np.ones(len(r5), bool)
    print(T.row("T4 w/o flag+dnn, T5 w/o flag+dnn", rules.allpass(q4), rules.allpass(q5)))
    none4 = np.ones(len(r4), bool); none5 = p5["start"]
    print(T.row("no cut at all (ceiling of this stage)", none4, none5))
    print("\nALL denominator")
    print("| rule | " + " | ".join(b for b, _, _ in BINS) + " |")
    print("|---|" + "---|" * len(BINS))
    print(T.row("current (replay)", cur4, cur5, crit=False))
    print(T.row("no cut at all", none4, none5, crit=False))


if __name__ == "__main__":
    main()
