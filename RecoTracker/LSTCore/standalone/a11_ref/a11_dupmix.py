#!/usr/bin/env python3
"""Duplicate PATTERN attribution: for every sim matched by >1 in-cut TC, report the
multiset of tc_type values sharing it.  Answers 'which pairs of row classes duplicate
each other', which per-type counts alone cannot.

Usage: a11_dupmix.py <file.root> [<file.root> ...]
"""
import sys
from collections import Counter
import ROOT

PTCUT, ETACUT = 0.9, 4.5
NAME = {4: "T5", 5: "pT3", 7: "pT5", 8: "pLS", 9: "T4"}


def one(path):
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    nev = t.GetEntries()
    pat = Counter()          # sorted type tuple -> n sims
    extra = Counter()        # sorted type tuple -> extra rows beyond the first
    nsim_dup = 0
    for i in range(nev):
        t.GetEntry(i)
        ty, pt, eta = t.tc_type, t.tc_pt, t.tc_eta
        sia = t.tc_simIdxAll
        bysim = {}
        for j in range(len(ty)):
            if pt[j] <= PTCUT or abs(eta[j]) >= ETACUT:
                continue
            for s in sia[j]:
                bysim.setdefault(int(s), []).append(int(ty[j]))
        for s, tl in bysim.items():
            if len(tl) < 2:
                continue
            nsim_dup += 1
            key = tuple(sorted(tl))
            pat[key] += 1
            extra[key] += len(tl) - 1
    f.Close()
    print("== %s  (%d evts)  sims with >1 in-cut TC: %.1f/evt" % (path.split('/')[-1], nev, nsim_dup / nev))
    print("  %-30s %10s %10s" % ("type multiset", "sims/evt", "extraTC/evt"))
    for k, v in extra.most_common(14):
        lbl = "+".join(NAME.get(x, str(x)) for x in k)
        print("  %-30s %10.2f %10.2f" % (lbl, pat[k] / nev, v / nev))
    print("  %-30s %10.2f %10.2f" % ("TOTAL", nsim_dup / nev, sum(extra.values()) / nev))
    print()


for p in sys.argv[1:]:
    one(p)
