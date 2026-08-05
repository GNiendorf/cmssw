#!/usr/bin/env python3
"""GEN-B -- OFFLINE DEDUP-RULE RANKER, calibrated against the end-to-end scoreboard.

BACKGROUND: R' = the in-cut sims covered by EVERY row of t_r_off.root, i.e. our pipeline
with the pT3 class deleted (-RT3 1 -T3E 0). That is the exact background every candidate
configuration sits on, so
      eff(config)  =  eff(r_off)  +  UNIQ(config) / D
with D the harness efficiency denominator. D is FITTED from two runs whose eff is known
(P25BASE = LST's carried pT3 rows, d_none = our deliveries at theta 6 with no dedup) and
then CHECKED against a third (d_n1), so the ranker's eff predictions are auditable.

Then every candidate rule is scored offline from
  gen_b_ref/pt3cmp300.txt   (per delivery: logit + matched sims)
  gen_b_ref/diag_*.txt      (per delivery: units already claimed by already-delivered TCs
                             [nPre] and by earlier deliveries in the sweep [nCur])
"""
import sys
import os
import ROOT

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
G = S + 'gen_b_ref/'
PTCUT = 0.9


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
DN, _, _ = covset(S + 't3attach_ref/t_d_none.root', lambda ty: ty == 5)
DN1, _, _ = covset(S + 't3attach_ref/t_d_n1.root', lambda ty: ty == 5)
U_LST = len(P25T5 - RP) / NEV
U_DN = len(DN - RP) / NEV
U_DN1 = len(DN1 - RP) / NEV
D = U_LST / (0.81303 - 0.77432)
print('background R\' = t_r_off coverage: %.2f in-cut sims/evt covered, %.1f in-cut/evt'
      % (len(RP) / NEV, len(INCUT) / NEV))
print('U(LST carried pT3) %.3f/evt  -> fitted harness denominator D = %.1f' % (U_LST, D))
print('CHECK d_none : U %.3f -> predicted eff %.5f (measured .81505)' % (U_DN, 0.77432 + U_DN / D))
print('CHECK d_n1   : U %.3f -> predicted eff %.5f (measured .80456)' % (U_DN1, 0.77432 + U_DN1 / D))
print('TARGET: U >= %.3f/evt at <= ~250 rows/evt\n' % U_LST)

# ---------------- load the delivery universe ------------------------------------------
recs = {}   # (evt,t3) -> [logit, isFake, [sims]]
with open(G + 'pt3cmp300.txt') as fh:
    for ln in fh:
        if ln[0] != 'O':
            continue
        w = ln.split()
        e = int(w[1])
        recs[(e, int(w[2]))] = (float(w[4]), int(w[12]) == 0,
                                tuple((e, int(x)) for x in w[14:]))
print('delivery universe %d rows = %.1f/evt' % (len(recs), len(recs) / NEV))


def load_diag(path):
    """(evt,t3) -> (logit, nPre, nCur, nUnits) in sweep order"""
    out = []
    with open(path) as fh:
        for ln in fh:
            if ln[0] != 'D':
                continue
            w = ln.split()
            out.append((int(w[1]), int(w[2]), float(w[4]), int(w[5]), int(w[6]), int(w[7])))
    return out


def score(name, rows, keep, nd, rp):
    n = nf = 0
    cov = set()
    for r in rows:
        if not keep(r):
            continue
        k = (r[0], r[1])
        rec = recs.get(k)
        n += 1
        if rec is None:
            continue
        nf += 1 if rec[1] else 0
        for s in rec[2]:
            if s in INCUT:
                cov.add(s)
    u = len(cov - rp) / nd
    print('%-34s%9.1f%9.4f%9.2f%9.3f%11.5f'
          % (name, n / nd, nf / max(n, 1), len(cov) / nd, u, 0.77432 + u / D))


if __name__ == '__main__':
    print('%-34s%9s%9s%9s%9s%11s'
          % ('rule', 'rows/evt', 'fakefrc', 'cov/evt', 'UNIQ', 'predEff'))
    for path, tag in ((G + 'diag_md.txt', 'MD  noRD'), (G + 'diag_mdrd.txt', 'MD  RD'),
                      (G + 'diag_hit.txt', 'HIT RD')):
        if not os.path.exists(path):
            continue
        rows = load_diag(path)
        if not rows:
            continue
        nd = max(r[0] for r in rows) + 1
        rp = set(k for k in RP if k[0] < nd)
        print('--- %s (%d rows = %.1f/evt, %d evts) ---' % (tag, len(rows), len(rows) / nd, nd))
        print('%-34s%9s%9s%9s%9s%11s'
              % ('rule', 'rows/evt', 'fakefrc', 'cov/evt', 'UNIQ', 'predEff'))
        for th in (3, 4, 5, 6, 7):
            for npre in (99, 4, 3, 2, 1):
                for ncur in (99, 4, 3, 2, 1):
                    if npre == 99 and ncur == 99:
                        continue
                    score('%s th%g pre>=%s cur>=%s' % (tag, th, npre, ncur), rows,
                          lambda r, th=th, a=npre, b=ncur: r[2] >= th and r[3] < a and r[4] < b,
                          nd, rp)
