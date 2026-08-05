#!/usr/bin/env python3
"""m16r_sims.py -- per-sim outcome table for an M16 replacement-mode A/B (READ-ONLY).

Methodology clone of m13_sims.py (validated to reproduce the compare_ab.py band ratios).
Usage:  m16r_sims.py <ab_root> <out_npy_tag>
Writes <scratch>/m16r_sims_<tag>.npy
"""
import sys

import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-"
           "LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")
BASE = f"{SA}/LSTNtuple_PU200RelVal_300evt.root"

CHAIN_TYPES = (4, 9)
PIX_TYPES = (5, 7, 8)


def main():
    ab_path, tag = sys.argv[1], sys.argv[2]
    ab = uproot.open(ab_path)["tree"]
    a = ab.arrays(["sim_pt", "sim_eta", "sim_vx", "sim_vy", "sim_vz", "sim_q", "sim_pca_dxy",
                   "sim_pdgId", "sim_tcIdx", "tc_type", "tc_isChain", "tc_simIdxAll",
                   "run", "lumi", "evt"], library="np")
    bt = uproot.open(BASE)["tree"]
    b = bt.arrays(["sim_pt", "sim_tcIdx", "tc_type", "tc_simIdxAll", "tc_simIdxAllFrac",
                   "evt", "lumi"], library="np")
    bmap = {(int(b["lumi"][i]), int(b["evt"][i])): i for i in range(len(b["evt"]))}

    rows = []
    for i in range(len(a["evt"])):
        j = bmap[(int(a["lumi"][i]), int(a["evt"][i]))]
        pt, eta = a["sim_pt"][i], a["sim_eta"][i]
        vxy = np.hypot(a["sim_vx"][i], a["sim_vy"][i])
        dxy = a["sim_pca_dxy"][i]
        vz, q, pdg = a["sim_vz"][i], a["sim_q"][i], a["sim_pdgId"][i]
        nS = len(pt)
        assert len(b["sim_pt"][j]) == nS
        chainMatched = np.zeros(nS, dtype=bool)
        pixMatched = np.zeros(nS, dtype=bool)
        chainType = np.full(nS, -1, dtype=np.int32)
        isChain, ttype = a["tc_isChain"][i], a["tc_type"][i]
        for k, sl in enumerate(a["tc_simIdxAll"][i]):
            tgt = chainMatched if isChain[k] else pixMatched
            for s in sl:
                if 0 <= s < nS:
                    tgt[s] = True
                    if isChain[k]:
                        chainType[s] = max(chainType[s], int(ttype[k]))
        anyMatched = a["sim_tcIdx"][i] >= 0
        baseMatched = b["sim_tcIdx"][j] >= 0
        btype = b["tc_type"][j]
        baseType = np.full(nS, -1, dtype=np.int32)
        stc = b["sim_tcIdx"][j]
        for s in range(nS):
            if stc[s] >= 0:
                baseType[s] = btype[stc[s]]
        # per-sim: which baseline TYPES delivered it (frac>0.75)
        baseOT = np.zeros(nS, dtype=bool)
        base7 = np.zeros(nS, dtype=bool)
        base5 = np.zeros(nS, dtype=bool)
        base8 = np.zeros(nS, dtype=bool)
        for k, (sl, fl) in enumerate(zip(b["tc_simIdxAll"][j], b["tc_simIdxAllFrac"][j])):
            t = int(btype[k])
            for s, f in zip(sl, fl):
                if f > 0.75 and 0 <= s < nS:
                    if t in CHAIN_TYPES:
                        baseOT[s] = True
                    elif t == 7:
                        base7[s] = True
                    elif t == 5:
                        base5[s] = True
                    elif t == 8:
                        base8[s] = True
        sel = (pt > 0.9) & (np.abs(eta) < 4.5)
        for s in np.nonzero(sel)[0]:
            rows.append((int(a["lumi"][i]), int(a["evt"][i]), int(s), float(pt[s]), float(eta[s]),
                         float(vxy[s]), float(abs(dxy[s])), float(vz[s]), int(q[s]), int(pdg[s]),
                         bool(chainMatched[s]), bool(pixMatched[s]), bool(anyMatched[s]),
                         int(chainType[s]), bool(baseMatched[s]), int(baseType[s]),
                         bool(baseOT[s]), bool(base7[s]), bool(base5[s]), bool(base8[s])))
    dt = np.dtype([("lumi", "i4"), ("evt", "i8"), ("sim", "i4"), ("pt", "f4"), ("eta", "f4"),
                   ("vxy", "f4"), ("dxy", "f4"), ("vz", "f4"), ("q", "i4"), ("pdg", "i4"),
                   ("chainTC", "?"), ("pixTC", "?"), ("anyTC", "?"), ("chainType", "i4"),
                   ("baseTC", "?"), ("baseType", "i4"), ("baseOT", "?"), ("base7", "?"),
                   ("base5", "?"), ("base8", "?")])
    arr = np.array(rows, dtype=dt)
    np.save(f"{SCRATCH}/m16r_sims_{tag}.npy", arr)
    ok = (np.abs(arr["vz"]) < 30) & (arr["q"] != 0)
    print(f"[{tag}] sims pt>0.9 |eta|<4.5: {len(arr)}; harness denom: {ok.sum()}"
          f"  eff={arr['anyTC'][ok].mean():.4f} base={arr['baseTC'][ok].mean():.4f}")
    lost = ok & arr["baseTC"] & ~arr["anyTC"]
    gain = ok & ~arr["baseTC"] & arr["anyTC"]
    print(f"  LOST (base yes / proto no) = {lost.sum()}   GAINED = {gain.sum()}"
          f"   net = {gain.sum() - lost.sum()}")


if __name__ == "__main__":
    main()
