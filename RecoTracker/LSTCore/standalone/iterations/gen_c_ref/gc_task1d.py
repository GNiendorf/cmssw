#!/usr/bin/env python3
"""GEN-C TASK 1d -- the MARGINAL-VALUE curve, the number the gate actually cares about.

`uniq` inside the low-margin curve run is contaminated: at -AT3 -8 stage B owns nearly
every pLS, so the carried bare-pLS rows are all retired and the run's own "everything
else" is artificially poor. The correct reference for "what would this class ADD" is the
coverage of OUR pipeline WITH THE pT3 CLASS OFF -- that is exactly the r_off run, and its
efficiency (.77432 = 58.8 in-cut sims/evt of 75.9) is the floor the class has to lift.

Same treatment for LST: its pT3 class's marginal value is measured against LST's own
non-pT3 rows in the same input ntuple.

Emits, per eta region and pT band, our rows/evt, row fake fraction and MARGINAL coverage
against the r_off reference, as the stage-B margin is scanned -- with LST's two operating
points on the same axes.
"""
import pickle
import sys

import ROOT

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
D = pickle.load(open(S + 'gen_c_ref/task1_%s.pkl' % (sys.argv[1] if len(sys.argv)>1 else 'c1_curve'), 'rb'))
NEV = float(D['nev'])
EV = D['evts']
REG = [('barrel', 0.0, 1.1), ('trans', 1.1, 1.7), ('endcap', 1.7, 99.0)]
PTB = [('0.9-1.5', 0.9, 1.5), ('1.5-3', 1.5, 3.0), ('3-10', 3.0, 10.0), ('10+', 10.0, 1e9)]


def bnd(v, bands):
    for i, (_, lo, hi) in enumerate(bands):
        if lo <= abs(v) < hi:
            return i
    return len(bands) - 1


# --- the r_off reference coverage set, per event -----------------------------------
fin = ROOT.TFile.Open(S + 'LSTNtuple_PU200RelVal_300evt.root')
tin = fin.Get('tree')
fo = ROOT.TFile.Open(S + 'gen_c_ref/t_r_off.root')
to = fo.Get('tree')
roff = []
for i in range(int(NEV)):
    tin.GetEntry(i)
    to.GetEntry(i)
    g2l = {g: k for k, g in enumerate(tin.sim_trkNtupIdx)}
    s = set()
    for j in range(len(to.tc_type)):
        for x in to.tc_simIdxAll[j]:
            k = g2l.get(x, -1)
            if k >= 0:
                s.add(k)
    roff.append(s)
fin.Close()
fo.Close()

lstoth = []
for e in EV:
    s = set()
    for sa in e['lst_other']:
        s.update(sa)
    lstoth.append(s)


def report(bands, simkey, rowkey, title):
    print('=' * 104)
    print(title)
    print('=' * 104)
    nb = len(bands)
    den = [0] * nb
    for e in EV:
        for k in range(len(e['incut'])):
            if e['incut'][k]:
                den[bnd(e[simkey][k], bands)] += 1
    # LST operating points
    for nm, key in (('LST pre-clean', 'lst_pre'), ('LST delivered', 'lst_post')):
        rows = [0] * nb
        fake = [0] * nb
        marg = [0] * nb
        for ei, e in enumerate(EV):
            covered = [set() for _ in range(nb)]
            for r in e[key]:
                b = bnd(r[1] if rowkey == 'eta' else r[0], bands)
                rows[b] += 1
                fake[b] += r[2]
                covered[b].update(r[3])
            for k in range(len(e['incut'])):
                if not e['incut'][k]:
                    continue
                b = bnd(e[simkey][k], bands)
                if k in covered[b] and k not in lstoth[ei]:
                    marg[b] += 1
        print('  %-14s %s' % (nm, ' | '.join(
            '%s rows %6.1f fake %.4f MARG %5.2f' % (bands[i][0], rows[i] / NEV,
                                                    fake[i] / float(rows[i]) if rows[i] else 0,
                                                    marg[i] / NEV) for i in range(nb))))
    print('  %-14s %s' % ('(denominator)', ' | '.join('%s %.1f in-cut sims/evt' %
                                                      (bands[i][0], den[i] / NEV) for i in range(nb))))
    print()
    print('  OURS, scanning the stage-B margin (MARG = in-cut sims this class covers that')
    print('  our pipeline WITHOUT it (r_off) does not):')
    hdr = '  %-6s' % 'T'
    for i in range(nb):
        hdr += '%22s' % (bands[i][0] + ' rows/fake/MARG')
    print(hdr + '%10s' % 'MARG tot')
    for T in [2, 3, 4, 5, 6, 6.5, 7, 7.5, 8, 8.5, 9, 9.5, 10, 11]:
        rows = [0] * nb
        fake = [0] * nb
        marg = [0] * nb
        for ei, e in enumerate(EV):
            covered = [set() for _ in range(nb)]
            for r in e['ours']:
                if r[2] < T:
                    continue
                b = bnd(r[1] if rowkey == 'eta' else r[0], bands)
                rows[b] += 1
                fake[b] += r[5]
                covered[b].update(r[6])
            for k in range(len(e['incut'])):
                if not e['incut'][k]:
                    continue
                b = bnd(e[simkey][k], bands)
                if k in covered[b] and k not in roff[ei]:
                    marg[b] += 1
        ln = '  %-6.1f' % T
        for i in range(nb):
            ln += '%22s' % ('%6.1f %.4f %5.2f' % (rows[i] / NEV,
                                                  fake[i] / float(rows[i]) if rows[i] else 0,
                                                  marg[i] / NEV))
        print(ln + '%10.2f' % (sum(marg) / NEV))
    print()


report(REG, 'sim_eta', 'eta', 'MARGINAL VALUE BY ETA REGION')
report(PTB, 'sim_pt', 'pt', 'MARGINAL VALUE BY pT BAND')
print('REFERENCE: our pipeline with the pT3 class OFF (r_off) = eff .77432 = 58.8/75.9 sims/evt.')
print('           P25BASE (LST pT3 rows carried) = eff .81303 = 61.7/75.9.')
print('           => the class must be worth +2.94 in-cut sims/evt to hold the gate.')
