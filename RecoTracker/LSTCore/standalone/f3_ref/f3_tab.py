#!/usr/bin/env python3
"""f3_tab.py -- delta table vs CHAINFINAL (and abs vs LST) from compare_ab jsons.

Usage: f3_tab.py <ref.json> <cand.json> [cand2.json ...]
Prints, per candidate: 5-decimal deltas vs the ref json's proto values for the
headline metrics, plus the absolute value and (value - LST base) for context.
"""
import json
import sys

KEYS = [
    ("eff", "eff_overall_incut"),
    ("effB", "eff_barrel"),
    ("effT", "eff_transition"),
    ("effE", "eff_endcap"),
    ("dup", "dup_overall_incut"),
    ("dupB", "dup_barrel"),
    ("dupT", "dup_transition"),
    ("dupE", "dup_endcap"),
    ("fak", "fake_overall_incut"),
    ("fakB", "fake_barrel"),
    ("fakT", "fake_transition"),
    ("fakE", "fake_endcap"),
    ("nh", "mean_nhitOT"),
    ("nhB", "mean_nhitOT_barrel"),
    ("nhT", "mean_nhitOT_transition"),
    ("nhE", "mean_nhitOT_endcap"),
]

ref = json.load(open(sys.argv[1]))["metrics"]
cands = sys.argv[2:]
hdr = "metric      REF(CF)      LST     " + "".join(
    "  %-26s" % (c.split("/")[-1].replace("r_", "").replace(".json", "")) for c in cands
)
print(hdr)
rows = {}
for name, key in KEYS:
    line = "%-8s %9.5f %9.5f " % (name, ref[key]["proto"], ref[key]["base"])
    for c in cands:
        m = json.load(open(c))["metrics"]
        v = m[key]["proto"]
        dcf = v - ref[key]["proto"]
        dlst = v - m[key]["base"]
        line += "  %9.5f dCF%+8.5f L%+8.5f" % (v, dcf, dlst)
    print(line)
