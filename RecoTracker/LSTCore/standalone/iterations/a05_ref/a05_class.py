#!/usr/bin/env python3
"""A05 pT3-CLASS CONTRIBUTION LEDGER.

For a prototype output file (a_<TAG>.root) it slices the delivered TCs by provenance
(tc_isChain: 0 carried, 1 chain, 2 attach-pT5, 3 attach-pT3) and reports, per event:

  rows      delivered rows of the slice
  fake      rows with tc_isFake == 1
  dup       rows with tc_isDuplicate == 1
  uniqSim   ACCEPTED in-cut-blind sims that are matched (>0.75, tc_simIdxAll) by a row of
            this slice AND by NO row outside it -- i.e. efficiency the slice alone carries
  covSim    accepted sims matched by at least one row of the slice (with or without help)

With --lst the same three numbers are computed for LST's OWN pT3 rows (tc_type == 5) read
straight out of the input ntuple, on the same events, so the class can be compared to the
thing it replaces rather than only to itself.

Usage:  a05_class.py <tag> [<tag> ...]  [--lst]
"""
import sys

import ROOT

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
LSTN = S + 'rebase_ref/LSTNtuple_instr_300evt.root'

SLICES = [('carried', 0), ('chain', 1), ('attachT5', 2), ('attachT3', 3)]


def scan(path, slicer, label):
    f = ROOT.TFile.Open(path)
    t = f.Get('tree')
    if not t:
        print('%-24s NO TREE' % label)
        return
    out = {}
    nev = 0
    for e in t:
        nev += 1
        nacc = len(e.sim_pt)
        keys = slicer(e)
        sims = list(e.tc_simIdxAll)
        isf = list(e.tc_isFake)
        isd = list(e.tc_isDuplicate)
        # sim -> set of slice keys that match it
        simowner = {}
        for i, k in enumerate(keys):
            for s in sims[i]:
                if s < nacc:
                    simowner.setdefault(s, set()).add(k)
        for i, k in enumerate(keys):
            d = out.setdefault(k, [0, 0, 0, 0, 0])
            d[0] += 1
            d[1] += 1 if isf[i] else 0
            d[2] += 1 if isd[i] else 0
        for s, ks in simowner.items():
            for k in ks:
                out.setdefault(k, [0, 0, 0, 0, 0])[4] += 1
            if len(ks) == 1:
                out.setdefault(next(iter(ks)), [0, 0, 0, 0, 0])[3] += 1
    for k in sorted(out):
        d = out[k]
        print('%-24s %-10s rows/evt %8.2f  fake %6.4f  dup %6.4f  uniqSim/evt %7.2f  covSim/evt %7.2f'
              % (label, k, d[0] / nev, d[1] / max(d[0], 1), d[2] / max(d[0], 1), d[3] / nev, d[4] / nev))
    f.Close()


def proto_slicer(e):
    ch = list(e.tc_isChain)
    ty = list(e.tc_type)
    names = {0: 'carried', 1: 'chain', 2: 'attachT5', 3: 'attachT3'}
    return [names.get(c, 'c%d' % c) if c != 0 else 'carried%d' % ty[i] for i, c in enumerate(ch)]


def lst_slicer(e):
    ty = list(e.tc_type)
    names = {7: 'LSTpT5', 5: 'LSTpT3', 4: 'LSTT5', 8: 'LSTpLS', 9: 'LSTT4'}
    return [names.get(x, 'LSTt%d' % x) for x in ty]


if __name__ == '__main__':
    tags = [a for a in sys.argv[1:] if not a.startswith('--')]
    for tg in tags:
        scan(S + 'a05_ref/a_%s.root' % tg, proto_slicer, tg)
    if '--lst' in sys.argv:
        scan(LSTN, lst_slicer, 'LST')
