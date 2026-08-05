#!/usr/bin/env python3
"""a08_distinct.py -- DISTINCT displaced sims, no double counting.

The band tables report vxy bands and dxy bands as separate rows, but a single sim
appears in BOTH projections (a sim born at vxy 20 cm with |dxy| 3 cm is counted once in
vxy[10,30) and once in dxy[1,5)). Summing the two sides therefore OVERSTATES how many
distinct tracks a change moved. This script counts DISTINCT sims.

Selection is the createPerfNumDenHists one (performance.cc:1208): pt > 0.9, |eta| < 4.5,
|vtx_z| < 30, charge != 0. "Displaced" is then defined by an OR over the two variables:
  DISP1 : vxy >= 1  OR |dxy| >= 1
  DISP5 : vxy >= 5  OR |dxy| >= 5
  DISP10: vxy >= 10 OR |dxy| >= 10

Usage: a08_distinct.py --a protoA.root [--b protoB.root] --input LSTntuple.root
"""
import argparse
from collections import Counter

import ROOT

TYPE_NAME = {4: "T5", 9: "T4", 7: "pT5", 5: "pT3", 8: "pLS", 3: "T3"}
TIERS = [("DISP1", 1.0), ("DISP5", 5.0), ("DISP10", 10.0), ("DISP30", 30.0)]


def evt_index(tree):
    tree.SetBranchStatus("*", 0)
    tree.SetBranchStatus("evt", 1)
    m = {}
    for i in range(tree.GetEntries()):
        tree.GetEntry(i)
        m[int(tree.evt)] = i
    tree.SetBranchStatus("*", 1)
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", default=None)
    ap.add_argument("--input", required=True)
    ap.add_argument("--labela", default="A")
    ap.add_argument("--labelb", default="B")
    args = ap.parse_args()
    ROOT.gROOT.SetBatch(True)
    ROOT.gErrorIgnoreLevel = ROOT.kError

    fa = ROOT.TFile.Open(args.a)
    fi = ROOT.TFile.Open(args.input)
    ta, ti = fa.Get("tree"), fi.Get("tree")
    fb = tb = None
    if args.b:
        fb = ROOT.TFile.Open(args.b)
        tb = fb.Get("tree")
    ea, ei = evt_index(ta), evt_index(ti)
    eb = evt_index(tb) if tb else None
    common = [e for e in ea if e in ei and (eb is None or e in eb)]

    # per tier: [nsel, nlst, na, nb, lostVsLst, gainVsLst, aOverB_lost, aOverB_gain]
    stat = {t: [0] * 8 for t, _ in TIERS}
    gtype = {t: Counter() for t, _ in TIERS}   # type of A's TC on A-over-B gains
    skipped = 0

    for evt in common:
        ta.GetEntry(ea[evt])
        ti.GetEntry(ei[evt])
        if tb:
            tb.GetEntry(eb[evt])
        sa, sl = list(ta.sim_tcIdx), list(ti.sim_tcIdx)
        sb = list(tb.sim_tcIdx) if tb else None
        if len(sa) != len(sl) or (sb is not None and len(sb) != len(sl)):
            skipped += 1
            continue
        pt, eta = list(ti.sim_pt), list(ti.sim_eta)
        dxy, vxy = list(ti.sim_pca_dxy), list(ti.sim_vtxperp)
        vz, chg = list(ti.sim_vz), list(ti.sim_q)
        atype = list(ta.tc_type)

        for s in range(len(sl)):
            if not (pt[s] > 0.9 and abs(eta[s]) < 4.5 and abs(vz[s]) < 30.0 and chg[s] != 0):
                continue
            d = max(vxy[s], abs(dxy[s]))
            ml, ma = sl[s] >= 0, sa[s] >= 0
            mb = (sb[s] >= 0) if sb is not None else None
            for name, cut in TIERS:
                if d < cut:
                    continue
                st = stat[name]
                st[0] += 1
                st[1] += ml
                st[2] += ma
                if mb is not None:
                    st[3] += mb
                if ml and not ma:
                    st[4] += 1
                elif ma and not ml:
                    st[5] += 1
                if mb is not None:
                    if mb and not ma:
                        st[6] += 1
                    elif ma and not mb:
                        st[7] += 1
                        gtype[name][atype[sa[s]]] += 1

    if skipped:
        print("WARNING: %d events skipped (sim-list length mismatch)" % skipped)
    print("DISTINCT DISPLACED SIMS  (displacement = max(vxy, |dxy|); no vxy/dxy double count)")
    print("A = %s   B = %s   input = %s" % (args.labela, args.labelb, args.input.split('/')[-1]))
    print("%-8s %8s %10s %10s %10s   %s" % ("tier", "nSim", "LST", args.labela[:10],
                                            args.labelb[:10] if tb else "-", "A vs LST (lost/gained)"))
    for name, cut in TIERS:
        st = stat[name]
        if st[0] == 0:
            continue
        row = "%-8s %8d %10d %10d %10s   %d/%d net %+d" % (
            name, st[0], st[1], st[2], (st[3] if tb else "-"), st[4], st[5], st[5] - st[4])
        print(row)
    if tb:
        print("\nA OVER B (what the change from B to A moved), distinct displaced sims:")
        for name, cut in TIERS:
            st = stat[name]
            if st[0] == 0:
                continue
            tstr = "  ".join("%s=%d" % (TYPE_NAME.get(k, k), v)
                             for k, v in sorted(gtype[name].items(), key=lambda x: -x[1]))
            print("  %-8s LOST %d  GAINED %d  net %+d   [gained TC types: %s]" %
                  (name, st[6], st[7], st[7] - st[6], tstr if tstr else "none"))


if __name__ == "__main__":
    main()
