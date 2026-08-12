#!/usr/bin/env python3
"""S2: streaming reader for LST_CHAIN_CHAIN_DUMP (magic 'P22C').

Byte layout, verbatim from src/alpaka/LSTEvent.dev.cc::dumpChains:

  per event record:
    u32 magic 0x50323243 ('P22C'), u32 ievt, u32 nChainNodes, u32 nEdges, u32 nC
    nC * [ u32 preN, u32 nNodes, u32 nMDs, u32 nLayers, i32 branch, i32 trimAction,
           u32 flags,
           f32 score, dcaXY, zFake, zPrompt, zDisp, marginP, marginD, marginX,
           f32 features[25],
           u32 nodeItems[preN],
           u32 edgeType[preN-1],
           u32 hits[2*nMDs]        (anchor,other) per member MD, K6 order
         ]

The hit values are TRACKING-NTUPLE ph2 rows: dumpChains writes
hitIdx[mdAnchorHit[md]] and interface/LSTPrepareInput.h:262 fills hits.idxs() with
iota over the OT block in ph2 file order.

Notes that matter for the truth join and the bar refit:
  * the MD list is the deduped union of the member triplets' {m0,m1,m2} in
    FIRST-APPEARANCE order walking innermost-first (ChainWeld.h:283-303), so MD index 2
    is the innermost node's m2 -- exactly the MD whose ANCHOR hit ChainGate.h:589 takes
    |eta| of for the eta-band decision (aEtaC).  Its ph2 row is hits[4].
  * `flags` carries the SHIPPED (i.e. dumping-binary) gate's own decision:
    0x1 killed, 0x2 exempt, 0x4 eta band (inZ), 0x8 C25 cell kill (ChainsSoA.h:130-133).
  * `score` already has gateKill = 1e9 subtracted for a killed chain.
"""
import struct

import numpy as np

MAGIC = 0x50323243
NFEAT = 25
# 7 u32/i32 + 8 floats + 25 feature floats
FIX = struct.Struct("<IIIIiiI" + "f" * (8 + NFEAT))
FIXW = 7 + 8 + NFEAT          # words
HDR = struct.Struct("<IIIII")

FIXCOLS = ("preN nNodes nMDs nLayers branch drop flags score dcaXY zF zP zD mP mD mX").split()


def iter_events(path):
    """yield (ievt, nChainNodes, nEdges, fix, hits, hoff)

    fix   : (nC, 40) float64 -- the fixed block per chain, columns in dump order
            (7 integer fields first, then the 8 margins/score, then the 25 features)
    hits  : flat uint32 array of ph2 rows, chain c occupies [hoff[c], hoff[c+1])
    """
    f = open(path, "rb")
    while True:
        h = f.read(20)
        if len(h) < 20:
            break
        magic, ievt, nN, nE, nC = HDR.unpack(h)
        assert magic == MAGIC, "bad magic 0x%08x" % magic
        fix = np.empty((nC, FIXW), dtype=np.float64)
        hoff = np.zeros(nC + 1, dtype=np.int64)
        hbuf = []
        for c in range(nC):
            v = FIX.unpack(f.read(FIX.size))
            fix[c] = v
            preN = v[0]
            m = v[2]
            f.read(4 * (preN + max(preN - 1, 0)))      # nodeItems + edgeType
            hb = f.read(8 * m)
            hbuf.append(hb)
            hoff[c + 1] = hoff[c] + 2 * m
        hits = np.frombuffer(b"".join(hbuf), dtype="<u4") if hbuf else np.zeros(0, dtype="<u4")
        assert len(hits) == hoff[-1]
        yield ievt, nN, nE, fix, hits, hoff
    f.close()


def count_records(path):
    n = 0
    ievts = []
    for ievt, _, _, fix, _, _ in iter_events(path):
        n += 1
        ievts.append((ievt, len(fix)))
    return n, ievts


if __name__ == "__main__":
    import sys
    p = sys.argv[1]
    nmax = int(sys.argv[2]) if len(sys.argv) > 2 else 10**9
    tot = 0
    nrec = 0
    bad = 0
    for k, (ievt, nN, nE, fix, hits, hoff) in enumerate(iter_events(p)):
        if ievt != k:
            bad += 1
        tot += len(fix)
        nrec += 1
        if k < 3:
            print("evt %4d ievt %4d nodes %7d edges %8d chains %6d  nL[%d..%d] "
                  "killed %d exempt %d band %d  cf00==nNodes ok=%s"
                  % (k, ievt, nN, nE, len(fix), fix[:, 3].min(), fix[:, 3].max(),
                     int((np.asarray(fix[:, 6], dtype=int) & 1).sum()),
                     int((np.asarray(fix[:, 6], dtype=int) & 2).sum()),
                     int((np.asarray(fix[:, 6], dtype=int) & 4).sum()),
                     bool(np.allclose(fix[:, 15], fix[:, 1]))))
        if k + 1 >= nmax:
            break
    print("records %d, chains %d, non-contiguous ievt %d" % (nrec, tot, bad))
