#!/usr/bin/env python3
"""Two cap families compared on the same E1 degree data.

A) per-MD-KEY cap C   : keep at most C in-triplets and C out-triplets at each shared MD.
                        kept(k) = min(dI,C) * min(dO,C)             -- quadratic in C
B) per-NODE top-C cap : every triplet keeps its C best out-edges and its C best in-edges;
                        an edge survives if either endpoint kept it.
                        kept(k) = dI*min(dO,C) + min(dI,C)*dO - min(dI,C)*min(dO,C)
                        (exact if the two top-C sets are nested; the first two terms alone
                         are a strict upper bound)     -- LINEAR in C
B is the one the weld can actually afford to lose nothing to: K6a/K6b is a mutual-best
matching run kChainWeldSweeps = 3 times, so each node's weld can only ever come from its
top few eligible edges per direction.
"""
import sys
import numpy as np
from degrees import events, degrees

CAPS = [2, 4, 8, 16, 32, 64, 128, 256, 512]

def keys(rows):
    md = [(rows[:, 1 + 2*k].astype(np.int64) << np.int64(32)) | rows[:, 2 + 2*k].astype(np.int64) for k in range(3)]
    allmd = np.concatenate(md); uniq, inv = np.unique(allmd, return_inverse=True); n = len(rows)
    return inv[:n], inv[n:2*n], inv[2*n:]

def run(path, label, maxev):
    tot_e1 = 0; keyk = {c: 0 for c in CAPS}; nodek = {c: 0 for c in CAPS}
    worst_key = {c: 0.0 for c in CAPS}; worst_node = {c: 0.0 for c in CAPS}
    per = []
    for ievt, rows in events(path, maxev):
        m0, m1, m2 = keys(rows)
        dI, dO = degrees(m2, m0)
        e1 = int((dI*dO).sum()); tot_e1 += e1
        row = [ievt, len(rows), e1]
        for c in CAPS:
            mi, mo = np.minimum(dI, c), np.minimum(dO, c)
            kk = int((mi*mo).sum())
            nk = int((dI*mo + mi*dO - mi*mo).sum())
            keyk[c] += kk; nodek[c] += nk
            worst_key[c] = max(worst_key[c], 1 - kk/e1)
            worst_node[c] = max(worst_node[c], 1 - nk/e1)
            row += [kk, nk]
        per.append(row)
    print("# %s  (%d events, pooled E1 = %d)" % (label, len(per), tot_e1))
    print("#  cap   A:per-MD-key removed   B:per-node top-C removed   B kept (Medges pooled)  B worst-event removed")
    for c in CAPS:
        print("#  %-5d %18.6f %24.6f %20.2f %20.6f" % (
            c, 1-keyk[c]/tot_e1, 1-nodek[c]/tot_e1, nodek[c]/1e6, worst_node[c]))
    return per

if __name__ == "__main__":
    maxev = int(sys.argv[1])
    jp = run(sys.argv[2], sys.argv[3], maxev)
    pp = run(sys.argv[4], sys.argv[5], maxev)
    print()
    print("# per-event B (per-node top-C) kept edge counts, %s:" % sys.argv[3])
    print("%6s %8s %11s " % ("evt", "nT3", "E1") + " ".join("%9s" % ("C=%d" % c) for c in CAPS))
    for r in jp:
        print("%6d %8d %11d " % (r[0], r[1], r[2]) + " ".join("%9d" % r[4+2*i] for i in range(len(CAPS))))
