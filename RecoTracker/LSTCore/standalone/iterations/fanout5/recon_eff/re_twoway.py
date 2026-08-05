#!/usr/bin/env python3
"""re_twoway.py -- sizing of the CAPPED 2-WAY OWNERSHIP idea.

The K9 owner map is single-valued (K9K10.cc: `owner[keyItems[k]] = c`), so no flag
combination can report per-hit owner MULTIPLICITY. What flags CAN measure exactly:

  (1) the population a second ownership layer could admit at all -- the -FS share pass
      (M17): a SECOND greedy walk over exactly the chains pass 1 rejected on the claim
      budget, same order, against the pass-1 owner map. -FS 1.01 admits every deferred
      chain, so it is the UPPER BOUND of any capped scheme (a 2-way cap admits a subset).
  (2) the density of already-doubly-owned hits in the flagship accepted set -- the -DD
      post-arbitration structural dedup counts accepted chains sharing >= n OT hit rows
      with a higher-ranked accepted chain; those chains are exactly the ones that put a
      SECOND owner on a hit.

This script joins (1) with the per-sim tables and prints the CLAIM-class recovery of each
share-pass tolerance, i.e. how many of the claim-blocked LST-delivered sims a second
ownership layer can return, and at what dup/fake price.
"""
import json
import os
import re
import sys

import numpy as np

P = os.path.dirname(os.path.abspath(__file__))


def key(a):
    return a["evt"].astype(np.int64) * 100000 + a["sim"].astype(np.int64)


def main():
    tags = ["noatt", "g0n", "t0n", "c0n", "alln", "fs05n", "fs10n", "f2n"]
    extra = [t for t in sys.argv[1:] if t not in tags]
    A = {t: np.load(f"{P}/re_sims_{t}.npy") for t in tags + extra}
    a = A["noatt"]
    k0 = key(a)
    for t in A:
        assert np.array_equal(key(A[t]), k0), t
    ok = (np.abs(a["vz"]) < 30) & (a["q"] != 0)
    m = {t: A[t]["anyTC"] for t in A}
    lost = ok & a["baseTC"] & ~m["noatt"]
    form = lost & ~m["alln"]
    gate = lost & (m["g0n"] | m["t0n"]) & ~form
    claim = lost & m["c0n"] & ~gate & ~form

    print(f"LOST(vs LST) at noatt = {int(lost.sum())}   CLAIM-only class = {int(claim.sum())}")
    print("\nsecond-ownership-layer recovery of the CLAIM-only class:")
    print("| point | claim-class recovered | of 106 | eff | dup | fake | nchain |")
    print("|---|---|---|---|---|---|---|")
    for t in ["fs05n", "fs10n", "f2n", "c0n"] + extra:
        if t not in m:
            continue
        j = json.load(open(f"{P}/re_{t}.json"))["metrics"]
        g = lambda k: j[k]["proto"]
        txt = open(f"{P}/re_{t}.log", errors="ignore").read()
        mm = re.search(r"chain TCs\s+total=(\d+)", txt)
        n = int((claim & m[t]).sum())
        print(f"| {t} | {n} | {100.0*n/max(1,int(claim.sum())):.0f}% | {g('eff_overall_incut'):.4f} "
              f"| {g('dup_overall_incut'):.4f} | {g('fake_overall_incut'):.4f} | {mm.group(1)} |")

    # Prompt / displaced split of the claim class.
    for name, sel in (("prompt vxy<1", claim & (a["vxy"] < 1)),
                      ("vxy 1-5", claim & (a["vxy"] >= 1) & (a["vxy"] < 5)),
                      ("vxy >=5", claim & (a["vxy"] >= 5))):
        print(f"  claim class {name:14s} = {int(sel.sum()):4d}"
              f"   recovered by fs10n = {int((sel & m['fs10n']).sum()):4d}")


if __name__ == "__main__":
    main()
