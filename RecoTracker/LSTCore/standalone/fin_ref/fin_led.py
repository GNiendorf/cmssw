#!/usr/bin/env python3
"""Ledger from the run logs: pT3-class deliveries/evt, XC retirements, stage-B volume.
Usage: fin_led.py <tag> [<tag> ...]"""
import os, re, sys
D = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
DIRS = [D + 'fin_ref/', D + 'xc_ref/']


def find(tag):
    for d in DIRS:
        p = d + 'r_%s.log' % tag
        if os.path.exists(p):
            return p
    return None


def num(pat, txt, default=0.0):
    m = re.search(pat, txt)
    return float(m.group(1)) if m else default


print("%-12s %8s %8s %8s %8s %8s %8s %8s" % (
    "tag", "delivT3", "pxRev", "otRev", "cand/evt", "xcRet", "xcCarr", "xcZp8"))
for t in sys.argv[1:]:
    p = find(t)
    if not p:
        print("%-12s  (no log)" % t)
        continue
    s = open(p, errors='replace').read()
    dl = num(r'DELIVERED=\d+ \(([\d.]+)/evt\)', s)
    px = num(r'PIXEL side \([^)]*\): revoked=\d+ \(([\d.]+)/evt\)', s)
    ot = num(r'OT side \([^)]*\): revoked=\d+ \(([\d.]+)/evt\)', s)
    cd = num(r'candidates before any dedup=\d+ \(([\d.]+)/evt\)', s)
    xr = num(r'seeds retired=\d+ mean=([\d.]+)', s)
    xc = num(r'carried type-8 rows killed=\d+ mean=([\d.]+)', s)
    xz = num(r'-ZP8 additions blocked=\d+ mean=([\d.]+)', s)
    print("%-12s %8.1f %8.1f %8.1f %8.1f %8.1f %8.1f %8.1f" % (t, dl, px, ot, cd, xr, xc, xz))
