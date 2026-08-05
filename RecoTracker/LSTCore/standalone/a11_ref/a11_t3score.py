#!/usr/bin/env python3
"""Separating power of the UPSTREAM t3dnn scores over the bare-T3 universe.

The attach head is BLIND to T3 quality: for a bare-T3 target PixelAttach.cc sets
gateLogit = 0 and nLayersF = 3, so features 9/10/11 carry no quality information at all.
t3_fakeScore / t3_promptScore already survive the P2.7 deletion (they are computed at T3
build time and ChainFeatures already consumes them), so they are admissible as a delivery
admission cut. This measures whether they would help.

Truth proxy: t3_pMatched > 0.75 (the harness coverage rule).
"""
import sys
import ROOT

NEV = int(sys.argv[2]) if len(sys.argv) > 2 else 30
f = ROOT.TFile.Open(sys.argv[1])
t = f.Get("tree")
CUTS = [0.02, 0.05, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.95, 0.98, 1.01]
ntrue = nfake = 0
kt = [0] * len(CUTS)
kf = [0] * len(CUTS)
pt_true = [0] * len(CUTS)
pt_fake = [0] * len(CUTS)
for i in range(min(NEV, t.GetEntries())):
    t.GetEntry(i)
    fs, pm, ps = t.t3_fakeScore, t.t3_pMatched, t.t3_promptScore
    for j in range(len(fs)):
        good = pm[j] > 0.75
        if good:
            ntrue += 1
        else:
            nfake += 1
        for c, v in enumerate(CUTS):
            if fs[j] <= v:
                (kt if good else kf)[c] += 1
            if ps[j] >= 1.0 - v:
                (pt_true if good else pt_fake)[c] += 1
print("T3 universe over %d evts: true=%d (%.0f/evt) fake=%d (%.0f/evt) purity=%.4f"
      % (NEV, ntrue, ntrue / NEV, nfake, nfake / NEV, ntrue / max(1, ntrue + nfake)))
print("%-10s %10s %10s %9s %9s" % ("fakeScore<=", "keptTrue", "keptFake", "trueRec", "purity"))
for c, v in enumerate(CUTS):
    print("%-10.3g %10d %10d %9.4f %9.4f"
          % (v, kt[c], kf[c], kt[c] / max(1, ntrue), kt[c] / max(1, kt[c] + kf[c])))
print()
print("%-10s %10s %10s %9s %9s" % ("promptScr>=", "keptTrue", "keptFake", "trueRec", "purity"))
for c, v in enumerate(CUTS):
    print("%-10.3g %10d %10d %9.4f %9.4f"
          % (1.0 - v, pt_true[c], pt_fake[c], pt_true[c] / max(1, ntrue),
             pt_true[c] / max(1, pt_true[c] + pt_fake[c])))
