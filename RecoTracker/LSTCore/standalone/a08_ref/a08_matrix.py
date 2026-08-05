#!/usr/bin/env python3
"""a08_matrix.py -- efficiency in every (displacement band x eta region) cell, with the
numerator and denominator, for a prototype output and for LST on the same sims.

Uses the EXACT createPerfNumDenHists denominator for the vxy/dxy plots
(performance.cc:1208): |eta| < 4.5, pt > 0.9, |vtx_z| < 30, charge != 0, and NO cut on
vxy or |dxy| (both plots are "N minus dxy"). Verified against the histograms: it returns
LST 24/423 and proto 13/423 for dxy[10,30) on the frozen 300.

Usage: a08_matrix.py --a r_TAG.root --input LSTNtuple.root [--var vxy|dxy]
"""
import argparse

import ROOT

BANDS = [(0.0, 1.0), (1.0, 5.0), (5.0, 10.0), (10.0, 30.0), (30.0, 1e18)]
REGIONS = [("barrel", 0.0, 1.1), ("trans", 1.1, 1.7), ("endcap", 1.7, 4.5)]


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
    ap.add_argument("--input", required=True)
    ap.add_argument("--var", default="both")
    args = ap.parse_args()
    ROOT.gROOT.SetBatch(True)
    ROOT.gErrorIgnoreLevel = ROOT.kError

    fa, fi = ROOT.TFile.Open(args.a), ROOT.TFile.Open(args.input)
    ta, ti = fa.Get("tree"), fi.Get("tree")
    ea, ei = evt_index(ta), evt_index(ti)
    common = [e for e in ea if e in ei]

    varlist = ["vxy", "dxy"] if args.var == "both" else [args.var]
    # cell[(var, band, region)] = [proto_num, lst_num, denom]
    cell = {}
    for v in varlist:
        for bi in range(len(BANDS)):
            for r, _, _ in REGIONS + [("ALL", 0, 0)]:
                cell[(v, bi, r)] = [0, 0, 0]

    for evt in common:
        ta.GetEntry(ea[evt])
        ti.GetEntry(ei[evt])
        sa, sl = list(ta.sim_tcIdx), list(ti.sim_tcIdx)
        if len(sa) != len(sl):
            continue
        pt, eta = list(ti.sim_pt), list(ti.sim_eta)
        dxy, vxy = list(ti.sim_pca_dxy), list(ti.sim_vtxperp)
        vz, chg = list(ti.sim_vz), list(ti.sim_q)
        for s in range(len(sl)):
            ae = abs(eta[s])
            if not (pt[s] > 0.9 and ae < 4.5 and abs(vz[s]) < 30.0 and chg[s] != 0):
                continue
            reg = "barrel" if ae < 1.1 else ("trans" if ae < 1.7 else "endcap")
            ma, ml = sa[s] >= 0, sl[s] >= 0
            for v in varlist:
                q = vxy[s] if v == "vxy" else abs(dxy[s])
                for bi, (lo, hi) in enumerate(BANDS):
                    if lo <= q < hi:
                        for r in (reg, "ALL"):
                            c = cell[(v, bi, r)]
                            c[0] += ma
                            c[1] += ml
                            c[2] += 1
                        break

    for v in varlist:
        print("\n=== efficiency vs %s, per eta region -- proto | LST | dTracks (denominator) ===" % v)
        hdr = "%-14s" % "band"
        for r, _, _ in REGIONS:
            hdr += "%28s" % r
        hdr += "%28s" % "ALL"
        print(hdr)
        for bi, (lo, hi) in enumerate(BANDS):
            lab = "%s[%g,%s)" % (v, lo, "inf" if hi > 1e17 else "%g" % hi)
            line = "%-14s" % lab
            for r, _, _ in REGIONS + [("ALL", 0, 0)]:
                pn, ln, d = cell[(v, bi, r)]
                if d == 0:
                    line += "%28s" % "-"
                else:
                    line += "%28s" % ("%.4f|%.4f %+d (%d)" % (pn / d, ln / d, pn - ln, d))
            print(line)


if __name__ == "__main__":
    main()
