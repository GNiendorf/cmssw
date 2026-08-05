#!/usr/bin/env python3
"""GEN-B -- HOW MUCH HEADROOM IS THERE IN RANKING FOR *UNIQUENESS*?

The campaign's whole difficulty is that the attach logit ranks pair COMPATIBILITY while
efficiency only pays for deliveries that cover a sim NOTHING ELSE covers. This asks the
headroom question directly: train, on a held-out event split, a model whose TARGET is
"this delivery covers an in-cut sim that the pT3-less pipeline (r_off) misses", from
features that exist at inference time --
   attach logit, t3 fake/prompt/displaced score, |eta|, pLS pt, t3 pt, t3 radius,
   and the two OWNERSHIP-MAP counts nPre / nCur (units already claimed by already-
   delivered TCs / by earlier deliveries), which are exactly what the dedup sweep knows.
If the (rows, UNIQ) curve of that model beats the best hand rule, a learned uniqueness
head is the answer; if it does not, the information simply is not there and the answer
has to be structural.
"""
import numpy as np
import ROOT

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
G = S + 'gen_b_ref/'
PTCUT = 0.9

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
UE = INCUT - RP
print('unique-eligible in-cut sims %.2f/evt' % (len(UE) / NEV))

rec = {}
with open(G + 'pt3cmp300.txt') as fh:
    for ln in fh:
        if ln[0] != 'O':
            continue
        w = ln.split()
        e = int(w[1])
        us = [(e, int(x)) for x in w[14:]]
        rec[(e, int(w[2]))] = (int(w[12]) == 0, float(w[5]), float(w[6]), float(w[7]),
                               float(w[8]), float(w[9]), float(w[10]), float(w[11]),
                               [k for k in us if k in UE])

E, LO, NPR, NCR = [], [], [], []
FEA, ISF, UID = [], [], []
ids = {}
with open(G + 'dmr.txt') as fh:       # MD granularity, pixel dedup on -- the shipping shape
    for ln in fh:
        if ln[0] != 'D':
            continue
        w = ln.split()
        e = int(w[1]); t3 = int(w[2])
        r = rec.get((e, t3))
        if r is None:
            continue
        isf, plspt, eta, fk, pr, ds, t3pt, t3r = r[:8]
        E.append(e); LO.append(float(w[4])); NPR.append(int(w[5])); NCR.append(int(w[6]))
        FEA.append([float(w[4]), np.log(max(fk, 1e-4)), np.log(max(pr, 1e-4)),
                    np.log(max(ds, 1e-4)), abs(eta), np.log10(max(plspt, 1e-3)),
                    np.log10(max(t3pt, 1e-3)), np.log10(max(t3r, 1e-3)),
                    float(w[5]), float(w[6])])
        ISF.append(1 if isf else 0)
        UID.append([ids.setdefault(k, len(ids)) for k in r[8]])
X = np.array(FEA, np.float32)
E = np.array(E); ISF = np.array(ISF)
y = np.array([1 if u else 0 for u in UID], np.int8)
print('rows %d (%.1f/evt) | unique-carrying rows %d (%.2f/evt)'
      % (len(X), len(X) / NEV, y.sum(), y.sum() / NEV))
NAMES = ['logit', 'lnFake', 'lnPrompt', 'lnDisp', '|eta|', 'lgPlsPt', 'lgT3Pt', 'lgT3R',
         'nPre', 'nCur']

tr = (E % 2 == 0)
te = ~tr
NTE = len(set(E[te].tolist()))
mu, sd = X[tr].mean(0), X[tr].std(0) + 1e-9
Xs = (X - mu) / sd
from sklearn.linear_model import LogisticRegression
from sklearn.ensemble import HistGradientBoostingClassifier
lr = LogisticRegression(max_iter=3000, class_weight='balanced').fit(Xs[tr], y[tr])
gb = HistGradientBoostingClassifier(max_iter=200, max_depth=6, learning_rate=0.1,
                                    random_state=0).fit(X[tr], y[tr])
scores = {'attach logit': X[:, 0],
          'logistic(uniq)': lr.decision_function(Xs),
          'GBM(uniq)': gb.predict_proba(X)[:, 1]}
print('logistic coefficients:')
for n, c in sorted(zip(NAMES, lr.coef_[0]), key=lambda z: -abs(z[1])):
    print('   %-10s %+8.3f' % (n, c))

rp_te = set()
print('\n%-16s%10s%10s%10s%10s' % ('rank by', 'rows/evt', 'fakes/ev', 'UNIQ', 'fakefrc'))
for nm, sc in scores.items():
    idx = np.nonzero(te)[0]
    order = idx[np.argsort(-sc[te])]
    for target in (100, 150, 200, 300, 500):
        k = int(target * NTE)
        if k > len(order):
            break
        sel = order[:k]
        u = set()
        for j in sel:
            u.update(UID[j])
        print('%-16s%10.1f%10.2f%10.3f%10.4f'
              % (nm if target == 100 else '', k / NTE, ISF[sel].sum() / NTE,
                 len(u) / NTE, ISF[sel].mean()))
    print()
