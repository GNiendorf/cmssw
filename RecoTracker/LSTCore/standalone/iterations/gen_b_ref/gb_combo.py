#!/usr/bin/env python3
"""GEN-B TASK 2 (offline) -- does the T3's OWN quality add anything the attach head lacks?

The attach head scores PAIR COMPATIBILITY. For a bare-T3 target its one object-quality
slot (feature 11 chainGateLogit) is hard-wired to 0 (PixelAttach.cc makeT3Pre), so the
head has NO input at all describing whether the 3-layer object is real. The T3's own DNN
scores are exactly that input and are already in the ntuple.

This fits a logistic combination of (attach logit, t3 fake/prompt/displaced scores, plus
a few cheap conditioned geometry terms) on an EVENT-SPLIT train half and reports the
operating curve on the held-out half, against the logit-alone curve. Same axes as the
campaign: rows/evt, fake fraction, in-cut sim coverage, UNIQUE coverage against R'.
"""
import numpy as np
import ROOT

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
G = S + 'gen_b_ref/'
PTCUT = 0.9

# background R' (our pipeline with the pT3 class deleted)
f = ROOT.TFile.Open(S + 't3attach_ref/t_r_off.root')
t = f.Get('tree')
NEV = t.GetEntries()
RP, INCUT = set(), set()
for i in range(NEV):
    t.GetEntry(i)
    sa = t.tc_simIdxAll
    spt = list(t.sim_pt)
    inc = set(s for s in range(len(spt)) if spt[s] > PTCUT)
    INCUT |= set((i, s) for s in inc)
    for j in range(len(list(t.tc_type))):
        for s in sa[j]:
            if s in inc:
                RP.add((i, s))
f.Close()

ev, lg, pt, eta, fk, pr, ds, t3pt, t3r, isf = [], [], [], [], [], [], [], [], [], []
sims = []
with open(G + 'pt3cmp300.txt') as fh:
    for ln in fh:
        if ln[0] != 'O':
            continue
        w = ln.split()
        e = int(w[1])
        ev.append(e); lg.append(float(w[4])); pt.append(float(w[5])); eta.append(float(w[6]))
        fk.append(float(w[7])); pr.append(float(w[8])); ds.append(float(w[9]))
        t3pt.append(float(w[10])); t3r.append(float(w[11]))
        isf.append(1 if int(w[12]) == 0 else 0)
        sims.append(tuple((e, int(x)) for x in w[14:]))
ev = np.array(ev); lg = np.array(lg); pt = np.array(pt); eta = np.array(eta)
fk = np.array(fk); pr = np.array(pr); ds = np.array(ds)
t3pt = np.array(t3pt); t3r = np.array(t3r); isf = np.array(isf)
print('deliveries %d (%.1f/evt), fake fraction %.4f' % (len(ev), len(ev) / NEV, isf.mean()))

X = np.column_stack([lg,
                     np.log(np.clip(fk, 1e-4, 1)),
                     np.log(np.clip(pr, 1e-4, 1)),
                     np.log(np.clip(ds, 1e-4, 1)),
                     np.log10(np.clip(pt, 1e-3, None)),
                     np.log10(np.clip(t3pt, 1e-3, None)),
                     np.log10(np.clip(t3r, 1e-3, None)),
                     np.abs(eta)])
NAMES = ['logit', 'ln fakeScore', 'ln promptScore', 'ln dispScore', 'log10 plsPt',
         'log10 t3Pt', 'log10 t3R', '|eta|']
y = 1 - isf   # 1 = the delivery matches SOME sim
tr = (ev % 2 == 0)
te = ~tr
mu, sd = X[tr].mean(0), X[tr].std(0) + 1e-9
Xs = (X - mu) / sd

from sklearn.linear_model import LogisticRegression
from sklearn.metrics import roc_auc_score
clf = LogisticRegression(max_iter=2000, C=1.0)
clf.fit(Xs[tr], y[tr])
sc = clf.decision_function(Xs)
print('TEST AUC  logit alone %.4f | combination %.4f'
      % (roc_auc_score(y[te], lg[te]), roc_auc_score(y[te], sc[te])))
print('coefficients (standardised):')
for n, c in sorted(zip(NAMES, clf.coef_[0]), key=lambda z: -abs(z[1])):
    print('   %-16s %+8.3f' % (n, c))

NTE = len(set(ev[te].tolist()))
rpte = set(k for k in RP if k[0] % 2 == 1)


def curve(name, score, mask):
    print('\n  %s (test half, %d evts)' % (name, NTE))
    print('  %10s%10s%10s%10s%10s' % ('rows/evt', 'fakefrc', 'fakes/evt', 'cov/evt', 'UNIQ'))
    s = score[mask]
    order = np.argsort(-s)
    idx = np.nonzero(mask)[0][order]
    for target in (100, 150, 200, 250, 300, 400, 600, 900):
        k = int(target * NTE)
        if k > len(idx):
            break
        sel = idx[:k]
        nf = isf[sel].sum()
        cov = set()
        for j in sel:
            for q in sims[j]:
                if q in INCUT:
                    cov.add(q)
        u = len(cov - rpte) / NTE
        print('  %10.1f%10.4f%10.2f%10.2f%10.3f'
              % (k / NTE, nf / k, nf / NTE, len(cov) / NTE, u))


curve('RANK BY ATTACH LOGIT (what ships)', lg, te)
curve('RANK BY LOGIT + T3 QUALITY', sc, te)
