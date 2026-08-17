#!/usr/bin/env python3
"""[DISP] Disposition census of the master-pT5 / our-bare-T5 deficit.

DENOMINATOR: sims that LST master reconstructs as a pT5 (tc_type 7) and that WE reconstruct as a
bare T5 (tc_type 4), on the same event, under the standard core selection.

For each such sim we walk OUR attach stage and say exactly what happened to its true (chain, pLS)
pair.  The chain is identified HIT-EXACTLY (the emitted T5's ph2 hit set is the chain's ph2 hit
set -- verified unique), not by a truth join; only the pLS side needs the truth join, and that
reuses label_attach.py's machinery verbatim through lstmatch_ref/census.py.

Buckets, in the order the DEPLOYED kernel decides them (ChainAttach.h at the dump's commit
01c3be68339: the delivery bar is applied BEFORE the per-target argmax, so a sub-bar pair is dead
whatever its rank):

  NOLAYERS    the chain has < 5 layers -> never a stage-A target (score-only aux list)
  DCASENT     the chain's dcaXY >= attachDcaMax (1e9) -> excluded from the target list
  NOPLS       no pLS in the event carries this sim
  NOTSCORED   a true pLS exists, the chain is a stage-A target, but the pair was never scored
              (the r/tanLambda/phi grid never brought them together, or the analytic prefilter
              attachEvalPairX rejected it)
  BELOWBAR    the pair was scored but no true pair reaches its seed's eta-banded delivery bar
  ARGMAXLOST  a true pair is above bar, but the chain's above-bar argmax is a DIFFERENT seed
  CONTENDLOST the true pair IS the chain's pick, but the seed was taken by another chain
  OTHER       the chain won its seed in our replay yet the TC came out bare -- downstream

Usage: census_disp.py [nevents] [outprefix] [dumpdir] [barmodel]
  barmodel = "vintage" (git 01c3be68339: one eta-banded row) or "shipped" (git da81b72b9f3 /
  9c12c1ca2a0: 2 pt rows x 3 eta bands, plus a displaced-TARGET row keyed on chain dcaXY).
  The bar model must match the binary that wrote the dump -- mixing them silently mis-buckets
  every sim, which is why it is an explicit argument and never inferred.
"""
import json
import os
import struct
import sys
import time

import awkward as ak
import numpy as np
import uproot

DNN = "/mnt/data1/gsn27/here/chain_clean/src/RecoTracker/LSTCore/standalone/analysis/DNN"
LSTM = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/lstmatch_ref"
sys.path.insert(0, DNN)
sys.path.insert(0, LSTM)
from join_dump_io import iter_events as iter_join            # noqa: E402
from label_attach import hit_sim_csr, group_threshold        # noqa: E402

HERE = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/disp_ref"
DUMP = "/mnt/data1/gsn27/here/chain_clean/dump/pu1000"
NTUPLE = "/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/event_1000.root"
OURS = os.path.join(DUMP, "run.root")
MAST = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/pt_ref/root/master_putune_p08.root"

BR = ["ph2_simHitIdx", "pix_simHitIdx", "simhit_simTrkIdx", "see_hitIdx", "see_hitType", "see_pt",
      "sim_pt", "sim_eta", "sim_bunchCrossing", "sim_event"]

CMAGIC = 0x50323243
NFEAT = 25
CFIX = struct.Struct("<IIIIiiI" + "f" * (8 + NFEAT))
CHDR = struct.Struct("<IIIII")
PMAGIC = 0x50414952
ATTACH_DCA_MAX = 1e9

# The delivery bars of the binary that WROTE this dump (git 01c3be68339,
# hdr_ChainConfig.h md5 0386da3e...): a SINGLE row banded on |seed eta| only.  The 2x3 (pt x eta)
# + displaced table quoted in the task brief is commit da81b72b9f3, which is LATER than this dump.
BAR_B, BAR_T, BAR_E = 6.64275, 6.23461, 6.209162
# The bars that are shipped NOW (da81b72b9f3), replayed as a counterfactual only.  Note that head
# was retrained (input 22 / dBeta) after this dump, so these logits are NOT its logits.
NEW_LO = (5.019017, 4.310568, 4.334996)
NEW_HI = (3.519017, 2.810568, 5.834996)
NEW_DISP = (6.519017, 5.810568, 5.834996)
NEW_PTSPLIT, NEW_DCASPLIT = 5.0, 0.5


def seed_eta_band(abs_eta):
    """The deployed lookup's own 3 bands on the SEED's |eta|: <1.1 / <1.7 / >=1.7."""
    return np.where(abs_eta < 1.1, 0, np.where(abs_eta < 1.7, 1, 2))


def bars_vintage(abs_eta, pt_in, disp_target):
    """git 01c3be68339: one row, banded on |seed eta| only. No pt split, no displaced row."""
    return np.array([BAR_B, BAR_T, BAR_E])[seed_eta_band(abs_eta)]


def bars_shipped(abs_eta, pt_in, disp_target):
    """git da81b72b9f3 / 9c12c1ca2a0, transcribed from ChainAttachPlsPre + ChainAttachTargetPre.

    `disp_target` is the kernel's `!(chains.dcaXY()[chainIdx] < dcaSplit)` -- NaN-rejecting, so an
    unfittable dca counts as displaced. It is a TARGET property, so the bar is per PAIR here, not
    per seed as in the vintage model.
    """
    e = seed_eta_band(abs_eta)
    prompt = np.where(pt_in >= NEW_PTSPLIT, np.array(NEW_HI)[e], np.array(NEW_LO)[e])
    return np.where(disp_target, np.array(NEW_DISP)[e], prompt)


BAR_MODELS = {"vintage": bars_vintage, "shipped": bars_shipped}

EDGES = np.array([0.0, 1.0, 1.5, 2.0, 2.5])
NB = len(EDGES) - 1
BANDS = ["|eta|<1.0", "1.0-1.5", "1.5-2.0", "2.0-2.5"]
BUCKETS = ["NOLAYERS", "DCASENT", "NOPLS", "NOTSCORED", "BELOWBAR", "ARGMAXLOST",
           "CONTENDLOST", "OTHER"]
T0 = time.time()


def log(m):
    print("[%7.1fs] %s" % (time.time() - T0, m), flush=True)


def iter_chains_hits(path):
    """census.py's iter_chains, plus the per-chain ph2 hit rows (which it skips)."""
    f = open(path, "rb")
    while True:
        h = f.read(20)
        if len(h) < 20:
            break
        magic, ievt, nN, nE, nC = CHDR.unpack(h)
        assert magic == CMAGIC
        nn = np.empty(nC, np.int64)
        nlay = np.empty(nC, np.int64)
        flg = np.empty(nC, np.int64)
        dca = np.empty(nC, np.float32)
        nhit = np.empty(nC, np.int64)
        chunks, hitchunks = [], []
        for c in range(nC):
            v = CFIX.unpack(f.read(CFIX.size))
            preN, n, m, drop = v[0], v[1], v[2], v[5]
            drop = drop if drop < 2 ** 31 else drop - 2 ** 32
            nlay[c], flg[c], dca[c] = v[3], v[6], v[8]
            items = np.frombuffer(f.read(4 * preN), dtype="<u4")
            f.read(4 * max(preN - 1, 0))
            hits = np.frombuffer(f.read(8 * m), dtype="<u4")
            s = 1 if drop == 1 else 0
            chunks.append(items[s:s + n])
            hitchunks.append(hits)
            nn[c] = n
            nhit[c] = 2 * m
        hoff = np.concatenate(([0], np.cumsum(nhit)))
        yield (ievt, nC, nn, (np.concatenate(chunks) if chunks else np.zeros(0, "<u4")),
               nlay, flg, dca, hoff,
               (np.concatenate(hitchunks) if hitchunks else np.zeros(0, "<u4")))
    f.close()


def iter_pairs_light(path):
    """(hdr, stage, target, pls, logit) -- the 16-byte row prefix; the feature block is skipped."""
    f = open(path, "rb")
    while True:
        pre = f.read(8)
        if len(pre) < 8:
            break
        magic, ver = struct.unpack("<2I", pre)
        assert magic == PMAGIC and ver == 2
        ievt, nChains, nT3, nRows, nDrop, dsA, dsB, nFeat, nProbe = struct.unpack("<9I", f.read(36))
        rb = 16 + 4 * (nFeat + nProbe)
        buf = np.frombuffer(f.read(rb * nRows), dtype=np.uint8)
        if nRows:
            buf = buf.reshape(nRows, rb)
            head = buf[:, :16].copy()
            ids = head.view("<u4").reshape(nRows, 4)
            st = ids[:, 0].astype(np.int64)
            tg = ids[:, 1].astype(np.int64)
            pl = ids[:, 2].astype(np.int64)
            lg = head[:, 12:16].copy().view("<f4").ravel()
        else:
            st = tg = pl = np.zeros(0, np.int64)
            lg = np.zeros(0, np.float32)
        yield dict(ievt=ievt, nChains=nChains, nT3=nT3, nRows=nRows, nDrop=nDrop,
                   dsA=dsA, dsB=dsB), st, tg, pl, lg
    f.close()


def orig_key(path):
    t = uproot.open(path)["trackingNtuple/tree"]
    d = t.arrays(["sim_pt", "sim_eta", "sim_bunchCrossing", "sim_event"], library="ak")
    m = (d["sim_bunchCrossing"] == 0) & (d["sim_event"] == 0)
    pt, eta = d["sim_pt"][m], d["sim_eta"][m]
    return (np.asarray(ak.num(pt, axis=1)).astype(np.float64) * 1.0e9
            + np.asarray(ak.sum(pt, axis=1)).astype(np.float64)
            + 1.0e-3 * np.asarray(ak.sum(abs(eta), axis=1)).astype(np.float64))


def lst_key(path):
    d = uproot.open(path)["tree"].arrays(["sim_pt", "sim_eta"], library="ak")
    return (np.asarray(ak.num(d["sim_pt"], axis=1)).astype(np.float64) * 1.0e9
            + np.asarray(ak.sum(d["sim_pt"], axis=1)).astype(np.float64)
            + 1.0e-3 * np.asarray(ak.sum(abs(d["sim_eta"]), axis=1)).astype(np.float64))


def build_pls_simsets(batch, b, plsrec, ph2f, ph2c, ph2o, pixf, pixc, pixo, sht):
    """label_attach.py's pLS sim sets, transcribed from lstmatch_ref/census.py."""
    nP = len(plsrec)
    seeds = plsrec["seed"].astype(np.int64)
    shi = batch["see_hitIdx"][b]
    sty = batch["see_hitType"][b]
    slen = ak.to_numpy(ak.num(shi)).astype(np.int64)
    shiF = ak.to_numpy(ak.flatten(shi)).astype(np.int64)
    shtF = ak.to_numpy(ak.flatten(sty)).astype(np.int64)
    soff = np.concatenate(([0], np.cumsum(slen)))[:-1]
    L = slen[seeds]
    sel = np.stack([np.zeros(nP, np.int64), np.ones(nP, np.int64),
                    np.full(nP, 2, np.int64), L - 1], axis=1)
    use = np.stack([np.ones(nP, bool), np.ones(nP, bool),
                    np.ones(nP, bool), L > 3], axis=1)
    gsrc = soff[seeds][:, None] + sel
    hidx = shiF[gsrc]
    htyp = shtF[gsrc]
    pid = np.repeat(np.arange(nP, dtype=np.int64), 4).reshape(nP, 4)
    pid_f, hidx_f, htyp_f = pid[use], hidx[use], htyp[use]
    key = (pid_f * (1 << 23) + hidx_f) * 8 + np.clip(htyp_f, 0, 7)
    key = np.unique(key)
    htyp_u = key & 7
    hidx_u = (key >> 3) & ((1 << 23) - 1)
    pid_u = key >> 26
    nuniqP = np.bincount(pid_u, minlength=nP)
    gid_l, sim_l = [], []
    for tcode, (fl, ct, of) in ((0, (pixf, pixc, pixo)), (4, (ph2f, ph2c, ph2o))):
        sm = htyp_u == tcode
        if not sm.any():
            continue
        rr = hidx_u[sm]
        ur2, uoff2, usims2 = hit_sim_csr(rr, fl, ct, of, sht)
        p2 = np.searchsorted(ur2, rr)
        cnts = np.diff(uoff2)[p2]
        tot = int(cnts.sum())
        if tot == 0:
            continue
        outo = np.concatenate(([0], np.cumsum(cnts)[:-1]))
        ar = np.arange(tot, dtype=np.int64) - np.repeat(outo, cnts)
        src = np.repeat(uoff2[p2], cnts) + ar
        gid_l.append(np.repeat(pid_u[sm], cnts))
        sim_l.append(usims2[src])
    if gid_l:
        return group_threshold(np.concatenate(gid_l), np.concatenate(sim_l), nP, nuniqP)
    return np.zeros(nP + 1, np.int64), np.zeros(0, np.int64)


def build_chain_simsets(nodes, nodeItems, nn, nC, ph2f, ph2c, ph2o, sht, chfrac=0.75):
    """label_attach.py's chain sim sets (only needed to name CONTENTION winners)."""
    nN = len(nodes)
    if nN == 0 or nC == 0:
        return np.zeros(nC + 1, np.int64), np.zeros(0, np.int64)
    h = nodes[:, 1:].astype(np.int64)
    ur, uoff, usims = hit_sim_csr(h.ravel(), ph2f, ph2c, ph2o, sht)
    pos_in_ur = np.searchsorted(ur, h)
    mdA = pos_in_ur[:, 0::2].ravel()
    mdB = pos_in_ur[:, 1::2].ravel()
    same = mdA == mdB
    nuniq = np.where(same, 1, 2)
    mdid = np.arange(nN * 3, dtype=np.int64)
    gid_l, sim_l, mem_l = [], [], []
    for side, arr in ((0, mdA), (1, mdB)):
        cnts = np.diff(uoff)[arr]
        if cnts.sum() == 0:
            continue
        tot = int(cnts.sum())
        outo = np.concatenate(([0], np.cumsum(cnts)[:-1]))
        ar = np.arange(tot, dtype=np.int64) - np.repeat(outo, cnts)
        src = np.repeat(uoff[arr], cnts) + ar
        gid_l.append(np.repeat(mdid, cnts))
        sim_l.append(usims[src])
        mem_l.append(np.full(tot, side, np.int64))
    if not gid_l:
        return np.zeros(nC + 1, np.int64), np.zeros(0, np.int64)
    g = np.concatenate(gid_l)
    s = np.concatenate(sim_l)
    mm = np.concatenate(mem_l)
    ok = ~(same[g] & (mm == 1))
    mdoff, mdsims = group_threshold(g[ok], s[ok], nN * 3, nuniq)
    mdcnt = np.diff(mdoff)
    gmd = np.repeat(np.arange(nN * 3, dtype=np.int64), mdcnt)
    t3off, t3sims = group_threshold(gmd // 3, mdsims, nN, 2)
    t3cnt = np.diff(t3off)
    if not len(nodeItems):
        return np.zeros(nC + 1, np.int64), np.zeros(0, np.int64)
    ni = nodeItems.astype(np.int64)
    cid = np.repeat(np.arange(nC, dtype=np.int64), nn)
    cc = t3cnt[ni]
    tot = int(cc.sum())
    if tot == 0:
        return np.zeros(nC + 1, np.int64), np.zeros(0, np.int64)
    outo = np.concatenate(([0], np.cumsum(cc)[:-1]))
    ar = np.arange(tot, dtype=np.int64) - np.repeat(outo, cc)
    src = np.repeat(t3off[ni], cc) + ar
    need = np.maximum(np.ceil(chfrac * nn).astype(np.int64), 1)
    return group_threshold(np.repeat(cid, cc), t3sims[src], nC, need)


def main():
    global DUMP, OURS
    nent = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    pref = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "disp1000")
    if len(sys.argv) > 3:
        DUMP = sys.argv[3]
        OURS = os.path.join(DUMP, "run.root")
    model = sys.argv[4] if len(sys.argv) > 4 else "vintage"
    barfn = BAR_MODELS[model]
    log("dump %s  bar model %s" % (DUMP, model))

    # ---- event alignment ---------------------------------------------------------------------
    ko, ka, km = orig_key(NTUPLE), lst_key(OURS), lst_key(MAST)
    assert len(np.unique(ko)) == len(ko) and len(np.unique(km)) == len(km)
    order = np.argsort(ko)
    a2o = order[np.searchsorted(ko, ka, sorter=order)]
    m2o = order[np.searchsorted(ko, km, sorter=order)]
    assert np.all(ko[a2o] == ka) and np.all(ko[m2o] == km), "event fingerprint join failed"
    assert np.all(a2o == np.arange(len(ka))), "our run.root is not in entry order"
    o2m = np.full(len(ko), -1, np.int64)
    o2m[m2o] = np.arange(len(km))
    log("alignment OK: ours identity, master permuted, %d common events" % int((o2m >= 0).sum()))

    ot = uproot.open(OURS)["tree"]
    mt = uproot.open(MAST)["tree"]
    OB = ["sim_pt", "sim_eta", "sim_vx", "sim_vy", "sim_vz", "sim_q", "sim_tcIdx",
          "sim_trkNtupIdx", "tc_type", "tc_hitIdx", "tc_hitType"]
    MB = ["sim_tcIdx", "sim_trkNtupIdx", "tc_type"]
    od = ot.arrays(OB, entry_stop=nent, library="ak")
    md = mt.arrays(MB, library="ak")

    tree = uproot.open(NTUPLE)["trackingNtuple/tree"]
    itc = iter_chains_hits(os.path.join(DUMP, "chains.bin"))
    itj = iter_join(os.path.join(DUMP, "join.bin"))
    itp = iter_pairs_light(os.path.join(DUMP, "pairs.bin"))

    acc = {k: np.zeros(NB, np.int64) for k in BUCKETS}
    acc["den"] = np.zeros(NB, np.int64)
    # cross-cutting: is the true pair the chain's top-logit pair among ALL its scored pairs?
    acc["notglobalargmax"] = np.zeros(NB, np.int64)
    acc["scored_den"] = np.zeros(NB, np.int64)
    margins = [[] for _ in range(NB)]        # ARGMAXLOST: winner logit - best true logit
    gmargins = [[] for _ in range(NB)]       # global-argmax margin over ALL scored true pairs
    shortfall = [[] for _ in range(NB)]      # BELOWBAR: bar - best true logit
    sf_pt = [[] for _ in range(NB)]          # the same sims' pt, for the pt profile
    allpt = [[] for _ in range(NB)]          # every deficit sim's pt
    conten = {"same": np.zeros(NB, np.int64), "other": np.zeros(NB, np.int64),
              "fake": np.zeros(NB, np.int64)}
    conten_gap = [[] for _ in range(NB)]
    examples = {k: [] for k in BUCKETS}
    rec = []

    ent = 0
    for batch in tree.iterate(BR, library="ak", step_size=20, entry_stop=nent):
        for b in range(len(batch["sim_pt"])):
            rc = next(itc)
            rj = next(itj)
            rp = next(itp)
            ievtC, nC, nn, nodeItems, nlay, flg, dca, hoff, chhits = rc
            ievtJ, nCj, nT3j, nodes, plsrec = rj
            hdr, st, tg, pl, lg = rp
            assert ievtC == ent and ievtJ == ent and hdr["ievt"] == ent
            assert nCj == hdr["nChains"] == nC and hdr["nDrop"] == 0 and hdr["dsA"] == 1
            mrow = int(o2m[ent])
            if mrow < 0:
                ent += 1
                continue

            # ---- the deficit population ---------------------------------------------------
            osim_tc = ak.to_numpy(od["sim_tcIdx"][ent]).astype(np.int64)
            oty = ak.to_numpy(od["tc_type"][ent]).astype(np.int64)
            msim_tc = ak.to_numpy(md["sim_tcIdx"][mrow]).astype(np.int64)
            mty = ak.to_numpy(md["tc_type"][mrow]).astype(np.int64)
            ontup = ak.to_numpy(od["sim_trkNtupIdx"][ent]).astype(np.int64)
            mntup = ak.to_numpy(md["sim_trkNtupIdx"][mrow]).astype(np.int64)
            assert len(ontup) == len(mntup) and np.array_equal(ontup, mntup)
            spt = ak.to_numpy(od["sim_pt"][ent]).astype(np.float64)
            seta = ak.to_numpy(od["sim_eta"][ent]).astype(np.float64)
            svz = ak.to_numpy(od["sim_vz"][ent]).astype(np.float64)
            svx = ak.to_numpy(od["sim_vx"][ent]).astype(np.float64)
            svy = ak.to_numpy(od["sim_vy"][ent]).astype(np.float64)
            sq = ak.to_numpy(od["sim_q"][ent]).astype(np.int64)
            core = ((spt > 0.9) & (np.abs(svz) < 30) & (np.hypot(svx, svy) < 2.5)
                    & (sq != 0) & (np.abs(seta) < 2.5))
            ourT5 = (osim_tc >= 0) & core
            ourT5[ourT5] = oty[osim_tc[ourT5]] == 4
            masP5 = (msim_tc >= 0) & core
            masP5[masP5] = mty[msim_tc[masP5]] == 7
            deficit = np.flatnonzero(ourT5 & masP5)
            if len(deficit) == 0:
                ent += 1
                continue
            bidx = np.clip(np.digitize(np.abs(seta), EDGES) - 1, 0, NB - 1)

            # ---- our stage-A pair stream ---------------------------------------------------
            ph2c = ak.to_numpy(ak.num(batch["ph2_simHitIdx"][b])).astype(np.int64)
            ph2f = ak.to_numpy(ak.flatten(batch["ph2_simHitIdx"][b])).astype(np.int64)
            ph2o = np.concatenate(([0], np.cumsum(ph2c)))[:-1]
            pixc = ak.to_numpy(ak.num(batch["pix_simHitIdx"][b])).astype(np.int64)
            pixf = ak.to_numpy(ak.flatten(batch["pix_simHitIdx"][b])).astype(np.int64)
            pixo = np.concatenate(([0], np.cumsum(pixc)))[:-1]
            sht = ak.to_numpy(batch["simhit_simTrkIdx"][b]).astype(np.int64)
            seept = ak.to_numpy(batch["see_pt"][b]).astype(np.float64)

            nP = len(plsrec)
            peta = np.abs(np.asarray(plsrec["eta"]).astype(np.float64))
            pptin = seept[plsrec["seed"].astype(np.int64)]
            # The displaced row is a TARGET property, so under the shipped model the bar is per
            # PAIR, not per seed. NaN-rejecting form, matching ChainAttachTargetPre.
            dispChain = ~(dca < NEW_DCASPLIT)

            sA = st == 0
            tgA, plA, lgA = tg[sA], pl[sA], lg[sA].astype(np.float64)
            targeted = np.zeros(nC, bool)
            targeted[np.unique(tg[(st == 0) | (st == 2)])] = True
            barA = barfn(peta[plA], pptin[plA], dispChain[tgA])
            passA = lgA >= barA

            # per-chain pick among ABOVE-BAR rows (deployed order: bar, then argmax)
            pickPls = np.full(nC, -1, np.int64)
            pickLog = np.full(nC, -np.inf)
            if passA.any():
                t2, p2, l2 = tgA[passA], plA[passA], lgA[passA]
                o2 = np.lexsort((p2, -l2, t2))
                t2s, p2s, l2s = t2[o2], p2[o2], l2[o2]
                first = np.ones(len(t2s), bool)
                first[1:] = t2s[1:] != t2s[:-1]
                pickPls[t2s[first]] = p2s[first]
                pickLog[t2s[first]] = l2s[first]
            # seed contention among the picks
            winner = np.full(nP, -1, np.int64)
            winLog = np.full(nP, -np.inf)
            pc = np.flatnonzero(pickPls >= 0)
            if len(pc):
                wp, wl = pickPls[pc], pickLog[pc]
                o3 = np.lexsort((pc, -wl, wp))
                wps, wls, wcs = wp[o3], wl[o3], pc[o3]
                f3 = np.ones(len(wps), bool)
                f3[1:] = wps[1:] != wps[:-1]
                winner[wps[f3]] = wcs[f3]
                winLog[wps[f3]] = wls[f3]

            # per-chain global argmax over ALL scored rows (bar-blind), for the NN-ranking question
            gPls = np.full(nC, -1, np.int64)
            gLog = np.full(nC, -np.inf)
            if len(tgA):
                o4 = np.lexsort((plA, -lgA, tgA))
                t4, p4, l4 = tgA[o4], plA[o4], lgA[o4]
                f4 = np.ones(len(t4), bool)
                f4[1:] = t4[1:] != t4[:-1]
                gPls[t4[f4]] = p4[f4]
                gLog[t4[f4]] = l4[f4]

            # ---- TC -> chain, hit-exact -----------------------------------------------------
            lut = {}
            for c in range(nC):
                lut.setdefault(np.unique(chhits[hoff[c]:hoff[c + 1]]).tobytes(), []).append(c)

            # ---- pLS sim sets (truth join) ---------------------------------------------------
            ploff, plsims = build_pls_simsets(batch, b, plsrec, ph2f, ph2c, ph2o,
                                              pixf, pixc, pixo, sht)
            plcnt = np.diff(ploff)
            pown = np.repeat(np.arange(nP, dtype=np.int64), plcnt)
            ordp = np.argsort(plsims, kind="stable")
            ps, po = plsims[ordp], pown[ordp]

            need_chain_sets = False
            per = []
            for srow in deficit:
                sim = int(ontup[srow])
                bi = int(bidx[srow])
                tcrow = int(osim_tc[srow])
                hi = np.asarray(od["tc_hitIdx"][ent][tcrow]).astype(np.uint32)
                ht = np.asarray(od["tc_hitType"][ent][tcrow]).astype(np.int64)
                cands = lut.get(np.unique(hi[ht == 4]).tobytes(), [])
                lo2, hi2 = np.searchsorted(ps, sim, "left"), np.searchsorted(ps, sim, "right")
                tp = po[lo2:hi2]
                per.append((srow, sim, bi, cands, tp))
                if len(cands) == 1 and len(tp):
                    need_chain_sets = True
            choff = chsims = None
            if need_chain_sets:
                choff, chsims = build_chain_simsets(nodes, nodeItems, nn, nC,
                                                    ph2f, ph2c, ph2o, sht)

            for srow, sim, bi, cands, tp in per:
                acc["den"][bi] += 1
                allpt[bi].append(float(spt[srow]))
                r = dict(evt=ent, sim=sim, band=bi, pt=float(spt[srow]), eta=float(seta[srow]))

                def emit(bucket, **kw):
                    acc[bucket][bi] += 1
                    r["bucket"] = bucket
                    r.update(kw)
                    if len(examples[bucket]) < 12:
                        examples[bucket].append(dict(r))
                    rec.append(r)

                if len(cands) != 1:
                    emit("OTHER", why="TC->chain map ambiguous (%d)" % len(cands))
                    continue
                c = cands[0]
                if nlay[c] < 5:
                    emit("NOLAYERS", nlay=int(nlay[c]))
                    continue
                if dca[c] >= ATTACH_DCA_MAX:
                    emit("DCASENT", dca=float(dca[c]))
                    continue
                if len(tp) == 0:
                    emit("NOPLS")
                    continue
                mtrue = np.isin(plA, tp) & (tgA == c)
                if not mtrue.any():
                    emit("NOTSCORED", targeted=bool(targeted[c]), npls=int(len(tp)),
                         nrows_chain=int((tgA == c).sum()))
                    continue
                tl = lgA[mtrue]
                tpl = plA[mtrue]
                tbar = barA[mtrue]
                acc["scored_den"][bi] += 1
                # the NN-ranking question, independent of the bar
                gm = float(gLog[c] - tl.max())
                gmargins[bi].append(gm)
                if int(gPls[c]) not in set(tpl.tolist()):
                    acc["notglobalargmax"][bi] += 1
                above = tl >= tbar
                if not above.any():
                    sf = float(np.min(tbar - tl))
                    shortfall[bi].append(sf)
                    sf_pt[bi].append(float(spt[srow]))
                    j = int(np.argmin(tbar - tl))
                    p = int(tpl[j])
                    emit("BELOWBAR", shortfall=sf, bar=float(tbar[j]),
                         logit=float(tl[j]), seedeta=float(peta[p]), seedpt=float(pptin[p]),
                         disp=bool(dispChain[c]), dca=float(dca[c]))
                    continue
                best = float(tl[above].max())
                if int(pickPls[c]) not in set(tpl[above].tolist()):
                    mg = float(pickLog[c] - best)
                    margins[bi].append(mg)
                    emit("ARGMAXLOST", margin=mg, truelogit=best,
                         winlogit=float(pickLog[c]), winpls=int(pickPls[c]))
                    continue
                p = int(pickPls[c])
                w = int(winner[p])
                if w == c:
                    emit("OTHER", why="won the seed in replay but emitted bare",
                         pls=p, logit=float(pickLog[c]))
                    continue
                gap = float(winLog[p] - pickLog[c])
                conten_gap[bi].append(gap)
                same = False
                if choff is not None:
                    same = sim in chsims[choff[w]:choff[w + 1]]
                real = False
                if choff is not None:
                    real = (choff[w + 1] - choff[w]) > 0
                if same:
                    conten["same"][bi] += 1
                elif real:
                    conten["other"][bi] += 1
                else:
                    conten["fake"][bi] += 1
                emit("CONTENDLOST", gap=gap, winner_same_sim=bool(same),
                     winner_has_sim=bool(real), winchain=w, pls=p)
            ent += 1
        if ent % 25 == 0:
            log("evt %d  den %s  belowbar %s argmaxlost %s notscored %s"
                % (ent, acc["den"].tolist(), acc["BELOWBAR"].tolist(),
                   acc["ARGMAXLOST"].tolist(), acc["NOTSCORED"].tolist()))

    def q(a, ps=(10, 25, 50, 75, 90)):
        return [float(np.percentile(a, p)) for p in ps] if len(a) else []

    out = dict(
        nevents=ent, bands=BANDS,
        den=acc["den"].tolist(),
        buckets={k: acc[k].tolist() for k in BUCKETS},
        scored_den=acc["scored_den"].tolist(),
        not_global_argmax=acc["notglobalargmax"].tolist(),
        argmax_margin_q=[q(m) for m in margins],
        argmax_margin_n=[len(m) for m in margins],
        global_margin_q=[q(m) for m in gmargins],
        global_margin_gt0=[float(np.mean(np.asarray(m) > 0)) if m else -1 for m in gmargins],
        shortfall_q=[q(s) for s in shortfall],
        shortfall_le3=[int(np.sum(np.asarray(s) <= 3.0)) if s else 0 for s in shortfall],
        shortfall_le1=[int(np.sum(np.asarray(s) <= 1.0)) if s else 0 for s in shortfall],
        shortfall_n=[len(s) for s in shortfall],
        # scale-VALID recovery curve: fraction of the BELOWBAR sims a uniform relaxation of the
        # dump's OWN bar by delta would put above bar (the head is unchanged, so the units are).
        relax_delta=[0.5, 1.0, 1.5, 2.0, 3.0, 5.0],
        relax_recovered=[[int(np.sum(np.asarray(s) <= d)) if s else 0
                          for d in (0.5, 1.0, 1.5, 2.0, 3.0, 5.0)] for s in shortfall],
        # pt profile of the deficit and of the shortfall
        pt_bins=[0.9, 2, 5, 10, 25, 50, 1e9],
        den_by_pt=[np.histogram(a, bins=[0.9, 2, 5, 10, 25, 50, 1e9])[0].tolist() if a else []
                   for a in allpt],
        shortfall_by_pt=[[float(np.median(np.asarray(s)[(np.asarray(p) >= lo) & (np.asarray(p) < hi)]))
                          if s and np.any((np.asarray(p) >= lo) & (np.asarray(p) < hi)) else -1
                          for lo, hi in zip([0.9, 2, 5, 10, 25, 50], [2, 5, 10, 25, 50, 1e9])]
                         for s, p in zip(shortfall, sf_pt)],
        contention={k: v.tolist() for k, v in conten.items()},
        contention_gap_q=[q(g) for g in conten_gap],
        examples=examples,
        bar_model=model, dump=DUMP,
    )
    print(json.dumps({k: v for k, v in out.items() if k != "examples"}, indent=1))
    with open(pref + ".json", "w") as f:
        json.dump(out, f, indent=1)
    np.save(pref + "_rows.npy", np.array(
        [(r["evt"], r["sim"], r["band"], r["pt"], r["eta"], BUCKETS.index(r["bucket"]))
         for r in rec],
        dtype=[("evt", "i4"), ("sim", "i4"), ("band", "i1"), ("pt", "f4"), ("eta", "f4"),
               ("bucket", "i1")]))
    log("wrote %s.json (%d deficit sims)" % (pref, len(rec)))


if __name__ == "__main__":
    main()
