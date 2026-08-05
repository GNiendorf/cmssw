#!/usr/bin/env python3
"""m13_gate.py - numpy replica of the m12_w7 K9 THETA stage on the frozen chain dump.

READ-ONLY. Reproduces, for every one of the 1.427M welded chains in
chains_m12_300evt.root, the -G 6 three-class gate decision under the w7 configuration
(from the ab_m12_w7.log header, verbatim):

  dcaSplit -X 0.5   Z 0.0   lambdaLen 0.5
  nL<=4, dca >= max(0.5,0)  -> EXEMPT-T4 : kill iff mD <  -0.5      (-M4D), thr U4 = 0
  nL<=4, dca <  0.5         -> IP-T4     : kill iff mX <  1.4950    (-M4),  thr kNoCut
  nL>=5, dca <  0.5         -> IP-5+     : kill iff mP < 0.8974 (nL=5) / -0.6054 (nL>=6)
                                           AND mX < -0.8 (-MR rescue), thr kNoCut
  nL>=5, dca >= 0.5         -> EXEMPT-5+ : kill iff mX < -0.8 (-MD 1e9 => pure mX),
                                           thr U5/U6 = 0 on the LEGACY score

Legacy score = cf_sumEdgeLogit + 0.5 * nLayers (K6 contract, lambdaLen 0.5).

Self-validation: the number of chains passing theta must equal the binary's
"chain funnel in=1427138 -> theta=839508".

Writes <scratch>/m13_chains_w7.npz
"""
import json

import numpy as np
import torch
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-"
           "LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")
DUMP = f"{SA}/prototype/chains_m12_300evt.root"
PT = f"{SA}/prototype/chain3_mlp_m12.pt"
NORM = f"{SA}/prototype/chain3_norm_m12.json"
DROP = {"maxBridgeChi2"}

# ---- w7 configuration (ab_m12_w7.log line 2-4) ----
DCA_SPLIT, T4_Z, LAMBDA_LEN = 0.5, 0.0, 0.5
M4, M4D, M5, M6, MD, MR = 1.4950, -0.5, 0.8974, -0.6054, 1e9, -0.800
U4 = U5 = U6 = 0.0
NOCUT = -1e5


def main():
    f = uproot.open(DUMP)
    cf_names = f["feature_spec"].member("fTitle")[3:].split(",")
    keep = [i for i, nm in enumerate(cf_names) if nm not in DROP]
    names = [f"cf_{cf_names[i]}" for i in keep] + ["cf_dcaXY"]
    t = f["chains"]
    meta = t.arrays(["evt", "label", "label_old", "matchFrac", "simIdx", "simVxy", "simPt",
                     "nLayers", "dcaXY"], library="np")
    n = len(meta["label"])
    X = np.empty((n, len(keep) + 1), dtype=np.float32)
    for j, i in enumerate(keep):
        X[:, j] = t[f"cf_{i:02d}"].array(library="np")
    X[:, len(keep)] = meta["dcaXY"]
    sumEdge = t["cf_02"].array(library="np")           # cf_sumEdgeLogit, contract slot 2
    print(f"loaded {n} chains, {X.shape[1]} model inputs")

    norm = json.load(open(NORM))
    assert norm["feature_names"] == names, "feature order mismatch vs norm json"
    Xc = X.astype(np.float64)
    for c in norm["conditioning"]:
        j = names.index(c["feature"])
        if c["op"] == "log10_1p":
            Xc[:, j] = np.log10(1.0 + Xc[:, j])
        else:
            np.clip(Xc[:, j], c["lo"], c["hi"], out=Xc[:, j])
    Xc = (Xc - np.array(norm["mean"])) / np.array(norm["std"])

    sd = torch.load(PT, map_location="cpu", weights_only=False)["state_dict"]
    def arr(t):
        return np.array(t.tolist(), dtype=np.float64)
    W1, b1 = arr(sd["0.weight"]), arr(sd["0.bias"])
    W2, b2 = arr(sd["2.weight"]), arr(sd["2.bias"])
    W3, b3 = arr(sd["4.weight"]), arr(sd["4.bias"])
    h = np.maximum(Xc @ W1.T + b1, 0)
    h = np.maximum(h @ W2.T + b2, 0)
    z = h @ W3.T + b3
    mP = z[:, 1] - z[:, 0]
    mD = z[:, 2] - z[:, 0]
    mX = np.maximum(z[:, 1], z[:, 2]) - z[:, 0]

    nL = meta["nLayers"].astype(np.int32)
    dca = meta["dcaXY"].astype(np.float64)
    score = sumEdge.astype(np.float64) + LAMBDA_LEN * nL

    isT4 = nL <= 4
    exemptT4 = isT4 & (dca >= max(DCA_SPLIT, T4_Z))
    ipT4 = isT4 & ~exemptT4
    exempt5 = (~isT4) & (dca >= DCA_SPLIT)
    ip5 = (~isT4) & (dca < DCA_SPLIT)

    kill = np.zeros(n, dtype=bool)
    kill |= exemptT4 & (mD < M4D)
    kill |= ipT4 & (mX < M4)
    thr5 = np.where(nL >= 6, M6, M5)
    kill |= ip5 & (mP < thr5) & (mX < MR)
    kill |= exempt5 & (mD < MD) & (mX < MR)

    exemptMask = exemptT4 | exempt5
    thrLegacy = np.where(exemptMask, np.where(nL >= 6, U6, np.where(nL == 5, U5, U4)), NOCUT)
    thetaPass = (score - np.where(kill, 1e9, 0.0)) >= thrLegacy

    print(f"REPLICA theta-pass = {int(thetaPass.sum())}   (binary log: 839508)  "
          f"delta = {int(thetaPass.sum()) - 839508}")
    for lab, m in [("IP-T4", ipT4), ("exempt-T4", exemptT4), ("IP-5+", ip5), ("exempt-5+", exempt5)]:
        print(f"  {lab:10s} n={int(m.sum()):8d}  killed={int((m & kill).sum()):8d}  "
              f"thetaPass={int((m & thetaPass).sum()):8d}  "
              f"harness-true(label=1)={int((m & (meta['label'] == 1)).sum()):7d}")

    np.savez(f"{SCRATCH}/m13_chains_w7.npz",
             evt=meta["evt"], label=meta["label"].astype(np.int8),
             label_old=meta["label_old"].astype(np.int8), matchFrac=meta["matchFrac"],
             simIdx=meta["simIdx"].astype(np.int32), simVxy=meta["simVxy"],
             simPt=meta["simPt"], nLayers=nL, dca=dca.astype(np.float32),
             score=score.astype(np.float32), mP=mP.astype(np.float32),
             mD=mD.astype(np.float32), mX=mX.astype(np.float32),
             kill=kill, thetaPass=thetaPass, exemptMask=exemptMask)
    print("wrote m13_chains_w7.npz")


if __name__ == "__main__":
    main()
