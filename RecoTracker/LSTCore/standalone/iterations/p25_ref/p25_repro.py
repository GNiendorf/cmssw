#!/usr/bin/env python3
"""P2.5 reproducibility metric: two runs of the SAME binary/flags compared on STABLE keys.

usage: p25_repro.py chain A.bin B.bin      -- welded-chain identity
       p25_repro.py tc    A.bin B.bin      -- track-candidate identity

Both legs are LST_CHAIN_CHAIN_DUMP / LST_CHAIN_TC_DUMP sidecars.

The chain key is deliberately NOT the member-node tuple used by p22_compare.py: chain-node indices
are a dense renumbering of the LST triplet slots, and those slots are handed out by an atomicAdd,
so they permute between runs even when the physics is identical. The key used here is the chain's
POST-TRIM MiniDoublet union expressed as (anchorHitRow, otherHitRow) pairs in the tracking-ntuple
hit numbering, which is input-ordered and therefore stable across runs AND across backends.

The TC key is (type, sorted outer-tracker hit rows), already fully stable.
"""
import struct
import sys
from collections import Counter

CH_MAGIC = 0x50323243
TC_MAGIC = 0x50323354
FIX = struct.Struct("<IIIIiII8f25f")
HDR = struct.Struct("<IIIII")


def read_chain(path):
    buf = open(path, "rb").read()
    events = []
    off = 0
    while off < len(buf):
        magic, ievt, nT3, nEdge, nC = HDR.unpack_from(buf, off)
        assert magic == CH_MAGIC, f"bad magic 0x{magic:08x} at {off}"
        off += HDR.size
        rows = []
        for _ in range(nC):
            v = FIX.unpack_from(buf, off)
            off += FIX.size
            preN, postN, nMD = v[0], v[1], v[2]
            nLay, branch, trim, flags = v[3], v[4], v[5], v[6]
            score = v[7]
            off += 4 * preN
            off += 4 * (preN - 1)
            mdhits = struct.unpack_from("<%dI" % (2 * nMD), buf, off)
            off += 8 * nMD
            mds = tuple(sorted((mdhits[2 * i], mdhits[2 * i + 1]) for i in range(nMD)))
            rows.append((mds, nLay, branch, trim, flags, struct.pack("<f", score)))
        events.append((ievt, nT3, nEdge, rows))
    return events


def read_tc(path):
    buf = open(path, "rb").read()
    events = []
    off = 0
    while off < len(buf):
        magic, ievt, nTC = struct.unpack_from("<III", buf, off)
        assert magic == TC_MAGIC, f"bad magic 0x{magic:08x} at {off}"
        off += 12
        rows = []
        for _ in range(nTC):
            ty, nOT = struct.unpack_from("<II", buf, off)
            off += 8
            hits = struct.unpack_from("<%dI" % nOT, buf, off) if nOT else ()
            off += 4 * nOT
            rows.append((ty, tuple(sorted(hits))))
        events.append((ievt, rows))
    return events


def compare(a, b, getrows, label, keyfn):
    n = min(len(a), len(b))
    if len(a) != len(b):
        print(f"WARNING: event count differs {len(a)} vs {len(b)}; comparing {n}")
    print(f"{'evt':>4} {'A':>7} {'B':>7} {'common':>7} {'onlyA':>6} {'onlyB':>6} {'ident%':>8}")
    tA = tB = tC = 0
    worst = (101.0, -1)
    for i in range(n):
        ca = Counter(keyfn(r) for r in getrows(a[i]))
        cb = Counter(keyfn(r) for r in getrows(b[i]))
        common = sum((ca & cb).values())
        na, nb = sum(ca.values()), sum(cb.values())
        pct = 100.0 * common / na if na else 100.0
        print(f"{i:>4} {na:>7} {nb:>7} {common:>7} {na-common:>6} {nb-common:>6} {pct:>7.2f}%")
        tA += na
        tB += nb
        tC += common
        if pct < worst[0]:
            worst = (pct, i)
    print()
    print(f"{label} TOTAL A={tA} B={tB} common={tC}")
    print(f"{label} IDENTITY = {100.0*tC/tA if tA else 100.0:.4f}%  (worst event {worst[1]}: {worst[0]:.2f}%)")
    return tC == tA == tB


def pooled(a, b, getrows, label, keyfn):
    """Event-order-insensitive identity. Needed for the multi-stream legs: with -s N the sidecar
    event counter is an atomic, so the RECORD order follows completion order, not input order."""
    ca = Counter(keyfn(r) for ev in a for r in getrows(ev))
    cb = Counter(keyfn(r) for ev in b for r in getrows(ev))
    common = sum((ca & cb).values())
    na, nb = sum(ca.values()), sum(cb.values())
    print(f"{label} POOLED A={na} B={nb} common={common} onlyA={na-common} onlyB={nb-common}")
    print(f"{label} POOLED IDENTITY = {100.0*common/na if na else 100.0:.4f}%")
    return common == na == nb


def main():
    what, pa, pb = sys.argv[1], sys.argv[2], sys.argv[3]
    if what == "chainpool":
        a, b = read_chain(pa), read_chain(pb)
        ok = pooled(a, b, lambda e: e[3], "CHAIN(mds)", lambda r: r[0])
        return 0 if ok else 1
    if what == "tcpool":
        a, b = read_tc(pa), read_tc(pb)
        ok = pooled(a, b, lambda e: e[1], "TC", lambda r: r)
        return 0 if ok else 1
    if what == "chain":
        a, b = read_chain(pa), read_chain(pb)
        print("nT3 / nEdgeRows per event:")
        for i in range(min(len(a), len(b))):
            flag = "" if (a[i][1], a[i][2]) == (b[i][1], b[i][2]) else "   <-- UPSTREAM DIFFERS"
            print(f"  evt {i:>3}  nT3 {a[i][1]}/{b[i][1]}   nEdge {a[i][2]}/{b[i][2]}{flag}")
        print()
        print("=== (1) chain identity on the stable MD-hit key alone ===")
        ok1 = compare(a, b, lambda e: e[3], "CHAIN(mds)", lambda r: r[0])
        print()
        print("=== (2) chain identity on MD key + nLayers + branch + trim + flags + score bits ===")
        ok2 = compare(a, b, lambda e: e[3], "CHAIN(full)", lambda r: r)
        return 0 if (ok1 and ok2) else 1
    elif what == "tc":
        a, b = read_tc(pa), read_tc(pb)
        ok = compare(a, b, lambda e: e[1], "TC", lambda r: r)
        return 0 if ok else 1
    else:
        print("usage: p25_repro.py {chain|tc} A.bin B.bin")
        return 2


if __name__ == "__main__":
    sys.exit(main())
