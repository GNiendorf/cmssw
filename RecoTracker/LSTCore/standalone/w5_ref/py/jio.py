#!/usr/bin/env python3
"""JPR: readers for the four in-tree chain sidecars, single-event flavour.

Formats verbatim from src/alpaka/LSTEvent.dev.cc (see also nnloop_ref/s1_work/dumpio.py and
nnloop_ref/s2_work/chainio.py, which this file agrees with field for field).  Everything here
reads ONE event per file because every JPR run is `-x <i> -s 1`, so the writers' completion-order
`ievt` counters are trivially 0 and no alignment recovery is needed.

  'P21E' LST_CHAIN_EDGE_DUMP   hdr 44 B: magic ievt run lumi event(u64) nNodes nE1 nE2 nE1K nE2K
                               rec 16 B: inner outer type logOdds        (type 1 = E1, 2 = E2)
  'P21F' LST_CHAIN_FEAT_DUMP   hdr 16 B: magic ievt nNodes 13
                               nNodes*13 f32 node features (RAW)
                               then u32 nKept, u32 14
                               rec 68 B: inner outer type + 14 f32 edge features (RAW)
  'P25N' LST_CHAIN_NODE_DUMP   hdr 12 B: magic ievt nNodes
                               rec 28 B: stableId + 6 hit rows (md0a md0o md1a md1o md2a md2o)
  'P22C' LST_CHAIN_CHAIN_DUMP  hdr 20 B: magic ievt nChainNodes nEdges nC
                               per chain 160 B fixed + preN u32 nodes + (preN-1) u32 edge types
                               + 2*nMDs u32 ph2 hit rows
"""
import struct

import numpy as np

MAGIC_EDGE = 0x50323145
MAGIC_FEAT = 0x50323146
MAGIC_NODE = 0x5032354E
MAGIC_CHAIN = 0x50323243

EDGE_DT = np.dtype([("inner", "<u4"), ("outer", "<u4"), ("type", "<u4"), ("lo", "<f4")])
FEAT_DT = np.dtype([("inner", "<u4"), ("outer", "<u4"), ("type", "<u4"), ("ef", "<f4", (14,))])
CHFIX = struct.Struct("<IIIIiiI" + "f" * 33)
assert CHFIX.size == 160


def read_edges(path):
    """-> dict(nNodes, nE1, nE2, rec) for the single event in `path`."""
    with open(path, "rb") as f:
        h = np.frombuffer(f.read(44), dtype="<u4")
        assert h[0] == MAGIC_EDGE, "bad P21E magic %x" % h[0]
        nN, nE1, nE2, nE1K, nE2K = (int(x) for x in h[6:11])
        rec = np.frombuffer(f.read((nE1K + nE2K) * 16), dtype=EDGE_DT)
        assert len(rec) == nE1K + nE2K, "truncated P21E"
        extra = f.read(1)
    assert extra == b"", "more than one event in %s" % path
    return dict(nNodes=nN, nE1=nE1, nE2=nE2, nE1K=nE1K, nE2K=nE2K, rec=rec)


def read_feat(path):
    """-> dict(nNodes, nf[nNodes,13], rec) for the single event."""
    with open(path, "rb") as f:
        h = np.frombuffer(f.read(16), dtype="<u4")
        assert h[0] == MAGIC_FEAT, "bad P21F magic %x" % h[0]
        nN, kf = int(h[2]), int(h[3])
        assert kf == 13
        nf = np.frombuffer(f.read(4 * nN * kf), dtype="<f4").reshape(nN, kf)
        h2 = np.frombuffer(f.read(8), dtype="<u4")
        nK, ke = int(h2[0]), int(h2[1])
        assert ke == 14
        rec = np.frombuffer(f.read(nK * 68), dtype=FEAT_DT)
        assert len(rec) == nK, "truncated P21F"
    return dict(nNodes=nN, nf=nf, rec=rec)


def read_nodes(path):
    """-> dict(nNodes, stableId[nN], hits[nN,6])."""
    with open(path, "rb") as f:
        h = np.frombuffer(f.read(12), dtype="<u4")
        assert h[0] == MAGIC_NODE, "bad P25N magic %x" % h[0]
        nN = int(h[2])
        rec = np.frombuffer(f.read(nN * 28), dtype="<u4").reshape(nN, 7)
        assert rec.shape[0] == nN, "truncated P25N"
    return dict(nNodes=nN, stableId=rec[:, 0].copy(), hits=rec[:, 1:].copy())


CH_FIELDS = ("preN", "nNodes", "nMDs", "nLayers", "branch", "trimAction", "flags",
             "score", "dcaXY", "zF", "zP", "zD", "mP", "mD", "mX")


def read_chains(path):
    """-> dict(nChainNodes, nEdges, nC, tab (structured), runs (list), etypes (list),
              hits (list of ph2 row arrays))."""
    with open(path, "rb") as f:
        buf = f.read()
    magic, ievt, nCN, nE, nC = struct.unpack_from("<IIIII", buf, 0)
    assert magic == MAGIC_CHAIN, "bad P22C magic %x" % magic
    off = 20
    cols = {k: np.zeros(nC, dtype=(np.int64 if k in ("preN", "nNodes", "nMDs", "nLayers",
                                                     "branch", "trimAction", "flags")
                                   else np.float64)) for k in CH_FIELDS}
    feats = np.zeros((nC, 25), dtype=np.float64)
    runs, etypes, hits = [], [], []
    for c in range(nC):
        v = CHFIX.unpack_from(buf, off)
        off += 160
        for j, k in enumerate(CH_FIELDS):
            cols[k][c] = v[j]
        feats[c] = v[15:40]
        preN, m = int(v[0]), int(v[2])
        runs.append(np.frombuffer(buf, dtype="<u4", count=preN, offset=off))
        off += 4 * preN
        ne = max(preN - 1, 0)
        etypes.append(np.frombuffer(buf, dtype="<u4", count=ne, offset=off))
        off += 4 * ne
        hits.append(np.frombuffer(buf, dtype="<u4", count=2 * m, offset=off))
        off += 8 * m
    assert off == len(buf), "P22C leftover/short: off=%d len=%d (more than one event?)" % (off, len(buf))
    return dict(nChainNodes=nCN, nEdges=nE, nC=nC, cols=cols, feats=feats,
                runs=runs, etypes=etypes, hits=hits)


KILLED = 1  # kChainFlagKilled  bit0
EXEMPT = 2  # kChainFlagExempt  bit1
ETABAND = 4
CELLKILL = 8


def welded_pairs(runs):
    a, b, ci = [], [], []
    for i, r in enumerate(runs):
        if len(r) >= 2:
            a.append(r[:-1].astype(np.int64))
            b.append(r[1:].astype(np.int64))
            ci.append(np.full(len(r) - 1, i, dtype=np.int64))
    if not a:
        z = np.zeros(0, dtype=np.int64)
        return z, z, z
    return np.concatenate(a), np.concatenate(b), np.concatenate(ci)


if __name__ == "__main__":
    import sys
    d = sys.argv[1]
    e = read_edges(d + "/edges.bin")
    n = read_nodes(d + "/nodes.bin")
    c = read_chains(d + "/chains.bin")
    print("edges nNodes=%d kept=%d (E1 %d E2 %d)  lo[%.3f,%.3f]"
          % (e["nNodes"], len(e["rec"]), e["nE1K"], e["nE2K"],
             e["rec"]["lo"].min(), e["rec"]["lo"].max()))
    print("nodes nNodes=%d" % n["nNodes"])
    print("chains nC=%d  killed=%d  nLayers hist %s"
          % (c["nC"], int((c["cols"]["flags"] & KILLED).astype(bool).sum()),
             np.bincount(c["cols"]["nLayers"]).tolist()))
    try:
        fe = read_feat(d + "/feat.bin")
        print("feat nNodes=%d kept=%d" % (fe["nNodes"], len(fe["rec"])))
    except FileNotFoundError:
        print("no feat.bin")
