#!/usr/bin/env python3
"""GEN-B TASK 1 -- the CEILING question.

Fix the background R = the in-cut sims covered by every NON-pT3-class row of P25BASE
(our chains + carried pT5 + surviving bare pLS). Then:
  LST-UNIQUE = sims covered by P25BASE's carried LST pT3 rows and NOT in R
             = exactly the efficiency the pT3 class buys us today.
  OUR-CEILING = sims covered by ANY delivery in the -PT3C dump (theta = -6, i.e. the
                WHOLE candidate universe our attach can ever produce) and NOT in R.
  RECALL      = |LST-UNIQUE reachable by some delivery| / |LST-UNIQUE|.
Broken down per eta region and per pT band, so a regional failure cannot hide.
"""
import sys
import ROOT

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
PTCUT = 0.9
ETA_REG = [('barrel', 0.0, 1.1), ('transit', 1.1, 1.7), ('endcap', 1.7, 99.)]
PT_BAND = [('0.9-1.5', 0.9, 1.5), ('1.5-3', 1.5, 3.0), ('3-10', 3.0, 10.0), ('10+', 10.0, 1e9)]

NDUMP = int(sys.argv[2]) if len(sys.argv) > 2 else 0
f = ROOT.TFile.Open(S + 't3attach_ref/t_P25BASE.root')
t = f.Get('tree')
NEVB = t.GetEntries()
R, LSTU, INCUT = set(), set(), set()
sim_pt, sim_eta = {}, {}
for i in range(NEVB):
    t.GetEntry(i)
    ty = list(t.tc_type)
    sa = t.tc_simIdxAll
    spt = list(t.sim_pt)
    seta = list(t.sim_eta)
    inc = set(s for s in range(len(spt)) if spt[s] > PTCUT)
    for s in inc:
        INCUT.add((i, s)); sim_pt[(i, s)] = spt[s]; sim_eta[(i, s)] = seta[s]
    a, b = set(), set()
    for j in range(len(ty)):
        tgt = a if ty[j] == 5 else b
        for s in sa[j]:
            if s in inc:
                tgt.add(s)
    R |= set((i, s) for s in b)
    LSTU |= set((i, s) for s in (a - b))
f.Close()

path = sys.argv[1] if len(sys.argv) > 1 else S + 'gen_b_ref/pt3cmp300.txt'
covall, cov_by_th = set(), {}
best_logit = {}
nev = 0
orecs = []
lrecs = []
with open(path) as fh:
    for ln in fh:
        if ln[0] == 'S':
            nev = max(nev, int(ln.split()[1]) + 1)
            continue
        if ln[0] == 'L':
            w = ln.split()
            e = int(w[1])
            lrecs.append((e, int(w[6]), [(e, int(x)) for x in w[8:]]))
            continue
        if ln[0] != 'O':
            continue
        w = ln.split()
        e = int(w[1]); lo = float(w[4])
        orecs.append((e, lo, int(w[12]), [(e, int(x)) for x in w[14:]]))
        for x in w[14:]:
            k = (e, int(x))
            if k in INCUT:
                covall.add(k)
                if lo > best_logit.get(k, -1e9):
                    best_logit[k] = lo
if NDUMP == 0:
    NDUMP = nev
DUMPEV = set(range(NDUMP))


def per(sset):
    return len(sset) / NDUMP


LSTUd = set(k for k in LSTU if k[0] in DUMPEV)
Rd = set(k for k in R if k[0] in DUMPEV)
INCUTd = set(k for k in INCUT if k[0] in DUMPEV)
OURC = covall - Rd
reach = LSTUd & covall

print('dump events %d | in-cut sims %.1f/evt' % (NDUMP, per(INCUTd)))
print('LST-UNIQUE (what the pT3 class buys today) %.2f/evt' % per(LSTUd))
print('OUR CEILING (any delivery, any threshold)  %.2f/evt' % per(OURC))
print('RECALL of LST-UNIQUE by our candidate universe %.2f/evt = %.1f%%'
      % (per(reach), 100.0 * len(reach) / max(len(LSTUd), 1)))
print('  of the reached ones, best-delivery logit percentiles:')
import numpy as np
bl = np.array([best_logit[k] for k in reach])
if len(bl):
    for q in (5, 25, 50, 75, 95):
        print('     p%-3d %7.3f' % (q, np.percentile(bl, q)))
print('OUR-CEILING sims LST does not uniquely have: %.2f/evt' % per(OURC - LSTUd))


def table(title, bins, key):
    print('\n  %s' % title)
    print('  %-9s%10s%10s%10s%10s' % ('bin', 'incut', 'LSTuniq', 'ourCeil', 'recall%'))
    for bn, lo, hi in bins:
        sel = lambda k: lo <= abs(key(k)) < hi
        ic = set(k for k in INCUTd if sel(k))
        lu = set(k for k in LSTUd if sel(k))
        oc = set(k for k in OURC if sel(k))
        rc = set(k for k in reach if sel(k))
        print('  %-9s%10.1f%10.2f%10.2f%10.1f' %
              (bn, per(ic), per(lu), per(oc), 100.0 * len(rc) / max(len(lu), 1)))


table('PER ETA REGION', ETA_REG, lambda k: sim_eta[k])
table('PER pT BAND', PT_BAND, lambda k: sim_pt[k])


# ---- LST PRE-CLEAN candidate set against the SAME background R ------------------------
lcov = set()
lrows = lfake = 0
for e, ntot, ss in lrecs:
    lrows += 1
    lfake += 1 if ntot == 0 else 0
    for k in ss:
        if k in INCUTd:
            lcov.add(k)
print('\n=== LST PRE-CLEAN CANDIDATE SET vs the SAME background R ===')
print('  rows/evt %.1f | fakefrc %.4f | cov %.2f/evt | UNIQ %.2f/evt'
      % (lrows / NDUMP, lfake / max(lrows, 1), per(lcov), per(lcov - Rd)))

# ---- OUR threshold curve against the SAME background R --------------------------------
print('\n=== OUR THRESHOLD CURVE vs the SAME background R ===')
print('%8s%10s%10s%10s%10s%10s' % ('theta', 'rows/evt', 'fakefrc', 'cov/evt', 'UNIQ/evt', '%ceiling'))
CEIL = per(OURC)
for th in (-6, 0, 2, 4, 5, 6, 6.5, 7, 7.5, 8, 8.5, 9, 9.5, 10, 10.5, 11):
    nrow = nfk = 0
    cov = set()
    for e, lo, ntot, ss in orecs:
        if lo < th:
            continue
        nrow += 1
        nfk += 1 if ntot == 0 else 0
        for k in ss:
            if k in INCUTd:
                cov.add(k)
    u = per(cov - Rd)
    print('%8.1f%10.1f%10.4f%10.2f%10.2f%10.1f' % (th, nrow / NDUMP, nfk / max(nrow, 1),
                                                   per(cov), u, 100.0 * u / CEIL))
print('LST post-clean operating point: 151.7 rows/evt, fake .0345, UNIQ %.2f/evt' % per(LSTUd))
