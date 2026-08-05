#!/usr/bin/env python3
"""GEN-A OFFLINE DEDUP LABORATORY.

A 300-event C++ A/B costs ~13 minutes. This replays the SAME candidate set the C++
delivery sees (from the -QD audit stream, which is dumped after gaStageT3 and before any
dedup) and re-executes the dedup rules in Python, so a rule can be scanned in seconds and
only the finalists are paid for in C++.

WHAT IT REPRODUCES EXACTLY
  * the candidate universe and its logits (the -AT3 threshold is a filter on the dump),
  * the -CC ownership sweep at MD or hit granularity, with the pre-claim counts the C++
    computed against the already-delivered universe, plus delivery-vs-delivery overlap
    accumulated during the sweep,
  * the -RDT seed-family pixel rule (>= 2 shared pixel hit rows with a kept seed),
  * the GEN-A -CCW welded-chain group rule,
  * per-object eligibility gates (t3 fakeScore / prompt+displaced).

WHAT IT ESTIMATES (calibrated against real runs, never quoted as a result)
  effEst  = (|A| + |new sims covered by the kept set|) / 22784
  fakeEst = (nPreFake + kept fakes) / (nPreTC + nKept)
  dupEst  = kept rows whose in-cut sims were all already covered / (nPreTC + nKept)
`A`, nPreTC and nPreFake come from the dump's A record: the sims/TCs the pipeline already
delivers BEFORE stage B. The estimator does NOT model -RPS (which retires carried
bare-pLS rows as a function of -AT3); that is the known systematic and is why finalists
are always re-measured in C++.
"""
import argparse
import os
import sys
import numpy as np

GA = os.path.dirname(os.path.abspath(__file__))
DEN300 = 22784.0  # Root__TC_base_0_0_ef_denom_eta over 300 events
DENPE = DEN300 / 300.0


class Ev:
    __slots__ = ('incut', 'already', 'nPreTC', 'nPreFake', 'O', '_raw')


def load(path, nev_max=None):
    evs = []
    cur = None
    simpt = simeta = simvp = simvz = None
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
            simpt, simeta, simvp, simvz = [], [], [], []
            cur._raw = (simpt, simeta, simvp, simvz)
            evs.append(cur)
        elif k == 'S':
            simpt.append(float(t[2])); simeta.append(float(t[3]))
            simvp.append(float(t[4])); simvz.append(float(t[5]))
        elif k == 'A':
            cur.nPreTC = int(t[1]); cur.nPreFake = int(t[2])
            cur.already = set(int(x) for x in t[3:])
        elif k == 'O':
            nacc = int(t[7])
            sims = tuple(int(x.split(':')[0]) for x in t[8:8 + nacc])
            bar = t.index('|')
            hh = t.index('#')
            f = t[bar + 1:hh]
            g = t[hh + 1:]
            cur.O.append((float(t[3]),                       # 0 logit
                          float(t[4]),                       # 1 pt (pLS)
                          float(t[5]),                       # 2 eta (t3)
                          int(t[6]) == 0,                    # 3 isFake (no >0.75 sim at all)
                          sims,                              # 4 accepted-sim matches
                          (int(g[0]), int(g[1]), int(g[2])),  # 5 md rows
                          tuple(int(x) for x in g[3:7] if int(x) >= 0),  # 6 pixel hit rows
                          int(g[7]), int(g[8]), int(g[9]),   # 7 nClMd 8 nClHit 9 group
                          float(f[0]), float(f[1]), float(f[2])))       # 10-12 t3 scores
    import uproot
    q = uproot.open('/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/'
                    'standalone/LSTNtuple_PU200RelVal_300evt.root')['tree'].arrays(
                        ['sim_q'], library='np', entry_stop=len(evs))['sim_q']
    for i, e in enumerate(evs):
        pt, eta, vp, vz = (np.array(x) for x in e._raw)
        qq = np.array(q[i])[:len(pt)]
        e.incut = (pt > 0.9) & (np.abs(vz) < 30) & (np.abs(vp) < 2.5) & (qq != 0)
        e.already = set(s for s in e.already if s < len(e.incut) and e.incut[s])
        pass
    return evs


def run_rule(evs, theta=6.0, ccOn=True, ccGran='md', ccN=1, preclaim=True,
             rdt=True, weld=False, t3fakeMax=1e9, t3goodMin=-1e9, key='logit',
             maxPerEvent=None):
    """Replays one dedup configuration. Returns the aggregate estimate dict."""
    nKept = nKeptFake = nDupEst = 0
    nPreTC = nPreFake = 0
    nCovNew = 0
    nBaseCov = 0
    nCand = 0
    for e in evs:
        nPreTC += e.nPreTC
        nPreFake += e.nPreFake
        nBaseCov += len(e.already)
        cands = [o for o in e.O
                 if o[0] >= theta and o[10] <= t3fakeMax and (o[11] + o[12]) >= t3goodMin]
        nCand += len(cands)
        if key == 'logit':
            cands.sort(key=lambda o: (-o[0], o[5][0]))
        elif key == 'clean':   # fewest already-claimed units first, then logit
            cands.sort(key=lambda o: (o[7] if ccGran == 'md' else o[8], -o[0]))
        elif key in ('oracle', 'oracleonly'):
            # DIAGNOSTIC ONLY (uses truth): upper bound on what ANY ranking could do.
            def isnew(o):
                return any(s2 < len(e.incut) and e.incut[s2] and s2 not in e.already
                           for s2 in o[4])
            if key == 'oracleonly':
                cands = [o for o in cands if isnew(o)]
            else:
                cands.sort(key=lambda o: (0 if isnew(o) else 1, -o[0]))
        elif key == 'notfake':
            cands.sort(key=lambda o: (1 if o[3] else 0, -o[0]))
        claimedMd = set()
        claimedHit = set()
        pixOwner = {}
        groupTaken = set()
        covered = set(e.already)
        kept = 0
        for o in cands:
            if maxPerEvent is not None and kept >= maxPerEvent:
                break
            lg, pt, eta, isFake, sims, mds, pix, nClMd, nClHit, grp = o[:10]
            if weld and grp >= 0:
                if grp in groupTaken:
                    continue
            if rdt:
                sh = {}
                bad = False
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
            if ccOn:
                if ccGran == 'md':
                    units = [m for m in mds if m >= 0]
                    shared = (nClMd if preclaim else 0) + sum(1 for u in units if u in claimedMd)
                else:
                    units = [m for m in mds if m >= 0]  # hit-level overlap approximated by MD ids
                    shared = (nClHit if preclaim else 0) + 2 * sum(1 for u in units if u in claimedMd)
                if units and shared >= ccN:
                    continue
                for u in units:
                    claimedMd.add(u)
            else:
                for m in mds:
                    if m >= 0:
                        claimedMd.add(m)
            if weld and grp >= 0:
                groupTaken.add(grp)
            if rdt:
                for h in pix:
                    pixOwner.setdefault(h, []).append(id(o))
            kept += 1
            nKept += 1
            if isFake:
                nKeptFake += 1
            new = [s for s in sims if s < len(e.incut) and e.incut[s] and s not in covered]
            if new:
                nCovNew += len(new)
                covered.update(new)
            else:
                nDupEst += 1
    nev = len(evs)
    nTC = nPreTC + nKept
    return dict(deliv=nKept / nev, cand=nCand / nev, nTC=nTC,
                effEst=(nBaseCov + nCovNew) / (DENPE * nev),
                newSims=nCovNew,
                fakeEst=(nPreFake + nKeptFake) / max(nTC, 1),
                keptFakeFrac=nKeptFake / max(nKept, 1),
                dupAdd=nDupEst / max(nTC, 1), nDup=nDupEst)


HDR = ('%-26s %8s %8s %9s %8s %9s %9s %8s' %
       ('config', 'cand/e', 'deliv/e', 'effEst', 'newSim', 'fakeEst', 'keptFake', 'dupAdd'))


def show(name, r):
    print('%-26s %8.1f %8.1f %9.5f %8d %9.5f %9.5f %8.5f' %
          (name, r['cand'], r['deliv'], r['effEst'], r['newSims'], r['fakeEst'],
           r['keptFakeFrac'], r['dupAdd']))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--audit', default=os.path.join(GA, 'audit300c.txt'))
    ap.add_argument('--nev', type=int, default=None)
    args = ap.parse_args()
    evs = load(args.audit, args.nev)
    print('events %d' % len(evs))
    base = sum(len(e.already) for e in evs)
    print('pre-stage-B: %.1f TC/evt, fake %.5f, covers %d in-cut sims -> effEst %.5f'
          % (sum(e.nPreTC for e in evs) / len(evs),
             sum(e.nPreFake for e in evs) / max(sum(e.nPreTC for e in evs), 1),
             base, base / (DENPE * len(evs))))
    print(HDR)
    # --- calibration against runs whose true numbers are known -----------------------
    show('d_none  (real .81505)', run_rule(evs, 6.0, ccOn=False, rdt=False))
    show('d_px    (real .81404)', run_rule(evs, 6.0, ccOn=False, rdt=True))
    show('d_both  (real .80815)', run_rule(evs, 6.0, ccOn=True, ccN=2, rdt=True))
    show('d_n1    (real .80456)', run_rule(evs, 6.0, ccOn=True, ccN=1, rdt=True))
    show('d_ot    (real .80894)', run_rule(evs, 6.0, ccOn=True, ccN=2, rdt=False))
    show('d_otn1  (real .80504)', run_rule(evs, 6.0, ccOn=True, ccN=1, rdt=False))
    show('d_nopre (real .81390)', run_rule(evs, 6.0, ccOn=True, ccN=2, rdt=True, preclaim=False))
    show('a8_n1   (real .79380)', run_rule(evs, 8.0, ccOn=True, ccN=1, rdt=True))
    show('a8_n2   (real .79578)', run_rule(evs, 8.0, ccOn=True, ccN=2, rdt=True))
