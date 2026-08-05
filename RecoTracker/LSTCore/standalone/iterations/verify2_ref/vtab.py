#!/usr/bin/env python3
"""Independent re-derivation of the scoreboard from createPerfNumDenHists output.

Written from the histogram conventions, not by importing compare_ab.py, so that a
bug (or a doctored json) in the agents' tooling cannot propagate into the audit.
Usage: vtab.py <hists.root> [<hists.root> ...]
"""
import sys
import ROOT

ROOT.gROOT.SetBatch(True)

EF = "Root__TC_base_0_0_ef_"
FR = "Root__TC_fr_"
DR = "Root__TC_dr_"
OL = "Root__TC_ol_"

BANDS = [(0.0, 1.0), (1.0, 5.0), (5.0, 10.0), (10.0, 30.0)]
REGIONS = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 1e9)]


def H(f, n):
    h = f.Get(n)
    if not h or not h.InheritsFrom("TH1"):
        return None
    return h


def tot(h):
    if h is None:
        return None
    s = 0.0
    for b in range(0, h.GetNbinsX() + 2):
        s += h.GetBinContent(b)
    return s


def band(h, lo, hi):
    if h is None:
        return None
    s = 0.0
    ax = h.GetXaxis()
    for b in range(1, h.GetNbinsX() + 1):
        c = abs(ax.GetBinCenter(b))
        if lo <= c < hi:
            s += h.GetBinContent(b)
    return s


def r(a, b):
    if a is None or b is None or b <= 0:
        return None
    return a / b


def metrics(path):
    f = ROOT.TFile.Open(path)
    if not f or f.IsZombie():
        raise SystemExit("cannot open " + path)
    m = {}
    en, ed = H(f, EF + "numer_eta"), H(f, EF + "denom_eta")
    m["eff"] = r(tot(en), tot(ed))
    m["nsim"] = tot(ed)
    m["nsim_num"] = tot(en)
    for var in ("vxy", "dxy"):
        hn, hd = H(f, EF + "numer_" + var), H(f, EF + "denom_" + var)
        for lo, hi in BANDS:
            k = "%s%g_%g" % (var[0], lo, hi)
            m[k] = r(band(hn, lo, hi), band(hd, lo, hi))
            m[k + "_num"] = band(hn, lo, hi)
            m[k + "_den"] = band(hd, lo, hi)
    fn, fd = H(f, FR + "numer_eta"), H(f, FR + "denom_eta")
    dn, dd = H(f, DR + "numer_eta"), H(f, DR + "denom_eta")
    m["fake"] = r(tot(fn), tot(fd))
    m["dup"] = r(tot(dn), tot(dd))
    m["ntc_incut"] = tot(fd)
    m["ntc"] = tot(H(f, FR + "denom_pt"))
    on, od = H(f, OL + "numer_eta"), H(f, OL + "denom_eta")
    m["len"] = r(tot(on), tot(od))
    for reg, lo, hi in REGIONS:
        m["eff_" + reg] = r(band(en, lo, hi), band(ed, lo, hi))
        m["fake_" + reg] = r(band(fn, lo, hi), band(fd, lo, hi))
        m["dup_" + reg] = r(band(dn, lo, hi), band(dd, lo, hi))
        m["len_" + reg] = r(band(on, lo, hi), band(od, lo, hi))
    f.Close()
    return m


KEYS = ["eff", "dup", "fake", "len", "len_barrel", "len_transition", "len_endcap",
        "eff_barrel", "eff_transition", "eff_endcap",
        "dup_barrel", "dup_transition", "dup_endcap",
        "fake_barrel", "fake_transition", "fake_endcap",
        "v0_1", "v1_5", "v5_10", "v10_30", "d0_1", "d1_5", "d5_10", "d10_30",
        "ntc", "ntc_incut", "nsim", "nsim_num"]

if __name__ == "__main__":
    rows = []
    for p in sys.argv[1:]:
        rows.append((p.split("/")[-1].replace("_hists.root", ""), metrics(p)))
    w = max(len(k) for k in KEYS) + 2
    hdr = "%-18s" % "metric" + "".join("%14s" % n[:14] for n, _ in rows)
    print(hdr)
    for k in KEYS:
        line = "%-18s" % k
        for _, m in rows:
            v = m.get(k)
            if v is None:
                line += "%14s" % "n/a"
            elif k.startswith("ntc") or k.startswith("nsim"):
                line += "%14.0f" % v
            elif k.startswith("len"):
                line += "%14.4f" % v
            else:
                line += "%14.5f" % v
        print(line)
    # raw band counts for the displaced bands (single-track moves matter)
    print()
    print("%-18s" % "band num/den" + "".join("%14s" % n[:14] for n, _ in rows))
    for k in ["v0_1", "v1_5", "v5_10", "v10_30", "d0_1", "d1_5", "d5_10", "d10_30"]:
        line = "%-18s" % k
        for _, m in rows:
            line += "%14s" % ("%.0f/%.0f" % (m[k + "_num"], m[k + "_den"]))
        print(line)
