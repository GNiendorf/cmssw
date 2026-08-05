"""CRITIC ITEM 4(a) -- DUP vs |eta| IN 0.25 BINS, plus the maintainer window aggregates.

The exploits reported only 3-4 coarse bands; the maintainer's localization request was for
fine bins, and "the win takes the window below LST" cannot be checked against a band average.
Reads the dr_ set straight out of the createPerfNumDenHists files (180 bins, -4.5..4.5, so
0.05/bin; |eta| folded, 0.25 = 5 bins/side).

Usage: python3 fn_dupwin.py <tag> [<tag> ...]      (LST base300 always shown first)
"""
import sys, os
import ROOT

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/final"
BASE = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/base300_hists.root"
WINDOWS = [(0.0, 1.1), (1.1, 1.7), (1.5, 2.5), (1.5, 3.0), (1.7, 4.5), (0.0, 1.5), (2.5, 4.5), (0.0, 4.5)]


def get(f, n):
    h = f.Get(n)
    return h if h else None


def fold(h, lo, hi):
    """Sum |eta| in [lo,hi) of a symmetric -4.5..4.5 histogram."""
    s = 0.0
    for b in range(1, h.GetNbinsX() + 1):
        c = abs(h.GetXaxis().GetBinCenter(b))
        if lo <= c < hi:
            s += h.GetBinContent(b)
    return s


def series(path):
    f = ROOT.TFile.Open(path)
    if not f or f.IsZombie():
        return None
    d = {}
    for k, pre in (("dup", "Root__TC_dr_"), ("fake", "Root__TC_fr_"), ("eff", "Root__TC_base_0_0_ef_"),
                   ("ol", "Root__TC_ol_")):
        n = get(f, pre + "numer_eta"); dd = get(f, pre + "denom_eta")
        d[k] = (n.Clone(), dd.Clone()) if n and dd else (None, None)
        if n: d[k][0].SetDirectory(0)
        if dd: d[k][1].SetDirectory(0)
    f.Close()
    return d


def main(tags):
    runs = [("LST", series(BASE))]
    for t in tags:
        p = "%s/f_%s_hists.root" % (P, t)
        if os.path.exists(p):
            runs.append((t, series(p)))
        else:
            print("missing %s" % p)

    print("=" * 40 + " DUP vs |eta|, 0.25 BINS " + "=" * 40)
    hdr = "%-12s" % "|eta| bin"
    for n, _ in runs:
        hdr += "%12s" % n[:12]
    hdr += "%12s" % "dTC(win)"
    print(hdr)
    e = 0.0
    while e < 3.0:
        row = "%-12s" % ("%.2f-%.2f" % (e, e + 0.25))
        for n, d in runs:
            num, den = d["dup"]
            dv = fold(den, e, e + 0.25)
            row += "%12s" % (("%.4f" % (fold(num, e, e + 0.25) / dv)) if dv else "-")
        print(row)
        e += 0.25

    for key, lbl in (("dup", "DUP"), ("eff", "EFF"), ("fake", "FAKE")):
        print()
        print("=" * 34 + (" %s -- WINDOW AGGREGATES " % lbl) + "=" * 34)
        hdr = "%-14s" % "window"
        for n, _ in runs:
            hdr += "%12s" % n[:12]
        print(hdr)
        for lo, hi in WINDOWS:
            row = "%-14s" % ("|eta| %.2g-%.2g" % (lo, hi))
            for n, d in runs:
                num, den = d[key]
                dv = fold(den, lo, hi)
                row += "%12s" % (("%.4f" % (fold(num, lo, hi) / dv)) if dv else "-")
            print(row)

    print()
    print("=" * 34 + " POOLED mean nhitOT (MIX METRIC - see fn_len.py) " + "=" * 26)
    hdr = "%-14s" % "window"
    for n, _ in runs:
        hdr += "%12s" % n[:12]
    print(hdr)
    for lo, hi in WINDOWS:
        row = "%-14s" % ("|eta| %.2g-%.2g" % (lo, hi))
        for n, d in runs:
            num, den = d["ol"]
            dv = fold(den, lo, hi)
            row += "%12s" % (("%.4f" % (fold(num, lo, hi) / dv)) if dv else "-")
        print(row)


if __name__ == "__main__":
    main(sys.argv[1:])
