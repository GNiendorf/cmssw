#!/usr/bin/env python3
"""A07: find STRUCTURAL cells that are simultaneously fake-rich and efficiency-empty.

For every fine structural cell it reports N, fake%, and the count of ESSENTIAL rows
(sole protected cover of a counted sim -- overall efficiency denominator OR any
displacement band). A cell with essential == 0 can be deleted at literally zero cost to
every efficiency number the scoreboard carries; the ratio fake/N then says how much fake
rate that deletion buys.

Cells are built only from quantities the pipeline already computes structurally:
delivery class, object type, -G 6 admission branch, layer / MD / PS-module counts,
innermost layer, and the object's own pt / |eta| coarsely binned.
"""
import sys
from a07_sim import load, prep
from a07_oracle import mark_essential

DELIV = {0: "carried", 1: "chain", 2: "attachT5", 3: "attachT3", 4: "zp8pLS"}
TYPE = {4: "T5", 5: "pT3", 7: "pT5", 8: "pLS", 9: "T4"}


def ptbin(p):
    if p < 1.5:
        return "p1"
    if p < 3.0:
        return "p2"
    if p < 6.0:
        return "p3"
    return "p4"


def etabin(a):
    a = abs(a)
    if a < 1.1:
        return "B"
    if a < 1.7:
        return "T"
    if a < 2.4:
        return "E1"
    return "E2"


def main(pkl, minN):
    ev = mark_essential(prep(load(pkl)))
    nev = float(len(ev))
    cells = {}
    for e in ev:
        for i in range(len(e["type"])):
            if not e["incut"][i]:
                continue
            k = (DELIV.get(e["deliv"][i], "?"), TYPE.get(e["type"][i], "?"), e["br"][i],
                 e["nps"][i], e["inlay"][i], ptbin(e["pt"][i]), etabin(e["eta"][i]))
            c = cells.setdefault(k, [0, 0, 0])
            c[0] += 1
            c[1] += 1 if not e["sims"][i] else 0
            c[2] += e["ess"][i]
    rows = [(k, c) for k, c in cells.items() if c[0] >= minN]
    print("### cells with N >= %d, sorted by fake count among ZERO-ESSENTIAL cells first" % minN)
    print("%-52s %8s %8s %8s %8s" % ("cell (deliv,type,br,nPS,inLay,pt,eta)", "N", "N/evt",
                                     "fake%", "essent"))
    z = [(k, c) for k, c in rows if c[2] == 0]
    z.sort(key=lambda r: -r[1][1])
    print("-- ZERO-ESSENTIAL cells (deletable at exactly zero efficiency cost) --")
    tot = [0, 0]
    for k, c in z[:40]:
        print("%-52s %8d %8.2f %8.2f %8d" % (str(k), c[0], c[0] / nev, 100.0 * c[1] / c[0], c[2]))
    for k, c in z:
        tot[0] += c[0]
        tot[1] += c[1]
    print("ALL zero-essential cells (N>=%d): N %d (%.2f/evt) fake %d -> purity %.1f%%"
          % (minN, tot[0], tot[0] / nev, tot[1], 100.0 * tot[1] / tot[0] if tot[0] else 0))
    print()
    print("-- highest-fake cells regardless of essential content --")
    rows.sort(key=lambda r: -r[1][1])
    for k, c in rows[:25]:
        print("%-52s %8d %8.2f %8.2f %8d" % (str(k), c[0], c[0] / nev, 100.0 * c[1] / c[0], c[2]))


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 150)
