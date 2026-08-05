#!/usr/bin/env python3
"""rd_decomp.py -- RECON A: dup-rate excess over LST, binned in |eta| x duplicate-pair cell.

Lineage: fanout4/dupcut/dc_decomp.py + dc_chch.py, adapted for
  (a) 0.25-wide |eta| bins instead of 3 coarse bands,
  (b) attach ON (tc_isChain in {0 carried, 1 chain, 2 attach-pT5, 3 attach-pT3}),
  (c) a per-band purity wall (same-sim fraction of accepted chain pairs vs shared-hit count).

Duplicate definition is the harness one, reproduced exactly:
  tc_simIdxAll on the proto side is ALREADY thresholded at frac > 0.75 by
  matchedSimTrkIdxsAndFracs, so a TC is a duplicate iff some sim in its list is
  in >= 2 TCs' lists. On the LST (input-ntuple) side tc_simIdxAll is unthresholded,
  so tc_simIdxAllFrac > 0.75 is applied here.
  Only TCs with pt > 0.9 enter, matching ana.pt_cut in the _dr_*_eta histograms.

Usage: rd_decomp.py --proto rd_fl.root [--base <LSTNtuple>] [--out prefix]
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

# 0.25-wide |eta| bins up to 3.0, then one overflow bin.
EDGES = [round(0.25 * i, 2) for i in range(13)] + [4.5]
NB = len(EDGES) - 1
WINDOW = (1.5, 3.0)          # the maintainer-identified dup window


def ebin(eta):
    a = abs(eta)
    for i in range(NB):
        if a < EDGES[i + 1]:
            return i
    return NB - 1


def blabel(i):
    return "%.2f-%.2f" % (EDGES[i], EDGES[i + 1])


def in_window(eta):
    return WINDOW[0] <= abs(eta) < WINDOW[1]


def pband(eta):
    """coarse purity-wall band"""
    a = abs(eta)
    if a < 1.1:
        return "barrel<1.1"
    if a < 1.5:
        return "1.1-1.5"
    if a < 2.0:
        return "1.5-2.0"
    if a < 2.5:
        return "2.0-2.5"
    if a < 3.0:
        return "2.5-3.0"
    return ">3.0"


PBANDS = ["barrel<1.1", "1.1-1.5", "1.5-2.0", "2.0-2.5", "2.5-3.0", ">3.0"]


def proto_cls(ty, isch):
    if isch == 1:
        return "CH"
    if isch == 2:
        return "ATT5"
    if isch == 3:
        return "ATT3"
    if ty == 7:
        return "cpT5"
    if ty == 5:
        return "cpT3"
    if ty == 8:
        return "cpLS"
    return "c?%d" % ty


def base_cls(ty):
    return {7: "pT5", 5: "pT3", 4: "T5", 9: "T4", 8: "pLS"}.get(ty, "?%d" % ty)


CHAINISH = ("CH", "ATT5", "ATT3")


def load(path, is_proto):
    t = uproot.open(path)["tree"]
    want = ["tc_pt", "tc_eta", "tc_type", "tc_nhitOT", "tc_simIdxAll", "tc_isDuplicate", "evt"]
    if is_proto:
        want += ["tc_isChain", "tc_hitOT"]
    else:
        want += ["tc_simIdxAllFrac", "tc_t5Idx", "tc_pt5Idx", "tc_pt3Idx",
                 "pT5_t5Idx", "t5_hitIndices", "pT3_otHitIndices"]
    have = set(k.split(";")[0] for k in t.keys())
    want = [w for w in want if w in have]
    a = t.arrays(want, library="np")
    if not is_proto and "t5_hitIndices" in a:
        n = len(a["tc_pt"])
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


def shlabel(sh):
    if sh == 0:
        return "sh0"
    if sh <= 2:
        return "sh1-2"
    if sh == 3:
        return "sh3"
    return "sh>=4"


def analyse(a, is_proto):
    """Per-|eta|-bin TC counts, dup-flagged counts, and per-cell excess."""
    n = len(a["tc_pt"])
    ntc = np.zeros(NB, dtype=np.int64)         # pt>cut TCs
    ndup = np.zeros(NB, dtype=np.int64)        # pt>cut dup-flagged TCs
    ntc_cls = defaultdict(lambda: np.zeros(NB, dtype=np.int64))
    ndup_cls = defaultdict(lambda: np.zeros(NB, dtype=np.int64))
    cell_ex = defaultdict(lambda: np.zeros(NB, dtype=np.int64))   # excess (n-1) per cell
    cell_grp = Counter()
    nbig = 0
    ngrp = 0
    dup_recon = 0
    hitless = 0

    # purity wall accumulators (proto only): chain-family pairs sharing k OT hits
    pw_same = defaultdict(Counter)   # band -> shared-count -> n same-sim pairs
    pw_diff = defaultdict(Counter)   # band -> shared-count -> n different-sim pairs

    for i in range(n):
        pt = a["tc_pt"][i]
        eta = a["tc_eta"][i]
        ty = a["tc_type"][i]
        sia = a["tc_simIdxAll"][i]
        isdup_br = a["tc_isDuplicate"][i]
        hits = a["tc_hitOT"][i] if "tc_hitOT" in a else None
        if is_proto:
            isch = a["tc_isChain"][i]
            cls = [proto_cls(int(ty[k]), int(isch[k])) for k in range(len(ty))]
            frac = None
        else:
            cls = [base_cls(int(ty[k])) for k in range(len(ty))]
            frac = a["tc_simIdxAllFrac"][i]

        keep = [k for k in range(len(ty)) if pt[k] > PT_CUT]
        for k in keep:
            b = ebin(eta[k])
            ntc[b] += 1
            ntc_cls[cls[k]][b] += 1
            if isdup_br[k]:
                ndup[b] += 1
                ndup_cls[cls[k]][b] += 1

        # sim -> rows (frac-thresholded)
        sim2tc = defaultdict(list)
        for k in range(len(ty)):
            sl = sia[k]
            if frac is not None:
                sl = [int(s) for s, f in zip(sl, frac[k]) if f > FRAC]
            else:
                sl = [int(s) for s in sl]
            for s in sl:
                sim2tc[s].append(k)

        recon = set()
        for s, rows in sim2tc.items():
            if len(rows) < 2:
                continue
            for r in rows:
                recon.add(r)
            kr = [r for r in rows if pt[r] > PT_CUT]
            if len(kr) < 2:
                continue
            ngrp += 1
            if len(kr) > 2:
                nbig += 1
            cc = sorted(cls[r] for r in kr)
            sig = "+".join(cc)
            # chain-chain groups get a shared-OT-hit refinement
            if hits is not None and all(c in CHAINISH for c in cc):
                shmax = 0
                ok = True
                for x in range(len(kr)):
                    for y in range(x + 1, len(kr)):
                        hx, hy = set(hits[kr[x]]), set(hits[kr[y]])
                        if not hx or not hy:
                            ok = False
                        shmax = max(shmax, len(hx & hy))
                if ok:
                    sig = sig + " " + shlabel(shmax)
                else:
                    hitless += 1
            b = ebin(np.mean([abs(eta[r]) for r in kr]))
            cell_ex[sig][b] += len(kr) - 1
            cell_grp[sig] += 1
        dup_recon += sum(1 for r in recon if pt[r] > PT_CUT)

        # ---- purity wall (proto only) ----
        if is_proto and hits is not None:
            rows = [k for k in range(len(ty)) if cls[k] in CHAINISH and pt[k] > PT_CUT]
            hset = {k: set(hits[k]) for k in rows}
            sims = {k: set(int(s) for s in sia[k]) for k in rows}
            h2c = defaultdict(list)
            for k in rows:
                for h in hset[k]:
                    h2c[h].append(k)
            cand = set()
            for h, ks in h2c.items():
                if len(ks) < 2:
                    continue
                for x in range(len(ks)):
                    for y in range(x + 1, len(ks)):
                        cand.add((min(ks[x], ks[y]), max(ks[x], ks[y])))
            for (x, y) in cand:
                sh = len(hset[x] & hset[y])
                k = sh if sh <= 5 else 6
                bd = pband(0.5 * (abs(eta[x]) + abs(eta[y])))
                if sims[x] & sims[y]:
                    pw_same[bd][k] += 1
                else:
                    pw_diff[bd][k] += 1

    return dict(ntc=ntc, ndup=ndup, ntc_cls=dict(ntc_cls), ndup_cls=dict(ndup_cls),
                cell_ex=dict(cell_ex), cell_grp=cell_grp, ngrp=ngrp, nbig=nbig,
                dup_recon=dup_recon, hitless=hitless,
                pw_same=pw_same, pw_diff=pw_diff)


def hdr(s):
    print("\n" + "=" * 100)
    print(s)
    print("=" * 100)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--proto", required=True)
    ap.add_argument("--base", default=BASE)
    args = ap.parse_args()

    P = analyse(load(args.proto, True), True)
    B = analyse(load(args.base, False), False)

    # ---------- sanity ----------
    hdr("SANITY: reconstructed dup population vs the tc_isDuplicate branch (pt>0.9)")
    print("  proto  dup-flagged=%d  reconstructed-from-simIdxAll=%d  (match=%s)"
          % (P["ndup"].sum(), P["dup_recon"], P["ndup"].sum() == P["dup_recon"]))
    print("  LST    dup-flagged=%d  reconstructed-from-simIdxAll=%d  (match=%s)"
          % (B["ndup"].sum(), B["dup_recon"], B["ndup"].sum() == B["dup_recon"]))
    print("  proto  nTC=%d rate=%.4f | LST nTC=%d rate=%.4f | delta=%+.4f"
          % (P["ntc"].sum(), P["ndup"].sum() / P["ntc"].sum(),
             B["ntc"].sum(), B["ndup"].sum() / B["ntc"].sum(),
             P["ndup"].sum() / P["ntc"].sum() - B["ndup"].sum() / B["ntc"].sum()))
    print("  proto groups=%d (size>2: %d)  LST groups=%d (size>2: %d)  chain grp w/o hits=%d"
          % (P["ngrp"], P["nbig"], B["ngrp"], B["nbig"], P["hitless"]))

    # ---------- Table 1: dup rate vs |eta| ----------
    hdr("TABLE 1: duplicate rate vs |eta| (0.25 bins, pt>0.9) -- proto FLAGSHIP vs LST")
    print("%-12s %9s %8s %8s | %9s %8s %8s | %9s %10s %10s"
          % ("|eta|", "nTC_p", "dup_p", "rate_p", "nTC_L", "dup_L", "rate_L",
             "d(rate)", "excessTC", "gl.pts"))
    gtotP = P["ntc"].sum()
    tot_ex = 0
    for i in range(NB):
        rp = P["ndup"][i] / max(P["ntc"][i], 1)
        rl = B["ndup"][i] / max(B["ntc"][i], 1)
        # excess dup TCs at fixed proto denominator
        exc = P["ndup"][i] - rl * P["ntc"][i]
        tot_ex += exc
        mark = "  <== WINDOW" if EDGES[i] >= 1.5 and EDGES[i + 1] <= 3.0 else ""
        print("%-12s %9d %8d %8.4f | %9d %8d %8.4f | %+9.4f %10.1f %10.4f%s"
              % (blabel(i), P["ntc"][i], P["ndup"][i], rp,
                 B["ntc"][i], B["ndup"][i], rl, rp - rl, exc, exc / gtotP, mark))
    print("%-12s %9d %8d %8.4f | %9d %8d %8.4f | %+9.4f %10.1f %10.4f"
          % ("TOTAL", P["ntc"].sum(), P["ndup"].sum(), P["ndup"].sum() / gtotP,
             B["ntc"].sum(), B["ndup"].sum(), B["ndup"].sum() / B["ntc"].sum(),
             P["ndup"].sum() / gtotP - B["ndup"].sum() / B["ntc"].sum(),
             tot_ex, tot_ex / gtotP))
    wlo = [i for i in range(NB) if EDGES[i] >= 1.5 and EDGES[i + 1] <= 3.0]
    wex = sum(P["ndup"][i] - (B["ndup"][i] / max(B["ntc"][i], 1)) * P["ntc"][i] for i in wlo)
    print("\n  WINDOW |eta| 1.5-3.0 : excess dup TCs = %.1f of %.1f total (%.1f%%)  "
          "= %.4f global dup-rate points" % (wex, tot_ex, 100 * wex / max(tot_ex, 1e-9), wex / gtotP))

    # ---------- Table 2: cell x eta excess matrix ----------
    for side, R, name in (("P", P, "PROTO (flagship)"), ("L", B, "LST (baseline)")):
        hdr("TABLE 2%s: dup-group EXCESS TCs (n_members-1) by cell x |eta| bin -- %s" % (side, name))
        tot = sum(v.sum() for v in R["cell_ex"].values())
        print("%-34s %8s %7s | %s" % ("cell (dup-group signature)", "excess", "frac",
                                      " ".join("%6s" % blabel(i).split("-")[0] for i in range(NB))))
        for sig, v in sorted(R["cell_ex"].items(), key=lambda x: -x[1].sum())[:26]:
            print("%-34s %8d %7.3f | %s"
                  % (sig, v.sum(), v.sum() / max(tot, 1), " ".join("%6d" % v[i] for i in range(NB))))
        print("%-34s %8d %7.3f | %s"
              % ("TOTAL", tot, 1.0,
                 " ".join("%6d" % sum(v[i] for v in R["cell_ex"].values()) for i in range(NB))))
        # window-only ranking
        print("\n  -- restricted to |eta| 1.5-3.0 --")
        wtot = sum(sum(v[i] for i in wlo) for v in R["cell_ex"].values())
        print("  %-34s %8s %7s" % ("cell", "excess", "frac"))
        for sig, v in sorted(R["cell_ex"].items(), key=lambda x: -sum(x[1][i] for i in wlo))[:16]:
            e = sum(v[i] for i in wlo)
            if e == 0:
                break
            print("  %-34s %8d %7.3f" % (sig, e, e / max(wtot, 1)))
        print("  %-34s %8d" % ("TOTAL window", wtot))

    # ---------- Table 3: purity wall per band ----------
    hdr("TABLE 3: PURITY WALL -- accepted chain-family pairs sharing k OT hits, fraction same-sim")
    print("%-12s %8s | %s" % ("band", "", " ".join("%14s" % ("share=%d" % k if k <= 5 else "share>=6")
                                                   for k in range(7))))
    for bd in PBANDS:
        s, d = P["pw_same"][bd], P["pw_diff"][bd]
        cells = []
        for k in range(7):
            n = s[k] + d[k]
            cells.append("%6d %7s" % (n, ("%.3f" % (s[k] / n)) if n else "  -  "))
        print("%-12s %8s | %s" % (bd, "n/purity", " ".join("%14s" % c for c in cells)))
    # global row
    gs, gd = Counter(), Counter()
    for bd in PBANDS:
        gs.update(P["pw_same"][bd])
        gd.update(P["pw_diff"][bd])
    cells = []
    for k in range(7):
        n = gs[k] + gd[k]
        cells.append("%6d %7s" % (n, ("%.3f" % (gs[k] / n)) if n else "  -  "))
    print("%-12s %8s | %s" % ("GLOBAL", "n/purity", " ".join("%14s" % c for c in cells)))

    # ---------- Table 4: class census in the window ----------
    hdr("TABLE 4: TC class census (pt>0.9) -- all |eta| vs window 1.5-3.0, with dup-flagged fraction")
    for side, R, name in (("P", P, "PROTO"), ("L", B, "LST")):
        print("\n  %s" % name)
        print("  %-8s %10s %10s %8s | %10s %10s %8s"
              % ("class", "nTC_all", "dup_all", "f", "nTC_win", "dup_win", "f"))
        for c in sorted(R["ntc_cls"], key=lambda c: -R["ntc_cls"][c].sum()):
            na, da = R["ntc_cls"][c].sum(), R["ndup_cls"].get(c, np.zeros(NB))[...].sum()
            nw = sum(R["ntc_cls"][c][i] for i in wlo)
            dw = sum(R["ndup_cls"].get(c, np.zeros(NB, dtype=np.int64))[i] for i in wlo)
            print("  %-8s %10d %10d %8.4f | %10d %10d %8.4f"
                  % (c, na, da, da / max(na, 1), nw, dw, dw / max(nw, 1)))


if __name__ == "__main__":
    main()
