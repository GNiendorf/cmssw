#!/usr/bin/env python3
"""B00 -- admission-branch anatomy of the seedless chain TCs (the fake driver), per band.

tc_dbgBr: 0 = 4-layer IP branch, 1 = 4-layer DISPLACED-EXEMPT, 2 = 5+-layer IP,
          3 = 5+-layer DISPLACED-EXEMPT.
For each (band, branch): rows, fakes, duplicates, and -- among the TRUE rows -- how many
match a sim with |dxy| >= 1 cm (the displaced population the exempt branches exist for)
and how many of those sims are covered ONLY by this row (its unique displaced value).
"""
import sys
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a07_ref")
from a07_sim import load, prep  # noqa: E402

PKL = sys.argv[1] if len(sys.argv) > 1 else \
    "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/b00_ref/cf977.pkl"
CHAIN = (4, 9)
BANDS = [("B", 0.0, 1.1), ("T", 1.1, 1.7), ("E", 1.7, 99.0)]


def bandof(a):
    a = abs(a)
    for n, lo, hi in BANDS:
        if lo <= a < hi:
            return n
    return "E"


def main():
    ev = prep(load(PKL))
    nev = len(ev)
    acc = {}
    for e in ev:
        n = len(e["type"])
        cnt = {}
        for i in range(n):
            for s in e["sims"][i]:
                cnt[s] = cnt.get(s, 0) + 1
        for i in range(n):
            if e["type"][i] not in CHAIN:
                continue
            if not (abs(e["eta"][i]) < 4.5 and e["pt"][i] > 0.9):
                continue
            k = (bandof(e["eta"][i]), e["br"][i])
            a = acc.setdefault(k, [0] * 7)
            a[0] += 1
            if e["fake"][i]:
                a[1] += 1
                continue
            dup = any(cnt.get(s, 0) > 1 for s in e["sims"][i])
            a[2] += 1 if dup else 0
            disp = any(abs(e["sdxy"][s]) >= 1.0 for s in e["sims"][i] if s < len(e["sdxy"]))
            if disp:
                a[3] += 1
                if not dup:
                    a[4] += 1          # sole cover of a displaced sim
            else:
                a[5] += 1
                if not dup:
                    a[6] += 1          # sole cover of a prompt sim
    print("SEEDLESS-CHAIN ADMISSION BRANCH ANATOMY  (per event, %d events)" % nev)
    print("br: 0=4lay IP  1=4lay EXEMPT  2=5lay IP  3=5lay EXEMPT")
    print("%-4s %-3s %9s %9s %8s %9s %9s %10s %9s %10s" %
          ("band", "br", "nTC", "fake", "fakefrac", "dup", "true_disp", "SOLEdisp", "true_prm", "SOLEprompt"))
    for bn, _, _ in BANDS:
        for br in (0, 1, 2, 3):
            a = acc.get((bn, br))
            if not a:
                continue
            print("%-4s %-3d %9.2f %9.2f %8.3f %9.2f %9.2f %10.2f %9.2f %10.2f" %
                  (bn, br, a[0] / nev, a[1] / nev, a[1] / max(a[0], 1), a[2] / nev,
                   a[3] / nev, a[4] / nev, a[5] / nev, a[6] / nev))


if __name__ == "__main__":
    main()
