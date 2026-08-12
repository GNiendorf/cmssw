#!/usr/bin/env python3
"""M3: reader for LST_CHAIN_CHAIN_DUMP ('P22C') that RETURNS the node runs.

standalone/nnloop_ref/s2_work/chainio.py skips nodeItems (it only needs the feature block and
the MD hit rows). The P3 recall study needs exactly the thing it skips: the pre-trim node run of
every chain, because consecutive nodes in that run ARE the welded edges -- the label for the study.

Layout, verbatim from src/alpaka/LSTEvent.dev.cc::dumpChains:
  u32 'P22C', u32 ievt, u32 nChainNodes, u32 nEdges, u32 nC
  nC * [ u32 preN, nNodes, nMDs, nLayers, i32 branch, i32 drop, u32 flags,
         f32 score,dcaXY,zF,zP,zD,mP,mD,mX, f32 feat[25],
         u32 nodeItems[preN], u32 edgeType[preN-1], u32 hits[2*nMDs] ]
"""
import struct

import numpy as np

MAGIC = 0x50323243
NFEAT = 25
FIX = struct.Struct("<IIIIiiI" + "f" * (8 + NFEAT))
HDR = struct.Struct("<IIIII")


def iter_chain_nodes(path, maxevt=None):
    """yield (ievt, nChainNodes, nEdges, list_of_node_runs, flags_array)"""
    f = open(path, "rb")
    k = 0
    while True:
        h = f.read(20)
        if len(h) < 20:
            break
        magic, ievt, nN, nE, nC = HDR.unpack(h)
        assert magic == MAGIC, "bad magic 0x%08x" % magic
        runs = []
        flags = np.empty(nC, dtype=np.uint32)
        for c in range(nC):
            v = FIX.unpack(f.read(FIX.size))
            preN = v[0]
            m = v[2]
            flags[c] = v[6]
            nodeItems = np.frombuffer(f.read(4 * preN), dtype="<u4")
            f.read(4 * max(preN - 1, 0))     # edgeType
            f.read(8 * m)                    # hit rows
            runs.append(nodeItems)
        yield ievt, nN, nE, runs, flags
        k += 1
        if maxevt is not None and k >= maxevt:
            break
    f.close()


def welded_pairs(runs):
    """the (inner,outer) node pairs consumed by the weld, over all chains of one event"""
    a, b = [], []
    for r in runs:
        if len(r) >= 2:
            a.append(r[:-1])
            b.append(r[1:])
    if not a:
        return np.zeros(0, dtype=np.uint32), np.zeros(0, dtype=np.uint32)
    return np.concatenate(a).astype(np.uint32), np.concatenate(b).astype(np.uint32)


if __name__ == "__main__":
    import sys
    p = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    for ievt, nN, nE, runs, flags in iter_chain_nodes(p, n):
        a, b = welded_pairs(runs)
        print("evt %4d nodes %7d edges %8d chains %6d weldedpairs %6d  runlen[%d..%d]"
              % (ievt, nN, nE, len(runs), len(a),
                 min(len(r) for r in runs), max(len(r) for r in runs)))
