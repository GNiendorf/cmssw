#!/usr/bin/env python3
"""Truth-partition + ledger digest straight out of the run logs.
Usage: a13_part.py <logfile> [<logfile> ...]  (tag = basename minus r_/p30_ and .log)"""
import os, re, sys

HDR = ("%-14s %7s %7s %7s %7s %7s | %7s %7s %7s | %7s %7s %7s"
       % ("tag", "A_N", "A_cons", "A_rps", "A_xc", "A_surv",
          "B_rps", "B_xc", "B_surv", "C_xc", "C_surv", "delivT3"))


def parse(path):
    tag = os.path.basename(path)
    tag = re.sub(r'^(r_|p30_)', '', tag)
    tag = re.sub(r'\.log$', '', tag)
    rows = {}
    delivT3 = float('nan')
    for ln in open(path, errors='ignore'):
        m = re.search(r'XC truth\s+([ABC]) [^0-9]*?([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s*$',
                      ln.rstrip())
        if m:
            rows[m.group(1)] = [float(x) for x in m.groups()[1:]]
        m2 = re.search(r'DELIVERED pT3-class rows=\d+ \(([\d.]+)/evt\)', ln)
        if m2:
            delivT3 = float(m2.group(1))
        m3 = re.search(r'delivered=(\d+) \(([\d.]+)/evt\)', ln)
        if m3:
            delivT3 = float(m3.group(2))
    if 'A' not in rows:
        return None
    A, B, C = rows['A'], rows['B'], rows.get('C', [0] * 5)
    return ("%-14s %7.1f %7.1f %7.1f %7.1f %7.1f | %7.1f %7.1f %7.1f | %7.1f %7.1f %7.1f"
            % (tag, A[0], A[1], A[2], A[3], A[4], B[2], B[3], B[4], C[3], C[4], delivT3))


print(HDR)
for p in sys.argv[1:]:
    r = parse(p)
    if r:
        print(r)
