#!/usr/bin/env python3
"""S3: reader for the LST_CHAIN_JOIN_DUMP truth-join sidecar (magic 'PJ01').

Written by LSTEvent::dumpChainJoin, called from exactly the two places
LSTEvent::dumpChainPairs is called from, so record i of join.bin IS record i of pairs.bin.

  per event record:
    u32 magic 0x504A3031 ('PJ01'), u32 ievt, u32 nChains, u32 nT3, u32 nNodes, u32 nPls
    nNodes * [ u32 tripletIndex, u32 hit[6] ]     ph2 rows, (anchor,other) x (md0,md1,md2)
    nPls   * [ u32 seedIdx, f32 eta ]             see_* row and pixelSeeds.eta()

(nChains, nT3) is the fingerprint against the pairs.bin header of the same record.
The node table is keyed by the SPARSE TRIPLET index -- the value a stage-1 pair row carries in
`target` -- and the chain dump's `nodeItems` names the same nodes by DENSE node row, so this one
table serves both the chain targets (stage 0/2) and the bare-T3 targets (stage 1).
"""
import struct

import numpy as np

MAGIC = 0x504A3031
HDR = struct.Struct("<6I")


def iter_events(path, limit=None):
    """yield (ievt, nChains, nT3, nodes, pls) where
         nodes : (nNodes, 7) uint32 -- [tripletIndex, h0a,h0b,h1a,h1b,h2a,h2b]
         pls   : (nPls,) structured  -- ('seed','<u4'), ('eta','<f4')
    """
    plsdt = np.dtype([("seed", "<u4"), ("eta", "<f4")])
    with open(path, "rb") as f:
        k = 0
        while True:
            h = f.read(24)
            if len(h) < 24:
                break
            magic, ievt, nC, nT3, nNodes, nPls = HDR.unpack(h)
            assert magic == MAGIC, "bad magic 0x%08x at record %d" % (magic, k)
            nodes = np.frombuffer(f.read(28 * nNodes), dtype="<u4").reshape(nNodes, 7)
            pls = np.frombuffer(f.read(8 * nPls), dtype=plsdt)
            yield ievt, nC, nT3, nodes, pls
            k += 1
            if limit is not None and k >= limit:
                break


if __name__ == "__main__":
    import sys
    n = 0
    for ievt, nC, nT3, nodes, pls in iter_events(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else None):
        if n < 3:
            print("evt %d ievt %d nChains %d nT3 %d nNodes %d nPls %d  t3max %d hitmax %d seedmax %d"
                  % (n, ievt, nC, nT3, len(nodes), len(pls),
                     nodes[:, 0].max() if len(nodes) else -1,
                     nodes[:, 1:].max() if len(nodes) else -1,
                     pls["seed"].max() if len(pls) else -1))
        n += 1
    print("records", n)
