#!/usr/bin/env python3
"""P2.3 gate (c): OFF-state bit identity, over the first N entries of each file.

usage: p23_bitcheck.py <ref.root> <new.root> [nentries]

Same contract as p20_bitcheck.py (bitwise float comparison, jagged-aware), with an entry limit so
the reference can be the frozen 300-event benchmark ntuple while the subject is a short run.
Exit code 0 iff every common branch matches exactly over the compared entries.
"""
import sys

import awkward as ak
import numpy as np
import uproot


def flatten(x):
    if hasattr(x, "layout") and getattr(x, "ndim", 1) > 1:
        counts = np.asarray(ak.num(x, axis=1))
        return np.asarray(ak.flatten(x, axis=None)), counts
    return np.asarray(x), None


def main():
    ref_path, new_path = sys.argv[1], sys.argv[2]
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 10

    ref = uproot.open(ref_path)["tree"]
    new = uproot.open(new_path)["tree"]
    n = min(n, ref.num_entries, new.num_entries)

    rb, nb = sorted(ref.keys()), sorted(new.keys())
    print(f"ref : {ref_path}  entries={ref.num_entries}  branches={len(rb)}")
    print(f"new : {new_path}  entries={new.num_entries}  branches={len(nb)}")
    print(f"comparing first {n} entries")

    failures = []
    only_ref, only_new = set(rb) - set(nb), set(nb) - set(rb)
    if only_ref or only_new:
        print(f"  branch sets differ: only-ref={len(only_ref)} only-new={len(only_new)}")
        if only_ref:
            print(f"    only-ref: {sorted(only_ref)[:20]}")
        if only_new:
            print(f"    only-new: {sorted(only_new)[:20]}")

    common = [b for b in rb if b in set(nb)]
    n_values = 0
    for name in common:
        a, ca = flatten(ref[name].array(entry_stop=n))
        b, cb = flatten(new[name].array(entry_stop=n))
        n_values += a.size
        if (ca is None) != (cb is None):
            failures.append(f"{name}: jaggedness differs")
            continue
        if ca is not None and not np.array_equal(ca, cb):
            failures.append(f"{name}: per-event multiplicity differs")
            continue
        if a.shape != b.shape:
            failures.append(f"{name}: shape {a.shape} vs {b.shape}")
            continue
        if a.dtype.kind == "f":
            same = a.view(f"u{a.itemsize}") == b.view(f"u{b.itemsize}")
        else:
            same = a == b
        bad = int(np.count_nonzero(~same))
        if bad:
            i = int(np.flatnonzero(~same)[0])
            failures.append(f"{name}: {bad}/{a.size} differ (first {a[i]!r} vs {b[i]!r})")

    print(f"branches compared : {len(common)}")
    print(f"values compared   : {n_values}")
    print(f"mismatches        : {len(failures)}")
    for f in failures[:40]:
        print("  MISMATCH " + f)
    if failures:
        print("RESULT: FAIL")
        return 1
    print("RESULT: BIT-IDENTICAL")
    return 0


if __name__ == "__main__":
    sys.exit(main())
