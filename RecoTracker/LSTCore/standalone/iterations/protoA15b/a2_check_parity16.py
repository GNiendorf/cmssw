#!/usr/bin/env python3
"""a2 regression check: the 25-slot ChainFeatures contract must leave slots 0-15
BIT-IDENTICAL to the frozen M6 16-slot contract.

Compares the first N chain rows of a NEW dump (25 features) against the reference
prototype dump (16 features), row by row, on cf_00..cf_15.
"""
import sys

import numpy as np
import uproot

NEW = sys.argv[1]
REF = sys.argv[2] if len(sys.argv) > 2 else (
    "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/"
    "prototype/chains_300evt.root")

fn = uproot.open(NEW)
fr = uproot.open(REF)
print("new spec:", fn["feature_spec"].member("fTitle"))
print("ref spec:", fr["feature_spec"].member("fTitle"))

br = [f"cf_{i:02d}" for i in range(16)] + ["label", "nLayers", "evt"]
tn = fn["chains"].arrays(br, library="np")
n = len(tn["label"])
tr = fr["chains"].arrays(br, entry_stop=n, library="np")

ok = True
for b in br:
    a, c = tn[b], tr[b]
    if a.dtype.kind == "f":
        bad = int((a != c).sum())
        mx = float(np.abs(a - c).max()) if len(a) else 0.0
    else:
        bad = int((a != c).sum())
        mx = 0.0
    flag = "OK " if bad == 0 else "DIFF"
    if bad:
        ok = False
    print(f"  {flag} {b:8s} ndiff={bad:8d} maxabsdiff={mx:.3e}")
print(f"compared {n} rows -> {'IDENTICAL' if ok else 'MISMATCH'}")

# New-feature summary
newb = [f"cf_{i:02d}" for i in range(16, 25)]
names = fn["feature_spec"].member("fTitle")[3:].split(",")
tnew = fn["chains"].arrays(newb + ["label", "dcaXY"], library="np")
lab = tnew["label"] == 1
print("\n new-feature stats (name / min / max / mean_true / mean_fake):")
for i, b in enumerate(newb):
    v = tnew[b]
    print(f"  {names[16+i]:>22s} {v.min():12.5g} {v.max():12.5g} "
          f"{v[lab].mean():12.5g} {v[~lab].mean():12.5g}")
d = tnew["dcaXY"]
print(f"  {'dcaXY(meta)':>22s} {d.min():12.5g} {d.max():12.5g} "
      f"{d[lab].mean():12.5g} {d[~lab].mean():12.5g}  frac>=0.5={float((d>=0.5).mean()):.4f}")
