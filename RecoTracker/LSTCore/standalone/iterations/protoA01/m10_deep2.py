#!/usr/bin/env python3
"""m10_deep2.py - displaced-only deep dive: per covering-chain acceptance state and
purity (with the k10 nLayers>=4 TC filter), plus the "2 T3s but no edge" near-miss test
(would an anchor-hit-level E1 relation connect them?). READ-ONLY."""
import sys
import time

import numpy as np
import uproot

from m10_pipe import build_edges, weld, extract_chains, t3_sim_pairs, SCRATCH, NT
from m10_pipe2 import chain_arrays, k9

BR = ["ls_mdIdx0", "ls_mdIdx1", "t3_lsIdx0", "t3_lsIdx1", "md_layer", "md_simIdxAll",
      "t3_partOfPT5", "t3_partOfPT3", "sim_pt", "sim_eta", "sim_vx", "sim_vy", "sim_vz",
      "sim_q", "sim_pca_dxy", "evt", "lumi", "md_anchorHitIdx", "md_detId"]


def main():
    nEvents = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    LOG = np.load(f"{SCRATCH}/m10_edgecache_300.npz")["logit"]
    idx = np.load(f"{SCRATCH}/m10_edgeidx.npz")
    ekey = {(int(idx["lumi"][b]), int(idx["evt"][b])): b for b in range(len(idx["keys"]))}
    tree = uproot.open(NT)["tree"]
    recs, near = [], []
    t0 = time.time()
    for i in range(nEvents):
        a = tree.arrays(BR, entry_start=i, entry_stop=i + 1, library="np")
        lumi, evt = int(a["lumi"][0]), int(a["evt"][0])
        b = ekey[(lumi, evt)]
        lo, hi = int(idx["starts"][b]), int(idx["ends"][b])
        ls0, ls1 = a["ls_mdIdx0"][0].astype(np.int64), a["ls_mdIdx1"][0].astype(np.int64)
        t3l0, t3l1 = a["t3_lsIdx0"][0].astype(np.int64), a["t3_lsIdx1"][0].astype(np.int64)
        md_layer = a["md_layer"][0].astype(np.int32)
        anch = a["md_anchorHitIdx"][0].astype(np.int64)
        nMD, nT3 = len(md_layer), len(t3l0)
        t3_md0, t3_md1, t3_md2 = ls0[t3l0], ls1[t3l0], ls1[t3l1]
        inner, outer, etype = build_edges(t3_md0, t3_md1, t3_md2, t3l0, t3l1, nMD, len(ls0))
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
        mdkey = np.unique((mdrep << 32) + flat)
        t3s, sims = t3_sim_pairs(flat, off, t3_md0, t3_md1, t3_md2)
        nAcc = len(a["sim_pt"][0])
        pk = np.sort((t3s.astype(np.int64) << 32) + sims.astype(np.int64))
        cnt = np.bincount(t3s, minlength=nT3)
        noff = np.zeros(nT3 + 1, dtype=np.int64)
        np.cumsum(cnt, out=noff[1:])
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
        vz, q = a["sim_vz"][0], a["sim_q"][0]
        sel = np.nonzero((pt > 0.9) & (np.abs(eta) < 4.5) & (np.abs(vz) < 30) & (q != 0)
                         & (vxy >= 1.0))[0]
        so = np.argsort(sims, kind="stable"); sims_s, t3s_s = sims[so], t3s[so]
        eo = np.argsort(te_sim, kind="stable"); esim_s, eedge_s = te_sim[eo], te_edge[eo]
        for s in sel:
            l0, l1 = np.searchsorted(sims_s, [s, s + 1])
            t3m = t3s_s[l0:l1]
            l0, l1 = np.searchsorted(esim_s, [s, s + 1])
            es = eedge_s[l0:l1]
            if len(es) == 0:
                # near-miss test: >=2 matched T3s, no edge -> would anchor-hit-level E1 fire?
                if len(t3m) >= 2:
                    ah2 = anch[t3_md2[t3m]]
                    ah0 = anch[t3_md0[t3m]]
                    shareAnchor = len(np.intersect1d(ah2, ah0)) > 0
                    lay2 = md_layer[t3_md2[t3m]]
                    lay0 = md_layer[t3_md0[t3m]]
                    adj = np.any(np.abs(lay2[:, None] - lay0[None, :]) == 1)
                    near.append((lumi, evt, int(s), float(vxy[s]), float(pt[s]), len(t3m),
                                 int(shareAnchor), int(adj),
                                 int(np.unique(md_layer[np.unique(np.concatenate(
                                     [t3_md0[t3m], t3_md1[t3m], t3_md2[t3m]]))]).size)))
                continue
            cs = np.unique(cOE[es][cOE[es] >= 0])
            best = dict(pur=-1.0, nl=0, acc=0, pix=0, th=0, cl=0, nmd=0)
            bestTC = dict(pur=-1.0, nl=0)          # accepted AND nLayers>=4
            for c in cs:
                mdc = mdsets[c]
                key = (mdc.astype(np.int64) << 32) + s
                p = np.searchsorted(mdkey, key)
                hm = (p < len(mdkey)) & (mdkey[np.minimum(p, len(mdkey) - 1)] == key)
                pur = float(hm.mean())
                if nLayers[c] >= 4 and pur > best["pur"]:
                    best = dict(pur=pur, nl=int(nLayers[c]), acc=int(accepted[c]),
                                pix=int(pixdrop[c]), th=int(not thetaPass[c]),
                                cl=int(thetaPass[c] and not pixdrop[c] and not accepted[c]),
                                nmd=len(mdc))
                if accepted[c] and nLayers[c] >= 4 and pur > bestTC["pur"]:
                    bestTC = dict(pur=pur, nl=int(nLayers[c]))
            recs.append((lumi, evt, int(s), float(vxy[s]), float(pt[s]), len(cs),
                         best["pur"], best["nl"], best["acc"], best["pix"], best["th"],
                         best["cl"], best["nmd"], bestTC["pur"], bestTC["nl"]))
        if i % 40 == 0:
            print(f"  evt {i} | {time.time()-t0:.0f}s", flush=True)
    np.save(f"{SCRATCH}/m10_disp.npy", np.array(recs, dtype=np.dtype(
        [("lumi", "i4"), ("evt", "i8"), ("sim", "i4"), ("vxy", "f4"), ("pt", "f4"),
         ("nCov", "i4"), ("bestPur", "f4"), ("bestNL", "i4"), ("bestAcc", "i4"),
         ("bestPix", "i4"), ("bestTh", "i4"), ("bestClaim", "i4"), ("bestNMD", "i4"),
         ("accPur", "f4"), ("accNL", "i4")])))
    np.save(f"{SCRATCH}/m10_near.npy", np.array(near, dtype=np.dtype(
        [("lumi", "i4"), ("evt", "i8"), ("sim", "i4"), ("vxy", "f4"), ("pt", "f4"),
         ("nT3", "i4"), ("shareAnchor", "i4"), ("layerAdj", "i4"), ("nLay", "i4")])))
    print(f"done: {len(recs)} displaced formable rows, {len(near)} near-miss rows, "
          f"{time.time()-t0:.0f}s")


if __name__ == "__main__":
    main()
