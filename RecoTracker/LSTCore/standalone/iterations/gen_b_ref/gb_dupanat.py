#!/usr/bin/env python3
"""GEN-B -- DUPLICATE ANATOMY. For a run's pT3-class rows, decompose the duplicate flags:
which TC type is the PARTNER that makes our row a duplicate, and how many partners are
themselves only duplicates because of us. Tells us exactly which claim is missing.
Usage: gb_dupanat.py <label>=<path.root> ...
"""
import sys
import collections
import ROOT

PTCUT = 0.9
TYNAME = {4: 'T5', 5: 'pT3', 7: 'pT5', 8: 'pLS', 9: 'T4'}


def anat(path, label):
    f = ROOT.TFile.Open(path)
    t = f.Get('tree')
    n = t.GetEntries()
    part = collections.Counter()
    ourrows = ourdup = 0
    extra_flags = 0
    for i in range(n):
        t.GetEntry(i)
        ty = list(t.tc_type)
        sa = t.tc_simIdxAll
        pt = list(t.tc_pt)
        sim2tc = collections.defaultdict(list)
        for j in range(len(ty)):
            for s in sa[j]:
                sim2tc[s].append(j)
        for j in range(len(ty)):
            if ty[j] != 5 or pt[j] <= PTCUT:
                continue
            ourrows += 1
            partners = set()
            for s in sa[j]:
                for k in sim2tc[s]:
                    if k != j:
                        partners.add(k)
            if partners:
                ourdup += 1
                seen = set()
                for k in partners:
                    tn = TYNAME.get(ty[k], str(ty[k]))
                    if tn not in seen:
                        part[tn] += 1
                        seen.add(tn)
    f.Close()
    print('%-12s rows/evt %7.1f  duplicate rows/evt %7.2f (%.1f%%)'
          % (label, ourrows / n, ourdup / n, 100.0 * ourdup / max(ourrows, 1)))
    for k, v in part.most_common():
        print('      partner %-4s %7.2f/evt' % (k, v / n))


for a in sys.argv[1:]:
    lab, p = a.split('=', 1)
    anat(p, lab)
