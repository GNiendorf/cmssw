#!/usr/bin/env python3
"""Compact A8 scoreboard: one row per config json, judged vs the LST-baseline floors."""
import json, sys, os

KEYS = [("eff_overall_incut", "eff"), ("eff_vxy_0_1", "vxy01"), ("eff_vxy_1_5", "vxy15"),
        ("eff_vxy_5_10", "vxy510"), ("eff_vxy_10_30", "vxy1030"), ("eff_dxy_0_1", "dxy01"),
        ("eff_dxy_1_5", "dxy15"), ("eff_dxy_5_10", "dxy510"), ("eff_dxy_10_30", "dxy1030"),
        ("eff_barrel", "bar"), ("eff_transition", "tra"), ("eff_endcap", "end")]
LEN = [("mean_nhitOT", "L"), ("mean_nhitOT_barrel", "Lb"), ("mean_nhitOT_transition", "Lt"),
       ("mean_nhitOT_endcap", "Le")]
TOL = 0.005

rows = []
for p in sys.argv[1:]:
    m = json.load(open(p))["metrics"]
    tag = os.path.basename(p).replace("ab_", "").replace(".json", "")
    nfail = sum(1 for k, _ in KEYS if m[k]["proto"] < m[k]["base"] - TOL)
    lfail = sum(1 for k, _ in LEN if m[k]["proto"] < m[k]["base"])
    rows.append((tag, m, nfail, lfail))

hdr = f"{'tag':<12}" + "".join(f"{s:>8}" for _, s in KEYS) + f"{'fake':>8}{'dup':>8}{'len':>7}{'effF':>5}{'lenF':>5}"
print(hdr)
print("-" * len(hdr))
for tag, m, nfail, lfail in rows:
    line = f"{tag:<12}" + "".join(f"{m[k]['proto']:>8.4f}" for k, _ in KEYS)
    line += f"{m['fake_overall_incut']['proto']:>8.4f}{m['dup_overall_incut']['proto']:>8.4f}"
    line += f"{m['mean_nhitOT']['proto']:>7.2f}{nfail:>5}{lfail:>5}"
    print(line)
b = rows[0][1]
print(f"{'BASE':<12}" + "".join(f"{b[k]['base']:>8.4f}" for k, _ in KEYS) +
      f"{b['fake_overall_incut']['base']:>8.4f}{b['dup_overall_incut']['base']:>8.4f}"
      f"{b['mean_nhitOT']['base']:>7.2f}")
print(f"{'FLOOR(-.005)':<12}" + "".join(f"{b[k]['base']-TOL:>8.4f}" for k, _ in KEYS))
