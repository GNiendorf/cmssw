#!/usr/bin/env python3
"""npz_cmp.py <npz dir A> <npz dir B> [tags file]: per file (ttbar), identity of every extracted MTV count histogram (extract.py keys:
6 collections x 14 num/den + events); per collection: files where all its histograms are identical. A = stock, B = port (or stockrep)."""
import sys, os, numpy as np, collections
A, B = sys.argv[1], sys.argv[2]
tags = open(sys.argv[3]).read().split() if len(sys.argv) > 3 else sorted(f[:-4] for f in os.listdir(B) if f.endswith(".npz"))
tags = [t for t in tags if os.path.exists(f"{A}/{t}.npz") and os.path.exists(f"{B}/{t}.npz")]
same = collections.Counter(); nh = same_h = 0
for t in tags:
    a, b = np.load(f"{A}/{t}.npz"), np.load(f"{B}/{t}.npz"); ok = collections.defaultdict(lambda: True)
    for k in a.files:
        nh += 1; eq = k in b.files and np.array_equal(a[k], b[k]); same_h += eq; ok[k.split("|")[0]] &= eq
    for c, v in ok.items(): same[c] += v
print(f"{len(tags)} files, {nh} histograms, {same_h} identical; per collection, files with all histograms identical:")
for c in sorted(same): print(f"  {c:45s} {same[c]}/{len(tags)}")
