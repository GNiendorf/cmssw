#!/usr/bin/env python3
"""Per-sim co-occurrence of TC classes: for every DUPLICATE TC, what class covers the
same sim? Tells whether the excess duplicates are (bare chain + bare seed) pairs of the
same track -- i.e. failed pixel attach -- or something else.

Usage: cooc.py <file.root>
"""
import sys
from collections import Counter
import ROOT

TN = {4: "T5chain", 5: "pT3cls", 7: "seeded", 8: "barePLS", 9: "T4chain"}


def reg(eta):
    a = abs(eta)
    return "B" if a < 1.1 else ("T" if a < 1.7 else "E")


def main(path):
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    n = t.GetEntries()
    pair = Counter()      # (dup-TC class, partner class) for duplicates
    simprof = Counter()   # sorted class multiset per sim with >1 TC
    nsim_multi = 0
    nsim = 0
    for i in range(n):
        t.GetEntry(i)
        ty = list(t.tc_type)
        et = list(t.tc_eta)
        du = list(t.tc_isDuplicate)
        sia = t.tc_simIdxAll
        sim2tc = {}
        for j in range(len(ty)):
            for s in sia[j]:
                sim2tc.setdefault(s, []).append(j)
        for s, js in sim2tc.items():
            nsim += 1
            if len(js) < 2:
                continue
            nsim_multi += 1
            key = tuple(sorted(TN.get(ty[j], str(ty[j])) for j in js))
            simprof[key] += 1
            for j in js:
                if not du[j]:
                    continue
                others = sorted(set(TN.get(ty[k], str(ty[k])) for k in js if k != j))
                pair[(TN.get(ty[j], str(ty[j])) + "/" + reg(et[j]), ",".join(others))] += 1
    print("=== %s  (%d evts, %d sims with >=1 TC, %d with >1) ===" % (path.split("/")[-1], n, nsim, nsim_multi))
    print("-- duplicate TC class/region  x  classes covering the same sim (top 30, per evt) --")
    for k, v in pair.most_common(30):
        print("  %-14s covered-with %-32s %8.2f" % (k[0], k[1], v / n))
    print("-- multi-TC sim profiles (top 20, per evt) --")
    for k, v in simprof.most_common(20):
        print("  %-52s %8.2f" % ("+".join(k), v / n))


if __name__ == "__main__":
    for p in sys.argv[1:]:
        main(p)
