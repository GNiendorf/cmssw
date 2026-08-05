#!/usr/bin/env python3
"""A07: inside the pT3-class cell.

Uses the diagnostics added to protoA07 (dbgBranch 4, dbgMP = the accepting attach logit,
dbgNPS / dbgNB / dbgInLay / dbgNL = the target T3's module and layer composition) to ask
the only question that matters for the permitted retrain: does the head's own score
separate the rows that BUY efficiency from the rows that are fake?

ESSENTIAL here = the row is the sole protected cover of a sim counted in the efficiency
denominator or in any displacement band, i.e. deleting it costs a track.
"""
import sys
from a07_sim import load, prep, evaluate, HDR, row
from a07_oracle import mark_essential


def logitbin(v):
    if v < 6.5:
        return "[6.0,6.5)"
    if v < 7.0:
        return "[6.5,7.0)"
    if v < 8.0:
        return "[7.0,8.0)"
    if v < 10.0:
        return "[8.0,10 )"
    if v < 14.0:
        return "[10 ,14 )"
    return "[14 ,inf)"


def main(pkl):
    ev = mark_essential(prep(load(pkl)))
    nev = float(len(ev))
    print("### pT3-class rows (deliv 3), in-cut, by ACCEPTING ATTACH LOGIT")
    print("%-12s %9s %8s %8s %9s %9s %11s" %
          ("logit bin", "N", "N/evt", "fake%", "essent", "ess/evt", "trk per fake"))
    cells = {}
    for e in ev:
        for i in range(len(e["type"])):
            if not e["incut"][i] or e["deliv"][i] != 3:
                continue
            c = cells.setdefault(logitbin(e["mp"][i]), [0, 0, 0])
            c[0] += 1
            c[1] += 1 if not e["sims"][i] else 0
            c[2] += e["ess"][i]
    for k in sorted(cells):
        c = cells[k]
        print("%-12s %9d %8.2f %8.2f %9d %9.3f %11.3f" %
              (k, c[0], c[0] / nev, 100.0 * c[1] / c[0], c[2], c[2] / nev,
               c[2] / float(c[1]) if c[1] else 0.0))
    print()
    for key, name in (("nps", "PS-module MDs"), ("nb", "barrel MDs"),
                      ("inlay", "innermost layer"), ("nl", "distinct layers")):
        cells = {}
        for e in ev:
            for i in range(len(e["type"])):
                if not e["incut"][i] or e["deliv"][i] != 3:
                    continue
                c = cells.setdefault(e[key][i], [0, 0, 0])
                c[0] += 1
                c[1] += 1 if not e["sims"][i] else 0
                c[2] += e["ess"][i]
        print("### pT3-class by %s" % name)
        print("%-8s %9s %8s %8s %9s %11s" % (name, "N", "N/evt", "fake%", "essent", "trk/fake"))
        for k in sorted(cells):
            c = cells[k]
            print("%-8s %9d %8.2f %8.2f %9d %11.3f" %
                  (k, c[0], c[0] / nev, 100.0 * c[1] / c[0], c[2],
                   c[2] / float(c[1]) if c[1] else 0.0))
        print()
    # what a delivery-side logit raise alone would do (post-claim, no re-arbitration)
    base = evaluate(ev, None, "BASELINE")
    print(HDR)
    print(row(base))
    for thr in (6.5, 7.0, 8.0, 10.0):
        m = evaluate(ev, lambda e, i, t=thr: e["deliv"][i] == 3 and e["mp"][i] < t,
                     "pT3 delivery logit >= %g" % thr)
        print(row(m))
        print("   d_eff %+.5f  d_dup %+.5f  d_fake %+.5f  d_v510 %+.5f  d_d15 %+.5f" %
              (m["eff"] - base["eff"], m["dup"] - base["dup"], m["fake"] - base["fake"],
               m["v510"] - base["v510"], m["d15"] - base["d15"]))


if __name__ == "__main__":
    main(sys.argv[1])
