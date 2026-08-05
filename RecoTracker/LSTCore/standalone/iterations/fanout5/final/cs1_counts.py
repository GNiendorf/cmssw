#!/usr/bin/env python3
"""cs1_counts.py -- raw sim-track numerator/denominator behind each displaced floor,
so floor margins can be quoted in TRACKS, not just in the 4th decimal."""
import sys
import ROOT

ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError

EF = "Root__TC_base_0_0_ef_"
BANDS = [("vxy", 1, 5, 0.7832), ("vxy", 5, 10, 0.7109), ("vxy", 10, 30, 0.6941),
         ("dxy", 1, 5, 0.5398), ("dxy", 5, 10, 0.2471), ("dxy", 10, 30, None),
         ("vxy", 0, 1, None), ("dxy", 0, 1, None)]


def band(h, lo, hi):
    tot = 0.0
    for b in range(1, h.GetNbinsX() + 1):
        c = abs(h.GetBinCenter(b))
        if c >= lo and (hi is None or c < hi):
            tot += h.GetBinContent(b)
    return tot


def main():
    for path in sys.argv[1:]:
        f = ROOT.TFile.Open(path)
        print("== %s" % path)
        # in-cut overall
        n = f.Get(EF + "numer_eta").Integral(0, f.Get(EF + "numer_eta").GetNbinsX() + 1)
        d = f.Get(EF + "denom_eta").Integral(0, f.Get(EF + "denom_eta").GetNbinsX() + 1)
        print("   eff in-cut      %8.0f / %8.0f = %.5f" % (n, d, n / d))
        for var, lo, hi, floor in BANDS:
            hn = f.Get(EF + "numer_" + var)
            hd = f.Get(EF + "denom_" + var)
            nn, dd = band(hn, lo, hi), band(hd, lo, hi)
            v = nn / dd if dd else 0.0
            extra = ""
            if floor is not None:
                need = floor * dd
                extra = " | floor %.4f needs %.1f -> margin %+.1f tracks" % (floor, need, nn - need)
            print("   %s[%2d,%2d) %8.0f / %8.0f = %.5f%s" % (var, lo, hi, nn, dd, v, extra))
        f.Close()


if __name__ == "__main__":
    main()
