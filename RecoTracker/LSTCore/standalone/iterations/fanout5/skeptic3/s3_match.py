#!/usr/bin/env python3
"""SKEPTIC-3: per-TC harness-matching audit for the chain-extension exploit.

The extension pass changes NO TC (same count, same order); it only appends hits.
So the two output trees can be aligned index-by-index and every efficiency / fake
move attributed to a matching-FRACTION change on a specific TC.

Reports, split by whether the TC was extended (tc_nhitOT increased):
  matched -> fake flips, fake -> matched flips, and the sim-match multiplicity change.

Usage: s3_match.py <ref.root> <test.root> [nevents]
"""
import sys

import ROOT

ROOT.gROOT.SetBatch(True)

ref, test = sys.argv[1], sys.argv[2]
NEV = int(sys.argv[3]) if len(sys.argv) > 3 else -1

fa, fb = ROOT.TFile.Open(ref), ROOT.TFile.Open(test)
ta, tb = fa.Get("tree"), fb.Get("tree")
na, nb = ta.GetEntries(), tb.GetEntries()
print("entries ref=%d test=%d" % (na, nb))
n = min(na, nb) if NEV < 0 else min(NEV, na, nb)

misalign = 0
tot_a = tot_b = 0
ext = 0
ext_hits = 0
flip_m2f = flip_f2m = 0
flip_m2f_ext = flip_f2m_ext = 0
flip_noext = 0
dup_a = dup_b = 0
# how many sim indices each TC claims, before/after, for extended TCs
lost_sim_ext = gain_sim_ext = 0

for i in range(n):
    ta.GetEntry(i)
    tb.GetEntry(i)
    pa, pb = list(ta.tc_pt), list(tb.tc_pt)
    if len(pa) != len(pb):
        misalign += 1
        continue
    ea, eb = list(ta.tc_eta), list(tb.tc_eta)
    ha, hb = list(ta.tc_nhitOT), list(tb.tc_nhitOT)
    ka, kb = list(ta.tc_isFake), list(tb.tc_isFake)
    da, db = list(ta.tc_isDuplicate), list(tb.tc_isDuplicate)
    sa, sb = ta.tc_simIdxAll, tb.tc_simIdxAll
    tot_a += len(pa)
    tot_b += len(pb)
    bad = 0
    for j in range(len(pa)):
        if abs(pa[j] - pb[j]) > 1e-5 or abs(ea[j] - eb[j]) > 1e-5:
            bad += 1
            continue
        dh = hb[j] - ha[j]
        if dh > 0:
            ext += 1
            ext_hits += dh
        if ka[j] == 0 and kb[j] == 1:
            flip_m2f += 1
            if dh > 0:
                flip_m2f_ext += 1
            else:
                flip_noext += 1
        if ka[j] == 1 and kb[j] == 0:
            flip_f2m += 1
            if dh > 0:
                flip_f2m_ext += 1
            else:
                flip_noext += 1
        if dh > 0:
            la, lb = len(sa[j]), len(sb[j])
            if lb < la:
                lost_sim_ext += (la - lb)
            elif lb > la:
                gain_sim_ext += (lb - la)
        dup_a += da[j]
        dup_b += db[j]
    misalign += bad

print("events compared      : %d" % n)
print("TCs ref / test       : %d / %d" % (tot_a, tot_b))
print("index misalignments  : %d  (pt/eta mismatch at same index)" % misalign)
print("TCs EXTENDED         : %d   (+%d OT hits total, %.2f per extension)"
      % (ext, ext_hits, ext_hits / max(1, ext)))
print("matched -> FAKE      : %d   (of which extended: %d)" % (flip_m2f, flip_m2f_ext))
print("fake -> matched      : %d   (of which extended: %d)" % (flip_f2m, flip_f2m_ext))
print("flips on NON-extended: %d" % flip_noext)
print("sim links LOST  on extended TCs: %d" % lost_sim_ext)
print("sim links GAINED on extended TCs: %d" % gain_sim_ext)
print("isDuplicate ref/test : %d / %d" % (dup_a, dup_b))
if ext:
    print("=> break rate: %.3f%% of extensions destroyed a sim match"
          % (100.0 * flip_m2f_ext / ext))
