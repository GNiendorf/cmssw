#!/usr/bin/env python3
"""How much outer-tracker material do the T4-band sims actually leave?

For each traced sim, count the distinct OT layers spanned by the delivering LST object's
6 T3-anchor slots, and (independently) the number of distinct layers over ALL t3 rows in
the event whose matched-sim list contains that sim -- i.e. the ceiling on chain nLayers
any chain built from our node set could reach for that track.
"""
import json
import ROOT

S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
P = S + "/fanout4/t4trace"
led = json.load(open(P + "/t4_targets.json"))
byev = {}
for r in led:
    byev.setdefault(r["iev"], []).append(r)

f = ROOT.TFile.Open(S + "/LSTNtuple_PU200RelVal_300evt.root")
t = f.Get("tree")
want = ["t4_t3_%d_layer" % k for k in range(6)] + ["t5_t3_%d_layer" % k for k in range(6)]
want += ["t3_matched_simIdx", "t3_lsIdx0", "t3_lsIdx1", "ls_mdIdx0", "ls_mdIdx1",
         "md_layer", "sim_pt"]
t.SetBranchStatus("*", 0)
for b in want:
    t.SetBranchStatus(b, 1)

res = []
for iev in sorted(byev):
    t.GetEntry(iev)
    md_layer = list(t.md_layer)
    ls0 = list(t.ls_mdIdx0)
    ls1 = list(t.ls_mdIdx1)
    t3l0 = list(t.t3_lsIdx0)
    t3l1 = list(t.t3_lsIdx1)
    nT3 = len(t3l0)
    t3md = [(ls0[t3l0[i]], ls1[t3l0[i]], ls1[t3l1[i]]) for i in range(nT3)]
    # sim -> set of t3 rows (t3_matched_simIdx is FULL sim space; accepted sims are prefix)
    sim2t3 = {}
    for i, sl in enumerate(t.t3_matched_simIdx):
        for s in sl:
            sim2t3.setdefault(int(s), []).append(i)
    t4lay = [list(getattr(t, "t4_t3_%d_layer" % k)) for k in range(6)]
    t5lay = [list(getattr(t, "t5_t3_%d_layer" % k)) for k in range(6)]
    for r in byev[iev]:
        row = r["objRow"]
        src = t4lay if r["type"] == 9 else t5lay
        objLayers = sorted(set(int(src[k][row]) for k in range(6)))
        rows = sim2t3.get(r["sim"], [])
        mds = set()
        for i in rows:
            mds.update(t3md[i])
        ceil = sorted(set(md_layer[m] for m in mds))
        res.append(dict(iev=iev, sim=r["sim"], type=r["type"], dxy=r["dxy"], vxy=r["vxy"],
                        objLayers=objLayers, nObjLay=len(objLayers),
                        nT3rows=len(rows), ceilLayers=ceil, nCeil=len(ceil)))

for ty, name in ((9, "LST-T4-delivered"), (4, "LST-T5-delivered (control)")):
    g = [x for x in res if x["type"] == ty]
    print("\n=== %s  n=%d ===" % (name, len(g)))
    print("  distinct layers of the delivering object : %s"
          % sorted(x["nObjLay"] for x in g))
    print("  # t3 rows sim-matched in our node set    : %s"
          % sorted(x["nT3rows"] for x in g))
    print("  CEILING distinct layers over all matched t3 : %s"
          % sorted(x["nCeil"] for x in g))
    nc = {}
    for x in g:
        nc[x["nCeil"]] = nc.get(x["nCeil"], 0) + 1
    print("  ceiling-layer histogram: %s" % dict(sorted(nc.items())))
    n0 = sum(1 for x in g if x["nT3rows"] == 0)
    n1 = sum(1 for x in g if x["nT3rows"] == 1)
    print("  sims with 0 matched t3 rows: %d ; with exactly 1: %d" % (n0, n1))

json.dump(res, open(P + "/t4_layers.json", "w"))
print("\nwrote", P + "/t4_layers.json")
