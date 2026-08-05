import sys, os, json, re

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_dupcc"

# M19 hard floors
FLOOR = dict(eff=.8127, v15=.8022, v510=.7267, v1030=.7170, d15=.5613, fake=.0480)
FL = dict(eff=.8132, v15=.8042, v510=.7287, v1030=.7190, d15=.5633, d510n=71,
          fake=.0474, dup=.0571, nhb=9.794, nht=9.698, nhe=3.417)

def load(tag):
    f = f"{P}/x_{tag}.json"
    if not os.path.exists(f):
        return None
    m = json.load(open(f))["metrics"]
    g = lambda k: m[k]["proto"]
    d = dict(
        eff=g("eff_overall_incut"), v01=g("eff_vxy_0_1"), v15=g("eff_vxy_1_5"),
        v510=g("eff_vxy_5_10"), v1030=g("eff_vxy_10_30"),
        d15=g("eff_dxy_1_5"), d510=g("eff_dxy_5_10"),
        dup=g("dup_overall_incut"), dupB=g("dup_barrel"), dupT=g("dup_transition"),
        dupE=g("dup_endcap"), fake=g("fake_overall_incut"),
        nhb=g("mean_nhitOT_barrel"), nht=g("mean_nhitOT_transition"), nhe=g("mean_nhitOT_endcap"),
        ntc=g("n_tc"))
    d["d510n"] = round(d["d510"] * 285)
    # timing: mean total ms/evt from the per-event log lines
    lg = f"{P}/x_{tag}.log"
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
    if d["eff"] < FLOOR["eff"] - 1e-9: bad.append("eff")
    if d["v15"] < FLOOR["v15"] - 1e-9: bad.append("v15")
    if d["v510"] < FLOOR["v510"] - 1e-9: bad.append("v510")
    if d["v1030"] < FLOOR["v1030"] - 1e-9: bad.append("v1030")
    if d["d15"] < FLOOR["d15"] - 1e-9: bad.append("d15")
    if d["d510n"] < 71: bad.append("d510")
    if d["fake"] > FLOOR["fake"] + 1e-9: bad.append("fake")
    return bad

hdr = ("%-12s %7s %7s %7s %7s %7s %5s %7s %7s | %7s %7s %7s %7s | %6s %6s %6s | %6s %5s  %s"
       % ("tag", "eff", "v15", "v510", "v1030", "d15", "d510", "fake", "dup",
          "dupB", "dupT", "dupE", "nTC", "nhb", "nht", "nhe", "ms/ev", "nev", "FLOORS"))
print(hdr); print("-" * len(hdr))
for tag in sys.argv[1:]:
    d = load(tag)
    if d is None:
        print("%-12s (pending)" % tag); continue
    bad = fails(d)
    print("%-12s %7.5f %7.5f %7.5f %7.5f %7.5f %5d %7.5f %7.5f | %7.5f %7.5f %7.5f %7d | %6.3f %6.3f %6.3f | %6.1f %5d  %s"
          % (tag, d["eff"], d["v15"], d["v510"], d["v1030"], d["d15"], d["d510n"], d["fake"], d["dup"],
             d["dupB"], d["dupT"], d["dupE"], int(d["ntc"]), d["nhb"], d["nht"], d["nhe"], d["ms"], d["nev"],
             "PASS" if not bad else "FAIL:" + ",".join(bad)))
