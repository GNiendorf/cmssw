#!/usr/bin/env python3
"""Independent re-derivation of the scoreboard metrics straight from
createPerfNumDenHists outputs. Deliberately does NOT import compare_ab.py -- the
histogram key names and band conventions are re-implemented here from the
createPerfNumDenHists output itself so that a bug in the round's judge would show up
as a disagreement.

Usage: v1_metrics.py <label>=<hists.root> [...]   -> one row per file, all metrics.
       v1_metrics.py --json <label>=<file> ...    -> JSON dump
"""
import sys, json
import ROOT

ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError

EF = "Root__TC_base_0_0_ef_"
FR = "Root__TC_fr_"
DR = "Root__TC_dr_"
OL = "Root__TC_ol_"

VXY = [(0.0, 1.0), (1.0, 5.0), (5.0, 10.0), (10.0, 30.0)]
REG = [("B", 0.0, 1.1), ("T", 1.1, 1.7), ("E", 1.7, None)]


def H(f, n):
    h = f.Get(n)
    if not h or not h.InheritsFrom("TH1"):
        return None
    return h


def S(h):
    return None if h is None else h.Integral(0, h.GetNbinsX() + 1)


def band(h, lo, hi):
    if h is None:
        return None
    t = 0.0
    ax = h.GetXaxis()
    for b in range(1, h.GetNbinsX() + 1):
        c = abs(ax.GetBinCenter(b))
        if c >= lo and (hi is None or c < hi):
            t += h.GetBinContent(b)
    return t


def r(n, d):
    if n is None or d is None or d <= 0:
        return None
    return n / d


def metrics(path):
    f = ROOT.TFile.Open(path)
    if not f or f.IsZombie():
        return None
    m = {}
    m["eff"] = r(S(H(f, EF + "numer_pt")), S(H(f, EF + "denom_pt")))
    m["nsim"] = S(H(f, EF + "denom_pt"))
    for var in ("vxy", "dxy"):
        hn, hd = H(f, EF + "numer_" + var), H(f, EF + "denom_" + var)
        for lo, hi in VXY:
            m["%s%g_%g" % (var, lo, hi)] = r(band(hn, lo, hi), band(hd, lo, hi))
            m["%s%g_%g_num" % (var, lo, hi)] = band(hn, lo, hi)
            m["%s%g_%g_den" % (var, lo, hi)] = band(hd, lo, hi)
    m["nTC"] = S(H(f, FR + "denom_pt"))
    m["fake"] = r(S(H(f, FR + "numer_pt")), S(H(f, FR + "denom_pt")))
    m["dup"] = r(S(H(f, DR + "numer_pt")), S(H(f, DR + "denom_pt")))
    efn, efd = H(f, EF + "numer_eta"), H(f, EF + "denom_eta")
    frn, frd = H(f, FR + "numer_eta"), H(f, FR + "denom_eta")
    drn, drd = H(f, DR + "numer_eta"), H(f, DR + "denom_eta")
    oln, old = H(f, OL + "numer_eta"), H(f, OL + "denom_eta")
    m["eff_ic"] = r(S(efn), S(efd))
    m["fake_ic"] = r(S(frn), S(frd))
    m["dup_ic"] = r(S(drn), S(drd))
    m["nh"] = r(S(oln), S(old))
    for g, lo, hi in REG:
        m["eff" + g] = r(band(efn, lo, hi), band(efd, lo, hi))
        m["fak" + g] = r(band(frn, lo, hi), band(frd, lo, hi))
        m["dup" + g] = r(band(drn, lo, hi), band(drd, lo, hi))
        m["nh" + g] = r(band(oln, lo, hi), band(old, lo, hi))
        m["nTC" + g] = band(frd, lo, hi)
        m["nsim" + g] = band(efd, lo, hi)
    f.Close()
    return m


COLS = ["eff", "vxy0_1", "vxy1_5", "vxy5_10", "vxy10_30", "dxy0_1", "dxy1_5", "dxy5_10",
        "dxy10_30", "dup", "fake", "eff_ic", "dup_ic", "fake_ic",
        "effB", "effT", "effE", "dupB", "dupT", "dupE", "fakB", "fakT", "fakE",
        "nh", "nhB", "nhT", "nhE", "nTC", "nsim"]


def main():
    args = sys.argv[1:]
    asjson = False
    if args and args[0] == "--json":
        asjson = True
        args = args[1:]
    out = {}
    for a in args:
        lab, path = a.split("=", 1)
        out[lab] = metrics(path)
    if asjson:
        print(json.dumps(out, indent=1))
        return
    hdr = "%-14s" % "tag" + "".join("%10s" % c for c in COLS)
    print(hdr)
    for lab, m in out.items():
        if m is None:
            print("%-14s  <unreadable>" % lab)
            continue
        row = "%-14s" % lab
        for c in COLS:
            v = m.get(c)
            if v is None:
                row += "%10s" % "n/a"
            elif c in ("nTC", "nsim"):
                row += "%10.0f" % v
            elif c.startswith("nh"):
                row += "%10.4f" % v
            else:
                row += "%10.5f" % v
        print(row)


main()
