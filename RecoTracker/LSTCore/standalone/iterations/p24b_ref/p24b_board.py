#!/usr/bin/env python3
"""P2.4b-1 TASK 6 scoreboard: replacement legs against the CURRENT state (b0).

Reads the compare_ab.py JSONs produced by run_board.sh. The `proto` field of each JSON is that
leg's own value (compare_ab.py's `base` column is LST's flag-OFF baseline and is identical in
every leg, so it is carried once for reference)."""
import json
import sys
import os

ROWS = [
    ("eff_overall_incut", "eff (pt>0.9)", 4, +1),
    ("eff_vxy_0_1", "eff vxy [0,1)", 4, +1),
    ("eff_vxy_1_5", "eff vxy [1,5)", 4, +1),
    ("eff_vxy_5_10", "eff vxy [5,10)", 4, +1),
    ("eff_vxy_10_30", "eff vxy [10,30)", 4, +1),
    ("eff_dxy_0_1", "eff dxy [0,1)", 4, +1),
    ("eff_dxy_1_5", "eff dxy [1,5)", 4, +1),
    ("eff_dxy_5_10", "eff dxy [5,10)", 4, +1),
    ("eff_dxy_10_30", "eff dxy [10,30)", 4, +1),
    ("eff_barrel", "eff barrel", 4, +1),
    ("eff_transition", "eff transition", 4, +1),
    ("eff_endcap", "eff endcap", 4, +1),
    ("dup_overall_incut", "dup (pt>0.9)", 4, -1),
    ("dup_barrel", "dup barrel", 4, -1),
    ("dup_transition", "dup transition", 4, -1),
    ("dup_endcap", "dup endcap", 4, -1),
    ("fake_overall_incut", "fake (pt>0.9)", 4, -1),
    ("fake_barrel", "fake barrel", 4, -1),
    ("fake_transition", "fake transition", 4, -1),
    ("fake_endcap", "fake endcap", 4, -1),
    ("mean_nhitOT", "mean nhitOT", 3, +1),
    ("mean_nhitOT_barrel", "mean nhitOT barrel", 3, +1),
    ("mean_nhitOT_transition", "mean nhitOT trans", 3, +1),
    ("mean_nhitOT_endcap", "mean nhitOT endcap", 3, +1),
    ("n_tc", "n TC (fr denom)", 0, 0),
]


def load(d, tag):
    p = os.path.join(d, f"bd_{tag}.json")
    if not os.path.exists(p):
        return None
    j = json.load(open(p))
    m = j["metrics"] if "metrics" in j else j
    return {k: (v["proto"] if isinstance(v, dict) else v) for k, v in m.items()}


def main(d, tags):
    legs = [(t, load(d, t)) for t in tags]
    legs = [(t, m) for t, m in legs if m is not None]
    if not legs:
        print("no JSONs found")
        return
    base = legs[0][1]
    lstref = json.load(open(os.path.join(d, f"bd_{legs[0][0]}.json")))
    lstm = lstref["metrics"] if "metrics" in lstref else lstref

    w = 11
    print(f"P2.4b-1 pT3-class REPLACEMENT scoreboard -- 300 PU200RelVal events, CPU, -s 32")
    print(f"baseline column = {legs[0][0]} (chain tracking on, LST's own pT3 rows CARRIED)")
    print(f"'LST' column    = LST flag-OFF baseline, carried by compare_ab.py for reference")
    print()
    hdr = f"{'metric':<22s}{'LST':>{w}s}" + "".join(f"{t:>{w}s}" for t, _ in legs)
    print(hdr)
    print(f"{'':<22s}{'':>{w}s}" + "".join(f"{'(delta vs ' + legs[0][0] + ')':>{w}s}" if i else f"{'BASE':>{w}s}"
                                           for i, (t, _) in enumerate(legs)))
    print("-" * len(hdr))
    for key, label, nd, sgn in ROWS:
        lv = lstm.get(key, {}).get("base") if isinstance(lstm.get(key), dict) else None
        line = f"{label:<22s}"
        line += f"{lv:>{w}.{nd}f}" if isinstance(lv, (int, float)) else f"{'n/a':>{w}s}"
        for i, (t, m) in enumerate(legs):
            v = m.get(key)
            if v is None:
                line += f"{'n/a':>{w}s}"
            elif i == 0:
                line += f"{v:>{w}.{nd}f}"
            else:
                line += f"{v - base[key]:>+{w}.{nd}f}"
        print(line)
    print()
    print("absolute values of every leg:")
    hdr2 = f"{'metric':<22s}" + "".join(f"{t:>{w}s}" for t, _ in legs)
    print(hdr2)
    print("-" * len(hdr2))
    for key, label, nd, sgn in ROWS:
        line = f"{label:<22s}"
        for t, m in legs:
            v = m.get(key)
            line += f"{v:>{w}.{nd}f}" if v is not None else f"{'n/a':>{w}s}"
        print(line)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
