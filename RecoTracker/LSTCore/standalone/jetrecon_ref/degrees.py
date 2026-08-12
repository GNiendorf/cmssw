#!/usr/bin/env python3
"""Per-key degree statistics of the chain edge graph, from an LST_CHAIN_NODE_DUMP file.

Record layout (LSTEvent::dumpChainNodes):
  uint32 magic 'P25N' (0x5032354E), uint32 ievt, uint32 nNodes,
  then per node: uint32 stableId, then 3 x (uint32 mdAnchorHit, uint32 mdOuterHit)
  for the inner / middle / outer MiniDoublet of the triplet.

E1 (MD family) joins triplet A -> B when A.md2 == B.md0, so for MD key k
  degIn(k)  = #{A : A.md2 == k},  degOut(k) = #{B : B.md0 == k},  E1 = sum_k degIn*degOut.
E2 (LS family) joins A -> B when A.outerLS == B.innerLS, with
  innerLS = (md0, md1), outerLS = (md1, md2).
"""
import sys
import numpy as np

MAGIC = 0x5032354E


def events(path, maxev=None):
    buf = np.memmap(path, dtype=np.uint32, mode="r")
    off = 0
    n = 0
    while off + 3 <= len(buf):
        if buf[off] != MAGIC:
            raise SystemExit("bad magic at word %d" % off)
        ievt = int(buf[off + 1])
        nNodes = int(buf[off + 2])
        off += 3
        rows = buf[off:off + 7 * nNodes].reshape(nNodes, 7)
        off += 7 * nNodes
        yield ievt, rows
        n += 1
        if maxev is not None and n >= maxev:
            return


def keys_of(rows):
    """Return (md0, md1, md2, innerLS, outerLS) as dense integer label arrays."""
    # MD identity = the (anchorHit, outerHit) pair -> pack into one int64.
    md = [(rows[:, 1 + 2 * k].astype(np.int64) << np.int64(32)) | rows[:, 2 + 2 * k].astype(np.int64)
          for k in range(3)]
    allmd = np.concatenate(md)
    uniq, inv = np.unique(allmd, return_inverse=True)
    n = len(rows)
    m0, m1, m2 = inv[:n], inv[n:2 * n], inv[2 * n:]
    nmd = len(uniq)
    innerLS = m0.astype(np.int64) * nmd + m1
    outerLS = m1.astype(np.int64) * nmd + m2
    return m0, m1, m2, innerLS, outerLS, nmd


def degrees(inKeys, outKeys):
    """Bincount degIn / degOut on a shared dense labelling of the two key arrays."""
    both = np.concatenate([inKeys, outKeys])
    uniq, inv = np.unique(both, return_inverse=True)
    n = len(inKeys)
    di = np.bincount(inv[:n], minlength=len(uniq))
    do = np.bincount(inv[n:], minlength=len(uniq))
    return di.astype(np.int64), do.astype(np.int64)


def cap_kept(di, do, c):
    return int((np.minimum(di, c) * np.minimum(do, c)).sum())


def analyse(path, maxev=None, label=""):
    caps = [8, 16, 32, 64, 128, 256]
    print("# %s  (%s)" % (label, path))
    hdr = ("%5s %8s %9s %9s %7s %7s %7s %7s " % ("evt", "nT3", "E1", "E2", "mxDgI", "mxDgO", "top10%", "top100%"))
    hdr += " ".join("%7s" % ("rm@%d" % c) for c in caps)
    print(hdr)
    tot = {c: [0, 0] for c in caps}
    rows_out = []
    for ievt, rows in events(path, maxev):
        m0, m1, m2, iLS, oLS, nmd = keys_of(rows)
        di, do = degrees(m2, m0)           # E1: in = A.md2, out = B.md0
        prod = di * do
        e1 = int(prod.sum())
        di2, do2 = degrees(oLS, iLS)       # E2
        e2 = int((di2 * do2).sum())
        order = np.argsort(prod)[::-1]
        top10 = float(prod[order[:10]].sum()) / e1 if e1 else 0.0
        top100 = float(prod[order[:100]].sum()) / e1 if e1 else 0.0
        line = "%5d %8d %9d %9d %7d %7d %7.3f %7.3f " % (
            ievt, len(rows), e1, e2, di.max(), do.max(), top10, top100)
        cells = []
        for c in caps:
            kept = cap_kept(di, do, c)
            tot[c][0] += kept
            tot[c][1] += e1
            cells.append("%7.4f" % (1.0 - kept / e1 if e1 else 0.0))
        print(line + " ".join(cells))
        rows_out.append((ievt, len(rows), e1, e2, int(di.max()), int(do.max())))
    print("# pooled fraction of E1 removed by a per-MD degree cap:")
    for c in caps:
        kept, e = tot[c]
        print("#   cap=%-4d removes %.5f of E1" % (c, 1.0 - kept / e if e else 0.0))
    return rows_out


if __name__ == "__main__":
    path = sys.argv[1]
    maxev = int(sys.argv[2]) if len(sys.argv) > 2 else None
    analyse(path, maxev, label=sys.argv[3] if len(sys.argv) > 3 else "")
