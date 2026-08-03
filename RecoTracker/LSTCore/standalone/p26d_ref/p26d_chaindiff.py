#!/usr/bin/env python3
"""P2.6d: field-level localisation of a CHAIN(full) mismatch.

p25_repro.py reports CHAIN(full) as a single number over the key
(mds, nLayers, branch, trim, flags, score). This tool matches the two sidecars on the stable MD key
and reports WHICH of the remaining fields disagree, and how.

usage: p26d_chaindiff.py A.bin B.bin
"""
import struct
import sys
from collections import Counter, defaultdict

CH_MAGIC = 0x50323243
FIX = struct.Struct("<IIIIiII8f25f")
HDR = struct.Struct("<IIIII")
NAMES = ["nLayers", "branch", "trim", "flags", "score"]


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
            rows.append((mds, nLay, branch, trim, flags, struct.pack("<f", score), score, preN, postN))
        events.append((ievt, nT3, nEdge, rows))
    return events


def main():
    a, b = read_chain(sys.argv[1]), read_chain(sys.argv[2])
    diff_counts = Counter()
    total = 0
    matched = 0
    examples = defaultdict(list)
    valpairs = defaultdict(Counter)
    for ea, eb in zip(a, b):
        da = defaultdict(list)
        for r in ea[3]:
            da[r[0]].append(r)
        db = defaultdict(list)
        for r in eb[3]:
            db[r[0]].append(r)
        for k, ra in da.items():
            rb = db.get(k)
            total += len(ra)
            if rb is None or len(rb) != len(ra):
                continue
            for x, y in zip(ra, rb):
                matched += 1
                for i, name in enumerate(NAMES):
                    va, vb = x[1 + i], y[1 + i]
                    if name == "score":
                        va, vb = x[6], y[6]
                    if va != vb:
                        diff_counts[name] += 1
                        valpairs[name][(va, vb)] += 1
                        if len(examples[name]) < 3:
                            examples[name].append((va, vb, x[7], y[7], x[8], y[8]))
    print(f"chains total={total} matched-on-MD-key={matched}")
    for name in NAMES:
        n = diff_counts[name]
        print(f"  {name:<9} differing: {n:>7}  ({100.0*n/matched if matched else 0:.2f}%)")
        if n:
            for (va, vb), c in valpairs[name].most_common(6):
                print(f"      A={va!r:<24} B={vb!r:<24} x{c}")
    for name in NAMES:
        if examples[name]:
            print(f"  example {name}: " + ", ".join(f"A={e[0]!r} B={e[1]!r} (preN {e[2]}/{e[3]}, postN {e[4]}/{e[5]})" for e in examples[name][:2]))


if __name__ == "__main__":
    main()
