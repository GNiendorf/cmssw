#!/usr/bin/env python3
"""S1: streaming readers for the round-1 on-policy dumps.

Formats (verbatim from src/alpaka/LSTEvent.dev.cc):

edgefeat.bin  per event:
    u32 magic 'P21F' = 0x50323146, u32 ievt, u32 nNodes, u32 kNodeFeat(13)
    nNodes*13 float32   node feature table (RAW, pre-preprocessing)
    u32 nKept, u32 kEdgeFeat(14)
    nKept * [u32 inner, u32 outer, u32 type, 14 float32]

edges.bin     per event:
    u32 'P21E' = 0x50323145, u32 ievt, u32 run, u32 lumi, u64 event,
    u32 nNodes, u32 nE1, u32 nE2, u32 nE1Kept, u32 nE2Kept
    (nE1Kept+nE2Kept) * [u32 inner, u32 outer, u32 type, f32 logOdds]

nodes.bin     per event:
    u32 'P25N' = 0x5032354E, u32 ievt, u32 nNodes
    nNodes * [u32 stableId, (u32 anchorHit, u32 otherHit) x 3]   (MD0, MD1, MD2)

The hit rows are tracking-ntuple ph2 rows directly: LSTPrepareInput.h fills
hits.idxs() with iota(0..nHitsOT) for the OT block (interface/LSTPrepareInput.h:113),
and the OT block is ph2 in file order, so LST hit index == ph2 row.
"""
import numpy as np

MAGIC_FEAT = 0x50323146
MAGIC_EDGE = 0x50323145
MAGIC_NODE = 0x5032354E

NODE_FEAT = 13
EDGE_FEAT = 14


class Reader:
    def __init__(self, path):
        self.f = open(path, "rb")

    def u32(self, n=1):
        b = self.f.read(4 * n)
        if len(b) < 4 * n:
            return None
        return np.frombuffer(b, dtype=np.uint32, count=n)

    def raw(self, nbytes):
        b = self.f.read(nbytes)
        assert len(b) == nbytes, "truncated dump"
        return b

    def close(self):
        self.f.close()


def iter_feat(path):
    """yield (ievt, nodefeat[nNodes,13] float32, ei[u32], eo[u32], et[u32], ef[nKept,14])"""
    r = Reader(path)
    while True:
        h = r.u32(4)
        if h is None:
            break
        assert h[0] == MAGIC_FEAT, "bad feat magic %x" % h[0]
        ievt, nN, kf = int(h[1]), int(h[2]), int(h[3])
        assert kf == NODE_FEAT
        nf = np.frombuffer(r.raw(4 * nN * kf), dtype=np.float32).reshape(nN, kf)
        h2 = r.u32(2)
        nK, ke = int(h2[0]), int(h2[1])
        assert ke == EDGE_FEAT
        rec = np.frombuffer(r.raw(nK * (3 + ke) * 4), dtype=np.uint32).reshape(nK, 3 + ke)
        ei = rec[:, 0].copy()
        eo = rec[:, 1].copy()
        et = rec[:, 2].copy()
        ef = rec[:, 3:].view(np.float32).reshape(nK, ke)
        yield ievt, nf, ei, eo, et, ef
    r.close()


def iter_edges(path):
    """yield (ievt, nNodes, nE1, nE2, inner, outer, type, logOdds)"""
    r = Reader(path)
    while True:
        h = r.u32(4)
        if h is None:
            break
        assert h[0] == MAGIC_EDGE, "bad edge magic %x" % h[0]
        ievt = int(h[1])
        _ = r.u32(2)  # event u64
        h2 = r.u32(5)
        nN, nE1, nE2, nE1K, nE2K = (int(x) for x in h2)
        nK = nE1K + nE2K
        rec = np.frombuffer(r.raw(nK * 4 * 4), dtype=np.uint32).reshape(nK, 4)
        yield (ievt, nN, nE1, nE2, rec[:, 0].copy(), rec[:, 1].copy(), rec[:, 2].copy(),
               rec[:, 3].copy().view(np.float32))
    r.close()


def iter_nodes(path):
    """yield (ievt, stableId[nN], hits[nN,6])  hits = ph2 rows (a0,o0,a1,o1,a2,o2)"""
    r = Reader(path)
    while True:
        h = r.u32(3)
        if h is None:
            break
        assert h[0] == MAGIC_NODE, "bad node magic %x" % h[0]
        ievt, nN = int(h[1]), int(h[2])
        rec = np.frombuffer(r.raw(nN * 7 * 4), dtype=np.uint32).reshape(nN, 7)
        yield ievt, rec[:, 0].copy(), rec[:, 1:].copy()
    r.close()


if __name__ == "__main__":
    import sys
    d = sys.argv[1] if len(sys.argv) > 1 else (
        "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/nnloop_ref/round1")
    nmax = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    tot_e = tot_n = 0
    for i, (ievt, nN, nE1, nE2, ei, eo, et, lo) in enumerate(iter_edges(d + "/edges.bin")):
        tot_e += len(ei)
        tot_n += nN
        if i < nmax:
            print("edges evt %4d: nodes %7d  nE1 %8d nE2 %8d  kept %8d (E1 %d E2 %d)  lo[%.2f,%.2f]"
                  % (ievt, nN, nE1, nE2, len(ei), (et == 1).sum(), (et == 2).sum(), lo.min(), lo.max()))
    print("EDGES total: %d records, %d edges, %d nodes" % (i + 1, tot_e, tot_n))
