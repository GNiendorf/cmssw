#!/usr/bin/env python3
"""GEN-A TASK 1: is OUR pT3-class matching better or worse than LST's?

Reads the -QD audit stream (both candidate sets, SAME >0.75 matcher, SAME hit-list
composition, dedup ignored on BOTH sides) and reports

  * sim coverage (unique-to-LST / unique-to-us / shared),
  * purity (fake fraction) at matched candidate count,
  * the full coverage-vs-purity curve with LST's operating point on it,
  * the same, broken down per eta region and per pT band.

Harness conventions reproduced exactly:
  sim in cut : pt > 0.9, |vz| < 30, vperp < 2.5   (ef_denom_eta, summed incl. overflow)
  TC  in cut : pt > 0.9                            (fr_denom_eta)
"""
import sys
import numpy as np

PATH = sys.argv[1] if len(sys.argv) > 1 else \
    '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/gen_a_ref/audit300.txt'

ETA_REG = [('barrel', 0.0, 1.1), ('trans', 1.1, 1.7), ('endcap', 1.7, 99.)]
PT_BANDS = [('0.9-1.5', 0.9, 1.5), ('1.5-3', 1.5, 3.0), ('3-10', 3.0, 10.0), ('10+', 10.0, 1e9)]


def load(path):
    """-> list of events; each = dict(sims=ndarray[n,4], L=[...], O=[...])."""
    evs = []
    cur = None
    for line in open(path):
        if line[0] == '#':
            continue
        t = line.split()
        k = t[0]
        if k == 'E':
            cur = dict(sim_pt=[], sim_eta=[], sim_vp=[], sim_vz=[], L=[], O=[])
            evs.append(cur)
        elif k == 'S':
            cur['sim_pt'].append(float(t[2])); cur['sim_eta'].append(float(t[3]))
            cur['sim_vp'].append(float(t[4])); cur['sim_vz'].append(float(t[5]))
        elif k == 'L':
            # L row pls t3 pt eta nFull nAcc acc:frac...
            nfull = int(t[6]); nacc = int(t[7])
            sims = [int(x.split(':')[0]) for x in t[8:8 + nacc]]
            cur['L'].append((float(t[4]), float(t[5]), nfull, sims))
        elif k == 'O':
            # O t3 pls logit pt eta nFull nAcc acc:frac... | feats
            nfull = int(t[6]); nacc = int(t[7])
            sims = [int(x.split(':')[0]) for x in t[8:8 + nacc]]
            bar = t.index('|')
            feats = [float(x) for x in t[bar + 1:]]
            cur['O'].append((float(t[3]), float(t[4]), float(t[5]), nfull, sims,
                             int(t[1]), int(t[2]), feats))
    return evs


def main():
    evs = load(PATH)
    nev = len(evs)
    print('events %d' % nev)

    # ---- sim universe -----------------------------------------------------------
    # sim_q is NOT in the dump; the harness EXCLUDES neutrals (performance.cc
    # "if (effset.pdgid == 0 and q == 0) return"), so it is read from the ntuple, whose
    # sim block is entry-aligned with the dump's event order. With it the denominator
    # reproduces Root__TC_base_0_0_ef_denom_eta EXACTLY (22784 over 300 evts).
    import uproot
    _q = uproot.open('/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/'
                     'standalone/LSTNtuple_PU200RelVal_300evt.root')['tree'].arrays(
                         ['sim_q'], library='np', entry_stop=len(evs))['sim_q']
    sim_incut, sim_pt, sim_eta = [], [], []
    for i, e in enumerate(evs):
        pt = np.array(e['sim_pt']); eta = np.array(e['sim_eta'])
        vp = np.array(e['sim_vp']); vz = np.array(e['sim_vz'])
        q = np.array(_q[i])[:len(pt)]
        sim_incut.append((pt > 0.9) & (np.abs(vz) < 30) & (np.abs(vp) < 2.5) & (q != 0))
        sim_pt.append(pt); sim_eta.append(eta)
    n_incut = sum(int(m.sum()) for m in sim_incut)
    print('sims in cut (eff denominator) %d = %.1f/evt' % (n_incut, n_incut / nev))

    # ---- candidate arrays -------------------------------------------------------
    # LST
    L_pt, L_eta, L_fake, L_ev = [], [], [], []
    L_sims = []
    for i, e in enumerate(evs):
        for (pt, eta, nfull, sims) in e['L']:
            L_pt.append(pt); L_eta.append(eta); L_fake.append(nfull == 0)
            L_ev.append(i); L_sims.append(sims)
    L_pt = np.array(L_pt); L_eta = np.array(L_eta); L_fake = np.array(L_fake); L_ev = np.array(L_ev)
    # ours
    O_lg, O_pt, O_eta, O_fake, O_ev = [], [], [], [], []
    O_sims, O_feat = [], []
    for i, e in enumerate(evs):
        for (lg, pt, eta, nfull, sims, t3, pls, feats) in e['O']:
            O_lg.append(lg); O_pt.append(pt); O_eta.append(eta); O_fake.append(nfull == 0)
            O_ev.append(i); O_sims.append(sims); O_feat.append(feats)
    O_lg = np.array(O_lg); O_pt = np.array(O_pt); O_eta = np.array(O_eta)
    O_fake = np.array(O_fake); O_ev = np.array(O_ev)
    O_feat = np.array(O_feat)
    print('LST pT3 candidates (pre-clean) %d = %.1f/evt' % (len(L_pt), len(L_pt) / nev))
    print('our bare-T3 winners (no threshold, pre-dedup) %d = %.1f/evt' % (len(O_lg), len(O_lg) / nev))

    def covered(evidx, simlists, mask=None):
        """set of (event, accSim) covered, restricted to in-cut sims."""
        s = set()
        rng = range(len(evidx)) if mask is None else np.nonzero(mask)[0]
        for j in rng:
            i = evidx[j]
            ic = sim_incut[i]
            for sm in simlists[j]:
                if sm < len(ic) and ic[sm]:
                    s.add((i, sm))
        return s

    covL = covered(L_ev, L_sims)
    print('LST covers %d in-cut sims (%.1f/evt) = %.5f of denominator'
          % (len(covL), len(covL) / nev, len(covL) / n_incut))

    # ---- threshold scan ---------------------------------------------------------
    order = np.argsort(-O_lg)
    thetas = np.arange(-2, 13.001, 0.25)
    print()
    print('%7s %9s %9s %9s %9s %9s %9s' %
          ('theta', 'cand/evt', 'cov', 'covFrac', 'fakeFrac', 'sharedL', 'uniqUs'))
    rows = []
    for th in thetas:
        m = O_lg >= th
        n = int(m.sum())
        if n == 0:
            continue
        cov = covered(O_ev, O_sims, m)
        # fake fraction over TCs in the harness FR denominator (pt > 0.9)
        sel = m & (O_pt > 0.9)
        ff = float(O_fake[sel].mean()) if sel.sum() else float('nan')
        rows.append((th, n / nev, len(cov), len(cov) / n_incut, ff,
                     len(cov & covL), len(cov - covL)))
        print('%7.2f %9.1f %9d %9.5f %9.5f %9d %9d' % rows[-1])

    selL = L_pt > 0.9
    print()
    print('LST OPERATING POINT: cand/evt %.1f  cov %d  covFrac %.5f  fakeFrac %.5f  (all rows)'
          % (len(L_pt) / nev, len(covL), len(covL) / n_incut, float(L_fake[selL].mean())))

    # ---- matched-count comparison ----------------------------------------------
    print()
    for target, label in ((len(L_pt) / nev, 'LST pre-clean %.1f/evt' % (len(L_pt) / nev)),
                          (151.7, 'LST retired TC rows 151.7/evt')):
        k = int(round(target * nev))
        if k > len(O_lg):
            k = len(O_lg)
        th = O_lg[order[k - 1]]
        m = O_lg >= th
        cov = covered(O_ev, O_sims, m)
        sel = m & (O_pt > 0.9)
        print('MATCHED COUNT vs %s -> theta %.4f : cand/evt %.1f cov %d (%.5f) fake %.5f'
              % (label, th, m.sum() / nev, len(cov), len(cov) / n_incut,
                 float(O_fake[sel].mean())))
        print('    shared with LST %d | unique to us %d | unique to LST %d'
              % (len(cov & covL), len(cov - covL), len(covL - cov)))

    # ---- per region / per pT ----------------------------------------------------
    def breakdown(name, binsel_sim, binsel_cand):
        """binsel_sim(i,simrow)->bool ; binsel_cand(pt,eta)->bool"""
        # LST
        covL_b = set(x for x in covL if binsel_sim(x[0], x[1]))
        mL = binsel_cand(L_pt, L_eta) & (L_pt > 0.9)
        den = 0
        for i, e in enumerate(evs):
            ic = sim_incut[i]
            for s in np.nonzero(ic)[0]:
                if binsel_sim(i, s):
                    den += 1
        print()
        print('=== %s ===  denom %d sims' % (name, den))
        print('  LST : cand/evt %6.1f  cov %5d (%.5f)  fake %.5f'
              % (mL.sum() / nev, len(covL_b), len(covL_b) / max(den, 1), float(L_fake[mL].mean())))
        print('  %7s %9s %9s %9s %9s %9s' % ('theta', 'cand/evt', 'cov', 'covFrac', 'fake', 'uniqUs'))
        for th in [0.0, 4.0, 6.0, 7.0, 8.0, 8.19, 9.0, 10.05, 11.0]:
            m = O_lg >= th
            cov = covered(O_ev, O_sims, m)
            cov_b = set(x for x in cov if binsel_sim(x[0], x[1]))
            mc = m & binsel_cand(O_pt, O_eta) & (O_pt > 0.9)
            if mc.sum() == 0:
                continue
            print('  %7.1f %9.1f %9d %9.5f %9.5f %9d'
                  % (th, mc.sum() / nev, len(cov_b), len(cov_b) / max(den, 1),
                     float(O_fake[mc].mean()), len(cov_b - covL_b)))

    for nm, lo, hi in ETA_REG:
        breakdown('ETA %s [%.1f,%.1f)' % (nm, lo, hi),
                  lambda i, s, lo=lo, hi=hi: lo <= abs(sim_eta[i][s]) < hi,
                  lambda pt, eta, lo=lo, hi=hi: (np.abs(eta) >= lo) & (np.abs(eta) < hi))
    for nm, lo, hi in PT_BANDS:
        breakdown('PT %s' % nm,
                  lambda i, s, lo=lo, hi=hi: lo <= sim_pt[i][s] < hi,
                  lambda pt, eta, lo=lo, hi=hi: (pt >= lo) & (pt < hi))

    np.save('/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/gen_a_ref/O_feat.npy', O_feat)
    np.save('/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/gen_a_ref/O_misc.npy',
            np.column_stack([O_lg, O_pt, O_eta, O_fake.astype(float), O_ev.astype(float)]))


if __name__ == '__main__':
    main()
