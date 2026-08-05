#!/usr/bin/env python3
"""GEN-B -- offline optimiser over the pT3-class delivery rule space, calibrated to the
end-to-end scoreboard. Vectorised.

INPUTS
  gen_b_ref/pt3cmp300.txt  per delivery: attach logit, T3 DNN scores, matched sims
  gen_b_ref/diag_*.txt     per delivery, in SWEEP ORDER: ownership units already claimed
                           by already-delivered TCs (nPre) and by earlier deliveries (nCur)

CALIBRATION (fitted on runs whose scoreboard is known, printed so it is auditable):
  eff  = eff(r_off) + UNIQ / D
  fake = (F_off + F_new) / (nTC_off + rows)
dup is NOT modelled (adding one duplicate row flags TWO rows, and the -RPS pLS retirement
removes duplicates at the same time); rows/evt is its proxy and every surviving point is
confirmed end to end.
"""
import os
import numpy as np
import ROOT

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
G = S + 'gen_b_ref/'
PTCUT = 0.9
EFF_OFF, NTC_OFF, FAKE_OFF = 0.77432, 1933.2, 0.04905
F_OFF = NTC_OFF * FAKE_OFF


def covset(path, sel=None):
    f = ROOT.TFile.Open(path)
    t = f.Get('tree')
    n = t.GetEntries()
    out, incut = set(), set()
    for i in range(n):
        t.GetEntry(i)
        ty = list(t.tc_type)
        sa = t.tc_simIdxAll
        spt = list(t.sim_pt)
        inc = set(s for s in range(len(spt)) if spt[s] > PTCUT)
        incut |= set((i, s) for s in inc)
        for j in range(len(ty)):
            if sel is not None and not sel(ty[j]):
                continue
            for s in sa[j]:
                if s in inc:
                    out.add((i, s))
    f.Close()
    return out, incut, n


RP, INCUT, NEV = covset(S + 't3attach_ref/t_r_off.root')
P25T5, _, _ = covset(S + 't3attach_ref/t_P25BASE.root', lambda ty: ty == 5)
U_LST = len(P25T5 - RP) / NEV
D = U_LST / (0.81303 - EFF_OFF)
UNIQ_ELIG = INCUT - RP
print('CALIBRATION D=%.1f U_LST=%.3f | unique-eligible sims %.2f/evt' % (D, U_LST, len(UNIQ_ELIG) / NEV))

rec = {}
with open(G + 'pt3cmp300.txt') as fh:
    for ln in fh:
        if ln[0] != 'O':
            continue
        w = ln.split()
        e = int(w[1])
        us = [(e, int(x)) for x in w[14:]]
        us = [k for k in us if k in UNIQ_ELIG]
        rec[(e, int(w[2]))] = (int(w[12]) == 0, float(w[7]), float(w[8]), float(w[9]), us)
print('universe %d deliveries' % len(rec))


def load(path):
    e_, t_, lo_, np_, nc_ = [], [], [], [], []
    with open(path) as fh:
        for ln in fh:
            if ln[0] != 'D':
                continue
            w = ln.split()
            e_.append(int(w[1])); t_.append(int(w[2])); lo_.append(float(w[4]))
            np_.append(int(w[5])); nc_.append(int(w[6]))
    return (np.array(e_), np.array(t_), np.array(lo_, np.float32),
            np.array(np_, np.int8), np.array(nc_, np.int8))


def prep(path):
    e, t, lo, npr, ncr = load(path)
    n = len(e)
    isf = np.zeros(n, np.int8)
    fkS = np.zeros(n, np.float32); prS = np.zeros(n, np.float32); dsS = np.zeros(n, np.float32)
    simid, simrow = [], []
    ids = {}
    for j in range(n):
        r = rec.get((e[j], t[j]))
        if r is None:
            continue
        isf[j] = 1 if r[0] else 0
        fkS[j], prS[j], dsS[j] = r[1], r[2], r[3]
        for k in r[4]:
            simid.append(ids.setdefault(k, len(ids)))
            simrow.append(j)
    return dict(e=e, lo=lo, npr=npr, ncr=ncr, isf=isf, fk=fkS, pr=prS, ds=dsS,
                simid=np.array(simid, np.int32), simrow=np.array(simrow, np.int64),
                nd=int(e.max()) + 1, n=n)


def ev(d, mask):
    n = int(mask.sum())
    F = float(d['isf'][mask].sum()) / d['nd']
    R = n / d['nd']
    if len(d['simrow']):
        U = len(np.unique(d['simid'][mask[d['simrow']]])) / d['nd']
    else:
        U = 0.0
    return R, F, U, EFF_OFF + U / D, (F_OFF + F) / (NTC_OFF + R)


if __name__ == '__main__':
    res = []
    for path, tag in ((G + 'dm.txt', 'MDnoRD'), (G + 'dmr.txt', 'MD_RD'), (G + 'dh.txt', 'HIT_RD')):
        if not os.path.exists(path):
            continue
        d = prep(path)
        print('%s: %d rows = %.1f/evt over %d evts' % (tag, d['n'], d['n'] / d['nd'], d['nd']))
        for th in (4, 5, 5.5, 6, 6.5, 7, 7.5, 8):
            mth = d['lo'] >= th
            for npre in (99, 6, 5, 4, 3, 2, 1):
                mp = mth & (d['npr'] < npre)
                for ncur in (99, 4, 3, 2, 1):
                    mc = mp & (d['ncr'] < ncur)
                    for qm, qcs in ((0, [0.]), (1, [0.02, 0.05, 0.1, 0.2, 0.35, 0.5]),
                                    (2, [0.5, 0.3, 0.2, 0.1, 0.05, 0.02]),
                                    (3, [0.1, 0.2, 0.35, 0.5, 0.7])):
                        for qc in qcs:
                            if qm == 0:
                                m = mc
                            elif qm == 1:
                                m = mc & (d['pr'] >= qc)
                            elif qm == 2:
                                m = mc & (d['fk'] <= qc)
                            else:
                                m = mc & ((d['pr'] + d['ds']) >= qc)
                            R, F, U, pe, pf = ev(d, m)
                            res.append((pe, pf, R, F, U, tag, th, npre, ncur, qm, qc))
    sel = [o for o in res if o[0] >= 0.8100 and o[1] <= 0.0480 and o[2] <= 300]
    sel.sort(key=lambda o: -o[0])
    print('\n%-8s%6s%5s%5s%4s%7s%9s%9s%8s%9s%9s' %
          ('tag', 'th', 'pre', 'cur', 'qm', 'qcut', 'rows/ev', 'fakes/ev', 'UNIQ', 'predEff', 'predFake'))
    for o in sel[:50]:
        pe, pf, R, F, U, tag, th, npre, ncur, qm, qc = o
        print('%-8s%6g%5d%5d%4d%7g%9.1f%9.2f%8.3f%9.5f%9.5f' % (tag, th, npre, ncur, qm, qc, R, F, U, pe, pf))
    print('\nscanned %d, surviving %d' % (len(res), len(sel)))
    np.save(G + 'opt_res.npy', np.array([o[:5] for o in res]))
