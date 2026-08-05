#!/usr/bin/env python3
"""What the tie-break swap actually changed, at the chain level.

usage: p25_tiediff.py <old_tiebreak.bin> <new_tiebreak.bin>

Both are LST_CHAIN_CHAIN_DUMP sidecars from the SAME binary family on the SAME events, one built
with the edge-index tie-break and one with the stable tie-break. Chains are keyed on their
post-trim MD hit-pair set. For the chains that exist on one side only, the report gives the score
distribution and the layer counts, which is how "an equal-score alternative" is checked: a tie flip
re-routes a weld between two edges of IDENTICAL logit, so the alternative chain should carry the
same length/score population, not a worse one.
"""
import sys
from collections import Counter

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from p25_repro import read_chain  # noqa: E402


def main():
    a, b = read_chain(sys.argv[1]), read_chain(sys.argv[2])
    n = min(len(a), len(b))
    import struct
    print(f"{'evt':>4} {'old':>7} {'new':>7} {'common':>7} {'onlyOld':>8} {'onlyNew':>8} {'same%':>7}")
    so, sn, lo_, ln = [], [], Counter(), Counter()
    tA = tB = tC = 0
    for i in range(n):
        ca = {r[0]: r for r in a[i][3]}
        cb = {r[0]: r for r in b[i][3]}
        ka, kb = set(ca), set(cb)
        common = len(ka & kb)
        print(f"{i:>4} {len(ka):>7} {len(kb):>7} {common:>7} {len(ka-kb):>8} {len(kb-ka):>8} "
              f"{100.0*common/len(ka):>6.2f}%")
        for k in ka - kb:
            so.append(struct.unpack("<f", ca[k][5])[0])
            lo_[ca[k][1]] += 1
        for k in kb - ka:
            sn.append(struct.unpack("<f", cb[k][5])[0])
            ln[cb[k][1]] += 1
        tA += len(ka)
        tB += len(kb)
        tC += common
    so, sn = np.array(so), np.array(sn)
    print()
    print(f"TOTAL old={tA} new={tB} common={tC}  ({100.0*tC/tA:.4f}% of the old set survives)")
    print(f"one-sided: old-only={len(so)}  new-only={len(sn)}   (a balanced count means a re-route,")
    print("           not a gain or a loss of chains)")
    if len(so) and len(sn):
        print(f"  chain score  old-only  mean={so.mean():.4f} median={np.median(so):.4f} "
              f"min={so.min():.4f} max={so.max():.4f}")
        print(f"  chain score  new-only  mean={sn.mean():.4f} median={np.median(sn):.4f} "
              f"min={sn.min():.4f} max={sn.max():.4f}")
        print(f"  score population shift (new - old, means): {sn.mean()-so.mean():+.5f}")
    print(f"  nLayers old-only: {dict(sorted(lo_.items()))}")
    print(f"  nLayers new-only: {dict(sorted(ln.items()))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
