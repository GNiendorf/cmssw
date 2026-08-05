#!/usr/bin/env python3
"""Where do the SHORT long-objects sit? f12 (fraction of type-4/7 rows with >= 12 OT hits)
split by pt band and by harness verdict, per eta region. Usage: a06_f12.py <file> [...]"""
import sys

import ROOT

ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError
REG = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 99.0)]
PT = [(0.9, 1.5), (1.5, 3.0), (3.0, 10.0), (10.0, 1e9)]


def run(path):
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    nev = t.GetEntries()
    byPt = {}
    byV = {}
    for i in range(nev):
        t.GetEntry(i)
        eta, pt, nh, ty = t.tc_eta, t.tc_pt, t.tc_nhitOT, t.tc_type
        isf, isd = t.tc_isFake, t.tc_isDuplicate
        for j in range(len(eta)):
            if pt[j] <= 0.9 or int(ty[j]) not in (4, 7):
                continue
            a = abs(eta[j])
            r = "barrel" if a < 1.1 else ("transition" if a < 1.7 else "endcap")
            long12 = 1 if int(nh[j]) >= 12 else 0
            for lo, hi in PT:
                if lo <= pt[j] < hi:
                    e = byPt.setdefault((r, lo), [0, 0])
                    e[0] += 1
                    e[1] += long12
                    break
            v = "fake" if int(isf[j]) else ("dup" if int(isd[j]) else "good")
            e = byV.setdefault((r, v), [0, 0])
            e[0] += 1
            e[1] += long12
    print("FILE %s (%d evts)  -- f12 of type 4/7 rows" % (path, nev))
    for r, _, _ in REG:
        pieces = []
        for lo, hi in PT:
            e = byPt.get((r, lo))
            if e and e[0]:
                pieces.append("pt%.1f-%s %.3f(n/e %5.1f)" % (lo, ("inf" if hi > 1e8 else "%.0f" % hi),
                                                             e[1] / float(e[0]), e[0] / float(nev)))
        print("  %-11s %s" % (r, "  ".join(pieces)))
        pieces = []
        for v in ("good", "dup", "fake"):
            e = byV.get((r, v))
            if e and e[0]:
                pieces.append("%s %.3f(n/e %5.1f)" % (v, e[1] / float(e[0]), e[0] / float(nev)))
        print("  %-11s %s" % ("", "  ".join(pieces)))
    f.Close()


for p in sys.argv[1:]:
    run(p)
