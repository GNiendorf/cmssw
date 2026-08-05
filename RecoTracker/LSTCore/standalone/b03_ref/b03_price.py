#!/usr/bin/env python3
"""B03 -- price BAND-SPLIT gate thresholds on the displaced-exempt admission branches.
br1 admitted by mD >= -M4D (global -1.2 today, +1.2 in the 1.1-1.7 band via -ZM4D);
br3 admitted by mX >= -MR (global -1.8 today, no band delta anywhere).
Priced as a final-pass deletion with the A07 simulator (first order: the real gate also
frees the member T3s, which the simulator cannot model)."""
import sys
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a07_ref")
from a07_sim import load, prep, evaluate, HDR, row  # noqa: E402

PKL = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/b00_ref/cf977.pkl"
CHAIN = (4, 9)


def mx(e, i):
    return max(e["mp"][i], e["md"][i])


def cut(br, lo, hi, thr, key):
    def f(e, i):
        if e["type"][i] not in CHAIN or e["br"][i] != br:
            return False
        if not (lo <= abs(e["eta"][i]) < hi):
            return False
        return (e["md"][i] if key == "mD" else mx(e, i)) < thr
    return f


def both(fns):
    return lambda e, i: any(f(e, i) for f in fns)


def main():
    ev = prep(load(PKL))
    base = evaluate(ev, None, "CHAINFINAL 977")
    print(HDR)
    print(row(base))
    C = []
    for t in (-0.5, 0.0, 0.5, 1.0):
        C.append(("B br1 mD<%.1f" % t, cut(1, 0.0, 1.1, t, "mD")))
    for t in (-1.5, -1.0, -0.5, 0.0, 0.5, 1.0):
        C.append(("B br3 mX<%.1f" % t, cut(3, 0.0, 1.1, t, "mX")))
    for t in (-1.0, -0.5, 0.0, 0.5, 1.0):
        C.append(("T br3 mX<%.1f" % t, cut(3, 1.1, 1.7, t, "mX")))
    for t in (1.0, 2.0, 3.0):
        C.append(("T br1 mD<%.1f" % t, cut(1, 1.1, 1.7, t, "mD")))
    # combinations
    C.append(("BT br3 mX<0.0", both([cut(3, 0.0, 1.1, 0.0, "mX"), cut(3, 1.1, 1.7, 0.0, "mX")])))
    C.append(("BT br3 mX<-0.5", both([cut(3, 0.0, 1.1, -0.5, "mX"), cut(3, 1.1, 1.7, -0.5, "mX")])))
    C.append(("PKG A: B br1 mD<0 + B br3 mX<0",
              both([cut(1, 0.0, 1.1, 0.0, "mD"), cut(3, 0.0, 1.1, 0.0, "mX")])))
    C.append(("PKG B: +T br3 mX<0",
              both([cut(1, 0.0, 1.1, 0.0, "mD"), cut(3, 0.0, 1.1, 0.0, "mX"),
                    cut(3, 1.1, 1.7, 0.0, "mX")])))
    C.append(("PKG C: br1 mD<.5 br3 mX<.5 BT",
              both([cut(1, 0.0, 1.1, 0.5, "mD"), cut(3, 0.0, 1.1, 0.5, "mX"),
                    cut(1, 1.1, 1.7, 0.5, "mD"), cut(3, 1.1, 1.7, 0.5, "mX")])))
    C.append(("PKG D: br1 mD<-.5 br3 mX<-.5 BT",
              both([cut(1, 0.0, 1.1, -0.5, "mD"), cut(3, 0.0, 1.1, -0.5, "mX"),
                    cut(3, 1.1, 1.7, -0.5, "mX")])))
    res = []
    for lbl, fn in C:
        m = evaluate(ev, fn, lbl)
        res.append(m)
        print(row(m))
    print("\nDELTAS vs CHAINFINAL  (LST: fakB .04365 fakT .04542 dupB .00971 dupT .01308)")
    print("%-32s %8s %8s %8s %8s %8s %8s %8s %8s %8s %7s %6s" %
          ("label", "d_eff", "d_fake", "d_fakB", "d_fakT", "d_fakE", "d_dupB", "d_dupT",
           "d_v510", "d_d15", "kill", "kFk%"))
    for m in res:
        print("%-32s %+8.5f %+8.5f %+8.5f %+8.5f %+8.5f %+8.5f %+8.5f %+8.5f %+8.5f %7d %6.1f" %
              (m["label"], m["eff"] - base["eff"], m["fake"] - base["fake"],
               m["fakB"] - base["fakB"], m["fakT"] - base["fakT"], m["fakE"] - base["fakE"],
               m["dupB"] - base["dupB"], m["dupT"] - base["dupT"],
               m["v510"] - base["v510"], m["d15"] - base["d15"],
               m["nKill"], 100.0 * m["nKillFake"] / max(m["nKill"], 1)))


if __name__ == "__main__":
    main()
