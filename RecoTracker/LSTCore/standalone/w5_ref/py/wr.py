#!/usr/bin/env python3
"""W5: per-event WELD REPLAY reducer.

usage: wr.py <ievt> <out.root> <dumpdir> <out.npz> [jet|pu]

Everything the weld needs is recoverable offline:
  * edges.bin  (P21E) gives inner / outer / type / logOdds for every kept edge row;
  * feat.bin   (P21F) gives the 40 head inputs, so mP / mD -- and therefore the 80-WP
    eligibility rule, which weldBar materializes and no dump carries -- can be replayed;
  * nodes.bin  (P25N) gives stableId, and ChainEdges::tie is stableId[inner] ^ stableId[outer];
  * chains.bin (P22C) gives the KERNEL's welded pair set (consecutive nodes of the pre-trim run),
    which is what the replay is validated against, per event, exactly.

The weld itself (K6a/K6b, ChainWeld.h) is a fixed number of mutual-best-argmax sweeps over a
frozen snapshot of the weld slots, and is therefore a pure function of the edge rows.  This file
replays it under several ARGMAX KEYS and reports which edges each key welds.

The local-occupancy observable is the SAME one ChainGate's feature 14 uses: the junction's
incidence degrees.  For an E1 edge the junction is the shared middle MD (t3md2[inner] ==
t3md0[outer]); for an E2 edge it is the shared line segment (lsIdx1[inner] == lsIdx0[outer]).
degIn / degOut are the counts of T3s ending / starting at that key, i.e. exactly what the K1c
incidence CSR offsets carry, and degProd = degIn * degOut is feature 14's per-edge summand.
"""
import os
import sys

import numpy as np
import uproot

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import edgew  # noqa: E402
import jio  # noqa: E402

PTCUT, ETACUT, VZCUT, VPCUT = 0.9, 4.5, 30.0, 2.5
JETPT, JETETA = 1000.0, 2.5

BR = ["sim_pt", "sim_eta", "sim_phi", "sim_q", "sim_vx", "sim_vy", "sim_vz", "sim_pca_dxy",
      "sim_tcIdx", "sim_trkNtupIdx", "sim_genjet_idx", "sim_genjet_deltaR", "genjet_pt", "genjet_eta",
      "md_anchorHitIdx", "md_otherHitIdx", "md_eta",
      "ls_mdIdx0", "ls_mdIdx1",
      "t3_pt", "t3_eta", "t3_lsIdx0", "t3_lsIdx1", "t3_simIdx"]


def ordf(x):
    """chainOrderFloat: the monotone float -> uint32 map of ChainWeld.h."""
    b = np.asarray(x, dtype=np.float32).view(np.uint32)
    return np.where(b >> 31, b ^ np.uint32(0xFFFFFFFF), b ^ np.uint32(0x80000000)).astype(np.uint64)


def key_base(lo, tie, fam):
    return (ordf(lo) << np.uint64(32)) | tie.astype(np.uint64)


def key_fam(lo, tie, fam, hi_is_e2=True):
    """Modes 5/6 of the N3 instrument: family lexicographic, the bit taken out of the float's
    last mantissa bit so the 32-bit tie word survives intact."""
    hi = (fam == 2) if hi_is_e2 else (fam == 1)
    w = (ordf(lo) >> np.uint64(1)) | np.where(hi, np.uint64(0x80000000), np.uint64(0))
    return (w << np.uint64(32)) | tie.astype(np.uint64)


def weld_replay(inner, outer, elig, key, nNodes, sweeps=2):
    """K6a/K6b, vectorized.  Returns a bool mask over edge rows: welded or not."""
    n = len(inner)
    outW = np.full(nNodes, False)
    inW = np.full(nNodes, False)
    done = np.zeros(n, dtype=bool)
    for _ in range(sweeps):
        live = elig & ~outW[inner] & ~inW[outer]
        if not live.any():
            break
        bo = np.zeros(nNodes, dtype=np.uint64)
        bi = np.zeros(nNodes, dtype=np.uint64)
        np.maximum.at(bo, inner[live], key[live])
        np.maximum.at(bi, outer[live], key[live])
        # K6b does NOT re-test the weld slots; a node welded earlier has best-key 0 and no key
        # can match it.  Reproduce that exactly: test over ALL eligible rows, not just `live`.
        m = elig & (bo[inner] == key) & (bi[outer] == key) & (key != 0)
        if not m.any():
            break
        done |= m
        outW[inner[m]] = True
        inW[outer[m]] = True
    return done


def main(ievt, outroot, dumpdir, outnpz, kind):
    W = edgew.load()
    t = uproot.open(outroot)["tree"]
    assert t.num_entries == 1, "expected a one-event ntuple, got %d" % t.num_entries
    have = set(t.keys())
    d = t.arrays([b for b in BR if b in have], entry_start=0, entry_stop=1, library="np")
    g = {k: v[0] for k, v in d.items()}

    mda = np.asarray(g["md_anchorHitIdx"], dtype=np.int64)
    mdo = np.asarray(g["md_otherHitIdx"], dtype=np.int64)
    l0 = np.asarray(g["t3_lsIdx0"], dtype=np.int64)
    l1 = np.asarray(g["t3_lsIdx1"], dtype=np.int64)
    lm0 = np.asarray(g["ls_mdIdx0"], dtype=np.int64)
    lm1 = np.asarray(g["ls_mdIdx1"], dtype=np.int64)
    t3md0, t3md1, t3md2 = lm0[l0], lm1[l0], lm1[l1]
    nT3 = len(l0)
    t3sim = np.asarray(g["t3_simIdx"], dtype=np.int64)

    ED = jio.read_edges(os.path.join(dumpdir, "edges.bin"))
    ND = jio.read_nodes(os.path.join(dumpdir, "nodes.bin"))
    CD = jio.read_chains(os.path.join(dumpdir, "chains.bin"))
    FD = jio.read_feat(os.path.join(dumpdir, "feat.bin"))
    assert ED["nNodes"] == nT3 == ND["nNodes"] == FD["nNodes"], "node/T3 count mismatch"
    assert np.array_equal(ED["rec"]["inner"], FD["rec"]["inner"]), "P21E/P21F row order mismatch"

    n6 = ND["hits"].astype(np.int64)
    t6 = np.stack([mda[t3md0], mdo[t3md0], mda[t3md1], mdo[t3md1], mda[t3md2], mdo[t3md2]], 1)
    assert np.array_equal(n6, t6), "dense chain-node order != ntuple t3 order"

    ei = ED["rec"]["inner"].astype(np.int64)
    eo = ED["rec"]["outer"].astype(np.int64)
    et = ED["rec"]["type"].astype(np.int64)
    elo = ED["rec"]["lo"].astype(np.float64)
    nE = len(ei)

    wpbin_node = edgew.wp_bin(np.asarray(g["t3_pt"]), np.asarray(g["md_eta"])[t3md0])

    # ---- replay the head on EVERY edge row ------------------------------------------------
    x = np.concatenate([FD["nf"][ei], FD["nf"][eo], FD["rec"]["ef"]], axis=1).astype(np.float64)
    mP, mD, mX = edgew.margins(x, W)
    rt = float(np.abs(mX - elo).max()) if nE else 0.0
    elig = edgew.eligible(mP, mD, et, wpbin_node[ei], W)
    # the kernel compares the STORED float32 logOdds, so use the dumped column for the key
    lo32 = ED["rec"]["lo"].astype(np.float32)

    sid = ND["stableId"].astype(np.uint32)
    tie = (sid[ei] ^ sid[eo]).astype(np.uint64)

    # ---- junction incidence degrees (ChainGate f[14]'s per-edge summand) -------------------
    md_in = np.bincount(t3md2, minlength=int(max(t3md2.max(), t3md0.max())) + 1)
    md_out = np.bincount(t3md0, minlength=len(md_in))
    md_in = np.pad(md_in, (0, max(0, len(md_out) - len(md_in))))
    ls_in = np.bincount(l1, minlength=int(max(l1.max(), l0.max())) + 1)
    ls_out = np.bincount(l0, minlength=len(ls_in))
    ls_in = np.pad(ls_in, (0, max(0, len(ls_out) - len(ls_in))))
    isE1 = et == 1
    assert np.array_equal(t3md2[ei[isE1]], t3md0[eo[isE1]]), "E1 junction is not the shared MD"
    assert np.array_equal(l1[ei[~isE1]], l0[eo[~isE1]]), "E2 junction is not the shared LS"
    jkey = np.where(isE1, t3md2[ei], l1[ei])
    degIn = np.where(isE1, md_in[np.where(isE1, t3md2[ei], 0)], ls_in[np.where(isE1, 0, l1[ei])])
    degOut = np.where(isE1, md_out[np.where(isE1, t3md2[ei], 0)], ls_out[np.where(isE1, 0, l1[ei])])

    # ---- the kernel's own welded set, for validation ---------------------------------------
    wa, wb, _ = jio.welded_pairs(CD["runs"])
    lut = {}
    for k in range(nE):
        lut[(int(ei[k]), int(eo[k]))] = k
    krows = np.array([lut.get((int(a), int(b)), -1) for a, b in zip(wa.tolist(), wb.tolist())],
                     dtype=np.int64)
    assert (krows >= 0).all(), "a welded pair has no edge row"
    kernel_welded = np.zeros(nE, dtype=bool)
    kernel_welded[krows] = True

    kb = key_base(lo32, tie, et)
    rep_base = weld_replay(ei, eo, elig, kb, nT3, sweeps=2)
    nmis = int((rep_base != kernel_welded).sum())

    ke2 = key_fam(lo32, tie, et, True)
    rep_e2 = weld_replay(ei, eo, elig, ke2, nT3, sweeps=2)

    # ---- truth / sim table ------------------------------------------------------------------
    spt, seta = np.asarray(g["sim_pt"]), np.asarray(g["sim_eta"])
    svx, svy, svz = np.asarray(g["sim_vx"]), np.asarray(g["sim_vy"]), np.asarray(g["sim_vz"])
    sq = np.asarray(g["sim_q"])
    sdxy = np.abs(np.asarray(g["sim_pca_dxy"])) if "sim_pca_dxy" in g else np.zeros(len(spt))
    svxy = np.hypot(svx, svy)
    stc = np.asarray(g["sim_tcIdx"], dtype=np.int64)
    bandsel = (sq != 0) & (np.abs(seta) < ETACUT) & (spt > PTCUT) & (np.abs(svz) < VZCUT)
    if kind == "jet" and "sim_genjet_idx" in g:
        gj = np.asarray(g["sim_genjet_idx"], dtype=np.int64)
        jpt, jeta = np.asarray(g["genjet_pt"]), np.asarray(g["genjet_eta"])
        core = ((sq != 0) & (spt > PTCUT) & (np.abs(seta) < ETACUT) & (np.abs(svz) < VZCUT) &
                (svxy < VPCUT) & (gj >= 0))
        cidx = np.where(core)[0]
        if len(cidx):
            keep = (jpt[gj[cidx]] > JETPT) & (np.abs(jeta[gj[cidx]]) < JETETA)
            core[:] = False
            core[cidx[keep]] = True
    else:
        core = np.zeros(len(spt), dtype=bool)

    trueEdge = (t3sim[ei] >= 0) & (t3sim[ei] == t3sim[eo])
    # t3_simIdx lives in the TRACKING-ntuple sim space; sim_trkNtupIdx maps the LST sim rows into
    # it, so invert that to get the LST row a true edge belongs to (-1 when the sim is not in the
    # LST sim list at all).
    trkn = np.asarray(g["sim_trkNtupIdx"], dtype=np.int64)
    top = int(max(trkn.max() if len(trkn) else 0, t3sim.max() if len(t3sim) else 0)) + 1
    trk2lst = np.full(max(top, 1), -1, dtype=np.int64)
    ok = trkn >= 0
    trk2lst[trkn[ok]] = np.where(ok)[0]
    t3sim_lst = np.where(t3sim >= 0, trk2lst[np.maximum(t3sim, 0)], -1)
    esim = np.where(trueEdge, t3sim_lst[ei], -1)

    np.savez_compressed(
        outnpz, ievt=ievt, kind=np.array(kind), nT3=nT3, nE=nE,
        roundtrip=rt, replay_mismatch=nmis, nKernelWelded=int(kernel_welded.sum()),
        ei=ei.astype(np.int32), eo=eo.astype(np.int32), et=et.astype(np.int8),
        lo=lo32, mP=mP.astype(np.float32), mD=mD.astype(np.float32),
        elig=elig, wpb=wpbin_node[ei].astype(np.int8), tiew=tie.astype(np.uint32),
        stableId=sid,
        degIn=degIn.astype(np.int32), degOut=degOut.astype(np.int32),
        jkey=jkey.astype(np.int32),
        wBase=rep_base, wE2=rep_e2, wKern=kernel_welded,
        trueEdge=trueEdge, esim=esim.astype(np.int32),
        sim_pt=spt.astype(np.float32), sim_eta=seta.astype(np.float32),
        sim_vxy=svxy.astype(np.float32), sim_dxy=sdxy.astype(np.float32),
        sim_tc=stc.astype(np.int32), sim_band=bandsel, sim_core=core,
        t3sim=t3sim_lst.astype(np.int32), t3pt=np.asarray(g["t3_pt"]).astype(np.float32),
        nodewpb=wpbin_node.astype(np.int8),
        t3md0=t3md0.astype(np.int32), t3md1=t3md1.astype(np.int32), t3md2=t3md2.astype(np.int32),
        t3ls0=l0.astype(np.int32), t3ls1=l1.astype(np.int32),
    )
    print("evt %d [%s]: nT3=%d nE=%d elig=%d kernelWeld=%d replayMismatch=%d roundtrip=%.2e "
          "e2Weld=%d" % (ievt, kind, nT3, nE, int(elig.sum()), int(kernel_welded.sum()), nmis,
                         rt, int(rep_e2.sum())))
    return 0 if nmis == 0 else 3


if __name__ == "__main__":
    sys.exit(main(int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4],
                  sys.argv[5] if len(sys.argv) > 5 else "pu"))
