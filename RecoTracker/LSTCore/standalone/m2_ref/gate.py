#!/usr/bin/env python3
"""Branch-diff judge for many candidates against one reference, in one pass per file.

usage: gate.py <ref.root> <cand.root> [<cand.root> ...]

Same comparison as rebase_ref/cmp_branches.py (raw byte image of the awkward array, no
tolerance anywhere), but it reads the reference once and prints one line per candidate:

    <name>  <nIdentical>/<nRef> IDENTICAL   [DIFFER: b1,b2,...]

35/35 means bit-identical on every pre-existing branch, which is the only real gate on CPU.
Offline python on already-written files, so it is broker-exempt.
"""
import sys
import uproot
import awkward as ak
import numpy as np


def blob(arr):
    if isinstance(arr, np.ndarray):
        return arr.tobytes() + b"|" + repr(arr.dtype).encode() + repr(arr.shape).encode()
    form, length, containers = ak.to_buffers(ak.to_packed(arr))
    parts = [str(form).encode(), str(length).encode()]
    for k in sorted(containers):
        parts.append(k.encode())
        parts.append(np.asarray(containers[k]).tobytes())
    return b"|".join(parts)


ref_path, cands = sys.argv[1], sys.argv[2:]
tref = uproot.open(ref_path)["tree"]
refkeys = sorted(tref.keys())
ref = {k: blob(tref[k].array()) for k in refkeys}
print("ref %s : %d entries, %d branches" % (ref_path.split("/")[-1], tref.num_entries, len(refkeys)))

for c in cands:
    try:
        t = uproot.open(c)["tree"]
    except Exception as e:  # a crashed arm leaves no readable file
        print("%-30s UNREADABLE (%s)" % (c.split("/")[-1], type(e).__name__))
        continue
    if t.num_entries != tref.num_entries:
        print("%-30s ENTRIES DIFFER %d vs %d" % (c.split("/")[-1], t.num_entries, tref.num_entries))
        continue
    keys = set(t.keys())
    same, diff, miss = [], [], []
    for k in refkeys:
        if k not in keys:
            miss.append(k)
        elif blob(t[k].array()) == ref[k]:
            same.append(k)
        else:
            diff.append(k)
    print("%-30s %d/%d IDENTICAL%s%s"
          % (c.split("/")[-1], len(same), len(refkeys),
             "  MISSING %d" % len(miss) if miss else "",
             "  DIFFER: " + ",".join(diff) if diff else ""))
