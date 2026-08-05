#!/usr/bin/env python3
"""cw_table.py -- tabulate all cheapwins A/B jsons vs ctl_noatt (base) and LST."""
import json, os, sys, glob

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins"
KEYS = ["eff_overall_incut", "eff_vxy_0_1", "eff_vxy_1_5", "eff_vxy_5_10", "eff_vxy_10_30",
        "eff_dxy_1_5", "eff_dxy_5_10", "fake_overall_incut", "dup_overall_incut", "n_tc"]
FLOORS = {"eff_vxy_1_5": .7832, "eff_vxy_5_10": .7109, "eff_vxy_10_30": .6941,
          "eff_dxy_1_5": .5398, "eff_dxy_5_10": .2471}
# ceilings are the ctl_noatt bars quoted to 4 dp; anything that still ROUNDS to the
# quoted value is at the bar, so compare against the round-to-4dp upper edge.
CEIL = {"fake_overall_incut": .05365, "dup_overall_incut": .06435}

order = sys.argv[1:] if len(sys.argv) > 1 else None
tags = order or sorted(os.path.basename(f)[3:-5] for f in glob.glob(P + "/ab_*.json"))
rows = {}
for t in tags:
    p = f"{P}/ab_{t}.json"
    if not os.path.exists(p):
        continue
    m = json.load(open(p))["metrics"]
    rows[t] = {k: m[k]["proto"] for k in KEYS}
    rows[t]["_lst"] = {k: m[k]["base"] for k in KEYS}

if "base" not in rows:
    print("no base row"); sys.exit(1)
B = rows["base"]
L = B["_lst"]
hdr = ("%-11s %7s %7s | %6s %6s %6s %6s %6s | %7s %7s | %7s  FLAGS"
       % ("tag", "eff", "vxy01", "v1-5", "v5-10", "v10-30", "d1-5", "d5-10", "fake", "dup", "nTC"))
print(hdr); print("-" * len(hdr))
for t in tags:
    if t not in rows:
        continue
    r = rows[t]
    bad = []
    for k, v in FLOORS.items():
        if r[k] < v - 1e-9:
            bad.append(k.replace("eff_", ""))
    for k, v in CEIL.items():
        if r[k] > v + 1e-9:
            bad.append(k.split("_")[0].upper())
    print("%-11s %7.4f %7.4f | %6.4f %6.4f %6.4f %6.4f %6.4f | %7.4f %7.4f | %7d  %s"
          % (t, r["eff_overall_incut"], r["eff_vxy_0_1"], r["eff_vxy_1_5"], r["eff_vxy_5_10"],
             r["eff_vxy_10_30"], r["eff_dxy_1_5"], r["eff_dxy_5_10"],
             r["fake_overall_incut"], r["dup_overall_incut"], int(r["n_tc"]),
             ("VIOLATES " + ",".join(bad)) if bad else "ok"))
print()
print("deltas vs base(ctl_noatt) [d_eff d_vxy01 d_fake d_dup]  and prompt sims recovered (dvxy01*21877)")
for t in tags:
    if t not in rows or t == "base":
        continue
    r = rows[t]
    d = lambda k: r[k] - B[k]
    print("%-11s  deff %+8.5f  dvxy01 %+8.5f (%+6.1f sims)  dfake %+8.5f  ddup %+8.5f  dnTC %+7d"
          % (t, d("eff_overall_incut"), d("eff_vxy_0_1"), d("eff_vxy_0_1") * 21877,
             d("fake_overall_incut"), d("dup_overall_incut"), int(d("n_tc"))))
print()
print("LST bars: eff %.4f vxy01 %.4f fake %.4f dup %.4f" %
      (L["eff_overall_incut"], L["eff_vxy_0_1"], L["fake_overall_incut"], L["dup_overall_incut"]))
