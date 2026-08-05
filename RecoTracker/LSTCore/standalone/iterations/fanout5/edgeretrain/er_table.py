#!/usr/bin/env python3
"""M19 scoreboard extractor: per-band values + raw numerators for the dxy bands.

  er_table.py <tag> [<tag> ...]
reads er_<tag>_hists.root (+ er_<tag>.json for the ratios, er_<tag>.log for timing)
and prints one row per tag with every M19-reported quantity, plus the dxy[5,10) and
dxy[10,30) NUMERATOR track counts (the floors are stated as counts, not rates).
"""
import json
import os
import re
import sys

import ROOT

PDIR = os.path.dirname(os.path.abspath(__file__))
EF = "Root__TC_base_0_0_ef_"


def band_counts(path, var, lo, hi):
    f = ROOT.TFile.Open(path)
    hn = f.Get(EF + "numer_" + var)
    hd = f.Get(EF + "denom_" + var)
    n = d = 0.0
    for i in range(1, hn.GetNbinsX() + 1):
        c = abs(hn.GetBinCenter(i))
        if lo <= c and (hi is None or c < hi):
            n += hn.GetBinContent(i)
            d += hd.GetBinContent(i)
    f.Close()
    return n, d


def timing(path):
    """(wall ms/evt from the er_run.sh stamp, mean instrumented stage ms/evt)."""
    if not os.path.exists(path):
        return None, None
    txt = open(path, errors="ignore").read()
    w = re.findall(r"WALL_MS_PER_EVT ([\d.]+)", txt)
    wall = float(w[-1]) if w else None
    st = re.findall(r"infer=([\d.]+) weld=([\d.]+) attach=([\d.]+) arb=([\d.]+) fill=([\d.]+) ms", txt)
    stage = None
    if st:
        stage = sum(sum(float(x) for x in row) for row in st) / len(st)
    return wall, stage


KEYS = [("eff_overall_incut", "eff"), ("eff_vxy_0_1", "vxy01"), ("eff_vxy_1_5", "v15"),
        ("eff_vxy_5_10", "v510"), ("eff_vxy_10_30", "v1030"), ("eff_dxy_1_5", "d15"),
        ("eff_dxy_5_10", "d510"), ("eff_dxy_10_30", "d1030"),
        ("fake_overall_incut", "fake"), ("dup_overall_incut", "dup"),
        ("dup_barrel", "dupB"), ("dup_transition", "dupT"), ("dup_endcap", "dupE"),
        ("mean_nhitOT_barrel", "nhB"), ("mean_nhitOT_transition", "nhT"),
        ("mean_nhitOT_endcap", "nhE"), ("n_tc", "nTC")]

FLOORS = {"eff": 0.8127, "v15": 0.8022, "v510": 0.7267, "v1030": 0.7170,
          "d15": 0.5613, "fake": None}


def main():
    tags = sys.argv[1:]
    hdr = f"{'tag':>22}" + "".join(f"{lab:>9}" for _, lab in KEYS)
    hdr += f"{'d510N':>7}{'d1030N':>8}{'wall/ev':>9}{'stg/ev':>8}"
    print(hdr)
    print("-" * len(hdr))
    for t in tags:
        jp = f"{PDIR}/er_{t}.json"
        hp = f"{PDIR}/er_{t}_hists.root"
        if not os.path.exists(jp):
            print(f"{t:>22}  MISSING {jp}")
            continue
        m = json.load(open(jp))["metrics"]
        row = f"{t:>22}"
        for k, _ in KEYS:
            v = m[k]["proto"]
            row += f"{v:>9.4f}" if abs(v) < 100 else f"{v:>9.0f}"
        n510, _ = band_counts(hp, "dxy", 5.0, 10.0)
        n1030, _ = band_counts(hp, "dxy", 10.0, 30.0)
        wall, stg = timing(f"{PDIR}/er_{t}.log")
        row += f"{n510:>7.0f}{n1030:>8.0f}"
        row += (f"{wall:>9.1f}" if wall else f"{'-':>9}") + (f"{stg:>8.1f}" if stg else f"{'-':>8}")
        print(row)
    print("\nFLOORS: eff>=.8127 v15>=.8022 v510>=.7267 v1030>=.7170 d15>=.5613 "
          "fake<=.0480 ; d510N must hold 71 (denom 285); d1030N baseline 12 (LST 22, denom 468)")


if __name__ == "__main__":
    main()
