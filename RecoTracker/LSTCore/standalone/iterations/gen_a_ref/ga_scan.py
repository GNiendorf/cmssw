#!/usr/bin/env python3
"""GEN-A: scan dedup configurations in the offline lab (ga_lab.run_rule)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ga_lab import load, run_rule, show, HDR

# Lab-to-real calibration measured on 9 runs whose true numbers are known:
#   real_eff  ~= effEst  - 0.009   (offset 0.0066..0.0100, rank order IDENTICAL)
#   real_fake ~= fakeEst + 0.0015
EFF_OFF, FAKE_OFF = -0.009, +0.0015

if __name__ == '__main__':
    nev = int(sys.argv[1]) if len(sys.argv) > 1 else None
    evs = load(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'audit300c.txt'), nev)
    print('events %d' % len(evs))
    print(HDR + '   effReal  fakeReal')

    def go(name, **kw):
        r = run_rule(evs, **kw)
        show(name, r)
        print('%56s -> eff~%.5f fake~%.5f' % ('', r['effEst'] + EFF_OFF, r['fakeEst'] + FAKE_OFF))
        return r

    print('--- threshold with NO OT dedup (pixel seed-family only) ---')
    for th in [6.0, 7.0, 7.5, 8.0, 8.5, 9.0]:
        go('nocc th%.1f' % th, theta=th, ccOn=False, rdt=True)
    print('--- threshold x ccN (MD granularity, pre-claim on, pixel on) ---')
    for th in [6.0, 7.0, 8.0]:
        for n in [1, 2, 3]:
            go('th%.1f ccN%d' % (th, n), theta=th, ccOn=True, ccN=n, rdt=True)
