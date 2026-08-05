#!/usr/bin/env python3
"""B00 -- REAL (non-oracle) fake cells: wholesale and feature-cut deletions of the
seedless-chain admission branches in barrel / transition, priced with the A07 simulator.
Every criterion here is one a real final-pass filter could evaluate.
"""
import sys
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a07_ref")
from a07_sim import load, prep, evaluate, HDR, row  # noqa: E402

PKL = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/b00_ref/cf977.pkl"
CHAIN = (4, 9)


def bnd(e, i, lo, hi):
    return lo <= abs(e["eta"][i]) < hi


def main():
    ev = prep(load(PKL))
    base = evaluate(ev, None, "CHAINFINAL 977")
    print(HDR)
    print(row(base))
    C = []
    for nm, lo, hi in (("B", 0.0, 1.1), ("T", 1.1, 1.7), ("BT", 0.0, 1.7)):
        for br in (1, 3):
            C.append(("R %s br%d wholesale" % (nm, br),
                      lambda e, i, lo=lo, hi=hi, br=br: (e["type"][i] in CHAIN and bnd(e, i, lo, hi) and
                                                         e["br"][i] == br)))
    # feature cuts INSIDE the fake-enriched barrel br1 cell (4-layer displaced-exempt)
    for d in (2.0, 5.0, 10.0, 20.0):
        C.append(("F B br1 & dca>%.0f" % d,
                  lambda e, i, d=d: (e["type"][i] in CHAIN and bnd(e, i, 0.0, 1.1) and
                                     e["br"][i] == 1 and e["dca"][i] > d)))
    for n in (0, 1, 2):
        C.append(("F B br1 & nPS<=%d" % n,
                  lambda e, i, n=n: (e["type"][i] in CHAIN and bnd(e, i, 0.0, 1.1) and
                                     e["br"][i] == 1 and e["nps"][i] <= n)))
    for m in (0.5, 1.0, 2.0):
        C.append(("F B br1 & mdMax>%.1f" % m,
                  lambda e, i, m=m: (e["type"][i] in CHAIN and bnd(e, i, 0.0, 1.1) and
                                     e["br"][i] == 1 and e["md"][i] > m)))
    # same feature family on the big barrel br3 cell
    for d in (5.0, 10.0, 20.0):
        C.append(("F B br3 & dca>%.0f" % d,
                  lambda e, i, d=d: (e["type"][i] in CHAIN and bnd(e, i, 0.0, 1.1) and
                                     e["br"][i] == 3 and e["dca"][i] > d)))
    for n in (0, 1):
        C.append(("F B br3 & nPS<=%d" % n,
                  lambda e, i, n=n: (e["type"][i] in CHAIN and bnd(e, i, 0.0, 1.1) and
                                     e["br"][i] == 3 and e["nps"][i] <= n)))
    res = []
    for lbl, fn in C:
        m = evaluate(ev, fn, lbl)
        res.append(m)
        print(row(m))
    print("\nDELTAS vs CHAINFINAL")
    print("%-28s %8s %8s %8s %8s %8s %8s %8s %8s %7s %7s" %
          ("label", "d_eff", "d_fake", "d_fakB", "d_fakT", "d_dup", "d_dupB", "d_v510", "d_d15", "kill", "kFk%"))
    for m in res:
        print("%-28s %+8.5f %+8.5f %+8.5f %+8.5f %+8.5f %+8.5f %+8.5f %+8.5f %7d %7.1f" %
              (m["label"], m["eff"] - base["eff"], m["fake"] - base["fake"],
               m["fakB"] - base["fakB"], m["fakT"] - base["fakT"], m["dup"] - base["dup"],
               m["dupB"] - base["dupB"], m["v510"] - base["v510"], m["d15"] - base["d15"],
               m["nKill"], 100.0 * m["nKillFake"] / max(m["nKill"], 1)))


if __name__ == "__main__":
    main()
