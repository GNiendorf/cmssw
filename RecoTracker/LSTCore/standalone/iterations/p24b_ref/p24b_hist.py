#!/usr/bin/env python3
"""Read the LST_CHAIN_T3_HIST sidecar: per event, the logit histogram over EVERY scored bare-T3
pair, plus the unthresholded per-target best logit. Answers the head-calibration half of task 5:
where the bare-T3 pair logits actually sit, and how the target yield moves with the margin."""
import sys
import struct
import numpy as np

LO, STEP = -30.0, 0.25


def read(path):
    hist = None
    bests = []
    nev = 0
    with open(path, "rb") as f:
        data = f.read()
    off = 0
    while off < len(data):
        magic, nb = struct.unpack_from("<II", data, off)
        off += 8
        assert magic == 0x54334831, f"bad magic {magic:x} at {off}"
        h = np.frombuffer(data, dtype="<u4", count=nb, offset=off).astype(np.int64)
        off += 4 * nb
        (nt,) = struct.unpack_from("<I", data, off)
        off += 4
        b = np.frombuffer(data, dtype="<f4", count=nt, offset=off)
        off += 4 * nt
        hist = h if hist is None else hist + h
        bests.append(b)
        nev += 1
    return nev, hist, np.concatenate(bests)


def main(path):
    nev, hist, best = read(path)
    edges = LO + STEP * np.arange(len(hist) + 1)
    tot = hist.sum()
    print(f"=== bare-T3 scored-pair logit distribution, {nev} events, {tot} pairs "
          f"({tot/nev:,.0f}/evt)")
    cum = np.cumsum(hist[::-1])[::-1]
    print(f"  {'logit >=':>10s} {'pairs':>14s} {'/evt':>12s} {'frac':>10s}")
    for th in [-5, 0, 2, 3, 4, 5, 6, 6.875, 8, 10, 12, 15]:
        k = int(round((th - LO) / STEP))
        k = max(0, min(len(hist) - 1, k))
        print(f"  {th:10.3f} {cum[k]:14d} {cum[k]/nev:12.1f} {cum[k]/tot:10.6f}")
    print()
    valid = best[best > -1e29]
    print(f"=== per-target BEST logit (unthresholded), {len(best):,} targets "
          f"({len(best)/nev:,.0f}/evt), {len(valid):,} with at least one window pair")
    print(f"  {'margin':>10s} {'targets with best >= margin':>30s} {'/evt':>10s}")
    for th in [0, 2, 3, 4, 5, 5.5, 6, 6.5, 6.875, 7, 8, 9, 10, 12]:
        k = int((valid >= th).sum())
        print(f"  {th:10.3f} {k:30d} {k/nev:10.1f}")
    print()
    q = [1, 5, 25, 50, 75, 95, 99, 99.9]
    print("  best-logit percentiles: " +
          "  ".join(f"p{p}={np.percentile(valid, p):.2f}" for p in q))


if __name__ == "__main__":
    main(sys.argv[1])
