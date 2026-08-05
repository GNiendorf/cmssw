#!/usr/bin/env python3
"""Bit-identity of every branch over the FIRST N entries of two files.
Usage: cmp_prefix.py <ref.root> <new.root> [N]"""
import sys
import uproot, awkward as ak, numpy as np
ref, new = sys.argv[1], sys.argv[2]
tr = uproot.open(ref)["tree"]; tn = uproot.open(new)["tree"]
N = int(sys.argv[3]) if len(sys.argv) > 3 else min(tr.num_entries, tn.num_entries)
print("comparing first %d entries (ref %d, new %d)" % (N, tr.num_entries, tn.num_entries))
def blob(a):
    if isinstance(a, np.ndarray):
        return a.tobytes()+b"|"+repr(a.dtype).encode()
    f,l,c = ak.to_buffers(ak.to_packed(a))
    p=[str(f).encode(), str(l).encode()]
    for k in sorted(c): p.append(k.encode()); p.append(np.asarray(c[k]).tobytes())
    return b"|".join(p)
same=diff=miss=0; bad=[]
for k in sorted(tr.keys()):
    if k not in tn.keys(): miss+=1; bad.append("MISSING "+k); continue
    a=tr[k].array(entry_stop=N); b=tn[k].array(entry_stop=N)
    if blob(a)==blob(b): same+=1
    else: diff+=1; bad.append("DIFFER "+k)
print("%d IDENTICAL, %d DIFFER, %d MISSING" % (same,diff,miss))
for x in bad: print("  ",x)
