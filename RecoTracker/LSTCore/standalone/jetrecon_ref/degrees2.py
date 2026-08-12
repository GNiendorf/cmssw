#!/usr/bin/env python3
"""Cap curves + scaling inputs from an LST_CHAIN_NODE_DUMP file. See degrees.py for the format."""
import sys
import numpy as np
from degrees import events, degrees

CAPS = [8, 16, 32, 64, 128, 256, 512, 1024, 2048]


def per_event(rows):
    md = [(rows[:, 1 + 2 * k].astype(np.int64) << np.int64(32)) | rows[:, 2 + 2 * k].astype(np.int64)
          for k in range(3)]
    allmd = np.concatenate(md)
    uniq, inv = np.unique(allmd, return_inverse=True)
    n = len(rows)
    m0, m1, m2 = inv[:n], inv[n:2 * n], inv[2 * n:]
    nmd = len(uniq)
    di, do = degrees(m2, m0)
    e1 = int((di * do).sum())
    iLS = m0.astype(np.int64) * nmd + m1
    oLS = m1.astype(np.int64) * nmd + m2
    di2, do2 = degrees(oLS, iLS)
    e2 = int((di2 * do2).sum())
    return dict(nT3=n, nMD=nmd, nMDused=int(((di > 0) | (do > 0)).sum()),
                nLS=len(di2), e1=e1, e2=e2, di=di, do=do, di2=di2, do2=do2)


def main(paths_labels, maxev):
    print("csv,label,evt,nT3,nMD,nMDused,nLS,E1,E2,maxDegIn,maxDegOut," + ",".join("keep%d" % c for c in CAPS))
    pooled = {}
    for path, label in paths_labels:
        tot_e1 = 0
        tot_keep = {c: 0 for c in CAPS}
        maxdeg = 0
        for i, (ievt, rows) in enumerate(events(path, maxev)):
            r = per_event(rows)
            keeps = [int((np.minimum(r['di'], c) * np.minimum(r['do'], c)).sum()) for c in CAPS]
            print("csv,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s" % (
                label, i, r['nT3'], r['nMD'], r['nMDused'], r['nLS'], r['e1'], r['e2'],
                r['di'].max(), r['do'].max(), ",".join(str(k) for k in keeps)))
            tot_e1 += r['e1']
            for c, k in zip(CAPS, keeps):
                tot_keep[c] += k
            maxdeg = max(maxdeg, int(r['di'].max()), int(r['do'].max()))
        pooled[label] = (tot_e1, tot_keep, maxdeg)
    print()
    for label, (e1, keep, maxdeg) in pooled.items():
        print("# pooled %s: E1=%d  max per-MD degree seen=%d" % (label, e1, maxdeg))
        for c in CAPS:
            print("#   cap=%-5d keeps %12d (%.6f of E1)  removes %.6f" % (c, keep[c], keep[c] / e1, 1 - keep[c] / e1))


if __name__ == "__main__":
    args = sys.argv[1:]
    maxev = int(args[0])
    pl = [(args[i], args[i + 1]) for i in range(1, len(args), 2)]
    main(pl, maxev)
