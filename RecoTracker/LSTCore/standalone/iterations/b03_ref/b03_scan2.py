#!/usr/bin/env python3
"""B03 -- price the STRUCTURAL cells the feature profile exposed:
  * br1 innermost-layer: a 4-layer LARGE-DCA chain that starts at layer 1/2 is
    geometrically inconsistent with a displaced track (which loses its inner layers);
    those rows are 75% fake and carry almost no sole-displaced cover.
  * br3 displaced-margin: the -MD arm of the exempt rule is DISABLED today (-MD 1e9),
    so a row is admitted on mX alone even when the DISPLACED head rejects it.
"""
import sys
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a07_ref")
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/b03_ref")
from a07_sim import load, prep  # noqa: E402
from b03_sim2 import evaluate2, HDR2, row2, DHDR, drow  # noqa: E402

PKL = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/b00_ref/cf977.pkl"
CHAIN = (4, 9)


def cell(br, lo, hi, pred):
    def f(e, i):
        return (e["type"][i] in CHAIN and e["br"][i] == br and lo <= abs(e["eta"][i]) < hi
                and pred(e, i))
    return f


def anyof(fns):
    return lambda e, i: any(f(e, i) for f in fns)


def main():
    ev = prep(load(PKL))
    base = evaluate2(ev, None, "CHAINFINAL 977")
    print(HDR2); print(row2(base)); print()
    C = []
    # --- br1: innermost-layer structure ---
    for L in (1, 2):
        C.append(("B br1 inlay<=%d" % L, cell(1, 0.0, 1.1, lambda e, i, L=L: e["inlay"][i] <= L)))
    C.append(("T br1 inlay<=2", cell(1, 1.1, 1.7, lambda e, i: e["inlay"][i] <= 2)))
    # br1 inlay<=2 crossed with the admitting margin
    for t in (-0.5, 0.0, 1.0, 3.0):
        C.append(("B br1 inlay<=2&mD<%+.1f" % t,
                  cell(1, 0.0, 1.1, lambda e, i, t=t: e["inlay"][i] <= 2 and e["md"][i] < t)))
    # --- br3: the disabled displaced-margin arm ---
    for t in (-2.0, -1.0, -0.5, 0.0):
        C.append(("B br3 mD<%+.1f" % t, cell(3, 0.0, 1.1, lambda e, i, t=t: e["md"][i] < t)))
    for t in (-2.0, -1.0, -0.5):
        C.append(("T br3 mD<%+.1f" % t, cell(3, 1.1, 1.7, lambda e, i, t=t: e["md"][i] < t)))
    # br3: BOTH heads must be bad (mD low AND mP low) -- the true AND rule
    for td, tp in ((-1.0, 0.0), (-1.0, -1.0), (0.0, 0.0), (0.0, -1.0), (-2.0, 0.0)):
        C.append(("B br3 mD<%+.0f&mP<%+.0f" % (td, tp),
                  cell(3, 0.0, 1.1, lambda e, i, td=td, tp=tp: e["md"][i] < td and e["mp"][i] < tp)))
    for td, tp in ((-1.0, 0.0), (-1.0, -1.0), (0.0, 0.0)):
        C.append(("T br3 mD<%+.0f&mP<%+.0f" % (td, tp),
                  cell(3, 1.1, 1.7, lambda e, i, td=td, tp=tp: e["md"][i] < td and e["mp"][i] < tp)))
    res = []
    for lbl, fn in C:
        res.append(evaluate2(ev, fn, lbl))
    print(DHDR)
    for m in res:
        print(drow(m, base))


if __name__ == "__main__":
    main()
