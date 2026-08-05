#!/usr/bin/env python3
"""B00 -- CALIBRATE the class-B seed count into real efficiency.

The -XCD 4 frontier prices a band-tightened retirement in CLASS-B SEEDS RETIRED, which is
an UPPER BOUND on efficiency: a class-B seed is one whose sim has no chain-family cover,
but the sim may still be covered by ANOTHER bare seed, or may fail the efficiency
denominator cuts entirely (pt, vz, vtx_perp, |eta|).

This measures the conversion factor directly with the A07 removal simulator: delete the
whole class-B-like bare-seed population of a band and read the efficiency actually lost
per row deleted.
"""
import sys
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a07_ref")
from a07_sim import load, prep, evaluate, HDR, row  # noqa: E402
from b00_cells import annotate  # noqa: E402

PKL = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/b00_ref/cf977.pkl"
CHAINFAM = {4, 9, 5, 7}
PLS = 8


def band(e, i, lo, hi):
    a = abs(e["eta"][i])
    return lo <= a < hi


def main():
    ev = annotate(prep(load(PKL)))
    base = evaluate(ev, None, "CHAINFINAL 977")
    print(HDR)
    print(row(base))
    CASES = []
    for nm, lo, hi in (("B", 0.0, 1.1), ("T", 1.1, 1.7), ("E", 1.7, 99.0)):
        CASES.append(("P1 %s all barePLS" % nm,
                      lambda e, i, lo=lo, hi=hi: e["type"][i] == PLS and band(e, i, lo, hi)))
        CASES.append(("P2 %s barePLS NO chainfam partner" % nm,
                      lambda e, i, lo=lo, hi=hi: (e["type"][i] == PLS and band(e, i, lo, hi) and
                                                  not (e["ptn"][i] & CHAINFAM))))
        CASES.append(("P3 %s = P2 and TRUE" % nm,
                      lambda e, i, lo=lo, hi=hi: (e["type"][i] == PLS and band(e, i, lo, hi) and
                                                  not (e["ptn"][i] & CHAINFAM) and e["fake"][i] == 0)))
        CASES.append(("P4 %s = P3 and SOLE cover" % nm,
                      lambda e, i, lo=lo, hi=hi: (e["type"][i] == PLS and band(e, i, lo, hi) and
                                                  not e["ptn"][i] and e["fake"][i] == 0)))
    res = []
    for lbl, fn in CASES:
        m = evaluate(ev, fn, lbl)
        res.append(m)
        print(row(m))
    print("\nCONVERSION: efficiency actually lost per DELETED ROW")
    print("%-34s %8s %10s %12s %12s" % ("label", "kill", "kill/evt", "d_eff", "eff_per_row"))
    for m in res:
        de = m["eff"] - base["eff"]
        print("%-34s %8d %10.2f %12.6f %12.3e" %
              (m["label"], m["nKill"], m["nKill"] / 977.0, de,
               (-de / m["nKill"] * base["effD"]) if m["nKill"] else 0.0))
    print("\n(eff_per_row = SIMS LOST PER DELETED ROW; multiply a frontier B-count by it,"
          " then by 1/%d sims to get the efficiency delta)" % base["effD"])


if __name__ == "__main__":
    main()
