#!/usr/bin/env python3
"""VERIFIER-3 independent distinct-displaced-sim counter.

Written from the createPerfNumDenHists source (efficiency/src/performance.cc:1208-1221:
the vxy/dxy denominator is pt > pt_cut AND |eta| < eta_cut AND |vtx_z| < 30, with NO
vtx_perp cut -- the vtx_perp < 2.5 cut applies only to the eta/pt/dz denominators).
Deliberately NOT a copy of a08_distinct.py: it slices vxy and dxy independently as well
as jointly, so the "double count" correction can be checked rather than assumed.

Usage: v3_distinct.py --input <LSTntuple.root> --lst-from-input <A.root> [<B.root> ...]
"""
import argparse
import sys

import ROOT

TYPE_NAME = {4: "T5", 9: "T4", 7: "pT5", 5: "pT3", 8: "pLS", 3: "T3"}


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
ap.add_argument("--pt", type=float, default=0.9)
ap.add_argument("--eta", type=float, default=4.5)
ap.add_argument("files", nargs="+")
args = ap.parse_args()
ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError

fi = ROOT.TFile.Open(args.input)
ti = fi.Get("tree")
ei = evt_index(ti)

handles = []
for p in args.files:
    f = ROOT.TFile.Open(p)
    t = f.Get("tree")
    handles.append((p.split('/')[-1].replace('.root', ''), f, t, evt_index(t)))

common = sorted(e for e in ei if all(e in h[3] for h in handles))
print("events in common: %d" % len(common))

# slices: (label, predicate on (vxy, dxy))
SLICES = [
    ("maxd>=1  (DISP1)", lambda v, d: max(v, abs(d)) >= 1.0),
    ("maxd>=5  (DISP5)", lambda v, d: max(v, abs(d)) >= 5.0),
    ("maxd>=10 (DISP10)", lambda v, d: max(v, abs(d)) >= 10.0),
    ("maxd>=30 (DISP30)", lambda v, d: max(v, abs(d)) >= 30.0),
    ("vxy [30,60)", lambda v, d: 30.0 <= v < 60.0),
    ("vxy >=30", lambda v, d: v >= 30.0),
    ("vxy [10,30)", lambda v, d: 10.0 <= v < 30.0),
    ("dxy [10,30)", lambda v, d: 10.0 <= abs(d) < 30.0),
    ("dxy [5,10)", lambda v, d: 5.0 <= abs(d) < 10.0),
    ("PROMPT vxy<1 & |dxy|<1", lambda v, d: v < 1.0 and abs(d) < 1.0),
]

nsl = len(SLICES)
den = [0] * nsl
lstn = [0] * nsl
pn = [[0] * nsl for _ in handles]
lost = [[0] * nsl for _ in handles]   # LST has it, proto does not
gain = [[0] * nsl for _ in handles]
skipped = 0

for evt in common:
    ti.GetEntry(ei[evt])
    sl = list(ti.sim_tcIdx)
    pt, eta = list(ti.sim_pt), list(ti.sim_eta)
    dxy, vxy = list(ti.sim_pca_dxy), list(ti.sim_vtxperp)
    vz, chg = list(ti.sim_vz), list(ti.sim_q)
    rows = []
    ok = True
    for name, f, t, idx in handles:
        t.GetEntry(idx[evt])
        sp = list(t.sim_tcIdx)
        if len(sp) != len(sl):
            ok = False
            break
        rows.append(sp)
    if not ok:
        skipped += 1
        continue
    for s in range(len(sl)):
        if not (pt[s] > args.pt and abs(eta[s]) < args.eta and abs(vz[s]) < 30.0 and chg[s] != 0):
            continue
        v, d = vxy[s], dxy[s]
        ml = sl[s] >= 0
        for j, sp in enumerate(rows):
            pass
        for k, (lab, pred) in enumerate(SLICES):
            if not pred(v, d):
                continue
            den[k] += 1
            lstn[k] += ml
            for j, sp in enumerate(rows):
                mp = sp[s] >= 0
                pn[j][k] += mp
                if ml and not mp:
                    lost[j][k] += 1
                elif mp and not ml:
                    gain[j][k] += 1

if skipped:
    print("WARNING: %d events skipped (sim-list length mismatch)" % skipped)
hdr = "%-26s%9s%9s" % ("slice", "nSim", "LST")
for name, _, _, _ in handles:
    hdr += "%14s%8s%8s%8s" % (name[:13], "d(LST)", "lost", "gain")
print(hdr)
print("-" * len(hdr))
for k, (lab, _) in enumerate(SLICES):
    line = "%-26s%9d%9d" % (lab, den[k], lstn[k])
    for j in range(len(handles)):
        line += "%14d%+8d%8d%8d" % (pn[j][k], pn[j][k] - lstn[k], lost[j][k], gain[j][k])
    print(line)
