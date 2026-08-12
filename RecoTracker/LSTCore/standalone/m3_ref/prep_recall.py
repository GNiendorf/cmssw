#!/usr/bin/env python3
"""M3 (c) step 1: build the compact per-event table the P3 pre-K5 recall study needs.

Joins three dumps that are all written in the same event order, one record per event:
  edgefeat.bin ('P21F')  -> the 14 edge features + the 13-float node feature table
  edges.bin    ('P21E')  -> logOdds (the K5 MLP scalar the weld argmaxes on)
  chains.bin   ('P22C')  -> the pre-trim node run of every chain == THE WELDED EDGES (the label)

Both edge dumps skip type==0 rows in the same loop order, so row i of one is row i of the other;
that is asserted on (inner,outer), not assumed.

Output: one .npz with the concatenated edge table and per-event offsets.
Columns kept (all the pre-score needs, nothing else):
  inner, outer (u32, node ids, per-event local)   evoff (event boundaries)
  etype (u8)
  f1 dKappa, f2 dKappaRel, f3 chargeAgree, f4 dTanLambda, f5 kinkPhi, f6 kinkTheta,
  f7 centerDist, f8 centerDistRel, f9 sharedLayer, f10 sharedIsPS, f12 degIn, f13 degOut
  logOdds (f32), welded (u8)

usage: prep_recall.py <dumpdir> <nevents> <out.npz>
"""
import sys

import numpy as np

sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/nnloop_ref/s1_work")
sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/m3_ref")
from dumpio import iter_feat, iter_edges          # noqa: E402
from chainnodes import iter_chain_nodes, welded_pairs  # noqa: E402

FEATCOLS = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13]   # skip 0 (etype, kept separately) and 11 (=f9<=6)
FEATNAMES = ["dKappa", "dKappaRel", "chargeAgree", "dTanLambda", "kinkPhi", "kinkTheta",
             "centerDist", "centerDistRel", "sharedLayer", "sharedIsPS", "degIn", "degOut"]


def main(d, nev, out):
    fi = iter_feat(d + "/edgefeat.bin")
    ei = iter_edges(d + "/edges.bin")
    ci = iter_chain_nodes(d + "/chains.bin")

    I, O, T, F, L, W, NN = [], [], [], [], [], [], []
    off = [0]
    nweld_tot = nweld_hit = 0
    for k in range(nev):
        try:
            ievt, nf, ein, eout, etyp, ef = next(fi)
            jevt, nN, nE1, nE2, xin, xout, xtyp, lo = next(ei)
            cevt, cnN, cnE, runs, flags = next(ci)
        except StopIteration:
            break
        assert ievt == jevt == cevt == k, (ievt, jevt, cevt, k)
        assert len(ein) == len(xin), (len(ein), len(xin))
        assert np.array_equal(ein, xin) and np.array_equal(eout, xout), "edge row order differs"
        assert cnN == nN == nf.shape[0]

        # label: welded == a consecutive pair in some chain's pre-trim node run
        wa, wb = welded_pairs(runs)
        key = ein.astype(np.int64) * np.int64(nN) + eout.astype(np.int64)
        wkey = wa.astype(np.int64) * np.int64(nN) + wb.astype(np.int64)
        # some chains share an edge? no -- each edge is welded at most once, but be safe
        wkey = np.unique(wkey)
        srt = np.argsort(key, kind="stable")
        pos = np.searchsorted(key[srt], wkey)
        pos = np.clip(pos, 0, len(key) - 1)
        okm = key[srt][pos] == wkey
        welded = np.zeros(len(key), dtype=np.uint8)
        welded[srt[pos[okm]]] = 1
        nweld_tot += len(wkey)
        nweld_hit += int(okm.sum())

        I.append(ein.astype(np.uint32))
        O.append(eout.astype(np.uint32))
        T.append(etyp.astype(np.uint8))
        F.append(ef[:, FEATCOLS].astype(np.float32))
        L.append(lo.astype(np.float32))
        W.append(welded)
        NN.append(nN)
        off.append(off[-1] + len(ein))
        if k < 5 or k % 20 == 0:
            print("evt %4d nodes %7d edges %8d welded %6d/%6d joined  chains %6d"
                  % (k, nN, len(ein), int(okm.sum()), len(wkey), len(runs)), flush=True)

    print("JOIN: %d/%d welded pairs found in the edge dump (%.4f%%)"
          % (nweld_hit, nweld_tot, 100.0 * nweld_hit / max(nweld_tot, 1)))
    np.savez(out,
             inner=np.concatenate(I), outer=np.concatenate(O), etype=np.concatenate(T),
             feat=np.concatenate(F), logOdds=np.concatenate(L), welded=np.concatenate(W),
             evoff=np.array(off, dtype=np.int64), nnodes=np.array(NN, dtype=np.int64),
             featnames=np.array(FEATNAMES))
    print("wrote", out)


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]), sys.argv[3])
