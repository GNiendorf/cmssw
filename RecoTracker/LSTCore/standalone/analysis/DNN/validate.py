#!/usr/bin/env python3
"""DISP validation: does the offline replay of bar -> per-chain argmax -> seed contention
reproduce WHICH chains came out as pixel-attached (tc_type 7) rather than bare (tc_type 4)?

If the replay is right, then for every accepted chain that carries an emitted TC:
    replay says it won a seed   <=>   its TC is type 7
Any disagreement is a bucket my census would mis-attribute, so it is counted and shown.
"""
import os
import sys

import awkward as ak
import numpy as np
import uproot

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, "/mnt/data1/gsn27/here/chain_clean/src/RecoTracker/LSTCore/standalone/analysis/DNN")
from join_dump_io import iter_events as iter_join   # noqa: E402
import census_disp as C                             # noqa: E402


def main():
    nent = int(sys.argv[1]) if len(sys.argv) > 1 else 25
    if len(sys.argv) > 2:
        C.DUMP = sys.argv[2]
        C.OURS = os.path.join(C.DUMP, "run.root")
    barfn = C.BAR_MODELS[sys.argv[3] if len(sys.argv) > 3 else "vintage"]
    print("dump %s  bars %s" % (C.DUMP, sys.argv[3] if len(sys.argv) > 3 else "vintage"))
    ot = uproot.open(C.OURS)["tree"]
    od = ot.arrays(["tc_type", "tc_hitIdx", "tc_hitType"], entry_stop=nent, library="ak")
    seept_all = uproot.open(C.NTUPLE)["trackingNtuple/tree"].arrays(
        ["see_pt"], entry_stop=nent, library="ak")["see_pt"]

    itc = C.iter_chains_hits(os.path.join(C.DUMP, "chains.bin"))
    itj = iter_join(os.path.join(C.DUMP, "join.bin"))
    itp = C.iter_pairs_light(os.path.join(C.DUMP, "pairs.bin"))

    n_t7 = n_t4 = agree7 = agree4 = 0
    bad = []
    for e in range(nent):
        ievtC, nC, nn, ni, nlay, flg, dca, hoff, chhits = next(itc)
        ievtJ, nCj, nT3j, nodes, plsrec = next(itj)
        hdr, st, tg, pl, lg = next(itp)
        assert ievtC == e and hdr["ievt"] == e
        peta = np.abs(np.asarray(plsrec["eta"]).astype(np.float64))
        pptin = np.asarray(seept_all[e]).astype(np.float64)[plsrec["seed"].astype(np.int64)]
        dispChain = ~(dca < C.NEW_DCASPLIT)
        nP = len(plsrec)
        sA = st == 0
        tgA, plA, lgA = tg[sA], pl[sA], lg[sA].astype(np.float64)
        passA = lgA >= barfn(peta[plA], pptin[plA], dispChain[tgA])
        pickPls = np.full(nC, -1, np.int64)
        pickLog = np.full(nC, -np.inf)
        if passA.any():
            t2, p2, l2 = tgA[passA], plA[passA], lgA[passA]
            o2 = np.lexsort((p2, -l2, t2))
            t2s, p2s, l2s = t2[o2], p2[o2], l2[o2]
            f2 = np.ones(len(t2s), bool)
            f2[1:] = t2s[1:] != t2s[:-1]
            pickPls[t2s[f2]] = p2s[f2]
            pickLog[t2s[f2]] = l2s[f2]
        won = np.zeros(nC, bool)
        pc = np.flatnonzero(pickPls >= 0)
        if len(pc):
            wp, wl = pickPls[pc], pickLog[pc]
            o3 = np.lexsort((pc, -wl, wp))
            wps, wcs = wp[o3], pc[o3]
            f3 = np.ones(len(wps), bool)
            f3[1:] = wps[1:] != wps[:-1]
            won[wcs[f3]] = True

        lut = {}
        for c in range(nC):
            lut.setdefault(np.unique(chhits[hoff[c]:hoff[c + 1]]).tobytes(), []).append(c)
        ty = np.asarray(od["tc_type"][e])
        for i in np.flatnonzero((ty == 4) | (ty == 7)):
            hi = np.asarray(od["tc_hitIdx"][e][i]).astype(np.uint32)
            ht = np.asarray(od["tc_hitType"][e][i]).astype(np.int64)
            cands = lut.get(np.unique(hi[ht == 4]).tobytes(), [])
            if len(cands) != 1:
                continue
            c = cands[0]
            if ty[i] == 7:
                n_t7 += 1
                if won[c]:
                    agree7 += 1
                elif len(bad) < 10:
                    bad.append(("type7 but replay says no win", e, int(c), int(nlay[c]),
                                float(dca[c]), int(pickPls[c]), float(pickLog[c])))
            else:
                n_t4 += 1
                if not won[c]:
                    agree4 += 1
                elif len(bad) < 10:
                    bad.append(("type4 but replay says WON", e, int(c), int(nlay[c]),
                                float(dca[c]), int(pickPls[c]), float(pickLog[c])))
    print("events %d" % nent)
    print("type-7 TCs %d  replay-agrees %d  (%.4f)" % (n_t7, agree7, agree7 / max(n_t7, 1)))
    print("type-4 TCs %d  replay-agrees %d  (%.4f)" % (n_t4, agree4, agree4 / max(n_t4, 1)))
    for b in bad:
        print("  MISMATCH", b)


if __name__ == "__main__":
    main()
