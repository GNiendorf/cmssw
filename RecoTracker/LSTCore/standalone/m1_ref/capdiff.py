#!/usr/bin/env python3
"""How BIG is the C=256 output change on PU200, event by event?

`cmp_branches.py` already says the tc_* columns DIFFER between the cap-off and cap-256 arms of the
same binary. "Differ" is a bit, and a bit is not a physics answer: the shipping decision needs to
know whether 18 branches differing means one track moved on two events or a percent of the
collection changed everywhere. So this counts, per event, the track candidates present in one arm
and not the other, matched on a CONTENT key rather than on collection index -- a single insertion
shifts every later index, so an index-wise diff would report a whole event as changed when one
track was added.

Key = the sorted tuple of (hitIdx, hitType) pairs plus the type byte, which is the identity of a
track candidate independent of where it sits in the collection.

Usage: capdiff.py <off.root> <cap.root>   (offline, on files that already exist)
"""
import sys

import awkward as ak
import numpy as np
import uproot

a = uproot.open(sys.argv[1])["tree"]
b = uproot.open(sys.argv[2])["tree"]
COLS = ["tc_hitIdx", "tc_hitType", "tc_type", "tc_pt", "tc_eta", "tc_isFake", "tc_isDuplicate"]
A = a.arrays(COLS)
B = b.arrays(COLS)
n = min(a.num_entries, b.num_entries)


def keys(ev, i):
    out = []
    for h, t, ty in zip(ev.tc_hitIdx[i], ev.tc_hitType[i], ev.tc_type[i]):
        out.append((int(ty), tuple(sorted(zip([int(x) for x in h], [int(x) for x in t])))))
    return out


nEvtChanged = 0
totA = totB = totOnlyA = totOnlyB = 0
fakeA = fakeB = dupA = dupB = 0
worst = []
for i in range(n):
    ka, kb = keys(A, i), keys(B, i)
    sa, sb = set(ka), set(kb)
    onlyA, onlyB = sa - sb, sb - sa
    totA += len(ka)
    totB += len(kb)
    totOnlyA += len(onlyA)
    totOnlyB += len(onlyB)
    fakeA += int(ak.sum(A.tc_isFake[i]))
    fakeB += int(ak.sum(B.tc_isFake[i]))
    dupA += int(ak.sum(A.tc_isDuplicate[i]))
    dupB += int(ak.sum(B.tc_isDuplicate[i]))
    if onlyA or onlyB:
        nEvtChanged += 1
        worst.append((len(onlyA) + len(onlyB), i, len(ka), len(kb), len(onlyA), len(onlyB)))

print(f"events compared: {n}")
print(f"events whose TC SET changed at all: {nEvtChanged} / {n} ({100*nEvtChanged/n:.1f}%)")
print(f"TCs: arm A {totA}  arm B {totB}  (net {totB-totA:+d}, {100*(totB-totA)/totA:+.4f}%)")
print(f"TCs only in A (LOST by the cap): {totOnlyA}  = {100*totOnlyA/totA:.4f}% of A")
print(f"TCs only in B (GAINED by the cap): {totOnlyB}  = {100*totOnlyB/totB:.4f}% of B")
print(f"fakes: {fakeA} -> {fakeB} ({100*fakeA/totA:.4f}% -> {100*fakeB/totB:.4f}% of TCs)")
print(f"dups : {dupA} -> {dupB} ({100*dupA/totA:.4f}% -> {100*dupB/totB:.4f}% of TCs)")
print("\nthe 10 events with the largest TC-set change (evt, nA, nB, onlyA, onlyB):")
for w in sorted(worst, reverse=True)[:10]:
    print(f"  evt {w[1]:4d}  nA {w[2]:5d}  nB {w[3]:5d}  onlyA {w[4]:4d}  onlyB {w[5]:4d}")
