#!/usr/bin/env python3
"""m10_graph.py - reconstruct the per-event T3 graph from the edge dump and replicate
K6 mutual-best welding in numpy (READ-ONLY forensics).

Node identity is recovered by hashing the 13 node-feature floats (ni_* / no_*): the
dump carries no T3 index, but node features are a deterministic function of the T3, so
identical vectors == same T3 (collisions are checked by reproducing the binary's chain
count). Edge order in the dump == K2 emission order == the K6 edge-index tie-break.
"""
import numpy as np
import uproot

import m10_weights

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SCRATCH = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad"

NI = [f"ni_{i:02d}" for i in range(13)]
NO = [f"no_{i:02d}" for i in range(13)]
EF = [f"ef_{i:02d}" for i in range(14)]
ALL = NI + NO + EF
KWELD = 3

_W = None


def weights():
    global _W
    if _W is None:
        _W = m10_weights.parse(f"{SA}/prototype/edge_mlp_weights.h", "edgemlp")
    return _W


def load_block(tree, lo, hi, extra=("label", "simIdx", "etype")):
    br = list(ALL) + list(extra)
    a = tree.arrays(br, entry_start=lo, entry_stop=hi, library="np")
    X = np.empty((hi - lo, 40), dtype=np.float32)
    for i, n in enumerate(ALL):
        X[:, i] = a[n]
    return X, a


def node_ids(X):
    """Return (inner_id, outer_id, nNodes) using 13-float node-feature hashing."""
    n = X.shape[0]
    ni = X[:, 0:13].copy()
    no = X[:, 13:26].copy()
    both = np.concatenate([ni, no], axis=0)
    v = np.ascontiguousarray(both).view([('f', np.float32, 13)]).ravel()
    uniq, inv = np.unique(v, return_inverse=True)
    return inv[:n].astype(np.int32), inv[n:].astype(np.int32), len(uniq)


def edge_logits(X):
    return m10_weights.logit(X, weights()).astype(np.float32)


def weld(inner, outer, logit, nNodes, theta=0.0, sweeps=KWELD):
    """Exact numpy replica of k6WeldChains slot logic. Returns outWeld, inWeld arrays."""
    outW = np.full(nNodes, -1, dtype=np.int32)
    inW = np.full(nNodes, -1, dtype=np.int32)
    elig0 = logit >= theta
    nE = len(logit)
    order = np.lexsort((np.arange(nE), -logit))  # score desc, index asc == beats()
    for _ in range(sweeps):
        free = elig0 & (outW[inner] == -1) & (inW[outer] == -1)
        idx = order[free[order]]
        if idx.size == 0:
            break
        # bestOut per tail: first occurrence in (score desc, index asc) order
        bestOut = np.full(nNodes, -1, dtype=np.int32)
        tails = inner[idx]
        firstT = np.unique(tails, return_index=True)[1]
        # np.unique returns index of FIRST occurrence in the sorted-unique sense, but we
        # need first in `idx` order -> use a reverse-fill trick
        bestOut[tails[::-1]] = idx[::-1]
        bestIn = np.full(nNodes, -1, dtype=np.int32)
        heads = outer[idx]
        bestIn[heads[::-1]] = idx[::-1]
        # mutual pairs
        cand = bestOut[bestOut >= 0]
        mut = cand[bestIn[outer[cand]] == cand]
        if mut.size == 0:
            break
        outW[inner[mut]] = mut
        inW[outer[mut]] = mut
    return outW, inW


def chains_from_weld(inner, outer, outW, inW, nNodes):
    """Walk paths; return list of node lists (innermost-first) and list of edge lists."""
    heads = np.nonzero((inW == -1) & (outW != -1))[0]
    nodes_out, edges_out = [], []
    for h in heads:
        ns = [int(h)]
        es = []
        n = int(h)
        while outW[n] != -1:
            e = int(outW[n])
            es.append(e)
            n = int(outer[e])
            ns.append(n)
        nodes_out.append(ns)
        edges_out.append(es)
    return nodes_out, edges_out


if __name__ == "__main__":
    idx = np.load(f"{SCRATCH}/m10_edgeidx.npz")
    t = uproot.open(f"{SA}/prototype/edges_300evt.root")["edges"]
    for b in range(3):
        lo, hi = int(idx["starts"][b]), int(idx["ends"][b])
        X, a = load_block(t, lo, hi)
        inner, outer, nN = node_ids(X)
        lg = edge_logits(X)
        outW, inW = weld(inner, outer, lg, nN)
        ns, es = chains_from_weld(inner, outer, outW, inW, nN)
        print(f"evt block {b} (lumi {idx['lumi'][b]} evt {idx['evt'][b]}): edges={hi-lo} "
              f"distinctNodes={nN} pass(theta=0)={(lg>=0).sum()} chains={len(ns)} "
              f"trueEdges={(a['label']==1).sum()}")
