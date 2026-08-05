#!/usr/bin/env python3
"""B03 -- fast frontier search over per-row criteria in the barrel/transition
displaced-exempt admission branches.

For every candidate row it precomputes what deleting it would cost:
  * fake?              -> the gain
  * SOLE cover of a sim in the efficiency denominator / v510 / v1030 / d15 bands
    -> the price (a row that is NOT the only surviving cover of its sim costs nothing).
This is exactly the A07 simulator's accounting for single-row deletions; overlapping
deletions of the same sim are handled by the exact simulator afterwards.
"""
import itertools
import sys
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a07_ref")
from a07_sim import load, prep, band  # noqa: E402

PKL = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/b00_ref/cf977.pkl"
CHAIN = (4, 9)


def collect(ev):
    """rows = list of (band, br, fake, costEff, costV510, costV1030, costD15, feat dict)"""
    rows = []
    for e in ev:
        n = len(e["type"])
        cnt = {}
        for i in range(n):
            for s in e["sims"][i]:
                cnt[s] = cnt.get(s, 0) + 1
        for i in range(n):
            if e["type"][i] not in CHAIN or e["br"][i] not in (1, 3) or not e["incut"][i]:
                continue
            a = abs(e["eta"][i])
            bn = "B" if a < 1.1 else ("T" if a < 1.7 else "E")
            if bn == "E":
                continue
            ce = cv5 = cv10 = cd1 = 0
            if not e["fake"][i]:
                for s in e["sims"][i]:
                    if cnt.get(s, 0) != 1 or s >= e["nsim"]:
                        continue
                    if e["sden"][s]:
                        ce += 1
                    if e["sbden"][s]:
                        if band(e["svxy"][s]) == 2: cv5 += 1
                        if band(e["svxy"][s]) == 3: cv10 += 1
                        if band(abs(e["sdxy"][s])) == 1: cd1 += 1
            rows.append((bn, e["br"][i], int(e["fake"][i]), ce, cv5, cv10, cd1,
                         e["md"][i], e["mp"][i], max(e["md"][i], e["mp"][i]), e["dca"][i],
                         e["inlay"][i], e["nps"][i], e["nb"][i], e["nn"][i], e["nl"][i],
                         e["pt"][i]))
    return rows


IDX = dict(mD=7, mP=8, mX=9, dca=10, inlay=11, nps=12, nb=13, nn=14, nl=15, pt=16)


def main():
    ev = prep(load(PKL))
    rows = collect(ev)
    nev = len(ev)
    # denominators for converting fake counts into fakB/fakT deltas
    nB = sum(1 for e in ev for i in range(len(e["type"])) if e["incut"][i] and abs(e["eta"][i]) < 1.1)
    nT = sum(1 for e in ev for i in range(len(e["type"])) if e["incut"][i] and 1.1 <= abs(e["eta"][i]) < 1.7)
    fB = sum(1 for e in ev for i in range(len(e["type"])) if e["incut"][i] and abs(e["eta"][i]) < 1.1 and e["fake"][i])
    fT = sum(1 for e in ev for i in range(len(e["type"])) if e["incut"][i] and 1.1 <= abs(e["eta"][i]) < 1.7 and e["fake"][i])
    print("denoms: nB=%d fB=%d (fakB %.5f)  nT=%d fT=%d (fakT %.5f)  nev=%d"
          % (nB, fB, fB / nB, nT, fT, fT / nT, nev))
    print("LST targets: fakB .04365 (gap %+.5f)  fakT .04542 (gap %+.5f)"
          % (fB / nB - 0.04365, fT / nT - 0.04542))

    def score(sel, bn):
        N = D = ce = cv5 = cv10 = cd1 = 0
        for r in rows:
            if r[0] != bn or not sel(r):
                continue
            N += 1; D += r[2]; ce += r[3]; cv5 += r[4]; cv10 += r[5]; cd1 += r[6]
        den, fk = (nB, fB) if bn == "B" else (nT, fT)
        return dict(n=N, nf=D, dfak=(fk - D) / (den - N) - fk / den,
                    eff=-ce, v510=-cv5, v1030=-cv10, d15=-cd1)

    # ---- exhaustive small-grid search over 2-feature conjunctions per (band, branch) ----
    GRIDS = {
        "mD": [-1.5, -1.0, -0.5, 0.0, 0.5, 1.0, 2.0],
        "mP": [-2.0, -1.0, 0.0, 1.0],
        "mX": [-1.5, -1.0, -0.5, 0.0],
        "dca": [1.0, 2.0, 5.0, 10.0],
        "inlay": [1, 2],
        "nps": [1, 2],
        "nb": [3, 4],
        "nn": [2, 3],
        "pt": [1.5, 2.0, 3.0, 5.0],
    }
    LT = ("mD", "mP", "mX")        # cut = feature <  thr
    GT = ("dca", "pt")             # cut = feature >  thr
    LE = ("inlay", "nps", "nb", "nn")  # cut = feature <= thr

    def pred(f, t):
        j = IDX[f]
        if f in LT: return lambda r: r[j] < t
        if f in GT: return lambda r: r[j] > t
        return lambda r: r[j] <= t

    for bn in ("B", "T"):
        for br in (1, 3):
            print("\n===== %s br%d  frontier (top by fake killed at each displaced budget) =====" % (bn, br))
            cands = []
            feats = list(GRIDS)
            for k in (1, 2):
                for combo in itertools.combinations(feats, k):
                    for thrs in itertools.product(*[GRIDS[f] for f in combo]):
                        ps = [pred(f, t) for f, t in zip(combo, thrs)]
                        lbl = " & ".join("%s%s%g" % (f, "<" if f in LT else (">" if f in GT else "<="), t)
                                         for f, t in zip(combo, thrs))
                        sel = lambda r, ps=ps, br=br: r[1] == br and all(p(r) for p in ps)
                        m = score(sel, bn)
                        if m["n"] == 0:
                            continue
                        m["label"] = lbl
                        cands.append(m)
            for budget in (3, 6, 10, 20, 40):
                ok = [m for m in cands
                      if abs(m["v510"]) <= budget and abs(m["v1030"]) <= budget
                      and abs(m["d15"]) <= budget and abs(m["eff"]) <= 4 * budget]
                ok.sort(key=lambda m: m["dfak"])
                print(" budget<=%d sims/band:" % budget)
                for m in ok[:4]:
                    print("   %-34s d_fak%s %+8.5f  kill %5d (%4.1f%% fk)  eff %+5d  v510 %+4d v1030 %+4d d15 %+4d"
                          % (m["label"], bn, m["dfak"], m["n"], 100.0 * m["nf"] / m["n"],
                             m["eff"], m["v510"], m["v1030"], m["d15"]))


if __name__ == "__main__":
    main()
