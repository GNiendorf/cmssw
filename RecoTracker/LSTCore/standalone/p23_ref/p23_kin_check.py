#!/usr/bin/env python3
"""P2.3 gate (a), kinematics half: the tc_* payload the harness actually reads.

usage: p23_kin_check.py <proto.root> <prod.root>

The hit-list gate proves the two pipelines emit the same track candidates. This one proves the
NTUPLE ROW each of them writes is the same: per event it compares the multiset of
(type, pt, eta, phi, nhitOT, isFake, isDuplicate) over all TCs, with pt/eta/phi compared BITWISE
(float32 bit patterns), and separately over the chain-class rows (type 4 and 9) alone.
"""
import sys
from collections import Counter

import numpy as np
import uproot

BRANCHES = ["tc_type", "tc_pt", "tc_eta", "tc_phi", "tc_nhitOT", "tc_isFake", "tc_isDuplicate"]


def load(path):
    with uproot.open(path)["tree"] as t:
        return {b: t[b].array(library="np") for b in BRANCHES}


def rows(d, i, chain_only=False):
    ty = np.asarray(d["tc_type"][i], dtype=np.int64)
    pt = np.asarray(d["tc_pt"][i], dtype=np.float32).view(np.uint32)
    eta = np.asarray(d["tc_eta"][i], dtype=np.float32).view(np.uint32)
    phi = np.asarray(d["tc_phi"][i], dtype=np.float32).view(np.uint32)
    nh = np.asarray(d["tc_nhitOT"][i], dtype=np.int64)
    fk = np.asarray(d["tc_isFake"][i], dtype=np.int64)
    dp = np.asarray(d["tc_isDuplicate"][i], dtype=np.int64)
    out = []
    for k in range(len(ty)):
        if chain_only and ty[k] not in (4, 9):
            continue
        out.append((int(ty[k]), int(pt[k]), int(eta[k]), int(phi[k]), int(nh[k]), int(fk[k]), int(dp[k])))
    return Counter(out)


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    n = min(len(a["tc_type"]), len(b["tc_type"]))
    print(f"{'evt':>4} {'allTC':>7} {'diff':>6} {'chainTC':>8} {'diff':>6}")
    tot_all = tot_chain = 0
    for i in range(n):
        ca, cb = rows(a, i), rows(b, i)
        da = sum((ca - cb).values()) + sum((cb - ca).values())
        pa, pb = rows(a, i, True), rows(b, i, True)
        dc = sum((pa - pb).values()) + sum((pb - pa).values())
        tot_all += da
        tot_chain += dc
        print(f"{i:>4} {sum(ca.values()):>7} {da:>6} {sum(pa.values()):>8} {dc:>6}")
    print()
    print(f"TOTAL row-multiset differences: all TCs = {tot_all}, chain-class TCs = {tot_chain}")
    print("KINEMATICS: " + ("PASS - bitwise identical tc rows" if tot_all == 0 else "MISMATCH"))
    return 0 if tot_all == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
