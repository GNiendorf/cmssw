#!/usr/bin/env python3
"""Convert the census JSON ledger into the flat targets file the binary trace reads."""
import json
import sys

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/t4trace"
led = json.load(open(sys.argv[1] if len(sys.argv) > 1 else P + "/t4_targets.json"))
out = sys.argv[2] if len(sys.argv) > 2 else P + "/t4_targets.txt"
n = 0
with open(out, "w") as fh:
    for r in led:
        if "t3a" not in r:
            print("SKIP (no t3 rows):", r)
            continue
        fh.write("%d %d %d %d %d\n" % (r["iev"], r["sim"], r["type"], r["t3a"], r["t3b"]))
        n += 1
print("wrote %s with %d targets" % (out, n))
