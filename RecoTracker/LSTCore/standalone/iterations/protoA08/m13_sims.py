#!/usr/bin/env python3
"""m13_sims.py - per-sim outcome table from the m12_w7 anchor (READ-ONLY).

Exact methodology clone of m10_sims.py (which built the m8_h4b table the M10 forensics
ran on), pointed at ab_m12_w7.root, plus the vz/q columns m10_report2.py had to fetch
separately so the harness denominator (pt>0.9, |eta|<4.5, |vz|<30, q!=0) is complete in
one array.

Writes  <scratch>/m13_sims_w7.npy
"""
import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-"
           "LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")

AB = f"{SA}/prototype/ab_m12_w7.root"
BASE = f"{SA}/LSTNtuple_PU200RelVal_300evt.root"

CHAIN_TYPES = (4, 9)
PIX_TYPES = (5, 7, 8)


def main():
    ab = uproot.open(AB)["tree"]
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
        for s in np.nonzero(sel)[0]:
            rows.append((int(a["lumi"][i]), int(a["evt"][i]), int(s), float(pt[s]), float(eta[s]),
                         float(vxy[s]), float(abs(dxy[s])), float(vz[s]), int(q[s]), int(pdg[s]),
                         bool(chainMatched[s]), bool(pixMatched[s]), bool(anyMatched[s]),
                         int(chainType[s]), bool(baseMatched[s]), int(baseType[s]),
                         bool(baseOT[s]), bool(basePix[s])))
    dt = np.dtype([("lumi", "i4"), ("evt", "i8"), ("sim", "i4"), ("pt", "f4"), ("eta", "f4"),
                   ("vxy", "f4"), ("dxy", "f4"), ("vz", "f4"), ("q", "i4"), ("pdg", "i4"),
                   ("chainTC", "?"), ("pixTC", "?"), ("anyTC", "?"), ("chainType", "i4"),
                   ("baseTC", "?"), ("baseType", "i4"), ("baseOT", "?"), ("basePix", "?")])
    arr = np.array(rows, dtype=dt)
    np.save(f"{SCRATCH}/m13_sims_w7.npy", arr)
    ok = (np.abs(arr["vz"]) < 30) & (arr["q"] != 0)
    print(f"sims pt>0.9 |eta|<4.5: {len(arr)}; harness denominator (+|vz|<30,q!=0): {ok.sum()}")
    for lab, m in [("vxy[0,1)", arr["vxy"] < 1), ("vxy[1,5)", (arr["vxy"] >= 1) & (arr["vxy"] < 5)),
                   ("vxy[5,10)", (arr["vxy"] >= 5) & (arr["vxy"] < 10)),
                   ("vxy[10,30)", (arr["vxy"] >= 10) & (arr["vxy"] < 30)),
                   ("dxy[0,1)", arr["dxy"] < 1), ("dxy[1,5)", (arr["dxy"] >= 1) & (arr["dxy"] < 5)),
                   ("dxy[5,10)", (arr["dxy"] >= 5) & (arr["dxy"] < 10)),
                   ("dxy[10,30)", (arr["dxy"] >= 10) & (arr["dxy"] < 30))]:
        m = m & ok
        n = m.sum()
        print(f"  {lab:12s} N={n:6d} anyTC={arr['anyTC'][m].sum():6d} ({arr['anyTC'][m].mean():.4f})"
              f"  base={arr['baseTC'][m].sum():6d} ({arr['baseTC'][m].mean():.4f})"
              f"  chain={arr['chainTC'][m].sum():6d} pix={arr['pixTC'][m].sum():6d}")


if __name__ == "__main__":
    main()
