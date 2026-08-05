#!/usr/bin/env python3
"""Fake population by object type, and bare-OT fakes by region x length, on the full 977."""
import ROOT
from collections import Counter
ROOT.gROOT.SetBatch(True); ROOT.gErrorIgnoreLevel = ROOT.kError
S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
TN = {4: 'T5cls', 9: 'T4cls', 7: 'pT5', 5: 'pT3cls', 8: 'pLS'}
for tag, path in [('W_X4', 'fin_ref/r_W_X4.root'), ('LST', 'rebase_ref/LSTNtuple_instr_977evt.root')]:
    f = ROOT.TFile.Open(S + path); t = f.Get('tree'); n = t.GetEntries()
    tot, fk = Counter(), Counter()
    tot2, fk2 = Counter(), Counter()
    for i in range(n):
        t.GetEntry(i)
        ty = list(t.tc_type); fkl = list(t.tc_isFake); L = list(t.tc_nhitOT); et = list(t.tc_eta)
        for k in range(len(ty)):
            tot[ty[k]] += 1; fk[ty[k]] += int(fkl[k] != 0)
            if ty[k] in (4, 9):
                r = 'barrel' if abs(et[k]) < 1.1 else ('trans' if abs(et[k]) < 1.7 else 'endcap')
                tot2[(r, L[k])] += 1; fk2[(r, L[k])] += int(fkl[k] != 0)
    print("\n=== %s (%d evts) : TCs and fakes by type ===" % (tag, n))
    print("%-8s %12s %10s %10s %10s" % ("type", "nTC", "nFake", "fakeRate", "fakes/evt"))
    for ty in sorted(tot, key=lambda x: -tot[x]):
        print("%-8s %12d %10d %10.5f %10.1f" % (TN.get(ty, ty), tot[ty], fk[ty], fk[ty] / tot[ty], fk[ty] / n))
    print("%-8s %12d %10d %10.5f %10.1f" % ('ALL', sum(tot.values()), sum(fk.values()),
          sum(fk.values()) / sum(tot.values()), sum(fk.values()) / n))
    print("  bare OT (type 4/9) fakes by region x nhitOT, top 8:")
    for key in sorted(tot2, key=lambda x: -fk2[x])[:8]:
        print("   %-8s nhitOT=%-3d nTC/evt=%7.1f fakes/evt=%6.1f fakeRate=%.4f" %
              (key[0], key[1], tot2[key] / n, fk2[key] / n, fk2[key] / tot2[key]))
    f.Close()
