#!/usr/bin/env python3
"""A07 gate-constant scan.

The -G 6 three-class gate is FULLY reconstructible from the diagnostics the writer already
dumps per TC: dbgBr (branch), dbgNL, dbgNN, dbgMP (= mP = promptLogit-fakeLogit) and
dbgMD (= mD = displacedLogit-fakeLogit); mX = max(mP, mD).

Branch rules actually in force on the assembled baseline (main.cc -G 6 block):
  br 0  nL<=4, dca <  max(-X,-Z) : kill iff mX < -M4     (-M4  = 4.0 on the frozen line)
  br 1  nL<=4, dca >= max(-X,-Z) : kill iff mD < -M4D    (-M4D = -1.2)
  br 2  nL>=5, dca <  -X         : kill iff mP < -M5/-M6 AND mX < -MRI   (-M5/-M6 = 1e9,
                                   so effectively: kill iff mX < -MRI    (-MRI = -0.5))
  br 3  nL>=5, dca >= -X         : kill iff mD < -MD AND mX < -MR        (-MD = 1e9,
                                   so effectively: kill iff mX < -MR     (-MR = -1.8))
  cell  nL==5 && nNodes==2       : additionally kill iff mP < -C25 AND mD < -C25D
                                   (-C25 = 0.0, -C25D = -2.0)

A gate kill removes the CHAIN, so it removes both the bare chain TC (deliv 1) and any
pT5-class attach delivery built on that chain (deliv 2). The simulator therefore applies
each candidate to deliv in {1,2}.

CAVEAT stated with every number here: the simulator DELETES rows, it does not re-run the
downstream arbitration. A real gate change also frees the chain's MDs for competitors and
lets the pixel attach re-target, so the simulated efficiency loss is an UPPER bound and the
simulated duplicate gain a LOWER bound. Points chosen here are confirmed with real runs.
"""
import sys
from a07_sim import load, prep, evaluate, HDR, row


def mx(e, i):
    return max(e["mp"][i], e["md"][i])


def build():
    C = []

    def add(lbl, fn):
        C.append((lbl, fn))

    chain = lambda e, i: e["deliv"][i] in (1, 2)
    # -M4  : br 0 (T4-class IP), threshold on mX. baseline 4.0
    for v in (4.5, 5.0, 6.0, 8.0):
        add("-M4 %g" % v, lambda e, i, v=v: chain(e, i) and e["br"][i] == 0 and mx(e, i) < v)
    # -M4D : br 1 (T4-class exempt/displaced), threshold on mD. baseline -1.2
    for v in (-1.0, -0.5, 0.0, 0.5, 1.0, 2.0):
        add("-M4D %g" % v, lambda e, i, v=v: chain(e, i) and e["br"][i] == 1 and e["md"][i] < v)
    # -MRI : br 2 (5+ IP), threshold on mX. baseline -0.5
    for v in (0.0, 0.5, 1.0, 2.0, 3.0):
        add("-MRI %g" % v, lambda e, i, v=v: chain(e, i) and e["br"][i] == 2 and mx(e, i) < v)
    # -MR  : br 3 (5+ exempt), threshold on mX. baseline -1.8
    for v in (-1.5, -1.0, -0.5, 0.0, 0.5, 1.0, 2.0):
        add("-MR %g" % v, lambda e, i, v=v: chain(e, i) and e["br"][i] == 3 and mx(e, i) < v)
    # -C25 / -C25D : the (nNodes==2, nLayers==5) cell. baseline (0.0, -2.0)
    for p, d in ((0.5, -2.0), (1.0, -2.0), (2.0, -2.0), (0.0, -1.0), (0.0, 0.0),
                 (1.0, -1.0), (2.0, 0.0)):
        add("-C25 %g -C25D %g" % (p, d),
            lambda e, i, p=p, d=d: (chain(e, i) and e["nl"][i] == 5 and e["nn"][i] == 2 and
                                    e["mp"][i] < p and e["md"][i] < d))
    return C


def main(pkl, only=None):
    ev = prep(load(pkl))
    base = evaluate(ev, None, "BASELINE")
    print(HDR)
    print(row(base))
    print("-" * 120)
    res = []
    for lbl, fn in build():
        if only and only not in lbl:
            continue
        m = evaluate(ev, fn, lbl)
        res.append(m)
        print(row(m))
    print("-" * 120)
    print("\nDELTAS vs baseline (sorted by fake gain)")
    print("%-24s %9s %9s %9s %9s %9s %9s %9s %9s" %
          ("label", "d_eff", "d_dup", "d_fake", "d_v15", "d_v510", "d_v1030", "d_d15", "kill/evt"))
    for m in sorted(res, key=lambda m: m["fake"] - base["fake"]):
        print("%-24s %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f %9.2f" %
              (m["label"], m["eff"] - base["eff"], m["dup"] - base["dup"],
               m["fake"] - base["fake"], m["v15"] - base["v15"], m["v510"] - base["v510"],
               m["v1030"] - base["v1030"], m["d15"] - base["d15"], m["nKill"] / float(len(ev))))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
