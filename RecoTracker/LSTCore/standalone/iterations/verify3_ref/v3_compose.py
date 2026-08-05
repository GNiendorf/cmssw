#!/usr/bin/env python3
"""VERIFIER-3: TC composition (tc_type x nhitOT) and displaced duplicate rate,
straight off the output trees. Checks the mechanism claims that the histogram
scoreboard cannot see:
  A13 "every removed row is a zero-OT-hit bare type-8 row"
  A08 "displaced dup rate is 3-25x better than LST from vxy 5 cm outward"
  A03 "delivered pT3-class rows fall 135.4 -> ~128/evt"
Usage: v3_compose.py --input <LSTntuple.root> <ref.root> <cmp.root> ...
"""
import argparse
from collections import Counter

import ROOT

TYPE_NAME = {3: "T3", 4: "T5", 5: "pT3", 7: "pT5", 8: "pLS", 9: "T4"}


def evt_index(tree):
    tree.SetBranchStatus("*", 0)
    tree.SetBranchStatus("evt", 1)
    m = {}
    for i in range(tree.GetEntries()):
        tree.GetEntry(i)
        m[int(tree.evt)] = i
    tree.SetBranchStatus("*", 1)
    return m


ap = argparse.ArgumentParser()
ap.add_argument("--input", required=True)
ap.add_argument("files", nargs="+")
args = ap.parse_args()
ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError

fi = ROOT.TFile.Open(args.input)
ti = fi.Get("tree")
ei = evt_index(ti)

results = []
for p in args.files:
    f = ROOT.TFile.Open(p)
    t = f.Get("tree")
    e = evt_index(t)
    common = sorted(x for x in ei if x in e)
    comp = Counter()          # (type, nhitOT) -> count
    dupband = {}              # band -> [nMatchedSims, nSimsWithMoreThanOneTC]
    nev = 0
    for evt in common:
        t.GetEntry(e[evt])
        ti.GetEntry(ei[evt])
        nev += 1
        ty = list(t.tc_type)
        nh = list(t.tc_nhitOT)
        for a, b in zip(ty, nh):
            comp[(int(a), int(b))] += 1
        # displaced duplicate rate: per sim, how many TCs claim it
        sim_tc = list(t.sim_tcIdx)
        vxy = list(ti.sim_vtxperp)
        pt, eta = list(ti.sim_pt), list(ti.sim_eta)
        vz, chg = list(ti.sim_vz), list(ti.sim_q)
        cnt = Counter()
        try:
            allmatch = t.tc_simIdxAll
            for itc in range(len(ty)):
                for s in allmatch[itc]:
                    cnt[int(s)] += 1
        except Exception:
            cnt = None
        if cnt is not None and len(sim_tc) == len(vxy):
            for s in range(len(sim_tc)):
                if not (pt[s] > 0.9 and abs(eta[s]) < 4.5 and abs(vz[s]) < 30.0 and chg[s] != 0):
                    continue
                if sim_tc[s] < 0:
                    continue
                v = vxy[s]
                band = ("vxy<1" if v < 1 else "vxy1-5" if v < 5 else
                        "vxy5-10" if v < 10 else "vxy10-30" if v < 30 else "vxy>=30")
                d = dupband.setdefault(band, [0, 0])
                d[0] += 1
                if cnt.get(s, 0) > 1:
                    d[1] += 1
    f.Close()
    results.append((p.split('/')[-1].replace('.root', ''), nev, comp, dupband))

# composition table
allkeys = sorted({k for _, _, c, _ in results for k in c})
print("TC COMPOSITION  (rows per event)")
hdr = "%-14s" % "type/nhitOT"
for n, nev, _, _ in results:
    hdr += "%16s" % n[:15]
print(hdr)
ref = results[0][2]
refn = results[0][1]
for k in allkeys:
    line = "%-14s" % ("%s/%d" % (TYPE_NAME.get(k[0], k[0]), k[1]))
    for n, nev, c, _ in results:
        line += "%16.2f" % (c[k] / float(nev))
    print(line)
print("%-14s" % "TOTAL" + "".join("%16.2f" % (sum(c.values()) / float(nev))
                                  for _, nev, c, _ in results))
print()
print("DISPLACED DUPLICATE RATE (fraction of MATCHED sims claimed by >1 TC)")
bands = ["vxy<1", "vxy1-5", "vxy5-10", "vxy10-30", "vxy>=30"]
hdr = "%-12s" % "band"
for n, _, _, _ in results:
    hdr += "%22s" % n[:21]
print(hdr)
for b in bands:
    line = "%-12s" % b
    for _, _, _, db in results:
        if b in db and db[b][0]:
            line += "%14.4f (%6d)" % (db[b][1] / float(db[b][0]), db[b][0])
        else:
            line += "%22s" % "-"
    print(line)
