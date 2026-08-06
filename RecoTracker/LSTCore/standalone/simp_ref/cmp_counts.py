#!/usr/bin/env python3
"""Count-level comparison of two LST ntuples (GPU gate).

Per event, compares the multiset of (tc_type, tc_pt, tc_eta, tc_phi) rows.
Reports per-type totals for both files, and the number of rows present in one
file but not the other (drift), as a fraction of the reference total.

Usage: cmp_counts.py <ref.root> <new.root>
"""
import sys
from collections import Counter

import uproot

ref_path, new_path = sys.argv[1], sys.argv[2]
tref = uproot.open(ref_path)["tree"]
tnew = uproot.open(new_path)["tree"]

branches = ["tc_type", "tc_pt", "tc_eta", "tc_phi"]
aref = tref.arrays(branches)
anew = tnew.arrays(branches)

if len(aref) != len(anew):
    print(f"FATAL: event count differs {len(aref)} vs {len(anew)}")
    sys.exit(2)

tot_ref = Counter()
tot_new = Counter()
drift = 0
nref_rows = 0
for iev in range(len(aref)):
    rows_r = Counter(
        zip(
            [int(x) for x in aref["tc_type"][iev]],
            [float(x) for x in aref["tc_pt"][iev]],
            [float(x) for x in aref["tc_eta"][iev]],
            [float(x) for x in aref["tc_phi"][iev]],
        )
    )
    rows_n = Counter(
        zip(
            [int(x) for x in anew["tc_type"][iev]],
            [float(x) for x in anew["tc_pt"][iev]],
            [float(x) for x in anew["tc_eta"][iev]],
            [float(x) for x in anew["tc_phi"][iev]],
        )
    )
    for t, _, _, _ in rows_r.elements():
        tot_ref[t] += 1
        nref_rows += 1
    for t, _, _, _ in rows_n.elements():
        tot_new[t] += 1
    only_r = rows_r - rows_n
    only_n = rows_n - rows_r
    d = sum(only_r.values()) + sum(only_n.values())
    if d:
        print(f"event {iev}: {sum(only_r.values())} ref-only, {sum(only_n.values())} new-only rows")
    drift += d

print("per-type totals (ref | new):")
for t in sorted(set(tot_ref) | set(tot_new)):
    mark = "" if tot_ref[t] == tot_new[t] else "  <-- DIFFER"
    print(f"  type {t}: {tot_ref[t]} | {tot_new[t]}{mark}")
print(f"total rows ref={nref_rows} drift={drift} ({100.0 * drift / max(1, nref_rows):.4f}%)")
print("RESULT:", "IDENTICAL" if drift == 0 else ("NOISE" if drift / max(1, nref_rows) < 2e-4 else "DIFFER"))
