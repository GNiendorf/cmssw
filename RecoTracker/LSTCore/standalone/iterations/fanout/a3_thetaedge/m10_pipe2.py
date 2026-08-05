#!/usr/bin/env python3
"""m10_pipe2.py - full per-sim funnel + weld-failure forensics + theta what-ifs.

READ-ONLY. Exact numpy replica of K1/K2 (Stages.cc), K6 (K6Weld.cc), K9 (K9K10.cc)
validated against the binary (evt0 chains=2284 == ab_m8_h4b.log). Truth from
md_simIdxAll per Labels.cc (>=2 of 3 MDs), crediting EVERY accepted sim in the
intersection (the oracle-mode convention, not the single-simIdx dump column).

Outputs (scratchpad):
  m10_funnel2.npy   one row per accepted sim, pt>0.9 |eta|<4.5
  m10_weldfail.npy  one row per (sim, true passing but unwelded edge) for displaced sims
  m10_evt2.npy      per-event counters incl. theta=-1/-2 what-ifs
"""
import sys
import time

import numpy as np
import uproot

from m10_pipe import (csr, cross_pairs, build_edges, weld, extract_chains, t3_sim_pairs,
                      SA, SCRATCH, NT, BRANCHES, LAMBDA_LEN, MAXCLAIMED)


def chain_arrays(cn, ce, lg, t3_md0, t3_md1, t3_md2, md_layer, p5, p3):
    nC = len(cn)
    nLayers = np.zeros(nC, dtype=np.int32)
    score = np.zeros(nC, dtype=np.float32)
    pixdrop = np.zeros(nC, dtype=bool)
    mdsets = []
    for c in range(nC):
        nodes = np.asarray(cn[c], dtype=np.int64)
        mds = np.unique(np.concatenate([t3_md0[nodes], t3_md1[nodes], t3_md2[nodes]]))
        mdsets.append(mds)
        nLayers[c] = np.unique(md_layer[mds]).size
        score[c] = lg[ce[c]].sum() + LAMBDA_LEN * nLayers[c]
        pixdrop[c] = p5[nodes].any() or p3[nodes].any()
    return nLayers, score, pixdrop, mdsets


def k9(nLayers, score, pixdrop, mdsets, nMD, t4pass=None):
    """Legacy branch of -G 2: thetaChain5/6 = 0 on the sum-logit scale.
    nLayers<=4 chains are gate-scored in the binary; t4pass (bool array or None)
    supplies their decision, None = treat as passing (upper bound)."""
    thetaPass = score >= 0.0
    small = nLayers <= 4
    if t4pass is None:
        thetaPass = np.where(small, True, thetaPass)
    else:
        thetaPass = np.where(small, t4pass, thetaPass)
    cand = thetaPass & (~pixdrop)
    order = np.lexsort((np.arange(len(score)), -score))
    order = order[cand[order]]
    claimed = np.zeros(nMD, dtype=bool)
    accepted = np.zeros(len(score), dtype=bool)
    for c in order:
        mds = mdsets[c]
        if len(mds) and claimed[mds].mean() > MAXCLAIMED:
            continue
        claimed[mds] = True
        accepted[c] = True
    return thetaPass, cand, accepted


def main():
    nEvents = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    cache = np.load(f"{SCRATCH}/m10_edgecache_300.npz")
    LOG = cache["logit"]
    idx = np.load(f"{SCRATCH}/m10_edgeidx.npz")
    ekey = {(int(idx["lumi"][b]), int(idx["evt"][b])): b for b in range(len(idx["keys"]))}
    tree = uproot.open(NT)["tree"]

    recs, wf, evrecs = [], [], []
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
        assert nE == hi - lo
        lg = LOG[lo:hi]
        p5 = a["t3_partOfPT5"][0].astype(bool)
        p3 = a["t3_partOfPT3"][0].astype(bool)

        outW, inW = weld(inner, outer, lg, nT3, theta=0.0)
        cON, cOE, cn, ce = extract_chains(inner, outer, outW, inW, nT3)
        nLayers, score, pixdrop, mdsets = chain_arrays(cn, ce, lg, t3_md0, t3_md1, t3_md2,
                                                       md_layer, p5, p3)
        thetaPass, cand, accepted = k9(nLayers, score, pixdrop, mdsets, nMD)

        # what-if welds at looser thetaEdge
        alt = {}
        for th in (-1.0, -2.0):
            oW, iW = weld(inner, outer, lg, nT3, theta=th)
            _, cOEa, cna, cea = extract_chains(inner, outer, oW, iW, nT3)
            nLa, sca, pda, mdsa = chain_arrays(cna, cea, lg, t3_md0, t3_md1, t3_md2,
                                               md_layer, p5, p3)
            tpa, cda, acca = k9(nLa, sca, pda, mdsa, nMD)
            alt[th] = (cOEa, nLa, sca, pda, acca, len(cna))

        # ---- truth ----
        mdsims = a["md_simIdxAll"][0]
        flat = (np.concatenate([np.asarray(x, dtype=np.int64) for x in mdsims])
                if nMD else np.empty(0, np.int64))
        off = np.zeros(nMD + 1, dtype=np.int64)
        np.cumsum([len(x) for x in mdsims], out=off[1:])
        t3s, sims = t3_sim_pairs(flat, off, t3_md0, t3_md1, t3_md2)
        nAcc = len(a["sim_pt"][0])
        pk = (t3s.astype(np.int64) << 32) + sims.astype(np.int64)
        pk.sort()
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
        trueAny = np.zeros(nE, dtype=bool)     # true for SOME accepted sim
        trueAny[te_edge] = True

        pt, eta = a["sim_pt"][0], a["sim_eta"][0]
        vxy = np.hypot(a["sim_vx"][0], a["sim_vy"][0])
        dxy = np.abs(a["sim_pca_dxy"][0])
        pdg = a["sim_pdgId"][0]
        sel = (pt > 0.9) & (np.abs(eta) < 4.5)

        o2 = np.argsort(te_sim, kind="stable")
        ts, tee = te_sim[o2], te_edge[o2]
        bnd = np.concatenate([[0], np.nonzero(np.diff(ts))[0] + 1, [len(ts)]]) if len(ts) else [0]
        for k in range(len(bnd) - 1):
            s = int(ts[bnd[k]])
            if not sel[s]:
                continue
            es = tee[bnd[k]:bnd[k + 1]]
            lgs = lg[es]
            passing = es[lgs >= 0]
            wmask = cOE[es] >= 0
            cs = np.unique(cOE[es][wmask])
            nl = nLayers[cs] if len(cs) else np.array([0], np.int32)
            srt = np.sort(lgs)[::-1]
            rec = [lumi, evt, s, float(pt[s]), float(eta[s]), float(vxy[s]), float(dxy[s]),
                   int(pdg[s]), len(es), int((lgs >= 0).sum()),
                   int(((lgs < 0) & (lgs >= -1)).sum()), int(((lgs < -1) & (lgs >= -2)).sum()),
                   float(srt[0]), float(srt[1]) if len(srt) > 1 else -99.0,
                   int(wmask.sum()), len(cs), int(nl.max()),
                   int(accepted[cs].sum()) if len(cs) else 0,
                   int((nLayers[cs] <= 4).sum()) if len(cs) else 0,
                   int((~thetaPass[cs]).sum()) if len(cs) else 0,
                   int((thetaPass[cs] & pixdrop[cs]).sum()) if len(cs) else 0,
                   int((thetaPass[cs] & ~pixdrop[cs] & ~accepted[cs]).sum()) if len(cs) else 0,
                   float(score[cs].max()) if len(cs) else -999.0]
            for th in (-1.0, -2.0):
                cOEa, nLa, sca, pda, acca, _ = alt[th]
                wa = cOEa[es] >= 0
                ca = np.unique(cOEa[es][wa])
                rec += [int(wa.sum()), len(ca),
                        int(nLa[ca].max()) if len(ca) else 0,
                        int(acca[ca].sum()) if len(ca) else 0]
            recs.append(tuple(rec))

            # weld-failure forensics: displaced sims with passing true edges, none welded
            if vxy[s] >= 1.0 and len(passing) and wmask.sum() == 0:
                for e in passing:
                    u, v = int(inner[e]), int(outer[e])
                    eo, ei = int(outW[u]), int(inW[v])
                    def info(x):
                        if x < 0:
                            return (-99.0, -1)
                        return (float(lg[x]), int(trueAny[x]))
                    lo_, to_ = info(eo)
                    li_, ti_ = info(ei)
                    wf.append((lumi, evt, s, float(vxy[s]), float(pt[s]), float(lg[e]),
                               int(etype[e]), int(eo != e), int(ei != e),
                               int(eo >= 0), int(ei >= 0), lo_, to_, li_, ti_))

        evrecs.append((lumi, evt, nT3, nE, int((lg >= 0).sum()), len(cn), int(cand.sum()),
                       int(accepted.sum()), alt[-1.0][5], int(alt[-1.0][4].sum()),
                       alt[-2.0][5], int(alt[-2.0][4].sum())))
        if i % 25 == 0:
            print(f"  evt {i}: nT3={nT3} E={nE} chains={len(cn)} acc={int(accepted.sum())} "
                  f"| {time.time()-t0:.0f}s", flush=True)

    names = ["lumi", "evt", "sim", "pt", "eta", "vxy", "dxy", "pdg", "nTrue", "nPass",
             "nIn_m1_0", "nIn_m2_m1", "maxL", "max2L", "nWeld", "nCh", "maxNL", "nAcc",
             "nT4cls", "nThetaKill", "nPixdrop", "nClaimLost", "maxScore",
             "nWeld_m1", "nCh_m1", "maxNL_m1", "nAcc_m1",
             "nWeld_m2", "nCh_m2", "maxNL_m2", "nAcc_m2"]
    fmt = ["i4", "i8", "i4", "f4", "f4", "f4", "f4", "i4", "i4", "i4", "i4", "i4",
           "f4", "f4", "i4", "i4", "i4", "i4", "i4", "i4", "i4", "i4", "f4",
           "i4", "i4", "i4", "i4", "i4", "i4", "i4", "i4"]
    arr = np.array(recs, dtype=np.dtype(list(zip(names, fmt))))
    np.save(f"{SCRATCH}/m10_funnel2.npy", arr)
    wfa = np.array(wf, dtype=np.dtype([("lumi", "i4"), ("evt", "i8"), ("sim", "i4"),
                                       ("vxy", "f4"), ("pt", "f4"), ("logit", "f4"),
                                       ("etype", "i4"), ("lostOut", "i4"), ("lostIn", "i4"),
                                       ("outTaken", "i4"), ("inTaken", "i4"),
                                       ("outCompL", "f4"), ("outCompTrue", "i4"),
                                       ("inCompL", "f4"), ("inCompTrue", "i4")]))
    np.save(f"{SCRATCH}/m10_weldfail.npy", wfa)
    ev = np.array(evrecs, dtype=np.dtype([("lumi", "i4"), ("evt", "i8"), ("nT3", "i4"),
                                          ("nE", "i8"), ("nPass", "i8"), ("nChains", "i4"),
                                          ("nCand", "i4"), ("nAcc", "i4"),
                                          ("nChains_m1", "i4"), ("nAcc_m1", "i4"),
                                          ("nChains_m2", "i4"), ("nAcc_m2", "i4")]))
    np.save(f"{SCRATCH}/m10_evt2.npy", ev)
    print(f"done {nEvents} evts: {len(arr)} sim rows, {len(wfa)} weldfail rows, "
          f"{time.time()-t0:.0f}s")


if __name__ == "__main__":
    main()
