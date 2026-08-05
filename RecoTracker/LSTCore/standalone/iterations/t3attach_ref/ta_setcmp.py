#!/usr/bin/env python3
"""Delivery-SET comparison (coordinator's matched-count experiment, step 3).

Compares the pT3-class delivery set of one or more prototype runs against LST's own
pT3 rows, row by row, instead of only through the aggregate rates.

The pT3-class set is exactly `tc_type == 5`:
  * in a prototype run with -RT3 1 every carried LST pT3 row is retired, so the only
    type-5 rows in the output are OUR bare-T3 deliveries;
  * in base300_identity.root (LST passed through the same writer) the type-5 rows are
    LST's own pT3s.
So the same selection names the two sets that are being compared. Identical matching
definitions on both sides -- tc_simIdxAll for coverage, tc_isFake for purity -- so the
numbers are directly comparable even where they are proxies for the official ones.

Usage: ta_setcmp.py <tag> [<tag> ...]      (tags are t_<tag>.root in t3attach_ref/)
"""
import sys
import ROOT

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
T = S + 't3attach_ref/'
PTCUT = 0.9


def analyse(path, label):
    f = ROOT.TFile.Open(path)
    t = f.Get('tree')
    n_evt = t.GetEntries()
    n_set = 0          # rows in the pT3-class set
    n_set_fake = 0     # of those, tc_isFake
    n_set_dup = 0      # of those, tc_isDuplicate
    n_other = 0
    n_other_fake = 0
    sims_in = 0        # in-cut sims (pt > PTCUT) seen
    cov_set = 0        # in-cut sims covered by the set
    cov_other = 0      # in-cut sims covered by anything else
    cov_both = 0
    uniq_set = 0       # covered by the set and by NOTHING else  <== the class's own value
    uniq_other = 0
    for i in range(n_evt):
        t.GetEntry(i)
        ty = list(t.tc_type)
        fk = list(t.tc_isFake)
        dp = list(t.tc_isDuplicate)
        simall = t.tc_simIdxAll
        spt = list(t.sim_pt)
        bySet, byOther = set(), set()
        for j, ty_j in enumerate(ty):
            isSet = (ty_j == 5)
            if isSet:
                n_set += 1
                n_set_fake += 1 if fk[j] else 0
                n_set_dup += 1 if dp[j] else 0
            else:
                n_other += 1
                n_other_fake += 1 if fk[j] else 0
            tgt = bySet if isSet else byOther
            for s in simall[j]:
                tgt.add(s)
        for s in range(len(spt)):
            if spt[s] <= PTCUT:
                continue
            sims_in += 1
            a, b = s in bySet, s in byOther
            cov_set += a
            cov_other += b
            cov_both += (a and b)
            uniq_set += (a and not b)
            uniq_other += (b and not a)
    f.Close()
    d = float(n_evt)
    return dict(label=label, evts=n_evt, rows=n_set, rows_evt=n_set / d,
                fakefrac=(n_set_fake / n_set if n_set else 0.0),
                dupfrac=(n_set_dup / n_set if n_set else 0.0),
                other_rows_evt=n_other / d,
                other_fakefrac=(n_other_fake / n_other if n_other else 0.0),
                cov=cov_set / d, uniq=uniq_set / d, both=cov_both / d,
                sims=sims_in / d)


rows = [analyse(S + 'prototype/base300_identity.root', 'LST pT3 (identity)')]
for tag in sys.argv[1:]:
    rows.append(analyse(T + 't_%s.root' % tag, tag))

print('pT3-CLASS DELIVERY SET (tc_type==5), 300 evts, sims with pt > %.1f' % PTCUT)
print('%-20s%9s%9s%9s%10s%9s%9s%9s' %
      ('set', 'rows/evt', 'fakefrc', 'dupfrc', 'simscov', 'uniq', 'both', 'otherFR'))
for r in rows:
    print('%-20s%9.1f%9.4f%9.4f%10.1f%9.1f%9.1f%9.4f' %
          (r['label'], r['rows_evt'], r['fakefrac'], r['dupfrac'],
           r['cov'], r['uniq'], r['both'], r['other_fakefrac']))
print('\n  rows/evt  size of the pT3-class delivery set')
print('  fakefrc   fraction of THOSE rows that are fakes  <== the building-quality number')
print('  dupfrc    fraction of those rows flagged duplicate')
print('  simscov   in-cut sims/evt the set covers (alone or not)')
print('  uniq      in-cut sims/evt the set covers that NO other TC covers <== its own value')
print('  both      in-cut sims/evt covered by the set AND by something else')
print('  otherFR   fake fraction of every NON-type-5 row, for context')
