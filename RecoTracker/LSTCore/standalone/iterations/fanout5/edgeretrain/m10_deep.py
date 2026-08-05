#!/usr/bin/env python3
"""m10_deep.py - (A) why sims are NOT formable, (B) purity of the accepted chain that
covers a sim (the S4->S5 harness-match failure). READ-ONLY."""
import sys
import time

import numpy as np
import uproot

from m10_pipe import build_edges, weld, extract_chains, t3_sim_pairs, SCRATCH, NT, BRANCHES
from m10_pipe2 import chain_arrays, k9


def main():
    nEvents = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    cache = np.load(f"{SCRATCH}/m10_edgecache_300.npz")
    LOG = cache["logit"]
    idx = np.load(f"{SCRATCH}/m10_edgeidx.npz")
    ekey = {(int(idx["lumi"][b]), int(idx["evt"][b])): b for b in range(len(idx["keys"]))}
    tree = uproot.open(NT)["tree"]
    recs = []
    t0 = time.time()
    for i in range(nEvents):
        a = tree.arrays(BRANCHES, entry_start=i, entry_stop=i + 1, library="np")
        lumi, evt = int(a["lumi"][0]), int(a["evt"][0])
        b = ekey[(lumi, evt)]
        lo, hi = int(idx["starts"][b]), int(idx["ends"][b])
        ls0, ls1 = a["ls_mdIdx0"][0].astype(np.int64), a["ls_mdIdx1"][0].astype(np.int64)
        t3l0, t3l1 = a["t3_lsIdx0"][0].astype(np.int64), a["t3_lsIdx1"][0].astype(np.int64)
        md_layer = a["md_layer"][0].astype(np.int32)
        nMD, nLS, nT3 = len(md_layer), len(ls0), len(t3l0)
        t3_md0, t3_md1, t3_md2 = ls0[t3l0], ls1[t3l0], ls1[t3l1]
        inner, outer, etype = build_edges(t3_md0, t3_md1, t3_md2, t3l0, t3l1, nMD, nLS)
        nE = len(inner)
        lg = LOG[lo:hi]
        p5 = a["t3_partOfPT5"][0].astype(bool)
        p3 = a["t3_partOfPT3"][0].astype(bool)
        outW, inW = weld(inner, outer, lg, nT3, theta=0.0)
        cON, cOE, cn, ce = extract_chains(inner, outer, outW, inW, nT3)
        nLayers, score, pixdrop, mdsets = chain_arrays(cn, ce, lg, t3_md0, t3_md1, t3_md2,
                                                       md_layer, p5, p3)
        thetaPass, cand, accepted = k9(nLayers, score, pixdrop, mdsets, nMD)

        mdsims = a["md_simIdxAll"][0]
        lens = np.array([len(x) for x in mdsims], dtype=np.int64)
        flat = (np.concatenate([np.asarray(x, dtype=np.int64) for x in mdsims])
                if nMD else np.empty(0, np.int64))
        off = np.zeros(nMD + 1, dtype=np.int64)
        np.cumsum(lens, out=off[1:])
        mdrep = np.repeat(np.arange(nMD, dtype=np.int64), lens)
        mdkey = np.unique((mdrep << 32) + flat)          # (md, sim) membership set
        t3s, sims = t3_sim_pairs(flat, off, t3_md0, t3_md1, t3_md2)
        nAcc = len(a["sim_pt"][0])
        pk = np.sort((t3s.astype(np.int64) << 32) + sims.astype(np.int64))
        cnt = np.bincount(t3s, minlength=nT3)
        noff = np.zeros(nT3 + 1, dtype=np.int64)
        np.cumsum(cnt, out=noff[1:])
        # (edge, sim) truth
        cin = cnt[inner].astype(np.int64)
        tot = int(cin.sum())
        erep = np.repeat(np.arange(nE, dtype=np.int64), cin)
        gidx = np.repeat(noff[inner], cin) + (np.arange(tot) - np.repeat(
            np.concatenate([[0], np.cumsum(cin)[:-1]]), cin))
        simv = pk[gidx] & ((1 << 32) - 1)
        want = (outer[erep].astype(np.int64) << 32) + simv
        pos = np.searchsorted(pk, want)
        ok = (pos < len(pk)) & (pk[np.minimum(pos, len(pk) - 1)] == want)
        te_edge, te_sim = erep[ok], simv[ok].astype(np.int32)
        m = te_sim < nAcc
        te_edge, te_sim = te_edge[m], te_sim[m]

        pt, eta = a["sim_pt"][0], a["sim_eta"][0]
        vxy = np.hypot(a["sim_vx"][0], a["sim_vy"][0])
        dxy = np.abs(a["sim_pca_dxy"][0])
        sel = np.nonzero((pt > 0.9) & (np.abs(eta) < 4.5))[0]
        # per-sim matched MDs / T3s
        msim = mdkey & ((1 << 32) - 1)
        mmd = mdkey >> 32
        omd = np.argsort(msim, kind="stable")
        msim_s, mmd_s = msim[omd], mmd[omd]
        tsim_o = np.argsort(sims, kind="stable")
        sims_s, t3s_s = sims[tsim_o], t3s[tsim_o]
        esim_o = np.argsort(te_sim, kind="stable")
        esim_s, eedge_s = te_sim[esim_o], te_edge[esim_o]

        for s in sel:
            l0, l1 = np.searchsorted(msim_s, [s, s + 1])
            mds = mmd_s[l0:l1]
            nMDm = len(mds)
            nLayM = np.unique(md_layer[mds]).size if nMDm else 0
            l0, l1 = np.searchsorted(sims_s, [s, s + 1])
            t3m = t3s_s[l0:l1]
            l0, l1 = np.searchsorted(esim_s, [s, s + 1])
            es = eedge_s[l0:l1]
            # best accepted covering chain by MD purity, and by score
            bestPur, bestPurAcc, bestNL, bestNMD, bestNMDm = -1.0, -1.0, 0, 0, 0
            cs = np.unique(cOE[es][cOE[es] >= 0]) if len(es) else np.array([], np.int32)
            for c in cs:
                mdc = mdsets[c]
                key = (mdc.astype(np.int64) << 32) + s
                p = np.searchsorted(mdkey, key)
                hitm = (p < len(mdkey)) & (mdkey[np.minimum(p, len(mdkey) - 1)] == key)
                pur = hitm.mean()
                if pur > bestPur:
                    bestPur, bestNL, bestNMD, bestNMDm = pur, int(nLayers[c]), len(mdc), int(hitm.sum())
                if accepted[c] and pur > bestPurAcc:
                    bestPurAcc = pur
            recs.append((lumi, evt, int(s), float(pt[s]), float(eta[s]), float(vxy[s]),
                         float(dxy[s]), nMDm, nLayM, len(t3m), len(es),
                         len(cs), float(bestPur), float(bestPurAcc), bestNL, bestNMD, bestNMDm,
                         int(accepted[cs].sum()) if len(cs) else 0))
        if i % 25 == 0:
            print(f"  evt {i} | {time.time()-t0:.0f}s", flush=True)
    dt = np.dtype([("lumi", "i4"), ("evt", "i8"), ("sim", "i4"), ("pt", "f4"), ("eta", "f4"),
                   ("vxy", "f4"), ("dxy", "f4"), ("nMDmatch", "i4"), ("nLayMatch", "i4"),
                   ("nT3match", "i4"), ("nTrueEdge", "i4"), ("nChain", "i4"),
                   ("bestPur", "f4"), ("bestPurAcc", "f4"), ("bestNL", "i4"),
                   ("bestNMD", "i4"), ("bestNMDm", "i4"), ("nAcc", "i4")])
    arr = np.array(recs, dtype=dt)
    np.save(f"{SCRATCH}/m10_deep.npy", arr)
    print(f"done: {len(arr)} rows, {time.time()-t0:.0f}s")


if __name__ == "__main__":
    main()
