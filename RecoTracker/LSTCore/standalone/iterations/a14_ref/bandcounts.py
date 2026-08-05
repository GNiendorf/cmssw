#!/usr/bin/env python3
"""Per-eta-band NUMERATORS and DENOMINATORS out of the perf hists.

The scoreboard reports per-region RATES, but the finish line is judged on the GLOBAL
rates. A per-region rate delta only says how a change lands globally once it is weighted
by that band's share of the global denominator -- and those shares are very unequal here
(the endcap holds most of the TCs but a minority of the sims). This prints the raw counts
so regional exchange rates can be compared in GLOBAL units.

Usage: bandcounts.py <tag> [<tag> ...]     (add --base to read the LST reference instead)
"""
import os
import sys

import uproot

S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [S + d + '/' for d in ('a14_ref', 'fin_ref', 'xc_ref', 'rebase_ref')]
BANDS = [('barrel', 0.0, 1.1), ('transit', 1.1, 1.7), ('endcap', 1.7, None)]
SETS = [('EFF', 'Root__TC_base_0_0_ef_'), ('FAKE', 'Root__TC_fr_'), ('DUP', 'Root__TC_dr_')]


def find(tag):
    if tag == 'LST':
        return S + 'rebase_ref/rb_base300_hists.root'
    for d in DIRS:
        p = d + 'r_%s_hists.root' % tag
        if os.path.exists(p):
            return p
    raise SystemExit('no hists for tag ' + tag)


def band(h, lo, hi):
    vals, edges = h.to_numpy()
    tot = 0.0
    for i, v in enumerate(vals):
        c = abs(0.5 * (edges[i] + edges[i + 1]))
        if c >= lo and (hi is None or c < hi):
            tot += v
    return tot


def table(tag):
    f = uproot.open(find(tag))
    keys = {k.split(';')[0] for k in f.keys()}
    out = {}
    for label, stem in SETS:
        if stem + 'numer_eta' not in keys:
            continue
        num, den = f[stem + 'numer_eta'], f[stem + 'denom_eta']
        for bn, lo, hi in BANDS + [('ALL', 0.0, None)]:
            out[(label, bn)] = (band(num, lo, hi), band(den, lo, hi))
    return out


def main():
    tags = sys.argv[1:]
    tabs = [(t, table(t)) for t in tags]
    print('%-6s %-8s' % ('what', 'band') + ''.join('%24s' % t for t in tags))
    print('%-6s %-8s' % ('', '') + ''.join('%10s%7s%7s' % ('numer', 'denom', 'rate') for _ in tags))
    for label, _ in SETS:
        for bn, _, _ in BANDS + [('ALL', 0.0, None)]:
            line = '%-6s %-8s' % (label, bn)
            for _, tb in tabs:
                if (label, bn) not in tb:
                    line += '%24s' % 'n/a'
                    continue
                n, d = tb[(label, bn)]
                line += '%10.0f%7.0f%7.4f' % (n, d, (n / d) if d else 0.0)
            print(line)


if __name__ == '__main__':
    main()
