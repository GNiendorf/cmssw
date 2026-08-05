#!/usr/bin/env python3
"""cs_mech.py -- mechanism check for the post-arbitration dedup.

Compares two prototype outputs (e.g. same -F, dedup off vs dedup on):
  - TC census by class (good / duplicate / fake), so the REMOVED population is
    decomposed the same way the scoreboard counts it,
  - sims matched in A but not in B (the efficiency cost of the pass) and vice versa,
  - per-sim TC multiplicity histogram.

Usage: cs_mech.py <A.root> <B.root>
"""
import sys

import numpy as np
import uproot

BR = ["sim_pt", "sim_eta", "sim_vx", "sim_vy", "sim_vz", "sim_q", "sim_tcIdx",
      "tc_type", "tc_isChain", "tc_isFake", "tc_isDuplicate", "tc_simIdxAll",
      "lumi", "evt"]


def load(path):
    return uproot.open(path)["tree"].arrays(BR, library="np")


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    bmap = {(int(b["lumi"][i]), int(b["evt"][i])): i for i in range(len(b["evt"]))}

    lost = gained = denom = 0
    cen = {"A": np.zeros(4, dtype=np.int64), "B": np.zeros(4, dtype=np.int64)}  # tot,good,dup,fake
    cenC = {"A": np.zeros(4, dtype=np.int64), "B": np.zeros(4, dtype=np.int64)}  # chain TCs only
    multA = np.zeros(12, dtype=np.int64)
    multB = np.zeros(12, dtype=np.int64)
    lostRows = []
    for i in range(len(a["evt"])):
        j = bmap[(int(a["lumi"][i]), int(a["evt"][i]))]
        pt, eta = a["sim_pt"][i], a["sim_eta"][i]
        vz, q = a["sim_vz"][i], a["sim_q"][i]
        vxy = np.hypot(a["sim_vx"][i], a["sim_vy"][i])
        ok = (pt > 0.9) & (np.abs(eta) < 4.5) & (np.abs(vz) < 30) & (q != 0)
        mA = a["sim_tcIdx"][i] >= 0
        mB = b["sim_tcIdx"][j] >= 0
        denom += int(ok.sum())
        lo = ok & mA & ~mB
        lost += int(lo.sum())
        gained += int((ok & ~mA & mB).sum())
        for s in np.nonzero(lo)[0]:
            lostRows.append((float(pt[s]), float(eta[s]), float(vxy[s])))
        for tagname, arr, k in (("A", a, i), ("B", b, j)):
            fk = arr["tc_isFake"][k].astype(bool)
            dp = arr["tc_isDuplicate"][k].astype(bool)
            ch = arr["tc_isChain"][k].astype(bool)
            n = len(fk)
            cen[tagname] += np.array([n, int((~fk & ~dp).sum()), int(dp.sum()), int(fk.sum())])
            cenC[tagname] += np.array([int(ch.sum()), int((ch & ~fk & ~dp).sum()),
                                       int((ch & dp).sum()), int((ch & fk).sum())])
        for arr, mult, k in ((a, multA, i), (b, multB, j)):
            nS = len(arr["sim_pt"][k])
            cnt = np.zeros(nS, dtype=np.int32)
            for sl in arr["tc_simIdxAll"][k]:
                for s in sl:
                    if 0 <= s < nS:
                        cnt[s] += 1
            okk = (arr["sim_pt"][k] > 0.9) & (np.abs(arr["sim_eta"][k]) < 4.5) & \
                  (np.abs(arr["sim_vz"][k]) < 30) & (arr["sim_q"][k] != 0)
            for c in cnt[okk]:
                mult[min(int(c), 11)] += 1

    print("A = %s" % sys.argv[1])
    print("B = %s" % sys.argv[2])
    print("denominator sims (pt>0.9,|eta|<4.5,|vz|<30,q!=0): %d" % denom)
    print("%-12s %10s %10s %10s %10s" % ("census", "nTC", "good", "duplicate", "fake"))
    for t in ("A", "B"):
        print("%-12s %10d %10d %10d %10d" % ("all TC " + t, *cen[t]))
    print("%-12s %10d %10d %10d %10d" % ("removed", *(cen["A"] - cen["B"])))
    for t in ("A", "B"):
        print("%-12s %10d %10d %10d %10d" % ("chainTC " + t, *cenC[t]))
    print("%-12s %10d %10d %10d %10d" % ("rm chain", *(cenC["A"] - cenC["B"])))
    print("sims matched in A only (LOST by B) = %d" % lost)
    print("sims matched in B only (gained)    = %d" % gained)
    print("net sims = %+d" % (gained - lost))
    print("per-sim TC multiplicity (denominator sims):")
    print("   nTC :" + "".join("%8d" % k for k in range(6)) + "     >=6")
    print("   A   :" + "".join("%8d" % multA[k] for k in range(6)) + "%8d" % multA[6:].sum())
    print("   B   :" + "".join("%8d" % multB[k] for k in range(6)) + "%8d" % multB[6:].sum())
    if lostRows:
        arr = np.array(lostRows)
        print("lost sims: median pt=%.2f |eta|=%.2f vxy=%.2f ; vxy>=1 frac=%.2f (n=%d)" %
              (np.median(arr[:, 0]), np.median(np.abs(arr[:, 1])), np.median(arr[:, 2]),
               float((arr[:, 2] >= 1).mean()), len(arr)))


if __name__ == "__main__":
    main()
