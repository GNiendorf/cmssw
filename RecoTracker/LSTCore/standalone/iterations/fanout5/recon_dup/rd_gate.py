#!/usr/bin/env python3
"""rd_gate.py -- can a kinematic gate raise the share=2 purity wall in |eta| 1.5-2.5?

The -DD dedup is deliberately structure-only. This measures whether adding a dR or a
pt-consistency requirement to the share>=2 test separates the same-sim pairs (which we
want to kill) from the different-sim collateral (which we do not), and then re-runs the
validated offline -DD replica with the extra gates so the ceiling can be quoted.
"""
import argparse
from collections import Counter, defaultdict

import numpy as np
import uproot

PT_CUT = 0.9
CHAINISH = (1, 2, 3)


def slab(e):
    a = abs(e)
    return "barrel<1.1" if a < 1.1 else ("1.1-1.5" if a < 1.5 else
                                         ("1.5-2.0" if a < 2.0 else
                                          ("2.0-2.5" if a < 2.5 else ">2.5")))


def dphi(a, b):
    d = a - b
    while d > np.pi:
        d -= 2 * np.pi
    while d < -np.pi:
        d += 2 * np.pi
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--proto", required=True)
    args = ap.parse_args()
    A = uproot.open(args.proto)["tree"].arrays(
        ["tc_pt", "tc_eta", "tc_phi", "tc_type", "tc_nhitOT", "tc_simIdxAll", "tc_isFake",
         "tc_isChain", "tc_hitOT", "sim_pca_dxy", "sim_tcIdx"], library="np")

    DRB = [(0.0, 0.005), (0.005, 0.01), (0.01, 0.02), (0.02, 0.05), (0.05, 9.0)]
    PRB = [(0.0, 0.05), (0.05, 0.10), (0.10, 0.20), (0.20, 0.50), (0.50, 9.0)]
    drt = defaultdict(Counter)
    prt = defaultdict(Counter)
    joint = defaultdict(Counter)

    for i in range(len(A["tc_pt"])):
        pt, eta, phi = A["tc_pt"][i], A["tc_eta"][i], A["tc_phi"][i]
        isch, hits, sia = A["tc_isChain"][i], A["tc_hitOT"][i], A["tc_simIdxAll"][i]
        rows = [k for k in range(len(pt)) if int(isch[k]) in CHAINISH and pt[k] > PT_CUT]
        hset = {k: set(hits[k]) for k in rows}
        sims = {k: set(int(s) for s in sia[k]) for k in rows}
        h2c = defaultdict(list)
        for k in rows:
            for h in hset[k]:
                h2c[h].append(k)
        cand = set()
        for h, ks in h2c.items():
            if len(ks) < 2:
                continue
            for x in range(len(ks)):
                for y in range(x + 1, len(ks)):
                    cand.add((min(ks[x], ks[y]), max(ks[x], ks[y])))
        for (x, y) in cand:
            if len(hset[x] & hset[y]) < 2:
                continue          # share=2 population only (the -DD 2 target)
            sl = slab(0.5 * (abs(eta[x]) + abs(eta[y])))
            same = "same" if (sims[x] & sims[y]) else "diff"
            dr = float(np.hypot(eta[x] - eta[y], dphi(phi[x], phi[y])))
            pr = abs(pt[x] - pt[y]) / max(min(pt[x], pt[y]), 1e-6)
            for lo, hi in DRB:
                if lo <= dr < hi:
                    drt[(sl, "%.3f-%.3f" % (lo, hi))][same] += 1
                    break
            for lo, hi in PRB:
                if lo <= pr < hi:
                    prt[(sl, "%.2f-%.2f" % (lo, hi))][same] += 1
                    break
            joint[(sl, dr < 0.02, pr < 0.20)][same] += 1

    def show(tab, bands, title):
        print("\n" + title)
        print("%-12s | %s" % ("slab", " ".join("%17s" % b for b in bands)))
        for sl in ("barrel<1.1", "1.1-1.5", "1.5-2.0", "2.0-2.5"):
            cells = []
            for b in bands:
                c = tab[(sl, b)]
                n = c["same"] + c["diff"]
                cells.append("%6d %10s" % (n, ("p=%.3f" % (c["same"] / n)) if n else "  -  "))
            print("%-12s | %s" % (sl, " ".join("%17s" % c for c in cells)))

    show(drt, ["%.3f-%.3f" % b for b in DRB], "share=2 chain pairs: n / same-sim purity vs dR")
    show(prt, ["%.2f-%.2f" % b for b in PRB], "share=2 chain pairs: n / same-sim purity vs |dpt|/min(pt)")

    print("\njoint gate (dR<0.02 AND |dpt|/pt<0.20) on share=2 pairs")
    print("%-12s %10s %10s %8s %10s %10s %8s" % ("slab", "IN n", "IN same", "purity",
                                                 "OUT n", "OUT same", "purity"))
    for sl in ("barrel<1.1", "1.1-1.5", "1.5-2.0", "2.0-2.5"):
        ic = joint[(sl, True, True)]
        oc = Counter()
        for k in ((False, False), (False, True), (True, False)):
            oc.update(joint[(sl, k[0], k[1])])
        ni, no = ic["same"] + ic["diff"], oc["same"] + oc["diff"]
        print("%-12s %10d %10d %8s %10d %10d %8s"
              % (sl, ni, ic["same"], ("%.3f" % (ic["same"] / ni)) if ni else "-",
                 no, oc["same"], ("%.3f" % (oc["same"] / no)) if no else "-"))


if __name__ == "__main__":
    main()
