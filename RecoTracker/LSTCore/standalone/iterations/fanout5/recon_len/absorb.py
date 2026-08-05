#!/usr/bin/env python3
"""RECON B -- 'absorbable duplicate' ceiling.

LST reaches 12/14 OT hits per T5 via ExtendT5FromDupT5 (Kernels.h): a winner T5 grafts
the extra logical-layer slot of a DUPLICATE T5 (>= 8 shared hits) into itself, so the
duplicate is absorbed instead of merely killed. Params_T5::kLayers = 7 = "5 base + max
2 extensions". 79% of LST barrel T5-class TCs are extended.

This script measures the analogous ceiling for the prototype: for every sim where our
best TC is SHORTER than LST's, is the missing layer already present on ANOTHER of our
own TCs that matches the same sim (i.e. on a duplicate we could absorb)?

usage: absorb.py <proto_with_tc_hitOT.root>
"""
import collections
import sys

import numpy as np
import uproot
import awkward as ak

S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/"
BASE = S + "LSTNtuple_PU200RelVal_300evt.root"


def region(eta):
    a = abs(eta)
    return "barrel" if a < 1.1 else ("transition" if a < 1.7 else "endcap")


BB = ["sim_pt", "sim_eta", "sim_tcIdx", "tc_nhitOT", "tc_type",
      "tc_pt5Idx", "tc_pt3Idx", "tc_t5Idx", "pT5_t5Idx", "t5_hitIndices",
      "pT3_otHitIndices", "md_layer", "md_anchorHitIdx", "md_otherHitIdx", "md_isPLS", "evt"]
PB = ["sim_tcIdx", "tc_nhitOT", "tc_hitOT", "tc_simIdxAll", "evt"]


def main(proto):
    b = uproot.open(BASE)["tree"].arrays(BB, library="ak")
    p = uproot.open(proto)["tree"].arrays(PB, library="ak")
    n = min(len(b), len(p))

    short = collections.Counter()      # region -> sims where we are shorter
    covered = collections.Counter()    # region -> ... and a sibling TC of ours has the layer
    partial = collections.Counter()
    hits_rec = collections.Counter()   # region -> OT hits recoverable by absorption
    hits_short = collections.Counter()  # region -> OT hits we are short
    nsib = collections.Counter()

    for e in range(n):
        ml = ak.to_numpy(b["md_layer"][e])
        ot = ~ak.to_numpy(b["md_isPLS"][e]).astype(bool)
        mlo = ml[ot]
        h2l = {}
        for arr in (ak.to_numpy(b["md_anchorHitIdx"][e])[ot], ak.to_numpy(b["md_otherHitIdx"][e])[ot]):
            for h, L in zip(arr.tolist(), mlo.tolist()):
                h2l[h] = L

        bt_type = ak.to_numpy(b["tc_type"][e])
        b_p5, b_p3, b_t5 = (ak.to_numpy(b[k][e]) for k in ("tc_pt5Idx", "tc_pt3Idx", "tc_t5Idx"))
        p5t5 = ak.to_numpy(b["pT5_t5Idx"][e])
        t5h, p3h = b["t5_hitIndices"][e], b["pT3_otHitIndices"][e]

        def lst_hits(i):
            t = bt_type[i]
            if t == 7:
                k = b_p5[i]
                if 0 <= k < len(p5t5):
                    j = p5t5[k]
                    if 0 <= j < len(t5h):
                        return ak.to_numpy(t5h[j]).tolist()
            elif t == 4:
                j = b_t5[i]
                if 0 <= j < len(t5h):
                    return ak.to_numpy(t5h[j]).tolist()
            elif t == 5:
                k = b_p3[i]
                if 0 <= k < len(p3h):
                    return ak.to_numpy(p3h[k]).tolist()
            return None

        # sim -> our TC indices (full match list, i.e. the duplicate pool)
        sim2tc = collections.defaultdict(list)
        tsa = p["tc_simIdxAll"][e]
        for it in range(len(tsa)):
            for s in ak.to_numpy(tsa[it]).tolist():
                sim2tc[s].append(it)

        bsim, psim = ak.to_numpy(b["sim_tcIdx"][e]), ak.to_numpy(p["sim_tcIdx"][e])
        spt, seta = ak.to_numpy(b["sim_pt"][e]), ak.to_numpy(b["sim_eta"][e])
        pn, bn = ak.to_numpy(p["tc_nhitOT"][e]), ak.to_numpy(b["tc_nhitOT"][e])
        ph = p["tc_hitOT"][e]

        for s in range(len(bsim)):
            if spt[s] <= 0.9:
                continue
            bi, pi = bsim[s], psim[s]
            if bi < 0 or pi < 0 or int(pn[pi]) >= int(bn[bi]):
                continue
            bh = lst_hits(bi)
            if bh is None or pi >= len(ph):
                continue
            r = region(seta[s])
            Lb = set(h2l[h] for h in bh if h in h2l)
            Lo = set(h2l[h] for h in ak.to_numpy(ph[pi]).tolist() if h in h2l)
            miss = Lb - Lo
            if not miss:
                continue
            short[r] += 1
            hits_short[r] += 2 * len(miss)
            sibs = [j for j in sim2tc.get(s, []) if j != pi and j < len(ph)]
            nsib[r] += len(sibs)
            sl = set()
            for j in sibs:
                sl |= set(h2l[h] for h in ak.to_numpy(ph[j]).tolist() if h in h2l)
            got = miss & sl
            hits_rec[r] += 2 * len(got)
            if got == miss:
                covered[r] += 1
            elif got:
                partial[r] += 1

    print("=" * 96)
    print("ABSORBABLE-DUPLICATE CEILING  (proto = %s)" % proto.split("/")[-1])
    print("For sims where our best TC is SHORTER than LST's: is the missing layer already")
    print("carried by another of OUR OWN TCs matching the same sim (an absorbable duplicate)?")
    for r in ("barrel", "transition", "endcap"):
        if not short[r]:
            continue
        print("\n-- %s: %d short sims, %d OT hits short in total (%.1f/evt)"
              % (r, short[r], hits_short[r], hits_short[r] / 300.0))
        print("   sibling TCs per short sim  : %.2f" % (nsib[r] / short[r]))
        print("   fully   recoverable by absorption: %5d sims (%.1f%%)" % (covered[r], 100.0 * covered[r] / short[r]))
        print("   partly  recoverable              : %5d sims (%.1f%%)" % (partial[r], 100.0 * partial[r] / short[r]))
        print("   OT hits recoverable              : %5d of %d (%.1f%%)  -> %.1f hits/evt"
              % (hits_rec[r], hits_short[r], 100.0 * hits_rec[r] / hits_short[r], hits_rec[r] / 300.0))


if __name__ == "__main__":
    main(sys.argv[1])
