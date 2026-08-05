#!/usr/bin/env python3
"""m10_sims.py - per-sim outcome table from the anchor A/B output (READ-ONLY).

Builds, for every accepted sim in ab_m8_h4b.root with pt>0.9 |eta|<4.5:
  vxy, dxy, matched-by-chain-TC, matched-by-pixel-TC, matched-by-any-TC,
  plus the baseline (LSTNtuple) matched flag and matched TC type.
Writes a numpy npz to the scratchpad for the funnel joins.
"""
import json
import sys

import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SCRATCH = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad"

AB = f"{SA}/prototype/ab_m8_h4b.root"
BASE = f"{SA}/LSTNtuple_PU200RelVal_300evt.root"

CHAIN_TYPES = (4, 9)
PIX_TYPES = (5, 7, 8)


def main():
    ab = uproot.open(AB)["tree"]
    a = ab.arrays(["sim_pt", "sim_eta", "sim_vx", "sim_vy", "sim_pca_dxy", "sim_pdgId",
                   "sim_tcIdx", "tc_type", "tc_isChain", "tc_simIdxAll", "tc_nhitOT",
                   "run", "lumi", "evt"], library="np")
    bt = uproot.open(BASE)["tree"]
    b = bt.arrays(["sim_pt", "sim_eta", "sim_tcIdx", "tc_type", "tc_simIdxAll",
                   "tc_simIdxAllFrac", "sim_vtxperp", "sim_pca_dxy", "evt", "lumi"], library="np")
    bmap = {(int(b["lumi"][i]), int(b["evt"][i])): i for i in range(len(b["evt"]))}

    rows = []
    for i in range(len(a["evt"])):
        key = (int(a["lumi"][i]), int(a["evt"][i]))
        j = bmap[key]
        pt = a["sim_pt"][i]
        eta = a["sim_eta"][i]
        vxy = np.hypot(a["sim_vx"][i], a["sim_vy"][i])
        dxy = a["sim_pca_dxy"][i]
        pdg = a["sim_pdgId"][i]
        nS = len(pt)
        assert len(b["sim_pt"][j]) == nS
        # chain / pixel coverage in the hybrid output
        chainMatched = np.zeros(nS, dtype=bool)
        pixMatched = np.zeros(nS, dtype=bool)
        isChain = a["tc_isChain"][i]
        for k, sl in enumerate(a["tc_simIdxAll"][i]):
            tgt = chainMatched if isChain[k] else pixMatched
            for s in sl:
                if 0 <= s < nS:
                    tgt[s] = True
        anyMatched = a["sim_tcIdx"][i] >= 0
        # baseline
        baseMatched = b["sim_tcIdx"][j] >= 0
        baseType = np.full(nS, -1, dtype=np.int32)
        stc = b["sim_tcIdx"][j]
        btype = b["tc_type"][j]
        for s in range(nS):
            if stc[s] >= 0:
                baseType[s] = btype[stc[s]]
        # baseline coverage by T5/T4 (bare-OT slice) and by pixel types
        baseOT = np.zeros(nS, dtype=bool)
        basePix = np.zeros(nS, dtype=bool)
        for k, (sl, fl) in enumerate(zip(b["tc_simIdxAll"][j], b["tc_simIdxAllFrac"][j])):
            t = btype[k]
            for s, f in zip(sl, fl):
                if f > 0.75 and 0 <= s < nS:
                    if t in CHAIN_TYPES:
                        baseOT[s] = True
                    elif t in PIX_TYPES:
                        basePix[s] = True
        sel = (pt > 0.9) & (np.abs(eta) < 4.5)
        idx = np.nonzero(sel)[0]
        for s in idx:
            rows.append((int(a["lumi"][i]), int(a["evt"][i]), int(s), float(pt[s]), float(eta[s]),
                         float(vxy[s]), float(abs(dxy[s])), int(pdg[s]),
                         bool(chainMatched[s]), bool(pixMatched[s]), bool(anyMatched[s]),
                         bool(baseMatched[s]), int(baseType[s]), bool(baseOT[s]), bool(basePix[s])))
    dt = np.dtype([("lumi", "i4"), ("evt", "i8"), ("sim", "i4"), ("pt", "f4"), ("eta", "f4"),
                   ("vxy", "f4"), ("dxy", "f4"), ("pdg", "i4"),
                   ("chainTC", "?"), ("pixTC", "?"), ("anyTC", "?"),
                   ("baseTC", "?"), ("baseType", "i4"), ("baseOT", "?"), ("basePix", "?")])
    arr = np.array(rows, dtype=dt)
    np.save(f"{SCRATCH}/m10_sims.npy", arr)
    print(f"sims selected (pt>0.9 |eta|<4.5): {len(arr)} over {len(a['evt'])} events")
    for lab, m in [("prompt vxy<1", arr["vxy"] < 1),
                   ("vxy[1,5)", (arr["vxy"] >= 1) & (arr["vxy"] < 5)),
                   ("vxy[5,10)", (arr["vxy"] >= 5) & (arr["vxy"] < 10)),
                   ("vxy[10,30)", (arr["vxy"] >= 10) & (arr["vxy"] < 30)),
                   ("vxy>=30", arr["vxy"] >= 30)]:
        n = m.sum()
        if n == 0:
            continue
        print(f"  {lab:14s} N={n:6d}  chainTC={arr['chainTC'][m].sum():6d} "
              f"({arr['chainTC'][m].mean():.3f})  pixTC={arr['pixTC'][m].mean():.3f}  "
              f"anyTC={arr['anyTC'][m].mean():.3f}  base={arr['baseTC'][m].mean():.3f}  "
              f"baseOT={arr['baseOT'][m].mean():.3f}  basePix={arr['basePix'][m].mean():.3f}")


if __name__ == "__main__":
    main()
