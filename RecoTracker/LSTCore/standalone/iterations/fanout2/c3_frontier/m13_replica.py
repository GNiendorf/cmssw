#!/usr/bin/env python3
"""m13_replica.py -- READ-ONLY numpy replica of the m12_w7 pipeline through K9/K10.

Reproduces, per event, the ACCEPTED chain TCs of ab_m12_w7 together with the per-chain
gate quantities the A/B ntuple does not carry (3-class margins mP/mD/mX, the 2-class a2
gate logit used by the -B order key, dcaXY, legacy score, nLayers).

Inputs (all pre-existing, none modified):
  - LSTNtuple_PU200RelVal_300evt.root      (T3/LS/MD topology, t3 kinematics, pix flags)
  - scratchpad m10_edgecache_300.npz       (v3 edge logits per edge dump row) + m10_edgeidx
  - prototype/chains_m12_300evt.root       (25 ChainFeatures + dcaXY + labels, one row per
                                            welded chain, SAME order as the C++ Chains)
  - chain_mlp_a2.pt/chain_norm_a2.json     (2-class gate = -B order key input)
  - chain3_mlp_m12.pt/chain3_norm_m12.json (3-class gate = -G 6 kills)

w7 config (from ab_m12_w7.log):
  -m hybrid -e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 1.495 -M4D -0.5 -M5 0.8974 -M6 -0.6054
  -MD 1e9 -MR -0.8 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.25 (pixdrop on)

Writes: <scratch>/m13_chains.npz  (one row per accepted chain TC)
"""
import json
import os
import sys
import time

import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
PROTO = f"{SA}/prototype"
SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-"
           "RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")

KWELD, THETA_EDGE, LAMBDA_LEN = 3, 0.0, 0.5
MAXCLAIMED, BRAID, ALPHA = 0.3, 0.25, 10.0
DCASPLIT = 0.5
M4, M4D, M5, M6, MD_, MR = 1.495, -0.5, 0.8974, -0.6054, 1e9, -0.8
U4 = U5 = U6 = 0.0
KGATEKILL, KNOCUT = 1e9, -1e5

BR = ["ls_mdIdx0", "ls_mdIdx1", "t3_lsIdx0", "t3_lsIdx1", "md_layer", "md_anchorHitIdx",
      "md_otherHitIdx", "t3_partOfPT5", "t3_partOfPT3", "t3_pt", "t3_eta", "t3_phi",
      "evt", "lumi"]


def csr(keys, nkeys):
    order = np.argsort(keys, kind="stable").astype(np.int64)
    counts = np.bincount(keys, minlength=nkeys)
    off = np.zeros(nkeys + 1, dtype=np.int64)
    np.cumsum(counts, out=off[1:])
    return off, order


def cross_pairs(offA, itemsA, offB, itemsB):
    nin, nout = np.diff(offA), np.diff(offB)
    npair = nin * nout
    tot = int(npair.sum())
    if tot == 0:
        return np.empty(0, np.int64), np.empty(0, np.int64)
    pstart = np.zeros(len(npair) + 1, dtype=np.int64)
    np.cumsum(npair, out=pstart[1:])
    e = np.arange(tot, dtype=np.int64)
    k = np.searchsorted(pstart, e, side="right") - 1
    l = e - pstart[k]
    no = nout[k]
    i_loc = l // no
    j_loc = l - i_loc * no
    return itemsA[offA[k] + i_loc], itemsB[offB[k] + j_loc]


def build_edges(t3_md0, t3_md2, t3_ls0, t3_ls1, nMD, nLS):
    offIn, itIn = csr(t3_md2, nMD)
    offOut, itOut = csr(t3_md0, nMD)
    i1, o1 = cross_pairs(offIn, itIn, offOut, itOut)
    keep = i1 != o1
    i1, o1 = i1[keep], o1[keep]
    lIn, litIn = csr(t3_ls1, nLS)
    lOut, litOut = csr(t3_ls0, nLS)
    i2, o2 = cross_pairs(lIn, litIn, lOut, litOut)
    keep = (i2 != o2) & (t3_md2[i2] != t3_md0[o2])
    i2, o2 = i2[keep], o2[keep]
    return (np.concatenate([i1, i2]).astype(np.int64),
            np.concatenate([o1, o2]).astype(np.int64))


def weld(inner, outer, logit, nNodes):
    """K6Weld.cc replica: kWeld sweeps of frozen-snapshot mutual best."""
    outW = np.full(nNodes, -1, dtype=np.int64)
    inW = np.full(nNodes, -1, dtype=np.int64)
    elig0 = logit >= THETA_EDGE
    nE = len(logit)
    order = np.lexsort((np.arange(nE), -logit))  # best first: logit desc, index asc
    for _ in range(KWELD):
        free = elig0 & (outW[inner] == -1) & (inW[outer] == -1)
        idx = order[free[order]]
        if idx.size == 0:
            break
        bestOut = np.full(nNodes, -1, dtype=np.int64)
        bestOut[inner[idx][::-1]] = idx[::-1]
        bestIn = np.full(nNodes, -1, dtype=np.int64)
        bestIn[outer[idx][::-1]] = idx[::-1]
        cand = bestOut[bestOut >= 0]
        mut = cand[bestIn[outer[cand]] == cand]
        if mut.size == 0:
            break
        outW[inner[mut]] = mut
        inW[outer[mut]] = mut
    return outW, inW


def extract_chains(inner, outer, outW, inW, nNodes):
    """Vectorized path walk. Chains ordered by head node index (== C++ extraction order)."""
    heads = np.nonzero((inW == -1) & (outW != -1))[0]
    nC = len(heads)
    chainOfNode = np.full(nNodes, -1, dtype=np.int64)
    chainOfEdge = np.full(len(inner), -1, dtype=np.int64)
    posOfNode = np.full(nNodes, -1, dtype=np.int32)
    active = heads.copy()
    cid = np.arange(nC, dtype=np.int64)
    depth = 0
    while active.size:
        chainOfNode[active] = cid
        posOfNode[active] = depth
        e = outW[active]
        has = e >= 0
        chainOfEdge[e[has]] = cid[has]
        active = outer[e[has]]
        cid = cid[has]
        depth += 1
        if depth > 64:
            raise RuntimeError("weld path too long -- cycle?")
    return heads, chainOfNode, chainOfEdge, posOfNode, nC


def group_unique(chain_ids, vals, nC, stride):
    """CSR of the per-chain deduped `vals` (chain_ids/vals same length)."""
    key = chain_ids.astype(np.int64) * stride + vals.astype(np.int64)
    key = np.unique(key)
    c = (key // stride).astype(np.int64)
    v = (key - c * stride).astype(np.int64)
    off = np.zeros(nC + 1, dtype=np.int64)
    np.cumsum(np.bincount(c, minlength=nC), out=off[1:])
    return off, v


def load_model(pt_path, norm_path):
    """Return ([(W,b), ...] as numpy float32, norm dict). Weights read via torch, applied
    in numpy (this torch build has no numpy bridge)."""
    import torch
    with open(norm_path) as fh:
        norm = json.load(fh)
    blob = torch.load(pt_path, map_location="cpu", weights_only=False)
    sd = blob["state_dict"]
    layers = []
    li = 0
    while f"{li}.weight" in sd:
        W = np.asarray(sd[f"{li}.weight"].tolist(), dtype=np.float32)
        b = np.asarray(sd[f"{li}.bias"].tolist(), dtype=np.float32)
        layers.append((W, b))
        li += 2  # Sequential(Linear, ReLU, Linear, ReLU, Linear)
    assert layers, f"no Linear layers found in {pt_path}: {list(sd)}"
    return layers, norm


def score_model(model, norm, cfmat, dca, cf_names):
    """cfmat: (n, 25) raw ChainFeatures in feature_spec order; dca: (n,)."""
    import torch
    names = norm["feature_names"]
    X = np.empty((cfmat.shape[0], len(names)), dtype=np.float32)
    for j, nm in enumerate(names):
        base = nm[3:]
        X[:, j] = dca if base == "dcaXY" else cfmat[:, cf_names.index(base)]
    for c in norm.get("conditioning") or []:
        if c["feature"] not in names:
            continue
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        elif c["op"] == "log10_1p":
            X[:, j] = np.log10(1.0 + X[:, j])
        else:
            raise ValueError(c["op"])
    Xs = (X - np.asarray(norm["mean"], np.float32)) / np.asarray(norm["std"], np.float32)
    h = Xs
    for k, (W, b) in enumerate(model):
        h = h @ W.T + b
        if k < len(model) - 1:
            h = np.maximum(h, 0.0)
    return h.astype(np.float32)


def main():
    nEvents = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    out_path = sys.argv[2] if len(sys.argv) > 2 else f"{SCRATCH}/m13_chains.npz"

    cache = np.load(f"{SCRATCH}/m10_edgecache_300.npz")
    LOG = cache["logit"]
    idx = np.load(f"{SCRATCH}/m10_edgeidx.npz")
    ekey = {(int(idx["lumi"][b]), int(idx["evt"][b])): b for b in range(len(idx["keys"]))}

    # chain dump: read whole file once, split by evt (rows are grouped per event in order)
    fd = uproot.open(f"{PROTO}/chains_m12_300evt.root")
    cf_names = fd["feature_spec"].member("fTitle")[3:].split(",")
    dtree = fd["chains"]
    dcols = ["evt", "label", "matchFrac", "simIdx", "simVxy", "simPt", "nLayers", "dcaXY"] + \
            [f"cf_{i:02d}" for i in range(len(cf_names))]
    d = dtree.arrays(dcols, library="np")
    devt = d["evt"]
    bnd = np.concatenate([[0], np.nonzero(np.diff(devt))[0] + 1, [len(devt)]])
    dstart = {int(devt[bnd[k]]): (int(bnd[k]), int(bnd[k + 1])) for k in range(len(bnd) - 1)}
    CF = np.stack([d[f"cf_{i:02d}"] for i in range(len(cf_names))], axis=1).astype(np.float32)

    m2, n2 = load_model(f"{PROTO}/chain_mlp_a2.pt", f"{PROTO}/chain_norm_a2.json")
    m3, n3 = load_model(f"{PROTO}/chain3_mlp_m12.pt", f"{PROTO}/chain3_norm_m12.json")
    print("scoring the full chain dump with both gates ...", flush=True)
    Z2 = score_model(m2, n2, CF, d["dcaXY"], cf_names)   # (N,1) 2-class logit
    Z3 = score_model(m3, n3, CF, d["dcaXY"], cf_names)   # (N,3)
    GATE = Z2[:, 0]
    mP, mD, mX = Z3[:, 1] - Z3[:, 0], Z3[:, 2] - Z3[:, 0], np.maximum(Z3[:, 1], Z3[:, 2]) - Z3[:, 0]
    print(f"  gate logit mean {GATE.mean():.3f}; mX mean {mX.mean():.3f}", flush=True)

    tree = uproot.open(f"{SA}/LSTNtuple_PU200RelVal_300evt.root")["tree"]
    recs = []
    nbad = 0
    t0 = time.time()
    for i in range(nEvents):
        a = tree.arrays(BR, entry_start=i, entry_stop=i + 1, library="np")
        lumi, evt = int(a["lumi"][0]), int(a["evt"][0])
        b = ekey[(lumi, evt)]
        lo, hi = int(idx["starts"][b]), int(idx["ends"][b])
        ls0, ls1 = a["ls_mdIdx0"][0].astype(np.int64), a["ls_mdIdx1"][0].astype(np.int64)
        t3l0, t3l1 = a["t3_lsIdx0"][0].astype(np.int64), a["t3_lsIdx1"][0].astype(np.int64)
        md_layer = a["md_layer"][0].astype(np.int64)
        md_h0 = a["md_anchorHitIdx"][0].astype(np.int64)
        md_h1 = a["md_otherHitIdx"][0].astype(np.int64)
        nMD, nLS, nT3 = len(md_layer), len(ls0), len(t3l0)
        t3_md0, t3_md1, t3_md2 = ls0[t3l0], ls1[t3l0], ls1[t3l1]

        inner, outer = build_edges(t3_md0, t3_md2, t3l0, t3l1, nMD, nLS)
        assert len(inner) == hi - lo, f"evt {evt}: edges {len(inner)} != cache {hi - lo}"
        lg = LOG[lo:hi]
        outW, inW = weld(inner, outer, lg, nT3)
        heads, cOfN, cOfE, posOfN, nC = extract_chains(inner, outer, outW, inW, nT3)

        ds, de = dstart[evt]
        nCd = de - ds
        if nC != nCd:
            # The cached edge logits are numpy-float64 evaluations of the same weights the
            # C++ float32 net uses; ~1e-6 differences flip a near-tie in K6 welding on a
            # handful of events. Alignment to the dump is then impossible -> drop the event
            # (never silently misalign).
            nbad += 1
            print(f"  !! evt {evt}: chains {nC} != dump {nCd} -- EVENT DROPPED", flush=True)
            continue

        nodes = np.nonzero(cOfN >= 0)[0]
        cn = cOfN[nodes]
        # per-chain MD set / layers / hits
        mds3 = np.concatenate([t3_md0[nodes], t3_md1[nodes], t3_md2[nodes]])
        cn3 = np.concatenate([cn, cn, cn])
        mdOff, mdItems = group_unique(cn3, mds3, nC, nMD)
        cmd = np.repeat(np.arange(nC), np.diff(mdOff))
        layOff, _ = group_unique(cmd, md_layer[mdItems], nC, 32)
        nLay = np.diff(layOff).astype(np.int32)
        nhits = int(max(md_h0.max(), md_h1.max())) + 1
        hitOff, hitItems = group_unique(np.concatenate([cmd, cmd]),
                                        np.concatenate([md_h0[mdItems], md_h1[mdItems]]),
                                        nC, nhits)
        nNodes_c = np.bincount(cn, minlength=nC).astype(np.int32)
        edgeSum = np.bincount(cOfE[cOfE >= 0], weights=lg[cOfE >= 0].astype(np.float64),
                              minlength=nC)

        # --- verification against the C++ dump (columns 0,1,2 = nNodes,nLayers,sumEdgeLogit)
        dslice = slice(ds, de)
        okN = np.array_equal(nNodes_c, CF[dslice, 0].astype(np.int32))
        okL = np.array_equal(nLay, CF[dslice, 1].astype(np.int32)) and \
              np.array_equal(nLay, d["nLayers"][dslice])
        dS = np.abs(edgeSum - CF[dslice, 2])
        if not (okN and okL and dS.max() < 2e-3):
            nbad += 1
            print(f"  !! evt {evt}: verify FAIL nNodes={okN} nLayers={okL} "
                  f"maxdSum={dS.max():.2e} -- EVENT DROPPED", flush=True)
            continue

        # --- scores exactly as C++ (legacy score from the dumped float32 sumEdgeLogit)
        score = (CF[dslice, 2] + np.float32(LAMBDA_LEN) * nLay.astype(np.float32)).astype(np.float32)
        gl = GATE[dslice]
        p, dd, x = mP[dslice], mD[dslice], mX[dslice]
        dca = d["dcaXY"][dslice]
        nL = nLay

        # --- -G 6 kills + exempt mask
        exempt = np.zeros(nC, dtype=bool)
        kill = np.zeros(nC, dtype=bool)
        t4 = nL <= 4
        ex4 = t4 & (dca >= max(DCASPLIT, 0.0))
        kill |= ex4 & (dd < M4D)
        exempt |= ex4
        ip4 = t4 & ~ex4
        kill |= ip4 & (x < M4)
        five = ~t4
        ip5 = five & (dca < DCASPLIT)
        thr = np.where(nL >= 6, M6, M5)
        kill |= ip5 & (p < thr) & (x < MR)
        ex5 = five & ~ip5
        kill |= ex5 & (dd < MD_) & (x < MR)
        exempt |= ex5
        score = np.where(kill, score - np.float32(KGATEKILL), score).astype(np.float32)

        # --- K9 candidate list
        thrAcc = np.where(exempt, np.where(nL >= 6, U6, np.where(nL == 5, U5, U4)), KNOCUT)
        pix = (np.bincount(cn, weights=(a["t3_partOfPT5"][0].astype(bool) |
                                        a["t3_partOfPT3"][0].astype(bool))[nodes].astype(float),
                           minlength=nC) > 0)
        cand = np.nonzero((score >= thrAcc) & ~pix)[0]

        # --- order key (-B 10), desc, index asc
        key = score - np.float32(ALPHA) * np.maximum(np.float32(0), -gl)
        cand = cand[np.lexsort((cand, -key[cand]))]

        # --- greedy hit claim (F=0.3) + owner-relative braid kill (W=0.25)
        owner = np.full(nhits, -1, dtype=np.int64)
        htot = np.diff(hitOff)
        accepted = []
        for c in cand:
            hs = hitItems[hitOff[c]:hitOff[c + 1]]
            ow = owner[hs]
            nClaimed = int((ow >= 0).sum())
            if nClaimed / len(hs) > MAXCLAIMED:
                continue
            if nClaimed > 0:
                o, cnts = np.unique(ow[ow >= 0], return_counts=True)
                if np.any(cnts >= BRAID * htot[o]):
                    continue
            owner[hs] = c
            accepted.append(c)
        accepted = np.array(accepted, dtype=np.int64)

        # --- K10 TC assembly (nLayers >= 4 only)
        acc = accepted[nL[accepted] >= 4]
        t3pt, t3eta, t3phi = a["t3_pt"][0], a["t3_eta"][0], a["t3_phi"][0]
        nodeOff = np.zeros(nC + 1, dtype=np.int64)
        np.cumsum(nNodes_c, out=nodeOff[1:])
        srt = np.lexsort((posOfN[nodes], cn))
        nodes_sorted = nodes[srt]
        cn_sorted = cn[srt]
        headNode = nodes_sorted[nodeOff[:-1]]
        tcpt = np.empty(len(acc), dtype=np.float32)
        for j, c in enumerate(acc):
            ns = nodes_sorted[nodeOff[c]:nodeOff[c + 1]]
            v = np.sort(t3pt[ns])
            tcpt[j] = v[(len(v) - 1) // 2]
        for j, c in enumerate(acc):
            recs.append((lumi, evt, int(c), int(nL[c]), float(tcpt[j]),
                         float(t3eta[headNode[c]]), float(t3phi[headNode[c]]),
                         float(score[c]), float(gl[c]), float(p[c]), float(dd[c]),
                         float(x[c]), float(dca[c]), int(d["label"][ds + c]),
                         float(d["matchFrac"][ds + c]), int(d["simIdx"][ds + c]),
                         float(d["simVxy"][ds + c]), float(d["simPt"][ds + c]),
                         int(exempt[c]), int(2 * (mdOff[c + 1] - mdOff[c]))))
        if i % 25 == 0:
            print(f"  evt {i} (lumi {lumi} evt {evt}): chains={nC} cand={len(cand)} "
                  f"acc={len(accepted)} tc={len(acc)}  {time.time() - t0:.1f}s", flush=True)

    dt = np.dtype([("lumi", "i4"), ("evt", "i8"), ("cidx", "i4"), ("nLayers", "i4"),
                   ("pt", "f4"), ("eta", "f4"), ("phi", "f4"), ("score", "f4"),
                   ("gate", "f4"), ("mP", "f4"), ("mD", "f4"), ("mX", "f4"),
                   ("dca", "f4"), ("label", "i4"), ("matchFrac", "f4"), ("simIdx", "i4"),
                   ("simVxy", "f4"), ("simPt", "f4"), ("exempt", "i4"), ("nhitOT", "i4")])
    arr = np.array(recs, dtype=dt)
    np.save(out_path.replace(".npz", ".npy"), arr)
    print(f"done {nEvents} events, {len(arr)} accepted chain TCs, verify-fail events={nbad}, "
          f"{time.time() - t0:.1f}s -> {out_path.replace('.npz', '.npy')}")


if __name__ == "__main__":
    main()
