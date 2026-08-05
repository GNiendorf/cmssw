#!/usr/bin/env python3
"""Bit-exact branch-by-branch comparison of two prototype output ntuples (uproot)."""
import sys
import uproot

a, b = sys.argv[1], sys.argv[2]
fa, fb = uproot.open(a), uproot.open(b)
tn = [k.split(";")[0] for k, v in fa.items(recursive=False) if hasattr(v, "keys")]
tn = sorted(set(tn))
name = tn[0]
ta, tb = fa[name], fb[name]
print("tree=%s entries=%d vs %d" % (name, ta.num_entries, tb.num_entries))
if ta.num_entries != tb.num_entries:
    print("FAIL: entry count differs")
    sys.exit(1)
ka, kb = sorted(ta.keys()), sorted(tb.keys())
if ka != kb:
    print("FAIL: branch list differs:", set(ka) ^ set(kb))
    sys.exit(1)
ok, bad = 0, []
for br in ka:
    xa = ta[br].array(library="np")
    xb = tb[br].array(library="np")
    same = True
    if len(xa) != len(xb):
        same = False
    else:
        for i in range(len(xa)):
            u, v = xa[i], xb[i]
            try:
                lu, lv = list(u), list(v)
                lu = [list(z) if hasattr(z, "__len__") else z for z in lu]
                lv = [list(z) if hasattr(z, "__len__") else z for z in lv]
            except TypeError:
                lu, lv = [u], [v]
            if lu != lv:
                # NaN != NaN: fall back to a raw-bit comparison (NaN payloads must match).
                import numpy as np
                fu, fv = np.asarray(lu, dtype=object), np.asarray(lv, dtype=object)
                try:
                    if np.asarray(lu).tobytes() == np.asarray(lv).tobytes():
                        continue
                except Exception:
                    pass
                same = False
                break
    if same:
        ok += 1
    else:
        bad.append(br)
print("BRANCHES IDENTICAL: %d/%d" % (ok, len(ka)))
if bad:
    print("DIFFER: %s" % bad)
    sys.exit(1)
print("PASS bit-exact")
