#!/usr/bin/env python3
"""diag_dups.py - classify duplicate TCs in a hybrid ab_*.root output.

Categories: chain-vs-chain (same best sim, both type 4/9), chain-vs-pixel
(4/9 vs 7/5/8), pixel-vs-pixel. Answers which dup source dominates:
welder braids (chain-chain) vs the hybrid pLS-crossclean artifact (chain-pixel).

Modes:
  EXACT       - hybrid file has tc_simIdxAll/tc_simIdxAllFrac: full group
                reconstruction, exact pair classification.
  DEGRADED    - hybrid file has only sim_tcIdx (current OutputWriter). We then
                reconstruct pixel-row sim matching EXACTLY from the input
                LSTNtuple (hybrid keeps input rows of type 7/5/8 verbatim, in
                input order, with the production >0.75 matching), and chain
                matches PARTIALLY from the sim_tcIdx best-pointers. A dup pixel
                TC none of whose sims has a second pixel matcher MUST have a
                chain partner (deduction). Chain dup TCs with no accepted sim
                best-pointing to them are classified via a kinematic
                nearest-dup-partner cross-check (braided chains share hits, so
                dR is tiny).

Usage: diag_dups.py [--hybrid ab_X.root] [--input LSTNtuple.root]
"""
import argparse
import math
import sys
from collections import Counter, defaultdict

import ROOT

CHAIN_TYPES = (4, 9)
PIX_TYPES = (7, 5, 8)
MATCH_FRAC = 0.75

STANDALONE = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"


def dphi(a, b):
    d = a - b
    while d > math.pi:
        d -= 2 * math.pi
    while d < -math.pi:
        d += 2 * math.pi
    return d


def dr(eta1, phi1, eta2, phi2):
    return math.hypot(eta1 - eta2, dphi(phi1, phi2))


def branch_names(tree):
    return set(b.GetName() for b in tree.GetListOfBranches())


def evt_map(tree):
    """evt value -> entry number (only the evt branch enabled)."""
    tree.SetBranchStatus("*", 0)
    tree.SetBranchStatus("evt", 1)
    m = {}
    for i in range(tree.GetEntries()):
        tree.GetEntry(i)
        m[int(tree.evt)] = i
    tree.SetBranchStatus("*", 1)
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hybrid", default=f"{STANDALONE}/prototype/ab_default.root")
    ap.add_argument("--input", default=f"{STANDALONE}/LSTNtuple_PU200RelVal_300evt.root")
    ap.add_argument("--drcut", type=float, default=0.02, help="dR for kinematic braid pairing")
    args = ap.parse_args()

    fh = ROOT.TFile.Open(args.hybrid)
    if not fh or fh.IsZombie():
        sys.exit(f"cannot open hybrid file {args.hybrid}")
    th = fh.Get("tree")
    hb = branch_names(th)

    # EXACT needs only tc_simIdxAll: the hybrid writer stores ONLY >0.75 matches there
    # (accumulate() pushes the already-thresholded matcher output), so the Frac branch
    # (absent in hybrid outputs) adds nothing; when present it is still applied.
    exact = "tc_simIdxAll" in hb
    have_frac = "tc_simIdxAllFrac" in hb
    if exact:
        print(f"[diag_dups] MODE: EXACT (tc_simIdxAll present%s) on {args.hybrid}"
              % ("" if have_frac else "; no Frac branch -- rows are pre-thresholded >0.75"))
    else:
        missing = [b for b in ("tc_simIdxAll", "tc_isChain", "tc_simIdx") if b not in hb]
        print(f"[diag_dups] MODE: DEGRADED - hybrid file lacks {missing}.")
        print("[diag_dups] Using sim_tcIdx best-pointers + EXACT pixel-row matching "
              "reconstructed from the input ntuple (pixel rows are copied verbatim, "
              "in order) + deduction + kinematic nearest-partner cross-check. "
              "chain-vs-chain is inferred, not exact.")

    # ---------- global accumulators ----------
    n_tc_tot = 0
    n_dup_tot = 0
    dup_by_type = Counter()
    tc_by_type = Counter()

    # exact mode
    pair_counts = Counter()  # 'chain-chain','chain-pix','pix-pix' unordered TC pairs
    cc_same_len = 0
    cc_diff_len = 0
    cc_dlen = Counter()
    dup_tc_class = Counter()  # per dup TC: partner composition

    # degraded mode
    pixdup = Counter()   # pixel dup TC classification
    chdup = Counter()    # chain dup TC classification
    kin_partner = Counter()
    kin_cc_same = 0
    kin_cc_diff = 0
    kin_cc_dlen = Counter()
    kin_drs = []
    n_sim_pixpix = 0     # sims (full list) with >=2 pixel matchers
    n_sim_chpix = 0      # accepted sims: best TC chain AND >=1 pixel matcher
    n_sim_chonly = 0     # accepted sims: best TC chain, no pixel matcher

    # M7 attach-aware classification: with -A 1 attached chains are written as tc_type 7
    # (pT5-class) but carry tc_isChain == 1. When the branch exists, chain/pix
    # classification uses tc_isChain (identical to the type-based rule on pre-attach
    # files, where tc_isChain == 1 iff tc_type in (4, 9)).
    use_ischain = exact and "tc_isChain" in hb
    if use_ischain:
        print("[diag_dups] chain/pix classification: tc_isChain (attach-aware)")
    n_dup_chain_cls = 0
    n_dup_pix_cls = 0

    if exact:
        need = ["tc_type", "tc_isDuplicate", "tc_isFake", "tc_nhitOT", "tc_simIdxAll"]
        if have_frac:
            need.append("tc_simIdxAllFrac")
        if use_ischain:
            need.append("tc_isChain")
        th.SetBranchStatus("*", 0)
        for b in need:
            th.SetBranchStatus(b, 1)
        for ie in range(th.GetEntries()):
            th.GetEntry(ie)
            ttype = list(th.tc_type)
            tdup = list(th.tc_isDuplicate)
            tlen = list(th.tc_nhitOT)
            if use_ischain:
                ischain = [bool(v) for v in th.tc_isChain]
            else:
                ischain = [ty in CHAIN_TYPES for ty in ttype]
            n_tc_tot += len(ttype)
            for t, ty in enumerate(ttype):
                tc_by_type[ty] += 1
                if tdup[t]:
                    n_dup_tot += 1
                    dup_by_type[ty] += 1
                    if ischain[t]:
                        n_dup_chain_cls += 1
                    else:
                        n_dup_pix_cls += 1
            # sim -> matched TCs (frac > MATCH_FRAC; hybrid files store only such rows)
            sim2tc = defaultdict(list)
            for t in range(len(ttype)):
                sims = th.tc_simIdxAll[t]
                if have_frac:
                    fracs = th.tc_simIdxAllFrac[t]
                    for s, f in zip(sims, fracs):
                        if f > MATCH_FRAC:
                            sim2tc[int(s)].append(t)
                else:
                    for s in sims:
                        sim2tc[int(s)].append(t)
            pairs = set()
            for s, tcs in sim2tc.items():
                if len(tcs) < 2:
                    continue
                for i in range(len(tcs)):
                    for j in range(i + 1, len(tcs)):
                        pairs.add((min(tcs[i], tcs[j]), max(tcs[i], tcs[j])))
            partner_kind = defaultdict(set)
            for a, b in pairs:
                ca = ischain[a]
                cb = ischain[b]
                if ca and cb:
                    pair_counts["chain-chain"] += 1
                    if tlen[a] == tlen[b]:
                        cc_same_len += 1
                    else:
                        cc_diff_len += 1
                    cc_dlen[abs(tlen[a] - tlen[b])] += 1
                    partner_kind[a].add("chain")
                    partner_kind[b].add("chain")
                elif ca or cb:
                    pair_counts["chain-pix"] += 1
                    partner_kind[a].add("chain" if cb else "pix")
                    partner_kind[b].add("chain" if ca else "pix")
                else:
                    pair_counts["pix-pix"] += 1
                    partner_kind[a].add("pix")
                    partner_kind[b].add("pix")
            for t in range(len(ttype)):
                if not tdup[t]:
                    continue
                me = "chain" if ischain[t] else "pix"
                pk = partner_kind.get(t, set())
                dup_tc_class[(me, tuple(sorted(pk)))] += 1
    else:
        fi = ROOT.TFile.Open(args.input)
        if not fi or fi.IsZombie():
            sys.exit(f"cannot open input file {args.input}")
        ti = fi.Get("tree")
        emap_h = evt_map(th)
        emap_i = evt_map(ti)
        common = [e for e in emap_h if e in emap_i]
        if len(common) != len(emap_h):
            print(f"[diag_dups] WARNING: only {len(common)}/{len(emap_h)} hybrid events "
                  "found in input by evt value")
        need_h = ["tc_type", "tc_isDuplicate", "tc_isFake", "tc_nhitOT",
                  "tc_eta", "tc_phi", "sim_tcIdx", "sim_pt", "evt"]
        th.SetBranchStatus("*", 0)
        for b in need_h:
            th.SetBranchStatus(b, 1)
        need_i = ["tc_type", "tc_simIdxAll", "tc_simIdxAllFrac", "evt"]
        ti.SetBranchStatus("*", 0)
        for b in need_i:
            ti.SetBranchStatus(b, 1)

        n_sanity_bad = 0
        for evt in common:
            th.GetEntry(emap_h[evt])
            ti.GetEntry(emap_i[evt])

            ittype = list(ti.tc_type)
            kept = [k for k in range(len(ittype)) if ittype[k] in PIX_TYPES]
            ttype = list(th.tc_type)
            tdup = list(th.tc_isDuplicate)
            tlen = list(th.tc_nhitOT)
            teta = list(th.tc_eta)
            tphi = list(th.tc_phi)
            npix = len(kept)

            # sanity: hybrid = [kept pixel rows in input order] + [chains 4/9]
            ok = (len(ttype) >= npix
                  and all(ttype[j] == ittype[kept[j]] for j in range(npix))
                  and all(ttype[j] in CHAIN_TYPES for j in range(npix, len(ttype))))
            if not ok:
                n_sanity_bad += 1
                continue

            n_tc_tot += len(ttype)
            for t, ty in enumerate(ttype):
                tc_by_type[ty] += 1
                if tdup[t]:
                    n_dup_tot += 1
                    dup_by_type[ty] += 1

            # exact pixel-row matching from the input (full sim index space)
            pix_sims = []
            pix_count = Counter()
            for j in range(npix):
                k = kept[j]
                sims = [int(s) for s, f in zip(ti.tc_simIdxAll[k], ti.tc_simIdxAllFrac[k])
                        if f > MATCH_FRAC]
                pix_sims.append(sims)
                for s in sims:
                    pix_count[s] += 1
            n_sim_pixpix += sum(1 for c in pix_count.values() if c >= 2)

            # best-pointers: accepted sim s -> merged TC (accepted idx == full idx)
            stc = list(th.sim_tcIdx)
            chain_best_of = defaultdict(list)  # chain tc -> [sims best-pointing to it]
            sim_chain_best = set()
            for s, t in enumerate(stc):
                if t is None or t < 0:
                    continue
                if t >= npix:
                    chain_best_of[t].append(s)
                    sim_chain_best.add(s)
                    if pix_count.get(s, 0) >= 1:
                        n_sim_chpix += 1
                    else:
                        n_sim_chonly += 1

            # ---- classify dup pixel TCs (exact + deduction) ----
            for t in range(npix):
                if not tdup[t]:
                    continue
                sims = pix_sims[t]
                has_pix = any(pix_count[s] >= 2 for s in sims)
                has_ch_conf = any(s in sim_chain_best for s in sims)
                if has_pix and has_ch_conf:
                    pixdup["both pix+chain partners"] += 1
                elif has_ch_conf:
                    pixdup["chain partner (confirmed)"] += 1
                elif has_pix:
                    pixdup["pixel-vs-pixel only (chain partner possible)"] += 1
                else:
                    # dup flag requires >=2 matchers on some sim; all pixel
                    # matchers are known exactly => partner must be a chain
                    pixdup["chain partner (deduced)"] += 1

            # ---- classify dup chain TCs ----
            dup_idx = [t for t in range(len(ttype)) if tdup[t]]
            for t in range(npix, len(ttype)):
                if not tdup[t]:
                    continue
                S = chain_best_of.get(t, [])
                if any(pix_count.get(s, 0) >= 1 for s in S):
                    chdup["chain-vs-pixel (confirmed via best sim)"] += 1
                elif S:
                    chdup["chain-vs-chain (inferred: best sim has no pixel matcher)"] += 1
                else:
                    chdup["unresolved (no accepted sim best-points here)"] += 1

            # ---- kinematic nearest-dup-partner cross-check for dup chains ----
            for t in range(npix, len(ttype)):
                if not tdup[t]:
                    continue
                best_d, best_p = 1e9, -1
                for p in dup_idx:
                    if p == t:
                        continue
                    d = dr(teta[t], tphi[t], teta[p], tphi[p])
                    if d < best_d:
                        best_d, best_p = d, p
                if best_p < 0:
                    kin_partner["none"] += 1
                    continue
                kin_drs.append(best_d)
                if best_d > args.drcut:
                    kin_partner[f"no partner within dR<{args.drcut}"] += 1
                    continue
                if ttype[best_p] in CHAIN_TYPES:
                    kin_partner["chain"] += 1
                    if tlen[t] == tlen[best_p]:
                        kin_cc_same += 1
                    else:
                        kin_cc_diff += 1
                    kin_cc_dlen[abs(tlen[t] - tlen[best_p])] += 1
                else:
                    kin_partner["pixel"] += 1
        if n_sanity_bad:
            print(f"[diag_dups] WARNING: {n_sanity_bad} events failed the "
                  "kept-pixel-prefix sanity check and were skipped")

    # ---------------- report ----------------
    fdup = 100.0 / max(n_dup_tot, 1)
    print(f"\ntotals: nTC={n_tc_tot}  nDup={n_dup_tot} "
          f"({100.0 * n_dup_tot / max(n_tc_tot, 1):.1f}% of all TCs)")
    print("TC counts by type      : " + "  ".join(
        f"type{ty}={tc_by_type[ty]}" for ty in (4, 9, 7, 5, 8)))
    print("dup TC counts by type  : " + "  ".join(
        f"type{ty}={dup_by_type[ty]} ({dup_by_type[ty] * fdup:.1f}%)" for ty in (4, 9, 7, 5, 8)))
    if use_ischain:
        n_dup_chain = n_dup_chain_cls
        n_dup_pix = n_dup_pix_cls
        print(f"dup TCs (by tc_isChain): chain={n_dup_chain} ({n_dup_chain * fdup:.1f}%)  "
              f"pixel={n_dup_pix} ({n_dup_pix * fdup:.1f}%)")
    else:
        n_dup_chain = sum(dup_by_type[t] for t in CHAIN_TYPES)
        n_dup_pix = sum(dup_by_type[t] for t in PIX_TYPES)
        print(f"dup TCs: chain(4/9)={n_dup_chain} ({n_dup_chain * fdup:.1f}%)  "
              f"pixel(7/5/8)={n_dup_pix} ({n_dup_pix * fdup:.1f}%)")

    if exact:
        tot_pairs = sum(pair_counts.values())
        print(f"\nduplicate TC pairs (exact, sharing a >{MATCH_FRAC} sim): {tot_pairs}")
        for k in ("chain-chain", "chain-pix", "pix-pix"):
            print(f"  {k:<12} = {pair_counts[k]:6d} "
                  f"({100.0 * pair_counts[k] / max(tot_pairs, 1):.1f}%)")
        print(f"chain-chain nhitOT: same={cc_same_len} "
              f"({100.0 * cc_same_len / max(cc_same_len + cc_diff_len, 1):.1f}%) "
              f"diff={cc_diff_len}; |dLen| histo: "
              + "  ".join(f"{k}:{v}" for k, v in sorted(cc_dlen.items())))
        print("\nper-dup-TC partner composition:")
        for (me, pk), n in sorted(dup_tc_class.items(), key=lambda x: -x[1]):
            print(f"  {me} dup with partners {list(pk) or ['?']}: {n} ({n * fdup:.1f}%)")
    else:
        print(f"\nPixel dup TCs ({n_dup_pix}):")
        for k, v in sorted(pixdup.items(), key=lambda x: -x[1]):
            print(f"  {k:<45} = {v:6d} ({v * fdup:.1f}% of all dups)")
        print(f"Chain dup TCs ({n_dup_chain}):")
        for k, v in sorted(chdup.items(), key=lambda x: -x[1]):
            print(f"  {k:<55} = {v:6d} ({v * fdup:.1f}% of all dups)")
        print(f"\nKinematic cross-check (nearest dup partner of each dup chain, "
              f"dR<{args.drcut}):")
        for k, v in sorted(kin_partner.items(), key=lambda x: -x[1]):
            print(f"  nearest partner {k:<28} = {v}")
        if kin_drs:
            kin_drs.sort()
            print(f"  median nearest-dR = {kin_drs[len(kin_drs) // 2]:.4f}")
        ncc = kin_cc_same + kin_cc_diff
        if ncc:
            print(f"  chain-chain kinematic pairs: same nhitOT={kin_cc_same} "
                  f"({100.0 * kin_cc_same / ncc:.1f}%)  diff={kin_cc_diff}; |dLen| histo: "
                  + "  ".join(f"{k}:{v}" for k, v in sorted(kin_cc_dlen.items())))
        print("\nGroup-level (sim view):")
        print(f"  sims (full list, incl. pileup) with >=2 pixel matchers   = {n_sim_pixpix}")
        print(f"  accepted sims best-matched by chain WITH pixel matcher   = {n_sim_chpix}"
              "   <- each is a confirmed chain-vs-pixel dup group")
        print(f"  accepted sims best-matched by chain, no pixel matcher    = {n_sim_chonly}")


if __name__ == "__main__":
    main()
