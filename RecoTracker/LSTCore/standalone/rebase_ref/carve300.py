#!/usr/bin/env python3
"""Carve the FROZEN 300-event iteration subset out of the regenerated chunks.

The four 250-event chunks stay as they are (maintainer decision: no merged 1000-event
file). This builds ONE 300-event file for the fast A/B loop: chunk 0 in full (250) plus
the first 50 entries of chunk 1, taken as the first 300 entries of a TChain over the two.
The exact (run, lumi, evt) keys are written alongside so the subset is frozen and
reproducible.

Usage: carve300.py
"""
import os
import sys
import ROOT

REF = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/rebase_ref'
GEN = os.path.join(REF, 'gen')
OUT = os.path.join(REF, 'LSTNtuple_instr_300evt.root')
KEYS = os.path.join(REF, 'frozen300_keys.txt')
N = 300

# The frozen subset is the FIRST 300 entries of the merged 977-event ntuple.
MERGED = os.path.join(os.path.dirname(GEN), 'LSTNtuple_instr_977evt.root')

ROOT.gROOT.SetBatch(True)
ch = ROOT.TChain('tree')
if not os.path.exists(MERGED):
    sys.exit('missing %s' % MERGED)
ch.Add(MERGED)
if ch.GetEntries() < N:
    sys.exit('only %d entries available, need %d' % (ch.GetEntries(), N))

fout = ROOT.TFile(OUT, 'RECREATE')
tout = ch.CopyTree('', '', N, 0)
tout.Write('tree', ROOT.TObject.kOverwrite)
n = tout.GetEntries()
fout.Close()

# Re-open and dump the frozen key list.
f = ROOT.TFile.Open(OUT)
t = f.Get('tree')
keys = []
for i in range(t.GetEntries()):
    t.GetEntry(i)
    keys.append((int(t.run), int(t.lumi), int(t.evt)))
f.Close()
with open(KEYS, 'w') as fh:
    for r, l, e in keys:
        fh.write('%d %d %d\n' % (r, l, e))
print('wrote %s with %d entries (%d unique keys)' % (OUT, n, len(set(keys))))
