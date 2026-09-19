#!/usr/bin/env python3
"""Rung 4: refusal of TRUE pairs by each builder cut against the TRUE impact parameter of the sim track (from its
production vertex and momentum), split all-primary hit pairs / electrons.   usage: d0true.py <t4|t5> <npz>   READ-ONLY"""
import sys

import numpy as np

import rules


def true_d0(d, tk):
    R = d["trk_pt"] / (0.299792458 * 3.8) * 100.0
    q = np.sign(d["trk_q"])
    cx = d["trk_vx"] + q * R * np.sin(d["trk_phi"])
    cy = d["trk_vy"] - q * R * np.cos(d["trk_phi"])
    return np.abs(np.hypot(cx, cy) - R)[tk]


def main():
    kind = sys.argv[1]
    d = np.load(sys.argv[2], allow_pickle=True)
    rec, tk, lp, prim = d["rec"], d["tk"], d["lp"], d["prim"]
    parts = rules.t4_parts(rec) if kind == "t4" else rules.t5_parts(rec)
    parts.pop("start", None)
    m0 = (tk >= 0) & np.all(lp >= 0.8, axis=1)
    tkc = np.maximum(tk, 0)
    d0 = true_d0(d, tkc)
    ele = np.abs(d["trk_pdg"][tkc]) == 11
    allprim = np.all(prim, axis=1)
    for lab, sel in (("all true pairs (criterion)", m0), ("all-primary, not electrons", m0 & allprim & ~ele), ("an alternate hit pair", m0 & ~allprim), ("electrons", m0 & ele)):
        print("\n### %s -- %s: share refused by each cut vs TRUE |d0|" % (kind.upper(), lab))
        print("| true d0 cm | pairs | " + " | ".join(parts) + " | made |")
        print("|---|---|" + "---|" * (len(parts) + 1))
        for lo, hi in ((0, 0.5), (0.5, 2), (2, 4), (4, 8), (8, 16), (16, 60)):
            s = sel & (d0 >= lo) & (d0 < hi)
            if s.sum() < 20:
                continue
            print("| %g-%g | %d | %s | %.4f |" % (lo, hi, s.sum(), " | ".join("%.4f" % (~v)[s].mean() for v in parts.values()), (rec["objIdx"] >= 0)[s].mean()))


if __name__ == "__main__":
    main()
