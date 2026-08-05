#!/usr/bin/env python3
"""make_dup_corrected.py - DIAGNOSTIC artifact-corrected duplicate flags.

Recomputes tc_isDuplicate for a hybrid chain-prototype output (ab_*.root)
EXCLUDING cross-seam (chain-vs-pixel) duplicate pairs, which are an artifact
of the hybrid harness: a chain TC and a carried-baseline pixel TC matching
the same sim would be cross-cleaned at real integration. The corrected flag
counts a TC as duplicate ONLY if it shares a >0.75-matched sim with another
TC of the SAME slice:

  chain slice = tc_isChain == 1 (or, absent that branch, tc_type in (4, 9))
  pixel slice = everything else

This is the EXACT-mode mechanism of diag_dups.py: the hybrid writer stores
in tc_simIdxAll ONLY the >0.75-matched sim rows (accumulate() pushes the
already-thresholded matcher output, full sim list incl. pileup), so grouping
TCs by shared tc_simIdxAll entries reproduces the writer's own duplicate
definition exactly. That is verified per event before the correction: the
UNRESTRICTED recomputation must equal the stored tc_isDuplicate bit-for-bit,
otherwise the script aborts.

Output: an identical tree with only tc_isDuplicate replaced, plus the three
TNamed (code_tag_data / gitdiff / input) copied, with code_tag_data tagged
"_dupfix" to mark the file as DIAGNOSTIC. Nothing else is modified.

Usage: make_dup_corrected.py [--hybrid ab_X.root] [--out ab_X_dupfix.root]
"""
import argparse
import sys
from collections import defaultdict

import ROOT

CHAIN_TYPES = (4, 9)
MATCH_FRAC = 0.75

STANDALONE = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"


def branch_names(tree):
    return set(b.GetName() for b in tree.GetListOfBranches())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hybrid", default=f"{STANDALONE}/prototype/ab_m8_h4b.root")
    ap.add_argument("--out", default=f"{STANDALONE}/prototype/ab_m8_h4b_dupfix.root")
    args = ap.parse_args()

    fin = ROOT.TFile.Open(args.hybrid)
    if not fin or fin.IsZombie():
        sys.exit(f"cannot open hybrid file {args.hybrid}")
    tin = fin.Get("tree")
    hb = branch_names(tin)

    if "tc_simIdxAll" not in hb:
        sys.exit("hybrid file lacks tc_simIdxAll - EXACT recomputation impossible. "
                 "This script only supports EXACT-mode files (see diag_dups.py).")
    have_frac = "tc_simIdxAllFrac" in hb
    use_ischain = "tc_isChain" in hb
    print(f"[make_dup_corrected] {args.hybrid}")
    print(f"[make_dup_corrected] MODE: EXACT (tc_simIdxAll present%s); slice via "
          % ("" if have_frac else "; rows pre-thresholded >0.75")
          + ("tc_isChain (attach-aware)" if use_ischain else "tc_type in (4,9)"))

    fout = ROOT.TFile(args.out, "RECREATE")
    fout.SetCompressionSettings(fin.GetCompressionSettings())
    tout = tin.CloneTree(0)  # shares branch addresses with tin

    n_tc_tot = 0
    n_dup_orig = 0
    n_dup_new = 0
    n_dup_new_chain = 0
    n_dup_new_pix = 0
    n_removed = 0
    n_mismatch = 0

    nent = tin.GetEntries()
    for ie in range(nent):
        tin.GetEntry(ie)
        ttype = list(tin.tc_type)
        n = len(ttype)
        if use_ischain:
            ischain = [bool(v) for v in tin.tc_isChain]
        else:
            ischain = [ty in CHAIN_TYPES for ty in ttype]

        # sim -> matched TCs (>MATCH_FRAC; hybrid files store only such rows)
        tc_sims = []
        sim2tc = defaultdict(list)
        for t in range(n):
            sims = tin.tc_simIdxAll[t]
            if have_frac:
                fr = tin.tc_simIdxAllFrac[t]
                sims = [int(s) for s, f in zip(sims, fr) if f > MATCH_FRAC]
            else:
                sims = [int(s) for s in sims]
            tc_sims.append(sims)
            for s in sims:
                sim2tc[s].append(t)

        # per-sim matcher counts, total and per slice
        sim_nch = {s: sum(1 for t in tcs if ischain[t]) for s, tcs in sim2tc.items()}

        vdup = tin.tc_isDuplicate  # shared with tout via CloneTree(0)
        for t in range(n):
            n_tc_tot += 1
            orig = int(vdup[t])
            if orig:
                n_dup_orig += 1
            # verification: unrestricted recomputation must match the stored flag
            recomp = any(len(sim2tc[s]) >= 2 for s in tc_sims[t])
            if int(recomp) != orig:
                n_mismatch += 1
            # corrected: same-slice partner required
            if ischain[t]:
                new = any(sim_nch[s] >= 2 for s in tc_sims[t])
            else:
                new = any(len(sim2tc[s]) - sim_nch[s] >= 2 for s in tc_sims[t])
            if new:
                n_dup_new += 1
                if ischain[t]:
                    n_dup_new_chain += 1
                else:
                    n_dup_new_pix += 1
            if orig and not new:
                n_removed += 1
            vdup[t] = 1 if new else 0

        tout.Fill()

    if n_mismatch:
        fout.Close()
        sys.exit(f"[make_dup_corrected] FATAL: {n_mismatch} TCs where the unrestricted "
                 "recomputation disagrees with the stored tc_isDuplicate - the "
                 "mechanism does not replicate the writer; refusing to write output.")

    fout.cd()
    tout.Write()
    # copy the three TNamed; tag code_tag_data as diagnostic
    for key in ("code_tag_data", "gitdiff", "input"):
        obj = fin.Get(key)
        title = obj.GetTitle() if obj else ""
        if key == "code_tag_data":
            title = title + "_dupfix"
        ROOT.TNamed(key, title).Write()
    fout.Close()

    print(f"[make_dup_corrected] events={nent}  nTC={n_tc_tot}")
    print(f"[make_dup_corrected] verification: unrestricted recomputation == stored "
          f"tc_isDuplicate for all {n_tc_tot} TCs (0 mismatches)")
    print(f"[make_dup_corrected] dup flags: orig={n_dup_orig} "
          f"({100.0 * n_dup_orig / max(n_tc_tot, 1):.2f}%)  "
          f"corrected={n_dup_new} ({100.0 * n_dup_new / max(n_tc_tot, 1):.2f}%)  "
          f"removed(cross-seam-only)={n_removed}")
    print(f"[make_dup_corrected] corrected dups by slice: "
          f"chain-vs-chain={n_dup_new_chain}  pixel-vs-pixel={n_dup_new_pix}")
    print(f"[make_dup_corrected] wrote {args.out}  (DIAGNOSTIC: code_tag_data "
          "tagged '_dupfix')")


if __name__ == "__main__":
    main()
