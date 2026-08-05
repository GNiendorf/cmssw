#!/usr/bin/env python3
"""VERIFIER-4 independent re-derivation.

Imports compute_metrics from the STANDARD tool (prototype/compare_ab.py) and prints a
full metric table for an arbitrary list of createPerfNumDenHists files, so that no
agent's own table or json is trusted.  Usage:

  python3 v4_tab.py LABEL=path_hists.root [LABEL=path ...]
"""
import sys
import os

sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype")
import ROOT
ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError
import compare_ab as C

ROWS = [k for k, _, _ in C.TABLE]
LAB = {k: l for k, l, _ in C.TABLE}
KIND = {k: t for k, _, t in C.TABLE}

args = []
for a in sys.argv[1:]:
    lab, path = a.split("=", 1)
    args.append((lab, path))

vals = {}
for lab, path in args:
    if not os.path.exists(path):
        print("MISSING %s -> %s" % (lab, path))
        continue
    vals[lab] = C.compute_metrics(path, lab)

labs = [l for l, _ in args if l in vals]
w = max(12, max(len(l) for l in labs) + 1)
hdr = "%-26s" % "metric" + "".join("%*s" % (w, l) for l in labs)
print(hdr)
print("-" * len(hdr))
for k in ROWS:
    line = "%-26s" % LAB[k]
    for l in labs:
        line += "%*s" % (w, C.fmt(vals[l].get(k), KIND[k]))
    print(line)
