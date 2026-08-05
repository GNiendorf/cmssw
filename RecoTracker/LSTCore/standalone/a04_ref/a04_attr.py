#!/usr/bin/env python3
"""MECHANISM ATTRIBUTION of the per-sim efficiency loss.

Takes the LST identity file, a REFERENCE prototype file (normally the assembled
baseline) and any number of VARIANT prototype files, all over the same events in the
same order. Builds

    LOSS  = sims in the harness denominator that LST covers and the reference does not
    GAIN  = sims the reference covers and LST does not

and reports, for every variant, how much of LOSS it recovers and how much of GAIN it
gives back -- sliced by eta region and by the LST object type that delivers the sim.
That is the attribution: a variant that turns one mechanism off recovers exactly the
sims that mechanism was killing.

Denominator convention is a04_diff.py's (the scoreboard headline one):
  sim q != 0, sim_pt > 0.9, |sim_vz| < 30, sqrt(vx^2+vy^2) < 2.5, no eta cut.

usage: a04_attr.py --base rb_base300.root --ref r_FINBASE.root \
                   --var TAG=path.root [--var TAG2=path2.root ...]
"""
import argparse

import ROOT

TYPENAME = {4: "T5", 5: "pT3", 7: "pT5", 8: "pLS", 9: "T4", -1: "none"}


def region(e):
    a = abs(e)
    if a >= 4.5:
        return "oflow"
    if a < 1.1:
        return "barrel"
    if a < 1.7:
        return "transi"
    return "endcap"


def scan(path, nmax):
    """returns (n_entries, list per entry of (covered bool list, tc_type list))"""
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    n = t.GetEntries() if nmax <= 0 else min(nmax, t.GetEntries())
    out = []
    for i in range(n):
        t.GetEntry(i)
        out.append(([x >= 0 for x in t.sim_tcIdx], list(t.sim_tcIdx), list(t.tc_type)))
    f.Close()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True)
    ap.add_argument("--ref", required=True)
    ap.add_argument("--var", action="append", default=[])
    ap.add_argument("--maxev", type=int, default=-1)
    args = ap.parse_args()

    ROOT.gROOT.SetBatch(True)
    ROOT.gErrorIgnoreLevel = ROOT.kError

    fb = ROOT.TFile.Open(args.base)
    tb = fb.Get("tree")
    nb = tb.GetEntries()

    variants = []
    for v in args.var:
        tag, path = v.split("=", 1)
        variants.append((tag, path))

    # pass 1: denominator + LST coverage + reference coverage
    fr = ROOT.TFile.Open(args.ref)
    tr = fr.Get("tree")
    n = min(nb, tr.GetEntries())
    if args.maxev > 0:
        n = min(n, args.maxev)

    keys = []      # (entry, simrow)
    meta = {}      # (entry,simrow) -> (region, lsttype)
    loss = set()
    gain = set()
    for ie in range(n):
        tb.GetEntry(ie)
        tr.GetEntry(ie)
        sq, se, spt = tb.sim_q, tb.sim_eta, tb.sim_pt
        svx, svy, svz = tb.sim_vx, tb.sim_vy, tb.sim_vz
        bidx, btype = tb.sim_tcIdx, tb.tc_type
        ridx = tr.sim_tcIdx
        for i in range(len(se)):
            if sq[i] == 0 or spt[i] <= 0.9:
                continue
            if abs(svz[i]) >= 30.0 or (svx[i] ** 2 + svy[i] ** 2) ** 0.5 >= 2.5:
                continue
            k = (ie, i)
            b = bidx[i] >= 0
            r = ridx[i] >= 0
            if b and not r:
                loss.add(k)
                meta[k] = (region(se[i]), TYPENAME.get(btype[bidx[i]], "?"))
            elif r and not b:
                gain.add(k)
                meta[k] = (region(se[i]), "none")
    fr.Close()

    print("reference LOSS (LST covers, ref does not) = %d ; GAIN (ref covers, LST does not) = %d"
          " over %d events" % (len(loss), len(gain), n))

    hdr = "%-10s %8s %8s %8s | %-28s | %-24s" % (
        "variant", "recov", "givenback", "net", "recovered by LST type", "recovered by region")
    print(hdr)
    print("-" * len(hdr))
    for tag, path in variants:
        fv = ROOT.TFile.Open(path)
        tv = fv.Get("tree")
        nv = min(n, tv.GetEntries())
        rec = []
        giveback = 0
        for ie in range(nv):
            tv.GetEntry(ie)
            vidx = tv.sim_tcIdx
            for (e, i) in [k for k in loss if k[0] == ie]:
                if vidx[i] >= 0:
                    rec.append((e, i))
            for (e, i) in [k for k in gain if k[0] == ie]:
                if vidx[i] < 0:
                    giveback += 1
        fv.Close()
        bytype = {}
        byreg = {}
        for k in rec:
            r, lt = meta[k]
            bytype[lt] = bytype.get(lt, 0) + 1
            byreg[r] = byreg.get(r, 0) + 1
        ts = " ".join("%s:%d" % (k, v) for k, v in sorted(bytype.items(), key=lambda x: -x[1]))
        rs = " ".join("%s:%d" % (k, v) for k, v in sorted(byreg.items(), key=lambda x: -x[1]))
        print("%-10s %8d %8d %+8d | %-28s | %-24s"
              % (tag, len(rec), giveback, len(rec) - giveback, ts, rs))


if __name__ == "__main__":
    main()
