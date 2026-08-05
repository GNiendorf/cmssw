#!/usr/bin/env python3
"""M22 CEIL OFFLINE LABORATORY -- the GEN-A dedup lab, re-pointed at the -QD stream this
campaign emits and extended with GEN-C's layer-containment rule.

Methodology is deliberately IDENTICAL to gen_a_ref/ga_lab2.py so the numbers are directly
comparable to sibling A's ceiling table:

  effNum = 17642 (r_off) + |new IN-CUT accepted sims covered by the kept set|
  frNum  = 22225 (r_off) + |kept rows with pt>0.9 and NO sim match at all|
  frDen  = 453150 (r_off) + |kept rows with pt>0.9|
  drNum  = 23808 (r_off) + |kept rows with pt>0.9 whose every matched sim already has a TC|
  drDen  = frDen

The r_off anchors are the measured 300-event createPerfNumDenHists integrals; the audit's
own A record reproduces r_off's effNum exactly, which is what makes the anchoring legitimate.
Not modelled (same known, measured optimism as A's lab): -RPS retirement of carried bare-pLS
rows, worth ~50 sims at the highest delivery volumes. Finalists are re-measured in C++.

WHAT IS NEW HERE
  * the audit carries, per candidate, WHICH of its MDs are pre-claimed and the LAYER MASK of
    the owner holding each -- so the N-shared rules are exact (no pre-claim/dynamic double
    count) and GEN-C's containment rule can be replayed too;
  * the same replay runs on a CONDITIONED dump (the target universe was masked before the
    head ever scored) and on the UNCONDITIONED one, which is the whole experiment.
"""
import argparse
import os
import sys

CE = os.path.dirname(os.path.abspath(__file__))
# measured r_off (pT3 class OFF) anchors, 300 events -- identical to ga_lab2.py
EFFDEN, EFFNUM0 = 22784.0, 17642.0
FRDEN0, FRNUM0, DRNUM0 = 453150.0, 22225.0, 23808.0


class Ev:
    __slots__ = ('incut', 'already', 'alreadyFull', 'nPreTC', 'nPreFake', 'nBareTgt', 'O')


def load(path, nev_max=None):
    """One pass over the text stream. O tuples are:
       (logit, pt, eta, isFake, matches, mds, ownLay, myLay, pix, nClMd, nClHit, t3row)
       matches = tuple of (fullSimRow, acceptedSimRow, frac)
       ownLay[k] = layer mask of the owner pre-claiming md[k], 0 = free."""
    evs, cur = [], None
    pt = eta = vp = vz = q = None
    for line in open(path):
        if line[0] == '#':
            continue
        t = line.split()
        k = t[0]
        if k == 'E':
            if nev_max is not None and len(evs) >= nev_max:
                break
            cur = Ev()
            cur.O = []
            cur.nBareTgt = int(t[7])
            pt, eta, vp, vz, q = [], [], [], [], []
            cur.incut = (pt, eta, vp, vz, q)   # replaced below
            evs.append(cur)
        elif k == 'S':
            pt.append(float(t[2])); eta.append(float(t[3]))
            vp.append(float(t[4])); vz.append(float(t[5])); q.append(int(t[6]))
        elif k == 'A':
            cur.nPreTC = int(t[1]); cur.nPreFake = int(t[2])
            cur.already = set(int(x) for x in t[3:])
        elif k == 'F':
            cur.alreadyFull = set(int(x) for x in t[2:])
        elif k == 'O':
            nfull = int(t[6])
            bar = t.index('|'); hh = t.index('#')
            ms = []
            for x in t[8:8 + nfull]:
                a, b, c = x.split(':')
                ms.append((int(a), int(b), float(c)))
            g = t[hh + 1:]
            cur.O.append((float(t[3]), float(t[4]), float(t[5]), nfull == 0, tuple(ms),
                          (int(g[0]), int(g[1]), int(g[2])),                 # mds
                          (int(g[10]), int(g[11]), int(g[12])),              # ownLay
                          int(g[9]),                                         # myLay
                          tuple(int(x) for x in g[3:7] if int(x) >= 0),      # pixel hits
                          int(g[7]), int(g[8]), int(t[1])))
    for e in evs:
        pt, eta, vp, vz, q = e.incut
        e.incut = [(pt[i] > 0.9 and abs(vz[i]) < 30 and abs(vp[i]) < 2.5 and q[i] != 0)
                   for i in range(len(pt))]
        e.already = set(s for s in e.already if s < len(e.incut) and e.incut[s])
    return evs


def run(evs, theta=6.0, rule='none', ccN=1, layer=False, rdt=True, oracle=False):
    """rule: 'none' | 'n' (veto when >= ccN units are owned) | 'layer' (containment only)
             | 'nlayer' (either). layer=True is shorthand folded into rule.
       oracle: keep ONLY candidates covering an in-cut accepted sim nothing else covers
               (the truth-based ceiling; no threshold, no ranking)."""
    nev = len(evs)
    scale = 300.0 / nev
    kept = keptPt = keptFake = keptDup = 0
    newS = 0
    useN = rule in ('n', 'nlayer')
    useL = rule in ('layer', 'nlayer')
    for e in evs:
        cands = [o for o in e.O if o[0] >= theta]
        if oracle:
            cands = [o for o in cands
                     if any(m[1] >= 0 and m[1] < len(e.incut) and e.incut[m[1]]
                            and m[1] not in e.already for m in o[4])]
        else:
            cands.sort(key=lambda o: (-o[0], o[11]))
        claimed = {}          # md row -> layer mask of the owner holding it (dynamic half)
        pixOwner = {}
        covIn = set(e.already)
        covFull = set(e.alreadyFull)
        for o in cands:
            lg, pt, eta, isFake, ms, mds, ownLay, myLay, pix, nClMd, nClHit, t3r = o
            if rdt:
                sh, bad = {}, False
                for h in pix:
                    for qq in pixOwner.get(h, ()):
                        sh[qq] = sh.get(qq, 0) + 1
                        if sh[qq] >= 2:
                            bad = True
                            break
                    if bad:
                        break
                if bad:
                    continue
            if useN or useL:
                nShared, contained = 0, False
                for k in range(3):
                    md = mds[k]
                    if md < 0:
                        continue
                    ol = ownLay[k] if ownLay[k] else claimed.get(md, 0)
                    if ol == 0:
                        continue
                    nShared += 1
                    if useL and (myLay & ~ol) == 0:
                        contained = True
                if (useN and nShared >= ccN) or (useL and contained):
                    continue
            for md in mds:
                if md >= 0 and md not in claimed:
                    claimed[md] = myLay
            if rdt:
                for h in pix:
                    pixOwner.setdefault(h, []).append(t3r)
            kept += 1
            if pt > 0.9:
                keptPt += 1
                if isFake:
                    keptFake += 1
                elif all(m[0] in covFull for m in ms):
                    keptDup += 1     # harness rule: every matched sim already has a TC
            for m in ms:
                covFull.add(m[0])
                a = m[1]
                if 0 <= a < len(e.incut) and e.incut[a] and a not in covIn:
                    covIn.add(a)
                    newS += 1
    effNum = EFFNUM0 + newS * scale
    frDen = FRDEN0 + keptPt * scale
    return dict(deliv=kept / nev, newSims=newS * scale, eff=effNum / EFFDEN,
                fake=(FRNUM0 + keptFake * scale) / frDen,
                dup=(DRNUM0 + keptDup * scale) / frDen,
                rowFake=keptFake / max(keptPt, 1), rowDup=keptDup / max(keptPt, 1))


def compose(evs, theta=6.0):
    """The candidate-universe decomposition, directly comparable to GEN-C's
    1132.7 = 884.4 dup-of-chain + 50.6 dup-of-each-other + 90.8 fake + 106.9 new."""
    nev = len(evs)
    nF = nChain = nEach = nNew = 0
    newSims = 0
    tgt = 0
    for e in evs:
        tgt += e.nBareTgt
        covFull = set(e.alreadyFull)
        covIn = set(e.already)
        for o in sorted([o for o in e.O if o[0] >= theta], key=lambda o: (-o[0], o[11])):
            ms = o[4]
            if o[3]:
                nF += 1
            elif all(m[0] in e.alreadyFull for m in ms):
                nChain += 1
            elif all(m[0] in covFull for m in ms):
                nEach += 1
            else:
                nNew += 1
            for m in ms:
                covFull.add(m[0])
                a = m[1]
                if 0 <= a < len(e.incut) and e.incut[a] and a not in covIn:
                    covIn.add(a)
                    newSims += 1
    n = float(nev)
    return dict(targets=tgt / n, rows=(nF + nChain + nEach + nNew) / n, dupChain=nChain / n,
                dupEach=nEach / n, fake=nF / n, new=nNew / n, newSims=newSims * 300.0 / n)


HDR = '%-28s %8s %8s %9s %9s %9s %8s %8s' % (
    'config', 'deliv/e', 'newSim', 'eff', 'dup', 'fake', 'rowFake', 'rowDup')


def show(nm, r):
    g = ('E' if r['eff'] >= 0.81303 else '.') + ('D' if r['dup'] <= 0.052 else '.') + \
        ('F' if r['fake'] <= 0.047 else '.')
    print('%-28s %8.1f %8.0f %9.5f %9.5f %9.5f %8.4f %8.4f  %s'
          % (nm, r['deliv'], r['newSims'], r['eff'], r['dup'], r['fake'],
             r['rowFake'], r['rowDup'], g))


RULES = [('nodedup', dict(rule='none')),
         ('veto-on-1', dict(rule='n', ccN=1)),
         ('veto-on-2', dict(rule='n', ccN=2)),
         ('veto-on-3', dict(rule='n', ccN=3)),
         ('layer-contain', dict(rule='layer'))]

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--audit', required=True)
    ap.add_argument('--nev', type=int, default=None)
    ap.add_argument('--tag', default='')
    ap.add_argument('--theta', type=float, default=6.0)
    ap.add_argument('--mode', default='all', choices=['all', 'compose', 'oracle', 'curve'])
    ap.add_argument('--rdt', type=int, default=1)
    args = ap.parse_args()
    evs = load(args.audit, args.nev)
    print('== %s ==  events %d  audit %s' % (args.tag or args.audit, len(evs), args.audit))
    if args.mode in ('all', 'compose'):
        c = compose(evs, args.theta)
        print('COMPOSITION at theta=%.1f (no dedup of any kind):' % args.theta)
        print('  bare targets/evt %8.1f' % c['targets'])
        print('  rows/evt         %8.1f  = %.1f dup-of-chain + %.1f dup-of-each-other'
              ' + %.1f fake + %.1f NEW' % (c['rows'], c['dupChain'], c['dupEach'], c['fake'],
                                           c['new']))
        print('  new in-cut sims (300-evt scale) %.0f' % c['newSims'])
    if args.mode in ('all', 'oracle'):
        print('\nORACLE CEILING (perfect truth-based selection, no threshold), rdt=%d'
              % args.rdt)
        print(HDR)
        for nm, kw in RULES:
            show(nm, run(evs, theta=-1e9, oracle=True, rdt=bool(args.rdt), **kw))
    if args.mode in ('all', 'curve'):
        print('\nHEAD-RANKED (the real selector) at theta=%.1f' % args.theta)
        print(HDR)
        for nm, kw in RULES:
            show(nm, run(evs, theta=args.theta, rdt=bool(args.rdt), **kw))
