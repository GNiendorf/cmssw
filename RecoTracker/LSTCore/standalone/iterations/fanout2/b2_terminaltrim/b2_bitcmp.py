#!/usr/bin/env python3
"""Bit-exact branch comparison of two prototype output ntuples (angle B2 regression gate)."""
import sys
import math
import ROOT


def same(x, y):
    if isinstance(x, float) and isinstance(y, float):
        if math.isnan(x) and math.isnan(y):
            return True
    return x == y

a, b = sys.argv[1], sys.argv[2]
fa, fb = ROOT.TFile.Open(a), ROOT.TFile.Open(b)
ta, tb = fa.Get("tree"), fb.Get("tree")
if ta.GetEntries() != tb.GetEntries():
    print("FAIL entries %d vs %d" % (ta.GetEntries(), tb.GetEntries()))
    sys.exit(1)
names = sorted([br.GetName() for br in ta.GetListOfBranches()])
nb = sorted([br.GetName() for br in tb.GetListOfBranches()])
if names != nb:
    print("FAIL branch list differs")
    sys.exit(1)
bad = []
for i in range(ta.GetEntries()):
    ta.GetEntry(i)
    tb.GetEntry(i)
    for n in names:
        va, vb = getattr(ta, n), getattr(tb, n)
        try:
            la, lb = list(va), list(vb)
            if len(la) != len(lb):
                bad.append((n, i, "len"))
                continue
            for x, y in zip(la, lb):
                try:
                    la2, lb2 = list(x), list(y)
                    if len(la2) != len(lb2) or not all(same(p, q) for p, q in zip(la2, lb2)):
                        bad.append((n, i, "nested"))
                        break
                except TypeError:
                    if not same(x, y):
                        bad.append((n, i, "val"))
                        break
        except TypeError:
            if not same(va, vb):
                bad.append((n, i, "scalar"))
    if bad:
        break
print("branches=%d entries=%d" % (len(names), ta.GetEntries()))
if bad:
    print("FAIL", bad[:10])
    sys.exit(1)
print("BIT-EXACT: all %d branches identical over %d entries" % (len(names), ta.GetEntries()))
