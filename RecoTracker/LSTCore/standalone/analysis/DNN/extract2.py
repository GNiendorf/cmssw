#!/usr/bin/env python3
"""[DISP2] Extract the compact stage-A delivery state needed to REPLAY the attach for any bar table.

One streaming pass over a pair dump. Two products:

  pairs  every PROMPT-target stage-A row with logit >= LOGIT_FLOOR, plus every DISPLACED-target row
         that clears its (untouched, shipped) displaced bar. Carries the row's bar cell -- (seed
         ptIn bin) x (seed |eta| band, the deployed lookup's own <1.1 / <1.7 / >=1.7) -- and a
         truth flag: does the seed share a sim with the target chain?
  sims   every core sim, with its master TC class, our TC class, and, when we match it to a
         T5-family TC (type 4 or 7), the HIT-EXACT chain that TC came from.

The prune is EXACT for any candidate table whose every prompt bar is >= LOGIT_FLOOR: raising a bar
only removes rows that are kept, and no candidate can admit a row below the floor. Displaced
targets are out of the fit by construction, so only their delivered rows matter and they are kept
at the shipped bar.

Usage: extract2.py <dumpdir> <outprefix> [nevents]
"""
import os
import sys
import time

import awkward as ak
import numpy as np
import uproot

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, "/mnt/data1/gsn27/here/chain_clean/src/RecoTracker/LSTCore/standalone/analysis/DNN")
from join_dump_io import iter_events as iter_join   # noqa: E402
import census_disp as C                             # noqa: E402

LOGIT_FLOOR = -3.0
# Bar-cell axes. The eta axis is the DEPLOYED lookup's own (ChainAttachPlsPre), so a fitted cell
# maps onto exactly one existing branch of that lookup. The pt axis refines the deployed 2-row
# split at 5 into 6 rows on the same quantity (the seed's raw ptIn).
PT_EDGES = np.array([2.0, 5.0, 10.0, 25.0, 50.0])
N_PT, N_ETA = len(PT_EDGES) + 1, 3
T0 = time.time()


def log(m):
    print("[%7.1fs] %s" % (time.time() - T0, m), flush=True)


def main():
    dump = sys.argv[1]
    pref = sys.argv[2]
    nent = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    C.DUMP = dump
    C.OURS = os.path.join(dump, "run.root")

    ko, ka, km = C.orig_key(C.NTUPLE), C.lst_key(C.OURS), C.lst_key(C.MAST)
    order = np.argsort(ko)
    a2o = order[np.searchsorted(ko, ka, sorter=order)]
    m2o = order[np.searchsorted(ko, km, sorter=order)]
    assert np.all(ko[a2o] == ka) and np.all(ko[m2o] == km)
    assert np.all(a2o == np.arange(len(ka))), "our run.root is not in entry order"
    o2m = np.full(len(ko), -1, np.int64)
    o2m[m2o] = np.arange(len(km))
    log("alignment OK")

    od = uproot.open(C.OURS)["tree"].arrays(
        ["sim_pt", "sim_eta", "sim_vx", "sim_vy", "sim_vz", "sim_q", "sim_tcIdx",
         "sim_trkNtupIdx", "tc_type", "tc_hitIdx", "tc_hitType"], entry_stop=nent, library="ak")
    md = uproot.open(C.MAST)["tree"].arrays(["sim_tcIdx", "sim_trkNtupIdx", "tc_type"], library="ak")

    tree = uproot.open(C.NTUPLE)["trackingNtuple/tree"]
    itc = C.iter_chains_hits(os.path.join(dump, "chains.bin"))
    itj = iter_join(os.path.join(dump, "join.bin"))
    itp = C.iter_pairs_light(os.path.join(dump, "pairs.bin"))

    P = {k: [] for k in ["ev", "tgt", "pls", "logit", "ecell", "ptcell", "disp", "istrue"]}
    S = {k: [] for k in ["ev", "sim", "chain", "pt", "eta", "our", "mast"]}
    nCmax = nPmax = 0
    ent = 0
    for batch in tree.iterate(C.BR, library="ak", step_size=20, entry_stop=nent):
        for b in range(len(batch["sim_pt"])):
            ievtC, nC, nn, nodeItems, nlay, flg, dca, hoff, chhits = next(itc)
            ievtJ, nCj, nT3j, nodes, plsrec = next(itj)
            hdr, st, tg, pl, lg = next(itp)
            assert ievtC == ent and ievtJ == ent and hdr["ievt"] == ent
            assert nCj == hdr["nChains"] == nC and hdr["nDrop"] == 0 and hdr["dsA"] == 1
            mrow = int(o2m[ent])
            nCmax = max(nCmax, nC)
            nPmax = max(nPmax, len(plsrec))

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
            dispChain = ~(dca < C.NEW_DCASPLIT)
            eband = C.seed_eta_band(peta)
            ptband = np.digitize(pptin, PT_EDGES)

            sA = st == 0
            tgA, plA, lgA = tg[sA], pl[sA], lg[sA].astype(np.float64)
            dA = dispChain[tgA]

            # ---- truth: does the seed share a sim with the target chain? ---------------------
            # Evaluated over EVERY stage-A row, because the working-point quantiles are taken over
            # the whole true-pair population and a logit prune would truncate their low tail.
            choff, chsims = C.build_chain_simsets(nodes, nodeItems, nn, nC,
                                                  ph2f, ph2c, ph2o, sht, chfrac=0.75)
            ploff, plsims = C.build_pls_simsets(batch, b, plsrec, ph2f, ph2c, ph2o,
                                                pixf, pixc, pixo, sht)
            allTrue = np.zeros(len(tgA), bool)
            ccnt = np.diff(choff)[tgA]
            tot = int(ccnt.sum())
            if tot:
                outo = np.concatenate(([0], np.cumsum(ccnt)[:-1]))
                ar = np.arange(tot, dtype=np.int64) - np.repeat(outo, ccnt)
                sims_exp = chsims[np.repeat(choff[tgA], ccnt) + ar]
                row_exp = np.repeat(np.arange(len(tgA), dtype=np.int64), ccnt)
                pcnt = np.diff(ploff)
                pown = np.repeat(np.arange(nP, dtype=np.int64), pcnt)
                pkey = np.unique(pown * np.int64(1 << 21) + plsims)
                hit = np.isin(plA[row_exp] * np.int64(1 << 21) + sims_exp, pkey)
                if hit.any():
                    allTrue[np.unique(row_exp[hit])] = True

            shipped = C.bars_shipped(peta[plA], pptin[plA], dA)
            # keep: every displaced row that DELIVERS today (the displaced row is out of the fit,
            # so only its verdict matters), every prompt row above the replay floor, and every
            # prompt TRUE pair regardless of logit (the WP quantile population).
            keep = np.where(dA, lgA >= shipped, (lgA >= LOGIT_FLOOR) | allTrue)
            k = np.flatnonzero(keep)
            kt, kp, kl = tgA[k], plA[k], lgA[k]
            istrue = allTrue[k]

            P["ev"].append(np.full(len(k), ent, np.int32))
            P["tgt"].append(kt.astype(np.int32))
            P["pls"].append(kp.astype(np.int32))
            P["logit"].append(kl.astype(np.float32))
            P["ecell"].append(eband[kp].astype(np.int8))
            P["ptcell"].append(ptband[kp].astype(np.int8))
            P["disp"].append(dA[k])
            P["istrue"].append(istrue)

            # ---- the sim table ----------------------------------------------------------------
            osim_tc = ak.to_numpy(od["sim_tcIdx"][ent]).astype(np.int64)
            oty = ak.to_numpy(od["tc_type"][ent]).astype(np.int64)
            msim_tc = ak.to_numpy(md["sim_tcIdx"][mrow]).astype(np.int64)
            mty = ak.to_numpy(md["tc_type"][mrow]).astype(np.int64)
            spt = ak.to_numpy(od["sim_pt"][ent]).astype(np.float64)
            seta = ak.to_numpy(od["sim_eta"][ent]).astype(np.float64)
            svz = ak.to_numpy(od["sim_vz"][ent]).astype(np.float64)
            svx = ak.to_numpy(od["sim_vx"][ent]).astype(np.float64)
            svy = ak.to_numpy(od["sim_vy"][ent]).astype(np.float64)
            sq = ak.to_numpy(od["sim_q"][ent]).astype(np.int64)
            core = ((spt > 0.9) & (np.abs(svz) < 30) & (np.hypot(svx, svy) < 2.5)
                    & (sq != 0) & (np.abs(seta) < 2.5))
            ourT = np.where(osim_tc >= 0, oty[np.clip(osim_tc, 0, len(oty) - 1)], -1)
            masT = np.where(msim_tc >= 0, mty[np.clip(msim_tc, 0, len(mty) - 1)], -1)
            rows = np.flatnonzero(core)
            lut = None
            chain = np.full(len(rows), -1, np.int64)
            fam = np.isin(ourT[rows], (4, 7))
            if fam.any():
                lut = {}
                for c in range(nC):
                    lut.setdefault(np.unique(chhits[hoff[c]:hoff[c + 1]]).tobytes(), []).append(c)
                for j in np.flatnonzero(fam):
                    tc = int(osim_tc[rows[j]])
                    hi = np.asarray(od["tc_hitIdx"][ent][tc]).astype(np.uint32)
                    ht = np.asarray(od["tc_hitType"][ent][tc]).astype(np.int64)
                    cd = lut.get(np.unique(hi[ht == 4]).tobytes(), [])
                    if len(cd) == 1:
                        chain[j] = cd[0]
            S["ev"].append(np.full(len(rows), ent, np.int32))
            S["sim"].append(rows.astype(np.int32))
            S["chain"].append(chain.astype(np.int32))
            S["pt"].append(spt[rows].astype(np.float32))
            S["eta"].append(seta[rows].astype(np.float32))
            S["our"].append(ourT[rows].astype(np.int8))
            S["mast"].append(masT[rows].astype(np.int8))
            ent += 1
        if ent % 50 == 0:
            log("evt %d  kept pairs %d" % (ent, sum(len(x) for x in P["ev"])))

    out = {("p_" + k): np.concatenate(v) for k, v in P.items()}
    out.update({("s_" + k): np.concatenate(v) for k, v in S.items()})
    out["meta"] = np.array([ent, nCmax, nPmax, LOGIT_FLOOR])
    out["pt_edges"] = PT_EDGES
    np.savez(pref + ".npz", **out)
    log("wrote %s.npz: %d pair rows, %d sims, nCmax %d nPmax %d"
        % (pref, len(out["p_ev"]), len(out["s_ev"]), nCmax, nPmax))


if __name__ == "__main__":
    main()
