#!/usr/bin/env python3
"""B03 -- 2-D scan inside the barrel/transition displaced-exempt admission branches:
the ADMITTING GATE MARGIN (mD for br1, mX for br3) crossed with the other per-row
features already written to the ntuple. Goal: reduce fakB/fakT while spending only
SINGLE SIMS in every displaced band."""
import sys
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a07_ref")
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/b03_ref")
from a07_sim import load, prep  # noqa: E402
from b03_sim2 import evaluate2, HDR2, row2, DHDR, drow  # noqa: E402

PKL = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/b00_ref/cf977.pkl"
CHAIN = (4, 9)


def mkcut(br, lo, hi, thr, key, extra=None):
    def f(e, i):
        if e["type"][i] not in CHAIN or e["br"][i] != br:
            return False
        a = abs(e["eta"][i])
        if not (lo <= a < hi):
            return False
        m = e["md"][i] if key == "mD" else max(e["mp"][i], e["md"][i])
        if m >= thr:
            return False
        return extra(e, i) if extra else True
    return f


def anyof(fns):
    return lambda e, i: any(f(e, i) for f in fns)


def main():
    ev = prep(load(PKL))
    base = evaluate2(ev, None, "CHAINFINAL 977")
    print(HDR2); print(row2(base)); print()
    C = []
    # -- br1 barrel: margin x dca (b00 showed high dca in br1 is fake-rich, d15-cheap)
    for t in (0.0, 0.5, 1.0, 2.0, 3.0):
        for d in (0.0, 2.0, 5.0):
            C.append(("B br1 mD<%.1f&dca>%.0f" % (t, d),
                      mkcut(1, 0.0, 1.1, t, "mD", (lambda e, i, d=d: e["dca"][i] > d) if d else None)))
    # -- br3 barrel: margin alone and margin x dca
    for t in (-1.5, -1.0, -0.5, 0.0):
        for d in (0.0, 5.0, 10.0):
            C.append(("B br3 mX<%+.1f&dca>%.0f" % (t, d),
                      mkcut(3, 0.0, 1.1, t, "mX", (lambda e, i, d=d: e["dca"][i] > d) if d else None)))
    # -- br3 transition
    for t in (-1.5, -1.0, -0.5):
        for d in (0.0, 5.0, 10.0):
            C.append(("T br3 mX<%+.1f&dca>%.0f" % (t, d),
                      mkcut(3, 1.1, 1.7, t, "mX", (lambda e, i, d=d: e["dca"][i] > d) if d else None)))
    res = []
    for lbl, fn in C:
        res.append(evaluate2(ev, fn, lbl))
    print(DHDR)
    for m in res:
        print(drow(m, base))


if __name__ == "__main__":
    main()
