#!/usr/bin/env python3
"""A07 fake-rate decomposition.

The HEADLINE fake rate quoted on the scoreboard is compare_ab.py's
`fake_overall_incut` = numer/denom of the _eta histograms, i.e.

    denominator = every TC with |eta| < 4.5 AND pt > 0.9
    numerator   = those with tc_isFake == 1

This script reproduces that number exactly from the ntuple and splits it into cells:
(delivery class tc_isChain) x (tc_type) x (eta region) x (pt band) x (nhitOT).

tc_isChain: 0 carried baseline pixel row, 1 bare chain TC, 2 attach pT5-class,
            3 attach pT3-class, 4 -ZP8 added bare-pLS row.
tc_type   : 7 pT5, 5 pT3, 4 T5, 8 pLS, 9 T4.  LST-identity files have no usable
            tc_isChain -> everything lands in class 0.

Usage: a07_decomp.py <file.root> [more.root ...]
"""
import sys
import ROOT

TYPE_NAME = {4: "T5", 5: "pT3", 7: "pT5", 8: "pLS", 9: "T4"}
DELIV_NAME = {0: "carried", 1: "chain", 2: "attachT5", 3: "attachT3", 4: "zp8pLS"}
REGIONS = [("B", 0.0, 1.1), ("T", 1.1, 1.7), ("E", 1.7, 4.5)]
PT_CUT = 0.9
ETA_CUT = 4.5


def region(eta):
    a = abs(eta)
    for n, lo, hi in REGIONS:
        if lo <= a < hi:
            return n
    return None


def decomp(path):
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    have_chain = t.GetBranch("tc_isChain") is not None
    have_nhit = t.GetBranch("tc_nhitOT") is not None
    cells, cells_reg, nhit_fake = {}, {}, {}
    nev = 0
    tot = [0, 0]        # in-cut  (the headline)
    tot_nopt = [0, 0]   # |eta|<4.5 only (compare_ab fake_overall)
    for ev in t:
        nev += 1
        types = list(ev.tc_type)
        fakes = list(ev.tc_isFake)
        etas = list(ev.tc_eta)
        pts = list(ev.tc_pt)
        chains = list(ev.tc_isChain) if have_chain else []
        if len(chains) != len(types):
            chains = [0] * len(types)
        nhits = list(ev.tc_nhitOT) if have_nhit else []
        if len(nhits) != len(types):
            nhits = [0] * len(types)
        for i in range(len(types)):
            if abs(etas[i]) >= ETA_CUT:
                continue
            fk = 1 if fakes[i] > 0 else 0
            tot_nopt[0] += 1
            tot_nopt[1] += fk
            if pts[i] <= PT_CUT:
                continue
            d, ty = chains[i], types[i]
            tot[0] += 1
            tot[1] += fk
            c = cells.setdefault((d, ty), [0, 0])
            c[0] += 1
            c[1] += fk
            r = region(etas[i])
            if r:
                c2 = cells_reg.setdefault((d, ty, r), [0, 0])
                c2[0] += 1
                c2[1] += fk
            nh = nhit_fake.setdefault((d, ty), {})
            c4 = nh.setdefault(min(nhits[i], 20), [0, 0])
            c4[0] += 1
            c4[1] += fk
    f.Close()
    return dict(nev=nev, tot=tot, tot_nopt=tot_nopt, cells=cells, cells_reg=cells_reg, nhit=nhit_fake)


def show(path, d):
    nev = float(d["nev"])
    n, nf = d["tot"]
    print("=" * 104)
    print("FILE %s   events=%d" % (path, d["nev"]))
    print("  HEADLINE  in-cut TC (|eta|<4.5, pt>0.9): %d (%.1f/evt)  fake %d (%.2f/evt)  FAKE RATE %.5f"
          % (n, n / nev, nf, nf / nev, nf / float(n)))
    print("  (no-pt-cut cross-check, = compare_ab fake_overall: %.5f)"
          % (d["tot_nopt"][1] / float(d["tot_nopt"][0])))
    print()
    print("  %-9s %-4s %10s %8s %9s %8s %9s %9s %10s" %
          ("deliv", "type", "N", "N/evt", "fake", "fk/evt", "fakerate", "shareOfF", "rate_contr"))
    for k in sorted(d["cells"], key=lambda x: -d["cells"][x][1]):
        c = d["cells"][k]
        print("  %-9s %-4s %10d %8.2f %9d %8.2f %9.5f %9.4f %10.5f" %
              (DELIV_NAME.get(k[0], str(k[0])), TYPE_NAME.get(k[1], str(k[1])),
               c[0], c[0] / nev, c[1], c[1] / nev,
               c[1] / float(c[0]) if c[0] else 0.0, c[1] / float(nf), c[1] / float(n)))
    print()
    print("  per region (in-cut). rate_contr = this cell's fakes / ALL in-cut TC in that region")
    regn = {}
    for k, c in d["cells_reg"].items():
        r = regn.setdefault(k[2], [0, 0])
        r[0] += c[0]
        r[1] += c[1]
    for r in ("B", "T", "E"):
        if r in regn:
            print("    region %s: N %d (%.1f/evt) fake %d  RATE %.5f" %
                  (r, regn[r][0], regn[r][0] / nev, regn[r][1], regn[r][1] / float(regn[r][0])))
    print("  %-9s %-4s %3s %10s %8s %9s %9s %10s" %
          ("deliv", "type", "reg", "N", "N/evt", "fake", "fakerate", "rate_contr"))
    for k in sorted(d["cells_reg"], key=lambda x: (x[2], -d["cells_reg"][x][1])):
        c = d["cells_reg"][k]
        if c[0] < 50:
            continue
        print("  %-9s %-4s %3s %10d %8.2f %9d %9.5f %10.5f" %
              (DELIV_NAME.get(k[0], str(k[0])), TYPE_NAME.get(k[1], str(k[1])), k[2],
               c[0], c[0] / nev, c[1], c[1] / float(c[0]), c[1] / float(regn[k[2]][0])))
    print()
    print("  nhitOT profile (in-cut, fake/N)")
    for k in sorted(d["cells"], key=lambda x: -d["cells"][x][1]):
        nh = d["nhit"][k]
        row = " ".join("%d:%d/%d(%.3f)" % (h, nh[h][1], nh[h][0], nh[h][1] / float(nh[h][0]))
                       for h in sorted(nh))
        print("  %-9s %-4s  %s" % (DELIV_NAME.get(k[0], str(k[0])), TYPE_NAME.get(k[1], str(k[1])), row))
    print()


if __name__ == "__main__":
    for p in sys.argv[1:]:
        show(p, decomp(p))
