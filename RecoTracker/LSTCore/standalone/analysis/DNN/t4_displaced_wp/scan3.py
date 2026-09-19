#!/usr/bin/env python3
"""Rung 4: candidate rules with the T4 r-z cut keyed on the LAYER COMBINATION (99.5% point of true pairs of the tune
sample, never below the stock eta-binned cut).  usage: scan3.py <t4 tune> <t5 tune> [<t4 test> <t5 test>]  READ-ONLY"""
import sys

import numpy as np

import rules
from scan45 import Tracks, load, BINS

MINPAIRS = 200


def derive_regions(d4, q=0.995, vlo=0.1):
    r = d4["rec"]
    tm = (d4["tk"] >= 0) & np.all(d4["lp"] >= 0.8, axis=1) & (r["rzLinear"] == 0) & np.isfinite(r["rzChi2"])
    L = r["lstLayers"].astype(np.int64)
    code = ((L[:, 0] * 20 + L[:, 1]) * 20 + L[:, 2]) * 20 + L[:, 3]
    tab = {}
    for c in np.unique(code[tm]):
        m = tm & (code == c)
        if m.sum() >= MINPAIRS:
            tab[int(c)] = float(np.quantile(r["rzChi2"][m], q))
    return tab


def region_cut(r, tab):
    L = r["lstLayers"].astype(np.int64)
    code = ((L[:, 0] * 20 + L[:, 1]) * 20 + L[:, 2]) * 20 + L[:, 3]
    reg = np.zeros(len(r))
    for c, v in tab.items():
        reg[code == c] = v
    stock = rules.t4_rz_cut(r)
    return np.where(r["rzLinear"] == 1, stock, np.maximum(stock, reg))


def decode(c):
    out = []
    for _ in range(4):
        out.append(c % 20); c //= 20
    return tuple(out[::-1])


def main():
    d4, d5 = load(sys.argv[1]), load(sys.argv[2])
    tab = derive_regions(d4)
    print("T4 r-z regions (tune sample, >= %d true pairs): " % MINPAIRS + ", ".join("%s: %.1f" % ("-".join(map(str, decode(c))), v) for c, v in tab.items()))
    sets = [("tune", d4, d5)]
    if len(sys.argv) > 4:
        sets.append(("test", load(sys.argv[3]), load(sys.argv[4])))
    for nm, a4, a5 in sets:
        T = Tracks(a4, a5)
        r4, r5 = a4["rec"], a5["rec"]
        nt4 = ~T.true4; nt5 = a5["tk"] < 0
        cur4 = rules.allpass(rules.t4_parts(r4)); cur5 = rules.allpass(rules.t5_parts(r5))
        one = np.ones(len(r4), bool)
        s = r4["scores"]
        rc = region_cut(r4, tab)

        def t4rule(flag="off", dnn="bypass", rz=True):
            p = rules.t4_parts(r4, flag=flag, rzcut=rc if rz else None)
            p["dnn"] = one if dnn == "bypass" else (s[:, 0] < dnn)
            return rules.allpass(p)

        def t5rule(flag="both", wps=1.0):
            return rules.allpass(rules.t5_parts(r5, wp_scale=wps, flag=flag))

        print("\n## %s sample; criterion denominator; cell = T4 or T5 (T4 / T5); cost = not-true records passing, T4 x current / T5 x current" % nm)
        print("| rule | " + " | ".join(b for b, _, _ in BINS) + " | cost |\n|---|" + "---|" * (len(BINS) + 1))
        rows = [("current", cur4, cur5),
                ("R0: T4 flag off, DNN bypass, r-z regions", t4rule(), cur5),
                ("R1: as R0, DNN fake < 0.99", t4rule(dnn=0.99), cur5),
                ("R2: as R0, DNN fake < 0.98", t4rule(dnn=0.98), cur5),
                ("R3: as R0, DNN fake < 0.95", t4rule(dnn=0.95), cur5),
                ("R4: as R0, DNN fake < 0.9", t4rule(dnn=0.9), cur5),
                ("R5: as R1, flag BOTH", t4rule(dnn=0.99, flag="both"), cur5),
                ("R6: R1 + T5 DNN WP x0.5", t4rule(dnn=0.99), t5rule(wps=0.5)),
                ("R7: R1 + T5 DNN WP x0.2", t4rule(dnn=0.99), t5rule(wps=0.2)),
                ("R8: R1 + T5 DNN WP x0.1", t4rule(dnn=0.99), t5rule(wps=0.1)),
                ("R9: R1 + T5 DNN WP x0.05", t4rule(dnn=0.99), t5rule(wps=0.05)),
                ("R10: R1 + T5 DNN WP x0.1 + T5 flag off", t4rule(dnn=0.99), t5rule(wps=0.1, flag="off")),
                ("T5 alone: WP x0.1 (T4 current)", cur4, t5rule(wps=0.1)),
                ]
        for lab, a, b in rows:
            print(T.row(lab, a, b)[:-1] + " %.2f / %.2f |" % ((a & nt4).sum() / max((cur4 & nt4).sum(), 1), (b & nt5).sum() / max((cur5 & nt5).sum(), 1)))
        # object level for the T4 under R1, criterion denominator
        ok4 = np.all(a4["lp"] >= 0.8, axis=1)
        vx = a4["trk_vxy"][np.maximum(a4["tk"], 0)]
        p1 = t4rule(dnn=0.99)
        print("\nT4 OBJECT level under R1 (true pairs, criterion): " + "  ".join("%s %.4f (%d)" % (b, p1[T.true4 & ok4 & (vx >= lo) & (vx < hi)].mean(), (T.true4 & ok4 & (vx >= lo) & (vx < hi)).sum()) for b, lo, hi in BINS))
        ok5 = np.all(a5["lp"] >= 0.8, axis=1) & (a5["tk"] >= 0)
        vx5 = a5["trk_vxy"][np.maximum(a5["tk"], 0)]
        for w in (1.0, 0.5, 0.2, 0.1, 0.05):
            p = t5rule(wps=w)
            print("T5 OBJECT level, DNN WP x%g: " % w + "  ".join("%s %.4f" % (b, p[ok5 & (vx5 >= lo) & (vx5 < hi)].mean()) for b, lo, hi in BINS))


if __name__ == "__main__":
    main()
