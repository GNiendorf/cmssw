#!/usr/bin/env python3
"""Seed-universe accounting from the instrumented ntuple.

Usage: seed_stats.py <file.root> [<file.root> ...]

Prints per-event means of the pLS / pT3 / pT5 algorithmic-duplicate populations and the
three exact closure identities that pin each snapshot to its kernel boundary.
"""
import sys
import numpy as np
import uproot

BR = ['pLS_isQuad', 'pLS_isDupAlgSelf', 'pLS_isDupAlgPass2', 'pLS_isDupAlgFinal',
      'pLS_isDuplicate', 'tc_type', 'pT3_isDupAlgSelf', 'pT3_isDupAlgFinal', 'pT5_isDupAlg']

tot = {}
nev = 0
okA = okB = okC = True
for path in sys.argv[1:]:
    t = uproot.open(path)['tree']
    for chunk in t.iterate(BR, step_size=25):
        for i in range(len(chunk)):
            q = np.asarray(chunk['pLS_isQuad'][i]).astype(bool)
            s = np.asarray(chunk['pLS_isDupAlgSelf'][i])
            p2 = np.asarray(chunk['pLS_isDupAlgPass2'][i])
            fi = np.asarray(chunk['pLS_isDupAlgFinal'][i])
            td = np.asarray(chunk['pLS_isDuplicate'][i])
            ty = np.asarray(chunk['tc_type'][i])
            p3s = np.asarray(chunk['pT3_isDupAlgSelf'][i])
            p3f = np.asarray(chunk['pT3_isDupAlgFinal'][i])
            p5 = np.asarray(chunk['pT5_isDupAlg'][i])
            d = dict(
                nPLS=len(q), nQuad=int(q.sum()),
                selfKeepQ=int(((s == 0) & q).sum()),
                pass2KeepQ=int(((p2 == 0) & q).sum()),
                finalKeepQ=int(((fi == 0) & q).sum()),
                # truth-duplicate content of each universe (sim-matching based)
                selfKeepQ_truthdup=int(((s == 0) & q & (td == 1)).sum()),
                pass2KeepQ_truthdup=int(((p2 == 0) & q & (td == 1)).sum()),
                finalKeepQ_truthdup=int(((fi == 0) & q & (td == 1)).sum()),
                tc8=int((ty == 8).sum()), tc7=int((ty == 7).sum()), tc5=int((ty == 5).sum()),
                tc4=int((ty == 4).sum()), tc9=int((ty == 9).sum()), nTC=len(ty),
                nPT3=len(p3s), pt3SelfKeep=int((p3s == 0).sum()), pt3FinalKeep=int((p3f == 0).sum()),
                nPT5=len(p5), pt5Keep=int((p5 == 0).sum()),
                notRecorded=int((s == -999).sum()),
            )
            okA &= (d['finalKeepQ'] == d['tc8'])
            okB &= (d['pt5Keep'] == d['tc7'])
            okC &= (d['pt3FinalKeep'] == d['tc5'])
            for k, v in d.items():
                tot[k] = tot.get(k, 0) + v
            nev += 1

print("events %d" % nev)
print("PER-EVENT MEANS")
for k in ['nPLS', 'nQuad', 'selfKeepQ', 'pass2KeepQ', 'finalKeepQ',
          'selfKeepQ_truthdup', 'pass2KeepQ_truthdup', 'finalKeepQ_truthdup',
          'nTC', 'tc8', 'tc7', 'tc5', 'tc4', 'tc9',
          'nPT3', 'pt3SelfKeep', 'pt3FinalKeep', 'nPT5', 'pt5Keep', 'notRecorded']:
    print("  %-20s %10.2f" % (k, tot[k] / nev))
print()
print("CLOSURE  isQuad&&Final==0 == n(tc_type 8) : %s" % okA)
print("CLOSURE  pT5_isDupAlg==0  == n(tc_type 7) : %s" % okB)
print("CLOSURE  pT3 Final==0     == n(tc_type 5) : %s" % okC)
print()
f = tot['finalKeepQ'] / nev
print("post-deletion bare-seed universe, pass1 only  : %.1f/evt  (x%.2f today's %.1f)"
      % (tot['selfKeepQ'] / nev, tot['selfKeepQ'] / tot['finalKeepQ'], f))
print("post-deletion bare-seed universe, pass1+pass2 : %.1f/evt  (x%.2f)"
      % (tot['pass2KeepQ'] / nev, tot['pass2KeepQ'] / tot['finalKeepQ']))
print("truth-duplicate fraction of the survivors: today %.3f | pass1+2 %.3f | pass1 %.3f"
      % (tot['finalKeepQ_truthdup'] / tot['finalKeepQ'],
         tot['pass2KeepQ_truthdup'] / tot['pass2KeepQ'],
         tot['selfKeepQ_truthdup'] / tot['selfKeepQ']))
