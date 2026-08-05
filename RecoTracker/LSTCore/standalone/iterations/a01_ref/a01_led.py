#!/usr/bin/env python3
"""A01 ledger: per-tag ownership-arm retirements + truth-partition fates.
Usage: a01_led.py <tag> [<tag> ...]
Columns: XCOret = seeds retired by the ownership arm (per evt); Aret/Bret/Cret = class
A/B/C seeds NEWLY retired by the whole crossclean (ported arms + ownership arm);
Asurv = class-A survivors, the population this angle targets."""
import os
import re
import sys

T = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a01_ref/'
F = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fin_ref/'


def load(tag):
    for d in (T, F):
        p = d + 'r_%s.log' % tag
        if os.path.exists(p):
            return open(p, errors='replace').read()
    return None


def parse(txt):
    o = {}
    m = re.search(r'XC ownarm .*?seeds retired=\d+ mean=([0-9.]+)', txt)
    o['xcoret'] = float(m.group(1)) if m else 0.0
    # truth partition rows: "A true, sim has cover TC   N consumed rps xc surv"
    for cls, key in (('A true', 'A'), ('B true', 'B'), ('C no true', 'C')):
        mm = re.findall(re.escape(cls) + r'[^\n]*?((?:\s+[0-9.]+){5})\s*\n', txt)
        if mm:
            v = [float(x) for x in mm[-1].split()]
            o[key] = v  # N, consumed, rpsblock, xcretire, survive
    return o


tags = sys.argv[1:]
print("%-12s %8s %8s %8s %8s %8s %8s" % ("tag", "XCOret", "Aret", "Bret", "Cret", "Asurv", "sel A:B"))
for t in tags:
    txt = load(t)
    if txt is None:
        print("%-12s  (no log)" % t)
        continue
    o = parse(txt)
    A = o.get('A', [0] * 5)
    B = o.get('B', [0] * 5)
    C = o.get('C', [0] * 5)
    sel = (A[3] / B[3]) if B[3] > 0 else float('inf')
    print("%-12s %8.1f %8.1f %8.1f %8.1f %8.1f %8.1f" % (t, o['xcoret'], A[3], B[3], C[3], A[4], sel))
