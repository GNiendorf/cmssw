#!/usr/bin/env python3
"""Sim-track COUNTS behind the ratios, so trades can be stated in tracks.
Denominators are fixed by the sample and verified against ab_g0/base300."""
import json
import sys

# Verified by summing Root__T4_base_0_0_ef_denom_{vxy,dxy} bins in the hists file.
DEN = {
    "eff_overall_incut": 22784, "eff_vxy_0_1": 21137, "eff_vxy_1_5": 1415,
    "eff_vxy_5_10": 634, "eff_vxy_10_30": 1224, "eff_dxy_1_5": 932,
    "eff_dxy_5_10": 285, "eff_dxy_10_30": 468,
}
ORDER = ["eff_overall_incut", "eff_vxy_0_1", "eff_vxy_1_5", "eff_vxy_5_10",
         "eff_vxy_10_30", "eff_dxy_1_5", "eff_dxy_5_10", "eff_dxy_10_30"]
SHORT = {"eff_overall_incut": "eff", "eff_vxy_0_1": "vxy01", "eff_vxy_1_5": "v15",
         "eff_vxy_5_10": "v510", "eff_vxy_10_30": "v1030", "eff_dxy_1_5": "d15",
         "eff_dxy_5_10": "d510", "eff_dxy_10_30": "d1030"}

tags = sys.argv[1:]
print("%-12s " % "tag" + " ".join("%8s" % SHORT[k] for k in ORDER) + "   nTC")
for t in tags:
    m = json.load(open("cs2_%s.json" % t))["metrics"]
    cells = []
    for k in ORDER:
        n = m[k]["proto"] * DEN[k]
        cells.append("%8s" % ("%d/%d" % (round(n), DEN[k])))
    print("%-12s " % t + " ".join(cells) + "   %d" % m["n_tc"]["proto"])
