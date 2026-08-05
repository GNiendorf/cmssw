#!/usr/bin/env python3
"""Multiset identity of the tc_* ntuple rows between two runs.

usage: p25_tcntuple.py <a.root> <b.root>

Used for the leg the sidecar cannot cover: LST with chain tracking OFF, where LST_CHAIN_TC_DUMP
never fires. The key is (type, nhits, nlayers, pt bits, eta bits, phi bits) which is as strong as
the sidecar's hit-list key for the purpose of detecting a run-to-run difference.
"""
import struct
import sys
from collections import Counter

import numpy as np
import uproot


def rows(path):
    with uproot.open(path)["tree"] as t:
        b = {k: t[k].array(library="np") for k in
             ["tc_type", "tc_nhits", "tc_nlayers", "tc_pt", "tc_eta", "tc_phi"]}
    out = []
    for i in range(len(b["tc_type"])):
        ev = []
        for j in range(len(b["tc_type"][i])):
            ev.append((int(b["tc_type"][i][j]), int(b["tc_nhits"][i][j]), int(b["tc_nlayers"][i][j]),
                       struct.pack("<f", float(b["tc_pt"][i][j])),
                       struct.pack("<f", float(b["tc_eta"][i][j])),
                       struct.pack("<f", float(b["tc_phi"][i][j]))))
        out.append(ev)
    return out


def main():
    a, b = rows(sys.argv[1]), rows(sys.argv[2])
    n = min(len(a), len(b))
    print(f"{'evt':>4} {'A':>7} {'B':>7} {'common':>7} {'onlyA':>6} {'onlyB':>6} {'ident%':>8}")
    tA = tB = tC = 0
    bytype = Counter()
    for i in range(n):
        ca, cb = Counter(a[i]), Counter(b[i])
        common = sum((ca & cb).values())
        na, nb = sum(ca.values()), sum(cb.values())
        for r, c in (ca - cb).items():
            bytype[("onlyA", r[0])] += c
        for r, c in (cb - ca).items():
            bytype[("onlyB", r[0])] += c
        print(f"{i:>4} {na:>7} {nb:>7} {common:>7} {na-common:>6} {nb-common:>6} "
              f"{100.0*common/na if na else 100.0:>7.2f}%")
        tA += na
        tB += nb
        tC += common
    print()
    print(f"TC(ntuple) TOTAL A={tA} B={tB} common={tC}")
    print(f"TC(ntuple) IDENTITY = {100.0*tC/tA if tA else 100.0:.4f}%")
    if bytype:
        print("differing rows by tc_type (7=pT5 5=pT3 4=T5 8=pLS 9=T4):")
        for k in sorted(bytype):
            print(f"  {k[0]} type={k[1]}: {bytype[k]}")
    return 0 if tC == tA == tB else 1


if __name__ == "__main__":
    sys.exit(main())
