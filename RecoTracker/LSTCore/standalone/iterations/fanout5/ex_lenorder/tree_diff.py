#!/usr/bin/env python3
"""Bit-exact TTree comparison for prototype output ROOT files (M16 regression gate).

Compares every branch of tree "tree" in two files, entry by entry, requiring EXACT
equality (no tolerance) for both flat and jagged branches.

Usage: python3 tree_diff.py A.root B.root [--nmax N]
Exit code 0 = bit-identical, 1 = differs.
"""
import argparse
import sys

import awkward as ak
import numpy as np
import uproot


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--nmax", type=int, default=-1)
    args = ap.parse_args()

    fa, fb = uproot.open(args.a), uproot.open(args.b)
    ta, tb = fa["tree"], fb["tree"]
    ba = sorted(k.split(";")[0] for k in ta.keys())
    bb = sorted(k.split(";")[0] for k in tb.keys())
    if ba != bb:
        print("BRANCH SET DIFFERS")
        print("  only in A:", [x for x in ba if x not in bb])
        print("  only in B:", [x for x in bb if x not in ba])
        return 1
    na, nb = ta.num_entries, tb.num_entries
    if na != nb:
        print("ENTRY COUNT DIFFERS: %d vs %d" % (na, nb))
        return 1
    stop = na if args.nmax < 0 else min(na, args.nmax)

    def raw(x):
        """Fully flattened value view + the per-level count arrays that define the
        jagged structure. Floats are compared BITWISE so NaN == NaN (sim_pca_dz
        legitimately carries NaN for sims with no PCA)."""
        levels = []
        cur = x
        while cur.ndim > 1:
            levels.append(ak.to_numpy(ak.num(cur, axis=1)))
            cur = ak.flatten(cur, axis=1)
        flat = ak.to_numpy(cur)
        if flat.dtype.kind == "f":
            flat = flat.view("u%d" % flat.dtype.itemsize)
        return flat, levels

    bad = []
    for br in ba:
        fa_, ca_ = raw(ta[br].array(entry_stop=stop))
        fb_, cb_ = raw(tb[br].array(entry_stop=stop))
        same = (fa_.shape == fb_.shape and np.array_equal(fa_, fb_)
                and len(ca_) == len(cb_)
                and all(np.array_equal(u, v) for u, v in zip(ca_, cb_)))
        if not same:
            bad.append(br)
    print("%d/%d branches identical over %d entries" % (len(ba) - len(bad), len(ba), stop))
    if bad:
        print("DIFFERING BRANCHES: %s" % bad)
        return 1
    print("BIT-EXACT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
