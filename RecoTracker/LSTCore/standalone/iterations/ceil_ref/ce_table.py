#!/usr/bin/env python3
"""M22 CEIL -- the two deliverable tables.

  (1) CONDITIONED-UNIVERSE COMPOSITION vs the unconditioned 1133 = 884/51/91/107.
  (2) THE ORACLE CEILING under every ownership rule, on both universes, alongside A's
      published numbers.

A's A5 table quotes "max real eff" = (17642 + 0.9 * labNewSims) / 22784 -- a flat 10%
haircut on the lab's new-sim count (reverse-engineered exactly from all four of his rows).
The same convention is applied here so the columns are directly comparable; the raw lab
new-sim count is printed too, and it is the quantity to trust.
"""
import os
import sys

CE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, CE)
from ce_lab import load, run, compose, EFFDEN, EFFNUM0   # noqa: E402

A5 = {'nodedup': (3.7, 1061, 0.8162), 'veto-on-3': (3.5, 1023, 0.8147),
      'veto-on-2': (3.2, 929, 0.8110), 'veto-on-1': (2.8, 826, 0.8069)}

RULES = [('nodedup', dict(rule='none')),
         ('veto-on-1', dict(rule='n', ccN=1)),
         ('veto-on-2', dict(rule='n', ccN=2)),
         ('veto-on-3', dict(rule='n', ccN=3)),
         ('layer-contain', dict(rule='layer'))]

# universe tag -> (audit file, the rule its conditioning applied)
UNIV = [('U0', 'unconditioned', None),
        ('C1', 'conditioned -CEN 1', 'veto-on-1'),
        ('C2', 'conditioned -CEN 2', 'veto-on-2'),
        ('C3', 'conditioned -CEN 3', 'veto-on-3'),
        ('CL', 'conditioned -CEL 1', 'layer-contain')]


def realeff(newSims):
    return (EFFNUM0 + 0.9 * newSims) / EFFDEN


def main():
    nev = int(sys.argv[1]) if len(sys.argv) > 1 else None
    theta = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0
    evs = {}
    for tag, _, _ in UNIV:
        p = os.path.join(CE, 'aud_%s.txt' % tag)
        if os.path.exists(p):
            evs[tag] = load(p, nev)
            print('loaded %s: %d events' % (tag, len(evs[tag])), flush=True)

    print('\n================ (1) CANDIDATE-UNIVERSE COMPOSITION at theta=%.1f ============'
          % theta)
    print('%-22s %9s %9s %9s %9s %9s %9s %9s' %
          ('universe', 'targ/e', 'rows/e', 'dupChain', 'dupEach', 'fake', 'NEW', 'newSims'))
    print('%-22s %9s %9s %9.1f %9.1f %9.1f %9.1f %9s' %
          ('GEN-C reference', '-', '1132.7', 884.4, 50.6, 90.8, 106.9, '-'))
    for tag, name, _ in UNIV:
        if tag not in evs:
            continue
        c = compose(evs[tag], theta)
        print('%-22s %9.0f %9.1f %9.1f %9.1f %9.1f %9.1f %9.0f' %
              ('%s (%s)' % (tag, name), c['targets'], c['rows'], c['dupChain'], c['dupEach'],
               c['fake'], c['new'], c['newSims']))

    print('\n================ (2) ORACLE CEILING (truth-based selection, no threshold) ====')
    print('Each conditioned universe is evaluated under the ownership rule it was')
    print('conditioned with (the natural full configuration); U0 carries all five as the')
    print('post-hoc controls, which is exactly what sibling A measured.\n')
    print('%-34s %8s %8s %10s %9s %9s %10s' %
          ('universe / ownership rule', 'deliv/e', 'newSim', 'labEff', 'dup', 'fake',
           'maxRealEff'))
    for nm, kw in RULES:
        r = run(evs['U0'], theta=-1e9, oracle=True, rdt=True, **kw)
        a = A5.get(nm)
        extra = ''
        if a:
            extra = '   [A5: %.1f / %d / %.4f]' % a
        print('%-34s %8.1f %8.0f %10.5f %9.5f %9.5f %10.5f%s' %
              ('U0 post-hoc ' + nm, r['deliv'], r['newSims'], r['eff'], r['dup'], r['fake'],
               realeff(r['newSims']), extra))
    print('')
    for tag, name, rule in UNIV:
        if tag == 'U0' or tag not in evs:
            continue
        kw = dict(RULES)[rule]
        r = run(evs[tag], theta=-1e9, oracle=True, rdt=True, **kw)
        print('%-34s %8.1f %8.0f %10.5f %9.5f %9.5f %10.5f' %
              ('%s %s + %s' % (tag, name, rule), r['deliv'], r['newSims'], r['eff'], r['dup'],
               r['fake'], realeff(r['newSims'])))
        # the conditioned universe with NO further dedup: the static conditioning alone
        r2 = run(evs[tag], theta=-1e9, oracle=True, rdt=True, rule='none')
        print('%-34s %8.1f %8.0f %10.5f %9.5f %9.5f %10.5f' %
              ('%s %s + nodedup' % (tag, name), r2['deliv'], r2['newSims'], r2['eff'],
               r2['dup'], r2['fake'], realeff(r2['newSims'])))

    print('\n================ (3) HEAD-RANKED frontier, theta scan (the real selector) ====')
    print('%-34s %8s %8s %8s %10s %9s %9s' %
          ('universe / rule', 'theta', 'deliv/e', 'newSim', 'labEff', 'dup', 'fake'))
    for tag, name, rule in UNIV:
        if tag not in evs:
            continue
        rules = [r[0] for r in RULES] if tag == 'U0' else [rule, 'nodedup']
        for rn in rules:
            kw = dict(RULES)[rn]
            for th in (4.0, 6.0, 7.5, 9.0):
                r = run(evs[tag], theta=th, rdt=True, **kw)
                g = ('E' if r['eff'] - 0.0027 >= 0.81303 else '.') + \
                    ('D' if r['dup'] <= 0.052 else '.') + ('F' if r['fake'] <= 0.047 else '.')
                print('%-34s %8.1f %8.1f %8.0f %10.5f %9.5f %9.5f  %s' %
                      ('%s %s' % (tag, rn), th, r['deliv'], r['newSims'], r['eff'], r['dup'],
                       r['fake'], g))


if __name__ == '__main__':
    main()
