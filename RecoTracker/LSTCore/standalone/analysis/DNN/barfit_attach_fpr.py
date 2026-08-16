#!/usr/bin/env python3
"""Re-derive the FOUR DELIVERY bars at matched per-cell FAKE-PAIR rate (FPR) instead of matched
signal acceptance.

Why: the deployed bars (bars2_DBETA, matched acceptance) spend the dBeta head's whole improvement
on -14% fakes. The project priority is efficiency >> dup > fake, and the attach round showed the
#1 conversion lever is exactly the delivery bar (9366/1000evt victims at a median 0.95 logits
under it). At matched FPR the same head buys +7.0% true-pair acceptance on chain targets and
+11.0% on bare triplets. This script computes those bars.

Reference side: the labelled corpora's `lgt` column holds the SHIPPED head's logit for every row
(the dump predates the dBeta deployment), and the shipped operating point is the pre-dBeta
ChainConfig defaults, hardcoded below from AttachNetworkWeights.h.shipped's era:
    attachTheta 6.64275 / attachThetaT 6.23461 / attachThetaE 6.209162 / attachThetaT3 5.558521
Per cell: ref FPR = fraction of FAKE pairs the shipped head admits at that bar; the new bar is the
quantile of the DBETA head's FAKE scores reproducing that FPR. Fit on the trainval split (seed-42
event-level 60/20/20, same as the trainer), pooled per cell. Across the two corpora the bar is the
MAX of the per-corpus bars, so neither sample's fake rate is exceeded (the mirror of the
acceptance-matching min rule).

Stage-B downsampling (dsB=16) is uniform within stage 1, so unweighted quantiles are exact there.
rpsThetaChain and the xcTheta* bars are NOT touched: retirement and cross-clean stay at their
matched-acceptance values.

usage: barfit_fpr.py --pu <labdir> --jet <labdir> --model DBETA.pt --out bars_fpr.json
"""
import argparse
import json

import numpy as np

REF = {"attachTheta": 6.64275, "attachThetaT": 6.23461, "attachThetaE": 6.209162,
       "attachThetaT3": 5.558521}
# (name, universe stage, eta band) -- bands on |seed eta|; None = global
CELLS = [("attachTheta", 0, (0.0, 1.1)), ("attachThetaT", 0, (1.1, 1.7)),
         ("attachThetaE", 0, (1.7, 99.0)), ("attachThetaT3", 1, None)]


def score(labdir, blob, dev):
    import torch
    X = np.load(labdir + "/X.npy", mmap_mode="r")
    z3 = np.load(labdir + "/z3.npy", mmap_mode="r")
    pb = np.load(labdir + "/probe.npy", mmap_mode="r")
    n = X.shape[0]
    zmu = np.array(blob["z3_mean"], np.float32); zsd = np.array(blob["z3_std"], np.float32)
    pmu = np.array(blob["probe_mean"], np.float32); psd = np.array(blob["probe_std"], np.float32)
    clip = float(blob.get("args", {}).get("probe_clip", 5.0))
    import torch.nn as nn
    n_in, n_hid = blob["arch"][0], blob["arch"][1]
    m = nn.Sequential(nn.Linear(n_in, n_hid), nn.ReLU(), nn.Linear(n_hid, n_hid), nn.ReLU(),
                      nn.Linear(n_hid, blob["arch"][3]))
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
            a = np.concatenate([a, p], axis=1)
            s[i:j] = np.from_dlpack(m(torch.tensor(a).to(dev))[:, 0].detach().cpu()).copy()
    return s


def trainval_mask(labdir, seed=42):
    evt = np.load(labdir + "/evt.npy")
    nev = int(evt.max()) + 1
    rng = np.random.default_rng(seed)
    perm = rng.permutation(nev)
    ntr, nva = int(0.6 * nev), int(0.2 * nev)
    role = np.zeros(nev, np.int8)
    role[perm[ntr:ntr + nva]] = 1
    role[perm[ntr + nva:]] = 2
    return role[evt] < 2  # train + val


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pu", required=True)
    ap.add_argument("--jet", required=True)
    ap.add_argument("--model", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    import torch
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    blob = torch.load(a.model, map_location="cpu", weights_only=False)

    out = {"ref": REF, "bars": {}, "per_lab": {}}
    labs = {}
    for nm, d in (("pu", a.pu), ("jet", a.jet)):
        s = score(d, blob, dev)
        y = np.load(d + "/y.npy"); st = np.load(d + "/st.npy")
        peta = np.abs(np.load(d + "/peta.npy")); lgt = np.load(d + "/lgt.npy")
        tv = trainval_mask(d)
        labs[nm] = (s, y, st, peta, lgt, tv)
        print("scored %s: %d rows" % (nm, len(s)), flush=True)

    for name, stage, band in CELLS:
        bars = {}
        for nm, (s, y, st, peta, lgt, tv) in labs.items():
            m = tv & (st == stage) & (y == 0)
            if band is not None:
                m &= (peta >= band[0]) & (peta < band[1])
            refFpr = float((lgt[m] >= REF[name]).mean())
            bar = float(np.quantile(s[m], 1.0 - refFpr))
            # diagnostics on the TRUE side at that bar
            t = tv & (st == stage) & (y == 1)
            if band is not None:
                t &= (peta >= band[0]) & (peta < band[1])
            accRef = float((lgt[t] >= REF[name]).mean())
            accNew = float((s[t] >= bar).mean())
            bars[nm] = bar
            out["per_lab"].setdefault(name, {})[nm] = dict(
                refFpr=refFpr, bar=bar, accRef=accRef, accNew=accNew, nFake=int(m.sum()), nTrue=int(t.sum()))
            print("%-14s %-4s refFPR %.4e  bar %.4f  acc %.4f -> %.4f (%+.1f%%)"
                  % (name, nm, refFpr, bar, accRef, accNew, (accNew / max(accRef, 1e-9) - 1) * 100), flush=True)
        final = max(bars.values())  # do not exceed either sample's fake rate
        out["bars"][name] = final
        print("%-14s FINAL bar %.6f  (max over labs)" % (name, final), flush=True)

    with open(a.out, "w") as fh:
        json.dump(out, fh, indent=1)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
