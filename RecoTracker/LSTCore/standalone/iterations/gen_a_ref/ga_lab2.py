#!/usr/bin/env python3
"""GEN-A OFFLINE DEDUP LABORATORY v2 -- with the HARNESS duplicate rule.

v1 could not model the duplicate rate because the audit only carried ACCEPTED-sim
matches, while the harness counts duplicates against the FULL sim list (pileup
included). The audit now carries full:accepted:frac for every match plus, per event, the
full-sim coverage of everything already delivered (the F record), so all three gate
metrics are modelled directly:

  effNum  = 17642 (r_off) + |new IN-CUT accepted sims covered by the kept set|
  frNum   = 22225 (r_off) + |kept rows with pt>0.9 and NO sim match at all|
  frDen   = 453150 + |kept rows with pt>0.9|
  drNum   = 23808 + |kept rows with pt>0.9 whose best sim already has a TC|
  drDen   = frDen
The r_off anchors are the measured 300-event createPerfNumDenHists integrals, and the
audit's own A/F records reproduce r_off's effNum exactly (17642), which is what makes the
anchoring legitimate. What is NOT modelled: -RPS retirement of carried bare-pLS rows,
which grows with the delivery volume and costs both rows and sims -- so the estimate is
optimistic by a known, measured margin (~50 sims at d_none), and finalists are always
re-measured in C++.
"""
import argparse
import os
import sys
import numpy as np

GA = os.path.dirname(os.path.abspath(__file__))
# measured r_off (class OFF) anchors, 300 events
EFFDEN, EFFNUM0 = 22784.0, 17642.0
FRDEN0, FRNUM0, DRNUM0 = 453150.0, 22225.0, 23808.0


class Ev:
    __slots__ = ('incut', 'already', 'alreadyFull', 'nPreTC', 'nPreFake', 'O', '_raw')


def load(path, nev_max=None):
    evs, cur = [], None
    simpt = simeta = simvp = simvz = None
    for line in open(path):
        if line[0] == '#':
            continue
        t = line.split()
        k = t[0]
        if k == 'E':
            if nev_max is not None and len(evs) >= nev_max:
                break
            cur = Ev(); cur.O = []
            simpt, simeta, simvp, simvz = [], [], [], []
            cur._raw = (simpt, simeta, simvp, simvz)
            evs.append(cur)
        elif k == 'S':
            simpt.append(float(t[2])); simeta.append(float(t[3]))
            simvp.append(float(t[4])); simvz.append(float(t[5]))
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
            f = t[bar + 1:hh]; g = t[hh + 1:]
            cur.O.append((float(t[3]), float(t[4]), float(t[5]), nfull == 0, tuple(ms),
                          (int(g[0]), int(g[1]), int(g[2])),
                          tuple(int(x) for x in g[3:7] if int(x) >= 0),
                          int(g[7]), int(g[8]), int(g[9]),
                          float(f[0]), float(f[1]), float(f[2])))
    import uproot
    q = uproot.open('/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/'
                    'standalone/LSTNtuple_PU200RelVal_300evt.root')['tree'].arrays(
                        ['sim_q'], library='np', entry_stop=len(evs))['sim_q']
    for i, e in enumerate(evs):
        pt, eta, vp, vz = (np.array(x) for x in e._raw)
        qq = np.array(q[i])[:len(pt)]
        e.incut = (pt > 0.9) & (np.abs(vz) < 30) & (np.abs(vp) < 2.5) & (qq != 0)
        e.already = set(s for s in e.already if s < len(e.incut) and e.incut[s])
    return evs


def run(evs, theta=6.0, ccOn=True, ccN=1, preclaim=True, rdt=True, weld=False,
        t3fakeMax=1e9, t3goodMin=-1e9, key='logit', tier2Theta=None):
    """tier2Theta: GEN-A two-tier ownership -- a candidate with >=ccN owned units may
    still be delivered if its logit clears this HIGHER bar (None = classic hard drop)."""
    nev = len(evs)
    scale = 300.0 / nev
    kept = keptPt = keptFake = keptDup = 0
    newS = 0
    for e in evs:
        cands = [o for o in e.O
                 if o[0] >= theta and o[10] <= t3fakeMax and (o[11] + o[12]) >= t3goodMin]
        if key == 'logit':
            cands.sort(key=lambda o: (-o[0], o[5][0]))
        elif key == 'oracleonly':
            cands = [o for o in cands
                     if any(m[1] >= 0 and m[1] < len(e.incut) and e.incut[m[1]]
                            and m[1] not in e.already for m in o[4])]
        claimedMd = set(); pixOwner = {}; groupTaken = set()
        covIn = set(e.already)
        covFull = set(e.alreadyFull)
        for o in cands:
            lg, pt, eta, isFake, ms, mds, pix, nClMd, nClHit, grp = o[:10]
            if weld and grp >= 0 and grp in groupTaken:
                continue
            if rdt:
                sh, bad = {}, False
                for h in pix:
                    for qq in pixOwner.get(h, ()):
                        sh[qq] = sh.get(qq, 0) + 1
                        if sh[qq] >= 2:
                            bad = True; break
                    if bad:
                        break
                if bad:
                    continue
            units = [m for m in mds if m >= 0]
            if ccOn:
                shared = (nClMd if preclaim else 0) + sum(1 for u in units if u in claimedMd)
                if units and shared >= ccN:
                    if tier2Theta is None or lg < tier2Theta:
                        continue
            for u in units:
                claimedMd.add(u)
            if weld and grp >= 0:
                groupTaken.add(grp)
            if rdt:
                for h in pix:
                    pixOwner.setdefault(h, []).append(id(o))
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
                if a >= 0 and a < len(e.incut) and e.incut[a] and a not in covIn:
                    covIn.add(a); newS += 1
    effNum = EFFNUM0 + newS * scale
    frDen = FRDEN0 + keptPt * scale
    return dict(deliv=kept / nev, newSims=newS * scale,
                eff=effNum / EFFDEN,
                fake=(FRNUM0 + keptFake * scale) / frDen,
                dup=(DRNUM0 + keptDup * scale) / frDen,
                keptFakeFrac=keptFake / max(keptPt, 1),
                keptDupFrac=keptDup / max(keptPt, 1))


HDR = '%-30s %8s %8s %9s %9s %9s %8s %8s' % (
    'config', 'deliv/e', 'newSim', 'eff', 'dup', 'fake', 'rowFake', 'rowDup')


def show(nm, r):
    g = ('E' if r['eff'] >= 0.81303 else '.') + ('D' if r['dup'] <= 0.052 else '.') + \
        ('F' if r['fake'] <= 0.047 else '.')
    print('%-30s %8.1f %8.0f %9.5f %9.5f %9.5f %8.4f %8.4f  %s'
          % (nm, r['deliv'], r['newSims'], r['eff'], r['dup'], r['fake'],
             r['keptFakeFrac'], r['keptDupFrac'], g))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--audit', default=os.path.join(GA, 'aud_r2.txt'))
    ap.add_argument('--nev', type=int, default=None)
    args = ap.parse_args()
    evs = load(args.audit, args.nev)
    print('events %d  audit %s' % (len(evs), args.audit))
    print('GATE: eff >= .81303 | dup <= .052 | fake <= .047   (r_off = .77432 / .05254 / .04905)')
    print(HDR)
    show('d_none  (real .81505/.539/.062)', run(evs, 6.0, ccOn=False, rdt=False))
    show('d_px    (real .81404/.283/.067)', run(evs, 6.0, ccOn=False, rdt=True))
    show('d_both  (real .80815/.072/.064)', run(evs, 6.0, ccOn=True, ccN=2, rdt=True))
    show('d_n1    (real .80456/.047/.056)', run(evs, 6.0, ccOn=True, ccN=1, rdt=True))
    show('a8_n1   (real .79380/.051/.049)', run(evs, 8.0, ccOn=True, ccN=1, rdt=True))
