#!/usr/bin/env python3
"""A07 cell profiler: for every structural cell available in the ntuple, report
N / fake% / dup% / (fake-or-dup)% among the in-cut TCs, so that candidate free-win
filters can be picked by purity before any removal is simulated.

A cell is free-ish exactly when (fake or duplicate)% is high: a fake TC costs no
efficiency by definition, and a duplicate TC costs no efficiency as long as the sim's
other TC survives (which the simulator then checks exactly).
"""
import sys
from a07_sim import load, prep

DELIV = {0: "carried", 1: "chain", 2: "attachT5", 3: "attachT3", 4: "zp8pLS"}
TYPE = {4: "T5", 5: "pT3", 7: "pT5", 8: "pLS", 9: "T4"}


def profile(events, keyfn, title, minN=200):
    cells = {}
    for e in events:
        for i in range(len(e["type"])):
            if not e["incut"][i]:
                continue
            k = keyfn(e, i)
            if k is None:
                continue
            c = cells.setdefault(k, [0, 0, 0])
            c[0] += 1
            c[1] += 1 if not e["sims"][i] else 0
            c[2] += 1 if e["dup"][i] else 0
    print("\n### %s" % title)
    print("%-46s %9s %8s %8s %8s %8s" % ("cell", "N", "N/evt", "fake%", "dup%", "f-or-d%"))
    rows = []
    for k, c in cells.items():
        if c[0] < minN:
            continue
        # fake and dup are disjoint by construction (a fake matches no sim)
        rows.append((k, c[0], 100.0 * c[1] / c[0], 100.0 * c[2] / c[0],
                     100.0 * (c[1] + c[2]) / c[0]))
    rows.sort(key=lambda r: -(r[4] * r[1]))
    for k, n, f, d, fd in rows:
        print("%-46s %9d %8.2f %8.2f %8.2f %8.2f" % (str(k), n, n / float(len(events)), f, d, fd))


def main(pkl):
    ev = prep(load(pkl))
    nm = lambda e, i: "%s/%s" % (DELIV.get(e["deliv"][i], "?"), TYPE.get(e["type"][i], "?"))
    profile(ev, lambda e, i: nm(e, i), "delivery class x type")
    profile(ev, lambda e, i: (nm(e, i), e["nhit"][i]), "x nhitOT")
    profile(ev, lambda e, i: (nm(e, i), e["br"][i]), "x -G 6 admission branch (dbgBr)")
    profile(ev, lambda e, i: (nm(e, i), e["nl"][i]), "x chain nLayers (dbgNL)")
    profile(ev, lambda e, i: (nm(e, i), e["nn"][i]), "x member-T3 count (dbgNN)")
    profile(ev, lambda e, i: (nm(e, i), e["nps"][i]), "x PS-module MD count (dbgNPS)")
    profile(ev, lambda e, i: (nm(e, i), e["inlay"][i]), "x innermost chain layer (dbgInLay)")
    profile(ev, lambda e, i: (nm(e, i), e["nb"][i]), "x barrel MD count (dbgNB)")
    profile(ev, lambda e, i: (nm(e, i), e["nmd"][i]), "x chain MD count (dbgNMD)")
    profile(ev, lambda e, i: (nm(e, i), e["nmd"][i] - 2 * e["nl"][i] if e["nl"][i] > 0 else None),
            "x (nMD - 2*nLayers)  [double-hit layers]")
    profile(ev, lambda e, i: (nm(e, i), min(int(e["pt"][i]), 6)), "x int(pt) capped 6")


if __name__ == "__main__":
    main(sys.argv[1])
