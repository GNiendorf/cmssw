#!/usr/bin/env python3
"""GEN-B -- the number that actually drives efficiency: UNIQUE in-cut sim coverage of the
pT3 class (sims the class covers that NOTHING else in the same output covers).

Three measurements, all with identical definitions (tc_simIdxAll, in-cut = accepted sim
with pt > 0.9):
  1. LST's REAL operating point: the INPUT LST ntuple itself, tc_type == 5 (post-clean
     pT3 rows as they enter the TC collection) against everything else in that file.
  2. OUR pre-dedup set at -AT3 6: t_d_none.root, tc_isChain == 3, against everything else.
  3. The THRESHOLD CURVE: our -PT3C dump (all deliveries with their logit) against the
     "rest" set taken from t_d_none.root. Approximate above theta 6 (fewer carried pLS
     rows get retired there, so the rest is slightly larger than modelled) -- used for
     RANKING thresholds, never as a final number; the winner is confirmed end to end.
"""
import sys
import ROOT

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
PTCUT = 0.9


def scan(path, sel):
    """sel(type, isChain) -> True if the row belongs to the class under test."""
    f = ROOT.TFile.Open(path)
    t = f.Get('tree')
    n = t.GetEntries()
    hasChain = 'tc_isChain' in [b.GetName() for b in t.GetListOfBranches()]
    rows = fakes = 0
    cov, rest, uniq = set(), set(), set()
    incut = 0
    for i in range(n):
        t.GetEntry(i)
        ty = list(t.tc_type)
        ch = list(t.tc_isChain) if hasChain else []
        if len(ch) != len(ty):
            ch = [0] * len(ty)
        fk = list(t.tc_isFake)
        sa = t.tc_simIdxAll
        spt = list(t.sim_pt)
        nacc = len(spt)
        inc = set(s for s in range(nacc) if spt[s] > PTCUT)
        incut += len(inc)
        a, b = set(), set()
        for j in range(len(ty)):
            mine = sel(ty[j], ch[j])
            if mine:
                rows += 1
                fakes += 1 if fk[j] else 0
            tgt = a if mine else b
            for s in sa[j]:
                if s in inc:
                    tgt.add(s)
        cov |= set((i, s) for s in a)
        rest |= set((i, s) for s in b)
        uniq |= set((i, s) for s in (a - b))
    f.Close()
    return dict(n=n, rows=rows / n, fake=fakes / max(rows, 1), cov=len(cov) / n,
                uniq=len(uniq) / n, incut=incut / n, restset=rest, nev=n)


print('=== pT3-CLASS VALUE, IDENTICAL DEFINITIONS ===')
print('%-42s%9s%9s%10s%10s' % ('set', 'rows/evt', 'fakefrc', 'cov/evt', 'UNIQ/evt'))
lst = scan(S + 'LSTNtuple_PU200RelVal_300evt.root', lambda ty, ch: ty == 5)
print('%-42s%9.1f%9.4f%10.2f%10.2f' % ('LST post-clean pT3 (its real WP)', lst['rows'],
                                       lst['fake'], lst['cov'], lst['uniq']))
ours = scan(S + 't3attach_ref/t_d_none.root', lambda ty, ch: ch == 3)
print('%-42s%9.1f%9.4f%10.2f%10.2f' % ('OURS pre-dedup @ -AT3 6', ours['rows'],
                                       ours['fake'], ours['cov'], ours['uniq']))
print('in-cut sims/evt %.1f (LST file) %.1f (ours)' % (lst['incut'], ours['incut']))

# ---- threshold curve for unique coverage --------------------------------------------
rest = ours['restset']
NEV = ours['nev']
path = sys.argv[1] if len(sys.argv) > 1 else S + 'gen_b_ref/pt3cmp300.txt'
sims_by_ev = {}
recs = []
with open(path) as fh:
    for ln in fh:
        if ln[0] != 'O':
            if ln[0] == 'S':
                w = ln.split()
                sims_by_ev.setdefault(int(w[1]), {})[int(w[2])] = float(w[3])
            continue
        w = ln.split()
        recs.append((int(w[1]), float(w[4]), int(w[12]), [int(x) for x in w[14:]]))
NDUMP = max(sims_by_ev) + 1
print('\n=== UNIQUE-COVERAGE THRESHOLD CURVE (dump events %d; "rest" from t_d_none) ===' % NDUMP)
print('%8s%10s%10s%10s%10s' % ('theta', 'rows/evt', 'fakefrc', 'cov/evt', 'UNIQ/evt'))
for th in (-6, 0, 2, 4, 5, 6, 6.5, 7, 7.5, 8, 8.5, 9, 9.5, 10, 10.5, 11):
    nrow = nfk = 0
    cov, uni = set(), set()
    for e, lo, ntot, ss in recs:
        if lo < th:
            continue
        nrow += 1
        nfk += 1 if ntot == 0 else 0
        for s in ss:
            if sims_by_ev[e].get(s, 0.0) > PTCUT:
                cov.add((e, s))
                if (e, s) not in rest:
                    uni.add((e, s))
    print('%8.1f%10.1f%10.4f%10.2f%10.2f' % (th, nrow / NDUMP, nfk / max(nrow, 1),
                                             len(cov) / NDUMP, len(uni) / NDUMP))
