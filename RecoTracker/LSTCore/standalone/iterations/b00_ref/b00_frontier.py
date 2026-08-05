#!/usr/bin/env python3
"""B00 -- decode the -XCD 4 B00ROW lines into the EX-ANTE FRONTIER of a band-tightened
retirement threshold.

For each band and each logit variable it prints, as a function of the threshold t,
how many CURRENTLY SURVIVING bare seeds have logit >= t, split by truth class:

  A_seedlessCover : the seed's sim already has a delivered SEEDLESS chain TC.  Retiring it
                    removes a duplicate PAIR (2 dup-TC counts, 1 TC) at ZERO efficiency cost.
  A_otherCover    : covered only by a seeded chain / pT3-class row -- same, still zero cost.
  B_noCover       : no chain-family cover.  Retiring it is an efficiency loss UNLESS the sim
                    is also covered by another bare seed -> UPPER BOUND on the eff price.
  C_noTruth       : no sim match.  Retiring it removes a fake row.  Free.

Usage: b00_frontier.py <log-with-B00ROW-lines> [nevents]
"""
import sys
from collections import defaultdict

BANDS = ["e00_06", "e06_11", "e11_17", "e17_24", "e24_up"]
CLS = ["A_seedlessCover", "A_otherCover", "B_noCover", "C_noTruth"]
FATE = ["consumed", "RPSblock", "XCretire", "SURVIVE"]
VARS = ["bestChainLogit(-RPSA)", "bestT3Logit(-RPST)", "bestSeedlessInWin(-XCT)", "bestSeedlessAnyDR",
        "bidMarginInWin", "bidMarginAnyDR"]
LO, STEP, NB = -2.0, 0.5, 25


def main():
    path = sys.argv[1]
    h = defaultdict(lambda: [0] * 26)
    nev = None
    for ln in open(path):
        if ln.startswith("B00 XCD4 nSeeds"):
            nev = float(ln.split()[-2])
        if not ln.startswith("B00ROW "):
            continue
        f = ln.split()
        band, cls, fate, var = f[1], f[2], f[3], f[4]
        vals = [int(x) for x in f[5:31]]
        for k in range(26):
            h[(band, cls, fate, var)][k] += vals[k]
    if len(sys.argv) > 2:
        nev = float(sys.argv[2])
    if not nev:
        nev = 1.0
    print("events = %.0f" % nev)

    GROUPS = [("BARREL |eta|<1.1", ["e00_06", "e06_11"]),
              ("TRANS 1.1-1.7", ["e11_17"]),
              ("ENDCAP >1.7", ["e17_24", "e24_up"])]
    if "--fine" in sys.argv:
        GROUPS = [(b, [b]) for b in BANDS]

    # --- census of the universe, per band group, per class, per fate -----------------
    print("\n=== UNIVERSE (post-deletion bare seeds), per event ===")
    print("%-18s %-16s %9s %9s %9s %9s %9s" % ("group", "class", "N", *FATE))
    for gname, bl in GROUPS:
        for c in CLS:
            tot = [0.0] * 4
            for fi, fa in enumerate(FATE):
                for b in bl:
                    tot[fi] += sum(h[(b, c, fa, VARS[0])])
            n = sum(tot)
            if n == 0:
                continue
            print("%-18s %-16s %9.2f %9.2f %9.2f %9.2f %9.2f" %
                  (gname, c, n / nev, tot[0] / nev, tot[1] / nev, tot[2] / nev, tot[3] / nev))

    # --- the frontier: SURVIVE-fate seeds with logit >= t ----------------------------
    # sims lost per class-B bare seed retired, measured with the A07 removal simulator on
    # the CHAINFINAL 977 (b00_ref/bprice977.txt); eff denominator 73782 sims / 977 evts.
    EFFCOST = {"BARREL |eta|<1.1": 0.03551, "TRANS 1.1-1.7": 0.03904, "ENDCAP >1.7": 0.01709}
    EFFDEN = 73782.0 / 977.0

    for var in (VARS[0], VARS[2], VARS[3], VARS[4], VARS[5]):
        print("\n=== FRONTIER  var=%s  (fate=SURVIVE only), per event ===" % var)
        print("%-18s %6s %9s %9s %9s %9s %9s %9s %10s" %
              ("group", "thr", "A_seedl", "A_other", "B_noCov", "C_noTru", "A/B", "dupTCgain", "d_eff_est"))
        for gname, bl in GROUPS:
            cum = {}
            for c in CLS:
                v = [0] * 26
                for b in bl:
                    for k in range(26):
                        v[k] += h[(b, c, "SURVIVE", var)][k]
                cum[c] = v
            for ti in range(NB + 1):
                t = LO + STEP * ti
                counts = {}
                for c in CLS:
                    counts[c] = sum(cum[c][1 + k] for k in range(NB) if LO + STEP * k >= t - 1e-6)
                a1, a2, bb, cc = (counts[CLS[0]], counts[CLS[1]], counts[CLS[2]], counts[CLS[3]])
                if a1 + a2 + bb + cc == 0:
                    continue
                ratio = (a1 + a2) / bb if bb else float("inf")
                deff = -(bb / nev) * EFFCOST.get(gname, 0.035) / EFFDEN
                print("%-18s %6.1f %9.3f %9.3f %9.3f %9.3f %9.2f %9.3f %+10.5f" %
                      (gname, t, a1 / nev, a2 / nev, bb / nev, cc / nev, ratio,
                       2.0 * (a1 + a2) / nev, deff))
        print("  (dupTCgain = 2 x A retired: the seed row AND its partner both stop being"
              " duplicates; upper bound, exact when the sim has exactly 2 TCs)")


if __name__ == "__main__":
    main()
