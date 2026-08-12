#!/usr/bin/env python3
"""Bit-exact comparison of two pu_judge.py outputs (the JSON line at the end of each file)."""
import json
import sys

def load(p):
    return json.loads(open(p).read().strip().split("\n")[-1])

ref, new = load(sys.argv[1]), load(sys.argv[2])
bad = [(k, ref[k], new.get(k, "MISSING")) for k in ref if repr(ref[k]) != repr(new.get(k))]
extra = [k for k in new if k not in ref]
print("ref fields %d  new fields %d  extra-in-new %s" % (len(ref), len(new), extra))
for k, a, b in bad:
    print("  MISMATCH %-24s ref %r  new %r" % (k, a, b))
print("VERDICT: %d/%d fields BIT-IDENTICAL at full float precision" % (len(ref) - len(bad), len(ref)))
sys.exit(1 if bad or extra else 0)
