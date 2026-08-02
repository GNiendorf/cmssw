#!/usr/bin/env python3
"""Bit-identity check between two LST standalone ntuples.

Phase P2.0 gate: with useChainTracking either off or on, every physics branch of the
output ntuple must be bit-for-bit identical to the pre-change baseline.

Usage: p20_bitcheck.py <ref.root> <new.root> [tree]
Exit code 0 iff every branch of every event matches exactly.
"""
import sys

import numpy as np
import uproot


def flatten(x):
    """Return a flat numpy array plus the per-entry counts, for jagged or flat branches."""
    if hasattr(x, "layout"):
        import awkward as ak

        if x.ndim > 1:
            counts = np.asarray(ak.num(x, axis=1))
            return np.asarray(ak.flatten(x, axis=None)), counts
        return np.asarray(x), None
    arr = np.asarray(x)
    return arr, None


def main():
    ref_path, new_path = sys.argv[1], sys.argv[2]
    tree_name = sys.argv[3] if len(sys.argv) > 3 else "tree"

    ref = uproot.open(ref_path)[tree_name]
    new = uproot.open(new_path)[tree_name]

    ref_branches = sorted(ref.keys())
    new_branches = sorted(new.keys())

    print(f"ref : {ref_path}  entries={ref.num_entries}  branches={len(ref_branches)}")
    print(f"new : {new_path}  entries={new.num_entries}  branches={len(new_branches)}")

    failures = []

    if ref_branches != new_branches:
        only_ref = set(ref_branches) - set(new_branches)
        only_new = set(new_branches) - set(ref_branches)
        failures.append(f"branch set differs: only-ref={sorted(only_ref)} only-new={sorted(only_new)}")

    if ref.num_entries != new.num_entries:
        failures.append(f"entry count differs: {ref.num_entries} vs {new.num_entries}")

    common = [b for b in ref_branches if b in set(new_branches)]
    n_checked = 0
    n_values = 0
    for name in common:
        a, ca = flatten(ref[name].array())
        b, cb = flatten(new[name].array())
        n_checked += 1
        n_values += a.size

        if (ca is None) != (cb is None):
            failures.append(f"{name}: jaggedness differs")
            continue
        if ca is not None and not np.array_equal(ca, cb):
            bad = int(np.count_nonzero(ca != cb)) if ca.shape == cb.shape else -1
            failures.append(f"{name}: per-event multiplicity differs ({bad} events)")
            continue
        if a.shape != b.shape:
            failures.append(f"{name}: shape {a.shape} vs {b.shape}")
            continue
        if a.dtype.kind == "f":
            # bitwise, so NaN==NaN and -0.0 != 0.0 are both caught
            same = a.view(np.uint8 if a.itemsize == 1 else f"u{a.itemsize}") == b.view(
                np.uint8 if b.itemsize == 1 else f"u{b.itemsize}"
            )
        else:
            same = a == b
        n_bad = int(np.count_nonzero(~same))
        if n_bad:
            idx = int(np.flatnonzero(~same)[0])
            failures.append(f"{name}: {n_bad}/{a.size} values differ (first at {idx}: {a[idx]!r} vs {b[idx]!r})")

    print(f"branches compared : {n_checked}")
    print(f"values compared   : {n_values}")
    print(f"mismatches        : {len(failures)}")
    for f in failures:
        print("  MISMATCH " + f)

    if failures:
        print("RESULT: FAIL")
        return 1
    print("RESULT: BIT-IDENTICAL")
    return 0


if __name__ == "__main__":
    sys.exit(main())
