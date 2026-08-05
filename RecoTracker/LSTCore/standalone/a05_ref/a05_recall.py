#!/usr/bin/env python3
"""A05: is the pT3-CLASS RECALL GAP an end-to-end efficiency gap?

For each event, using the prototype output (which carries BOTH our tc_* rows and, from the
same instrumented ntuple, LST's own rows are NOT present) we instead pair the prototype
file with the input ntuple event-by-event (same order, same events).

  lstP3only : accepted sims covered by an LST pT3 row (type 5) and by NO other LST row
  ourMiss   : of those, how many are covered by NO prototype row at all (the real loss)
  ourElse   : covered by a prototype row that is NOT an attachT3 row (absorbed elsewhere)
  ourT3     : covered by one of OUR attachT3 rows (like-for-like)
Also the reverse: sims OUR attachT3 uniquely carries that LST's pT3 does not cover.
Usage: a05_recall.py <tag>
"""
import sys
import ROOT

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
LSTN = S + 'rebase_ref/LSTNtuple_instr_300evt.root'

tag = sys.argv[1]
fp = ROOT.TFile.Open(S + 'a05_ref/a_%s.root' % tag)
tp = fp.Get('tree')
fl = ROOT.TFile.Open(LSTN)
tl = fl.Get('tree')

n = min(tp.GetEntries(), tl.GetEntries())
agg = dict(lstP3=0, lstP3only=0, ourMiss=0, ourElse=0, ourT3=0,
           ourT3only=0, lstMissOurT3=0, nev=0)
itp = iter(tp)
itl = iter(tl)
for _ in range(n):
    ep = next(itp)
    el = next(itl)
    nacc = len(ep.sim_pt)
    # LST side
    lty = list(el.tc_type)
    lsim = list(el.tc_simIdxAll)
    lp3, lother = set(), set()
    for i, t in enumerate(lty):
        tgt = lp3 if t == 5 else lother
        for s in lsim[i]:
            if s < nacc:
                tgt.add(s)
    # our side
    pch = list(ep.tc_isChain)
    psim = list(ep.tc_simIdxAll)
    ot3, oother = set(), set()
    for i, c in enumerate(pch):
        tgt = ot3 if c == 3 else oother
        for s in psim[i]:
            if s < nacc:
                tgt.add(s)
    p3only = lp3 - lother
    agg['lstP3'] += len(lp3)
    agg['lstP3only'] += len(p3only)
    for s in p3only:
        if s in ot3:
            agg['ourT3'] += 1
        elif s in oother:
            agg['ourElse'] += 1
        else:
            agg['ourMiss'] += 1
    t3only = ot3 - oother
    agg['ourT3only'] += len(t3only)
    agg['lstMissOurT3'] += len([s for s in t3only if s not in lp3 and s not in lother])
    agg['nev'] += 1

nev = float(agg['nev'])
print('tag %s  events %d' % (tag, agg['nev']))
print('  LST pT3 covers                       %7.2f sims/evt' % (agg['lstP3'] / nev))
print('  ... of which ONLY pT3 covers         %7.2f sims/evt' % (agg['lstP3only'] / nev))
print('      -> we cover with OUR pT3 class   %7.2f' % (agg['ourT3'] / nev))
print('      -> we cover with something else  %7.2f' % (agg['ourElse'] / nev))
print('      -> WE MISS ENTIRELY              %7.2f   <== the real end-to-end loss'
      % (agg['ourMiss'] / nev))
print('  OUR pT3 class uniquely carries       %7.2f sims/evt' % (agg['ourT3only'] / nev))
print('  ... of which LST covers NOWHERE      %7.2f   <== efficiency the class ADDS'
      % (agg['lstMissOurT3'] / nev))
