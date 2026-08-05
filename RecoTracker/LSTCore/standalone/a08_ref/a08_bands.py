#!/usr/bin/env python3
"""a08_bands.py -- DISPLACED BAND AUDIT with DENOMINATORS and BINOMIAL NOISE.

compare_ab.py reports displaced efficiency as a RATE only. For the displaced bands the
denominators are small (the d510 band is ~285 sims on the frozen 300), so a rate delta
of a few 1e-3 can be a single track. This prints, for every band:

    numerator, DENOMINATOR, rate, binomial sigma(rate) = sqrt(p(1-p)/N)

for the proto file and the base file, the delta, and the delta expressed in units of the
uncorrelated 1-sigma combination. It also prints the delta in TRACKS (numerator counts),
which is the only honest unit for a small-denominator band.

The two samples are the SAME sims (same input ntuple), so the numerator difference is
paired and the uncorrelated sigma is a CONSERVATIVE (over-)estimate of the noise. A band
whose track delta is 0 has not moved at all, regardless of what the rate rounding shows.

Usage:  a08_bands.py --proto r_X_hists.root [--base rb_base300_hists.root] [--label X]
        a08_bands.py --multi tagA=fileA.root tagB=fileB.root ... [--base ...]
"""
import argparse
import math
import sys

import ROOT

EF = "Root__TC_base_0_0_ef_"
FR = "Root__TC_fr_"
DR = "Root__TC_dr_"
OL = "Root__TC_ol_"

# Displacement bands. The first four of each match compare_ab.py exactly so the numbers
# are quotable against the round scoreboard; the >=30 tail is added because nothing in
# the round has ever looked at it.
VXY_BANDS = [(0.0, 1.0), (1.0, 5.0), (5.0, 10.0), (10.0, 30.0), (30.0, None)]
DXY_BANDS = [(0.0, 1.0), (1.0, 5.0), (5.0, 10.0), (10.0, 30.0), (30.0, None)]
ETA_REGIONS = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, None)]


def band_label(var, lo, hi):
    return "%s[%g,%s)" % (var, lo, "inf" if hi is None else "%g" % hi)


def get(f, name):
    h = f.Get(name)
    if not h or not h.InheritsFrom("TH1"):
        return None
    return h


def sum_band(h, lo, hi):
    if h is None:
        return None
    tot = 0.0
    ax = h.GetXaxis()
    for b in range(1, h.GetNbinsX() + 1):
        c = abs(ax.GetBinCenter(b))
        if c >= lo and (hi is None or c < hi):
            tot += h.GetBinContent(b)
    return tot


def sum_all(h):
    if h is None:
        return None
    return h.Integral(0, h.GetNbinsX() + 1)


def rows_for(path):
    """[(label, numer, denom)] for every band/region of one hists file."""
    f = ROOT.TFile.Open(path)
    if not f or f.IsZombie():
        sys.exit("cannot open %s" % path)
    out = []
    hn_eta, hd_eta = get(f, EF + "numer_eta"), get(f, EF + "denom_eta")
    out.append(("eff overall(pt>0.9)", sum_all(hn_eta), sum_all(hd_eta)))
    for var, bands in (("vxy", VXY_BANDS), ("dxy", DXY_BANDS)):
        hn, hd = get(f, EF + "numer_" + var), get(f, EF + "denom_" + var)
        for lo, hi in bands:
            out.append(("eff " + band_label(var, lo, hi), sum_band(hn, lo, hi), sum_band(hd, lo, hi)))
    for reg, lo, hi in ETA_REGIONS:
        out.append(("eff " + reg, sum_band(hn_eta, lo, hi), sum_band(hd_eta, lo, hi)))
    for tag, pfx in (("dup", DR), ("fake", FR)):
        n, d = get(f, pfx + "numer_eta"), get(f, pfx + "denom_eta")
        out.append(("%s overall" % tag, sum_all(n), sum_all(d)))
        for reg, lo, hi in ETA_REGIONS:
            out.append(("%s %s" % (tag, reg), sum_band(n, lo, hi), sum_band(d, lo, hi)))
    on, od = get(f, OL + "numer_eta"), get(f, OL + "denom_eta")
    out.append(("nhitOT overall", sum_all(on), sum_all(od)))
    for reg, lo, hi in ETA_REGIONS:
        out.append(("nhitOT " + reg, sum_band(on, lo, hi), sum_band(od, lo, hi)))
    f.Close()
    return out


def sigma(n, d):
    if not d or d <= 0:
        return None
    p = n / d
    return math.sqrt(max(p * (1.0 - p), 0.0) / d)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--proto")
    ap.add_argument("--base", default=None)
    ap.add_argument("--label", default="proto")
    ap.add_argument("--multi", nargs="*", default=None, help="tag=hists.root ...")
    args = ap.parse_args()
    ROOT.gROOT.SetBatch(True)
    ROOT.gErrorIgnoreLevel = ROOT.kError

    if args.multi:
        sets = []
        for spec in args.multi:
            tag, path = spec.split("=", 1)
            sets.append((tag, rows_for(path)))
        base = rows_for(args.base) if args.base else None
        labels = [r[0] for r in sets[0][1]]
        hdr = "%-22s" % "band"
        for tag, _ in sets:
            hdr += "%22s" % tag
        if base:
            hdr += "%22s" % "LST(base)"
        print(hdr)
        print("-" * len(hdr))
        for i, lab in enumerate(labels):
            line = "%-22s" % lab
            for _, rr in sets:
                n, d = rr[i][1], rr[i][2]
                line += "%22s" % ("%.5f %d/%d" % (n / d, n, d) if d else "n/a")
            if base:
                n, d = base[i][1], base[i][2]
                line += "%22s" % ("%.5f %d/%d" % (n / d, n, d) if d else "n/a")
            print(line)
        return

    pr = rows_for(args.proto)
    ba = rows_for(args.base) if args.base else None
    hdr = ("%-22s %10s %9s %9s %10s %9s %9s %10s %9s %8s" %
           ("band", "protoRate", "pNum", "pDen", "baseRate", "bNum", "dRate", "dTracks", "sigComb", "n_sig"))
    print("proto=%s" % args.proto)
    if args.base:
        print("base =%s" % args.base)
    print(hdr)
    print("-" * len(hdr))
    for i, (lab, pn, pd) in enumerate(pr):
        if not pd:
            continue
        prate = pn / pd
        if ba is None:
            print("%-22s %10.5f %9.0f %9.0f" % (lab, prate, pn, pd))
            continue
        bn, bd = ba[i][1], ba[i][2]
        brate = bn / bd if bd else float("nan")
        ps, bs = sigma(pn, pd), sigma(bn, bd)
        sc = math.sqrt((ps or 0) ** 2 + (bs or 0) ** 2)
        d = prate - brate
        nsig = (d / sc) if sc > 0 else 0.0
        print("%-22s %10.5f %9.0f %9.0f %10.5f %9.0f %+9.5f %+10.0f %9.5f %+8.2f" %
              (lab, prate, pn, pd, brate, bn, d, pn - bn, sc, nsig))


if __name__ == "__main__":
    main()
