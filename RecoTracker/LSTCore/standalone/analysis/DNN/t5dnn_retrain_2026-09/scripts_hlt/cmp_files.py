#!/usr/bin/env python3
"""HLT MTV numbers from harvested files: efficiency by pT (prompt TP selection, r_vertex < 2.5 cm), efficiency by
production radius (vertpos, pT > 0.9 selection), fake rate and duplicate rate by reco pT, for hltGeneral.
usage: cmp_files.py label=harvested.root [label=harvested.root ...]   (first = reference for deltas)"""
import os
import sys

import uproot

B = "DQMData/Run 1/HLT/Run summary/Tracking/ValidationWRTtp/"
COLL = os.environ.get("COLL", "hltGeneral")
arms = [a.split("=", 1) for a in sys.argv[1:]]


def ratio(f, num, den, lo, hi, inv=False):
    d = uproot.open(f)[B + COLL + "_hltAssociatorByHits"]
    n, dd = d[num].values(), d[den].values()
    e = d[den].axis().edges()
    c = 0.5 * (e[1:] + e[:-1])
    m = (c >= lo) & (c < hi)
    N, D = n[m].sum(), dd[m].sum()
    r = N / D if D else 0.0
    return (1 - r if inv else r), D


rows = [("eff  pT", "num_assoc(simToReco)_pT", "num_simul_pT", b, False) for b in ((0.9, 10), (10, 100), (100, 1000))]
rows += [("eff  vxy", "num_assoc(simToReco)_vertpos", "num_simul_vertpos", b, False)
         for b in ((0, 2.5), (2.5, 10), (10, 30), (30, 60))]
rows += [("fake pT", "num_assoc(recoToSim)_pT", "num_reco_pT", b, True) for b in ((0.9, 10), (10, 40), (40, 100), (100, 1000))]
rows += [("dup  pT", "num_duplicate_pT", "num_reco_pT", b, False) for b in ((0.9, 1000),)]
print(f"{COLL}: " + " | ".join(f"{l}={os.path.basename(p)}" for l, p in arms))
print(f"{'':20s}" + "".join(f"{l:>26s}" for l, _ in arms))
for lab, num, den, (lo, hi), inv in rows:
    vals = [ratio(p, num, den, lo, hi, inv) for _, p in arms]
    line = f"{lab} {lo:g}-{hi:g}".ljust(20)
    for i, (v, D) in enumerate(vals):
        line += f"{v:10.4f}" + (f" ({v - vals[0][0]:+.4f})" if i else " " * 10) + f" {int(D):>6d}"[-6:]
    print(line)
