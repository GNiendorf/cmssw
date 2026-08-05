#!/usr/bin/env python3
"""Full delta table vs a reference hists file, over EVERY metric -- used to hunt for
regressions in bands the agents did not quote. Also prints the LST column so that
'above/below LST' statements can be checked per region.

Usage: v1_delta.py <ref=baseline.root> <lst=lst.root> <lab>=<file> [...]
"""
import sys
import subprocess
import json

sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/verify1_ref")

import ROOT  # noqa: E402

exec(open("/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/verify1_ref/v1_metrics.py")
     .read().split("def main()")[0])

KEYS = ["eff_ic", "effB", "effT", "effE", "dup_ic", "dupB", "dupT", "dupE",
        "fake_ic", "fakB", "fakT", "fakE", "nh", "nhB", "nhT", "nhE",
        "vxy1_5", "vxy5_10", "vxy10_30", "dxy1_5", "dxy5_10", "dxy10_30", "nTC"]

ref = metrics(sys.argv[1])
lst = metrics(sys.argv[2])
rows = [(a.split("=", 1)[0], metrics(a.split("=", 1)[1])) for a in sys.argv[3:]]

print("%-11s %10s %10s" % ("metric", "BASE", "LST") + "".join("%12s" % (l + " D") for l, _ in rows)
      + "".join("%12s" % (l + " vsLST") for l, _ in rows))
for k in KEYS:
    line = "%-11s %10.5f %10.5f" % (k, ref[k], lst[k])
    for _, m in rows:
        line += "%12.5f" % (m[k] - ref[k])
    for _, m in rows:
        line += "%12.5f" % (m[k] - lst[k])
    print(line)
