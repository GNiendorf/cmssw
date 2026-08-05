#!/usr/bin/env python3
"""M10 Mission A Q2/Q3/Q5: per-candidate features + TC-LEVEL truth + arbitration levers.

Same validated re-derivation as m10_q3_recon.py (K1/K2 -> v3 edge logits from the feature
dump -> K6 -> K9), but truth and features are computed for EVERY 5+-layer post-pixdrop
candidate, so arbitration variants (maxClaimedFrac) can be scored on identical chains.
"""
import sys, json, pickle
from collections import Counter, defaultdict
import numpy as np
import uproot
import awkward as ak
import torch

PROTO = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/"
NT = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/LSTNtuple_PU200RelVal_300evt.root"
OUT = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad/m10_cand.pkl"
NEV = int(sys.argv[1]) if len(sys.argv) > 1 else 25
LAMBDA_LEN, THETA_EDGE, SWEEPS = 0.5, 0.0, 3

norm = json.load(open(PROTO + "edge_norm_v3.json"))
enames = norm["feature_names"]; emean = np.array(norm["mean"], np.float32)
estd = np.array(norm["std"], np.float32); econd = norm["conditioning"]
ck = torch.load(PROTO + "edge_mlp_v3.pt", map_location="cpu", weights_only=False)
sd = ck["state_dict"]
WS = [(np.array(sd[k].tolist(), np.float32), np.array(sd[k.replace("weight", "bias")].tolist(), np.float32))
      for k in sd if k.endswith("weight")]

def edge_logit(X):
    Xc = X.astype(np.float32).copy()
    for c in econd:
        j = enames.index(c["feature"])
        if c["op"] == "clip":
            np.clip(Xc[:, j], c["lo"], c["hi"], out=Xc[:, j])
        else:
            Xc[:, j] = np.log10(1.0 + Xc[:, j])
    h = (Xc - emean) / np.where(estd > 0, estd, 1.0)
    for i, (w, b) in enumerate(WS):
        h = h @ w.T + b
        if i < len(WS) - 1:
            np.maximum(h, 0.0, out=h)
    return h[:, 0]

def kasa(x, y):
    """Kasa algebraic circle fit; returns (chi2PerHit, kappa_unsigned_mag, R)."""
    n = len(x)
    if n < 3:
        return 0.0, 0.0, 0.0
    xm, ym = x.mean(), y.mean()
    u, v = x - xm, y - ym
    Suu = (u * u).sum(); Svv = (v * v).sum(); Suv = (u * v).sum()
    Suuu = (u ** 3).sum(); Svvv = (v ** 3).sum()
    Suvv = (u * v * v).sum(); Svuu = (v * u * u).sum()
    det = Suu * Svv - Suv * Suv
    if abs(det) <= 1e-12 * (Suu + Svv) ** 2:
        return 0.0, 0.0, 0.0
    b1 = 0.5 * (Suuu + Suvv); b2 = 0.5 * (Svvv + Svuu)
    uc = (b1 * Svv - b2 * Suv) / det
    vc = (b2 * Suu - b1 * Suv) / det
    R = np.sqrt(uc * uc + vc * vc + (Suu + Svv) / n)
    d = np.sqrt((u - uc) ** 2 + (v - vc) ** 2) - R
    cx, cy = uc + xm, vc + ym
    dca = abs(np.sqrt(cx * cx + cy * cy) - R)
    return float((d * d).sum() / n), float(dca), float(R)

def rzfit(x, y, z):
    n = len(x)
    s = np.zeros(n)
    s[1:] = np.cumsum(np.sqrt(np.diff(x) ** 2 + np.diff(y) ** 2))
    sb = s.mean(); den = ((s - sb) ** 2).sum()
    if den <= 1e-12:
        return 0.0
    b = ((s - sb) * (z - z.mean())).sum() / den
    a = z.mean() - b * sb
    r = z - a - b * s
    return float((r * r).sum() / n)

BR = ["evt", "md_layer", "md_type", "md_simIdxAll", "md_anchor_x", "md_anchor_y", "md_anchor_z",
      "ls_mdIdx0", "ls_mdIdx1", "t3_lsIdx0", "t3_lsIdx1", "t3_partOfPT5", "t3_partOfPT3",
      "t3_pt", "t3_eta", "t3_isFake", "sim_pt", "sim_vtxperp",
      "t3_fakeScore", "t3_promptScore", "t3_displacedScore", "t3_radius"]
nt = uproot.open(NT + ":tree").arrays(BR, entry_stop=NEV)
EFEAT = ["ni_%02d" % i for i in range(13)] + ["no_%02d" % i for i in range(13)] + ["ef_%02d" % i for i in range(14)]
edump = uproot.open(PROTO + "edges_300evt.root:edges")

def csr(key, nkeys, nitems):
    order = np.argsort(key, kind="stable")
    off = np.zeros(nkeys + 1, np.int64)
    np.cumsum(np.bincount(key, minlength=nkeys), out=off[1:])
    return off, order.astype(np.int64)

def pairs(off, items, offO, itemsO):
    dI, dO = np.diff(off), np.diff(offO)
    n = dI * dO
    grp = np.nonzero(n > 0)[0]
    if len(grp) == 0:
        return np.empty(0, np.int64), np.empty(0, np.int64)
    ng = n[grp]
    base = np.repeat(np.cumsum(np.concatenate([[0], ng]))[:-1], ng)
    k = np.arange(ng.sum(), dtype=np.int64) - base
    dOr = np.repeat(dO[grp], ng)
    return items[np.repeat(off[grp], ng) + k // dOr], itemsO[np.repeat(offO[grp], ng) + k % dOr]

cursor = 0
rows = []
for ie in range(NEV):
    md_layer = ak.to_numpy(nt["md_layer"][ie]).astype(np.int64)
    md_type = ak.to_numpy(nt["md_type"][ie]).astype(np.int64)
    ax = ak.to_numpy(nt["md_anchor_x"][ie]).astype(np.float64)
    ay = ak.to_numpy(nt["md_anchor_y"][ie]).astype(np.float64)
    az = ak.to_numpy(nt["md_anchor_z"][ie]).astype(np.float64)
    ls_md0 = ak.to_numpy(nt["ls_mdIdx0"][ie]).astype(np.int64)
    ls_md1 = ak.to_numpy(nt["ls_mdIdx1"][ie]).astype(np.int64)
    t3_ls0 = ak.to_numpy(nt["t3_lsIdx0"][ie]).astype(np.int64)
    t3_ls1 = ak.to_numpy(nt["t3_lsIdx1"][ie]).astype(np.int64)
    nMD, nLS, nT3 = len(md_layer), len(ls_md0), len(t3_ls0)
    t3_md0 = ls_md0[t3_ls0]; t3_md1 = ls_md1[t3_ls0]; t3_md2 = ls_md1[t3_ls1]
    offOutM, itOutM = csr(t3_md0, nMD, nT3); offInM, itInM = csr(t3_md2, nMD, nT3)
    offOutL, itOutL = csr(t3_ls0, nLS, nT3); offInL, itInL = csr(t3_ls1, nLS, nT3)
    i1, o1 = pairs(offInM, itInM, offOutM, itOutM); k = i1 != o1; i1, o1 = i1[k], o1[k]
    i2, o2 = pairs(offInL, itInL, offOutL, itOutL)
    k = (i2 != o2) & (t3_md2[i2] != t3_md0[o2]); i2, o2 = i2[k], o2[k]
    inner = np.concatenate([i1, i2]); outer = np.concatenate([o1, o2])
    etype = np.concatenate([np.ones(len(i1), np.int8), 2 * np.ones(len(i2), np.int8)])
    nE = len(inner)
    d = edump.arrays(["evt", "ef_00"] + EFEAT, entry_start=cursor, entry_stop=cursor + nE, library="np")
    cursor += nE
    assert d["evt"][0] == nt["evt"][ie] and np.array_equal(d["ef_00"].astype(np.int8), etype)
    lo = edge_logit(np.stack([d[c] for c in EFEAT], axis=1))
    dgI = np.where(etype == 1, np.diff(offInM)[np.where(etype == 1, t3_md2[inner], 0)],
                   np.diff(offInL)[t3_ls1[inner]])
    dgO = np.where(etype == 1, np.diff(offOutM)[np.where(etype == 1, t3_md2[inner], 0)],
                   np.diff(offOutL)[t3_ls1[inner]])
    degprod = dgI * dgO

    outW = np.full(nT3, -1, np.int64); inW = np.full(nT3, -1, np.int64)
    elig0 = lo >= THETA_EDGE; eidx = np.arange(nE, dtype=np.int64)
    for _ in range(SWEEPS):
        elig = elig0 & (outW[inner] == -1) & (inW[outer] == -1)
        ee = eidx[elig]
        if len(ee) == 0:
            break
        o = np.lexsort((ee, -lo[ee], inner[ee])); s = ee[o]; kk = inner[s]
        f = np.concatenate([[True], kk[1:] != kk[:-1]])
        bO = np.full(nT3, -1, np.int64); bO[kk[f]] = s[f]
        o = np.lexsort((ee, -lo[ee], outer[ee])); s = ee[o]; kk = outer[s]
        f = np.concatenate([[True], kk[1:] != kk[:-1]])
        bI = np.full(nT3, -1, np.int64); bI[kk[f]] = s[f]
        c = np.nonzero(bO >= 0)[0]; e = bO[c]; mut = bI[outer[e]] == e
        if mut.sum() == 0:
            break
        outW[c[mut]] = e[mut]; inW[outer[e[mut]]] = e[mut]

    p5 = ak.to_numpy(nt["t3_partOfPT5"][ie]).astype(bool)
    p3 = ak.to_numpy(nt["t3_partOfPT3"][ie]).astype(bool)
    mdsim = ak.to_list(nt["md_simIdxAll"][ie])
    t3pt = ak.to_numpy(nt["t3_pt"][ie]); t3eta = ak.to_numpy(nt["t3_eta"][ie])
    t3fk = ak.to_numpy(nt["t3_isFake"][ie])
    simpt = ak.to_numpy(nt["sim_pt"][ie]); simvxy = ak.to_numpy(nt["sim_vtxperp"][ie])
    t3fs = ak.to_numpy(nt["t3_fakeScore"][ie]); t3ps = ak.to_numpy(nt["t3_promptScore"][ie])
    t3ds = ak.to_numpy(nt["t3_displacedScore"][ie]); t3rad = ak.to_numpy(nt["t3_radius"][ie])

    for h in np.nonzero((inW == -1) & (outW != -1))[0]:
        nodes = [int(h)]; es = []; n = int(h)
        while outW[n] != -1:
            e = int(outW[n]); es.append(e); n = int(outer[e]); nodes.append(n)
        mds = []
        for t3 in nodes:
            for m in (t3_md0[t3], t3_md1[t3], t3_md2[t3]):
                if m not in mds:
                    mds.append(int(m))
        nl = len(set(md_layer[mds].tolist()))
        if nl < 5:
            continue
        if any(p5[t] or p3[t] for t in nodes):
            continue
        tsets = []
        for t3 in nodes:
            cnt = Counter()
            for m in (t3_md0[t3], t3_md1[t3], t3_md2[t3]):
                for s in mdsim[m]:
                    cnt[s] += 1
            tsets.append({s for s, v in cnt.items() if v >= 2})
        inter = set.intersection(*tsets)
        cnt = Counter()
        for m in mds:
            for s in mdsim[m]:
                cnt[s] += 1
        bestsim, bestfrac = (-1, 0.0)
        if cnt:
            bestsim, bc = cnt.most_common(1)[0]
            bestfrac = bc / len(mds)
        nAcc = len(simpt)
        bsPt = float(simpt[bestsim]) if 0 <= bestsim < nAcc else -999.0
        bsVxy = float(simvxy[bestsim]) if 0 <= bestsim < nAcc else -999.0
        mm = np.array(mds)
        chi2, dca, R = kasa(ax[mm], ay[mm])
        rz = rzfit(ax[mm], ay[mm], az[mm])
        pts = sorted(float(t3pt[t]) for t in nodes)
        rows.append(dict(evt=ie, nodes=nodes, mds=mds, nLayers=nl, nNodes=len(nodes),
                         sumEdge=float(np.sum(lo[es])), minEdge=float(np.min(lo[es])),
                         meanEdge=float(np.mean(lo[es])),
                         maxDeg=int(np.max(degprod[es])),
                         score=float(np.sum(lo[es]) + LAMBDA_LEN * nl),
                         chi2=chi2, rz=rz, R=R, dca=dca, nMD=len(mds),
                         missPos=[k for k, m in enumerate(mds) if bestsim not in mdsim[m]],
                         fakePos=[k for k, t in enumerate(nodes) if t3fk[t]],
                         emptyPos=[k for k, t in enumerate(tsets) if not t],
                         nEmpty=sum(1 for t in tsets if not t), nInter=len(inter),
                         nFakeT3=int(sum(t3fk[t] for t in nodes)),
                         bestfrac=bestfrac, bestsim=int(bestsim), bsPt=bsPt, bsVxy=bsVxy,
                         t3fkMax=float(max(t3fs[t] for t in nodes)),
                         t3fkMean=float(np.mean([t3fs[t] for t in nodes])),
                         t3pmMin=float(min(t3ps[t] for t in nodes)),
                         t3dsMin=float(min(t3ds[t] for t in nodes)),
                         nPS=int(sum(1 for m in mds if md_type[m] == 1)),
                         nBarrel=int(sum(1 for m in mds if md_layer[m] <= 6)),
                         innerLayer=int(md_layer[mds[0]]),
                         layerSpan=int(md_layer[mds].max() - md_layer[mds].min()),
                         radSpread=float(np.std([t3rad[t] for t in nodes])),
                         tcpt=pts[(len(pts) - 1) // 2],
                         tceta=float(t3eta[nodes[0]])))
    print("evt %3d: cand5=%d" % (ie, sum(1 for r in rows if r["evt"] == ie)), flush=True)

pickle.dump(rows, open(OUT, "wb"))
print("wrote %d candidate rows" % len(rows))
