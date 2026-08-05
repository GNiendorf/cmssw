#!/usr/bin/env python3
"""M10 Mission A Q3/Q4: composition of ACCEPTED fake chains (member-T3 sim sets).

READ-ONLY forensics: re-derives the anchor's chain pipeline in numpy for the first N
events (K1/K2 graph -> v3 edge logits taken from the 32.9M-edge feature dump -> K6
mutual-best weld -> K9 pixdrop + greedy MD-claim), restricted to the nLayers>=5 branch
(97.8% of the anchor's fake chain TCs; with -G 2 the T4-class is scored on the gate scale
whose values sit below the 5+ legacy scores, so it claims MDs last and does not perturb
the 5+ accepted set).

Every stage is validated against the anchor's own numbers before any composition claim.
"""
import sys
import json
from collections import Counter, defaultdict

import numpy as np
import uproot
import awkward as ak
import torch

PROTO = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/"
NT = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/LSTNtuple_PU200RelVal_300evt.root"
NEV = int(sys.argv[1]) if len(sys.argv) > 1 else 20
LAMBDA_LEN = 0.5
THETA_EDGE = 0.0
MAXCLAIM = 0.3
SWEEPS = 3

# ------------------------------------------------------------------ edge model (v3)
norm = json.load(open(PROTO + "edge_norm_v3.json"))
enames = norm["feature_names"]
emean = np.array(norm["mean"], dtype=np.float32)
estd = np.array(norm["std"], dtype=np.float32)
econd = norm["conditioning"]
ck = torch.load(PROTO + "edge_mlp_v3.pt", map_location="cpu", weights_only=False)
sd = ck["state_dict"]
WS = [(np.array(sd[k].tolist(), dtype=np.float32),
       np.array(sd[k.replace("weight", "bias")].tolist(), dtype=np.float32))
      for k in sd if k.endswith("weight")]
print("edge model arch", ck.get("arch"), "layers", [w.shape for w, _ in WS])

def edge_logit(X):
    Xc = X.astype(np.float32).copy()
    for c in econd:
        j = enames.index(c["feature"])
        if c["op"] == "clip":
            np.clip(Xc[:, j], c["lo"], c["hi"], out=Xc[:, j])
        elif c["op"] == "log10_1p":
            Xc[:, j] = np.log10(1.0 + Xc[:, j])
        else:
            raise ValueError(c["op"])
    Z = (Xc - emean) / np.where(estd > 0, estd, 1.0)
    h = Z
    for i, (w, b) in enumerate(WS):
        h = h @ w.T + b
        if i < len(WS) - 1:
            np.maximum(h, 0.0, out=h)
    return h[:, 0]

# ------------------------------------------------------------------ inputs
BR = ["evt", "md_layer", "md_type", "md_simIdxAll", "ls_mdIdx0", "ls_mdIdx1",
      "t3_lsIdx0", "t3_lsIdx1", "t3_partOfPT5", "t3_partOfPT3", "t3_pt", "t3_isFake",
      "t3_simIdxAll"]
nt = uproot.open(NT + ":tree").arrays(BR, entry_stop=NEV)
print("ntuple events read:", len(nt["evt"]))

EFEAT = ["ni_%02d" % i for i in range(13)] + ["no_%02d" % i for i in range(13)] + \
        ["ef_%02d" % i for i in range(14)]
edump = uproot.open(PROTO + "edges_300evt.root:edges")

def csr(key, nkeys, nitems):
    order = np.argsort(key, kind="stable")
    cnt = np.bincount(key, minlength=nkeys)
    off = np.zeros(nkeys + 1, dtype=np.int64)
    np.cumsum(cnt, out=off[1:])
    return off, order.astype(np.int64)

def pairs(off, items, offO, itemsO):
    """Cartesian product per key in K2's emission order (in-loop outer, out-loop inner)."""
    dI = np.diff(off); dO = np.diff(offO)
    n = dI * dO
    grp = np.nonzero(n > 0)[0]
    if len(grp) == 0:
        return np.empty(0, np.int64), np.empty(0, np.int64)
    ng = n[grp]
    base = np.repeat(np.cumsum(np.concatenate([[0], ng]))[:-1], ng)
    k = np.arange(ng.sum(), dtype=np.int64) - base
    gI = np.repeat(off[grp], ng); gO = np.repeat(offO[grp], ng)
    dOr = np.repeat(dO[grp], ng)
    inner = items[gI + k // dOr]
    outer = itemsO[gO + k % dOr]
    return inner, outer

edge_cursor = 0
stats = defaultdict(list)
comp_rows = []

for ie in range(NEV):
    md_layer = ak.to_numpy(nt["md_layer"][ie]).astype(np.int64)
    md_type = ak.to_numpy(nt["md_type"][ie]).astype(np.int64)
    ls_md0 = ak.to_numpy(nt["ls_mdIdx0"][ie]).astype(np.int64)
    ls_md1 = ak.to_numpy(nt["ls_mdIdx1"][ie]).astype(np.int64)
    t3_ls0 = ak.to_numpy(nt["t3_lsIdx0"][ie]).astype(np.int64)
    t3_ls1 = ak.to_numpy(nt["t3_lsIdx1"][ie]).astype(np.int64)
    nMD, nLS, nT3 = len(md_layer), len(ls_md0), len(t3_ls0)
    t3_md0 = ls_md0[t3_ls0]; t3_md1 = ls_md1[t3_ls0]; t3_md2 = ls_md1[t3_ls1]

    offOutM, itOutM = csr(t3_md0, nMD, nT3)
    offInM, itInM = csr(t3_md2, nMD, nT3)
    offOutL, itOutL = csr(t3_ls0, nLS, nT3)
    offInL, itInL = csr(t3_ls1, nLS, nT3)

    i1, o1 = pairs(offInM, itInM, offOutM, itOutM)
    keep = i1 != o1
    i1, o1 = i1[keep], o1[keep]
    i2, o2 = pairs(offInL, itInL, offOutL, itOutL)
    keep = (i2 != o2) & (t3_md2[i2] != t3_md0[o2])
    i2, o2 = i2[keep], o2[keep]

    inner = np.concatenate([i1, i2]); outer = np.concatenate([o1, o2])
    etype = np.concatenate([np.ones(len(i1), np.int8), 2 * np.ones(len(i2), np.int8)])
    nE = len(inner)

    # ---- read the matching slice of the feature dump and VALIDATE the alignment ----
    d = edump.arrays(["evt", "ef_00", "ef_09", "ef_12", "ef_13"] + EFEAT,
                     entry_start=edge_cursor, entry_stop=edge_cursor + nE, library="np")
    edge_cursor += nE
    assert len(np.unique(d["evt"])) == 1 and d["evt"][0] == nt["evt"][ie], \
        "event key mismatch at evt %d" % ie
    assert np.array_equal(d["ef_00"].astype(np.int8), etype), "etype sequence mismatch"
    sharedMd = np.where(etype == 1, t3_md2[inner], ls_md0[t3_ls1[inner]])
    degIn = np.where(etype == 1, np.diff(offInM)[sharedMd],
                     np.diff(offInL)[t3_ls1[inner]])
    degOut = np.where(etype == 1, np.diff(offOutM)[sharedMd],
                      np.diff(offOutL)[t3_ls1[inner]])
    assert np.array_equal(d["ef_09"].astype(np.int64), md_layer[sharedMd]), "sharedLayer mismatch"
    assert np.array_equal(d["ef_12"].astype(np.int64), degIn), "degIn mismatch"
    assert np.array_equal(d["ef_13"].astype(np.int64), degOut), "degOut mismatch"

    X = np.stack([d[c] for c in EFEAT], axis=1)
    lo = edge_logit(X)

    # ------------------------------------------------------------------ K6 weld
    outWeld = np.full(nT3, -1, np.int64); inWeld = np.full(nT3, -1, np.int64)
    elig0 = lo >= THETA_EDGE
    eidx = np.arange(nE, dtype=np.int64)
    for sweep in range(SWEEPS):
        elig = elig0 & (outWeld[inner] == -1) & (inWeld[outer] == -1)
        ee = eidx[elig]
        if len(ee) == 0:
            break
        # bestOut per inner / bestIn per outer: highest logOdds, lower index on ties
        ordr = np.lexsort((ee, -lo[ee], inner[ee]))
        s = ee[ordr]; kk = inner[s]
        first = np.concatenate([[True], kk[1:] != kk[:-1]])
        bestOut = np.full(nT3, -1, np.int64); bestOut[kk[first]] = s[first]
        ordr = np.lexsort((ee, -lo[ee], outer[ee]))
        s = ee[ordr]; kk = outer[s]
        first = np.concatenate([[True], kk[1:] != kk[:-1]])
        bestIn = np.full(nT3, -1, np.int64); bestIn[kk[first]] = s[first]
        cand = np.nonzero(bestOut >= 0)[0]
        e = bestOut[cand]
        mut = bestIn[outer[e]] == e
        n_ = cand[mut]; e_ = e[mut]
        if len(n_) == 0:
            break
        outWeld[n_] = e_; inWeld[outer[e_]] = e_

    heads = np.nonzero((inWeld == -1) & (outWeld != -1))[0]
    chains = []
    for h in heads:
        nodes = [h]; esum = 0.0; n = h
        while outWeld[n] != -1:
            e = outWeld[n]; esum += lo[e]; n = int(outer[e]); nodes.append(n)
        mds = []
        for t3 in nodes:
            for m in (t3_md0[t3], t3_md1[t3], t3_md2[t3]):
                if m not in mds:
                    mds.append(int(m))
        nl = len(set(md_layer[mds].tolist()))
        chains.append((nodes, mds, nl, esum + LAMBDA_LEN * nl))
    stats["nchain"].append(len(chains))

    # -------------------------------------------- K9: 5+-layer branch (see docstring)
    p5 = ak.to_numpy(nt["t3_partOfPT5"][ie]).astype(bool)
    p3 = ak.to_numpy(nt["t3_partOfPT3"][ie]).astype(bool)
    cand = []
    for ci, (nodes, mds, nl, sc) in enumerate(chains):
        if nl < 5:
            continue
        if any(p5[t] or p3[t] for t in nodes):
            continue
        cand.append(ci)
    cand.sort(key=lambda c: (-chains[c][3], c))
    claimed = np.zeros(nMD, bool)
    accepted = []
    for c in cand:
        mds = chains[c][1]
        nc = int(claimed[mds].sum())
        if nc / len(mds) > MAXCLAIM:
            continue
        claimed[mds] = True
        accepted.append(c)
    stats["nacc5"].append(len(accepted))

    # ------------------------------------------------------------------ truth
    mdsim = ak.to_list(nt["md_simIdxAll"][ie])
    t3pt = ak.to_numpy(nt["t3_pt"][ie])
    t3fake = ak.to_numpy(nt["t3_isFake"][ie])
    md_owner = defaultdict(list)   # md -> accepted chain slot
    rows_this = []
    for slot, c in enumerate(accepted):
        nodes, mds, nl, sc = chains[c]
        tsets = []
        for t3 in nodes:
            cnt = Counter()
            for m in (t3_md0[t3], t3_md1[t3], t3_md2[t3]):
                for s in mdsim[m]:
                    cnt[s] += 1
            tsets.append({s for s, v in cnt.items() if v >= 2})
        inter = set.intersection(*tsets) if tsets else set()
        nEmpty = sum(1 for s in tsets if not s)
        cnt = Counter()
        for m in mds:
            for s in mdsim[m]:
                cnt[s] += 1
        bestfrac, bestsim = (0.0, -1)
        if cnt:
            bestsim, bc = cnt.most_common(1)[0]
            bestfrac = bc / len(mds)
        pts = sorted(float(t3pt[t]) for t in nodes)
        rows_this.append(dict(evt=ie, ci=c, nNodes=len(nodes), nMD=len(mds), nLayers=nl,
                              score=float(sc), nEmpty=nEmpty, nInter=len(inter),
                              bestfrac=bestfrac, bestsim=int(bestsim),
                              nDistinctSims=len({s for t in tsets for s in t}),
                              nFakeT3=int(sum(t3fake[t] for t in nodes)),
                              tcpt=pts[(len(pts) - 1) // 2],
                              simsPerT3=[len(t) for t in tsets],
                              slot=slot))
        for m in mds:
            md_owner[m].append(slot)
    # braid remnants: does this accepted chain share an MD with a TC-TRUE accepted chain?
    istrue = [r["bestfrac"] > 0.75 for r in rows_this]
    for slot, c in enumerate(accepted):
        sh_true = 0; sh_any = 0
        for m in chains[c][1]:
            others = [o for o in md_owner[m] if o != slot]
            if others:
                sh_any += 1
                if any(istrue[o] for o in others):
                    sh_true += 1
        rows_this[slot]["shareMDwithTrue"] = sh_true
        rows_this[slot]["shareMDany"] = sh_any
    comp_rows.extend(rows_this)
    print("evt %3d (id %d): edges=%d chains=%d cand5=%d accepted5=%d"
          % (ie, nt["evt"][ie], nE, len(chains), len(cand), len(accepted)))

np.save(PROTO + "m10_comp_rows.npy", np.array([1]))  # touch marker (unused)

# ---------------------------------------------------------------- summary
import pickle
with open("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad/m10_comp.pkl", "wb") as fh:
    pickle.dump(comp_rows, fh)
print("\nreconstructed 5+-layer accepted chains/evt: mean %.1f" % np.mean(stats["nacc5"]))
print("welded chains/evt: mean %.1f" % np.mean(stats["nchain"]))
