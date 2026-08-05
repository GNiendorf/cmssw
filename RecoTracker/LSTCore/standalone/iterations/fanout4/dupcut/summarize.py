#!/usr/bin/env python3
"""M16 verdict summarizer: one row per tag, aggregate + per-class verdict."""
import json, os, sys

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout3/m16"
BANDS = ["prompt vxy<2.5", "vxy[0,1)", "vxy[1,5)", "vxy[5,10)", "vxy[10,30)"]
CLASSES = ["pT5-class ALL (7)", "pT3-class ALL (5)", "pLS-class ALL (8)", "pixel-class ALL (7+5+8)"]

print("%-14s %6s %6s %6s %6s %6s | %6s %6s %6s %6s | %6s %6s" %
      ("tag", "eff", "vx01", "vx15", "vx510", "vx1030", "dx01", "dx15", "dx510", "dx1030", "fake", "dup"))
print("%-14s %6.4f %6.4f %6.4f %6.4f %6.4f | %6.4f %6.4f %6.4f %6.4f | %6.4f %6.4f" %
      ("ANCHOR f2", .8168, .8501, .7972, .7129, .6912, .8426, .5397, .2526, .0235, .0480, .0603))
for tag in sys.argv[1:]:
    j = os.path.join(P, "ab_%s.json" % tag)
    if not os.path.exists(j):
        print("%-14s MISSING" % tag); continue
    m = json.load(open(j))["metrics"]
    g = lambda k: m[k]["proto"]
    print("%-14s %6.4f %6.4f %6.4f %6.4f %6.4f | %6.4f %6.4f %6.4f %6.4f | %6.4f %6.4f" %
          (tag, g("eff_overall_incut"), g("eff_vxy_0_1"), g("eff_vxy_1_5"), g("eff_vxy_5_10"),
           g("eff_vxy_10_30"), g("eff_dxy_0_1"), g("eff_dxy_1_5"), g("eff_dxy_5_10"),
           g("eff_dxy_10_30"), g("fake_overall_incut"), g("dup_overall_incut")))

print()
print("%-14s %-20s %-5s %-14s %8s %8s %8s | %s" %
      ("tag", "class", "gate", "worst band", "worstD", "FR", "DR", "  ".join("%13s" % b for b in BANDS)))
for tag in sys.argv[1:]:
    j = os.path.join(P, "ct_%s.json" % tag)
    if not os.path.exists(j):
        continue
    d = json.load(open(j))
    t = d["m16_table"]
    pm = {k: v["proto"] for k, v in t.items()}
    bm = {k: v["base"] for k, v in t.items()}
    for cl in CLASSES:
        cells, worst, wb = [], None, None
        for b in BANDS:
            pv, bv = pm[cl]["eff " + b], bm[cl]["eff " + b]
            if pv is None or bv is None:
                cells.append("%13s" % "n/a"); continue
            dd = pv - bv
            cells.append("%13s" % ("%.4f%+.4f" % (pv, dd)))
            if worst is None or dd < worst:
                worst, wb = dd, b
        print("%-14s %-20s %-5s %-14s %+8.4f %8.4f %8.4f | %s" %
              (tag, cl, "PASS" if (worst is not None and worst >= 0) else "FAIL", wb or "-",
               worst or 0.0, pm[cl]["FR"] or 0.0, pm[cl]["DR"] or 0.0, "  ".join(cells)))
    print()
