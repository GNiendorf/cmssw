#!/usr/bin/env python3
"""Per-tc_type attribution of the in-cut TC population: count, fakes, duplicates.

Same in-cut selection createPerfNumDenHists uses for the _eta sums (pt > 0.9,
|eta| < 4.5), so the per-type numbers ADD UP to the scoreboard's fake_overall_incut
and dup_overall_incut.  Usage: a11_types.py <file.root> [<file.root> ...]
"""
import sys
import ROOT

PTCUT, ETACUT = 0.9, 4.5


def one(path):
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    nev = t.GetEntries()
    agg = {}
    tot = [0, 0, 0]
    for i in range(nev):
        t.GetEntry(i)
        ty, pt, eta = t.tc_type, t.tc_pt, t.tc_eta
        fk, dp = t.tc_isFake, t.tc_isDuplicate
        for j in range(len(ty)):
            if pt[j] <= PTCUT or abs(eta[j]) >= ETACUT:
                continue
            a = agg.setdefault(int(ty[j]), [0, 0, 0])
            a[0] += 1
            a[1] += 1 if fk[j] else 0
            a[2] += 1 if dp[j] else 0
            tot[0] += 1
            tot[1] += 1 if fk[j] else 0
            tot[2] += 1 if dp[j] else 0
    f.Close()
    print("== %s   (%d events)" % (path.split('/')[-1], nev))
    print("  %-6s %10s %10s %10s %9s %9s" % ("type", "N/evt", "fake/evt", "dup/evt", "fakeFrac", "dupFrac"))
    for k in sorted(agg):
        a = agg[k]
        print("  %-6d %10.1f %10.2f %10.2f %9.4f %9.4f"
              % (k, a[0] / nev, a[1] / nev, a[2] / nev, a[1] / max(1, a[0]), a[2] / max(1, a[0])))
    print("  %-6s %10.1f %10.2f %10.2f %9.5f %9.5f"
          % ("ALL", tot[0] / nev, tot[1] / nev, tot[2] / nev,
             tot[1] / max(1, tot[0]), tot[2] / max(1, tot[0])))
    print()


for p in sys.argv[1:]:
    one(p)
