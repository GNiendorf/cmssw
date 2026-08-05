#!/usr/bin/env python3
"""A02 M18 -- where the sharp-label head's retirements land, by region and by seed pT.

    m18_regions.py --pairs pairs_L0.txt --scores head_L0.txt [--head-thr T] [--att-thr 4]

Columns are the sharp label, so every number is in the harness's own currency:
dup rows removed, fake rows removed, pileup-sole rows removed (the neutral trap), and
in-cut matches lost.
"""
import argparse

import numpy as np

FEAT0 = 6  # first feature column


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pairs", required=True)
    ap.add_argument("--scores", required=True)
    ap.add_argument("--head-thr", type=float, required=True)
    ap.add_argument("--att-thr", type=float, default=4.0)
    a = ap.parse_args()

    hdr = open(a.pairs).readline().split()[1:]
    i = {n: k for k, n in enumerate(hdr)}
    best = {}
    for line in open(a.pairs):
        if line.startswith("#") or not line.endswith("\n"):
            continue
        t = line.split()
        if len(t) != len(hdr):
            continue
        k = (t[0], t[1], t[2], t[3])
        att = float(t[i["df07"]])
        rec = best.get(k)
        if rec is None:
            best[k] = [att, int(float(t[i["isDup"]])), int(float(t[i["isFake"]])),
                       int(float(t[i["nSoleCut"]])), abs(float(t[i["df01"]])),
                       10 ** float(t[i["df00"]])]
        else:
            rec[0] = max(rec[0], att)
    sc = {}
    for line in open(a.scores):
        t = line.split()
        if len(t) >= 5:
            sc[(t[0], t[1], t[2], t[3])] = float(t[4])
    keys = list(best.keys())
    V = np.array([best[k] for k in keys])
    H = np.array([sc.get(k, -1e9) for k in keys])
    nev = len(set((k[0], k[1], k[2]) for k in keys))
    att, dup, fake, sole, aeta, pt = V[:, 0], V[:, 1], V[:, 2], V[:, 3], V[:, 4], V[:, 5]
    neither = (dup == 0) & (fake == 0) & (sole == 0)
    A, Hs = att >= a.att_thr, H >= a.head_thr

    def block(name, bins, val):
        print("\n%s (%d events, seeds/evt)" % (name, nev))
        print("%-16s %7s | %7s %7s %7s %7s | %7s %7s %7s %7s"
              % ("bin", "pool", "A ret", "A dup", "A neu", "A cost",
                 "H ret", "H dup", "H neu", "H cost"))
        for lab, m in bins:
            print("%-16s %7.1f | %7.1f %7.1f %7.1f %7.2f | %7.1f %7.1f %7.1f %7.2f"
                  % (lab, m.sum() / nev,
                     (m & A).sum() / nev, dup[m & A].sum() / nev,
                     neither[m & A].sum() / nev, sole[m & A].sum() / nev,
                     (m & Hs).sum() / nev, dup[m & Hs].sum() / nev,
                     neither[m & Hs].sum() / nev, sole[m & Hs].sum() / nev))

    block("BY REGION", [("barrel <1.1", aeta < 1.1),
                        ("transition", (aeta >= 1.1) & (aeta < 1.7)),
                        ("endcap >=1.7", aeta >= 1.7)], aeta)
    block("BY SEED pT", [("0.8-1", pt < 1.0), ("1-2", (pt >= 1) & (pt < 2)),
                         ("2-5", (pt >= 2) & (pt < 5)), ("5-20", (pt >= 5) & (pt < 20)),
                         (">20", pt >= 20)], pt)
    print("\nTOTAL   pool %.1f | attach ret %.1f dup %.1f fake %.2f neu %.1f cost %.2f"
          " | head ret %.1f dup %.1f fake %.2f neu %.1f cost %.2f"
          % (len(V) / nev, A.sum() / nev, dup[A].sum() / nev, fake[A].sum() / nev,
             neither[A].sum() / nev, sole[A].sum() / nev, Hs.sum() / nev,
             dup[Hs].sum() / nev, fake[Hs].sum() / nev, neither[Hs].sum() / nev,
             sole[Hs].sum() / nev))


if __name__ == "__main__":
    main()
