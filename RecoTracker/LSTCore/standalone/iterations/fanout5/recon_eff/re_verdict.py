#!/usr/bin/env python3
"""re_verdict.py -- RECON C classification of the sims the FLAGSHIP stack loses.

Consumes re_sims_<tag>.npy (written by re_sims.py) for the ladder points.
Base for the attach-ON family  : fl
Base for the attach-OFF family : noatt   (so an ablation that must run with -a 999 is
                                          never charged for the attach deliveries it lost)

Class of a sim LOST by the base, in priority order:
  FORMATION : still lost with everything off (alln)  -> no welded chain reaches it
  GATE      : recovered by g0n (3-class kills off) or t0n (legacy per-length cuts off)
  CLAIM     : recovered by c0n only (K9 hit-claim arbitration off)
  BOTH-ONLY : recovered by b0n/alln but by neither single ablation (interaction)
"""
import os
import sys

import numpy as np

P = os.path.dirname(os.path.abspath(__file__))


def key(a):
    return a["evt"].astype(np.int64) * 100000 + a["sim"].astype(np.int64)


def load(tags):
    A = {}
    for t in tags:
        A[t] = np.load(f"{P}/re_sims_{t}.npy")
    k0 = key(A[tags[0]])
    for t in tags:
        assert np.array_equal(key(A[t]), k0), t
    return A


def band_report(name, mask, a):
    n = int(mask.sum())
    if n == 0:
        return f"  {name:26s} 0"
    vx = a["vxy"][mask]
    dx = a["dxy"][mask]
    return (f"  {name:26s} {n:5d}   prompt(vxy<1)={int((vx<1).sum()):5d}"
            f"  vxy1-5={int(((vx>=1)&(vx<5)).sum()):4d}  vxy>=5={int((vx>=5).sum()):4d}"
            f"  |dxy|>=1={int((dx>=1).sum()):4d}")


def main():
    tags = sys.argv[1:]
    A = load(tags)
    a = A[tags[0]]
    ok = (np.abs(a["vz"]) < 30) & (a["q"] != 0)
    m = {t: A[t]["anyTC"] for t in tags}
    base = a["baseTC"]

    print(f"denominator (harness in-cut sims, 300 evt) = {int(ok.sum())}")
    print("per-point delivery / LST-lost counts:")
    for t in tags:
        lost = ok & base & ~m[t]
        gain = ok & ~base & m[t]
        print(f"  {t:8s} eff={m[t][ok].mean():.4f}  LOST(vs LST)={int(lost.sum()):5d}"
              f"  GAINED={int(gain.sum()):5d}  net={int(gain.sum()-lost.sum()):+5d}")

    for bt, fam in (("fl", ["g0", "tr0"]), ("noatt", ["g0n", "t0n", "c0n", "b0n", "alln"])):
        if bt not in tags:
            continue
        print(f"\n=== LOST-sim classification, base = {bt} ===")
        lost = ok & base & ~m[bt]
        print(band_report("LOST total", lost, a))
        for t in fam:
            if t in tags:
                print(band_report(f"  recovered by {t}", lost & m[t], a))
        if all(x in tags for x in ("g0n", "t0n", "c0n", "alln")) and bt == "noatt":
            gate = lost & (m["g0n"] | m["t0n"])
            claim = lost & m["c0n"] & ~gate
            form = lost & ~m["alln"]
            both = lost & ~gate & ~claim & ~form
            print("  --- priority classes ---")
            print(band_report("FORMATION (alln lost too)", form, a))
            print(band_report("GATE", gate & ~form, a))
            print(band_report("CLAIM only", claim & ~form, a))
            print(band_report("BOTH-ONLY (interaction)", both, a))
            # overlap detail
            print(f"  overlap: gate&claim(recoverable by either) = "
                  f"{int((lost & (m['g0n']|m['t0n']) & m['c0n'] & ~form).sum())}")


if __name__ == "__main__":
    main()
