#!/usr/bin/env python3
"""Per-cell groundwork for the attach delivery WP TABLE (stage A), in the standard T3-DNN binning
(2 pt rows split at 5 GeV x 10 |eta| bins of 0.25, last absorbing >2.5), on the DEPLOYED (dBeta)
head's scores over the labelled corpora.

Produces, per cell and per corpus: nTrue / nFake occupancy, the deployed bar's acceptance and FPR
in that cell, and the true-pair logit quantile ladder (so ANY target acceptance converts to a bar
value instantly once the deployed sweep names the operating point). Also the per-LENGTH (4-layer vs
5+) true-margin comparison, which decides whether pT4 needs its own row.

Pt axis = the SEED's ptIn, destandardized from head input 0 (af_log10PtIn) with the deployed
header's constants. Length = head input 10 (af_nLayers), destandardized the same way. Stage A rows
only (st == 0 or 2).

Output: p4_ref/tablefit.json + a human table on stdout.
"""
import json
import re
import sys

import numpy as np

C = "/mnt/data1/gsn27/here/chain_clean"
HDR = C + "/src/RecoTracker/LSTCore/src/alpaka/AttachNetworkWeights.h"
BARS = {"barrel": (0.0, 1.1, 6.519017), "transition": (1.1, 1.7, 5.810568), "endcap": (1.7, 99.0, 5.834996)}
ETA_EDGES = [0.0, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 2.25, 99.0]
QUANTS = [0.5, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95]


def header_consts(path):
    src = open(path).read()
    def arr(name):
        m = re.search(name + r"\s*\[[^\]]*\]\s*=\s*\{([^}]*)\}", src, re.S)
        return np.array([float(x) for x in m.group(1).replace("f", "").split(",") if x.strip()], np.float64)
    return arr("kFeatMean"), arr("kFeatStd")


def score(labdir, blob, dev):
    import torch, torch.nn as nn
    X = np.load(labdir + "/X.npy", mmap_mode="r")
    z3 = np.load(labdir + "/z3.npy", mmap_mode="r")
    pb = np.load(labdir + "/probe.npy", mmap_mode="r")
    n = X.shape[0]
    zmu = np.array(blob["z3_mean"], np.float32); zsd = np.array(blob["z3_std"], np.float32)
    pmu = np.array(blob["probe_mean"], np.float32); psd = np.array(blob["probe_std"], np.float32)
    clip = float(blob.get("args", {}).get("probe_clip", 5.0))
    m = nn.Sequential(nn.Linear(blob["arch"][0], blob["arch"][1]), nn.ReLU(),
                      nn.Linear(blob["arch"][1], blob["arch"][1]), nn.ReLU(),
                      nn.Linear(blob["arch"][1], blob["arch"][3]))
    m.load_state_dict(blob["state_dict"]); m.eval(); m.to(dev)
    s = np.empty(n, np.float32)
    B = 1 << 21
    with torch.no_grad():
        for i in range(0, n, B):
            j = min(i + B, n)
            a = np.array(X[i:j], dtype=np.float32)
            a[:, 11:14] = (np.asarray(z3[i:j], dtype=np.float32) - zmu) / zsd
            p = (np.asarray(pb[i:j], dtype=np.float32) - pmu) / psd
            np.clip(p, -clip, clip, out=p)
            s[i:j] = np.from_dlpack(m(torch.tensor(np.concatenate([a, p], 1)).to(dev))[:, 0].detach().cpu()).copy()
    return s


def main():
    import torch
    mean, std = header_consts(HDR)
    blob = torch.load(C + "/models/DBETA.pt", map_location="cpu", weights_only=False)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    out = {"eta_edges": ETA_EDGES, "quants": QUANTS, "cells": {}, "length": {}}
    for lab in ("pu", "jet"):
        d = f"{C}/lab/{lab}"
        y = np.load(d + "/y.npy"); st = np.load(d + "/st.npy")
        X = np.load(d + "/X.npy", mmap_mode="r")
        peta = np.abs(np.load(d + "/peta.npy"))
        s = score(d, blob, dev)
        stA = (st == 0) | (st == 2)
        # destandardize seed pt and target length
        lpt = np.asarray(X[:, 0], np.float64) * std[0] + mean[0]
        pt = 10.0 ** lpt
        nlay = np.asarray(X[:, 10], np.float64) * std[10] + mean[10]
        print(f"[{lab}] rows {len(y)}  stageA {stA.sum()}", flush=True)
        for ptlo, pthi, ptname in ((0.0, 5.0, "lo"), (5.0, 1e9, "hi")):
            for ie in range(10):
                elo, ehi = ETA_EDGES[ie], ETA_EDGES[ie + 1]
                cell = stA & (pt >= ptlo) & (pt < pthi) & (peta >= elo) & (peta < ehi)
                t = cell & (y == 1); f = cell & (y == 0)
                nT, nF = int(t.sum()), int(f.sum())
                # deployed bar for this eta
                bar = next(b for lo2, hi2, b in BARS.values() if lo2 <= elo < hi2)
                rec = {"nTrue": nT, "nFake": nF}
                if nT >= 20:
                    ts = np.sort(s[t])
                    rec["accNow"] = float((ts >= bar).mean())
                    rec["barForAcc"] = {str(q): float(np.quantile(ts, 1 - q)) for q in QUANTS}
                if nF >= 20:
                    rec["fprNow"] = float((s[f] >= bar).mean())
                out["cells"][f"{lab}_{ptname}_eta{ie}"] = rec
        # per-length true-margin comparison in the >=5 GeV row
        for ptname, lo2, hi2 in (("lo", 0, 5), ("hi", 5, 1e9)):
            sel = stA & (y == 1) & (pt >= lo2) & (pt < hi2)
            l4 = sel & (nlay < 4.5); l5 = sel & (nlay >= 4.5)
            if l4.sum() >= 20 and l5.sum() >= 20:
                out["length"][f"{lab}_{ptname}"] = {
                    "n4": int(l4.sum()), "n5": int(l5.sum()),
                    "median4": float(np.median(s[l4])), "median5": float(np.median(s[l5])),
                    "p25_4": float(np.quantile(s[l4], .25)), "p25_5": float(np.quantile(s[l5], .25))}
    with open("/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/p4_ref/tablefit.json", "w") as fh:
        json.dump(out, fh, indent=1)
    # human summary: the hi-pt row
    print("\n=== >=5 GeV row, per eta bin (pu) ===")
    print(f"{'bin':6s}{'nTrue':>8s}{'nFake':>9s}{'accNow':>8s}{'bar@.85':>9s}{'bar@.90':>9s}")
    for ie in range(10):
        r = out["cells"].get(f"pu_hi_eta{ie}", {})
        if not r: continue
        b85 = r.get("barForAcc", {}).get("0.85"); b90 = r.get("barForAcc", {}).get("0.9")
        print(f"eta{ie:<3d}{r['nTrue']:8d}{r['nFake']:9d}{r.get('accNow', float('nan')):8.3f}"
              f"{b85 if b85 is not None else float('nan'):9.3f}{b90 if b90 is not None else float('nan'):9.3f}")
    for k, v in out["length"].items():
        print(f"length {k}: n4 {v['n4']} n5 {v['n5']}  median4 {v['median4']:.2f} median5 {v['median5']:.2f}")
    print("TABLEFIT-DONE")


if __name__ == "__main__":
    main()
