#!/usr/bin/env python3
"""PER-SIM efficiency decomposition between two prototype/identity output files.

Both files must carry the SAME events in the SAME order with the SAME sim rows
(they do: every writer copies sim_* verbatim from the input event).

Reproduces the HEADLINE efficiency of the scoreboard EXACTLY. That number is
compare_ab.py's `eff_overall_incut` = sum_all(TC_base_0_0_ef_numer_eta) /
sum_all(..._denom_eta) INCLUDING under/overflow, i.e. from
efficiency/src/performance.cc:1189 the denominator is

    sim q != 0 AND sim_pt > 0.9 AND |sim_vz| < 30 AND sqrt(vx^2+vy^2) < 2.5
    (NO eta cut -- the eta histogram is the N-minus-eta-cut one and the
     |eta| >= 4.5 sims land in its overflow, which sum_all counts)

and the numerator is sim_tcIdx >= 0.  VERIFIED against r_FINBASE_hists.root:
numer 18331 / denom 22633 = 0.80993.

Per-REGION efficiencies use sum_band, which drops under/overflow, so the region
rows below are restricted to |eta| < 4.5 -- again exactly what compare_ab reports.

Splits the denominator into BOTH / LSTONLY (efficiency we lose) / OURSONLY
(efficiency we gain) / NEITHER and slices each by eta region, |eta|, pt, vxy, dxy
and -- the mechanism attribution -- by the tc_type of the row that DID deliver
the sim in each file.

usage: a04_diff.py --proto r_X.root --base rb_base300.root [--label X] [--json out.json]
"""
import argparse
import json
import sys

import ROOT

TYPENAME = {4: "T5", 5: "pT3", 7: "pT5", 8: "pLS", 9: "T4", -1: "none"}


def region(e):
    a = abs(e)
    if a >= 4.5:
        return "OFLOW"
    if a < 1.1:
        return "barrel"
    if a < 1.7:
        return "transi"
    return "endcap"


ETA_EDGES = [0.0, 0.6, 1.1, 1.4, 1.7, 2.0, 2.4, 2.8, 3.2, 4.5, 1e9]
PT_EDGES = [0.9, 1.2, 2.0, 4.0, 10.0, 1e9]
VXY_EDGES = [0.0, 1.0, 5.0, 10.0, 30.0, 1e9]
DXY_EDGES = [0.0, 1.0, 5.0, 10.0, 30.0, 1e9]


def binof(v, edges):
    for i in range(len(edges) - 1):
        if v < edges[i + 1]:
            return i
    return len(edges) - 2


def label(edges, i):
    hi = edges[i + 1]
    return "[%g,%s)" % (edges[i], "inf" if hi >= 1e9 else "%g" % hi)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--proto", required=True)
    ap.add_argument("--base", required=True)
    ap.add_argument("--label", default="proto")
    ap.add_argument("--json", default=None)
    ap.add_argument("--maxev", type=int, default=-1)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    ROOT.gROOT.SetBatch(True)
    ROOT.gErrorIgnoreLevel = ROOT.kError

    fp = ROOT.TFile.Open(args.proto)
    fb = ROOT.TFile.Open(args.base)
    tp = fp.Get("tree")
    tb = fb.Get("tree")
    n = min(tp.GetEntries(), tb.GetEntries())
    if args.maxev > 0:
        n = min(n, args.maxev)

    cnt = {}
    denom_tot = 0
    cat_tot = {"BOTH": 0, "LSTONLY": 0, "OURSONLY": 0, "NEITHER": 0}
    # region-restricted (|eta| < 4.5) tallies, the compare_ab per-region convention
    reg_den = {}
    reg_num_p = {}
    reg_num_b = {}

    def add(cat, axis, key):
        cnt[(cat, axis, key)] = cnt.get((cat, axis, key), 0) + 1

    for ie in range(n):
        tp.GetEntry(ie)
        tb.GetEntry(ie)
        se = tb.sim_eta
        sq = tb.sim_q
        svx, svy, svz = tb.sim_vx, tb.sim_vy, tb.sim_vz
        spt, sdxy = tb.sim_pt, tb.sim_pca_dxy
        bidx, pidx = tb.sim_tcIdx, tp.sim_tcIdx
        btype, ptype = tb.tc_type, tp.tc_type
        ns = len(se)
        if len(pidx) != ns:
            sys.exit("sim row count mismatch at entry %d (%d vs %d)" % (ie, ns, len(pidx)))
        for i in range(ns):
            if sq[i] == 0:
                continue
            pt = spt[i]
            if pt <= 0.9:
                continue
            vperp = (svx[i] ** 2 + svy[i] ** 2) ** 0.5
            if abs(svz[i]) >= 30.0 or vperp >= 2.5:
                continue
            denom_tot += 1
            eta = se[i]
            b = bidx[i] >= 0
            p = pidx[i] >= 0
            cat = "BOTH" if (b and p) else ("LSTONLY" if b else ("OURSONLY" if p else "NEITHER"))
            cat_tot[cat] += 1
            r = region(eta)
            if r != "OFLOW":
                reg_den[r] = reg_den.get(r, 0) + 1
                if p:
                    reg_num_p[r] = reg_num_p.get(r, 0) + 1
                if b:
                    reg_num_b[r] = reg_num_b.get(r, 0) + 1
            lt = TYPENAME.get(btype[bidx[i]] if b else -1, "?")
            ot = TYPENAME.get(ptype[pidx[i]] if p else -1, "?")
            add(cat, "region", r)
            add(cat, "eta", label(ETA_EDGES, binof(abs(eta), ETA_EDGES)))
            add(cat, "pt", label(PT_EDGES, binof(pt, PT_EDGES)))
            add(cat, "vxy", label(VXY_EDGES, binof(vperp, VXY_EDGES)))
            add(cat, "dxy", label(DXY_EDGES, binof(abs(sdxy[i]), DXY_EDGES)))
            add(cat, "lsttype", lt)
            add(cat, "ourtype", ot)
            add(cat, "lsttype_x_region", "%s/%s" % (lt, r))
            add(cat, "ourtype_x_region", "%s/%s" % (ot, r))

    print("=" * 82)
    print("PER-SIM EFFICIENCY DECOMPOSITION   %s  vs  %s   (%d events)" % (args.label, args.base, n))
    print("=" * 82)
    effP = (cat_tot["BOTH"] + cat_tot["OURSONLY"]) / float(denom_tot)
    effB = (cat_tot["BOTH"] + cat_tot["LSTONLY"]) / float(denom_tot)
    print("denominator (q!=0, pt>0.9, |vz|<30, vperp<2.5; NO eta cut) = %d  (%.1f/evt)"
          % (denom_tot, denom_tot / float(n)))
    print("eff ours = %.5f   eff LST = %.5f   delta = %+.5f" % (effP, effB, effP - effB))
    print("  BOTH %d | LSTONLY(loss) %d (%.2f/evt) | OURSONLY(gain) %d (%.2f/evt) | NEITHER %d"
          % (cat_tot["BOTH"], cat_tot["LSTONLY"], cat_tot["LSTONLY"] / float(n),
             cat_tot["OURSONLY"], cat_tot["OURSONLY"] / float(n), cat_tot["NEITHER"]))
    print("  CHURN = loss+gain = %d sims (%.2f%% of our numerator)"
          % (cat_tot["LSTONLY"] + cat_tot["OURSONLY"],
             100.0 * (cat_tot["LSTONLY"] + cat_tot["OURSONLY"]) / (cat_tot["BOTH"] + cat_tot["OURSONLY"])))
    print("\nper region (|eta| < 4.5, the compare_ab convention):")
    print("%-8s %8s %9s %9s %9s" % ("region", "denom", "eff ours", "eff LST", "delta"))
    for r in ("barrel", "transi", "endcap"):
        d = reg_den.get(r, 0)
        if d:
            print("%-8s %8d %9.5f %9.5f %+9.5f"
                  % (r, d, reg_num_p.get(r, 0) / float(d), reg_num_b.get(r, 0) / float(d),
                     (reg_num_p.get(r, 0) - reg_num_b.get(r, 0)) / float(d)))

    if args.quiet:
        axes = ["region", "lsttype", "ourtype"]
    else:
        axes = ["region", "eta", "pt", "vxy", "dxy", "lsttype", "ourtype",
                "lsttype_x_region", "ourtype_x_region"]
    for axis in axes:
        keys = sorted({k[2] for k in cnt if k[1] == axis})
        print("\n-- %s --" % axis)
        print("%-18s %9s %9s %9s %9s %9s" % ("bin", "LSTONLY", "OURSONLY", "NET", "BOTH", "NEITHER"))
        for k in keys:
            lo = cnt.get(("LSTONLY", axis, k), 0)
            gn = cnt.get(("OURSONLY", axis, k), 0)
            bo = cnt.get(("BOTH", axis, k), 0)
            ne = cnt.get(("NEITHER", axis, k), 0)
            print("%-18s %9d %9d %+9d %9d %9d" % (k, lo, gn, gn - lo, bo, ne))

    if args.json:
        payload = {"label": args.label, "n_events": n, "denom": denom_tot,
                   "eff_ours": effP, "eff_lst": effB, "cat_tot": cat_tot,
                   "reg_den": reg_den, "reg_num_ours": reg_num_p, "reg_num_lst": reg_num_b,
                   "cells": {"%s|%s|%s" % k: v for k, v in cnt.items()}}
        with open(args.json, "w") as jf:
            json.dump(payload, jf, indent=1)
        print("\nwrote %s" % args.json)


if __name__ == "__main__":
    main()
