#!/usr/bin/env python3
"""Build the labelled attach-pair corpus that the attach-head trainer consumes.

Every attach pair scored by the deployed binary is joined back to the tracking ntuple, given a
truth label and the kinematics of the matched sim track, and written out as flat .npy columns.

THE LABEL.  A pair of (target, pLS) is true when the two objects share at least one sim track:

    label[pair] = 1  iff  the intersection of sims(target) and sims(pLS) is NON-EMPTY

where the sim set of each object is built from the production hit-to-sim matcher at matchfrac 0.75:

  sims(MD)      the deduped sim list of the MD's 2 hits. For a 2-hit object frac > 0.75 means
                frac == 1, so a sim is in the MD's set only if it is on BOTH hits.
  sims(T3)      a sim is in the triplet's set iff it appears in at least 2 of the triplet's 3
                deduped MD sets.
  sims(chain)   the INTERSECTION of sims(T3) over ALL member triplets of the chain, taken over the
                POST-TRIM node list.
  sims(bareT3)  for a bare-triplet target, that triplet's own sims(T3).
  sims(pLS)     the deduped sim list of the pLS's own 3 or 4 hits, the same matcher at 0.75, so
                again every hit must carry the sim.
  kinematics    taken from the highest-sim_pt ACCEPTED sim in the intersection (the accepted sims
                are the in-time, hard-scatter prefix of the sim collection). A pair matched ONLY to
                pileup sims keeps label 1 and carries simVxy = simPt = -999; that sentinel is part
                of the label convention, not a failure, and downstream code has to treat it as one.

THE JOIN, asserted rather than matched.  Four inputs are walked in lockstep, one record per event:
  pairs.bin   one record per SCORED attach pair: stage, target, pls, the deployed head's own logit
              on that row, and the standardized feature row x. The row WIDTH is taken from the
              record header (kAttachFeatures), never assumed, so a pair dump from a build with a
              different attach input count is read correctly instead of being silently mis-sliced.
  chains.bin  record magic 'P22C'; per chain the pre-trim node run plus trimAction, from which this
              file recovers the post-trim nodeItems, and the three raw gate logits.
  join.bin    record magic 'PJ01'; per chain node the sparse triplet index and its 3 MDs'
              (anchor, other) ph2 hit rows; per pLS the tracking-ntuple see_* row and its eta.
  the ntuple  ph2/pix simhit lists, simhit_simTrkIdx, see_hitIdx/see_hitType, sim kinematics.
The three sidecars are written by the same single-stream run, so record i of each is entry i of the
ntuple; the per-record (nChains, nT3) fingerprint and the event index are asserted, so a misaligned
set of files fails loudly instead of producing plausible nonsense.

Inputs -- the three binaries are sidecars of a run with the dump variables set, not files in this
repository. Produce them with a SINGLE-STREAM run (record i must be entry i):

    LST_CHAIN_PAIR_DUMP=pairs.bin LST_CHAIN_JOIN_DUMP=join.bin LST_CHAIN_CHAIN_DUMP=chains.bin \
    LST_CHAIN_PAIR_CAP=2000000 lst_cpu -i <ntuple> -n <N> -p 0.8 -s 1 -w 1 -o run.root

The tracking ntuple is an external file; the default NTUPLE path below is an absolute path on the
machine this was run on, and is overridden by the last positional argument. The sidecar readers are
`pair_dump_io` and `join_dump_io`, beside this file.

Output: one set of flat .npy columns in <outdir> -- X, y, st, vxy, spt, peta, lgt, evt, z3, and on
a format-2 pair dump also probe -- all row-aligned with pairs.bin's own row order, plus
labstats.npz with the run counters. `probe` holds the probe columns, which are NOT head inputs: it
is a separate array so a trainer opts into them explicitly.

Run:
  python3 label_attach.py <chains.bin> <join.bin> <pairs.bin> <outdir> [nEntries] [nRowsTotal] \
      [ntuple.root]
nEntries defaults to 1000. nRowsTotal is the total pair-row count and is REQUIRED in practice: the
output columns are memory-mapped at fixed length, so it has to be known before the first batch is
written. Get it with `python3 pair_dump_io.py --rows <pairs.bin>`.
"""
import os
import sys
import time

import awkward as ak
import numpy as np
import uproot

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
from join_dump_io import iter_events as iter_join      # noqa: E402
from pair_dump_io import read_events as iter_pairs     # noqa: E402

T0 = time.time()
BR = ["ph2_simHitIdx", "pix_simHitIdx", "simhit_simTrkIdx", "see_hitIdx", "see_hitType",
      "sim_pt", "sim_bunchCrossing", "sim_event", "sim_parentVtxIdx", "simvtx_x", "simvtx_y",
      "sim_pca_dxy"]
WPAD = 6          # sim-set pad width; overflow is COUNTED, never silently dropped
# Default ntuple; argv[7] overrides it, so one labeller serves every sample without the label
# definition changing.
NTUPLE = "/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/event_1000.root"


def log(m):
    print("[%7.1fs] %s" % (time.time() - T0, m), flush=True)


# ---------------------------------------------------------------------------------------------
# chains.bin reader. Unlike the generic sidecar readers it also returns the member node list and
# the trim action, which is what the chain sim set is built from.
import struct  # noqa: E402

CMAGIC = 0x50323243
NFEAT = 25
CFIX = struct.Struct("<IIIIiiI" + "f" * (8 + NFEAT))
CHDR = struct.Struct("<IIIII")


def iter_chains(path):
    """Yield (ievt, nC, nNodes_per_chain, nodeItems_flat, z3): POST-TRIM node lists + gate logits."""
    f = open(path, "rb")
    while True:
        h = f.read(20)
        if len(h) < 20:
            break
        magic, ievt, nN, nE, nC = CHDR.unpack(h)
        assert magic == CMAGIC
        nn = np.empty(nC, dtype=np.int64)
        z3 = np.empty((nC, 3), dtype=np.float32)
        chunks = []
        for c in range(nC):
            v = CFIX.unpack(f.read(CFIX.size))
            preN, n, m, drop = v[0], v[1], v[2], v[5]
            drop = drop if drop < 2**31 else drop - 2**32
            z3[c] = (v[9], v[10], v[11])     # zFake, zPrompt, zDisp
            items = np.frombuffer(f.read(4 * preN), dtype="<u4")
            f.read(4 * max(preN - 1, 0))    # edgeType
            f.read(8 * m)                   # hit pairs
            # The dump carries the PRE-trim node run. The post-trim run starts one node later when
            # the trim dropped the inner node, and at the head of the list otherwise.
            s = 1 if drop == 1 else 0
            chunks.append(items[s:s + n])
            nn[c] = n
        yield ievt, nC, nn, (np.concatenate(chunks) if chunks else np.zeros(0, "<u4")), z3
    f.close()


# ---------------------------------------------------------------------------------------------
def hit_sim_csr(rows, sh_flat, sh_cnt, sh_off, sht):
    """For a set of hit rows, return (uniq_rows, off, sims) : the DEDUPED sim list per unique row.

    `rows` need not be unique. Returns uniq_rows sorted ascending plus a CSR of its sim lists.
    """
    ur = np.unique(rows)
    if len(ur) == 0:
        return ur, np.zeros(1, np.int64), np.zeros(0, np.int64)
    cnts = sh_cnt[ur]
    tot = int(cnts.sum())
    if tot == 0:
        return ur, np.zeros(len(ur) + 1, np.int64), np.zeros(0, np.int64)
    starts = sh_off[ur]
    outoff = np.concatenate(([0], np.cumsum(cnts)[:-1]))
    ar = np.arange(tot, dtype=np.int64) - np.repeat(outoff, cnts)
    sims = sh_flat[np.repeat(starts, cnts) + ar]
    sims = np.where((sims >= 0) & (sims < len(sht)), sht[np.clip(sims, 0, len(sht) - 1)], -1)
    owner = np.repeat(np.arange(len(ur), dtype=np.int64), cnts)
    ok = sims >= 0
    owner, sims = owner[ok], sims[ok]
    # dedupe (row, sim)
    k = owner * (1 << 21) + sims
    k = np.unique(k)
    owner = k >> 21
    sims = k & ((1 << 21) - 1)
    off = np.zeros(len(ur) + 1, np.int64)
    np.cumsum(np.bincount(owner, minlength=len(ur)), out=off[1:])
    return ur, off, sims


def group_threshold(gid, sims, ngroups, need):
    """(gid, sims) pairs -> CSR sim list per group, keeping sims whose count >= need[gid].

    `need` may be a scalar or an array of length ngroups.  Input pairs must already be deduped on
    (gid, member, sim); this counts MEMBERS per (gid, sim).
    """
    if len(gid) == 0:
        return np.zeros(ngroups + 1, np.int64), np.zeros(0, np.int64)
    k = gid * (1 << 21) + sims
    k.sort()
    uk, cc = np.unique(k, return_counts=True)
    ug = uk >> 21
    us = uk & ((1 << 21) - 1)
    nd = need if np.isscalar(need) else need[ug]
    keep = cc >= nd
    ug, us = ug[keep], us[keep]
    off = np.zeros(ngroups + 1, np.int64)
    np.cumsum(np.bincount(ug, minlength=ngroups), out=off[1:])
    order = np.argsort(ug, kind="stable")
    return off, us[order]


def pad_sets(off, sims, n, w=WPAD):
    """CSR -> (n, w) int32 padded with -1.  Returns (pad, n_overflow_objects)."""
    out = np.full((n, w), -1, dtype=np.int32)
    cnt = np.diff(off)
    nov = int((cnt > w).sum())
    take = np.minimum(cnt, w)
    if len(sims):
        gid = np.repeat(np.arange(n, dtype=np.int64), take)
        slot = np.arange(len(gid), dtype=np.int64) - np.repeat(
            np.concatenate(([0], np.cumsum(take)[:-1])), take)
        src = np.repeat(off[:-1], take) + slot
        out[gid, slot] = sims[src]
    return out, nov


def main():
    global NTUPLE
    chains_bin, join_bin, pairs_bin, outdir = sys.argv[1:5]
    nent = int(sys.argv[5]) if len(sys.argv) > 5 else 1000
    ntot = int(sys.argv[6]) if len(sys.argv) > 6 else None
    if len(sys.argv) > 7:
        NTUPLE = sys.argv[7]
    log("ntuple %s" % NTUPLE)
    os.makedirs(outdir, exist_ok=True)

    tree = uproot.open(NTUPLE)["trackingNtuple/tree"]
    assert tree.num_entries >= nent

    itc = iter_chains(chains_bin)
    itj = iter_join(join_bin)
    itp = iter_pairs(pairs_bin)

    cols = None
    pos = 0
    ent = 0
    stats = dict(nov_chain=0, nov_t3=0, nov_pls=0, ntrue=0, nrow=0, nmiss_t3=0)

    for batch in tree.iterate(BR, library="ak", step_size=20, entry_stop=nent):
        nb = len(batch["sim_pt"])
        for b in range(nb):
            rc = next(itc); rj = next(itj); rp = next(itp)
            ievtC, nC, nn, nodeItems, cz3 = rc
            ievtJ, nCj, nT3j, nodes, plsrec = rj
            hdr, rows = rp
            assert ievtC == ent and ievtJ == ent and hdr["ievt"] == ent, \
                "ALIGNMENT: record %d carries ievt %d/%d/%d" % (ent, ievtC, ievtJ, hdr["ievt"])
            assert nCj == hdr["nChains"] == nC and nT3j == hdr["nT3"], \
                "FINGERPRINT: (nChains,nT3) %d/%d vs pairs %d/%d vs chains %d" % (
                    nCj, nT3j, hdr["nChains"], hdr["nT3"], nC)
            assert hdr["nDrop"] == 0

            # ---- ntuple side ---------------------------------------------------------------
            ph2c = ak.to_numpy(ak.num(batch["ph2_simHitIdx"][b])).astype(np.int64)
            ph2f = ak.to_numpy(ak.flatten(batch["ph2_simHitIdx"][b])).astype(np.int64)
            ph2o = np.concatenate(([0], np.cumsum(ph2c)))[:-1]
            pixc = ak.to_numpy(ak.num(batch["pix_simHitIdx"][b])).astype(np.int64)
            pixf = ak.to_numpy(ak.flatten(batch["pix_simHitIdx"][b])).astype(np.int64)
            pixo = np.concatenate(([0], np.cumsum(pixc)))[:-1]
            sht = ak.to_numpy(batch["simhit_simTrkIdx"][b]).astype(np.int64)
            bx = ak.to_numpy(batch["sim_bunchCrossing"][b])
            se = ak.to_numpy(batch["sim_event"][b])
            accmask = (bx == 0) & (se == 0)
            nacc = int(accmask.sum())
            assert accmask[:nacc].all(), "accepted sims not a prefix at entry %d" % ent
            spt = ak.to_numpy(batch["sim_pt"][b]).astype(np.float64)
            nsim = len(bx)
            # rank: unique integer per ACCEPTED sim, ascending in sim_pt, -1 for pileup-only.
            rank = np.full(nsim, -1, dtype=np.int32)
            order = np.argsort(spt[:nacc], kind="stable")
            rank[:nacc][order] = np.arange(nacc, dtype=np.int64)
            rank2sim = np.full(max(nacc, 1), -1, dtype=np.int64)
            if nacc:
                rank2sim[np.arange(nacc)] = order

            # ---- node MD sim sets ----------------------------------------------------------
            nN = len(nodes)
            t3idx = nodes[:, 0].astype(np.int64)
            h = nodes[:, 1:].astype(np.int64)              # (nN, 6): a0,b0,a1,b1,a2,b2
            ur, uoff, usims = hit_sim_csr(h.ravel(), ph2f, ph2c, ph2o, sht)
            pos_in_ur = np.searchsorted(ur, h)             # (nN,6) index into ur
            # an MD is (2*k, 2*k+1); unique-hit count is 1 when the two rows coincide
            mdA = pos_in_ur[:, 0::2].ravel()               # (nN*3,)
            mdB = pos_in_ur[:, 1::2].ravel()
            same = mdA == mdB
            nuniq = np.where(same, 1, 2)
            mdid = np.arange(nN * 3, dtype=np.int64)
            # (mdid, whichHit, sim) pairs, already deduped per (row, sim) by hit_sim_csr
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
            if gid_l:
                g = np.concatenate(gid_l); s = np.concatenate(sim_l); mm = np.concatenate(mem_l)
                # drop the duplicate side when both rows are the same hit
                ok = ~(same[g] & (mm == 1))
                g, s = g[ok], s[ok]
                mdoff, mdsims = group_threshold(g, s, nN * 3, nuniq)
            else:
                mdoff, mdsims = np.zeros(nN * 3 + 1, np.int64), np.zeros(0, np.int64)

            # ---- T3 sim set: sim in >= 2 of the node's 3 MD sets ---------------------------
            mdcnt = np.diff(mdoff)
            gmd = np.repeat(np.arange(nN * 3, dtype=np.int64), mdcnt)
            t3off, t3sims = group_threshold(gmd // 3, mdsims, nN, 2)

            # ---- chain sim set: intersection over the chain's post-trim nodes --------------
            t3cnt = np.diff(t3off)
            if len(nodeItems):
                ni = nodeItems.astype(np.int64)
                cid = np.repeat(np.arange(nC, dtype=np.int64), nn)
                cc = t3cnt[ni]
                tot = int(cc.sum())
                if tot:
                    outo = np.concatenate(([0], np.cumsum(cc)[:-1]))
                    ar = np.arange(tot, dtype=np.int64) - np.repeat(outo, cc)
                    src = np.repeat(t3off[ni], cc) + ar
                    choff, chsims = group_threshold(np.repeat(cid, cc), t3sims[src], nC,
                                                    np.maximum(nn, 1))
                else:
                    choff, chsims = np.zeros(nC + 1, np.int64), np.zeros(0, np.int64)
            else:
                choff, chsims = np.zeros(nC + 1, np.int64), np.zeros(0, np.int64)

            # ---- pLS sim set ---------------------------------------------------------------
            nP = len(plsrec)
            seeds = plsrec["seed"].astype(np.int64)
            shi = batch["see_hitIdx"][b]
            sht_t = batch["see_hitType"][b]
            slen = ak.to_numpy(ak.num(shi)).astype(np.int64)
            shiF = ak.to_numpy(ak.flatten(shi)).astype(np.int64)
            shtF = ak.to_numpy(ak.flatten(sht_t)).astype(np.int64)
            soff = np.concatenate(([0], np.cumsum(slen)))[:-1]
            L = slen[seeds]
            assert (L >= 3).all()
            # Same hit selection LSTPrepareInput.h makes when it builds a pLS: see_hitIdx[0], [1]
            # and [2], plus the last hit when the seed has more than three.
            sel = np.stack([np.zeros(nP, np.int64), np.ones(nP, np.int64),
                            np.full(nP, 2, np.int64), L - 1], axis=1)
            use = np.stack([np.ones(nP, bool), np.ones(nP, bool),
                            np.ones(nP, bool), L > 3], axis=1)
            gsrc = soff[seeds][:, None] + sel
            hidx = shiF[gsrc]
            htyp = shtF[gsrc]
            pid = np.repeat(np.arange(nP, dtype=np.int64), 4).reshape(nP, 4)
            m = use
            pid_f, hidx_f, htyp_f = pid[m], hidx[m], htyp[m]
            # unique (pls, hitidx, hittype) -- the matcher dedupes on the (idx, type) pair
            assert hidx_f.max(initial=0) < (1 << 23), "hit row overflows the pLS pack"
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
                ploff, plsims = group_threshold(np.concatenate(gid_l), np.concatenate(sim_l),
                                                nP, nuniqP)
            else:
                ploff, plsims = np.zeros(nP + 1, np.int64), np.zeros(0, np.int64)

            # ---- pad, then label every pair ------------------------------------------------
            chpad, ov1 = pad_sets(choff, chsims, nC)
            t3pad, ov2 = pad_sets(t3off, t3sims, nN)
            plpad, ov3 = pad_sets(ploff, plsims, nP)
            stats["nov_chain"] += ov1; stats["nov_t3"] += ov2; stats["nov_pls"] += ov3

            # Sparse triplet index -> dense node row. A bare-T3 target is named by its TRIPLET index
            # in the pair record, but its hits are carried on the chain NODE, so the sim set has to
            # be looked up through this map. A target with no node row is counted, not assumed.
            t3map = np.full(int(nT3j), -1, dtype=np.int64)
            t3map[t3idx] = np.arange(nN, dtype=np.int64)

            st = rows["stage"].astype(np.int64)
            tg = rows["target"].astype(np.int64)
            pl = rows["pls"].astype(np.int64)
            nr = len(rows)
            T = np.full((nr, WPAD), -1, dtype=np.int32)
            isch = (st == 0) | (st == 2)
            if isch.any():
                T[isch] = chpad[tg[isch]]
            ist3 = st == 1
            if ist3.any():
                nd = t3map[tg[ist3]]
                stats["nmiss_t3"] += int((nd < 0).sum())
                T[ist3] = np.where((nd >= 0)[:, None], t3pad[np.maximum(nd, 0)], -1)
            P = plpad[pl]

            match = (T[:, :, None] == P[:, None, :]) & (T[:, :, None] >= 0)
            lab = match.any(axis=(1, 2))
            # kinematics from the highest-sim_pt ACCEPTED sim in the intersection
            rk = np.where(match, rank[np.clip(T, 0, nsim - 1)][:, :, None], -1)
            bestrank = rk.reshape(nr, -1).max(axis=1)
            vxy = np.full(nr, -999.0, np.float32)
            sptv = np.full(nr, -999.0, np.float32)
            good = bestrank >= 0
            if good.any():
                si = rank2sim[bestrank[good]]
                pvi = ak.to_numpy(batch["sim_parentVtxIdx"][b]).astype(np.int64)
                svx = ak.to_numpy(batch["simvtx_x"][b]).astype(np.float64)
                svy = ak.to_numpy(batch["simvtx_y"][b]).astype(np.float64)
                v = pvi[si]
                gv = (v >= 0) & (v < len(svx))
                tmp = np.full(len(si), -999.0)
                tmp[gv] = np.hypot(svx[v[gv]], svy[v[gv]])
                vxy[good] = tmp
                sptv[good] = spt[si]

            # ---- append --------------------------------------------------------------------
            if cols is None:
                assert ntot is not None, "pass the total row count as argv[6]"
                cols = dict(
                    X=np.lib.format.open_memmap(os.path.join(outdir, "X.npy"), mode="w+",
                                                dtype=np.float32, shape=(ntot, int(hdr["nFeat"]))),
                    y=np.lib.format.open_memmap(os.path.join(outdir, "y.npy"), mode="w+",
                                                dtype=np.int8, shape=(ntot,)),
                    st=np.lib.format.open_memmap(os.path.join(outdir, "st.npy"), mode="w+",
                                                 dtype=np.int8, shape=(ntot,)),
                    vxy=np.lib.format.open_memmap(os.path.join(outdir, "vxy.npy"), mode="w+",
                                                  dtype=np.float32, shape=(ntot,)),
                    spt=np.lib.format.open_memmap(os.path.join(outdir, "spt.npy"), mode="w+",
                                                  dtype=np.float32, shape=(ntot,)),
                    peta=np.lib.format.open_memmap(os.path.join(outdir, "peta.npy"), mode="w+",
                                                   dtype=np.float32, shape=(ntot,)),
                    lgt=np.lib.format.open_memmap(os.path.join(outdir, "lgt.npy"), mode="w+",
                                                  dtype=np.float32, shape=(ntot,)),
                    evt=np.lib.format.open_memmap(os.path.join(outdir, "evt.npy"), mode="w+",
                                                  dtype=np.int16, shape=(ntot,)),
                    zf=np.lib.format.open_memmap(os.path.join(outdir, "z3.npy"), mode="w+",
                                                 dtype=np.float32, shape=(ntot, 3)),
                )
                # Format-2 probe columns (currently just dBeta). NOT head inputs -- kept in their
                # own array so a trainer opts in explicitly rather than finding the feature width
                # silently changed under it. Absent on a format-1 dump.
                if int(hdr.get("nProbe", 0)) > 0:
                    cols["pb"] = np.lib.format.open_memmap(
                        os.path.join(outdir, "probe.npy"), mode="w+",
                        dtype=np.float32, shape=(ntot, int(hdr["nProbe"])))
            sl = slice(pos, pos + nr)
            cols["X"][sl] = rows["x"]
            if "pb" in cols:
                cols["pb"][sl] = rows["probe"]
            cols["y"][sl] = lab.astype(np.int8)
            cols["st"][sl] = st.astype(np.int8)
            cols["vxy"][sl] = vxy
            cols["spt"][sl] = sptv
            cols["peta"][sl] = plsrec["eta"][pl]
            cols["lgt"][sl] = rows["logit"]
            cols["evt"][sl] = ent
            # The three RAW 3-class gate logits of the TARGET. A bare T3 has no chain gate, so all
            # three slots take the same 0 sentinel the C++ uses (ChainAttachT3.h feeds 0 into those
            # inputs), with the targetType input flagging the absence.
            z = np.zeros((nr, 3), np.float32)
            if isch.any():
                z[isch] = cz3[tg[isch]]
            cols["zf"][sl] = z
            pos += nr
            stats["nrow"] += nr
            stats["ntrue"] += int(lab.sum())
            ent += 1
        if ent % 20 == 0:
            log("entry %d rows %d true %d (%.4f) t3miss %d ov %d/%d/%d"
                % (ent, stats["nrow"], stats["ntrue"], stats["ntrue"] / max(stats["nrow"], 1),
                   stats["nmiss_t3"], stats["nov_chain"], stats["nov_t3"], stats["nov_pls"]))

    log("DONE entries %d rows %d (expected %s)" % (ent, pos, ntot))
    assert ntot is None or pos == ntot, "row count mismatch"
    np.savez(os.path.join(outdir, "labstats.npz"), **{k: np.int64(v) for k, v in stats.items()},
             nent=np.int64(ent))
    log(str(stats))


if __name__ == "__main__":
    main()
