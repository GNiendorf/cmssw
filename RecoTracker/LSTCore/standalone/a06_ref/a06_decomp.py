#!/usr/bin/env python3
"""Decompose mean tc_nhitOT (the harness's track-length metric) by TC class.

The harness metric is  mean_nhitOT = sum(tc_nhitOT)/nTC  over EVERY TC row that passes the
harness pt cut (the ol_*_eta histograms carry it), fakes and duplicates included. So it is
a MIXTURE mean: bare pixel rows contribute 0, pT3-class rows 6, chain / pT5-class rows
their real OT hit count. This script prints, per file and per eta region, the population
share and the mean nhitOT of each class, so a length deficit can be attributed to
COMPOSITION (how many 0-hit and 6-hit rows) vs to the long objects themselves.

Usage: a06_decomp.py [--ptcut X] <file.root> [<file2.root> ...]
"""
import sys

import ROOT

ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError

REGIONS = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 99.0), ("ALL", 0.0, 99.0)]
TYPENAME = {4: "T5", 5: "pT3cls", 7: "pT5cls", 8: "pLS", 9: "T4"}
DELIVNAME = {0: "carried", 1: "chain", 2: "attachT5", 3: "attachT3", 4: "zp8add", -1: "-"}


def analyse(path, ptcut):
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    if not t:
        print("%s: no tree" % path)
        return
    nev = t.GetEntries()
    acc = {}
    tot = {r: [0, 0] for r, _, _ in REGIONS}
    # fake / dup split of the whole population, per region
    fd = {r: [0, 0, 0, 0, 0, 0] for r, _, _ in REGIONS}  # nFake,sFake,nDup,sDup,nGood,sGood
    for i in range(nev):
        t.GetEntry(i)
        eta = t.tc_eta
        pt = t.tc_pt
        nh = t.tc_nhitOT
        ty = t.tc_type
        isf = t.tc_isFake
        isd = t.tc_isDuplicate
        try:
            dv = t.tc_isChain
            if len(dv) != len(eta):
                dv = None
        except Exception:
            dv = None
        for j in range(len(eta)):
            if pt[j] <= ptcut:
                continue
            a = abs(eta[j])
            h = int(nh[j])
            k = (int(ty[j]), int(dv[j]) if dv is not None else -1)
            for rn, lo, hi in REGIONS:
                if not (lo <= a < hi):
                    continue
                e = acc.setdefault((rn, k), [0, 0])
                e[0] += 1
                e[1] += h
                tot[rn][0] += 1
                tot[rn][1] += h
                g = fd[rn]
                if int(isf[j]):
                    g[0] += 1
                    g[1] += h
                elif int(isd[j]):
                    g[2] += 1
                    g[3] += h
                else:
                    g[4] += 1
                    g[5] += h
    print("=" * 104)
    print("FILE %s   (%d events, ptcut %.2f)" % (path, nev, ptcut))
    for rn, _, _ in REGIONS:
        N, S = tot[rn]
        if N == 0:
            continue
        print("-- region %-11s  nTC/evt %8.2f   mean nhitOT %8.5f" % (rn, N / float(nev), S / float(N)))
        rows = sorted([(k, v) for (r, k), v in acc.items() if r == rn], key=lambda x: -x[1][0])
        for (ty, dv), (n, s) in rows:
            print("     type %-8s deliv %-9s n/evt %8.2f  share %6.2f%%  meanOT %7.3f  contrib %7.4f"
                  % (TYPENAME.get(ty, str(ty)), DELIVNAME.get(dv, str(dv)),
                     n / float(nev), 100.0 * n / N, s / float(n), s / float(N)))
        g = fd[rn]
        for lab, n, s in (("fake", g[0], g[1]), ("dup", g[2], g[3]), ("good", g[4], g[5])):
            if n:
                print("     [%-4s]                     n/evt %8.2f  share %6.2f%%  meanOT %7.3f  contrib %7.4f"
                      % (lab, n / float(nev), 100.0 * n / N, s / float(n), s / float(N)))
    f.Close()


if __name__ == "__main__":
    args = sys.argv[1:]
    ptcut = 0.9
    if args and args[0] == "--ptcut":
        ptcut = float(args[1])
        args = args[2:]
    for p in args:
        analyse(p, ptcut)
