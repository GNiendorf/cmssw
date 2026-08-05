#!/usr/bin/env python3
"""nhitOT DISTRIBUTION per TC class per eta region (pt > 0.9, the harness cut).

Tells whether a track-length deficit is a uniform shift or a missing tail.
Usage: a06_hist.py <file.root> [...]
"""
import sys
from collections import Counter

import ROOT

ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError

REGIONS = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 99.0)]
TYPENAME = {4: "T5", 5: "pT3cls", 7: "pT5cls", 8: "pLS", 9: "T4"}


def analyse(path, ptcut=0.9):
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    nev = t.GetEntries()
    h = {}
    for i in range(nev):
        t.GetEntry(i)
        eta, pt, nh, ty = t.tc_eta, t.tc_pt, t.tc_nhitOT, t.tc_type
        for j in range(len(eta)):
            if pt[j] <= ptcut:
                continue
            a = abs(eta[j])
            for rn, lo, hi in REGIONS:
                if lo <= a < hi:
                    h.setdefault((rn, int(ty[j])), Counter())[int(nh[j])] += 1
                    break
    print("=" * 100)
    print("FILE %s (%d evts)" % (path, nev))
    for rn, _, _ in REGIONS:
        for ty in (7, 4, 9, 5, 8):
            c = h.get((rn, ty))
            if not c:
                continue
            tot = sum(c.values())
            mean = sum(k * v for k, v in c.items()) / float(tot)
            body = "  ".join("%d:%5.1f%%" % (k, 100.0 * c[k] / tot) for k in sorted(c) if c[k] / float(tot) > 0.004)
            print("  %-11s %-7s n/evt %7.2f mean %6.3f | %s" % (rn, TYPENAME.get(ty, ty), tot / float(nev), mean, body))
    f.Close()


if __name__ == "__main__":
    for p in sys.argv[1:]:
        analyse(p)
