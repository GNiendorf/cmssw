#!/usr/bin/env python3
"""RECON B -- length anatomy.

Decomposes mean tc_nhitOT (the harness ol_ metric: sum of tc_nhitOT over ALL TCs
with pt>0.9, divided by the TC count, per eta band) into per-class contributions.

Classes for a prototype output:  tc_isChain 0 = carried baseline row, 1 = chain TC,
2 = attach-delivered pT5-like, 3 = attach-delivered pT3-like; sub-split by tc_type.
For the LST baseline the only label is tc_type (7 pT5, 5 pT3, 4 T5, 8 pLS).
"""
import sys
import numpy as np
import uproot
import awkward as ak

REG = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 99.0)]
PTCUT = 0.9


def load(path, is_base):
    br = ["tc_pt", "tc_eta", "tc_type", "tc_nhitOT", "tc_isFake", "tc_isDuplicate"]
    with uproot.open(path) as f:
        t = f["tree"]
        names = set(t.keys())
        if "tc_isChain" in names:
            br.append("tc_isChain")
        a = t.arrays(br, library="ak")
    d = {k: ak.to_numpy(ak.flatten(a[k])) for k in br}
    if "tc_isChain" not in d:
        d["tc_isChain"] = np.zeros_like(d["tc_type"])
    return d


def label(typ, ch):
    if ch == 1:
        return "chain(t%d)" % typ
    if ch == 2:
        return "attachP5"
    if ch == 3:
        return "attachP3"
    return {7: "carr_pT5", 5: "carr_pT3", 4: "carr_T5", 8: "carr_pLS", 9: "carr_T4"}.get(typ, "carr_t%d" % typ)


def anat(d, tag):
    sel = d["tc_pt"] > PTCUT
    out = {}
    for reg, lo, hi in REG:
        ae = np.abs(d["tc_eta"])
        m = sel & (ae >= lo) & (ae < hi)
        n = int(m.sum())
        h = float(d["tc_nhitOT"][m].sum())
        rows = {}
        labs = np.array([label(t, c) for t, c in zip(d["tc_type"][m], d["tc_isChain"][m])])
        for L in sorted(set(labs.tolist())):
            mm = labs == L
            rows[L] = (int(mm.sum()), float(d["tc_nhitOT"][m][mm].sum()))
        out[reg] = dict(n=n, hits=h, mean=h / n if n else 0.0, rows=rows)
    return out


def show(name, A):
    print("=" * 100)
    print("%s" % name)
    for reg in ("barrel", "transition", "endcap"):
        r = A[reg]
        print("  %-11s nTC=%7d  sumOThits=%10.0f  mean=%7.3f" % (reg, r["n"], r["hits"], r["mean"]))
        for L, (n, h) in sorted(r["rows"].items(), key=lambda kv: -kv[1][0]):
            print("      %-12s n=%7d (%5.1f%%)  hits=%9.0f  meanOwn=%6.2f  contrib=%6.3f"
                  % (L, n, 100.0 * n / r["n"], h, h / n if n else 0, h / r["n"]))


if __name__ == "__main__":
    base = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/LSTNtuple_PU200RelVal_300evt.root"
    show("LST BASELINE (input ntuple)", anat(load(base, True), "base"))
    for p in sys.argv[1:]:
        show(p.split("/")[-1], anat(load(p, False), p))
