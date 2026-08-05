#!/usr/bin/env python3
"""Independent re-count of the dupcut M17 decomposition claims.

Written from scratch by the adversarial verifier: does NOT import or reuse
dc_decomp.py / dc_chch.py. Checks:
  1. dc_base.root vs dc_diag.root are the SAME physics (per-event TC arrays equal).
  2. The dup-group definition reproduces the harness tc_isDuplicate flag exactly.
  3. Pair-level shared-OT-hit table for CH+cpLS (the "zero shared OT hits" claim),
     CH+CH and cpLS+cpLS.
  4. CH-CH accepted-pair purity wall (dup vs collateral) by shared-hit count.
  5. LST-side counterpart pair counts for the delta claims.
"""
import sys
from collections import Counter, defaultdict

import numpy as np
import uproot

S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
PROTO = f"{S}/fanout4/dupcut/dc_diag.root"
PROTO_NOHIT = f"{S}/fanout4/dupcut/dc_base.root"
LST = f"{S}/LSTNtuple_PU200RelVal_300evt.root"
PT = 0.9
FR = 0.75


def pcls(ty, isch):
    if isch == 1:
        return "CH"
    if isch in (2, 3):
        return "ATT"
    return {7: "cpT5", 5: "cpT3", 8: "cpLS"}.get(ty, "c?%d" % ty)


def lcls(ty):
    return {7: "pT5", 5: "pT3", 4: "T5", 9: "T4", 8: "pLS"}.get(ty, "?%d" % ty)


def sanity_same_physics():
    a = uproot.open(PROTO_NOHIT)["tree"].arrays(
        ["tc_pt", "tc_type", "tc_isChain", "tc_isDuplicate", "tc_isFake"], library="np")
    b = uproot.open(PROTO)["tree"].arrays(
        ["tc_pt", "tc_type", "tc_isChain", "tc_isDuplicate", "tc_isFake"], library="np")
    bad = 0
    for i in range(len(a["tc_pt"])):
        for k in a:
            if len(a[k][i]) != len(b[k][i]) or not np.array_equal(np.asarray(a[k][i]), np.asarray(b[k][i])):
                bad += 1
                break
    print("[1] dc_base.root vs dc_diag.root: %d/%d events differ -> %s"
          % (bad, len(a["tc_pt"]), "SAME PHYSICS" if bad == 0 else "DIFFERENT"))


def run_proto():
    t = uproot.open(PROTO)["tree"]
    a = t.arrays(["tc_pt", "tc_eta", "tc_type", "tc_nhitOT", "tc_simIdxAll",
                  "tc_isChain", "tc_isDuplicate", "tc_hitOT"], library="np")
    n = len(a["tc_pt"])
    tot = dupflag_harness = dup_recomp = 0
    mismatch = 0
    clsall = Counter()
    pairtab = defaultdict(Counter)          # class-pair -> shared-hit bucket
    chch_spec = defaultdict(lambda: Counter())  # shared-hit -> {'dup','collat'}
    nrows_by_cls = Counter()
    for i in range(n):
        pt = a["tc_pt"][i]
        ty = a["tc_type"][i]
        ich = a["tc_isChain"][i]
        sia = a["tc_simIdxAll"][i]
        hit = a["tc_hitOT"][i]
        idup = a["tc_isDuplicate"][i]
        nt = len(pt)
        cls = [pcls(int(ty[k]), int(ich[k])) for k in range(nt)]

        sim2 = defaultdict(list)
        for k in range(nt):
            for s in sia[k]:
                sim2[int(s)].append(k)
        rec = np.zeros(nt, dtype=bool)
        groups = []
        for s, rows in sim2.items():
            if len(rows) > 1:
                for r in rows:
                    rec[r] = True
                groups.append(rows)
        for k in range(nt):
            if pt[k] > PT:
                tot += 1
                clsall[cls[k]] += 1
                if idup[k]:
                    dupflag_harness += 1
                if rec[k]:
                    dup_recomp += 1
                if bool(idup[k]) != bool(rec[k]):
                    mismatch += 1
        # pair table inside dup groups
        for rows in groups:
            keep = [r for r in rows if pt[r] > PT]
            for x in range(len(keep)):
                for y in range(x + 1, len(keep)):
                    p, q = keep[x], keep[y]
                    key = "+".join(sorted([cls[p], cls[q]]))
                    sh = len(set(hit[p].tolist()) & set(hit[q].tolist()))
                    bucket = "0" if sh == 0 else ("1-3" if sh < 4 else ("4-7" if sh < 8 else ">=8"))
                    pairtab[key][bucket] += 1
                    pairtab[key]["TOT"] += 1
        # CH-CH ACCEPTED-pair spectrum: all CH pairs (pt>0.9) sharing >=0 OT hits,
        # labelled dup (same sim, both in a common dup group) vs collateral.
        chidx = [k for k in range(nt) if cls[k] == "CH" and pt[k] > PT]
        simset = [set(int(s) for s in sia[k]) for k in range(nt)]
        # index hits -> chains to find sharing pairs without O(N^2)
        h2c = defaultdict(list)
        for k in chidx:
            for h in hit[k].tolist():
                h2c[int(h)].append(k)
        cand = set()
        for h, lst in h2c.items():
            if len(lst) > 1:
                for x in range(len(lst)):
                    for y in range(x + 1, len(lst)):
                        cand.add((lst[x], lst[y]))
        for (p, q) in cand:
            sh = len(set(hit[p].tolist()) & set(hit[q].tolist()))
            same = len(simset[p] & simset[q]) > 0
            chch_spec[sh]["dup" if same else "collat"] += 1
    print("[2] proto TCs(pt>%.1f)=%d  harness tc_isDuplicate=%d  recomputed=%d  mismatch=%d"
          % (PT, tot, dupflag_harness, dup_recomp, mismatch))
    print("    dup rate harness=%.4f recomputed=%.4f" % (dupflag_harness / tot, dup_recomp / tot))
    print("    class TC counts:", dict(clsall))
    print("[3] pair shared-OT-hit table inside dup groups (pt>%.1f both)" % PT)
    print("    %-16s %8s %8s %8s %8s %8s" % ("pair", "share0", "1-3", "4-7", ">=8", "total"))
    for key, c in sorted(pairtab.items(), key=lambda x: -x[1]["TOT"]):
        print("    %-16s %8d %8d %8d %8d %8d"
              % (key, c["0"], c["1-3"], c["4-7"], c[">=8"], c["TOT"]))
    print("[4] CH-CH accepted-pair purity wall by shared OT hits (pt>%.1f both)" % PT)
    print("    %-8s %8s %8s %8s" % ("shared", "DUP", "COLLAT", "purity"))
    for sh in sorted(chch_spec):
        d = chch_spec[sh]["dup"]
        c = chch_spec[sh]["collat"]
        print("    %-8d %8d %8d %8.3f" % (sh, d, c, d / max(d + c, 1)))
    return pairtab


def run_lst():
    t = uproot.open(LST)["tree"]
    keys = set(t.keys())
    want = ["tc_pt", "tc_eta", "tc_type", "tc_simIdxAll", "tc_simIdxAllFrac"]
    want = [w for w in want if w in keys]
    a = t.arrays(want, library="np")
    n = len(a["tc_pt"])
    tot = dup = 0
    pairtab = defaultdict(Counter)
    for i in range(n):
        pt = a["tc_pt"][i]
        ty = a["tc_type"][i]
        sia = a["tc_simIdxAll"][i]
        fr = a["tc_simIdxAllFrac"][i]
        nt = len(pt)
        cls = [lcls(int(ty[k])) for k in range(nt)]
        sim2 = defaultdict(list)
        for k in range(nt):
            for s, f in zip(sia[k], fr[k]):
                if f > FR:
                    sim2[int(s)].append(k)
        rec = np.zeros(nt, dtype=bool)
        groups = []
        for s, rows in sim2.items():
            if len(rows) > 1:
                for r in rows:
                    rec[r] = True
                groups.append(rows)
        for k in range(nt):
            if pt[k] > PT:
                tot += 1
                if rec[k]:
                    dup += 1
        for rows in groups:
            keep = [r for r in rows if pt[r] > PT]
            for x in range(len(keep)):
                for y in range(x + 1, len(keep)):
                    key = "+".join(sorted([cls[keep[x]], cls[keep[y]]]))
                    pairtab[key]["TOT"] += 1
    print("[5] LST TCs(pt>%.1f)=%d dup=%d rate=%.4f" % (PT, tot, dup, dup / tot))
    print("    LST dup-group pair counts by class pair:")
    for key, c in sorted(pairtab.items(), key=lambda x: -x[1]["TOT"]):
        print("    %-16s %8d" % (key, c["TOT"]))
    return pairtab


if __name__ == "__main__":
    sanity_same_physics()
    pp = run_proto()
    lp = run_lst()
    print("[6] DELTA checks (proto pair count - LST counterpart pair count)")
    ch_pls = pp["CH+cpLS"]["TOT"]
    lst_ot_pls = sum(lp[k]["TOT"] for k in ("pLS+pT5", "T5+pLS", "T4+pLS", "pLS+pT3") if k in lp)
    lst_ot_pls_nopt3 = sum(lp[k]["TOT"] for k in ("pLS+pT5", "T5+pLS", "T4+pLS") if k in lp)
    print("    CH+cpLS pairs        = %d" % ch_pls)
    print("    LST OT+pLS pairs (incl pT3) = %d -> delta %+d" % (lst_ot_pls, ch_pls - lst_ot_pls))
    print("    LST OT+pLS pairs (excl pT3) = %d -> delta %+d" % (lst_ot_pls_nopt3, ch_pls - lst_ot_pls_nopt3))
    chch = pp["CH+CH"]["TOT"]
    chch13 = pp["CH+CH"]["1-3"]
    lst_otot = sum(v["TOT"] for k, v in lp.items()
                   if "pLS" not in k)
    print("    CH+CH pairs total=%d (1-3 shared=%d)" % (chch, chch13))
    print("    LST OT-OT pairs total=%d -> delta total %+d, delta(1-3 vs all OT-OT) %+d"
          % (lst_otot, chch - lst_otot, chch13 - lst_otot))
    print("    cpLS+cpLS pairs=%d | LST pLS+pLS pairs=%d -> delta %+d"
          % (pp["cpLS+cpLS"]["TOT"], lp["pLS+pLS"]["TOT"],
             pp["cpLS+cpLS"]["TOT"] - lp["pLS+pLS"]["TOT"]))
