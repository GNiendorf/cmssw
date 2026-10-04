#!/usr/bin/env python3
"""join.py <dump.py> <timing json>[,json...] <label>...: for each legacy collection, its producer and every consumer that RUNS in
the CI-shape timing job (FastTimerService merged JSON of the same menu), with host ms per input event (time_real / input events;
@alpaka modules: host side incl. waiting), and in how many events it ran. Consumers absent from the timing job are listed as
'not run' (validation-only, or tasks nobody consumes). Read-only."""
import sys, json, collections
sys.path.insert(0, __import__("os").path.dirname(__file__))
from menu_stageD_consumers import load, scan
proc = load(sys.argv[1]); labels = sys.argv[3:]
T = collections.defaultdict(lambda: [0.0, 0, 0]); nin = 0
for f in sys.argv[2].split(","):
    j = json.load(open(f)); nin += j["total"]["events"]
    for m in j["modules"]:
        t = T[m["label"]]; t[0] += m["time_real"]; t[1] += m["events"]; t[2] += 1
ms = lambda lab: T[lab][0] / nin if lab in T else None
mods = {}
for d in (proc.producers_(), proc.filters_(), proc.analyzers_()): mods.update(d)
for lab in labels:
    p = mods.get(lab); pt = p.type_() if p is not None else "NOT IN MENU"
    print(f"=== {lab} [{pt}] producer host {ms(lab) if ms(lab) is not None else float('nan'):.2f} ms/ev, ran in {T[lab][1] if lab in T else 0} of {nin} ev")
    rows = []
    for name, m in sorted(mods.items()):
        out = []; scan(m, {lab}, "", out)
        if not out: continue
        par = ",".join(sorted(set(o[1] for o in out)))
        if name in T: rows.append((ms(name), f"  RUN  {name:52s} {m.type_():46s} {ms(name):7.2f} ms/ev  {T[name][1]:5d} ev  [{par}]"))
        else: rows.append((-1, f"  --   {name:52s} {m.type_():46s}  not run            [{par}]"))
    for _, r in sorted(rows, key=lambda x: -x[0]): print(r[:260])
