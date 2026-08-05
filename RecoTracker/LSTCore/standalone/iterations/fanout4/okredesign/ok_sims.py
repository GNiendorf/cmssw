#!/usr/bin/env python3
"""Exact SIM COUNT deltas (not rate x denominator) between two ok_<tag>_hists.root files.

The efficiency denominators are identical across A/B runs (same sim sample), so the
numerator difference IS the number of sim tracks gained or lost -- the quantity the
"263 claim-order casualties" recon budget is denominated in.

  python3 ok_sims.py <tagA> <tagB> [tagC ...]      (all compared against tagA)
"""
import os
import sys

import ROOT

P = os.path.dirname(os.path.abspath(__file__))
EF = "Root__TC_base_0_0_ef_"
BANDS = [("vxy", 0, 1), ("vxy", 1, 5), ("vxy", 5, 10), ("vxy", 10, 30),
         ("dxy", 1, 5), ("dxy", 5, 10)]


def band(h, lo, hi):
    return sum(h.GetBinContent(i) for i in range(1, h.GetNbinsX() + 1)
               if lo <= abs(h.GetBinCenter(i)) < hi)


def counts(tag):
    f = ROOT.TFile.Open(os.path.join(P, f"ok_{tag}_hists.root"))
    out = {}
    he = f.Get(EF + "numer_eta")
    out["all(pt>0.9)"] = he.Integral(0, he.GetNbinsX() + 1)
    for var, lo, hi in BANDS:
        out[f"{var}[{lo},{hi})"] = band(f.Get(EF + "numer_" + var), lo, hi)
    for nm, hn in (("fakeTC", "Root__TC_fr_numer_eta"), ("dupTC", "Root__TC_dr_numer_eta"),
                   ("nTC", "Root__TC_fr_denom_eta")):
        h = f.Get(hn)
        out[nm] = h.Integral(0, h.GetNbinsX() + 1)
    f.Close()
    return out


def main():
    tags = sys.argv[1:]
    ref = counts(tags[0])
    keys = list(ref)
    print(f"{'tag':<18}" + "".join(f"{k:>13}" for k in keys))
    print(f"{tags[0] + ' (ref)':<18}" + "".join(f"{ref[k]:>13.0f}" for k in keys))
    for t in tags[1:]:
        c = counts(t)
        print(f"{t:<18}" + "".join(f"{c[k] - ref[k]:>+13.0f}" for k in keys))


if __name__ == "__main__":
    main()
