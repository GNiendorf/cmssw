#!/usr/bin/env python3
"""GEN-B -- the achievable (rows, fakes, UNIQ) frontier for
   PURITY GATE  x  GRADED-OWNERSHIP RANKING.

The hand rules use the ownership-map count as a BINARY VETO (kill if >= N units already
claimed). The uniqueness-headroom test showed that count is the second strongest predictor
of whether a delivery adds a track -- but graded, together with the attach logit and the
T3's own quality. This measures what a GRADED use of the same, already-computed quantity
buys, on a held-out event half, under a hard purity floor (attach logit >= theta and
t3_promptScore >= q) so the fake budget stays payable.

Everything here is per-object or ownership-map derived. No pairwise candidate loop, no
proximity term.
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

rec = {}
with open(G + 'pt3cmp300.txt') as fh:
    for ln in fh:
        if ln[0] != 'O':
            continue
        w = ln.split()
        e = int(w[1])
        rec[(e, int(w[2]))] = (int(w[12]) == 0, float(w[5]), float(w[6]), float(w[7]),
                               float(w[8]), float(w[9]),
                               [(e, int(x)) for x in w[14:] if (e, int(x)) in UE])

E, F, ISF, UID = [], [], [], []
ids = {}
with open(G + 'dmr.txt') as fh:
    for ln in fh:
        if ln[0] != 'D':
            continue
        w = ln.split()
        e = int(w[1]); t3 = int(w[2])
        r = rec.get((e, t3))
        if r is None:
            continue
        isf, plspt, eta, fk, pr, ds = r[:6]
        E.append(e)
        F.append([float(w[4]), float(w[5]), float(w[6]), np.log(max(pr, 1e-4)),
                  np.log(max(fk, 1e-4)), abs(eta), np.log10(max(plspt, 1e-3))])
        ISF.append(1 if isf else 0)
        UID.append([ids.setdefault(k, len(ids)) for k in r[6]])
X = np.array(F, np.float32)
E = np.array(E); ISF = np.array(ISF, np.int32)
y = np.array([1 if u else 0 for u in UID], np.int8)
NAMES = ['logit', 'nPre', 'nCur', 'lnPrompt', 'lnFake', '|eta|', 'lgPlsPt']
tr, te = (E % 2 == 0), (E % 2 == 1)
NTE = len(set(E[te].tolist()))
mu, sd = X[tr].mean(0), X[tr].std(0) + 1e-9
from sklearn.linear_model import LogisticRegression
lr = LogisticRegression(max_iter=4000, class_weight='balanced').fit((X[tr] - mu) / sd, y[tr])
sc = lr.decision_function((X - mu) / sd)
print('graded-ownership uniqueness score, standardised coefficients:')
for n, c in sorted(zip(NAMES, lr.coef_[0]), key=lambda z: -abs(z[1])):
    print('   %-10s %+8.3f' % (n, c))

print('\n%6s%6s%10s%10s%9s%9s' % ('theta', 'qP', 'rows/evt', 'fakes/ev', 'UNIQ', 'predEff'))
D = 79.3
best = []
for th in (4, 5, 6, 6.5, 7, 7.5):
    for q in (-1, 0.05, 0.1, 0.2, 0.35, 0.5):
        gate = te & (X[:, 0] >= th) & (np.exp(X[:, 3]) >= q)
        idx = np.nonzero(gate)[0]
        if len(idx) == 0:
            continue
        order = idx[np.argsort(-sc[idx])]
        for target in (120, 150, 200, 260):
            k = int(target * NTE)
            if k > len(order):
                continue
            sel = order[:k]
            u = set()
            for j in sel:
                u.update(UID[j])
            U = len(u) / NTE
            fk = ISF[sel].sum() / NTE
            best.append((U, fk, k / NTE, th, q, 0.77432 + U / D))
            print('%6g%6g%10.1f%10.2f%9.3f%9.5f' % (th, q, k / NTE, fk, U, 0.77432 + U / D))
print('\nBEST BY FAKE BUDGET (fakes/evt <= .04636*rows - 4.98, the P25BASE-relative rule):')
ok = [b for b in best if b[1] <= 0.04636 * b[2] - 4.98]
ok.sort(key=lambda b: -b[0])
for b in ok[:10]:
    print('  U %.3f fakes %.2f rows %.1f theta %g qP %g predEff %.5f' % b)
if not ok:
    print('  NONE -- the fake budget is not payable by this family')
    ok2 = sorted(best, key=lambda b: -(b[0] - 0.02 * max(0, b[1] - (0.04636 * b[2] - 4.98))))
    for b in ok2[:10]:
        print('  U %.3f fakes %.2f (budget %.2f) rows %.1f th %g qP %g predEff %.5f'
              % (b[0], b[1], 0.04636 * b[2] - 4.98, b[2], b[3], b[4], b[5]))
