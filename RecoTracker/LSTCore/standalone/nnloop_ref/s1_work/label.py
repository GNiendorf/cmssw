#!/usr/bin/env python3
"""S1: truth labels + WP bin keys for the round-1 edge dump.

Label rule = prototype/Labels.cc::labelEdges VERBATIM (see FINDINGS_NN [S1 19:59]):
  md sim set  = sims present on BOTH hits of the MD (matcher frac 1.0 > 0.75)
  T3 sim set  = sims present in >= 2 of the T3's 3 md sim sets
  edge label  = 1 iff sims(inner) INTERSECT sims(outer) is non-empty

Also emits, per edge, the WP bin key of the INNER node on LST's T3-DNN binning
(NeuralNetwork.h:127-133): ptbin = (radius*k2Rinv1GeVf*2 > 5), etabin = |anchorEta(md0)|/0.25
with the last bin absorbing > 2.5.  radius comes from node feature 1 (log10R), eta from
ph2 x/y/z at the MD0 anchor row.

Per event it writes one .npz shard into <out>/ev%04d.npz with
  label (uint8), ptbin (uint8), etabin (uint8), simVxy (float32, -999 for fakes / pileup),
  simPt (float32), nsim_in/nsim_out (uint8 diagnostics)
in the SAME ORDER as the kept-edge records of edges.bin / edgefeat.bin.
"""
import os
import sys

import numpy as np
import uproot
import awkward as ak

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dumpio import iter_edges, iter_nodes, iter_feat  # noqa: E402

TRK = ("/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/event_1000.root")
DUMP = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/nnloop_ref/round1"

K2RINV = np.float32(0.00299792458 * 3.8 / 2.0)  # interface/alpaka/Common.h:35
PT_SPLIT = np.float32(5.0)
KETASIZE = np.float32(0.25)
KETABINS = 10


def md_sim_sets(hits, sims_off, sims_flat):
    """hits: (nMD,2) ph2 rows.  Returns CSR (offsets, values) of the per-MD sim set."""
    n = len(hits)
    out_keys = []
    for col in (0, 1):
        h = hits[:, col].astype(np.int64)
        cnt = (sims_off[h + 1] - sims_off[h]).astype(np.int64)
        idx = np.repeat(np.arange(n, dtype=np.int64), cnt)
        # gather the sim values of each hit
        start = sims_off[h]
        pos = np.arange(cnt.sum(), dtype=np.int64) - np.repeat(np.cumsum(cnt) - cnt, cnt) + np.repeat(start, cnt)
        out_keys.append(idx * SIMSHIFT + sims_flat[pos].astype(np.int64))
    a, b = out_keys
    both = np.intersect1d(a, b, assume_unique=False)
    md = (both // SIMSHIFT).astype(np.int64)
    sim = (both % SIMSHIFT).astype(np.int32)
    off = np.zeros(n + 1, dtype=np.int64)
    np.add.at(off, md + 1, 1)
    np.cumsum(off, out=off)
    return off, sim  # `both` is sorted so values are grouped by md already


SIMSHIFT = np.int64(1 << 22)  # > max sim rows per event (checked at runtime)


def t3_sim_sets(nodemd, md_off, md_sim):
    """nodemd: (nN,3) md indices.  T3 sim set = sims in >= 2 of the 3 md sets."""
    nN = len(nodemd)
    keys = []
    for col in range(3):
        m = nodemd[:, col].astype(np.int64)
        cnt = (md_off[m + 1] - md_off[m]).astype(np.int64)
        tot = int(cnt.sum())
        idx = np.repeat(np.arange(nN, dtype=np.int64), cnt)
        start = md_off[m]
        pos = np.arange(tot, dtype=np.int64) - np.repeat(np.cumsum(cnt) - cnt, cnt) + np.repeat(start, cnt)
        keys.append(idx * SIMSHIFT + md_sim[pos].astype(np.int64))
    allk = np.concatenate(keys)
    allk.sort()
    if len(allk) == 0:
        return np.zeros(nN + 1, dtype=np.int64), np.zeros(0, dtype=np.int32)
    # a key with multiplicity >= 2 == the sim is in >= 2 of the 3 md sets (md sets are unique)
    same = allk[1:] == allk[:-1]
    keep = allk[:-1][same]
    keep = np.unique(keep)
    node = (keep // SIMSHIFT).astype(np.int64)
    sim = (keep % SIMSHIFT).astype(np.int32)
    off = np.zeros(nN + 1, dtype=np.int64)
    np.add.at(off, node + 1, 1)
    np.cumsum(off, out=off)
    return off, sim


def pad_sets(off, sim, nN, kmax):
    """dense (nN,kmax) int32 padded with -1; returns (dense, n_over) where n_over counts
    nodes with more than kmax sims (handled by the caller)."""
    cnt = (off[1:] - off[:-1]).astype(np.int64)
    dense = np.full((nN, kmax), -1, dtype=np.int32)
    for k in range(kmax):
        m = cnt > k
        dense[m, k] = sim[off[:-1][m] + k]
    return dense, int((cnt > kmax).sum())


def main():
    outdir = sys.argv[1]
    ev0 = int(sys.argv[2])
    ev1 = int(sys.argv[3])
    os.makedirs(outdir, exist_ok=True)

    t = uproot.open(TRK)["trackingNtuple/tree"]
    branches = ["ph2_simHitIdx", "simhit_simTrkIdx", "ph2_x", "ph2_y", "ph2_z",
                "sim_pt", "sim_parentVtxIdx", "simvtx_x", "simvtx_y"]

    ed = iter_edges(DUMP + "/edges.bin")
    nd = iter_nodes(DUMP + "/nodes.bin")
    ft = iter_feat(DUMP + "/edgefeat.bin")

    T = None
    for iev in range(ev1):
        e = next(ed)
        n = next(nd)
        f = next(ft)
        if iev < ev0:
            continue
        if T is None or iev >= T_stop:
            T_start = iev
            T_stop = min(ev1, iev + 25)
            T = t.arrays(branches, library="ak", entry_start=T_start, entry_stop=T_stop)
        r = iev - T_start

        ievt_e, nN, nE1, nE2, ei, eo, et, lo = e
        ievt_n, stable, hitrows = n
        ievt_f, nodefeat, fi, fo, fty, ef = f
        assert ievt_e == ievt_n == ievt_f == iev, (ievt_e, ievt_n, ievt_f, iev)
        assert len(nodefeat) == nN == len(hitrows)
        assert np.array_equal(ei, fi) and np.array_equal(eo, fo) and np.array_equal(et, fty)

        # ---- per-hit sim sets (unique per hit), CSR over ph2 rows
        ph2 = T["ph2_simHitIdx"][r]
        sht = np.asarray(T["simhit_simTrkIdx"][r], dtype=np.int64)
        flat = np.asarray(ak.flatten(ph2), dtype=np.int64)
        counts = np.asarray(ak.num(ph2), dtype=np.int64)
        simv = np.where((flat >= 0) & (flat < len(sht)), sht[np.clip(flat, 0, len(sht) - 1)], -1)
        nh = len(counts)
        hid = np.repeat(np.arange(nh, dtype=np.int64), counts)
        good = simv >= 0
        keys = np.unique(hid[good] * SIMSHIFT + simv[good])
        assert simv.max(initial=0) < SIMSHIFT
        hh = (keys // SIMSHIFT).astype(np.int64)
        ss = (keys % SIMSHIFT).astype(np.int32)
        sims_off = np.zeros(nh + 1, dtype=np.int64)
        np.add.at(sims_off, hh + 1, 1)
        np.cumsum(sims_off, out=sims_off)
        sims_flat = ss

        # ---- distinct MDs of this event's nodes: hitrows is (nN, 6) = (a0,o0,a1,o1,a2,o2)
        pairs = hitrows.reshape(nN * 3, 2).astype(np.int64)
        assert pairs.max() < nh, "hit row out of ph2 range -- event misalignment"
        pkey = pairs[:, 0] * (1 << 21) + pairs[:, 1]
        upk, inv = np.unique(pkey, return_inverse=True)
        umd = np.stack([upk // (1 << 21), upk % (1 << 21)], axis=1)
        md_off, md_sim = md_sim_sets(umd, sims_off, sims_flat)
        nodemd = inv.reshape(nN, 3)

        t3off, t3sim = t3_sim_sets(nodemd, md_off, md_sim)
        cnt3 = (t3off[1:] - t3off[:-1]).astype(np.int64)
        KMAX = 4
        dense, n_over = pad_sets(t3off, t3sim, nN, KMAX)

        # ---- edge label: any common sim between the padded sets
        A = dense[ei.astype(np.int64)]
        B = dense[eo.astype(np.int64)]
        lab = np.zeros(len(ei), dtype=np.uint8)
        common = np.full(len(ei), -1, dtype=np.int32)
        for k in range(KMAX):
            ak_ = A[:, k]
            m = ak_ >= 0
            if not m.any():
                continue
            hit = m & ((B == ak_[:, None]).any(axis=1))
            new = hit & (common < 0)
            common[new] = ak_[new]
            lab |= hit.astype(np.uint8)
        # exact fallback for the (rare) nodes with more than KMAX sims
        if n_over:
            over = np.where(cnt3 > KMAX)[0]
            oset = set(over.tolist())
            sel = np.where(np.isin(ei, over) | np.isin(eo, over))[0]
            for e_ in sel:
                a = set(t3sim[t3off[ei[e_]]:t3off[ei[e_] + 1]].tolist())
                b = set(t3sim[t3off[eo[e_]]:t3off[eo[e_] + 1]].tolist())
                c = a & b
                lab[e_] = 1 if c else 0
                common[e_] = min(c) if c else -1
            del oset

        # ---- sim kinematics: highest sim_pt ACCEPTED sim in the intersection.
        # We keep the single representative found above (sets are size<=1 for 99%+ of true
        # edges); for the multi-sim intersections pick the highest-pt accepted one exactly.
        spt = np.asarray(T["sim_pt"][r], dtype=np.float32)
        pvi = np.asarray(T["sim_parentVtxIdx"][r], dtype=np.int64)
        svx = np.asarray(T["simvtx_x"][r], dtype=np.float32)
        svy = np.asarray(T["simvtx_y"][r], dtype=np.float32)
        nAcc = len(spt)
        best = common.copy()
        multi = np.where(lab.astype(bool))[0]
        if len(multi):
            # for each true edge recompute the accepted-highest-pt choice only when the inner
            # or outer set has more than one sim (cheap mask)
            need = multi[(cnt3[ei[multi]] > 1) | (cnt3[eo[multi]] > 1)]
            for e_ in need:
                a = t3sim[t3off[ei[e_]]:t3off[ei[e_] + 1]]
                b = t3sim[t3off[eo[e_]]:t3off[eo[e_] + 1]]
                c = np.intersect1d(a, b)
                acc = c[c < nAcc]
                best[e_] = acc[np.argmax(spt[acc])] if len(acc) else (c[0] if len(c) else -1)
        simPt = np.full(len(ei), -999.0, dtype=np.float32)
        simVxy = np.full(len(ei), -999.0, dtype=np.float32)
        okk = (best >= 0) & (best < nAcc)
        bb = best[okk].astype(np.int64)
        simPt[okk] = spt[bb]
        vok = (pvi[bb] >= 0) & (pvi[bb] < len(svx))
        vv = np.full(len(bb), -999.0, dtype=np.float32)
        vv[vok] = np.hypot(svx[pvi[bb][vok]], svy[pvi[bb][vok]])
        simVxy[okk] = vv

        # ---- WP bin key of the INNER node
        radius = np.power(np.float32(10.0), nodefeat[:, 1].astype(np.float32))
        pt = radius * K2RINV * np.float32(2.0)
        ptbin_n = (pt > PT_SPLIT).astype(np.uint8)
        a0 = hitrows[:, 0].astype(np.int64)
        hx = np.asarray(T["ph2_x"][r], dtype=np.float32)[a0]
        hy = np.asarray(T["ph2_y"][r], dtype=np.float32)[a0]
        hz = np.asarray(T["ph2_z"][r], dtype=np.float32)[a0]
        rt = np.sqrt(hx * hx + hy * hy)
        eta = np.abs(np.arccosh(np.sqrt(hx * hx + hy * hy + hz * hz) / rt)).astype(np.float32)
        etabin_n = np.where(eta > np.float32(2.5), KETABINS - 1,
                            (eta / KETASIZE).astype(np.int32)).astype(np.uint8)
        etabin_n = np.minimum(etabin_n, KETABINS - 1)

        np.savez(os.path.join(outdir, "ev%04d.npz" % iev),
                 label=lab,
                 ptbin=ptbin_n[ei.astype(np.int64)],
                 etabin=etabin_n[ei.astype(np.int64)],
                 simPt=simPt, simVxy=simVxy,
                 nsim_in=np.minimum(cnt3[ei.astype(np.int64)], 255).astype(np.uint8),
                 nsim_out=np.minimum(cnt3[eo.astype(np.int64)], 255).astype(np.uint8))
        if iev % 25 == 0 or iev == ev0:
            print("evt %4d: edges %7d  true %7d (%.4f)  E1true %.4f E2true %.4f  nodes-with-sim %.3f  over%d=%d"
                  % (iev, len(ei), int(lab.sum()), lab.mean(),
                     lab[et == 1].mean() if (et == 1).any() else -1,
                     lab[et == 2].mean() if (et == 2).any() else -1,
                     (cnt3 > 0).mean(), KMAX, n_over), flush=True)


if __name__ == "__main__":
    main()
