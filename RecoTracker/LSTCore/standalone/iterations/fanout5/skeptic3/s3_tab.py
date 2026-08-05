#!/usr/bin/env python3
"""SKEPTIC-3 independent scoreboard.

Everything is recomputed from the *_hists.root files directly (raw numerators and
denominators), not from any other agent's json. Accepts tags found in skeptic3/ or,
with a dir: prefix, in another agent's workspace (read-only).

Usage: s3_tab.py [dir=<name>] tag [tag...]
"""
import os
import sys

import ROOT

ROOT.gROOT.SetBatch(True)
S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/"
HOME = S + "fanout5/skeptic3/"

FLOOR = dict(eff=.8127, v15=.8022, v510=.7267, v1030=.7170, d15=.5613, fake=.0480)


def path(tag):
    """Allow 'workspace:tag' to point at another agent's artefacts (read-only)."""
    if ":" in tag:
        d, t = tag.split(":", 1)
        return S + "fanout5/" + d + "/" + t + "_hists.root", t
    return HOME + tag + "_hists.root", tag


def band(h, lo, hi):
    if h is None:
        return float("nan")
    t = 0.0
    ax = h.GetXaxis()
    for b in range(1, h.GetNbinsX() + 1):
        c = abs(ax.GetBinCenter(b))
        if c >= lo and (hi is None or c < hi):
            t += h.GetBinContent(b)
    return t


def allsum(h):
    return float("nan") if h is None else h.Integral(0, h.GetNbinsX() + 1)


def metrics(tag):
    p, _ = path(tag)
    if not os.path.exists(p):
        return None
    f = ROOT.TFile.Open(p)
    if not f or f.IsZombie():
        return None
    EF = "Root__TC_base_0_0_ef_"
    g = lambda n: f.Get(n) if f.Get(n) else None
    efn_e, efd_e = g(EF + "numer_eta"), g(EF + "denom_eta")
    efn_v, efd_v = g(EF + "numer_vxy"), g(EF + "denom_vxy")
    efn_d, efd_d = g(EF + "numer_dxy"), g(EF + "denom_dxy")
    frn, frd = g("Root__TC_fr_numer_eta"), g("Root__TC_fr_denom_eta")
    drn, drd = g("Root__TC_dr_numer_eta"), g("Root__TC_dr_denom_eta")
    oln, old = g("Root__TC_ol_numer_eta"), g("Root__TC_ol_denom_eta")
    m = {}
    m["effN"] = allsum(efn_e)
    m["effD"] = allsum(efd_e)
    m["eff"] = m["effN"] / m["effD"]
    for nm, hn, hd, lo, hi in (
            ("vxy01", efn_v, efd_v, 0, 1), ("v15", efn_v, efd_v, 1, 5),
            ("v510", efn_v, efd_v, 5, 10), ("v1030", efn_v, efd_v, 10, 30),
            ("d01", efn_d, efd_d, 0, 1), ("d15", efn_d, efd_d, 1, 5),
            ("d510", efn_d, efd_d, 5, 10), ("d1030", efn_d, efd_d, 10, 30)):
        n, d = band(hn, lo, hi), band(hd, lo, hi)
        m[nm] = n / d if d > 0 else float("nan")
        m[nm + "n"] = n
        m[nm + "d"] = d
    m["fake"] = allsum(frn) / allsum(frd)
    m["nTC"] = allsum(frd)
    m["dup"] = allsum(drn) / allsum(drd)
    m["dupN"] = allsum(drn)
    for nm, lo, hi in (("dupB", 0, 1.1), ("dupT", 1.1, 1.7), ("dupE", 1.7, None),
                       ("dupW", 1.5, 3.0), ("dupW25", 1.5, 2.5), ("dupLo", 0, 1.5)):
        m[nm] = band(drn, lo, hi) / max(1e-9, band(drd, lo, hi))
    m["nhB"] = band(oln, 0, 1.1) / max(1e-9, band(old, 0, 1.1))
    m["nhT"] = band(oln, 1.1, 1.7) / max(1e-9, band(old, 1.1, 1.7))
    m["nhE"] = band(oln, 1.7, None) / max(1e-9, band(old, 1.7, None))
    m["nhW"] = band(oln, 1.5, 3.0) / max(1e-9, band(old, 1.5, 3.0))
    f.Close()
    return m


def timing(tag):
    if ":" in tag:
        d, t = tag.split(":", 1)
        p = S + "fanout5/" + d + "/" + t + ".log"
    else:
        p = HOME + tag + ".log"
    if not os.path.exists(p):
        return float("nan")
    best = float("nan")
    for ln in open(p, errors="ignore"):
        if "ms/evt" in ln or "ms per event" in ln:
            for tk in ln.replace("=", " ").split():
                try:
                    v = float(tk)
                except ValueError:
                    continue
                if 50 < v < 2000:
                    best = v
    return best


def fails(m):
    bad = []
    if m["eff"] < FLOOR["eff"]:
        bad.append("EFF")
    if m["v15"] < FLOOR["v15"]:
        bad.append("V15")
    if m["v510"] < FLOOR["v510"]:
        bad.append("V510")
    if m["v1030"] < FLOOR["v1030"]:
        bad.append("V1030")
    if m["d15"] < FLOOR["d15"]:
        bad.append("D15")
    if m["fake"] > FLOOR["fake"]:
        bad.append("FAKE")
    if round(m["d510n"]) < 71:
        bad.append("D510")
    return ",".join(bad) if bad else "pass"


HDR = ("%-22s %7s %7s %7s %7s %7s %7s %7s %7s | %6s %6s %6s %6s | %6s %6s %5s | "
       "%5s %6s %7s %-14s" % (
           "tag", "eff", "vxy01", "v15", "v510", "v1030", "d15", "fake", "dup",
           "dupB", "dupT", "dupE", "dupW", "nhB", "nhT", "nhE", "d510", "nTC", "ms",
           "floors"))


def row(tag, m):
    return ("%-22s %7.5f %7.5f %7.5f %7.5f %7.5f %7.5f %7.5f %7.5f | "
            "%6.4f %6.4f %6.4f %6.4f | %6.3f %6.3f %5.3f | %2d/%-3d %6.0f %7.1f %-14s" % (
                tag, m["eff"], m["vxy01"], m["v15"], m["v510"], m["v1030"], m["d15"],
                m["fake"], m["dup"], m["dupB"], m["dupT"], m["dupE"], m["dupW"],
                m["nhB"], m["nhT"], m["nhE"], round(m["d510n"]), round(m["d510d"]),
                m["nTC"], timing(tag), fails(m)))


if __name__ == "__main__":
    print(HDR)
    print("-" * len(HDR))
    for tag in sys.argv[1:]:
        m = metrics(tag)
        if m is None:
            print("%-22s MISSING" % tag)
            continue
        print(row(tag.split(":")[-1] if ":" in tag else tag, m))
