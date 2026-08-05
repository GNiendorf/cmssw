#!/usr/bin/env python3
"""B03 -- what the gate MARGINS look like inside the barrel/transition displaced-exempt
admission branches. tc_dbgMP = mP (prompt-fake margin), tc_dbgMD = mD (displaced-fake
margin). br1 is admitted by mD >= -M4D; br3 by mX = max(mP,mD) >= -MR.
Both thresholds are GLOBAL today (only the 1.1-1.7 band has deltas, -ZM4D/-ZM4)."""
import sys
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a07_ref")
from a07_sim import load, prep  # noqa: E402

PKL = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/b00_ref/cf977.pkl"
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
    # cumulative profile: for each (band, br) and each threshold on the ADMITTING margin,
    # how many rows fall below it and what fraction of them are fake / true-displaced.
    for br, mkey, mname in ((1, "md", "mD"), (3, "mx", "mX")):
        print("\n=== br%d : cut on %s (rows with %s < thr would be gate-killed) ===" % (br, mname, mname))
        print("%-4s %6s %8s %8s %8s %8s %8s %8s" %
              ("band", "thr", "kill/ev", "kFake", "kTrue", "kFk%", "kSoleD", "kSoleP"))
        for bn, _, _ in BANDS:
            for thr in (-3.0, -2.5, -2.0, -1.5, -1.0, -0.5, 0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0):
                nk = nkf = nkt = nsd = nsp = 0
                for e in ev:
                    n = len(e["type"])
                    cnt = {}
                    for i in range(n):
                        for s in e["sims"][i]:
                            cnt[s] = cnt.get(s, 0) + 1
                    for i in range(n):
                        if e["type"][i] not in CHAIN or e["br"][i] != br:
                            continue
                        if not e["incut"][i] or bandof(e["eta"][i]) != bn:
                            continue
                        m = e["md"][i] if mkey == "md" else max(e["mp"][i], e["md"][i])
                        if m >= thr:
                            continue
                        nk += 1
                        if e["fake"][i]:
                            nkf += 1
                            continue
                        nkt += 1
                        dup = any(cnt.get(s, 0) > 1 for s in e["sims"][i])
                        if dup:
                            continue
                        disp = any(abs(e["sdxy"][s]) >= 1.0 for s in e["sims"][i] if s < len(e["sdxy"]))
                        if disp:
                            nsd += 1
                        else:
                            nsp += 1
                print("%-4s %6.1f %8.3f %8.3f %8.3f %8.1f %8.3f %8.3f" %
                      (bn, thr, nk / nev, nkf / nev, nkt / nev, 100.0 * nkf / max(nk, 1),
                       nsd / nev, nsp / nev))


if __name__ == "__main__":
    main()
