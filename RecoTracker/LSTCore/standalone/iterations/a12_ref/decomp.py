#!/usr/bin/env python3
"""Decompose the TC output of a chainproto run by tc_type x eta region, reporting
count / fake / duplicate per cell. Usage: decomp.py <file.root> [<file.root> ...]

Answers the question the scoreboard cannot: WHICH class of emitted row carries the
excess duplicate rate and the excess fake rate against LST.
"""
import sys
import ROOT

TYPENAME = {4: "T5chain", 5: "pT3cls", 7: "pT5/upg", 8: "barePLS", 9: "T4chain"}
REG = [("B", 0.0, 1.1), ("T", 1.1, 1.7), ("E", 1.7, 99.0)]


def region(eta):
    a = abs(eta)
    for n, lo, hi in REG:
        if lo <= a < hi:
            return n
    return "E"


def run(path, ptcut=0.0):
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    n = t.GetEntries()
    cells = {}
    tot = [0, 0, 0]
    for i in range(n):
        t.GetEntry(i)
        ty = t.tc_type
        et = t.tc_eta
        fk = t.tc_isFake
        du = t.tc_isDuplicate
        pt = t.tc_pt
        for j in range(len(ty)):
            if pt[j] < ptcut:
                continue
            k = (int(ty[j]), region(et[j]))
            c = cells.setdefault(k, [0, 0, 0])
            c[0] += 1
            c[1] += 1 if fk[j] else 0
            c[2] += 1 if du[j] else 0
            tot[0] += 1
            tot[1] += 1 if fk[j] else 0
            tot[2] += 1 if du[j] else 0
    return n, cells, tot


def show(path, ptcut):
    n, cells, tot = run(path, ptcut)
    print("=== %s   (%d evts, ptcut %.2f) ===" % (path.split("/")[-1], n, ptcut))
    print("%-10s %-3s %10s %10s %10s %9s %9s" % ("type", "reg", "nTC/evt", "fake/evt", "dup/evt", "fakefrac", "dupfrac"))
    for k in sorted(cells):
        c = cells[k]
        print("%-10s %-3s %10.2f %10.2f %10.2f %9.4f %9.4f" % (
            TYPENAME.get(k[0], str(k[0])), k[1], c[0] / n, c[1] / n, c[2] / n,
            c[1] / max(c[0], 1), c[2] / max(c[0], 1)))
    print("%-10s %-3s %10.2f %10.2f %10.2f %9.4f %9.4f" % (
        "ALL", "*", tot[0] / n, tot[1] / n, tot[2] / n, tot[1] / max(tot[0], 1), tot[2] / max(tot[0], 1)))
    # per-region roll-up
    for r in ("B", "T", "E"):
        s = [0, 0, 0]
        for k in cells:
            if k[1] == r:
                for a in range(3):
                    s[a] += cells[k][a]
        print("%-10s %-3s %10.2f %10.2f %10.2f %9.4f %9.4f" % (
            "REGION", r, s[0] / n, s[1] / n, s[2] / n, s[1] / max(s[0], 1), s[2] / max(s[0], 1)))
    print()
    return n, cells, tot


if __name__ == "__main__":
    ptcut = 0.0
    args = []
    for a in sys.argv[1:]:
        if a.startswith("--pt="):
            ptcut = float(a.split("=")[1])
        else:
            args.append(a)
    for p in args:
        show(p, ptcut)
