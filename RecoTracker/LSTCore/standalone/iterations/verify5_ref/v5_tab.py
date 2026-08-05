#!/usr/bin/env python3
"""Verifier-5 independent scoreboard. Imports compute_metrics from the SHARED,
untouched prototype/compare_ab.py (mtime 2026-08-01, predates every agent round)
and prints EVERY metric at 5 decimals for an arbitrary set of hists files.

  v5_tab.py LABEL=path/to/hists.root [LABEL2=path2 ...]
"""
import sys, os
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype")
import ROOT
ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError
from compare_ab import compute_metrics, TABLE

cols = []
for a in sys.argv[1:]:
    lab, path = a.split("=", 1)
    if not os.path.exists(path):
        print("MISSING %s -> %s" % (lab, path), file=sys.stderr)
        continue
    cols.append((lab, compute_metrics(path, lab)))

w = 13
print("%-26s" % "metric" + "".join("%*s" % (w, c[0]) for c in cols))
print("-" * (26 + w * len(cols)))
for key, label, kind in TABLE:
    row = "%-26s" % label
    for _, m in cols:
        v = m.get(key)
        if v is None:
            row += "%*s" % (w, "n/a")
        elif kind == "count":
            row += "%*d" % (w, round(v))
        else:
            row += "%*.5f" % (w, v)
    print(row)
