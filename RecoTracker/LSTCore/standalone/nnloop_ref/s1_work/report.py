#!/usr/bin/env python3
"""S1: the gate table -- every judge field, arm vs the recorded shipped value, with the
binomial sigma of the arm's own count so a small move can be called by size.

  report.py <armprefix>
"""
import json
import math
import os
import subprocess
import sys

O = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
R = O + "/nnloop_ref/s1_work/runs"
JUDGE = O + "/d3_ref/pu_judge.py"

EFF_N = {  # judge field -> its denominator-count field
    "eff_overall_incut": "n_sim_denom", "eff_barrel": "n_eff_barrel",
    "eff_transition": "n_eff_transition", "eff_endcap": "n_eff_endcap",
    "eff_vxy_0_1": "n_eff_vxy_0_1", "eff_vxy_1_5": "n_eff_vxy_1_5",
    "eff_vxy_5_10": "n_eff_vxy_5_10", "eff_vxy_10_30": "n_eff_vxy_10_30",
    "eff_dxy_0_1": "n_eff_dxy_0_1", "eff_dxy_1_5": "n_eff_dxy_1_5",
    "eff_dxy_5_10": "n_eff_dxy_5_10", "eff_dxy_10_30": "n_eff_dxy_10_30",
    "fake_overall_incut": "n_tc", "dup_overall_incut": "n_tc",
    "fake_barrel": "n_tc", "dup_barrel": "n_tc", "fake_transition": "n_tc",
    "dup_transition": "n_tc", "fake_endcap": "n_tc", "dup_endcap": "n_tc",
}
ORDER = ["eff_overall_incut", "eff_barrel", "eff_transition", "eff_endcap",
         "eff_vxy_0_1", "eff_vxy_1_5", "eff_vxy_5_10", "eff_vxy_10_30",
         "eff_dxy_0_1", "eff_dxy_1_5", "eff_dxy_5_10", "eff_dxy_10_30",
         "dup_overall_incut", "fake_overall_incut", "dup_barrel", "fake_barrel",
         "dup_transition", "fake_transition", "dup_endcap", "fake_endcap",
         "n_tc", "n_tc_t4cl", "n_sim_denom", "n_evt"]


def judge(root, cache):
    if os.path.exists(cache):
        return json.load(open(cache))
    subprocess.run(["python3", JUDGE, root, "--json", cache], check=True,
                   stdout=subprocess.DEVNULL)
    return json.load(open(cache))


def last_json_line(p):
    return json.loads(open(p).read().strip().split("\n")[-1])


def table(name, ship, arm):
    print("\n=== %s" % name)
    print("  %-22s %12s %12s %12s %9s %s" % ("field", "shipped", "arm", "delta", "sigma", "verdict"))
    for k in ORDER:
        if k not in ship or k not in arm:
            continue
        s, a = ship[k], arm[k]
        if isinstance(s, int) or k.startswith("n_"):
            print("  %-22s %12d %12d %+12d %9s %s" % (k, s, a, a - s, "-",
                  "" if a == s else "%+.3f%%" % (100.0 * (a - s) / max(s, 1))))
            continue
        n = arm.get(EFF_N.get(k, ""), 0)
        sig = math.sqrt(max(a, 1e-9) * (1 - a) / n) if n else float("nan")
        d = a - s
        v = ""
        if sig == sig and sig > 0:
            z = d / sig
            v = "%+.1f sig" % z
            if abs(z) < 1:
                v += " (noise)"
        print("  %-22s %12.6f %12.6f %+12.6f %9.6f %s" % (k, s, a, d, sig, v))


def main():
    P = sys.argv[1]
    # PU200 tune + holdout: recorded shipped judges
    table("PU200 TUNE event_1000 (vs ship_ref/B2_shipped.judge)",
          last_json_line(O + "/ship_ref/B2_shipped.judge"),
          json.load(open("%s/%s_tune.json" % (R, P))))
    table("PU200 HOLDOUT event_2000 (vs holdout_ref/Chains.judge)",
          last_json_line(O + "/holdout_ref/Chains.judge"),
          json.load(open("%s/%s_h2000.json" % (R, P))))
    for ev in (3000, 4000, 5000, 6000, 7000):
        table("PU200 HOLDOUT event_%d (vs a5_ref/big/ours_%d)" % (ev, ev),
              judge("%s/a5_ref/big/ours_%d.root" % (O, ev),
                    "%s/SHIP_ours_%d.json" % (R, ev)),
              json.load(open("%s/%s_h%d.json" % (R, P, ev))))
    table("cube50 5000 evt (vs e1_ref/cube/B2_cube50.root)",
          judge(O + "/e1_ref/cube/B2_cube50.root", R + "/SHIP_cube50.json"),
          json.load(open("%s/%s_cube50.json" % (R, P))))
    table("cube50_highPt 5000 evt (vs e1_ref/cube/B2_cube50_highPt.root)",
          judge(O + "/e1_ref/cube/B2_cube50_highPt.root", R + "/SHIP_cubehi.json"),
          json.load(open("%s/%s_cubehi.json" % (R, P))))


if __name__ == "__main__":
    main()
