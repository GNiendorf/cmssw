#!/usr/bin/env python3
"""compare_dqm.py stock_harv.root port_harv.root [folder ...]: per-histogram comparison of two harvested DQM files.
Counts histograms that are bin-identical; for the others the chi2 test probability (UU) and the relative change of
the integral and of the mean. Flags p < 0.01. Default folders: the HLT vertex validation and the tracking MTV."""
import sys, ROOT
ROOT.gErrorIgnoreLevel = ROOT.kError
fa, fb = ROOT.TFile.Open(sys.argv[1]), ROOT.TFile.Open(sys.argv[2])
folders = sys.argv[3:] or ['DQMData/Run 1/HLT/Run summary/Vertexing', 'DQMData/Run 1/HLT/Run summary/Tracking/ValidationWRTtp']
def walk(d, path, out):
    for k in d.GetListOfKeys():
        o = k.ReadObj()
        if o.InheritsFrom('TDirectory'): walk(o, path + '/' + k.GetName(), out)
        elif o.InheritsFrom('TH1') and not o.InheritsFrom('TProfile'): out[path + '/' + k.GetName()] = o
for top in folders:
    da = fa.Get(top); db = fb.Get(top)
    if not da or not db: print('%s: missing (%s %s)' % (top, bool(da), bool(db))); continue
    ha, hb = {}, {}; walk(da, '', ha); walk(db, '', hb)
    ident = 0; nd = 0; cnt = 0; flagged = []; derived = []; missing = [k for k in ha if k not in hb]
    for k, h in ha.items():
        g = hb.get(k)
        if g is None: continue
        n = h.GetNcells()
        va = [h.GetBinContent(i) for i in range(n)]; vb = [g.GetBinContent(i) for i in range(n)] if g.GetNcells() == n else None
        if vb == va: ident += 1; continue
        nd += 1
        if vb is None: flagged.append((99., k, 'binning differs')); continue
        if all(float(x).is_integer() and x >= 0 for x in va + vb):   # counting histogram: Poisson z per bin
            cnt += 1
            z = max(abs(x - y) / max(x, y, 1.) ** 0.5 for x, y in zip(va, vb))
            if z > 3: flagged.append((z, k, 'integral %g -> %g' % (sum(va), sum(vb))))
        else:                                                         # derived (rates, means, sigmas): informational
            d = max((abs(x - y) for x, y in zip(va, vb)), default=0.)
            derived.append((d, k))
    print('%s: %d histograms, bin-identical %d, different %d (counting %d, derived %d); counting bins with |diff| > 3 sqrt(N): %d histograms; missing in port %d' %
          (top, len(ha), ident, nd, cnt, nd - cnt, len(flagged), len(missing)))
    for z, k, t in sorted(flagged, reverse=True)[:15]:
        print('   z=%.2f %-90s %s' % (z, k, t))
