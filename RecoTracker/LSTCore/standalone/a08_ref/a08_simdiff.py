#!/usr/bin/env python3
"""a08_simdiff.py -- TRACK-LEVEL displaced diff. Rates hide single tracks; this counts them.

Aligns a prototype output ntuple (or two of them) with the LST input ntuple event-by-event
by `evt` and compares `sim_tcIdx >= 0` per sim. For a chosen displacement band it reports
LOST / GAINED / net and profiles both sets: the TC type and nhitOT LST used to find the
lost ones, and the TC type WE used to find the gained ones, plus pt / eta / vxy / |dxy|.

  --a  proto output A (required)                  --b  proto output B (optional)
  --input  the LST ntuple A and B were run on     (truth + LST's own sim_tcIdx)
With --b, the A-vs-B comparison is printed as well (stage attribution: which tracks the
change between two configurations actually moved).

Bands are named on the command line, e.g. --band dxy:10:30 or --band vxy:5:10; the
selection is the standard one (pt > 0.9, |eta| < 4.5) plus the band.
"""
import argparse
import math
import sys
from collections import Counter

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


def qt(v):
    if not v:
        return "n/a"
    s = sorted(v)
    n = len(s)
    return "min=%.2f q25=%.2f med=%.2f q75=%.2f max=%.2f" % (s[0], s[n // 4], s[n // 2], s[3 * n // 4], s[-1])


def profile(name, rows, typec, lenc):
    if not rows:
        print("  %s: none" % name)
        return
    n = len(rows)
    f = 100.0 / n
    print("  %s: N=%d" % (name, n))
    if typec:
        print("    TC type   : " + "  ".join("%s=%d (%.0f%%)" % (TYPE_NAME.get(t, t), c, c * f)
                                             for t, c in sorted(typec.items(), key=lambda x: -x[1])))
    if lenc:
        print("    nhitOT    : " + "  ".join("%d:%d" % (k, v) for k, v in sorted(lenc.items())))
    print("    pt        : " + qt([r[0] for r in rows]))
    print("    |eta|     : " + qt([abs(r[1]) for r in rows]))
    print("    vxy       : " + qt([r[2] for r in rows]))
    print("    |dxy|     : " + qt([abs(r[3]) for r in rows]))
    pdg = Counter(abs(r[4]) for r in rows)
    print("    |pdgId|   : " + "  ".join("%d:%d" % (k, v) for k, v in sorted(pdg.items(), key=lambda x: -x[1])[:6]))
    er = Counter("barrel<1.1" if abs(r[1]) < 1.1 else ("trans1.1-1.7" if abs(r[1]) < 1.7 else "endcap>1.7")
                 for r in rows)
    print("    eta region: " + "  ".join("%s=%d (%.0f%%)" % (k, v, v * f) for k, v in er.most_common()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", default=None)
    ap.add_argument("--input", required=True)
    ap.add_argument("--band", default="dxy:10:30", help="vxy:lo:hi or dxy:lo:hi or all")
    args = ap.parse_args()
    ROOT.gROOT.SetBatch(True)
    ROOT.gErrorIgnoreLevel = ROOT.kError

    var, lo, hi = "all", 0.0, 1e18
    if args.band != "all":
        p = args.band.split(":")
        var, lo, hi = p[0], float(p[1]), float(p[2])

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
    print("[a08_simdiff] band=%s  events a=%d input=%d common=%d" % (args.band, len(ea), len(ei), len(common)))

    n_sel = n_lst = n_a = n_b = 0
    lost, gained = [], []            # A vs LST
    lost_type, lost_len = Counter(), Counter()
    gain_type, gain_len = Counter(), Counter()
    ab_lost, ab_gain = [], []        # B relative to A
    ab_lost_type, ab_gain_type = Counter(), Counter()
    skipped = 0

    for evt in common:
        ta.GetEntry(ea[evt])
        ti.GetEntry(ei[evt])
        if tb:
            tb.GetEntry(eb[evt])
        stc_a = list(ta.sim_tcIdx)
        stc_l = list(ti.sim_tcIdx)
        stc_b = list(tb.sim_tcIdx) if tb else None
        if len(stc_a) != len(stc_l) or (stc_b is not None and len(stc_b) != len(stc_l)):
            skipped += 1
            continue
        pt, eta = list(ti.sim_pt), list(ti.sim_eta)
        dxy, vxy = list(ti.sim_pca_dxy), list(ti.sim_vtxperp)
        pdg = list(ti.sim_pdgId)
        vz, chg = list(ti.sim_vz), list(ti.sim_q)
        ltype, llen = list(ti.tc_type), list(ti.tc_nhitOT)
        atype, alen = list(ta.tc_type), list(ta.tc_nhitOT)
        btype = list(tb.tc_type) if tb else None
        blen = list(tb.tc_nhitOT) if tb else None

        for s in range(len(stc_l)):
            # EXACTLY the createPerfNumDenHists "N minus dxy" denominator selection
            # (performance.cc:1208): |eta| < 4.5, pt > 0.9, |vtx_z| < 30, charge != 0.
            # Neither the vxy nor the |dxy| cut is applied for these two plots, so the
            # counts below reproduce the histogram numerators/denominators exactly.
            if not (pt[s] > 0.9 and abs(eta[s]) < 4.5 and abs(vz[s]) < 30.0 and chg[s] != 0):
                continue
            q = vxy[s] if var == "vxy" else (abs(dxy[s]) if var == "dxy" else 0.0)
            if not (lo <= q < hi):
                continue
            n_sel += 1
            ml, ma = stc_l[s] >= 0, stc_a[s] >= 0
            n_lst += ml
            n_a += ma
            row = (pt[s], eta[s], vxy[s], dxy[s], pdg[s])
            if ml and not ma:
                lost.append(row)
                lost_type[ltype[stc_l[s]]] += 1
                lost_len[llen[stc_l[s]]] += 1
            elif ma and not ml:
                gained.append(row)
                gain_type[atype[stc_a[s]]] += 1
                gain_len[alen[stc_a[s]]] += 1
            if stc_b is not None:
                mb = stc_b[s] >= 0
                n_b += mb
                if ma and not mb:
                    ab_lost.append(row)
                    ab_lost_type[atype[stc_a[s]]] += 1
                elif mb and not ma:
                    ab_gain.append(row)
                    ab_gain_type[btype[stc_b[s]]] += 1

    if skipped:
        print("WARNING: %d events skipped (sim-list length mismatch)" % skipped)
    print("\nselection pt>0.9 |eta|<4.5 + band -> nSim=%d" % n_sel)
    print("  LST matched   %d  (%.5f)" % (n_lst, n_lst / max(n_sel, 1)))
    print("  A   matched   %d  (%.5f)   net vs LST %+d" % (n_a, n_a / max(n_sel, 1), n_a - n_lst))
    if tb:
        print("  B   matched   %d  (%.5f)   net vs A   %+d" % (n_b, n_b / max(n_sel, 1), n_b - n_a))
    print("\n--- A vs LST ---   LOST=%d  GAINED=%d  net=%+d" % (len(lost), len(gained), len(gained) - len(lost)))
    profile("LOST  (LST found, A did not) -- LST's matched TC", lost, lost_type, lost_len)
    profile("GAINED(A found, LST did not) -- A's matched TC", gained, gain_type, gain_len)
    if tb:
        print("\n--- B vs A ---   LOST=%d  GAINED=%d  net=%+d" %
              (len(ab_lost), len(ab_gain), len(ab_gain) - len(ab_lost)))
        profile("B LOST (A found, B did not) -- A's TC", ab_lost, ab_lost_type, None)
        profile("B GAINED (B found, A did not) -- B's TC", ab_gain, ab_gain_type, None)


if __name__ == "__main__":
    main()
