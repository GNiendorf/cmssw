#!/usr/bin/env python3
"""For each in-cut sim in the efficiency denominator, which delivery class provides its
BEST-matching TC (the harness's own sim_tcIdx). Comparable between our ntuple and the LST
identity ntuple, and it is exactly what the harness books as the per-type efficiency sets.
Pairs with the fake budget: 'this class buys N tracks and costs M fakes per event'."""
import sys
import ROOT

DELIV = {0: "carried", 1: "chain", 2: "attachT5", 3: "attachT3", 4: "zp8pLS"}
TYPE = {4: "T5", 5: "pT3", 7: "pT5", 8: "pLS", 9: "T4"}


def run(path):
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    have_ch = t.GetBranch("tc_isChain") is not None
    cnt = {}
    tot = 0
    nev = 0
    for ev in t:
        nev += 1
        ty = list(ev.tc_type)
        ch = list(ev.tc_isChain) if have_ch else []
        if len(ch) != len(ty):
            ch = [0] * len(ty)
        stc = list(ev.sim_tcIdx)
        spt = list(ev.sim_pt)
        svz = list(ev.sim_vz)
        svx = list(ev.sim_vx)
        svy = list(ev.sim_vy)
        sq = list(ev.sim_q)
        for s in range(len(spt)):
            if not (spt[s] > 0.9 and abs(svz[s]) < 30 and sq[s] != 0 and
                    (svx[s] ** 2 + svy[s] ** 2) < 6.25):
                continue
            tot += 1
            i = stc[s]
            if i < 0:
                cnt["NOT RECO"] = cnt.get("NOT RECO", 0) + 1
                continue
            k = "%s/%s" % (DELIV.get(ch[i], "?"), TYPE.get(ty[i], "?"))
            cnt[k] = cnt.get(k, 0) + 1
    f.Close()
    print("=" * 80)
    print("%s   events %d   eff denominator %d (%.1f/evt)" % (path, nev, tot, tot / float(nev)))
    for k in sorted(cnt, key=lambda k: -cnt[k]):
        print("  %-20s %8d  %8.2f/evt  %7.4f of denominator" %
              (k, cnt[k], cnt[k] / float(nev), cnt[k] / float(tot)))


for p in sys.argv[1:]:
    run(p)
