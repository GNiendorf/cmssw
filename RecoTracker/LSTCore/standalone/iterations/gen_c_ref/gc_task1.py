#!/usr/bin/env python3
"""GEN-C TASK 1 -- is OUR pT3 matching better or worse than LST's?  Dedup ignored on BOTH
sides. Reads the pickle from gc_extract.py.

Compared sets (all with the same >75% hit-truth rule and the same in-cut sim denominator):
  LSTpre   LST's COMPLETE pre-cleaning pT3 candidate set (pT3_* rows)
  LSTpost  LST's DELIVERED pT3 rows (tc_type == 5) -- the 151.7/evt the campaign retires
  ours@T   our bare-T3 deliveries with stage-B logit >= T (monotone re-thresholding of a
           single low-margin run)

Reported: rows/evt, row fake fraction, in-cut sim coverage (total / unique-to-set), and
the same split by eta region (barrel |eta|<1.1, transition 1.1-1.7, endcap >1.7) and by
sim pT band. Purity is binned on the ROW's own eta/pt, coverage on the SIM's.

Usage: gc_task1.py <pickle> [--curve-only]
"""
import bisect
import pickle
import sys

PKL = sys.argv[1]
D = pickle.load(open(PKL, 'rb'))
NEV = float(D['nev'])
EV = D['evts']

REG = [('barrel', 0.0, 1.1), ('transition', 1.1, 1.7), ('endcap', 1.7, 99.0)]
PTB = [('0.9-1.5', 0.9, 1.5), ('1.5-3', 1.5, 3.0), ('3-10', 3.0, 10.0), ('10+', 10.0, 1e9)]


def regof(eta):
    a = abs(eta)
    for i, (_, lo, hi) in enumerate(REG):
        if lo <= a < hi:
            return i
    return len(REG) - 1


def ptof(pt):
    for i, (_, lo, hi) in enumerate(PTB):
        if lo <= pt < hi:
            return i
    return -1


def score(getrows, other_key):
    """getrows(evt) -> list of (pt, eta, fake, localsims). other_key names the per-event
    list of match lists for every OTHER delivered object of that same pipeline."""
    nR = 0
    nF = 0
    rowsR = [0] * len(REG)
    fakeR = [0] * len(REG)
    rowsP = [0] * len(PTB)
    fakeP = [0] * len(PTB)
    covR = [0] * len(REG)
    denR = [0] * len(REG)
    uniR = [0] * len(REG)
    covP = [0] * len(PTB)
    denP = [0] * len(PTB)
    uniP = [0] * len(PTB)
    cov = 0
    den = 0
    uni = 0
    simset = []   # per event: set of in-cut sims covered by this candidate set
    for e in EV:
        rows = getrows(e)
        covered = set()
        for (pt, eta, fk, sims) in rows:
            nR += 1
            nF += fk
            r = regof(eta)
            rowsR[r] += 1
            fakeR[r] += fk
            b = ptof(pt)
            if b >= 0:
                rowsP[b] += 1
                fakeP[b] += fk
            covered.update(sims)
        oth = set()
        for sa in e[other_key]:
            oth.update(sa)
        ic = e['incut']
        spt = e['sim_pt']
        seta = e['sim_eta']
        keep = set()
        for k in range(len(ic)):
            if not ic[k]:
                continue
            den += 1
            r = regof(seta[k])
            denR[r] += 1
            b = ptof(spt[k])
            if b >= 0:
                denP[b] += 1
            if k in covered:
                keep.add(k)
                cov += 1
                covR[r] += 1
                if b >= 0:
                    covP[b] += 1
                if k not in oth:
                    uni += 1
                    uniR[r] += 1
                    if b >= 0:
                        uniP[b] += 1
        simset.append(keep)
    return dict(rows=nR / NEV, fake=(nF / float(nR) if nR else 0.0), nrows=nR,
                cov=cov / NEV, den=den / NEV, uni=uni / NEV,
                rowsR=[x / NEV for x in rowsR],
                fakeR=[(fakeR[i] / float(rowsR[i]) if rowsR[i] else 0.0) for i in range(len(REG))],
                rowsP=[x / NEV for x in rowsP],
                fakeP=[(fakeP[i] / float(rowsP[i]) if rowsP[i] else 0.0) for i in range(len(PTB))],
                covR=[x / NEV for x in covR], denR=[x / NEV for x in denR],
                uniR=[x / NEV for x in uniR],
                covP=[x / NEV for x in covP], denP=[x / NEV for x in denP],
                uniP=[x / NEV for x in uniP],
                simset=simset)


def ours_at(T):
    return lambda e: [(r[0], r[1], r[5], r[6]) for r in e['ours'] if r[2] >= T]


lstpre = score(lambda e: e['lst_pre'], 'lst_other')
lstpost = score(lambda e: e['lst_post'], 'lst_other')

print('=' * 108)
print('GEN-C TASK 1 -- pT3-CLASS MATCHING, DEDUP IGNORED ON BOTH SIDES   (300 evts PU200RelVal)')
print('=' * 108)
print('in-cut sims/evt (harness ef_denom): %.1f' % lstpre['den'])
print()
hdr = ('%-16s%9s%9s%9s%9s | %s' %
       ('set', 'rows/ev', 'fakefrc', 'simcov', 'uniq', '   '.join(r[0] for r in REG)))
print(hdr)


def line(name, m):
    print('%-16s%9.1f%9.4f%9.1f%9.1f | cov %5.1f/%5.1f  %5.1f/%5.1f  %5.1f/%5.1f' %
          (name, m['rows'], m['fake'], m['cov'], m['uni'],
           m['covR'][0], m['denR'][0], m['covR'][1], m['denR'][1], m['covR'][2], m['denR'][2]))


line('LST pre-clean', lstpre)
line('LST delivered', lstpost)

# ---- the curve ------------------------------------------------------------------------
print()
print('OUR CURVE (stage-B margin scan; row set = deliveries with logit >= T)')
print('%-8s%9s%9s%9s%9s   %s' % ('T', 'rows/ev', 'fakefrc', 'simcov', 'uniq',
                                 'fake by region B/T/E      cov by region B/T/E'))
THR = [-8, -6, -4, -2, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20]
curve = []
for T in THR:
    m = score(ours_at(T), 'our_other')
    curve.append((T, m))
    print('%-8.1f%9.1f%9.4f%9.1f%9.1f   %.4f %.4f %.4f      %5.1f %5.1f %5.1f' %
          (T, m['rows'], m['fake'], m['cov'], m['uni'],
           m['fakeR'][0], m['fakeR'][1], m['fakeR'][2],
           m['covR'][0], m['covR'][1], m['covR'][2]))


def interp_at_rows(target_rows):
    """Threshold whose row count matches target_rows (curve is monotone decreasing in T)."""
    xs = [(m['rows'], T) for T, m in curve]
    xs.sort()
    rs = [x[0] for x in xs]
    i = bisect.bisect_left(rs, target_rows)
    if i <= 0:
        return xs[0][1]
    if i >= len(xs):
        return xs[-1][1]
    r0, t0 = xs[i - 1]
    r1, t1 = xs[i]
    if r1 == r0:
        return t1
    return t0 + (t1 - t0) * (target_rows - r0) / (r1 - r0)


print()
for name, ref in (('LST pre-clean', lstpre), ('LST delivered', lstpost)):
    Tm = interp_at_rows(ref['rows'])
    m = score(ours_at(Tm), 'our_other')
    print('MATCHED-COUNT vs %s (%.1f rows/evt) -> our T = %.2f' % (name, ref['rows'], Tm))
    print('   %-14s rows %7.1f  fake %.4f  simcov %6.1f  uniq %5.1f' %
          ('LST', ref['rows'], ref['fake'], ref['cov'], ref['uni']))
    print('   %-14s rows %7.1f  fake %.4f  simcov %6.1f  uniq %5.1f' %
          ('ours', m['rows'], m['fake'], m['cov'], m['uni']))
    print('   per region (barrel/transition/endcap)')
    print('      rows LST %6.1f %6.1f %6.1f | ours %6.1f %6.1f %6.1f' %
          (tuple(ref['rowsR']) + tuple(m['rowsR'])))
    print('      fake LST %6.4f %6.4f %6.4f | ours %6.4f %6.4f %6.4f' %
          (tuple(ref['fakeR']) + tuple(m['fakeR'])))
    print('      cov  LST %6.1f %6.1f %6.1f | ours %6.1f %6.1f %6.1f  (den %.1f %.1f %.1f)' %
          (tuple(ref['covR']) + tuple(m['covR']) + tuple(ref['denR'])))
    print('      uniq LST %6.1f %6.1f %6.1f | ours %6.1f %6.1f %6.1f' %
          (tuple(ref['uniR']) + tuple(m['uniR'])))
    print('   per pT band %s' % ('/'.join(b[0] for b in PTB)))
    print('      rows LST %s | ours %s' %
          (' '.join('%6.1f' % x for x in ref['rowsP']), ' '.join('%6.1f' % x for x in m['rowsP'])))
    print('      fake LST %s | ours %s' %
          (' '.join('%6.4f' % x for x in ref['fakeP']), ' '.join('%6.4f' % x for x in m['fakeP'])))
    print('      cov  LST %s | ours %s  (den %s)' %
          (' '.join('%6.1f' % x for x in ref['covP']), ' '.join('%6.1f' % x for x in m['covP']),
           ' '.join('%.1f' % x for x in ref['denP'])))
    # set overlap of the two coverage sets
    a = b = both = 0
    for i in range(len(EV)):
        L = ref['simset'][i]
        O = m['simset'][i]
        both += len(L & O)
        a += len(L - O)
        b += len(O - L)
    print('   SIM-SET OVERLAP: shared %.1f/evt | LST-only %.1f/evt | ours-only %.1f/evt' %
          (both / NEV, a / NEV, b / NEV))
    print()
