#!/usr/bin/env python3
"""Bit-identity check of every PRE-EXISTING branch between two LST ntuples.

Usage: cmp_branches.py <ref.root> <new.root>

Reports, per branch present in the reference: IDENTICAL / DIFFER / MISSING.
Branches present only in <new.root> are listed as ADDED (expected: the instrument).
Comparison is on the raw serialized bytes of the awkward array so float NaN /
-0.0 / bit patterns are all covered; no tolerance is applied anywhere.
"""
import sys
import uproot
import awkward as ak
import numpy as np

ref_path, new_path = sys.argv[1], sys.argv[2]
tref = uproot.open(ref_path)["tree"]
tnew = uproot.open(new_path)["tree"]

ref_keys = set(tref.keys())
new_keys = set(tnew.keys())

print("ref entries", tref.num_entries, " new entries", tnew.num_entries)
if tref.num_entries != tnew.num_entries:
    print("FATAL: entry count differs")
    sys.exit(2)


def blob(arr):
    """Deterministic byte image of an awkward/numpy array."""
    if isinstance(arr, np.ndarray):
        return arr.tobytes() + b"|" + repr(arr.dtype).encode() + repr(arr.shape).encode()
    layout = ak.to_buffers(ak.to_packed(arr))
    form, length, containers = layout
    parts = [str(form).encode(), str(length).encode()]
    for k in sorted(containers):
        parts.append(k.encode())
        parts.append(np.asarray(containers[k]).tobytes())
    return b"|".join(parts)


same, diff, missing = [], [], []
for k in sorted(ref_keys):
    if k not in new_keys:
        missing.append(k)
        continue
    a = tref[k].array()
    b = tnew[k].array()
    if blob(a) == blob(b):
        same.append(k)
    else:
        diff.append(k)

added = sorted(new_keys - ref_keys)

print("PRE-EXISTING branches: %d IDENTICAL, %d DIFFER, %d MISSING" % (len(same), len(diff), len(missing)))
if diff:
    print("DIFFER:")
    for k in diff:
        print("   ", k)
if missing:
    print("MISSING in new:")
    for k in missing:
        print("   ", k)
print("ADDED in new (%d):" % len(added))
for k in added:
    print("   ", k)
sys.exit(0 if (not diff and not missing) else 1)
