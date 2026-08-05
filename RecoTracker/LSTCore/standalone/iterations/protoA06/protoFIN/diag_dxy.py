#!/usr/bin/env python3
"""diag_dxy.py - diagnose the eff-vs-dxy regression of a hybrid ab_*.root.

For displaced-selection sims (pt>0.9, |eta|<4.5, 1<=|dxy|<=30) compare
sim_tcIdx>=0 between the hybrid output and the baseline input LSTNtuple,
event-by-event (aligned by evt; the hybrid sim list is the input accepted sim
list copied verbatim, so rows align 1:1). Profiles the sims matched in the
baseline but NOT in the hybrid: pt, eta, vxy-vs-dxy cross-tab, baseline
matched-TC type and nhitOT, plus whether any KEPT pixel row (type 7/5/8)
matched them in the input (such losses should be impossible by construction -
the hybrid copies all pixel rows - so a nonzero count flags a bug).

Usage: diag_dxy.py [--hybrid ab_X.root] [--input LSTNtuple.root]
"""
import argparse
import sys
from collections import Counter

import ROOT

PIX_TYPES = (7, 5, 8)
CHAIN_TYPES = (4, 9)
MATCH_FRAC = 0.75
STANDALONE = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"

TYPE_NAME = {4: "T5", 9: "T4", 7: "pT5", 5: "pT3", 8: "pLS"}


def evt_map(tree):
    tree.SetBranchStatus("*", 0)
    tree.SetBranchStatus("evt", 1)
    m = {}
    for i in range(tree.GetEntries()):
        tree.GetEntry(i)
        m[int(tree.evt)] = i
    tree.SetBranchStatus("*", 1)
    return m


def qtiles(vals):
    if not vals:
        return "n/a"
    v = sorted(vals)
    n = len(v)
    return (f"min={v[0]:.2f} q25={v[n // 4]:.2f} med={v[n // 2]:.2f} "
            f"q75={v[3 * n // 4]:.2f} max={v[-1]:.2f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hybrid", default=f"{STANDALONE}/prototype/ab_default.root")
    ap.add_argument("--input", default=f"{STANDALONE}/LSTNtuple_PU200RelVal_300evt.root")
    args = ap.parse_args()

    fh = ROOT.TFile.Open(args.hybrid)
    fi = ROOT.TFile.Open(args.input)
    if not fh or fh.IsZombie():
        sys.exit(f"cannot open hybrid file {args.hybrid}")
    if not fi or fi.IsZombie():
        sys.exit(f"cannot open input file {args.input}")
    th = fh.Get("tree")
    ti = fi.Get("tree")

    emap_h = evt_map(th)
    emap_i = evt_map(ti)
    common = [e for e in emap_h if e in emap_i]
    print(f"[diag_dxy] events: hybrid={len(emap_h)} input={len(emap_i)} common={len(common)}")

    th.SetBranchStatus("*", 0)
    for b in ("sim_tcIdx", "evt"):
        th.SetBranchStatus(b, 1)
    ti.SetBranchStatus("*", 0)
    for b in ("sim_pt", "sim_eta", "sim_pca_dxy", "sim_vtxperp", "sim_pdgId",
              "sim_tcIdx", "tc_type", "tc_nhitOT", "tc_simIdxAll",
              "tc_simIdxAllFrac", "evt"):
        ti.SetBranchStatus(b, 1)

    n_sel = 0
    n_base = 0
    n_hyb = 0
    n_lost = 0
    n_gain = 0
    lost_base_type = Counter()
    lost_pix_covered = 0  # lost sims that a KEPT pixel row matched in input (bug if >0)
    lost_pt, lost_eta, lost_dxy, lost_vxy = [], [], [], []
    lost_len = Counter()
    lost_pdg = Counter()
    vxy_bins = [(0, 1), (1, 5), (5, 15), (15, 1e9)]
    dxy_bins = [(1, 3), (3, 10), (10, 30.0001)]
    cross = Counter()  # (vxy_bin, dxy_bin) -> n
    eta_reg = Counter()
    pt_bins = Counter()
    n_mismatch_rows = 0

    for evt in common:
        th.GetEntry(emap_h[evt])
        ti.GetEntry(emap_i[evt])
        stc_h = list(th.sim_tcIdx)
        stc_b = list(ti.sim_tcIdx)
        if len(stc_h) != len(stc_b):
            n_mismatch_rows += 1
            continue
        pt = list(ti.sim_pt)
        eta = list(ti.sim_eta)
        dxy = list(ti.sim_pca_dxy)
        vxy = list(ti.sim_vtxperp)
        pdg = list(ti.sim_pdgId)
        ittype = list(ti.tc_type)
        itlen = list(ti.tc_nhitOT)

        # sims matched by a KEPT pixel row in the input (frac > MATCH_FRAC)
        pix_matched = set()
        for k in range(len(ittype)):
            if ittype[k] not in PIX_TYPES:
                continue
            for s, f in zip(ti.tc_simIdxAll[k], ti.tc_simIdxAllFrac[k]):
                if f > MATCH_FRAC:
                    pix_matched.add(int(s))

        for s in range(len(stc_b)):
            if not (pt[s] > 0.9 and abs(eta[s]) < 4.5 and 1.0 <= abs(dxy[s]) <= 30.0):
                continue
            n_sel += 1
            bm = stc_b[s] >= 0
            hm = stc_h[s] >= 0
            n_base += bm
            n_hyb += hm
            if bm and not hm:
                n_lost += 1
                bt = ittype[stc_b[s]]
                lost_base_type[bt] += 1
                lost_len[itlen[stc_b[s]]] += 1
                lost_pdg[abs(pdg[s])] += 1
                lost_pt.append(pt[s])
                lost_eta.append(eta[s])
                lost_dxy.append(abs(dxy[s]))
                lost_vxy.append(vxy[s])
                if s in pix_matched:
                    lost_pix_covered += 1
                ae = abs(eta[s])
                reg = "barrel(|eta|<0.8)" if ae < 0.8 else (
                    "transition(0.8-1.6)" if ae < 1.6 else "endcap(>1.6)")
                eta_reg[reg] += 1
                p = pt[s]
                pb = "0.9-1.5" if p < 1.5 else ("1.5-3" if p < 3 else ("3-10" if p < 10 else ">10"))
                pt_bins[pb] += 1
                for iv, (vlo, vhi) in enumerate(vxy_bins):
                    if vlo <= vxy[s] < vhi:
                        for idx, (dlo, dhi) in enumerate(dxy_bins):
                            if dlo <= abs(dxy[s]) < dhi:
                                cross[(iv, idx)] += 1
            elif hm and not bm:
                n_gain += 1

    if n_mismatch_rows:
        print(f"[diag_dxy] WARNING: {n_mismatch_rows} events had different sim-list "
              "lengths between files and were skipped")

    print(f"\nselection: pt>0.9 |eta|<4.5 1<=|dxy|<=30  ->  nSim={n_sel}")
    print(f"matched: baseline={n_base} ({100.0 * n_base / max(n_sel, 1):.2f}%)  "
          f"hybrid={n_hyb} ({100.0 * n_hyb / max(n_sel, 1):.2f}%)")
    print(f"LOST (base yes, hybrid no) = {n_lost}   GAINED (hybrid only) = {n_gain}   "
          f"net = {n_hyb - n_base:+d}")

    if n_lost == 0:
        print("no lost sims - nothing to profile")
        return

    fl = 100.0 / n_lost
    print(f"\n--- profile of the {n_lost} LOST sims ---")
    print("baseline matched-TC type : " + "  ".join(
        f"{TYPE_NAME.get(t, t)}={n} ({n * fl:.1f}%)"
        for t, n in sorted(lost_base_type.items(), key=lambda x: -x[1])))
    print(f"lost but pixel-covered in input (should be 0) : {lost_pix_covered}")
    print("baseline matched-TC nhitOT : " + "  ".join(
        f"{k}:{v}" for k, v in sorted(lost_len.items())))
    print("pt bins  : " + "  ".join(f"{k}={v} ({v * fl:.1f}%)"
                                    for k, v in sorted(pt_bins.items())))
    print("pt       : " + qtiles(lost_pt))
    print("eta reg  : " + "  ".join(f"{k}={v} ({v * fl:.1f}%)"
                                    for k, v in sorted(eta_reg.items(), key=lambda x: -x[1])))
    print("|dxy|    : " + qtiles(lost_dxy))
    print("vxy      : " + qtiles(lost_vxy))
    print("|pdgId|  : " + "  ".join(f"{k}:{v}" for k, v in
                                    sorted(lost_pdg.items(), key=lambda x: -x[1])[:8]))

    print("\nvxy vs |dxy| cross-tab of LOST sims (rows=vxy, cols=|dxy|):")
    hdr = "  ".join(f"dxy[{lo:g},{min(hi, 30):g})".rjust(12) for lo, hi in dxy_bins)
    print(" " * 14 + hdr)
    for iv, (vlo, vhi) in enumerate(vxy_bins):
        lbl = f"vxy[{vlo:g},{'inf' if vhi > 1e8 else f'{vhi:g}'})".ljust(14)
        row = "  ".join(str(cross.get((iv, idx), 0)).rjust(12) for idx in range(len(dxy_bins)))
        print(lbl + row)
    print("\n(vxy<1 with |dxy|>=1 = prompt-origin loopers/curlers whose PCA is far "
          "from the beamline; vxy>=1 = genuinely displaced origin.)")


if __name__ == "__main__":
    main()
