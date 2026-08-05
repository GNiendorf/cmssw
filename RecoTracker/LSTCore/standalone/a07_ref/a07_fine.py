#!/usr/bin/env python3
"""Fine sweep of the two cheapest existing gate constants, hunting for a setting that is
EXACTLY free on efficiency and on all four vxy and all four dxy bands."""
import sys
from a07_sim import load, prep, evaluate, HDR, row

ev = prep(load(sys.argv[1]))
base = evaluate(ev, None, "BASELINE")
cases = []
for v in (-1.15, -1.10, -1.05, -1.00):
    cases.append(("-M4D %g" % v,
                  lambda e, i, v=v: e["deliv"][i] in (1, 2) and e["br"][i] == 1 and e["md"][i] < v))
for p in (0.1, 0.2, 0.3, 0.5):
    cases.append(("-C25 %g -C25D -2" % p,
                  lambda e, i, p=p: (e["deliv"][i] in (1, 2) and e["nl"][i] == 5 and
                                     e["nn"][i] == 2 and e["mp"][i] < p and e["md"][i] < -2.0)))
for v in (-1.75, -1.70, -1.65, -1.60):
    cases.append(("-MR %g" % v,
                  lambda e, i, v=v: e["deliv"][i] in (1, 2) and e["br"][i] == 3 and
                  max(e["mp"][i], e["md"][i]) < v))
print(HDR)
print(row(base))
print("\n%-20s %9s %9s %9s %9s %9s %9s %9s %9s %9s" %
      ("label", "d_eff", "d_dup", "d_fake", "d_v01", "d_v15", "d_v510", "d_v1030", "d_d01", "d_d15"))
for lbl, fn in cases:
    m = evaluate(ev, fn, lbl)
    print("%-20s %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f %+9.5f" %
          (lbl, m["eff"] - base["eff"], m["dup"] - base["dup"], m["fake"] - base["fake"],
           m["v01"] - base["v01"], m["v15"] - base["v15"], m["v510"] - base["v510"],
           m["v1030"] - base["v1030"], m["d01"] - base["d01"], m["d15"] - base["d15"]))
