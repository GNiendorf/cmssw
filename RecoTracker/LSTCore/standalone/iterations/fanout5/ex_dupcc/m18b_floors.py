#!/usr/bin/env python3
"""M18b closeout: floor audit + nhitOT for every flagship run.

For each hard floor, reports the value, the pass/fail verdict, and the MARGIN EXPRESSED
IN SIM TRACKS (the numerator surplus over the bar), so a "pass" that is really 1-3
tracks of headroom is visible as such. Band sums replicate compare_ab.py exactly
(bin centers, |center| folded, under/overflow excluded).
"""
import argparse
import json
import sys

import ROOT

EF = "Root__TC_base_0_0_ef_"  # pdgid 0, charge 0 = all (compare_ab.py convention)
OL = "Root__TC_ol_"           # mean track length (nhitOT) set
FLOORS = [("eff_vxy_1_5", "v15", 0.7832), ("eff_vxy_5_10", "v510", 0.7109),
          ("eff_vxy_10_30", "v1030", 0.6941), ("eff_dxy_1_5", "d15", 0.5398),
          ("eff_dxy_5_10", "d510", 0.2471)]
BANDS = {"eff_vxy_1_5": ("vxy", 1.0, 5.0), "eff_vxy_5_10": ("vxy", 5.0, 10.0),
         "eff_vxy_10_30": ("vxy", 10.0, 30.0), "eff_dxy_1_5": ("dxy", 1.0, 5.0),
         "eff_dxy_5_10": ("dxy", 5.0, 10.0)}


def sum_band(h, lo, hi):
    total = 0.0
    ax = h.GetXaxis()
    for b in range(1, h.GetNbinsX() + 1):
        c = abs(ax.GetBinCenter(b))
        if c >= lo and (hi is None or c < hi):
            total += h.GetBinContent(b)
    return total


def audit(path, tag):
    f = ROOT.TFile.Open(path)
    out = {"tag": tag, "floors": {}}
    for key, short, bar in FLOORS:
        var, lo, hi = BANDS[key]
        hn = f.Get(EF + "numer_" + var)
        hd = f.Get(EF + "denom_" + var)
        num, den = sum_band(hn, lo, hi), sum_band(hd, lo, hi)
        val = num / den if den > 0 else 0.0
        out["floors"][short] = {"val": val, "bar": bar, "num": num, "den": den,
                                "margin_tracks": num - bar * den, "pass": val >= bar}
    # mean track length per eta region from the ol_ set.
    for reg, lo, hi in (("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, None)):
        hn = f.Get(OL + "numer_eta")
        hd = f.Get(OL + "denom_eta")
        if hn and hd:
            n, d = sum_band(hn, lo, hi), sum_band(hd, lo, hi)
            out.setdefault("nhitOT", {})[reg] = n / d if d > 0 else None
    f.Close()
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("pairs", nargs="+", help="tag=path/to/hists.root")
    a = ap.parse_args()
    res = [audit(p.split("=", 1)[1], p.split("=", 1)[0]) for p in a.pairs]
    hdr = "%-20s" % "tag"
    for _, s, bar in FLOORS:
        hdr += " | %-19s" % ("%s>=%.4f" % (s, bar))
    hdr += " | nhitOT b/t/e"
    print(hdr)
    for r in res:
        line = "%-20s" % r["tag"]
        for _, s, _bar in FLOORS:
            d = r["floors"][s]
            line += " | %.4f %s %+7.1ftrk" % (d["val"], "PASS" if d["pass"] else "FAIL",
                                              d["margin_tracks"])
        nh = r.get("nhitOT", {})
        line += " | %s" % "/".join("%.3f" % nh[k] if nh.get(k) is not None else "-"
                                   for k in ("barrel", "transition", "endcap"))
        print(line)
