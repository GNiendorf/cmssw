#!/usr/bin/env python3
"""Independent re-derivation of mean nhitOT / dup / fake / nTC straight from the
output tree (tc_pt, tc_eta, tc_nhitOT, tc_isFake, tc_isDuplicate).
Mirrors efficiency/src/performance.cc: pt > 0.9, bands |eta| <1.1 / 1.1-1.7 / >=1.7."""
import sys
import ROOT
ROOT.gROOT.SetBatch(True)

def analyze(path):
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    n = t.GetEntries()
    # sums: [all, barrel, transition, endcap], plus window 1.5-3.0
    sumOT = [0.0]*5; cnt = [0.0]*5
    nfake = [0.0]*5; ndup = [0.0]*5
    ntc_total = 0
    for i in range(n):
        t.GetEntry(i)
        pt = t.tc_pt; eta = t.tc_eta; nh = t.tc_nhitOT
        isf = t.tc_isFake; isd = t.tc_isDuplicate
        m = len(pt)
        ntc_total += m
        for j in range(m):
            if pt[j] <= 0.9:
                continue
            a = abs(eta[j])
            idxs = [0]
            if a < 1.1: idxs.append(1)
            elif a < 1.7: idxs.append(2)
            else: idxs.append(3)
            if 1.5 <= a < 3.0: idxs.append(4)
            for k in idxs:
                sumOT[k] += nh[j]; cnt[k] += 1
                nfake[k] += isf[j]; ndup[k] += isd[j]
    f.Close()
    lab = ["ALL", "barrel", "transition", "endcap", "win1.5-3.0"]
    out = {}
    for k in range(5):
        out[lab[k]] = dict(meanOT=sumOT[k]/max(1,cnt[k]), nTC=cnt[k],
                           fake=nfake[k]/max(1,cnt[k]), dup=ndup[k]/max(1,cnt[k]))
    out["_rawTC"] = ntc_total
    return out

for p in sys.argv[1:]:
    r = analyze(p)
    print("== %s   (all TC rows incl pt<=0.9: %d)" % (p.split('/')[-1], r["_rawTC"]))
    print("   %-12s %10s %10s %10s %10s" % ("band", "meanOT", "nTC", "fake", "dup"))
    for k in ("ALL", "barrel", "transition", "endcap", "win1.5-3.0"):
        d = r[k]
        print("   %-12s %10.4f %10.0f %10.5f %10.5f" % (k, d["meanOT"], d["nTC"], d["fake"], d["dup"]))
