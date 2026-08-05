#!/usr/bin/env python3
"""GEN-B TASK 1 -- is OUR pT3-class matching better or worse than LST's?

Reads the -PT3C dump (gen_b_ref/pt3cmp300.txt), which carries BOTH candidate sets with
the SAME ported >0.75 hit-fraction matcher:
  O = our stage-B bare-T3 deliveries BEFORE any dedup (pixel seed-family OR OT), taken
      at -AT3 -6 so every threshold is reachable offline (monotonicity lemma).
  L = LST's COMPLETE pre-cleaning pT3 candidate set (the pT3_* branches are written
      before crossCleanpT3 touches anything).
  S = the accepted sim table.

Reported: count, purity, sim coverage; globally, per eta region, per pT band; and the
full threshold curve with LST's two operating points (pre-clean 516.8/evt, post-clean
151.7/evt) placed on it.
"""
import sys
import numpy as np

P = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/gen_b_ref/'
PATH = sys.argv[1] if len(sys.argv) > 1 else P + 'pt3cmp300.txt'
PTCUT = 0.9
ETA_REG = [('barrel', 0.0, 1.1), ('transit', 1.1, 1.7), ('endcap', 1.7, 99.)]
PT_BAND = [('0.9-1.5', 0.9, 1.5), ('1.5-3', 1.5, 3.0), ('3-10', 3.0, 10.0), ('10+', 10.0, 1e9)]

# ---------------------------------------------------------------- load
sim_pt, sim_eta = {}, {}          # (evt,row) -> value
ours = []                          # evt, logit, pt, eta, nTot, fk, pr, ds, t3pt, t3rad, t3, pls
lst = []                           # evt, pt, eta, nTot, t3, pls
osims, lsims = [], []              # matched ACCEPTED sim rows per candidate
nev = 0
with open(PATH) as fh:
    for ln in fh:
        if ln[0] == '#':
            continue
        w = ln.split()
        if w[0] == 'S':
            e = int(w[1]); r = int(w[2])
            sim_pt[(e, r)] = float(w[3]); sim_eta[(e, r)] = float(w[4])
            nev = max(nev, e + 1)
        elif w[0] == 'O':
            e = int(w[1])
            ours.append((e, float(w[4]), float(w[5]), float(w[6]), int(w[12]),
                         float(w[7]), float(w[8]), float(w[9]), float(w[10]), float(w[11]),
                         int(w[2]), int(w[3])))
            osims.append([int(x) for x in w[14:]])
        elif w[0] == 'L':
            e = int(w[1])
            lst.append((e, float(w[4]), float(w[5]), int(w[6]), int(w[2]), int(w[3])))
            lsims.append([int(x) for x in w[8:]])
NEV = nev
O = np.array([r[:10] for r in ours], dtype=np.float64) if ours else np.zeros((0, 10))
Oi = np.array([[r[0], r[10], r[11]] for r in ours], dtype=np.int64) if ours else np.zeros((0, 3), np.int64)
L = np.array([r[:4] for r in lst], dtype=np.float64) if lst else np.zeros((0, 4))
Li = np.array([[r[0], r[4], r[5]] for r in lst], dtype=np.int64) if lst else np.zeros((0, 3), np.int64)
o_evt, o_logit, o_pt, o_eta, o_ntot = O[:, 0], O[:, 1], O[:, 2], O[:, 3], O[:, 4]
l_evt, l_pt, l_eta, l_ntot = L[:, 0], L[:, 1], L[:, 2], L[:, 3]

# in-cut sim universe
incut = [(e, r) for (e, r) in sim_pt if sim_pt[(e, r)] > PTCUT]
incut_set = set(incut)
print('events %d | accepted sims %d | in-cut (pt>%.1f) sims %d (%.1f/evt)'
      % (NEV, len(sim_pt), PTCUT, len(incut), len(incut) / NEV))
print('our pre-dedup deliveries (theta=-6) %d = %.1f/evt | LST pre-clean pT3 %d = %.1f/evt'
      % (len(ours), len(ours) / NEV, len(lst), len(lst) / NEV))


def cov_sets(cands, sims, mask):
    """set of (evt,simRow) in-cut sims covered by the selected candidates"""
    out = set()
    idx = np.nonzero(mask)[0]
    for j in idx:
        e = cands[j][0]
        for s in sims[j]:
            k = (e, s)
            if k in incut_set:
                out.add(k)
    return out


def summarize(name, cands, sims, mask, ptv, etav, ntotv):
    n = int(mask.sum())
    fk = int(((ntotv == 0) & mask).sum())
    pmask = mask & (ptv > PTCUT)
    npt = int(pmask.sum()); fkpt = int(((ntotv == 0) & pmask).sum())
    cov = cov_sets(cands, sims, mask)
    return dict(name=name, n=n, n_evt=n / NEV, fake=fk / max(n, 1),
                n_incut=npt, fake_incut=fkpt / max(npt, 1),
                cov=len(cov), cov_evt=len(cov) / NEV, covset=cov)


def region(v, lo, hi):
    return (np.abs(v) >= lo) & (np.abs(v) < hi)


# ---------------------------------------------------------------- threshold curve
print('\n=== OUR THRESHOLD CURVE (pre-dedup) vs LST OPERATING POINTS ===')
print('%8s %9s %9s %9s %9s %9s' % ('thetaT3', 'rows/evt', 'fakefrac', 'fake(pt>.9)', 'simcov/evt', 'cov/row'))
ths = [-6, -4, -2, 0, 1, 2, 3, 4, 5, 6, 7, 8, 8.5, 9, 9.5, 10, 10.5, 11, 11.5, 12]
curve = {}
for th in ths:
    m = o_logit >= th
    s = summarize('th%g' % th, ours, osims, m, o_pt, o_eta, o_ntot)
    curve[th] = s
    print('%8.1f %9.1f %9.4f %9.4f %11.1f %9.4f'
          % (th, s['n_evt'], s['fake'], s['fake_incut'], s['cov_evt'], s['cov_evt'] / max(s['n_evt'], 1e-9)))

Lall = summarize('LST-all', lst, lsims, np.ones(len(lst), bool), l_pt, l_eta, l_ntot)
print('%8s %9.1f %9.4f %9.4f %11.1f %9.4f'
      % ('LSTpre', Lall['n_evt'], Lall['fake'], Lall['fake_incut'], Lall['cov_evt'],
         Lall['cov_evt'] / Lall['n_evt']))


def th_for_count(target_evt):
    """lowest theta whose row count/evt is <= target"""
    lo, hi = -6.0, 20.0
    for _ in range(60):
        mid = 0.5 * (lo + hi)
        if (o_logit >= mid).sum() / NEV > target_evt:
            lo = mid
        else:
            hi = mid
    return hi


TH_PRE = th_for_count(Lall['n_evt'])
TH_POST = th_for_count(151.7)
print('\nmatched-count thresholds: theta=%.3f -> %.1f rows/evt (== LST pre-clean %.1f)'
      % (TH_PRE, (o_logit >= TH_PRE).sum() / NEV, Lall['n_evt']))
print('                          theta=%.3f -> %.1f rows/evt (== LST post-clean 151.7)'
      % (TH_POST, (o_logit >= TH_POST).sum() / NEV))

# ---------------------------------------------------------------- head to head
print('\n=== HEAD TO HEAD (matched count) ===')
rows = []
rows.append(('LST pre-clean (all pT3 cands)', Lall))
for th, lab in ((TH_PRE, 'OURS @ LST-preclean count'), (TH_POST, 'OURS @ LST-postclean count')):
    rows.append((lab, summarize(lab, ours, osims, o_logit >= th, o_pt, o_eta, o_ntot)))
print('%-32s%9s%9s%12s%11s' % ('set', 'rows/evt', 'fakefrc', 'fake(pt>.9)', 'simcov/evt'))
for lab, s in rows:
    print('%-32s%9.1f%9.4f%12.4f%11.1f' % (lab, s['n_evt'], s['fake'], s['fake_incut'], s['cov_evt']))

# coverage overlap at matched count
for th, lab in ((TH_PRE, 'preclean-count'), (TH_POST, 'postclean-count')):
    a = summarize('x', ours, osims, o_logit >= th, o_pt, o_eta, o_ntot)['covset']
    b = Lall['covset']
    print('  overlap @%-16s ours-only %6.2f/evt | LST-only %6.2f/evt | shared %7.2f/evt'
          % (lab, len(a - b) / NEV, len(b - a) / NEV, len(a & b) / NEV))

# ---------------------------------------------------------------- per region
print('\n=== PER ETA REGION (coverage binned by SIM eta, purity by CANDIDATE eta) ===')
sim_by_reg = {}
for rn, lo, hi in ETA_REG:
    sim_by_reg[rn] = set(k for k in incut_set if lo <= abs(sim_eta[k]) < hi)
    print('  in-cut sims %-8s %8.1f/evt' % (rn, len(sim_by_reg[rn]) / NEV))


def region_table(title, th):
    print('\n  %s' % title)
    print('  %-9s%10s%10s%10s%10s%10s%10s%10s' %
          ('region', 'LSTrows', 'OURrows', 'LSTfake', 'OURfake', 'LSTcov', 'OURcov', 'dCov'))
    for rn, lo, hi in ETA_REG:
        lm = region(l_eta, lo, hi)
        om = (o_logit >= th) & region(o_eta, lo, hi)
        ls_ = summarize('l', lst, lsims, lm, l_pt, l_eta, l_ntot)
        os_ = summarize('o', ours, osims, om, o_pt, o_eta, o_ntot)
        lc = len(Lall['covset'] & sim_by_reg[rn]) / NEV
        oc = len(summarize('o2', ours, osims, o_logit >= th, o_pt, o_eta, o_ntot)['covset']
                 & sim_by_reg[rn]) / NEV
        print('  %-9s%10.1f%10.1f%10.4f%10.4f%10.2f%10.2f%+10.2f'
              % (rn, ls_['n_evt'], os_['n_evt'], ls_['fake'], os_['fake'], lc, oc, oc - lc))


region_table('at OUR theta = %.2f (matched to LST PRE-clean count)' % TH_PRE, TH_PRE)
region_table('at OUR theta = %.2f (matched to LST POST-clean count)' % TH_POST, TH_POST)

# ---------------------------------------------------------------- per pT
print('\n=== PER pT BAND (coverage binned by SIM pt, purity by CANDIDATE pt) ===')
sim_by_pt = {}
for bn, lo, hi in PT_BAND:
    sim_by_pt[bn] = set(k for k in incut_set if lo <= sim_pt[k] < hi)


def pt_table(title, th):
    print('\n  %s' % title)
    print('  %-9s%10s%10s%10s%10s%10s%10s%10s' %
          ('ptband', 'LSTrows', 'OURrows', 'LSTfake', 'OURfake', 'LSTcov', 'OURcov', 'dCov'))
    ocov = summarize('o2', ours, osims, o_logit >= th, o_pt, o_eta, o_ntot)['covset']
    for bn, lo, hi in PT_BAND:
        lm = (l_pt >= lo) & (l_pt < hi)
        om = (o_logit >= th) & (o_pt >= lo) & (o_pt < hi)
        ls_ = summarize('l', lst, lsims, lm, l_pt, l_eta, l_ntot)
        os_ = summarize('o', ours, osims, om, o_pt, o_eta, o_ntot)
        lc = len(Lall['covset'] & sim_by_pt[bn]) / NEV
        oc = len(ocov & sim_by_pt[bn]) / NEV
        print('  %-9s%10.1f%10.1f%10.4f%10.4f%10.2f%10.2f%+10.2f'
              % (bn, ls_['n_evt'], os_['n_evt'], ls_['fake'], os_['fake'], lc, oc, oc - lc))


pt_table('at OUR theta = %.2f (matched to LST PRE-clean count)' % TH_PRE, TH_PRE)
pt_table('at OUR theta = %.2f (matched to LST POST-clean count)' % TH_POST, TH_POST)

# ---------------------------------------------------------------- pair overlap
print('\n=== PAIR-LEVEL OVERLAP ((t3,pls) identity; same index spaces) ===')
lpairs = set(map(tuple, Li))
for th in (-6, TH_PRE, TH_POST, 6):
    m = o_logit >= th
    op = set(map(tuple, Oi[m]))
    print('  theta %6.2f: ours %7.1f/evt | of LST pairs reproduced %6.1f%% | ours not in LST %6.1f%%'
          % (th, m.sum() / NEV, 100.0 * len(op & lpairs) / max(len(lpairs), 1),
             100.0 * len(op - lpairs) / max(len(op), 1)))

# ---------------------------------------------------------------- T3 score separation
print('\n=== T3 OWN-DNN SCORE SEPARATION on OUR deliveries (Task-2 feature probe) ===')
fkm = (o_ntot == 0)
for i, nm in ((5, 't3_fakeScore'), (6, 't3_promptScore'), (7, 't3_displacedScore')):
    v = O[:, i]
    print('  %-20s real mean %8.4f med %8.4f | fake mean %8.4f med %8.4f'
          % (nm, v[~fkm].mean(), np.median(v[~fkm]), v[fkm].mean(), np.median(v[fkm])))
try:
    from sklearn.metrics import roc_auc_score
    for i, nm in ((1, 'attach logit'), (5, 't3_fakeScore'), (6, 't3_promptScore'),
                  (7, 't3_displacedScore')):
        print('  AUC(real vs fake) %-20s %.4f' % (nm, roc_auc_score(~fkm, O[:, i])))
except Exception as e:
    print('  (sklearn unavailable: %s)' % e)
