#!/usr/bin/env python3
"""S2: the round-level deliverable table -- N arms side by side against a reference judge,
with the binomial sigma of each arm's own count so a small move can be sized.

table.py <ref.json> <name=arm.json> [<name=arm.json> ...]
"""
import json
import math
import sys

EFF_N = {"eff_overall_incut": "n_sim_denom", "eff_barrel": "n_eff_barrel",
         "eff_transition": "n_eff_transition", "eff_endcap": "n_eff_endcap",
         "eff_vxy_0_1": "n_eff_vxy_0_1", "eff_vxy_1_5": "n_eff_vxy_1_5",
         "eff_vxy_5_10": "n_eff_vxy_5_10", "eff_vxy_10_30": "n_eff_vxy_10_30",
         "eff_dxy_0_1": "n_eff_dxy_0_1", "eff_dxy_1_5": "n_eff_dxy_1_5",
         "eff_dxy_5_10": "n_eff_dxy_5_10", "eff_dxy_10_30": "n_eff_dxy_10_30",
         "fake_overall_incut": "n_tc", "dup_overall_incut": "n_tc",
         "fake_barrel": "n_tc", "dup_barrel": "n_tc", "fake_transition": "n_tc",
         "dup_transition": "n_tc", "fake_endcap": "n_tc", "dup_endcap": "n_tc"}
ORDER = ["eff_overall_incut", "eff_barrel", "eff_transition", "eff_endcap",
         "eff_vxy_1_5", "eff_vxy_5_10", "eff_vxy_10_30",
         "eff_dxy_0_1", "eff_dxy_1_5", "eff_dxy_5_10", "eff_dxy_10_30",
         "dup_overall_incut", "dup_barrel", "dup_transition", "dup_endcap",
         "fake_overall_incut", "fake_barrel", "fake_transition", "fake_endcap",
         "n_tc", "n_tc_t4cl", "n_evt"]


def load(p):
    """Accepts either the pretty-printed --json file or the .judge transcript (whose LAST line
    is the single-line json)."""
    txt = open(p).read()
    try:
        return json.loads(txt)
    except Exception:
        pass
    for ln in reversed(txt.strip().split("\n")):
        ln = ln.strip()
        if ln.startswith("{"):
            return json.loads(ln)
    raise ValueError(p)


def main():
    ref = load(sys.argv[1])
    arms = []
    for a in sys.argv[2:]:
        nm, p = a.split("=", 1)
        arms.append((nm, load(p)))
    w = 21
    print("%-21s %11s" % ("field", "REFERENCE") + "".join("%*s" % (w, n) for n, _ in arms))
    for k in ORDER:
        if k not in ref:
            continue
        s = ref[k]
        if k.startswith("n_"):
            row = "%-21s %11d" % (k, s)
            for _, a in arms:
                row += "%*s" % (w, "%d (%+.2f%%)" % (a[k], 100.0 * (a[k] - s) / max(s, 1)))
            print(row)
            continue
        row = "%-21s %11.5f" % (k, s)
        for _, a in arms:
            n = a.get(EFF_N.get(k, ""), 0)
            v = a[k]
            sig = math.sqrt(max(v, 1e-9) * (1 - v) / n) if n else float("nan")
            nsig = (v - s) / sig if sig and sig == sig and sig > 0 else float("nan")
            row += "%*s" % (w, "%+.5f (%+.1fs)" % (v - s, nsig))
        print(row)


if __name__ == "__main__":
    main()
