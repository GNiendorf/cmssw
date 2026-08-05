#!/usr/bin/env python3
"""SKEPTIC-3: raw matched-sim COUNTS per band (numerators), not rates.

The M19 floors are all rates, which hide whether an efficiency move is a handful of
sim tracks or a structural change.  This prints the integer numerators so a claimed
'+33 sims' or 'zero sims' can be checked directly, plus the dup/fake numerators.

Usage: s3_counts.py <ref_tag> <tag> [tag...]
"""
import sys

import ROOT

from s3_tab import band, allsum, path

ROOT.gROOT.SetBatch(True)

EF = "Root__TC_base_0_0_ef_"
VB = [("vxy01", "vxy", 0, 1), ("v15", "vxy", 1, 5), ("v510", "vxy", 5, 10),
      ("v1030", "vxy", 10, 30), ("d01", "dxy", 0, 1), ("d15", "dxy", 1, 5),
      ("d510", "dxy", 5, 10), ("d1030", "dxy", 10, 30)]
EB = [("etaB", 0, 1.1), ("etaT", 1.1, 1.7), ("etaE", 1.7, None)]


def counts(tag):
    p, _ = path(tag)
    f = ROOT.TFile.Open(p)
    c = {}
    hn, hd = f.Get(EF + "numer_eta"), f.Get(EF + "denom_eta")
    c["effN"] = allsum(hn)
    c["effD"] = allsum(hd)
    for nm, lo, hi in EB:
        c["eff_" + nm + "N"] = band(hn, lo, hi)
        c["eff_" + nm + "D"] = band(hd, lo, hi)
    for nm, var, lo, hi in VB:
        a, b = f.Get(EF + "numer_" + var), f.Get(EF + "denom_" + var)
        c[nm + "N"] = band(a, lo, hi)
        c[nm + "D"] = band(b, lo, hi)
    c["dupN"] = allsum(f.Get("Root__TC_dr_numer_eta"))
    c["dupD"] = allsum(f.Get("Root__TC_dr_denom_eta"))
    c["fakeN"] = allsum(f.Get("Root__TC_fr_numer_eta"))
    c["fakeD"] = allsum(f.Get("Root__TC_fr_denom_eta"))
    for nm, lo, hi in [("W", 1.5, 3.0), ("B", 0, 1.1), ("T", 1.1, 1.7), ("E", 1.7, None)]:
        c["dup" + nm + "N"] = band(f.Get("Root__TC_dr_numer_eta"), lo, hi)
        c["dup" + nm + "D"] = band(f.Get("Root__TC_dr_denom_eta"), lo, hi)
    c["olN"] = allsum(f.Get("Root__TC_ol_numer_eta"))
    c["olD"] = allsum(f.Get("Root__TC_ol_denom_eta"))
    f.Close()
    return c


if __name__ == "__main__":
    ref = sys.argv[1]
    R = counts(ref)
    keys = [k for k in R if k.endswith("N") or k.endswith("D")]
    order = ["effN", "effD", "eff_etaBN", "eff_etaTN", "eff_etaEN",
             "vxy01N", "v15N", "v510N", "v1030N", "d01N", "d15N", "d510N", "d1030N",
             "dupN", "dupD", "dupBN", "dupTN", "dupEN", "dupWN",
             "fakeN", "fakeD", "olN", "olD"]
    print("ref = %s" % ref)
    hdr = "%-12s %12s" % ("count", ref.split(":")[-1])
    for t in sys.argv[2:]:
        hdr += " %14s" % t.split(":")[-1]
    print(hdr)
    print("-" * len(hdr))
    C = [counts(t) for t in sys.argv[2:]]
    for k in order:
        line = "%-12s %12.0f" % (k, R[k])
        for c in C:
            line += " %8.0f(%+5.0f)" % (c[k], c[k] - R[k])
        print(line)
