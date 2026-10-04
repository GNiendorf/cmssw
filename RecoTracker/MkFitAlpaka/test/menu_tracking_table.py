#!/usr/bin/env python3
"""tracking_table.py ARM=dir[,dir...] [ARM=...]: CI-style tracking-time table from patatrack benchmark output dirs
(merged Phase2Timing_resources.json + bench.log). Grouping = circles/web/groups/hlt.json, as hltqcd/timing/ci_ib/
track_break.py (the 742 ms/event 'Tracking' number = groups Tracking + Tracking|Conversion + Tracking|Portable).
Module types the circles file does not know (our MkFitAlpaka* modules, the stock mkFit-fit modules) go to 'Tracking'
(listed). ms per INPUT event (time_real / events of the job, as CI); the arms' repetitions are averaged."""
import json, re, sys, collections, os
T = "/mnt/data1/gsn27/here/hltqcd/timing"
groups = []
for pat, g in json.load(open(f"{T}/circles/web/groups/hlt.json")).items():
    ct, lb = pat.split("|") if "|" in pat else ("", pat)
    rx = lambda s: re.compile(s.replace("?", ".").replace("*", ".*") + "$") if s else None
    groups.append((rx(ct), rx(lb), g))
added = set()
def grp(m):
    for c, l, g in groups:
        if (c is None or c.match(m["type"])) and (l is None or l.match(m["label"])): return g
    if "MkFit" in m["type"]:
        added.add((m["label"], m["type"])); return "Tracking"
    return "Unassigned"
def evs(d):
    try:
        for line in open(os.path.join(d, "bench.log")):
            mm = re.search(r"^\s*([0-9.]+)\s*\S+\s*([0-9.]+) ev/s", line)
            if mm: return float(mm.group(1)), float(mm.group(2))
    except OSError: pass
    return None, None
arms = []
for a in sys.argv[1:]:
    name, dirs = a.split("=", 1); runs = []
    for d in dirs.split(","):
        f = os.path.join(d, "Phase2Timing_resources.json")
        if not os.path.exists(f): print("missing", f); continue
        j = json.load(open(f)); ev = j["total"]["events"]
        runs.append({"dir": d, "ev": ev, "evs": evs(d), "tot": j["total"]["time_real"] / ev,
                     "mods": {m["label"]: (m["type"], grp(m), m["time_real"] / ev) for m in j["modules"] if m["label"] != j["total"].get("label")}})
    arms.append((name, runs))
avg = lambda xs: sum(xs) / len(xs) if xs else float("nan")
TRK = ("Tracking", "Tracking|Conversion", "Tracking|Portable")
print("%-12s %5s %22s %10s %10s %10s %10s %10s" % ("arm", "reps", "ev/s (each rep)", "ev/s mean", "total", "Tracking", "@alpaka", "other"))
print("%-12s %5s %22s %10s %10s %10s %10s %10s" % ("", "", "", "", "ms/ev", "ms/ev", "modules", "modules"))
summary = {}
for name, runs in arms:
    G = collections.defaultdict(float); mods = collections.defaultdict(float); typ = {}
    for r in runs:
        for lab, (t, g, ms) in r["mods"].items():
            G[g] += ms / len(runs); mods[lab] += ms / len(runs); typ[lab] = (t, g)
    trk = sum(G[g] for g in TRK)
    port = sum(ms for l, ms in mods.items() if typ[l][1] in TRK and "@alpaka" in typ[l][0])
    summary[name] = (G, mods, typ, trk)
    e = [r["evs"][0] for r in runs if r["evs"][0] is not None]
    print("%-12s %5d %22s %10.2f %10.1f %10.1f %10.1f %10.1f" % (name, len(runs), " ".join("%.1f" % x for x in e), avg(e),
          avg([r["tot"] for r in runs]), trk, port, trk - port))
print("\nTracking slice per module (type = in the last arm that has it; ms per input event; H = host module, D = @alpaka module (on GPU: its host time only), S = explicit serial_sync)")
labs = sorted({l for _, (G, mods, typ, trk) in summary.items() for l in mods if typ[l][1] in TRK},
              key=lambda l: -max(s[1].get(l, 0) for s in summary.values()))
print("%-52s %-46s %-4s" % ("module", "type", "") + "".join("%11s" % n for n, _ in arms))
for l in labs:
    t = [s[2][l][0] for s in summary.values() if l in s[2]][-1]  # type in the LAST arm that has the label
    hd = "S" if t.startswith("alpaka_serial_sync::") else ("D" if "@alpaka" in t else "H")
    row = "".join("%11s" % ("%.2f" % summary[n][1][l] if l in summary[n][1] else "-") for n, _ in arms)
    if max(summary[n][1].get(l, 0) for n, _ in arms) >= 0.05:
        print("%-52s %-46s %-4s" % (l[:52], t[:46], hd) + row)
print("%-103s" % "SUM Tracking" + "".join("%11.1f" % summary[n][3] for n, _ in arms))
CALO = ("HGCal", "ECAL", "HCAL")
calo = {n: sum(summary[n][0].get(g, 0) for g in CALO) for n, _ in arms}
cm = avg(list(calo.values()))
print("\nload-normalised (box load drifts between arms): Tracking x (mean calo / arm calo), calo = HGCal+ECAL+HCAL, which no arm changes")
print("hltLST's host time is mostly waiting for a GPU shared with other jobs: also shown without it")
print("%-12s %10s %10s %14s %14s %16s" % ("arm", "calo ms/ev", "factor", "Tracking norm", "excl. hltLST", "excl. hltLST norm"))
for n, _ in arms:
    xl = summary[n][3] - summary[n][1].get("hltLST", 0)
    print("%-12s %10.1f %10.3f %14.1f %14.1f %16.1f" % (n, calo[n], cm / calo[n], summary[n][3] * cm / calo[n], xl, xl * cm / calo[n]))
if added: print("\nnot in circles hlt.json, counted as Tracking:", ", ".join(sorted("%s(%s)" % x for x in added)))
print("\ngroups (ms/ev):")
gs = sorted({g for s in summary.values() for g in s[0]}, key=lambda g: -max(s[0].get(g, 0) for s in summary.values()))
for g in gs[:24]:
    print("  %-26s" % g + "".join("%11.1f" % summary[n][0].get(g, 0) for n, _ in arms))
