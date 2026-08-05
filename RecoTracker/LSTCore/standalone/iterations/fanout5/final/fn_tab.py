"""M19 FINAL scoreboard. Floors + per-band dup (incl. the maintainer window) + nhitOT +
exact d510/d15 track counts + timing. Usage: python3 fn_tab.py <tag> [<tag> ...]"""
import sys, os, json, re

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/final"

FLOOR = dict(eff=.8127, v15=.8022, v510=.7267, v1030=.7170, d15=.5613, fake=.0480)
# denominators, from the flagship hists (fixed across configs: sim-side)
DEN = dict(eff=22784, v15=1415, v510=634, v1030=1224, d15=932, d510=285)


def load(tag):
    f = "%s/f_%s.json" % (P, tag)
    if not os.path.exists(f):
        return None
    m = json.load(open(f))["metrics"]
    g = lambda k: m[k]["proto"]
    d = dict(
        eff=g("eff_overall_incut"), v01=g("eff_vxy_0_1"), v15=g("eff_vxy_1_5"),
        v510=g("eff_vxy_5_10"), v1030=g("eff_vxy_10_30"),
        d15=g("eff_dxy_1_5"), d510=g("eff_dxy_5_10"), d1030=g("eff_dxy_10_30"),
        dup=g("dup_overall_incut"), dupB=g("dup_barrel"), dupT=g("dup_transition"),
        dupE=g("dup_endcap"), fake=g("fake_overall_incut"),
        nhb=g("mean_nhitOT_barrel"), nht=g("mean_nhitOT_transition"),
        nhe=g("mean_nhitOT_endcap"), ntc=g("n_tc"))
    d["d510n"] = int(round(d["d510"] * DEN["d510"]))
    d["d15n"] = int(round(d["d15"] * DEN["d15"]))
    d["effn"] = int(round(d["eff"] * DEN["eff"]))
    d["v510n"] = int(round(d["v510"] * DEN["v510"]))
    d["v1030n"] = int(round(d["v1030"] * DEN["v1030"]))
    d["v15n"] = int(round(d["v15"] * DEN["v15"]))
    lg = "%s/f_%s.log" % (P, tag)
    tot, n = 0.0, 0
    if os.path.exists(lg):
        for ln in open(lg, errors="ignore"):
            mm = re.search(r"infer=([\d.]+) weld=([\d.]+) attach=([\d.]+) arb=([\d.]+) fill=([\d.]+) ms", ln)
            if mm:
                tot += sum(float(x) for x in mm.groups()); n += 1
    d["ms"] = tot / n if n else float("nan")
    d["nev"] = n
    return d


def fails(d):
    bad = []
    if d["eff"] < FLOOR["eff"] - 1e-9: bad.append("EFF")
    if d["v15"] < FLOOR["v15"] - 1e-9: bad.append("V15")
    if d["v510"] < FLOOR["v510"] - 1e-9: bad.append("V510")
    if d["v1030"] < FLOOR["v1030"] - 1e-9: bad.append("V1030")
    if d["d15"] < FLOOR["d15"] - 1e-9: bad.append("D15")
    if d["d510n"] < 71: bad.append("D510")
    if d["fake"] > FLOOR["fake"] + 1e-9: bad.append("FAKE")
    return bad


def main(tags):
    print("%-16s %8s %8s %8s %8s %8s %5s %8s %8s | %7s %7s %7s | %6s %6s %6s | %7s %6s %s"
          % ("tag", "eff", "v15", "v510", "v1030", "d15", "d510", "fake", "dup",
             "dupB", "dupT", "dupE", "nhb", "nht", "nhe", "nTC", "ms/ev", "FLOORS"))
    print("-" * 190)
    for t in tags:
        d = load(t)
        if d is None:
            print("%-16s  (no json)" % t); continue
        bad = fails(d)
        print("%-16s %8.5f %8.5f %8.5f %8.5f %8.5f %5d %8.5f %8.5f | %7.5f %7.5f %7.5f | %6.3f %6.3f %6.3f | %7d %6.1f %s"
              % (t, d["eff"], d["v15"], d["v510"], d["v1030"], d["d15"], d["d510n"],
                 d["fake"], d["dup"], d["dupB"], d["dupT"], d["dupE"],
                 d["nhb"], d["nht"], d["nhe"], d["ntc"], d["ms"],
                 ("FAIL:" + ",".join(bad)) if bad else "pass"))
    print()
    print("track counts (numerator / denominator):")
    print("%-16s %13s %11s %10s %11s %9s %9s" % ("tag", "eff", "v15", "v510", "v1030", "d15", "d510"))
    for t in tags:
        d = load(t)
        if d is None: continue
        print("%-16s %6d/%-6d %5d/%-5d %4d/%-5d %5d/%-5d %4d/%-4d %3d/%-4d"
              % (t, d["effn"], DEN["eff"], d["v15n"], DEN["v15"], d["v510n"], DEN["v510"],
                 d["v1030n"], DEN["v1030"], d["d15n"], DEN["d15"], d["d510n"], DEN["d510"]))


if __name__ == "__main__":
    main(sys.argv[1:])
