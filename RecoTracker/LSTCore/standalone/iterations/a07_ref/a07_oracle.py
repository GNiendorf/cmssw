#!/usr/bin/env python3
"""A07 ORACLE: how much fake rate is removable from each cell AT ZERO COST.

A delivered TC is ESSENTIAL if it is the only surviving TC covering at least one sim that
sits in the efficiency denominator or in any displacement-band denominator. Everything
else can be deleted with EXACTLY zero efficiency and zero displaced cost (a greedy pass
protects one cover per covered sim, so no sim can lose its last cover).

Per cell this gives the ceiling for ANY fake-reduction filter that is free by construction,
which is the number to compare a candidate structural predicate against.
"""
import sys
from a07_sim import load, prep, evaluate, band, HDR, row

DELIV = {0: "carried", 1: "chain", 2: "attachT5", 3: "attachT3", 4: "zp8pLS"}
TYPE = {4: "T5", 5: "pT3", 7: "pT5", 8: "pLS", 9: "T4"}


def mark_essential(events):
    """essential[i] = 1 if TC i is the protected (sole/first) cover of a counted sim."""
    for e in events:
        n = len(e["type"])
        ess = [0] * n
        cover = {}
        for i in range(n):
            for s in e["sims"][i]:
                cover.setdefault(s, []).append(i)
        for s, lst in cover.items():
            counted = e["sden"][s] if s < e["nsim"] else False
            if s < e["nsim"] and e["sbden"][s]:
                if band(e["svxy"][s]) is not None or band(abs(e["sdxy"][s])) is not None:
                    counted = True
            if not counted:
                continue
            # protect the cover with the most OT hits (a stand-in for "best"), ties -> first
            best = max(lst, key=lambda i: (e["nhit"][i], -i))
            ess[best] = 1
        e["ess"] = ess
    return events


def main(pkl):
    ev = mark_essential(prep(load(pkl)))
    nev = float(len(ev))
    print("### per-cell ORACLE (in-cut rows only)")
    print("%-22s %9s %8s %8s %8s %8s %9s" %
          ("cell", "N", "N/evt", "fake%", "essent%", "free%", "free/evt"))
    cells = {}
    for e in ev:
        for i in range(len(e["type"])):
            if not e["incut"][i]:
                continue
            k = "%s/%s" % (DELIV.get(e["deliv"][i], "?"), TYPE.get(e["type"][i], "?"))
            c = cells.setdefault(k, [0, 0, 0])
            c[0] += 1
            c[1] += 1 if not e["sims"][i] else 0
            c[2] += e["ess"][i]
    for k in sorted(cells, key=lambda k: -cells[k][1]):
        c = cells[k]
        free = c[0] - c[2]
        print("%-22s %9d %8.2f %8.2f %8.2f %8.2f %9.2f" %
              (k, c[0], c[0] / nev, 100.0 * c[1] / c[0], 100.0 * c[2] / c[0],
               100.0 * free / c[0], free / nev))
    print()
    base = evaluate(ev, None, "BASELINE")
    print(HDR)
    print(row(base))
    # oracle removals, cell by cell and cumulative
    order = ["attachT3/pT3", "chain/T5", "chain/T4", "carried/pLS", "zp8pLS/pLS", "attachT5/pT5"]
    for k in order:
        d, t = k.split("/")
        dv = [a for a, b in DELIV.items() if b == d][0]
        tv = [a for a, b in TYPE.items() if b == t][0]
        m = evaluate(ev, lambda e, i, dv=dv, tv=tv: (e["deliv"][i] == dv and e["type"][i] == tv
                                                     and not e["ess"][i]),
                     "ORACLE free-drop %s" % k)
        print(row(m))
    m = evaluate(ev, lambda e, i: not e["ess"][i], "ORACLE free-drop EVERYTHING")
    print(row(m))
    print("\nDELTAS")
    print("%-34s %9s %9s %9s %9s %9s %9s" %
          ("label", "d_eff", "d_dup", "d_fake", "d_v510", "d_v1030", "d_d15"))


if __name__ == "__main__":
    main(sys.argv[1])
