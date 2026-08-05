#!/usr/bin/env python3
"""Length-vs-eff frontier over every scored run in ex_trim.

Ranks all PASSING configs (every M19 hard floor held, incl. d510 >= 71/285) by total
nhitOT gain over the flagship, and prints the exchange rate in milli-hits per milli-eff.
"""
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xt_tab as T  # noqa: E402

D = T.D
FLAG = "g_flag"


def main():
    ref = T.row(FLAG)
    rb = ref["m"]["mean_nhitOT_barrel"]
    rt = ref["m"]["mean_nhitOT_transition"]
    re_ = ref["m"]["mean_nhitOT_endcap"]
    rf = ref["m"]["eff_overall_incut"]

    tags = sorted(os.path.basename(p)[:-5] for p in glob.glob(D + "*.json"))
    rows = []
    for t in tags:
        if t in (FLAG, "g_def"):
            continue
        r = T.row(t)
        if r is None:
            continue
        m = r["m"]
        db = m["mean_nhitOT_barrel"] - rb
        dt = m["mean_nhitOT_transition"] - rt
        de = m["mean_nhitOT_endcap"] - re_
        deff = (m["eff_overall_incut"] - rf) * 1000.0
        rows.append((db + dt + de, db, dt, de, deff, r))

    rows.sort(key=lambda x: -x[0])
    hdr = ("%-20s %8s %8s %8s %8s | %9s | %9s | %8s %8s %s" %
           ("tag", "dnhB", "dnhT", "dnhE", "dTOT", "d_eff(m)", "hits/meff",
            "d_dup", "d_fake", "status"))
    print(hdr)
    print("-" * len(hdr))
    for tot, db, dt, de, deff, r in rows:
        m = r["m"]
        rate = (tot * 1000.0 / -deff) if deff < -1e-9 else float("inf")
        ok = "PASS" if not r["fl"] else "fail:" + ",".join(r["fl"])
        print("%-20s %+8.3f %+8.3f %+8.3f %+8.3f | %+9.3f | %9.1f | %+8.5f %+8.5f %s"
              % (r["tag"], db, dt, de, tot, deff, rate,
                 m["dup_overall_incut"] - ref["m"]["dup_overall_incut"],
                 m["fake_overall_incut"] - ref["m"]["fake_overall_incut"], ok))
    print("-" * len(hdr))
    print("PASSING configs with net POSITIVE total length gain, best first:")
    good = [x for x in rows if not x[5]["fl"] and x[0] > 0]
    if not good:
        print("  (none)")
    for tot, db, dt, de, deff, r in good:
        print("  %-20s dTOT=%+.3f  d_eff=%+.3f milli  nhitOT=%.3f/%.3f/%.3f  d510=%.0f"
              % (r["tag"], tot, deff, r["m"]["mean_nhitOT_barrel"],
                 r["m"]["mean_nhitOT_transition"], r["m"]["mean_nhitOT_endcap"], r["dn"]))


if __name__ == "__main__":
    main()
