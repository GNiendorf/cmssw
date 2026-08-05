#!/usr/bin/env python3
"""A09 "two laws" table. For each tag: the truth partition (class-A survivors, class-B
retired) next to the measured eff/dup/fake, plus the prediction from

    dup = A0 + A1 * survA          (duplicate rate is set by class-A survivors alone)
    eff = B0 - B1 * Bretired       (efficiency is set by class-B retirements alone)

Re-fits both laws over whatever tags are given, so the fit is always over the current data.
Usage: a09_laws.py <tag> [<tag> ...]
"""
import json
import os
import sys

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [S + 'a09_ref/', S + 'fin_ref/', S + 'xc_ref/']


def get(tag):
    for d in DIRS:
        jp, lp = d + 'r_%s.json' % tag, d + 'r_%s.log' % tag
        if not os.path.exists(jp):
            continue
        m = json.load(open(jp))['metrics']
        survA = bret = None
        if os.path.exists(lp):
            for line in open(lp):
                s = line.strip()
                if s.startswith('XC truth        A true'):
                    f = s.split()
                    survA = float(f[-1])
                elif s.startswith('XC truth        B true'):
                    f = s.split()
                    # N consumed RPSblock XCretire survive ; retired = RPSblock + XCretire
                    bret = float(f[-3]) + float(f[-2])
        return survA, bret, m['dup_overall_incut']['proto'], m['eff_overall_incut']['proto'], \
            m['fake_overall_incut']['proto']
    return None


rows = []
for t in sys.argv[1:]:
    r = get(t)
    if r is None or r[0] is None:
        print('# skip %s (no json or no -XCD 2 partition)' % t)
        continue
    rows.append((t,) + r)


def fit(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    den = sum((x - mx) ** 2 for x in xs)
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / den if den else 0.0
    return my - b * mx, b


a0, a1 = fit([r[1] for r in rows], [r[3] for r in rows])
b0, b1 = fit([r[2] for r in rows], [r[4] for r in rows])
print('dup = %.6f + %.8f * survA' % (a0, a1))
print('eff = %.6f + %.8f * Bretired' % (b0, b1))
print('%-12s %7s %8s %9s %9s %9s %9s %9s' % ('tag', 'survA', 'Bretired', 'dup', 'fit', 'eff', 'fit', 'fake'))
for t, sa, br, du, ef, fa in rows:
    print('%-12s %7.1f %8.1f %9.5f %9.5f %9.5f %9.5f %9.5f'
          % (t, sa, br, du, a0 + a1 * sa, ef, b0 + b1 * br, fa))
