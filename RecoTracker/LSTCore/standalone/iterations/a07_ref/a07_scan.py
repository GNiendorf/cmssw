#!/usr/bin/env python3
"""A07 free-win scan: evaluate a library of STRUCTURAL final-pass filters with the
validated removal simulator and rank them by (fake gained) at (efficiency and displaced
efficiency held).

Every predicate here is structural -- delivery class, object type, layer/module counts
from the chain's own composition, the -G 6 admission branch, the object's own pt/eta.
No deltaR, no proximity, no embedding.
"""
import sys
from a07_sim import load, prep, evaluate, HDR, row

D_CARRIED, D_CHAIN, D_AT5, D_AT3, D_ZP8 = 0, 1, 2, 3, 4


def build():
    C = []

    def add(lbl, fn):
        C.append((lbl, fn))

    # ---- whole-class ablations (diagnostic upper bounds, NOT proposals) ----
    add("X: drop all attachT3", lambda e, i: e["deliv"][i] == D_AT3)
    add("X: drop all chain T4", lambda e, i: e["deliv"][i] == D_CHAIN and e["type"][i] == 9)
    add("X: drop all zp8 pLS", lambda e, i: e["deliv"][i] == D_ZP8)
    add("X: drop chainT5 nhit10", lambda e, i: e["deliv"][i] == D_CHAIN and e["nhit"][i] == 10)

    # ---- chain T4 by admission branch ----
    for b in (0, 1):
        add("T4 br==%d" % b,
            lambda e, i, b=b: e["deliv"][i] == D_CHAIN and e["type"][i] == 9 and e["br"][i] == b)
    # ---- chain T4 by PS-module count (structural: module type composition) ----
    for k in (1, 2, 3):
        add("T4 nPS<=%d" % k,
            lambda e, i, k=k: e["deliv"][i] == D_CHAIN and e["type"][i] == 9 and e["nps"][i] <= k)
    for k in (1, 2, 3):
        add("T4 br1 & nPS<=%d" % k,
            lambda e, i, k=k: (e["deliv"][i] == D_CHAIN and e["type"][i] == 9 and
                               e["br"][i] == 1 and e["nps"][i] <= k))
    # ---- chain T4 by barrel-MD count ----
    for k in (2, 3, 4):
        add("T4 nB>=%d" % k,
            lambda e, i, k=k: e["deliv"][i] == D_CHAIN and e["type"][i] == 9 and e["nb"][i] >= k)
    add("T4 br1 & nB==4",
        lambda e, i: (e["deliv"][i] == D_CHAIN and e["type"][i] == 9 and e["br"][i] == 1 and
                      e["nb"][i] == 4))
    # ---- chain T4 by innermost layer ----
    for k in (1, 2, 3):
        add("T4 inLay==%d" % k,
            lambda e, i, k=k: e["deliv"][i] == D_CHAIN and e["type"][i] == 9 and e["inlay"][i] == k)
    add("T4 inLay in {2,3}",
        lambda e, i: e["deliv"][i] == D_CHAIN and e["type"][i] == 9 and e["inlay"][i] in (2, 3))
    # ---- chain T4 by pt ----
    for p in (2.0, 3.0, 5.0):
        add("T4 pt>%g" % p,
            lambda e, i, p=p: e["deliv"][i] == D_CHAIN and e["type"][i] == 9 and e["pt"][i] > p)

    # ---- chain T5 structural sub-cells ----
    for k in (1, 2):
        add("T5 nPS<=%d" % k,
            lambda e, i, k=k: e["deliv"][i] == D_CHAIN and e["type"][i] == 4 and e["nps"][i] <= k)
    add("T5 nhit10 & nPS<=2",
        lambda e, i: (e["deliv"][i] == D_CHAIN and e["type"][i] == 4 and e["nhit"][i] == 10 and
                      e["nps"][i] <= 2))
    add("T5 nhit10 & inLay>=2",
        lambda e, i: (e["deliv"][i] == D_CHAIN and e["type"][i] == 4 and e["nhit"][i] == 10 and
                      e["inlay"][i] >= 2))
    add("T5 inLay>=2",
        lambda e, i: e["deliv"][i] == D_CHAIN and e["type"][i] == 4 and e["inlay"][i] >= 2)
    add("T5 br3 & nPS<=2",
        lambda e, i: (e["deliv"][i] == D_CHAIN and e["type"][i] == 4 and e["br"][i] == 3 and
                      e["nps"][i] <= 2))
    add("T5 br3 & nhit10 & nPS<=2",
        lambda e, i: (e["deliv"][i] == D_CHAIN and e["type"][i] == 4 and e["br"][i] == 3 and
                      e["nhit"][i] == 10 and e["nps"][i] <= 2))
    add("T5 nN==2 & nPS<=2",
        lambda e, i: (e["deliv"][i] == D_CHAIN and e["type"][i] == 4 and e["nn"][i] == 2 and
                      e["nps"][i] <= 2))

    # ---- pixel-seed rows by pt (the high-pt bare-seed tail) ----
    for p in (5.0, 8.0, 10.0, 20.0):
        add("bare pLS pt>%g" % p,
            lambda e, i, p=p: e["deliv"][i] in (D_CARRIED, D_ZP8) and e["type"][i] == 8 and
            e["pt"][i] > p)
    # ---- attachT3 by pt / region ----
    for p in (3.0, 5.0, 8.0):
        add("attachT3 pt>%g" % p,
            lambda e, i, p=p: e["deliv"][i] == D_AT3 and e["pt"][i] > p)
    add("attachT3 |eta|>2.0", lambda e, i: e["deliv"][i] == D_AT3 and abs(e["eta"][i]) > 2.0)
    add("attachT3 |eta|>2.4", lambda e, i: e["deliv"][i] == D_AT3 and abs(e["eta"][i]) > 2.4)
    return C


def main(pkl, only=None):
    ev = prep(load(pkl))
    base = evaluate(ev, None, "BASELINE")
    print(HDR)
    print(row(base))
    print("-" * len(HDR))
    res = []
    for lbl, fn in build():
        if only and only not in lbl:
            continue
        m = evaluate(ev, fn, lbl)
        res.append(m)
        print(row(m))
    print("-" * len(HDR))
    print("\nDELTAS vs baseline (sorted by fake gain; +eff/-dup/-fake are good)")
    print("%-34s %9s %9s %9s %9s %9s %9s %9s" %
          ("label", "d_eff", "d_dup", "d_fake", "d_v510", "d_d15", "d_effB", "kill/evt"))
    for m in sorted(res, key=lambda m: m["fake"] - base["fake"]):
        print("%-34s %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f %9.2f" %
              (m["label"], m["eff"] - base["eff"], m["dup"] - base["dup"],
               m["fake"] - base["fake"], m["v510"] - base["v510"], m["d15"] - base["d15"],
               m["effB"] - base["effB"], m["nKill"] / float(len(ev))))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
