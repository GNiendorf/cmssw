#!/usr/bin/env python3
"""S1: how far off-distribution is the 3-CLASS GATE after the edge swap?

Chain features 2/3/4/18 are edge-logit aggregates and the chain score is a sum of logOdds, so a
new edge head moves the gate's INPUTS while the gate's bars (m3Theta4 / m3Theta4D / m3ThetaR*)
stay where they were fitted. This compares the shipped chain dump (nnloop_ref/round1/chains.bin)
with an arm dump of the SAME events, on those inputs and on the gate margins.

Record layout ('P22C', LSTEvent.dev.cc dumpChains):
  hdr  u32 magic, u32 ievt, u32 nNodes, u32 nEdges, u32 nChains
  per chain: u32 preN, n, m, nLayers, branch, drop, flags
             f32 score, dcaXY, zF, zP, zD, mP, mD, mX
             f32 feats[25]
             u32 nodeItems[preN], u32 edgeType[preN-1], u32 hits[2*m]

Usage: chaindiff.py <shipped.bin> <arm.bin> [nevents]
"""
import sys

import numpy as np

MAGIC = 0x50323243
NFEAT = 25


def parse(path, nev):
    f = open(path, "rb")
    out = []
    for _ in range(nev):
        h = f.read(20)
        if len(h) < 20:
            break
        magic, ievt, nN, nE, nC = np.frombuffer(h, dtype=np.uint32)
        assert magic == MAGIC, hex(magic)
        rows = []
        for _c in range(int(nC)):
            a = np.frombuffer(f.read(7 * 4), dtype=np.uint32)
            preN, n, m, nLay = int(a[0]), int(a[1]), int(a[2]), int(a[3])
            branch = np.int32(a[4])
            drop = np.int32(a[5])
            flags = int(a[6])
            g = np.frombuffer(f.read(8 * 4), dtype=np.float32)
            ft = np.frombuffer(f.read(NFEAT * 4), dtype=np.float32)
            f.read(preN * 4)
            f.read(max(preN - 1, 0) * 4)
            f.read(2 * m * 4)
            rows.append((int(ievt), nLay, int(branch), flags, *[float(x) for x in g],
                         *[float(x) for x in ft]))
        out.extend(rows)
    f.close()
    cols = (["ievt", "nLayers", "branch", "flags", "score", "dcaXY", "zF", "zP", "zD", "mP", "mD", "mX"]
            + ["f%d" % i for i in range(NFEAT)])
    return np.array(out, dtype=np.float64), cols


def main():
    pa, pb = sys.argv[1], sys.argv[2]
    nev = int(sys.argv[3]) if len(sys.argv) > 3 else 200
    A, cols = parse(pa, nev)
    B, _ = parse(pb, nev)
    ia = {c: i for i, c in enumerate(cols)}
    print("shipped chains %d, arm chains %d over %d events (%+.2f%%)"
          % (len(A), len(B), nev, 100.0 * (len(B) - len(A)) / len(A)))
    print("%-10s %10s %10s %10s %10s %10s %10s" %
          ("col", "ship p10", "arm p10", "ship p50", "arm p50", "ship p90", "arm p90"))
    for c in ["f2", "f3", "f4", "f18", "score", "mP", "mD", "mX", "dcaXY"]:
        a, b = A[:, ia[c]], B[:, ia[c]]
        qa, qb = np.percentile(a, [10, 50, 90]), np.percentile(b, [10, 50, 90])
        print("%-10s %10.4f %10.4f %10.4f %10.4f %10.4f %10.4f" %
              (c, qa[0], qb[0], qa[1], qb[1], qa[2], qb[2]))
    # the class populations the gate bars act on
    for tag, sel in (("4-layer (nLayers==4)", lambda M: M[:, ia["nLayers"]] == 4),
                     ("5+ layer", lambda M: M[:, ia["nLayers"]] >= 5),
                     ("far dca >= 12", lambda M: M[:, ia["dcaXY"]] >= 12.0),
                     ("dca in [0.5,12)", lambda M: (M[:, ia["dcaXY"]] >= 0.5) & (M[:, ia["dcaXY"]] < 12.0))):
        a, b = A[sel(A)], B[sel(B)]
        print("  %-22s n ship %8d arm %8d (%+.2f%%)   mX p50 %8.4f -> %8.4f   mD p50 %8.4f -> %8.4f"
              % (tag, len(a), len(b), 100.0 * (len(b) - len(a)) / max(len(a), 1),
                 np.median(a[:, ia["mX"]]) if len(a) else 0, np.median(b[:, ia["mX"]]) if len(b) else 0,
                 np.median(a[:, ia["mD"]]) if len(a) else 0, np.median(b[:, ia["mD"]]) if len(b) else 0))


if __name__ == "__main__":
    main()
