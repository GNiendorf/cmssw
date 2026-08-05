#!/usr/bin/env python3
"""A09 ledger: pull the seed-retirement lines out of one or more run logs.

Usage: a09_led.py <tag> [<tag> ...]
Prints, per tag: the M16 suppression line, the -ZP8 audit line, the XC seed-crossclean
line, the XC truth partition, the M20 pT3-dedup line and (if present) the -UM map.
"""
import os
import sys

DIRS = ['/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a09_ref/',
        '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fin_ref/',
        '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/xc_ref/']
WANT = ('M16 suppression', 'AUDIT -ZP8', 'XC crossclean', 'XC truth', 'M20 pT3 dedup',
        'M20 stageB', 'M16 delivery', 'UM map', 'UM marg', 'UM overlap', 'UM sets')

for tag in sys.argv[1:]:
    path = None
    for d in DIRS:
        if os.path.exists(d + 'r_%s.log' % tag):
            path = d + 'r_%s.log' % tag
            break
    if path is None:
        print('== %s : NO LOG ==' % tag)
        continue
    print('== %s ==' % tag)
    for line in open(path):
        s = line.strip()
        if any(s.startswith(w) for w in WANT):
            print('  ' + s)
    print()
