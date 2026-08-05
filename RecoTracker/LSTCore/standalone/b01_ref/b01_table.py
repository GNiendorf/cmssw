#!/usr/bin/env python3
"""B01 per-band WP table. Usage: b01_table.py <ref.json> <tag1.json> ...

Column 1 is the reference (CHAINFINAL). Every other column is printed as the ABSOLUTE
value and, in the delta table, as the move vs the reference. All numbers come from
compare_ab.py's json, i.e. the same harness every other agent quotes.
"""
import json
import os
import sys

ROWS = [
    ("eff", "eff_overall_incut"),
    ("effB", "eff_barrel"),
    ("effT", "eff_transition"),
    ("effE", "eff_endcap"),
    ("dup", "dup_overall_incut"),
    ("dupB", "dup_barrel"),
    ("dupT", "dup_transition"),
    ("dupE", "dup_endcap"),
    ("fake", "fake_overall_incut"),
    ("fakB", "fake_barrel"),
    ("fakT", "fake_transition"),
    ("fakE", "fake_endcap"),
    ("v15", "eff_vxy_1_5"),
    ("v510", "eff_vxy_5_10"),
    ("v1030", "eff_vxy_10_30"),
    ("d15", "eff_dxy_1_5"),
    ("d510", "eff_dxy_5_10"),
    ("nh", "mean_nhitOT"),
    ("nhB", "mean_nhitOT_barrel"),
    ("nhT", "mean_nhitOT_transition"),
    ("nhE", "mean_nhitOT_endcap"),
]


def load(p):
    with open(p) as f:
        return json.load(f)["metrics"]


paths = sys.argv[1:]
tags = [os.path.basename(p).replace("r_", "").replace(".json", "") for p in paths]
mets = [load(p) for p in paths]
base = mets[0]

w = 10
print("ABSOLUTE")
print("metric".ljust(7) + "".join(t.rjust(w) for t in tags))
for name, key in ROWS:
    line = name.ljust(7)
    for m in mets:
        v = m[key]["proto"]
        line += ("%.5f" % v if "nh" not in name else "%.3f" % v).rjust(w)
    print(line)
print()
print("DELTA vs " + tags[0])
print("metric".ljust(7) + "".join(t.rjust(w) for t in tags[1:]))
for name, key in ROWS:
    line = name.ljust(7)
    for m in mets[1:]:
        d = m[key]["proto"] - base[key]["proto"]
        line += ("%+.5f" % d if "nh" not in name else "%+.3f" % d).rjust(w)
    print(line)
print()
print("LST (base column of the same json, for reference)")
line = "LST".ljust(7)
for name, key in ROWS:
    print(("  %-7s %.5f" % (name, base[key]["base"])))
