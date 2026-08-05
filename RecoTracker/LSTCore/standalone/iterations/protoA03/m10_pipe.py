#!/usr/bin/env python3
"""m10_pipe.py - exact numpy replica of the chain pipeline K1/K2/K6/K9 (READ-ONLY).

Reproduces, per event, from the LST ntuple + the cached v3 edge logits:
  K1/K2 edge enumeration (same order as Stages.cc, verified against the edge dump),
  K6 mutual-best welding (K6Weld.cc), chain nLayers/score, T3SimSets + per-(edge,sim)
  truth (Labels.cc), and the K9 theta/pixdrop/claim funnel (K9K10.cc).
Emits a per-sim funnel record for every accepted sim with pt>0.9 |eta|<4.5.

Chain-gate caveat: the m8_h4b anchor runs -G 2 (chain-gate logit for nLayers<=4 chains,
legacy sum-logit for >=5). The legacy branch is replicated exactly; nLayers<=4 chains are
flagged and their gate decision is taken from an optional chain-gate score (computed
separately) or reported as UNKNOWN.
"""
import sys
import time

import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SCRATCH = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad"
NT = f"{SA}/LSTNtuple_PU200RelVal_300evt.root"

KWELD = 3
LAMBDA_LEN = 0.5
THETA_EDGE = 0.0
MAXCLAIMED = 0.3

BRANCHES = ["ls_mdIdx0", "ls_mdIdx1", "t3_lsIdx0", "t3_lsIdx1", "md_layer", "md_simIdxAll",
            "t3_partOfPT5", "t3_partOfPT3", "sim_pt", "sim_eta", "sim_vx", "sim_vy",
            "sim_pca_dxy", "sim_pdgId", "evt", "lumi", "t3_pt"]


def csr(keys, nkeys):
    """buildCsr: items sorted by key, ties in ascending item index (stable)."""
    order = np.argsort(keys, kind="stable").astype(np.int32)
    counts = np.bincount(keys, minlength=nkeys)
    off = np.zeros(nkeys + 1, dtype=np.int64)
    np.cumsum(counts, out=off[1:])
    return off, order


def cross_pairs(offA, itemsA, offB, itemsB):
    """For every key k emit (itemsA[i], itemsB[j]) with i outer-loop, j inner-loop --
    exactly the C++ nested loop order."""
    nin = np.diff(offA)
    nout = np.diff(offB)
    npair = nin * nout
    tot = int(npair.sum())
    if tot == 0:
        return np.empty(0, np.int32), np.empty(0, np.int32)
    pstart = np.zeros(len(npair) + 1, dtype=np.int64)
    np.cumsum(npair, out=pstart[1:])
    e = np.arange(tot, dtype=np.int64)
    k = np.searchsorted(pstart, e, side="right") - 1
    l = e - pstart[k]
    no = nout[k]
    i_loc = l // no
    j_loc = l - i_loc * no
    inner = itemsA[offA[k] + i_loc]
    outer = itemsB[offB[k] + j_loc]
    return inner, outer


def build_edges(t3_md0, t3_md1, t3_md2, t3_ls0, t3_ls1, nMD, nLS):
    offIn, itIn = csr(t3_md2, nMD)      # T3s whose last MD is m
    offOut, itOut = csr(t3_md0, nMD)    # T3s whose first MD is m
    i1, o1 = cross_pairs(offIn, itIn, offOut, itOut)
    keep = i1 != o1
    i1, o1 = i1[keep], o1[keep]
    lIn, litIn = csr(t3_ls1, nLS)
    lOut, litOut = csr(t3_ls0, nLS)
    i2, o2 = cross_pairs(lIn, litIn, lOut, litOut)
    keep = (i2 != o2) & (t3_md2[i2] != t3_md0[o2])
    i2, o2 = i2[keep], o2[keep]
    inner = np.concatenate([i1, i2]).astype(np.int32)
    outer = np.concatenate([o1, o2]).astype(np.int32)
    etype = np.concatenate([np.ones(len(i1), np.int8), 2 * np.ones(len(i2), np.int8)])
    return inner, outer, etype


def weld(inner, outer, logit, nNodes, theta=THETA_EDGE, sweeps=KWELD):
    outW = np.full(nNodes, -1, dtype=np.int32)
    inW = np.full(nNodes, -1, dtype=np.int32)
    elig0 = logit >= theta
    nE = len(logit)
    order = np.lexsort((np.arange(nE), -logit))
    for _ in range(sweeps):
        free = elig0 & (outW[inner] == -1) & (inW[outer] == -1)
        idx = order[free[order]]
        if idx.size == 0:
            break
        bestOut = np.full(nNodes, -1, dtype=np.int32)
        bestOut[inner[idx][::-1]] = idx[::-1]
        bestIn = np.full(nNodes, -1, dtype=np.int32)
        bestIn[outer[idx][::-1]] = idx[::-1]
        cand = bestOut[bestOut >= 0]
        mut = cand[bestIn[outer[cand]] == cand]
        if mut.size == 0:
            break
        outW[inner[mut]] = mut
        inW[outer[mut]] = mut
    return outW, inW


def extract_chains(inner, outer, outW, inW, nNodes):
    """Return chainOfNode (-1 if none), list-of-node-lists, list-of-edge-lists."""
    heads = np.nonzero((inW == -1) & (outW != -1))[0]
    chainOfNode = np.full(nNodes, -1, dtype=np.int32)
    chainOfEdge = np.full(len(inner), -1, dtype=np.int32)
    nodes, edges = [], []
    for c, h in enumerate(heads):
        ns = [int(h)]
        es = []
        n = int(h)
        while outW[n] != -1:
            e = int(outW[n])
            es.append(e)
            n = int(outer[e])
            ns.append(n)
        for x in ns:
            chainOfNode[x] = c
        for x in es:
            chainOfEdge[x] = c
        nodes.append(ns)
        edges.append(es)
    return chainOfNode, chainOfEdge, nodes, edges


def t3_sim_pairs(md_sims_flat, md_sims_off, t3_md0, t3_md1, t3_md2):
    """Return (t3, sim) pairs where sim appears in >=2 of the T3's 3 MDs (per-MD deduped)."""
    nT3 = len(t3_md0)
    rows, vals = [], []
    for mds in (t3_md0, t3_md1, t3_md2):
        cnt = (md_sims_off[mds + 1] - md_sims_off[mds]).astype(np.int64)
        t3rep = np.repeat(np.arange(nT3, dtype=np.int64), cnt)
        # gather the flat slices
        starts = md_sims_off[mds]
        idx = np.repeat(starts, cnt) + (np.arange(cnt.sum()) - np.repeat(
            np.concatenate([[0], np.cumsum(cnt)[:-1]]), cnt))
        v = md_sims_flat[idx]
        # per-MD dedup: unique (md, sim) -> here unique (t3, sim) within this slot
        key = t3rep * (1 << 24) + v
        key = np.unique(key)
        rows.append(key >> 24)
        vals.append(key & ((1 << 24) - 1))
    key = np.concatenate([r * (1 << 24) + v for r, v in zip(rows, vals)])
    key.sort()
    dup = key[:-1][key[:-1] == key[1:]]
    dup = np.unique(dup)
    return (dup >> 24).astype(np.int32), (dup & ((1 << 24) - 1)).astype(np.int32)


def main():
    nEvents = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    cache = np.load(f"{SCRATCH}/m10_edgecache_300.npz")
    LOG = cache["logit"]
    DLAB = cache["label"]
    idx = np.load(f"{SCRATCH}/m10_edgeidx.npz")
    ekey = {(int(idx["lumi"][b]), int(idx["evt"][b])): b for b in range(len(idx["keys"]))}

    tree = uproot.open(NT)["tree"]
    recs = []
    chainrecs = []
    t0 = time.time()
    nchk = 0
    for i in range(nEvents):
        a = tree.arrays(BRANCHES, entry_start=i, entry_stop=i + 1, library="np")
        lumi, evt = int(a["lumi"][0]), int(a["evt"][0])
        b = ekey[(lumi, evt)]
        lo, hi = int(idx["starts"][b]), int(idx["ends"][b])
        ls0, ls1 = a["ls_mdIdx0"][0].astype(np.int64), a["ls_mdIdx1"][0].astype(np.int64)
        t3l0, t3l1 = a["t3_lsIdx0"][0].astype(np.int64), a["t3_lsIdx1"][0].astype(np.int64)
        md_layer = a["md_layer"][0].astype(np.int32)
        nMD, nLS, nT3 = len(md_layer), len(ls0), len(t3l0)
        t3_md0 = ls0[t3l0]
        t3_md1 = ls1[t3l0]
        t3_md2 = ls1[t3l1]
        inner, outer, etype = build_edges(t3_md0, t3_md1, t3_md2, t3l0, t3l1, nMD, nLS)
        nE = len(inner)
        assert nE == hi - lo, f"evt {evt}: edges {nE} != dump {hi-lo}"
        if nchk < 3:
            assert np.array_equal(etype, cache["etype"][lo:hi]), "etype order mismatch"
            nchk += 1
        lg = LOG[lo:hi]
        outW, inW = weld(inner, outer, lg, nT3)
        chainOfNode, chainOfEdge, cn, ce = extract_chains(inner, outer, outW, inW, nT3)
        nC = len(cn)

        # chain nLayers / score / MD sets
        nLayers = np.zeros(nC, dtype=np.int32)
        score = np.zeros(nC, dtype=np.float64)
        mdsets = []
        pixdrop = np.zeros(nC, dtype=bool)
        p5 = a["t3_partOfPT5"][0].astype(bool)
        p3 = a["t3_partOfPT3"][0].astype(bool)
        for c in range(nC):
            nodes = np.array(cn[c], dtype=np.int64)
            mds = np.unique(np.concatenate([t3_md0[nodes], t3_md1[nodes], t3_md2[nodes]]))
            mdsets.append(mds)
            nLayers[c] = len(np.unique(md_layer[mds]))
            score[c] = lg[ce[c]].sum() + LAMBDA_LEN * nLayers[c]
            pixdrop[c] = p5[nodes].any() or p3[nodes].any()

        # ---- truth ----
        mdsims = a["md_simIdxAll"][0]
        flat = np.concatenate([np.asarray(x, dtype=np.int64) for x in mdsims]) if nMD else np.empty(0, np.int64)
        off = np.zeros(nMD + 1, dtype=np.int64)
        np.cumsum([len(x) for x in mdsims], out=off[1:])
        t3s, sims = t3_sim_pairs(flat, off, t3_md0, t3_md1, t3_md2)
        nAcc = len(a["sim_pt"][0])
        # per-(edge,sim) truth: join inner's sims with outer's sim-set membership
        pk = (t3s.astype(np.int64) << 32) + sims.astype(np.int64)
        ordr = np.argsort(pk)
        nodekey = pk[ordr]
        cnt = np.bincount(t3s, minlength=nT3)
        noff = np.zeros(nT3 + 1, dtype=np.int64)
        np.cumsum(cnt, out=noff[1:])
        # expand edges by inner sims
        cin = cnt[inner].astype(np.int64)
        erep = np.repeat(np.arange(nE, dtype=np.int64), cin)
        starts = noff[inner]
        gidx = np.repeat(starts, cin) + (np.arange(cin.sum()) - np.repeat(
            np.concatenate([[0], np.cumsum(cin)[:-1]]), cin))
        simv = (nodekey[gidx] & ((1 << 32) - 1)).astype(np.int64)
        want = (outer[erep].astype(np.int64) << 32) + simv
        pos = np.searchsorted(nodekey, want)
        ok = (pos < len(nodekey)) & (nodekey[np.minimum(pos, len(nodekey) - 1)] == want)
        te_edge = erep[ok]
        te_sim = simv[ok].astype(np.int32)
        # restrict to accepted sims
        m = te_sim < nAcc
        te_edge, te_sim = te_edge[m], te_sim[m]

        # ---- per-sim funnel ----
        pt = a["sim_pt"][0]
        eta = a["sim_eta"][0]
        vxy = np.hypot(a["sim_vx"][0], a["sim_vy"][0])
        dxy = np.abs(a["sim_pca_dxy"][0])
        pdg = a["sim_pdgId"][0]
        sel = (pt > 0.9) & (np.abs(eta) < 4.5)

        # aggregate per sim
        ordr2 = np.argsort(te_sim, kind="stable")
        ts, te = te_sim[ordr2], te_edge[ordr2]
        bnd = np.concatenate([[0], np.nonzero(np.diff(ts))[0] + 1, [len(ts)]])
        # K9 candidate/accept sets (legacy branch only; nLayers<=4 flagged)
        gateNeeded = nLayers <= 4
        thetaPass = np.where(gateNeeded, True, score >= 0.0)  # T5=T6=0; T4 handled by gate
        cand = thetaPass & (~pixdrop)
        # greedy claim
        order = np.lexsort((np.arange(nC), -score))
        order = order[cand[order]]
        claimed = np.zeros(nMD, dtype=bool)
        accepted = np.zeros(nC, dtype=bool)
        for c in order:
            mds = mdsets[c]
            f = claimed[mds].mean() if len(mds) else 0.0
            if f > MAXCLAIMED:
                continue
            claimed[mds] = True
            accepted[c] = True

        for k in range(len(bnd) - 1):
            s = int(ts[bnd[k]])
            if not sel[s]:
                continue
            es = te[bnd[k]:bnd[k + 1]]
            lgs = lg[es]
            welded = chainOfEdge[es] >= 0
            cs = np.unique(chainOfEdge[es][welded])
            nl = nLayers[cs] if len(cs) else np.array([], np.int32)
            recs.append((lumi, evt, s, float(pt[s]), float(eta[s]), float(vxy[s]), float(dxy[s]),
                         int(pdg[s]),
                         len(es), int((lgs >= 0).sum()), float(lgs.max()),
                         float(np.sort(lgs)[-2]) if len(lgs) > 1 else float(lgs.max()),
                         int(welded.sum()), len(cs),
                         int(nl.max()) if len(nl) else 0,
                         int(accepted[cs].sum()) if len(cs) else 0,
                         int((gateNeeded[cs] & (~pixdrop[cs])).sum()) if len(cs) else 0,
                         int((~thetaPass[cs]).sum()) if len(cs) else 0,
                         int((thetaPass[cs] & pixdrop[cs]).sum()) if len(cs) else 0,
                         int((thetaPass[cs] & (~pixdrop[cs]) & (~accepted[cs]) & (~gateNeeded[cs])).sum()) if len(cs) else 0,
                         float(score[cs].max()) if len(cs) else -999.0))
        # per-event chain bookkeeping
        chainrecs.append((lumi, evt, nT3, nE, int((lg >= 0).sum()), nC,
                          int(cand.sum()), int(accepted.sum()), int((DLAB[lo:hi] == 1).sum())))
        if i % 20 == 0:
            print(f"  evt {i} (lumi {lumi} evt {evt}): nT3={nT3} edges={nE} chains={nC} "
                  f"cand={int(cand.sum())} accepted={int(accepted.sum())}  {time.time()-t0:.1f}s",
                  flush=True)

    dt = np.dtype([("lumi", "i4"), ("evt", "i8"), ("sim", "i4"), ("pt", "f4"), ("eta", "f4"),
                   ("vxy", "f4"), ("dxy", "f4"), ("pdg", "i4"),
                   ("nTrueEdge", "i4"), ("nTruePass", "i4"), ("maxTrueLogit", "f4"),
                   ("secondTrueLogit", "f4"), ("nTrueWelded", "i4"), ("nChains", "i4"),
                   ("maxNLayers", "i4"), ("nAccepted", "i4"), ("nGateUnknown", "i4"),
                   ("nThetaKill", "i4"), ("nPixdrop", "i4"), ("nClaimLost", "i4"),
                   ("maxScore", "f4")])
    arr = np.array(recs, dtype=dt)
    np.save(f"{SCRATCH}/m10_funnel.npy", arr)
    ev = np.array(chainrecs, dtype=[("lumi", "i4"), ("evt", "i8"), ("nT3", "i4"), ("nE", "i8"),
                                    ("nPass", "i8"), ("nChains", "i4"), ("nCand", "i4"),
                                    ("nAcc", "i4"), ("nTrue", "i8")])
    np.save(f"{SCRATCH}/m10_evt.npy", ev)
    print(f"done {nEvents} events, {len(arr)} sim records, {time.time()-t0:.1f}s")


if __name__ == "__main__":
    main()
