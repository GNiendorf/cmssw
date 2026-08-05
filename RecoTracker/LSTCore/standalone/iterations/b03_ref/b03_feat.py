#!/usr/bin/env python3
"""B03 -- per-feature purity profile INSIDE the fake-rich barrel/transition exempt cells.
For every feature already written per TC, show rows / fake% / SOLE-displaced content as a
function of the feature, so a structural criterion can be chosen with eyes open."""
import sys
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a07_ref")
from a07_sim import load, prep  # noqa: E402

PKL = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/b00_ref/cf977.pkl"
CHAIN = (4, 9)
CELLS = [("B br1", 1, 0.0, 1.1), ("B br3", 3, 0.0, 1.1), ("T br3", 3, 1.1, 1.7)]
IFEAT = ["nl", "nmd", "nb", "nps", "nn", "inlay"]
FFEAT = [("pt", [0.9, 1.0, 1.2, 1.5, 2.0, 3.0, 5.0, 1e9]),
         ("dca", [-1e9, 0.5, 1.0, 2.0, 5.0, 10.0, 1e9]),
         ("mp", [-1e9, -4, -2, 0, 2, 4, 1e9]),
         ("md", [-1e9, -1, 0, 1, 2, 4, 1e9]),
         ("nhit", [0, 8, 10, 12, 14, 99])]


def main():
    ev = prep(load(PKL))
    nev = len(ev)
    for cname, br, lo, hi in CELLS:
        acc = {}
        for e in ev:
            n = len(e["type"])
            cnt = {}
            for i in range(n):
                for s in e["sims"][i]:
                    cnt[s] = cnt.get(s, 0) + 1
            for i in range(n):
                if e["type"][i] not in CHAIN or e["br"][i] != br or not e["incut"][i]:
                    continue
                if not (lo <= abs(e["eta"][i]) < hi):
                    continue
                fk = e["fake"][i]
                sd = 0
                if not fk:
                    dup = any(cnt.get(s, 0) > 1 for s in e["sims"][i])
                    if not dup and any(abs(e["sdxy"][s]) >= 1.0 for s in e["sims"][i] if s < len(e["sdxy"])):
                        sd = 1
                for f in IFEAT:
                    a = acc.setdefault((f, e[f][i]), [0, 0, 0])
                    a[0] += 1; a[1] += fk; a[2] += sd
                for f, edges in FFEAT:
                    v = e[f][i]
                    b = 0
                    for j in range(len(edges) - 1):
                        if edges[j] <= v < edges[j + 1]:
                            b = j; break
                    else:
                        b = len(edges) - 2
                    a = acc.setdefault((f, "[%g,%g)" % (edges[b], edges[b + 1])), [0, 0, 0])
                    a[0] += 1; a[1] += fk; a[2] += sd
        print("\n==== CELL %s (per event over %d events) ====" % (cname, nev))
        for f in IFEAT + [x[0] for x in FFEAT]:
            ks = sorted([k for k in acc if k[0] == f], key=lambda k: str(k[1]))
            if len(ks) <= 1:
                continue
            print("  %-6s %s" % (f, "  ".join(
                "%s:n=%.2f fk=%.0f%% sD=%.3f" % (k[1], acc[k][0] / nev, 100.0 * acc[k][1] / acc[k][0],
                                                 acc[k][2] / nev) for k in ks)))


if __name__ == "__main__":
    main()
