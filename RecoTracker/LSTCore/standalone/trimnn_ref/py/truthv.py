#!/usr/bin/env python3
"""TRIM-NN: p3_ref/truth3.py with TWO changes, both mechanical.

  1. the `drop` column is kept at int32.  In a VARIANT dump that slot carries the chain index
     within the event, not the -1/0/1/2 trim action truth3.py narrows to int8.
  2. the sibling-import paths point back at the standalone tree from trimnn_ref/py.

The LABEL is untouched: labelChainsHarness, replicated not corrected -- a chain (here: a chain
VARIANT, over its own MD hit list) is true iff some sim's deduped hit fraction is > 0.75.
"""
import os
import sys
import time

import awkward as ak
import numpy as np
import uproot

STAND = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(STAND, "p3_ref"))
sys.path.insert(0, os.path.join(STAND, "nnloop_ref", "s2_work"))
from chainio import iter_events  # noqa: E402

T0 = time.time()
BR = ["ph2_simHitIdx", "simhit_simTrkIdx", "ph2_x", "ph2_y", "ph2_z",
      "sim_pt", "sim_eta", "sim_pca_dxy", "sim_parentVtxIdx", "simvtx_x", "simvtx_y",
      "sim_bunchCrossing", "sim_event"]
JETBR = ["sim_genjet_idx", "sim_genjet_deltaR", "genjet_pt", "genjet_eta"]
JETPT, JETETA, CORE_DR = 1000.0, 2.5, 0.05     # performance.cc's -J selection + the core band


def log(m):
    print("[%7.1fs] %s" % (time.time() - T0, m), flush=True)


def event_truth(fix, hits, hoff, ph2sh_flat, ph2sh_cnt, ph2sh_off, sht, nacc):
    """Vectorised harness matcher for one event.

    Returns (frac, simIdx, frac_acc, simIdx_acc, nUniq) per chain.
    """
    nC = len(fix)
    nh = np.diff(hoff)
    cid = np.repeat(np.arange(nC, dtype=np.int64), nh)
    # ---- unique (chain, hit) pairs -----------------------------------------------------
    h64 = hits.astype(np.int64)
    key = cid * (1 << 21) + h64
    key.sort()
    keep = np.empty(len(key), dtype=bool)
    keep[0] = True
    np.not_equal(key[1:], key[:-1], out=keep[1:])
    key = key[keep]
    pc = key >> 21                      # chain id per unique pair
    ph = key & ((1 << 21) - 1)          # ph2 row per unique pair
    nUniq = np.bincount(pc, minlength=nC).astype(np.int64)

    # ---- expand each unique hit to its (deduped) sim list ------------------------------
    cnts = ph2sh_cnt[ph]
    tot = int(cnts.sum())
    if tot == 0:
        z = np.zeros(nC, dtype=np.float64)
        return z, np.full(nC, -1, np.int64), z.copy(), np.full(nC, -1, np.int64), nUniq
    starts = ph2sh_off[ph]
    outoff = np.concatenate(([0], np.cumsum(cnts)[:-1]))
    ar = np.arange(tot, dtype=np.int64) - np.repeat(outoff, cnts)
    gather = np.repeat(starts, cnts) + ar
    sims = ph2sh_flat[gather]
    sims = np.where((sims >= 0) & (sims < len(sht)), sht[np.clip(sims, 0, len(sht) - 1)], -1)
    pc2 = np.repeat(pc, cnts)
    ph2i = np.repeat(ph, cnts)
    ok = sims >= 0
    pc2, ph2i, sims = pc2[ok], ph2i[ok], sims[ok]
    if len(sims) == 0:
        z = np.zeros(nC, dtype=np.float64)
        return z, np.full(nC, -1, np.int64), z.copy(), np.full(nC, -1, np.int64), nUniq

    # dedupe (chain, hit, sim) -- one ph2 row counts at most 1 for any sim
    k3 = (pc2 * (1 << 21) + ph2i) * (1 << 20) + sims
    k3.sort()
    keep = np.empty(len(k3), dtype=bool)
    keep[0] = True
    np.not_equal(k3[1:], k3[:-1], out=keep[1:])
    k3 = k3[keep]
    s_of = k3 & ((1 << 20) - 1)
    c_of = (k3 >> 41)

    # count per (chain, sim)
    k2 = c_of * (1 << 20) + s_of
    k2.sort()
    uk, cc = np.unique(k2, return_counts=True)
    uc = uk >> 20
    us = uk & ((1 << 20) - 1)

    # best over all sims, and best over ACCEPTED sims (full row < nacc)
    best = np.zeros(nC, dtype=np.int64)
    np.maximum.at(best, uc, cc)
    hit_best = cc == best[uc]
    simIdx = np.full(nC, -1, dtype=np.int64)
    simIdx[uc[hit_best]] = us[hit_best]

    acc = us < nacc
    bestA = np.zeros(nC, dtype=np.int64)
    if acc.any():
        np.maximum.at(bestA, uc[acc], cc[acc])
    hb = acc & (cc == bestA[uc]) & (bestA[uc] > 0)
    simIdxA = np.full(nC, -1, dtype=np.int64)
    simIdxA[uc[hb]] = us[hb]

    den = np.maximum(nUniq, 1)
    return (best / den, simIdx, bestA / den, simIdxA, nUniq)


def main():
    dump, trknt, nent, outdir = sys.argv[1:5]
    jets = "--jets" in sys.argv[5:]
    nent = int(nent)
    os.makedirs(outdir, exist_ok=True)

    log("scanning %s" % dump)
    tree = uproot.open(trknt)["trackingNtuple/tree"]
    assert tree.num_entries >= nent, "ntuple has only %d entries" % tree.num_entries
    br = BR + (JETBR if jets else [])
    if jets:
        miss = [b for b in JETBR if b not in tree.keys()]
        assert not miss, "--jets but the ntuple lacks %s" % miss

    it = iter_events(dump)
    out = {k: [] for k in ("evt nLayers nNodes nMDs branch drop flags score dcaXY zF zP zD "
                           "aEtaC label frac simIdx vxy dxy simPt nUniq nacc dR isCore").split()}
    Xs = []
    nrec = 0
    ent = 0
    for batch in tree.iterate(br, library="ak", step_size=25, entry_stop=nent):
        nb = len(batch["sim_pt"])
        for b in range(nb):
            rec = next(it, None)
            assert rec is not None, "dump ran out at entry %d" % ent
            ievt, _, _, fix, hits, hoff = rec
            assert ievt == ent, ("ALIGNMENT: dump record %d carries ievt %d -- the record "
                                "counter is not the entry index" % (ent, ievt))
            nrec += 1
            nC = len(fix)

            shj = batch["ph2_simHitIdx"][b]
            cnt = ak.to_numpy(ak.num(shj)).astype(np.int64)
            flat = ak.to_numpy(ak.flatten(shj)).astype(np.int64)
            off = np.concatenate(([0], np.cumsum(cnt)))
            sht = ak.to_numpy(batch["simhit_simTrkIdx"][b]).astype(np.int64)
            bx = ak.to_numpy(batch["sim_bunchCrossing"][b])
            se = ak.to_numpy(batch["sim_event"][b])
            accmask = (bx == 0) & (se == 0)
            nacc = int(accmask.sum())
            assert accmask[:nacc].all(), "accepted sims are not a contiguous prefix at entry %d" % ent

            frac, sidx, fracA, sidxA, nUniq = event_truth(
                fix, hits, hoff, flat, cnt, off[:-1], sht, nacc)

            lab = (frac > 0.75).astype(np.int8)
            useA = lab.astype(bool) & (fracA > 0.75) & (sidxA >= 0)
            pvi = ak.to_numpy(batch["sim_parentVtxIdx"][b]).astype(np.int64)
            svx = ak.to_numpy(batch["simvtx_x"][b]).astype(np.float64)
            svy = ak.to_numpy(batch["simvtx_y"][b]).astype(np.float64)
            dxyb = ak.to_numpy(batch["sim_pca_dxy"][b]).astype(np.float64)
            sptb = ak.to_numpy(batch["sim_pt"][b]).astype(np.float64)
            vxy = np.full(nC, -999.0)
            dxy = np.full(nC, -999.0)
            spt = np.full(nC, -999.0)
            drc = np.full(nC, -999.0)
            iscore = np.zeros(nC, np.int8)
            si = sidxA[useA]
            if jets and len(si):
                gji = ak.to_numpy(batch["sim_genjet_idx"][b]).astype(np.int64)
                sdr = ak.to_numpy(batch["sim_genjet_deltaR"][b]).astype(np.float64)
                jpt = ak.to_numpy(batch["genjet_pt"][b]).astype(np.float64)
                jet = ak.to_numpy(batch["genjet_eta"][b]).astype(np.float64)
                g = gji[si]
                okj = (g >= 0) & (g < len(jpt))
                tmpd = np.full(len(si), -999.0)
                tmpd[okj] = sdr[si][okj]
                drc[useA] = tmpd
                selj = np.zeros(len(si), np.int8)
                if okj.any():
                    gg = g[okj]
                    selj[okj] = ((jpt[gg] > JETPT) & (np.abs(jet[gg]) < JETETA)
                                 & (sdr[si][okj] < CORE_DR)).astype(np.int8)
                iscore[useA] = selj
            if len(si):
                v = pvi[si]
                good = (v >= 0) & (v < len(svx))
                tmp = np.full(len(si), -999.0)
                tmp[good] = np.hypot(svx[v[good]], svy[v[good]])
                vxy[useA] = tmp
                dxy[useA] = np.abs(dxyb[si])
                spt[useA] = sptb[si]

            # aEtaC from the anchor hit of MD index 2 (dump hit slot 4)
            px = ak.to_numpy(batch["ph2_x"][b]).astype(np.float32)
            py = ak.to_numpy(batch["ph2_y"][b]).astype(np.float32)
            pz = ak.to_numpy(batch["ph2_z"][b]).astype(np.float32)
            nmd = fix[:, 2].astype(np.int64)
            aeta = np.full(nC, -1.0, dtype=np.float64)
            has = (nmd >= 3) & (fix[:, 1] > 0)
            if has.any():
                row = hits[hoff[:-1][has] + 4].astype(np.int64)
                x, y, z = px[row].astype(np.float64), py[row].astype(np.float64), pz[row].astype(np.float64)
                r3 = np.sqrt(x * x + y * y + z * z)
                rt = np.sqrt(x * x + y * y)
                aeta[has] = np.abs(np.arccosh(np.maximum(r3 / np.maximum(rt, 1e-30), 1.0)))

            out["evt"].append(np.full(nC, ent, np.int32))
            out["nLayers"].append(fix[:, 3].astype(np.int8))
            out["nNodes"].append(fix[:, 1].astype(np.int16))
            out["nMDs"].append(fix[:, 2].astype(np.int16))
            out["branch"].append(fix[:, 4].astype(np.int8))
            # TRIM-NN: this slot carries the CHAIN INDEX in a variant dump, not a trim action,
            # so it is kept at full width (truth3.py narrows it to int8).
            out["drop"].append(fix[:, 5].astype(np.int32))
            out["flags"].append(fix[:, 6].astype(np.uint8))
            for j, k in ((7, "score"), (8, "dcaXY"), (9, "zF"), (10, "zP"), (11, "zD")):
                out[k].append(fix[:, j].astype(np.float32))
            out["aEtaC"].append(aeta.astype(np.float32))
            out["label"].append(lab)
            out["frac"].append(frac.astype(np.float32))
            out["simIdx"].append(sidx.astype(np.int32))
            out["vxy"].append(vxy.astype(np.float32))
            out["dxy"].append(dxy.astype(np.float32))
            out["simPt"].append(spt.astype(np.float32))
            out["nUniq"].append(nUniq.astype(np.int16))
            out["nacc"].append(np.full(nC, nacc, np.int32))
            out["dR"].append(drc.astype(np.float32))
            out["isCore"].append(iscore)
            Xs.append(fix[:, 15:].astype(np.float32))
            ent += 1
        if nrec % 100 < nb:
            log("entry %d: %d chains so far" % (ent, sum(len(a) for a in out["label"])))

    if os.environ.get("S2_FULL", "1") == "1":
        assert next(it, None) is None, "dump has MORE records than %d entries" % nent
    log("records %d == entries %d, ievt contiguous 0..N-1" % (nrec, nent))

    M = {k: np.concatenate(v) for k, v in out.items()}
    X = np.concatenate(Xs)
    np.save(os.path.join(outdir, "X.npy"), X)
    np.savez(os.path.join(outdir, "meta.npz"), **M)
    n = len(X)
    lab = M["label"] == 1
    vxy = M["vxy"]
    y3 = np.zeros(n, np.int8)
    y3[lab & (vxy < 1.0)] = 1
    y3[lab & (vxy >= 1.0)] = 2
    log("chains %d | fake %d prompt %d displaced %d (true frac %.4f)"
        % (n, (y3 == 0).sum(), (y3 == 1).sum(), (y3 == 2).sum(), lab.mean()))
    log("pileup-only trues (label 1, vxy -999) = %d  -> PROMPT class by the shipped rule"
        % int((lab & (vxy < -998)).sum()))
    if "isCore" in M:
        ic = M["isCore"] == 1
        log("core-true chains (matched sim in a >1 TeV genjet, dR < %.2f) = %d (%.4f of all trues)"
            % (CORE_DR, int(ic.sum()), ic.sum() / max(lab.sum(), 1)))
    log("wrote %s" % outdir)


if __name__ == "__main__":
    main()
