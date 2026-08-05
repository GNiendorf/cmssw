#!/usr/bin/env python3
"""B00 -- PER-ETA-BAND DUPLICATE PATTERN CENSUS on CHAINFINAL vs LST (full 977).

For every DUPLICATE TC: its class, its |eta| band, and the multiset of classes covering
the same sim.  Also the per-band per-class TC / dup / fake counts (decomp) so every
pattern count has its own denominator.

Usage: b00_census.py <tag>=<file.root> ... [--fine]
"""
import sys
from collections import Counter, defaultdict
import ROOT

TN = {4: "T5chain", 5: "pT3cls", 7: "seeded", 8: "barePLS", 9: "T4chain"}
COARSE = [("B", 0.0, 1.1), ("T", 1.1, 1.7), ("E", 1.7, 99.0)]
FINE = [("e00_04", 0.0, 0.4), ("e04_08", 0.4, 0.8), ("e08_11", 0.8, 1.1),
        ("e11_14", 1.1, 1.4), ("e14_17", 1.4, 1.7), ("e17_21", 1.7, 2.1),
        ("e21_25", 2.1, 2.5), ("e25_up", 2.5, 99.0)]


def bandof(eta, bands):
    a = abs(eta)
    for n, lo, hi in bands:
        if lo <= a < hi:
            return n
    return bands[-1][0]


def run(path, bands):
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    t.SetBranchStatus("*", 0)
    for b in ("tc_type", "tc_eta", "tc_isDuplicate", "tc_isFake", "tc_simIdxAll"):
        t.SetBranchStatus(b, 1)
    n = t.GetEntries()
    cell = defaultdict(lambda: [0, 0, 0])      # (band,cls) -> nTC, nFake, nDup
    pat = Counter()                             # (band, dupcls, partners) -> count
    prof = Counter()                            # (band, full multiset) -> count  (per sim)
    tot = [0, 0, 0]
    for i in range(n):
        t.GetEntry(i)
        ty = list(t.tc_type)
        et = list(t.tc_eta)
        du = list(t.tc_isDuplicate)
        fk = list(t.tc_isFake)
        sia = t.tc_simIdxAll
        sim2tc = defaultdict(list)
        for j in range(len(ty)):
            b = bandof(et[j], bands)
            c = cell[(b, TN.get(int(ty[j]), str(ty[j])))]
            c[0] += 1
            c[1] += 1 if fk[j] else 0
            c[2] += 1 if du[j] else 0
            tot[0] += 1
            tot[1] += 1 if fk[j] else 0
            tot[2] += 1 if du[j] else 0
            for s in sia[j]:
                sim2tc[int(s)].append(j)
        for s, js in sim2tc.items():
            if len(js) < 2:
                continue
            # band of the sim's cluster = band of the LOWEST-index (any) TC eta; use
            # the mean |eta| of the group -> all group members are within a hair anyway
            b = bandof(sum(abs(et[j]) for j in js) / len(js), bands)
            prof[(b, tuple(sorted(TN.get(int(ty[j]), str(ty[j])) for j in js)))] += 1
            for j in js:
                if not du[j]:
                    continue
                others = tuple(sorted(TN.get(int(ty[k]), str(ty[k])) for k in js if k != j))
                pat[(bandof(et[j], bands), TN.get(int(ty[j]), str(ty[j])), others)] += 1
    f.Close()
    return n, cell, pat, prof, tot


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    bands = FINE if "--fine" in sys.argv else COARSE
    res = {}
    order = []
    for spec in args:
        tag, path = spec.split("=", 1)
        order.append(tag)
        res[tag] = run(path, bands)
        print("loaded %s (%d evts)" % (tag, res[tag][0]), file=sys.stderr)

    bn = [b[0] for b in bands]
    print("=" * 100)
    print("A. TC / DUP / FAKE per BAND x CLASS  (per event)")
    print("=" * 100)
    classes = sorted({k[1] for tg in order for k in res[tg][1]})
    hdr = "%-6s %-9s" % ("band", "class")
    for tg in order:
        hdr += "%11s %8s %8s" % (tg + ".nTC", "dup", "fake")
    print(hdr)
    for b in bn:
        for c in classes:
            if all(res[tg][1].get((b, c), [0, 0, 0])[0] == 0 for tg in order):
                continue
            line = "%-6s %-9s" % (b, c)
            for tg in order:
                nev, cell = res[tg][0], res[tg][1]
                v = cell.get((b, c), [0, 0, 0])
                line += "%11.2f %8.2f %8.2f" % (v[0] / nev, v[2] / nev, v[1] / nev)
            print(line)
        line = "%-6s %-9s" % (b, "ALL")
        for tg in order:
            nev, cell = res[tg][0], res[tg][1]
            s = [0, 0, 0]
            for k, v in cell.items():
                if k[0] == b:
                    for a in range(3):
                        s[a] += v[a]
            line += "%11.2f %8.2f %8.2f" % (s[0] / nev, s[2] / nev, s[1] / nev)
        print(line)

    print()
    print("=" * 100)
    print("B. DUPLICATE PATTERN CENSUS  (dup TC class | partner classes), per event")
    print("=" * 100)
    for b in bn:
        keys = set()
        for tg in order:
            keys |= {k for k in res[tg][2] if k[0] == b}
        rows = []
        for k in keys:
            vals = [res[tg][2].get(k, 0) / res[tg][0] for tg in order]
            rows.append((max(vals), k, vals))
        rows.sort(reverse=True)
        print("-- band %s --" % b)
        hdr = "%-9s %-34s" % ("dupClass", "coveredWith")
        for tg in order:
            hdr += "%10s" % tg
        print(hdr)
        for _, k, vals in rows[:18]:
            line = "%-9s %-34s" % (k[1], ",".join(k[2]))
            for v in vals:
                line += "%10.3f" % v
            print(line)
        line = "%-9s %-34s" % ("TOTAL", "")
        for tg in order:
            line += "%10.3f" % (sum(v for k, v in res[tg][2].items() if k[0] == b) / res[tg][0])
        print(line)
        print()

    print("=" * 100)
    print("C. MULTI-TC SIM PROFILES per band (the PAIR patterns), per event")
    print("=" * 100)
    for b in bn:
        keys = set()
        for tg in order:
            keys |= {k for k in res[tg][3] if k[0] == b}
        rows = []
        for k in keys:
            vals = [res[tg][3].get(k, 0) / res[tg][0] for tg in order]
            rows.append((max(vals), k, vals))
        rows.sort(reverse=True)
        print("-- band %s --" % b)
        hdr = "%-46s" % "profile"
        for tg in order:
            hdr += "%10s" % tg
        print(hdr)
        for _, k, vals in rows[:14]:
            line = "%-46s" % "+".join(k[1])
            for v in vals:
                line += "%10.3f" % v
            print(line)
        print()


if __name__ == "__main__":
    ROOT.gROOT.SetBatch(True)
    ROOT.gErrorIgnoreLevel = ROOT.kError
    main()
