#!/usr/bin/env python3
"""Structural safety check: does the extension ever put a hit on two output TCs?

Reads the tc_hitOT dump (PROTO_DUMP_TCHITS=1) of the SAME 30 events with the
extension off and on, and reports (a) the number of ph2 rows carried by more
than one TC in each run, and (b) how many of the ON-run multi-owner rows are
hits the extension added (i.e. rows that no TC held in the OFF run)."""
import sys
import ROOT

D = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_extend/"


def load(tag):
    f = ROOT.TFile.Open(D + tag + ".root")
    t = f.Get("tree")
    per_evt = []
    for i in range(t.GetEntries()):
        t.GetEntry(i)
        owners = {}
        for tc in t.tc_hitOT:
            for h in tc:
                owners[h] = owners.get(h, 0) + 1
        per_evt.append(owners)
    f.Close()
    return per_evt


off = load("ovlp_off")
on = load("ovlp_on")
tot_off = tot_on = new_multi = added = 0
for a, b in zip(off, on):
    tot_off += sum(1 for v in a.values() if v > 1)
    tot_on += sum(1 for v in b.values() if v > 1)
    for h, v in b.items():
        if h not in a:
            added += 1
            if v > 1:
                new_multi += 1
print("events                                        %d" % len(off))
print("ph2 rows held by >1 output TC, extension OFF  %d" % tot_off)
print("ph2 rows held by >1 output TC, extension ON   %d" % tot_on)
print("rows the extension ADDED to the TC pool       %d" % added)
print("  ...of those, held by >1 TC                  %d" % new_multi)
