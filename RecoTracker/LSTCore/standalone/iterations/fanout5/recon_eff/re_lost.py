#!/usr/bin/env python3
"""re_lost.py -- composition of the sims the flagship loses vs LST delivery:
which LST class delivered them, kinematics, and which relaxation returns them.
"""
import os

import numpy as np

P = os.path.dirname(os.path.abspath(__file__))
TAGS = ["fl", "noatt", "g0n", "t0n", "c0n", "alln", "fs10n"]


def key(a):
    return a["evt"].astype(np.int64) * 100000 + a["sim"].astype(np.int64)


def main():
    A = {t: np.load(f"{P}/re_sims_{t}.npy") for t in TAGS}
    a = A["noatt"]
    for t in TAGS:
        assert np.array_equal(key(A[t]), key(a)), t
    m = {t: A[t]["anyTC"] for t in TAGS}
    ok = (np.abs(a["vz"]) < 30) & (a["q"] != 0)
    lost = ok & a["baseTC"] & ~m["noatt"]
    form = lost & ~m["alln"]
    gate = lost & (m["g0n"] | m["t0n"]) & ~form
    claim = lost & m["c0n"] & ~gate & ~form
    both = lost & ~form & ~gate & ~claim

    print("LST delivery class of the LOST sims (a sim can have several):")
    hdr = f"{'class':14s} {'n':>5s} {'baseOT':>7s} {'base7(pT5)':>11s} {'base5(pT3)':>11s} {'base8(pLS)':>11s} {'<pt>':>6s} {'|eta|>1.5':>10s}"
    print(hdr)
    for name, sel in (("ALL LOST", lost), ("FORMATION", form), ("GATE", gate),
                      ("CLAIM", claim), ("BOTH-ONLY", both)):
        n = int(sel.sum())
        if n == 0:
            continue
        print(f"{name:14s} {n:5d} {int(a['baseOT'][sel].sum()):7d} {int(a['base7'][sel].sum()):11d}"
              f" {int(a['base5'][sel].sum()):11d} {int(a['base8'][sel].sum()):11d}"
              f" {a['pt'][sel].mean():6.2f} {int((np.abs(a['eta'][sel])>1.5).sum()):10d}")

    print("\nGAINED sims (proto delivers, LST does not) by point:")
    for t in TAGS:
        g = ok & ~a["baseTC"] & m[t]
        print(f"  {t:7s} gained={int(g.sum()):5d}  of which prompt(vxy<1)={int((g & (a['vxy']<1)).sum()):5d}")

    print("\nCLAIM class -- share-pass recovery vs eta:")
    for lo, hi in ((0, 1.5), (1.5, 3.0), (3.0, 4.5)):
        s = claim & (np.abs(a["eta"]) >= lo) & (np.abs(a["eta"]) < hi)
        print(f"  |eta| [{lo},{hi}): claim={int(s.sum()):4d}  fs10n recovers={int((s & m['fs10n']).sum()):4d}")


if __name__ == "__main__":
    main()
