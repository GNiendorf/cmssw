import sys
import ROOT

f = ROOT.TFile.Open(sys.argv[1])
t = f.Get("tree")
n = 0
per_type = {}
for e in t:
    n += len(e.tc_pt)
    for ty in e.tc_type:
        per_type[ty] = per_type.get(ty, 0) + 1
print("nTC", n)
for k in sorted(per_type):
    print("type", k, per_type[k])
