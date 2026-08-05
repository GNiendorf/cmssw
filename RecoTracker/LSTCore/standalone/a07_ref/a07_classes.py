#!/usr/bin/env python3
"""Full-band cost of deleting each delivery class outright -- the diagnostic that says
which cells are even ALLOWED to be touched under 'protect the displaced win'."""
import sys
from a07_sim import load, prep, evaluate, HDR, row

CASES = [
    ("drop attachT3 pT3", lambda e, i: e["deliv"][i] == 3),
    ("drop chain T4", lambda e, i: e["deliv"][i] == 1 and e["type"][i] == 9),
    ("drop chain T4 br1(exempt)", lambda e, i: e["deliv"][i] == 1 and e["type"][i] == 9 and e["br"][i] == 1),
    ("drop chain T4 br0(IP)", lambda e, i: e["deliv"][i] == 1 and e["type"][i] == 9 and e["br"][i] == 0),
    ("drop chain T5 br3(exempt)", lambda e, i: e["deliv"][i] == 1 and e["type"][i] == 4 and e["br"][i] == 3),
    ("drop chain T5 br2(IP)", lambda e, i: e["deliv"][i] == 1 and e["type"][i] == 4 and e["br"][i] == 2),
    ("drop zp8 pLS", lambda e, i: e["deliv"][i] == 4),
]

ev = prep(load(sys.argv[1]))
base = evaluate(ev, None, "BASELINE")
print(HDR)
print(row(base))
res = []
for lbl, fn in CASES:
    m = evaluate(ev, fn, lbl)
    res.append(m)
    print(row(m))
print("\nDELTAS  (1 track = eff .000044 | v15 .000731 | v510 .001686 | v1030 .000801 | d15 .001115)")
print("%-28s %9s %9s %9s %9s %9s %9s %9s" %
      ("label", "d_eff", "d_dup", "d_fake", "d_v15", "d_v510", "d_v1030", "d_d15"))
for m in res:
    print("%-28s %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f" %
          (m["label"], m["eff"] - base["eff"], m["dup"] - base["dup"], m["fake"] - base["fake"],
           m["v15"] - base["v15"], m["v510"] - base["v510"], m["v1030"] - base["v1030"],
           m["d15"] - base["d15"]))
