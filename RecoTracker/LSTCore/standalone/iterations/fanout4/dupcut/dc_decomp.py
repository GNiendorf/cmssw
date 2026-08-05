#!/usr/bin/env python3
"""dc_decomp.py -- duplicate decomposition for the chain-replacement config vs LST.

Reconstructs the EXACT harness duplicate definition (tc_isDuplicate: a TC is a
duplicate iff any sim it matches at frac > 0.75 is matched by >= 2 TCs at > 0.75)
and decomposes the duplicate population by

  (a) provenance-class pair of the duplicate group,
  (b) shared-OT-hit count between the pair (only when hit lists are available),
  (c) nLayers (= nhitOT / 2) of each member,
  (d) eta band.

Both sides use the same events. The proto side is a hybrid chainproto output
(tc_simIdxAll + tc_isChain present); the LST side is the input --allobj ntuple
(tc_simIdxAll + tc_simIdxAllFrac), which is exactly what base300_hists.root was
made from.

Usage: dc_decomp.py --proto dc_base.root [--base <LSTNtuple>] [--tag base]
"""
import argparse
import sys
from collections import Counter, defaultdict

import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
BASE = f"{SA}/LSTNtuple_PU200RelVal_300evt.root"
PT_CUT = 0.9
FRAC = 0.75


def band(eta):
    a = abs(eta)
    if a < 1.1:
        return "barrel"
    if a < 1.7:
        return "trans"
    return "endcap"


# provenance class of a TC row
def proto_cls(ty, isch):
    if isch == 1:
        return "CH"          # bare chain delivery (type 4/9)
    if isch in (2, 3):
        return "ATT"         # attach delivery
    if ty == 7:
        return "cpT5"
    if ty == 5:
        return "cpT3"
    if ty == 8:
        return "cpLS"
    return "c?%d" % ty


def base_cls(ty):
    return {7: "pT5", 5: "pT3", 4: "T5", 9: "T4", 8: "pLS"}.get(ty, "?%d" % ty)


def load(path, is_proto):
    t = uproot.open(path)["tree"]
    want = ["tc_pt", "tc_eta", "tc_type", "tc_nhitOT", "tc_simIdxAll", "lumi", "evt"]
    if is_proto:
        want += ["tc_isChain", "tc_hitOT"]
    else:
        want += ["tc_simIdxAllFrac", "tc_t5Idx", "tc_pt5Idx", "tc_pt3Idx",
                 "pT5_t5Idx", "t5_hitIndices", "pT3_otHitIndices"]
    have = set(t.keys())
    want = [w for w in want if w in have]
    a = t.arrays(want, library="np")
    if not is_proto and "t5_hitIndices" in a:
        # Reconstruct per-TC OT hit rows exactly as the -PU pre-claim mapping does:
        # T5 -> t5_hitIndices, pT5 -> pT5_t5Idx -> t5_hitIndices, pT3 -> pT3_otHitIndices,
        # pLS -> none. T4 rows have no hit branch in the ntuple -> empty (flagged below).
        n = len(a["evt"])
        out = np.empty(n, dtype=object)
        for i in range(n):
            ty = a["tc_type"][i]
            t5h = a["t5_hitIndices"][i]
            p3h = a["pT3_otHitIndices"][i]
            p5t5 = a["pT5_t5Idx"][i]
            rows = []
            for k in range(len(ty)):
                tk = int(ty[k])
                h = []
                if tk == 4:
                    j = int(a["tc_t5Idx"][i][k])
                    if 0 <= j < len(t5h):
                        h = list(t5h[j])
                elif tk == 7:
                    j = int(a["tc_pt5Idx"][i][k])
                    if 0 <= j < len(p5t5):
                        j5 = int(p5t5[j])
                        if 0 <= j5 < len(t5h):
                            h = list(t5h[j5])
                elif tk == 5:
                    j = int(a["tc_pt3Idx"][i][k])
                    if 0 <= j < len(p3h):
                        h = list(p3h[j])
                rows.append(h)
            out[i] = rows
        a["tc_hitOT"] = out
    return a


def analyse(a, is_proto, hits_key=None):
    """Return (per-event records). Each record: list of dup groups."""
    n = len(a["evt"])
    groups = []          # (evt, simIdx, [tc rows])
    tot_tc = 0           # TCs with pt > PT_CUT
    dup_tc = 0
    cls_all = Counter()  # class -> n TCs (pt>cut)
    cls_dup = Counter()  # class -> n dup-flagged TCs (pt>cut)
    for i in range(n):
        pt = a["tc_pt"][i]
        eta = a["tc_eta"][i]
        ty = a["tc_type"][i]
        nh = a["tc_nhitOT"][i]
        sia = a["tc_simIdxAll"][i]
        if is_proto:
            isch = a["tc_isChain"][i]
            cls = [proto_cls(int(ty[k]), int(isch[k])) for k in range(len(ty))]
            frac = None
        else:
            cls = [base_cls(int(ty[k])) for k in range(len(ty))]
            frac = a["tc_simIdxAllFrac"][i]
        hits = a[hits_key][i] if hits_key else None

        sim2tc = defaultdict(list)
        for k in range(len(ty)):
            sl = sia[k]
            if frac is not None:
                fl = frac[k]
                sl = [int(s) for s, f in zip(sl, fl) if f > FRAC]
            else:
                sl = [int(s) for s in sl]
            for s in sl:
                sim2tc[s].append(k)

        isdup = np.zeros(len(ty), dtype=bool)
        for s, rows in sim2tc.items():
            if len(rows) > 1:
                for r in rows:
                    isdup[r] = True
                groups.append(
                    dict(evt=i, sim=s,
                         rows=rows,
                         cls=[cls[r] for r in rows],
                         pt=[float(pt[r]) for r in rows],
                         eta=[float(eta[r]) for r in rows],
                         nh=[int(nh[r]) for r in rows],
                         hits=[list(hits[r]) for r in rows] if hits is not None else None))
        for k in range(len(ty)):
            if pt[k] > PT_CUT:
                tot_tc += 1
                cls_all[cls[k]] += 1
                if isdup[k]:
                    dup_tc += 1
                    cls_dup[cls[k]] += 1
    return dict(groups=groups, tot=tot_tc, dup=dup_tc, cls_all=cls_all, cls_dup=cls_dup)


def report(name, R):
    print("=" * 78)
    print("%s: TCs(pt>%.1f)=%d  dup-flagged=%d  dup rate=%.4f  dup groups=%d"
          % (name, PT_CUT, R["tot"], R["dup"], R["dup"] / max(R["tot"], 1), len(R["groups"])))
    print("-- per-class TC counts (pt>%.1f) and dup-flagged fraction" % PT_CUT)
    for c, nall in sorted(R["cls_all"].items(), key=lambda x: -x[1]):
        nd = R["cls_dup"][c]
        print("   %-6s  nTC=%8d  dup=%7d  (%.4f of class, %.4f of all TCs)"
              % (c, nall, nd, nd / max(nall, 1), nd / max(R["tot"], 1)))

    # group-signature decomposition, counting EXCESS TCs (len-1) with pt>cut
    sig_excess = Counter()
    sig_n = Counter()
    sig_excess_band = defaultdict(Counter)
    for g in R["groups"]:
        keep = [j for j in range(len(g["rows"])) if g["pt"][j] > PT_CUT]
        if len(keep) < 2:
            continue
        sig = "+".join(sorted(Counter(g["cls"][j] for j in keep).elements()))
        sig_n[sig] += 1
        sig_excess[sig] += len(keep) - 1
        b = band(np.mean([g["eta"][j] for j in keep]))
        sig_excess_band[sig][b] += len(keep) - 1
    tot_ex = sum(sig_excess.values())
    print("-- dup GROUP signature -> excess TCs (n_in_group - 1), pt>%.1f members only" % PT_CUT)
    print("   %-28s %8s %8s %7s   %s" % ("signature", "groups", "excess", "frac", "excess by band (B/T/E)"))
    for sig, ex in sorted(sig_excess.items(), key=lambda x: -x[1])[:22]:
        bb = sig_excess_band[sig]
        print("   %-28s %8d %8d %6.3f   %d/%d/%d"
              % (sig, sig_n[sig], ex, ex / max(tot_ex, 1),
                 bb["barrel"], bb["trans"], bb["endcap"]))
    print("   %-28s %8d %8d" % ("TOTAL", sum(sig_n.values()), tot_ex))
    return sig_excess, sig_excess_band


def hit_overlap_report(R, restrict=None):
    """Shared-OT-hit structure of PAIRS inside dup groups (needs hit lists)."""
    if not R["groups"] or R["groups"][0]["hits"] is None:
        return
    print("-- pair shared-OT-hit structure (pairs within dup groups, pt>%.1f both)" % PT_CUT)
    tab = defaultdict(Counter)
    for g in R["groups"]:
        keep = [j for j in range(len(g["rows"])) if g["pt"][j] > PT_CUT]
        for x in range(len(keep)):
            for y in range(x + 1, len(keep)):
                a, b = keep[x], keep[y]
                ca, cb = sorted([g["cls"][a], g["cls"][b]])
                key = ca + "+" + cb
                if restrict and key not in restrict:
                    continue
                sh = len(set(g["hits"][a]) & set(g["hits"][b]))
                bucket = "0" if sh == 0 else ("1-3" if sh < 4 else ("4-7" if sh < 8 else ">=8"))
                tab[key][bucket] += 1
    print("   %-16s %8s %8s %8s %8s %8s" % ("pair", "share0", "1-3", "4-7", ">=8", "total"))
    for key, c in sorted(tab.items(), key=lambda x: -sum(x[1].values())):
        tot = sum(c.values())
        print("   %-16s %8d %8d %8d %8d %8d"
              % (key, c["0"], c["1-3"], c["4-7"], c[">=8"], tot))


def nlayer_report(R, want_sigs):
    print("-- nLayers (nhitOT/2) profile of pt>%.1f dup-group members, by signature" % PT_CUT)
    tab = defaultdict(Counter)
    for g in R["groups"]:
        keep = [j for j in range(len(g["rows"])) if g["pt"][j] > PT_CUT]
        if len(keep) < 2:
            continue
        sig = "+".join(sorted(Counter(g["cls"][j] for j in keep).elements()))
        if sig not in want_sigs:
            continue
        for j in keep:
            tab[sig][g["nh"][j] // 2] += 1
    for sig in want_sigs:
        if sig not in tab:
            continue
        c = tab[sig]
        s = " ".join("%d:%d" % (k, c[k]) for k in sorted(c))
        print("   %-28s %s" % (sig, s))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--proto", required=True)
    ap.add_argument("--base", default=BASE)
    ap.add_argument("--nobase", action="store_true")
    args = ap.parse_args()

    pa = load(args.proto, True)
    hk = "tc_hitOT" if "tc_hitOT" in pa else None
    P = analyse(pa, True, hk)
    sigp, _ = report("PROTO  " + args.proto.split("/")[-1], P)
    hit_overlap_report(P)
    nlayer_report(P, [s for s, _ in sorted(sigp.items(), key=lambda x: -x[1])[:6]])

    if args.nobase:
        return
    ba = load(args.base, False)
    hkb = "tc_hitOT" if "tc_hitOT" in ba else None
    B = analyse(ba, False, hkb)
    sigb, _ = report("LST    " + args.base.split("/")[-1], B)
    hit_overlap_report(B)
    nlayer_report(B, [s for s, _ in sorted(sigb.items(), key=lambda x: -x[1])[:6]])

    print("=" * 78)
    print("SIDE BY SIDE excess-TC budget (pt>%.1f)" % PT_CUT)
    print("  proto TCs=%d dup=%d (%.4f) | LST TCs=%d dup=%d (%.4f)"
          % (P["tot"], P["dup"], P["dup"] / P["tot"], B["tot"], B["dup"], B["dup"] / B["tot"]))
    print("  proto excess=%d  LST excess=%d  delta=%+d"
          % (sum(sigp.values()), sum(sigb.values()), sum(sigp.values()) - sum(sigb.values())))


if __name__ == "__main__":
    main()
