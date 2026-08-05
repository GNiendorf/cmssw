#!/usr/bin/env python3
"""a15_dup.py -- FULL duplicate decomposition of a TC collection.

Works on both
  --kind proto : a hybrid chainproto output (needs tc_simIdxAll, tc_isChain and,
                 for the structure axis, tc_hitOT written under PROTO_DUMP_TCHITS=1)
  --kind lst   : the input LSTNtuple itself (tc_simIdxAll/Frac + tc_hitIdx/tc_hitType),
                 which IS the LST reference collection (the identity run copies it).

METRIC FIDELITY.  tc_isDuplicate is computed over ALL TCs: a TC is a duplicate iff one
of its matched sims (fraction > 0.75, FULL sim index space incl. pileup) is matched by
more than one TC.  Verified bit-exact against the file's own tc_isDuplicate branch.
The HEADLINE duplicate rate on the scoreboard is the harness's Root__TC_dr_*_eta ratio,
i.e. it counts only TCs with pt > 0.9 (608190 -> 473752 TCs on the LST 300-evt file,
giving .05179, not the unrestricted .05032).  So flags are computed on everything and
REPORTED on the pt > 0.9 universe.  Both are printed.

AXES
  class      proto: carried pT5 / carried pT3 / carried pLS / chain-T5 / chain-T4 /
                    attach-pT5 / attach-pT3   (tc_isChain x tc_type)
             lst  : PT5 / PT3 / T5 / T4 / PLS   (tc_type)
  region     barrel |eta|<1.1, transition 1.1-1.7, endcap >1.7 (per TC, as the harness bins)
  structure  OT-hit relation of the two members of a duplicate pair:
             identical / subset / partial / disjoint / one-noOT / both-noOT

ORACLE YIELD.  For a cell predicate P: take only the duplicate pairs satisfying P, build
the graph they induce, keep ONE TC per connected component (pixel-backed and longer
first), drop the rest, then RECOMPUTE the duplicate flags from scratch over the
survivors, and re-evaluate the HEADLINE rate.  simsLost counts sims that lose their last
matcher -- the efficiency risk of that cell (0 means the cell is free efficiency-wise).
"""
import argparse
import json
import sys
from collections import Counter, defaultdict

import ROOT

MATCH_FRAC = 0.75
REGIONS = ("B", "T", "E")


def region_of(eta):
    a = abs(eta)
    if a < 1.1:
        return "B"
    if a < 1.7:
        return "T"
    return "E"


# tc_isChain carries the OutDeliv provenance code, not a boolean:
#   0 carried baseline row, 1 bare chain TC, 2 attach pT5-class, 3 attach pT3-class,
#   4 = a -ZP8 synthetic bare-seed row (the seeds LST's deleted CrossCleanpLS used to kill).
PROTO_CLASS = {
    (0, 7): "cPT5", (0, 5): "cPT3", (0, 8): "cPLS",
    (1, 4): "chT5", (1, 9): "chT4",
    (2, 7): "aPT5", (3, 5): "aPT3",
    (4, 8): "zPLS",
}
LST_CLASS = {7: "PT5", 5: "PT3", 4: "T5", 9: "T4", 8: "PLS"}

# Keep-priority when an oracle collapses a component: higher wins.
KEEP_PRIO = {
    "cPT5": 60, "aPT5": 55, "chT5": 50, "cPT3": 45, "aPT3": 40, "chT4": 35, "cPLS": 10,
    "zPLS": 9,
    "PT5": 60, "T5": 50, "PT3": 45, "T4": 35, "PLS": 10,
}
STRUCTS = ["identical", "subset", "partial", "disjoint", "one-noOT", "both-noOT"]


def struct_of(a_ot, b_ot):
    na, nb = len(a_ot), len(b_ot)
    if na == 0 and nb == 0:
        return "both-noOT"
    if na == 0 or nb == 0:
        return "one-noOT"
    sh = len(a_ot & b_ot)
    if sh == 0:
        return "disjoint"
    if sh == na and sh == nb:
        return "identical"
    if sh == min(na, nb):
        return "subset"
    return "partial"


class Event:
    __slots__ = ("cls", "reg", "eta", "phi", "pt", "nhit", "sims", "ot", "inc", "n")

    def __init__(self, cls, reg, eta, phi, pt, nhit, sims, ot, inc):
        self.cls, self.reg, self.eta, self.phi, self.pt = cls, reg, eta, phi, pt
        self.nhit, self.sims, self.ot, self.inc = nhit, sims, ot, inc
        self.n = len(cls)


def read_events(path, kind, nmax=-1, ptcut=0.9):
    f = ROOT.TFile.Open(path)
    if not f or f.IsZombie():
        sys.exit("cannot open %s" % path)
    t = f.Get("tree")
    have = set(b.GetName() for b in t.GetListOfBranches())
    need = ["tc_type", "tc_eta", "tc_phi", "tc_pt", "tc_nhitOT", "tc_simIdxAll"]
    frac_br = "tc_simIdxAllFrac" if "tc_simIdxAllFrac" in have else None
    if kind == "proto":
        need.append("tc_isChain")
        hits_br = "tc_hitOT" if "tc_hitOT" in have else None
    else:
        hits_br = "tc_hitIdx" if "tc_hitIdx" in have else None
        if hits_br:
            need.append("tc_hitType")
    if frac_br:
        need.append(frac_br)
    if hits_br:
        need.append(hits_br)
    for b in need:
        if b not in have:
            sys.exit("%s: missing branch %s" % (path, b))
    t.SetBranchStatus("*", 0)
    for b in need:
        t.SetBranchStatus(b, 1)

    out = []
    n = t.GetEntries() if nmax < 0 else min(nmax, t.GetEntries())
    for ie in range(n):
        t.GetEntry(ie)
        ttype = list(t.tc_type)
        teta = list(t.tc_eta)
        tphi = list(t.tc_phi)
        tpt = list(t.tc_pt)
        tnh = list(t.tc_nhitOT)
        ntc = len(ttype)
        if kind == "proto":
            isch = list(t.tc_isChain)
            cls = [PROTO_CLASS.get((isch[i], ttype[i]), "?%d/%d" % (isch[i], ttype[i]))
                   for i in range(ntc)]
        else:
            cls = [LST_CLASS.get(ttype[i], "?%d" % ttype[i]) for i in range(ntc)]
        reg = [region_of(e) for e in teta]
        inc = [p > ptcut for p in tpt]
        sims = []
        for i in range(ntc):
            s = t.tc_simIdxAll[i]
            if frac_br:
                fr = getattr(t, frac_br)[i]
                sims.append([int(x) for x, y in zip(s, fr) if y > MATCH_FRAC])
            else:
                sims.append([int(x) for x in s])
        if hits_br:
            ot = []
            for i in range(ntc):
                h = getattr(t, hits_br)[i]
                if kind == "lst":
                    ht = t.tc_hitType[i]
                    ot.append(frozenset(int(x) for x, y in zip(h, ht) if y == 4))
                else:
                    ot.append(frozenset(int(x) for x in h))
        else:
            ot = [frozenset()] * ntc
        out.append(Event(cls, reg, teta, tphi, tpt, tnh, sims, ot, inc))
    f.Close()
    return out, (hits_br is not None)


def dup_flags(sims_list, keep=None):
    sim2tc = defaultdict(list)
    for i, ss in enumerate(sims_list):
        if keep is not None and not keep[i]:
            continue
        for s in ss:
            sim2tc[s].append(i)
    flags = [False] * len(sims_list)
    for i, ss in enumerate(sims_list):
        if keep is not None and not keep[i]:
            continue
        for s in ss:
            if len(sim2tc[s]) > 1:
                flags[i] = True
                break
    return flags, sim2tc


def pairs_of(sim2tc):
    p = set()
    for s, tcs in sim2tc.items():
        if len(tcs) < 2:
            continue
        for i in range(len(tcs)):
            for j in range(i + 1, len(tcs)):
                p.add((tcs[i], tcs[j]) if tcs[i] < tcs[j] else (tcs[j], tcs[i]))
    return p


def oracle(events, pred, prio=None):
    prio = prio or KEEP_PRIO
    a = dict(nTC=0, nDup=0, nTCi=0, nDupi=0)
    b = dict(nTC=0, nDup=0, nTCi=0, nDupi=0)
    nRemoved = nRemovedI = simsLost = 0
    for ev in events:
        f0, s2t = dup_flags(ev.sims)
        for i in range(ev.n):
            a["nTC"] += 1
            a["nDup"] += f0[i]
            if ev.inc[i]:
                a["nTCi"] += 1
                a["nDupi"] += f0[i]
        sel = [(x, y) for (x, y) in pairs_of(s2t) if pred(ev, x, y)]
        parent = {}

        def find(x):
            parent.setdefault(x, x)
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x

        for x, y in sel:
            rx, ry = find(x), find(y)
            if rx != ry:
                parent[rx] = ry
        comp = defaultdict(list)
        for x in list(parent):
            comp[find(x)].append(x)
        keep = [True] * ev.n
        for members in comp.values():
            if len(members) < 2:
                continue
            best = max(members, key=lambda i: (prio.get(ev.cls[i], 0), ev.nhit[i], -i))
            for i in members:
                if i != best:
                    keep[i] = False
                    nRemoved += 1
                    if ev.inc[i]:
                        nRemovedI += 1
        for s, tcs in s2t.items():
            if all(not keep[i] for i in tcs):
                simsLost += 1
        f1, _ = dup_flags(ev.sims, keep)
        for i in range(ev.n):
            if not keep[i]:
                continue
            b["nTC"] += 1
            b["nDup"] += f1[i]
            if ev.inc[i]:
                b["nTCi"] += 1
                b["nDupi"] += f1[i]
    return {
        "dup0": a["nDupi"] / max(a["nTCi"], 1), "dup1": b["nDupi"] / max(b["nTCi"], 1),
        "dup0_all": a["nDup"] / max(a["nTC"], 1), "dup1_all": b["nDup"] / max(b["nTC"], 1),
        "nTCi0": a["nTCi"], "nTCi1": b["nTCi"],
        "removed": nRemoved, "removedInc": nRemovedI, "simsLost": simsLost,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", required=True)
    ap.add_argument("--kind", choices=("proto", "lst"), required=True)
    ap.add_argument("-n", type=int, default=-1)
    ap.add_argument("--ptcut", type=float, default=0.9)
    ap.add_argument("--label", default=None)
    ap.add_argument("--json", default=None)
    ap.add_argument("--oracles", action="store_true")
    ap.add_argument("--drill", default=None, help='class pair to characterise, e.g. "aPT3,cPLS"')
    args = ap.parse_args()
    ROOT.gROOT.SetBatch(True)
    ROOT.gErrorIgnoreLevel = ROOT.kError
    label = args.label or args.file

    events, have_hits = read_events(args.file, args.kind, args.n, args.ptcut)
    nev = len(events)
    print("=" * 104)
    print("A15 DUPLICATE DECOMPOSITION  [%s]  kind=%s  events=%d  OT-hit axis=%s  metric pt>%.2f"
          % (label, args.kind, nev, "ON" if have_hits else "OFF", args.ptcut))
    print("=" * 104)

    nTC = nDup = nTCi = nDupi = 0
    tc_cls = Counter(); dup_cls = Counter(); fake_cls = Counter()
    tc_cr = Counter(); dup_cr = Counter(); fake_cr = Counter()
    pair_cell = Counter(); pair_struct = Counter()
    pair_reg = Counter(); struct_only = Counter()
    pair_inc = Counter()
    dupTC_partner = Counter()
    grpsize = Counter()

    for ev in events:
        f0, s2t = dup_flags(ev.sims)
        for i in range(ev.n):
            nTC += 1
            nDup += f0[i]
            if not ev.inc[i]:
                continue
            nTCi += 1
            nDupi += f0[i]
            tc_cls[ev.cls[i]] += 1
            tc_cr[(ev.cls[i], ev.reg[i])] += 1
            if f0[i]:
                dup_cls[ev.cls[i]] += 1
                dup_cr[(ev.cls[i], ev.reg[i])] += 1
            if not ev.sims[i]:
                fake_cls[ev.cls[i]] += 1
                fake_cr[(ev.cls[i], ev.reg[i])] += 1
        for s, tcs in s2t.items():
            if len(tcs) > 1:
                grpsize[len(tcs)] += 1
        partner = defaultdict(set)
        for a, b in pairs_of(s2t):
            ca, cb = ev.cls[a], ev.cls[b]
            key = tuple(sorted((ca, cb)))
            rg = region_of(0.5 * (abs(ev.eta[a]) + abs(ev.eta[b])))
            st = struct_of(ev.ot[a], ev.ot[b]) if have_hits else "n/a"
            ni = int(ev.inc[a]) + int(ev.inc[b])
            pair_inc[ni] += 1
            if ni > 0:  # only pairs that can produce a COUNTED duplicate flag
                pair_cell[(key[0], key[1], rg)] += 1
                pair_reg[rg] += 1
                pair_struct[(key[0], key[1], st)] += 1
                struct_only[st] += 1
            partner[a].add(cb)
            partner[b].add(ca)
        for i in range(ev.n):
            if f0[i] and ev.inc[i]:
                dupTC_partner[(ev.cls[i], ev.reg[i], tuple(sorted(partner.get(i, ()))))] += 1

    print("\n[1] TOTALS")
    print("    all TCs        nTC=%8d  nDup=%7d  dup=%.5f" % (nTC, nDup, nDup / max(nTC, 1)))
    print("    METRIC pt>%.1f  nTC=%8d  nDup=%7d  dup=%.5f   <== headline"
          % (args.ptcut, nTCi, nDupi, nDupi / max(nTCi, 1)))
    print("    per event: %.1f TC, %.1f dup (metric universe)" % (nTCi / nev, nDupi / nev))

    print("\n[2] PER-CLASS (metric universe)")
    print("  %-6s %9s %7s %9s %7s %9s | %-23s | %s"
          % ("class", "nTC", "%TC", "nDup", "%dup", "dupRate", "dupRate  B / T / E", "nDup B/T/E"))
    for c, _ in tc_cls.most_common():
        per = " / ".join("%.4f" % (dup_cr[(c, r)] / max(tc_cr[(c, r)], 1)) for r in REGIONS)
        cnt = " / ".join("%d" % dup_cr[(c, r)] for r in REGIONS)
        print("  %-6s %9d %6.1f%% %9d %6.1f%% %9.5f | %-23s | %s"
              % (c, tc_cls[c], 100.0 * tc_cls[c] / max(nTCi, 1), dup_cls[c],
                 100.0 * dup_cls[c] / max(nDupi, 1), dup_cls[c] / max(tc_cls[c], 1), per, cnt))

    print("\n[2b] FAKE PER CLASS (metric universe; fake == no sim matched > 0.75)")
    print("  %-6s %9s %9s %9s | %s" % ("class", "nTC", "nFake", "fakeRate", "fakeRate B / T / E"))
    for c, _ in tc_cls.most_common():
        per = " / ".join("%.4f" % (fake_cr[(c, r)] / max(tc_cr[(c, r)], 1)) for r in REGIONS)
        print("  %-6s %9d %9d %9.5f | %s"
              % (c, tc_cls[c], fake_cls[c], fake_cls[c] / max(tc_cls[c], 1), per))
    print("  %-6s %9d %9d %9.5f  <== headline fake rate"
          % ("ALL", nTCi, sum(fake_cls.values()), sum(fake_cls.values()) / max(nTCi, 1)))

    print("\n[3] PER REGION (metric universe)")
    for r in REGIONS:
        nt = sum(tc_cr[(c, r)] for c in tc_cls)
        nd = sum(dup_cr[(c, r)] for c in tc_cls)
        print("    %s : nTC=%8d  nDup=%7d  dupRate=%.5f  (%.1f%% of all dups)"
              % (r, nt, nd, nd / max(nt, 1), 100.0 * nd / max(nDupi, 1)))

    tot = sum(pair_cell.values())
    print("\n[4] DUP PAIRS with >=1 member in the metric universe: %d (%.1f/evt). "
          "[pairs by #members in-universe: 2=%d 1=%d 0=%d]"
          % (tot, tot / nev, pair_inc[2], pair_inc[1], pair_inc[0]))
    print("  %-14s %8s %8s %8s %9s %8s" % ("class pair", "B", "T", "E", "total", "%pairs"))
    agg = Counter()
    for (a, b, r), v in pair_cell.items():
        agg[(a, b)] += v
    for (a, b), v in agg.most_common():
        print("  %-14s %8d %8d %8d %9d %7.1f%%"
              % ("%s+%s" % (a, b), pair_cell[(a, b, "B")], pair_cell[(a, b, "T")],
                 pair_cell[(a, b, "E")], v, 100.0 * v / max(tot, 1)))
    print("  %-14s %8d %8d %8d %9d" % ("ALL", pair_reg["B"], pair_reg["T"], pair_reg["E"], tot))

    if have_hits:
        print("\n[5] SHARED STRUCTURE (OT hit sets of the two members)")
        print("  %-14s %10s %8s %8s %9s %9s %10s %9s"
              % ("class pair", *STRUCTS, "total"))
        for (a, b), v in agg.most_common():
            row = [pair_struct[(a, b, s)] for s in STRUCTS]
            print("  %-14s %10d %8d %8d %9d %9d %10d %9d" % ("%s+%s" % (a, b), *row, v))
        print("  %-14s %10d %8d %8d %9d %9d %10d %9d"
              % ("ALL", *[struct_only[s] for s in STRUCTS], tot))

    print("\n[6] GROUP SIZES (sims matched by k TCs; all TCs)")
    for k in sorted(grpsize):
        print("    k=%d : %7d groups (%.2f/evt)" % (k, grpsize[k], grpsize[k] / nev))

    print("\n[7] PER-DUP-TC PARTNER COMPOSITION (metric universe, top 22)")
    for (c, r, pk), v in dupTC_partner.most_common(22):
        print("  %-6s reg %s  partners %-22s : %7d (%5.1f%% of dups)"
              % (c, r, ",".join(pk) or "-", v, 100.0 * v / max(nDupi, 1)))

    payload = {"label": label, "nev": nev, "nTC": nTC, "nDup": nDup, "nTCi": nTCi,
               "nDupi": nDupi, "dup": nDupi / max(nTCi, 1),
               "tc_cls": dict(tc_cls), "dup_cls": dict(dup_cls), "fake_cls": dict(fake_cls),
               "dup_cr": {"%s|%s" % k: v for k, v in dup_cr.items()},
               "tc_cr": {"%s|%s" % k: v for k, v in tc_cr.items()},
               "pair_cell": {"%s|%s|%s" % k: v for k, v in pair_cell.items()},
               "pair_struct": {"%s|%s|%s" % k: v for k, v in pair_struct.items()},
               "grpsize": dict(grpsize)}

    if args.oracles:
        print("\n[8] ORACLE YIELDS -- headline dup rate if that cell were solved perfectly")
        print("  %-40s %9s %9s %9s %9s %8s" % ("cell", "dup after", "delta", "TCdropped",
                                               "simsLost", "pairs"))
        cells = [("EVERYTHING (all dup pairs)", lambda ev, a, b: True, tot)]
        for (a, b), v in agg.most_common(12):
            cells.append(("classpair %s+%s" % (a, b),
                          (lambda A, B: (lambda ev, x, y:
                                         tuple(sorted((ev.cls[x], ev.cls[y]))) == (A, B)))(a, b), v))
        if have_hits:
            for st in STRUCTS:
                if struct_only[st] == 0:
                    continue
                cells.append(("structure %s" % st,
                              (lambda S: (lambda ev, x, y: struct_of(ev.ot[x], ev.ot[y]) == S))(st),
                              struct_only[st]))
        for name, pred, npair in cells:
            r = oracle(events, pred)
            print("  %-40s %9.5f %+9.5f %9d %9d %8d"
                  % (name, r["dup1"], r["dup1"] - r["dup0"], r["removedInc"], r["simsLost"], npair))
            payload.setdefault("oracles", {})[name] = r

    if args.drill:
        want = tuple(sorted(args.drill.split(",")))
        print("\n[9] DRILL-DOWN on cell %s+%s" % want)
        drs, aeta, nh1, nh2, sh = [], [], Counter(), Counter(), Counter()
        ndr_small = ntot = 0
        for ev in events:
            _, s2t = dup_flags(ev.sims)
            for a, b in pairs_of(s2t):
                if tuple(sorted((ev.cls[a], ev.cls[b]))) != want:
                    continue
                if not (ev.inc[a] or ev.inc[b]):
                    continue
                ntot += 1
                dphi = ev.phi[a] - ev.phi[b]
                while dphi > 3.14159265:
                    dphi -= 6.28318531
                while dphi < -3.14159265:
                    dphi += 6.28318531
                d = ((ev.eta[a] - ev.eta[b]) ** 2 + dphi ** 2) ** 0.5
                drs.append(d)
                ndr_small += (d * d < 0.02)
                aeta.append(0.5 * (abs(ev.eta[a]) + abs(ev.eta[b])))
                x, y = (a, b) if ev.cls[a] == want[0] else (b, a)
                nh1[ev.nhit[x]] += 1
                nh2[ev.nhit[y]] += 1
                sh[len(ev.ot[a] & ev.ot[b])] += 1
        if ntot:
            drs.sort()
            aeta.sort()
            q = lambda v, f: v[min(len(v) - 1, int(f * len(v)))]
            print("  pairs=%d (%.2f/evt);  dR quantiles 10/50/90 = %.4f / %.4f / %.4f; "
                  "dR^2<0.02 (LST window): %.1f%%"
                  % (ntot, ntot / nev, q(drs, .1), q(drs, .5), q(drs, .9),
                     100.0 * ndr_small / ntot))
            print("  |eta| quantiles 10/50/90 = %.2f / %.2f / %.2f" % (q(aeta, .1), q(aeta, .5), q(aeta, .9)))
            print("  nhitOT of %-5s : %s" % (want[0], " ".join("%d:%d" % kv for kv in sorted(nh1.items()))))
            print("  nhitOT of %-5s : %s" % (want[1], " ".join("%d:%d" % kv for kv in sorted(nh2.items()))))
            print("  shared OT hits  : %s" % " ".join("%d:%d" % kv for kv in sorted(sh.items())))

    if args.json:
        with open(args.json, "w") as jf:
            json.dump(payload, jf, indent=1)
        print("\nwrote %s" % args.json)


if __name__ == "__main__":
    main()
