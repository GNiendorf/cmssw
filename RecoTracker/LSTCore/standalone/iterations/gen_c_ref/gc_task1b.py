#!/usr/bin/env python3
"""GEN-C TASK 1b -- the REGION-LOCAL and pT-LOCAL matched-count comparison.

The global matched-count scan hides an eta-dependent calibration failure: at one global
margin our delivery mix is nothing like LST's (barrel starved, endcap flooded). This
script therefore matches the row count SEPARATELY IN EACH REGION (and each pT band) and
compares purity and coverage there, which is the only way to say whether our MATCHING is
worse in a regime or only our THRESHOLD is.

Usage: gc_task1b.py <pickle>
"""
import pickle
import sys

D = pickle.load(open(sys.argv[1], 'rb'))
NEV = float(D['nev'])
EV = D['evts']

REG = [('barrel  |eta|<1.1', 0.0, 1.1), ('transition 1.1-1.7', 1.1, 1.7),
       ('endcap   |eta|>1.7', 1.7, 99.0)]
PTB = [('pt 0.9-1.5', 0.9, 1.5), ('pt 1.5-3', 1.5, 3.0),
       ('pt 3-10', 3.0, 10.0), ('pt 10+', 10.0, 1e9)]


def band(v, bands):
    for i, (_, lo, hi) in enumerate(bands):
        if lo <= abs(v) < hi:
            return i
    return -1


def analyse(sel_lst, sel_ours, bands, keyfun_lst, keyfun_ours, title):
    """sel_* pick a band index for a row; keyfun_* give the binning value."""
    print('=' * 100)
    print(title)
    print('=' * 100)
    nb = len(bands)
    # LST reference per band (pre-clean and delivered)
    for refname, refkey in (('LST pre-clean', 'lst_pre'), ('LST delivered', 'lst_post')):
        rows = [0] * nb
        fake = [0] * nb
        covs = [set() for _ in range(nb)]
        othr = [set() for _ in range(nb)]
        den = [0] * nb
        cov = [0] * nb
        uni = [0] * nb
        for e in EV:
            oth = set()
            for sa in e['lst_other']:
                oth.update(sa)
            covered = [set() for _ in range(nb)]
            for r in e[refkey]:
                b = band(keyfun_lst(r), bands)
                if b < 0:
                    continue
                rows[b] += 1
                fake[b] += r[2]
                covered[b].update(r[3])
            for k in range(len(e['incut'])):
                if not e['incut'][k]:
                    continue
                sb = band(e['sim_eta'][k] if bands is REG else e['sim_pt'][k], bands)
                if sb < 0:
                    continue
                den[sb] += 1
                if k in covered[sb] or any(k in covered[j] for j in range(nb)):
                    pass
                if k in covered[sb]:
                    cov[sb] += 1
                    if k not in oth:
                        uni[sb] += 1
        print('  %-22s %10s %8s %8s %8s %8s' % (refname, 'rows/ev', 'fakefrc', 'cov', 'uniq', 'den'))
        for i, (nm, _, _) in enumerate(bands):
            print('    %-20s %10.1f %8.4f %8.2f %8.2f %8.1f' %
                  (nm, rows[i] / NEV, fake[i] / float(rows[i]) if rows[i] else 0,
                   cov[i] / NEV, uni[i] / NEV, den[i] / NEV))
        yield refname, [r / NEV for r in rows], [cov[i] / NEV for i in range(nb)], \
            [uni[i] / NEV for i in range(nb)], [fake[i] / float(rows[i]) if rows[i] else 0 for i in range(nb)]


def our_band_curve(bands, keyfun):
    """Per band: sorted list of (logit, fake, sims) rows, plus per-event other-coverage."""
    nb = len(bands)
    rows = [[] for _ in range(nb)]
    for ei, e in enumerate(EV):
        for r in e['ours']:
            b = band(keyfun(r), bands)
            if b >= 0:
                rows[b].append((r[2], r[5], tuple(r[6]), ei))
    for b in range(nb):
        rows[b].sort(key=lambda x: -x[0])
    return rows


def our_at_count(rowsb, target_rows_per_evt):
    """Take the top-N rows of a band by logit so the count matches; return metrics."""
    n = int(round(target_rows_per_evt * NEV))
    sel = rowsb[:n]
    fake = sum(1 for r in sel if r[1])
    per_evt = {}
    for r in sel:
        per_evt.setdefault(r[3], set()).update(r[2])
    thr = sel[-1][0] if sel else float('inf')
    return len(sel), (fake / float(len(sel)) if sel else 0.0), per_evt, thr


print('#' * 100)
print('# GEN-C TASK 1b -- REGION-LOCAL / pT-LOCAL matched-count comparison, 300 evts')
print('#' * 100)

for bands, keyl, keyo, title, simkey in (
        (REG, lambda r: r[1], lambda r: r[1], 'BY ETA REGION (rows binned on the ROW eta, sims on SIM eta)', 'sim_eta'),
        (PTB, lambda r: r[0], lambda r: r[0], 'BY pT BAND (rows binned on the ROW pt, sims on SIM pt)', 'sim_pt')):
    nb = len(bands)
    refs = list(analyse(None, None, bands, keyl, keyo, title))
    ourrows = our_band_curve(bands, keyo)
    # our per-event coverage by everything else we deliver
    oth_per_evt = []
    for e in EV:
        s = set()
        for sa in e['our_other']:
            s.update(sa)
        oth_per_evt.append(s)
    for refname, rrows, rcov, runi, rfake in refs:
        print('  --- OURS at the SAME per-band row count as %s ---' % refname)
        print('    %-20s %10s %8s %8s %8s %8s' % ('band', 'rows/ev', 'fakefrc', 'cov', 'uniq', 'thr'))
        for i, (nm, lo, hi) in enumerate(bands):
            n, fk, per_evt, thr = our_at_count(ourrows[i], rrows[i])
            cov = 0
            uni = 0
            for ei, e in enumerate(EV):
                got = per_evt.get(ei, set())
                for k in range(len(e['incut'])):
                    if not e['incut'][k]:
                        continue
                    v = e[simkey][k]
                    if band(v, bands) != i:
                        continue
                    if k in got:
                        cov += 1
                        if k not in oth_per_evt[ei]:
                            uni += 1
            print('    %-20s %10.1f %8.4f %8.2f %8.2f %8.2f   [LST %8.4f %8.2f %8.2f]' %
                  (nm, n / NEV, fk, cov / NEV, uni / NEV, thr, rfake[i], rcov[i], runi[i]))
        print()
