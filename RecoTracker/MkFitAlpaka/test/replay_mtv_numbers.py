#!/usr/bin/env python3
"""Integrated MTV numbers from harvested replay_mtv files (harness lane).
   python3 test/replay_mtv_numbers.py harvested.root [more.root ...]
   efficiency = num_assoc(simToReco)_eta / num_simul_eta (MTV TP selection), fake = 1 - num_assoc(recoToSim)_eta /
   num_reco_eta, duplicate = num_duplicate_eta / num_reco_eta; binomial errors."""
import sys, math
import ROOT
base = 'DQMData/Run 1/HLT/Run summary/Tracking/ValidationWRTtp'
def integral(d, name):
    h = d.Get(name)
    return h.Integral(0, h.GetNbinsX() + 1) if h else float('nan')
def rate(n, d):
    if not d > 0: return float('nan'), float('nan')
    r = n / d
    return r, math.sqrt(max(r * (1 - r), 0) / d)
for fn in sys.argv[1:]:
    f = ROOT.TFile.Open(fn)
    top = f.Get(base)
    print(fn)
    print('  %-50s %8s %8s  %-17s %-17s %-17s' % ('collection', 'nSimTP', 'nReco', 'efficiency', 'fake rate', 'duplicate rate'))
    for k in top.GetListOfKeys():
        d = k.ReadObj()
        if k.GetName() == 'simulation' or not d or not d.InheritsFrom('TDirectory'): continue
        nsim, nsa = integral(d, 'num_simul_eta'), integral(d, 'num_assoc(simToReco)_eta')
        nreco, nra, ndup = integral(d, 'num_reco_eta'), integral(d, 'num_assoc(recoToSim)_eta'), integral(d, 'num_duplicate_eta')
        e, ee = rate(nsa, nsim); fa, fe = rate(nreco - nra, nreco); du, de = rate(ndup, nreco)
        print('  %-50s %8d %8d  %.4f +- %.4f   %.4f +- %.4f   %.4f +- %.4f' % (k.GetName(), nsim, nreco, e, ee, fa, fe, du, de))
