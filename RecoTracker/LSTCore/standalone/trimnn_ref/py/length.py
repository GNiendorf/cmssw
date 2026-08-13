#!/usr/bin/env python3
"""TRIM-NN: mean OUTER-TRACKER track length (tc_nhitOT) per arm, with the breakdowns the
maintainer asked for.

Definition, matched to `prototype/compare_ab.py`'s `mean_nhitOT` (the `Root__TC_ol_` set: numerator
= per-eta-bin sum of tc_nhitOT, denominator = the TC count of the same set) and to
`d3_ref/pu_judge.py`'s TC denominator:

    TC set   : tc_pt > 0.9                         (the fake / duplicate denominator)
    overall  : mean tc_nhitOT over that set
    barrel   : the same restricted to |tc_eta| < 1.1

Breakdowns, because an average that falls because bare pixel seeds were replaced by real 4-layer
chains means something completely different from one that falls because real 5-layer tracks were
truncated:

    matched  : tc_isFake == 0            (a real track's length -- the one that costs resolution)
    fake     : tc_isFake  > 0
    per CLASS: tc_type 4 = T5, 5 = pT3, 7 = pT5, 8 = bare pLS, 9 = T4
    jet-core : on the jets sample, TCs whose matched sim is in a > 1 TeV genjet with dR < 0.05
               (the -J core selection), taken through tc_simIdx.

Historical reference the arms are quoted against: at the 2026-08-06 master baseline the chain
pipeline sat at mean nhitOT 6.427 against LST master's 6.519.

Usage: length.py <label=file.root> [...] [--jets]
"""
import os
import sys

import awkward as ak
import numpy as np
import uproot

MASTER_BASELINE = 6.519      # LST master, 2026-08-06
PROTO_BASELINE = 6.427       # the chain pipeline at that same baseline
CLASSES = [(4, "T5"), (5, "pT3"), (7, "pT5"), (8, "pLS"), (9, "T4")]
JETPT, JETETA, CORE_DR = 1000.0, 2.5, 0.05


def mean(a):
    a = ak.to_numpy(ak.flatten(a)) if a.ndim > 1 else ak.to_numpy(a)
    return float(a.mean()) if len(a) else float("nan")


def row(path, jets):
    br = ["tc_pt", "tc_eta", "tc_isFake", "tc_type", "tc_nhitOT", "tc_nhits", "tc_nlayers", "tc_simIdx"]
    if jets:
        br += ["sim_genjet_idx", "sim_genjet_deltaR", "genjet_pt", "genjet_eta"]
    t = uproot.open(path)["tree"]
    d = t.arrays(br)
    sel = d.tc_pt > 0.9
    L = d.tc_nhitOT[sel]
    out = {}
    out["n_tc"] = int(ak.sum(sel))
    out["len_overall"] = mean(L)
    out["len_barrel"] = mean(d.tc_nhitOT[sel & (abs(d.tc_eta) < 1.1)])
    out["len_matched"] = mean(d.tc_nhitOT[sel & (d.tc_isFake == 0)])
    out["len_fake"] = mean(d.tc_nhitOT[sel & (d.tc_isFake > 0)])
    out["nhits_overall"] = mean(d.tc_nhits[sel])
    out["nlay_overall"] = mean(d.tc_nlayers[sel])
    out["nlay_matched"] = mean(d.tc_nlayers[sel & (d.tc_isFake == 0)])
    for ty, nm in CLASSES:
        s = sel & (d.tc_type == ty)
        out["n_" + nm] = int(ak.sum(s))
        out["len_" + nm] = mean(d.tc_nhitOT[s])
    if jets:
        # jet-core TCs: the matched sim is in a > 1 TeV genjet within dR < 0.05
        gj, sdr = d.sim_genjet_idx, d.sim_genjet_deltaR
        jpt, jeta = d.genjet_pt, d.genjet_eta
        core_len, core_n, ncore_len, ncore_n = [], 0, [], 0
        for e in range(len(d.tc_pt)):
            si = ak.to_numpy(d.tc_simIdx[e])
            ok = ak.to_numpy(d.tc_pt[e]) > 0.9
            lo = ak.to_numpy(d.tc_nhitOT[e])
            g = ak.to_numpy(gj[e])
            dr = ak.to_numpy(sdr[e])
            p = ak.to_numpy(jpt[e])
            et = ak.to_numpy(jeta[e])
            m = ok & (si >= 0)
            iscore = np.zeros(len(si), bool)
            if m.any():
                s2 = si[m]
                gg = g[s2]
                good = (gg >= 0) & (gg < len(p))
                tmp = np.zeros(len(s2), bool)
                if good.any():
                    tmp[good] = (p[gg[good]] > JETPT) & (np.abs(et[gg[good]]) < JETETA) & (dr[s2][good] < CORE_DR)
                iscore[m] = tmp
            core_len.append(lo[ok & iscore])
            ncore_len.append(lo[ok & ~iscore])
        cl = np.concatenate(core_len) if core_len else np.zeros(0)
        nl = np.concatenate(ncore_len) if ncore_len else np.zeros(0)
        out["len_jetcore"] = float(cl.mean()) if len(cl) else float("nan")
        out["n_jetcore"] = len(cl)
        out["len_nonjetcore"] = float(nl.mean()) if len(nl) else float("nan")
    return out


def main():
    jets = "--jets" in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    rows = {}
    for a in args:
        lab, path = a.split("=", 1)
        rows[lab] = row(path, jets)
    labs = list(rows)
    keys = [k for k in rows[labs[0]] if k.startswith("len_")] + \
           ["nhits_overall", "nlay_overall", "nlay_matched"] + \
           [k for k in rows[labs[0]] if k.startswith("n_")]
    ref = labs[0]
    hdr = "%-18s" % "metric" + "".join("%12s" % l for l in labs) + "   | vs %s" % ref
    print(hdr)
    for k in keys:
        line = "%-18s" % k
        for l in labs:
            v = rows[l][k]
            line += ("%12.4f" % v) if isinstance(v, float) else ("%12d" % v)
        v0, v1 = rows[ref][k], rows[labs[-1]][k]
        line += ("   | %+9.4f" % (v1 - v0)) if isinstance(v1, float) else ("   | %+9d" % (v1 - v0))
        print(line)
    print("reference: chain pipeline 2026-08-06 baseline mean nhitOT %.3f, LST master %.3f"
          % (PROTO_BASELINE, MASTER_BASELINE))


if __name__ == "__main__":
    main()
