#!/usr/bin/env python3
"""One compact length line per prototype output file (pt > 0.9, harness convention).

Columns
  nhB/nhT/nhE   the harness mixture mean (what the scoreboard reports)
  gB/gT/gE      the same mean over GOOD rows only (not fake, not dup) -- the part of the
                metric this angle owns, with the dup/fake dilution divided out
  f12B          fraction of barrel LONG rows (type 4/7) that carry 12+ OT hits
  f12T f12E     same in transition / endcap
  nLongB        barrel long rows per event
  n0B           barrel 0-hit rows per event
Usage: a06_len.py <tag-or-path> [...]   (bare tags resolve under a06_ref then fin_ref)
"""
import os
import sys

import ROOT

ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError
B = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [B + 'a06_ref/r_%s.root', B + 'fin_ref/r_%s.root', B + 'xc_ref/r_%s.root']
REG = [(0.0, 1.1), (1.1, 1.7), (1.7, 99.0)]


def resolve(tag):
    if os.path.exists(tag):
        return tag
    for d in DIRS:
        p = d % tag
        if os.path.exists(p):
            return p
    return None


def run(tag):
    path = resolve(tag)
    if path is None:
        print("%-12s  (missing)" % tag)
        return
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    nev = t.GetEntries()
    n = [0] * 3
    s = [0] * 3
    gn = [0] * 3
    gs = [0] * 3
    ln = [0] * 3
    l12 = [0] * 3
    z = [0] * 3
    for i in range(nev):
        t.GetEntry(i)
        eta, pt, nh, ty = t.tc_eta, t.tc_pt, t.tc_nhitOT, t.tc_type
        isf, isd = t.tc_isFake, t.tc_isDuplicate
        for j in range(len(eta)):
            if pt[j] <= 0.9:
                continue
            a = abs(eta[j])
            r = 0 if a < 1.1 else (1 if a < 1.7 else 2)
            h = int(nh[j])
            n[r] += 1
            s[r] += h
            if not int(isf[j]) and not int(isd[j]):
                gn[r] += 1
                gs[r] += h
            if int(ty[j]) in (4, 7):
                ln[r] += 1
                if h >= 12:
                    l12[r] += 1
            if h == 0:
                z[r] += 1
    e = float(nev)
    print("%-12s nh %7.4f %7.4f %7.4f | good %7.4f %7.4f %7.4f | f12 %6.3f %6.3f %6.3f | "
          "nLongB %6.1f n0B %5.1f nTC/e %7.1f"
          % (tag, s[0] / n[0], s[1] / n[1], s[2] / n[2],
             gs[0] / gn[0], gs[1] / gn[1], gs[2] / gn[2],
             l12[0] / float(ln[0]), l12[1] / float(ln[1]), l12[2] / float(ln[2]),
             ln[0] / e, z[0] / e, sum(n) / e))
    f.Close()


for a in sys.argv[1:]:
    run(a)
