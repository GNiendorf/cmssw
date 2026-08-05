#!/usr/bin/env python3
"""m16r_profile.py -- denominator validation + LOST-set profile for an M16 A/B (READ-ONLY).

Denominators (verbatim from efficiency/src/performance.cc):
  OVERALL (the compare_ab "eff overall (pt>0.9)" / per-eta sets):
      pt>0.9, |eta|<4.5, |vz|<30, vxy<2.5, q!=0
  VXY/DXY bands ("N minus dxy cut"):
      pt>0.9, |eta|<4.5, |vz|<30, q!=0     (no vxy cut)
"""
import sys

import numpy as np

SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-"
           "LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")


def load(tag):
    return np.load(f"{SCRATCH}/m16r_sims_{tag}.npy")


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else "ctl_noatt"
    a = load(tag)
    band = (np.abs(a["vz"]) < 30) & (a["q"] != 0)
    over = band & (a["vxy"] < 2.5)
    print(f"=== {tag} denominator validation ===")
    print(f"OVERALL denom N={over.sum()}  proto eff={a['anyTC'][over].mean():.4f}"
          f"  base eff={a['baseTC'][over].mean():.4f}")
    for lab, m in [("vxy[0,1)", a["vxy"] < 1), ("vxy[1,5)", (a["vxy"] >= 1) & (a["vxy"] < 5)),
                   ("vxy[5,10)", (a["vxy"] >= 5) & (a["vxy"] < 10)),
                   ("vxy[10,30)", (a["vxy"] >= 10) & (a["vxy"] < 30)),
                   ("dxy[0,1)", a["dxy"] < 1), ("dxy[1,5)", (a["dxy"] >= 1) & (a["dxy"] < 5)),
                   ("dxy[5,10)", (a["dxy"] >= 5) & (a["dxy"] < 10)),
                   ("dxy[10,30)", (a["dxy"] >= 10) & (a["dxy"] < 30))]:
        m = m & band
        print(f"  {lab:12s} N={m.sum():6d} proto={a['anyTC'][m].mean():.4f}"
              f" base={a['baseTC'][m].mean():.4f}")

    for name, den in [("OVERALL", over), ("BAND(all vxy)", band)]:
        lost = den & a["baseTC"] & ~a["anyTC"]
        gain = den & ~a["baseTC"] & a["anyTC"]
        print(f"\n=== {name}: LOST={lost.sum()} GAINED={gain.sum()} net={gain.sum()-lost.sum()}")
        L = a[lost]
        print("  baseline delivery type of the LOST sims (>0.75 rows, non-exclusive):")
        print(f"    pT5(7)={L['base7'].sum():5d}  pT3(5)={L['base5'].sum():5d}"
              f"  pLS(8)={L['base8'].sum():5d}  OT-T5/T4(4/9)={L['baseOT'].sum():5d}")
        print(f"    pT5-only (no pT3/pLS/OT row)={(L['base7'] & ~L['base5'] & ~L['base8'] & ~L['baseOT']).sum()}")
        print("  sim_tcIdx best type:", {int(t): int((L["baseType"] == t).sum())
                                         for t in np.unique(L["baseType"])})
        for lab, m in [("pt 0.9-1.5", (L["pt"] < 1.5)), ("pt 1.5-3", (L["pt"] >= 1.5) & (L["pt"] < 3)),
                       ("pt 3-10", (L["pt"] >= 3) & (L["pt"] < 10)), ("pt >10", L["pt"] >= 10)]:
            print(f"    {lab:12s} {m.sum():5d}")
        for lab, m in [("barrel<1.1", np.abs(L["eta"]) < 1.1),
                       ("trans1.1-1.7", (np.abs(L["eta"]) >= 1.1) & (np.abs(L["eta"]) < 1.7)),
                       ("endcap>1.7", np.abs(L["eta"]) >= 1.7)]:
            print(f"    {lab:12s} {m.sum():5d}")
        for lab, m in [("vxy<1", L["vxy"] < 1), ("vxy1-5", (L["vxy"] >= 1) & (L["vxy"] < 5)),
                       ("vxy5-10", (L["vxy"] >= 5) & (L["vxy"] < 10)),
                       ("vxy>=10", L["vxy"] >= 10)]:
            print(f"    {lab:12s} {m.sum():5d}")
        # chain slice covered it at all? (chainTC true means some chain TC matched >0.75)
        print(f"    of LOST, pixTC(kept pixel row) matched = {L['pixTC'].sum()}"
              f" ; chainTC matched = {L['chainTC'].sum()}  (both must be 0 by construction)")


if __name__ == "__main__":
    main()
