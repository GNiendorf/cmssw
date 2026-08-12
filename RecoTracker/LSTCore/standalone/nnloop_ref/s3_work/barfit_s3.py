#!/usr/bin/env python3
"""S3: re-derive the EIGHT ChainConfig bars that sit on the attach-head logit scale, at FIXED
per-bin TRUE-PAIR (signal) acceptance, with ZERO free parameters.

The eight bars and the universe x band each one lives in (interface/ChainConfig.h):

    attachTheta   / attachThetaT / attachThetaE   stage-A delivery margin, |seed eta| bands
                                                  < 1.1 / [1.1,1.7) / >= 1.7
                                                  universe: chain targets with nLayers >= 5
    attachThetaT3                                 stage-B delivery margin, GLOBAL
                                                  universe: bare-T3 targets
    rpsThetaChain                                 -RPS chain-side retirement bar, GLOBAL
                                                  universe: chain targets with nLayers >= 5
    xcTheta       / xcThetaT      / xcThetaE      -XC bare-chain crossclean bar, same |seed eta|
                                                  bands; universe: chain-kind targets (stage A
                                                  5+ AND the aux 4-layer -XC4 tail, which is the
                                                  only reason those rows are scored at all)

THE RULE, and why it has no parameters.  Every row in the round-3 dump carries the SHIPPED head's
own logit on that exact row, so the reference is IN THE DATA: for each bar, measure the fraction of
TRUE pairs in its universe x band that the SHIPPED head admits at the SHIPPED bar, then take the
quantile of the NEW head's true-pair logits in the same cell that reproduces that fraction. Signal
efficiency is pinned by construction, so only the false-positive rate is free to move with head
quality (FINDINGS_NN, "THE CALIBRATION PROTOCOL").  Pair acceptance rate and per-target conversion
rate are MIXED quantities and matching either has already failed twice; conv_T5 is an OUTPUT.

The 2x10 (pT x |eta|) T3-DNN table is REPORTED as a diagnostic per bar -- the C++ bars are scalars
per eta band, so a per-cell table is not expressible without new plumbing, and this file says by
how much a single scalar per band misses.

usage: barfit_s3.py --lab <labdir> --model <head.pt> --out bars.json [--split test|val|trainval]
"""
import argparse
import json
import os

import numpy as np

SHIPPED = {
    "attachTheta": 7.3, "attachThetaT": 7.0, "attachThetaE": 6.4,
    "attachThetaT3": 6.450, "rpsThetaChain": 6.084,
    "xcTheta": 4.5, "xcThetaT": 4.0, "xcThetaE": 4.2,
}
# universe: 'A' chain 5+ (stage 0), 'B' bare T3 (stage 1), 'C' chain-kind (stage 0 + 2)
BARS = [
    ("attachTheta", "A", 0), ("attachThetaT", "A", 1), ("attachThetaE", "A", 2),
    ("attachThetaT3", "B", None), ("rpsThetaChain", "A", None),
    ("xcTheta", "C", 0), ("xcThetaT", "C", 1), ("xcThetaE", "C", 2),
]
# the shipped attach_norm slot-0 conditioning: clip log10(pt) to [-1, 4] then standardize
PT_MEAN, PT_STD = 0.167004988, 0.26579234


def etaband(ae):
    return np.where(ae < 1.1, 0, np.where(ae < 1.7, 1, 2))


def wp_bins(pt, ae):
    """LST's kWp binning: kPtBins = 2 split at 5 GeV, kEtaBins = 10 x 0.25 with the last
    absorbing |eta| > 2.5 (interface/alpaka/Common.h)."""
    pb = (pt > 5.0).astype(np.int64)
    eb = np.where(ae > 2.5, 9, np.minimum((ae / 0.25).astype(np.int64), 9))
    return pb * 10 + eb


def quantile_bar(scores, target_frac):
    """The largest bar b with mean(scores >= b) >= target_frac, i.e. the target_frac upper
    quantile. Ties are resolved toward ADMITTING (the shipped bars are >= comparisons)."""
    if len(scores) == 0:
        return None
    if target_frac >= 1.0:
        return float(np.min(scores))
    if target_frac <= 0.0:
        return float(np.max(scores)) + 1.0
    s = np.sort(scores)[::-1]
    k = int(np.floor(target_frac * len(s)))
    k = min(max(k, 1), len(s))
    return float(s[k - 1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lab", required=True)
    ap.add_argument("--model", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--fit-on", choices=["trainval", "test", "all"], default="trainval")
    ap.add_argument("--judge-on", choices=["test", "all"], default="test")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--offset", type=float, default=0.0,
                   help="uniform additive offset applied to every fitted bar (a DIAL, default off)")
    args = ap.parse_args()

    import torch
    L = args.lab
    X20 = np.load(L + "/X20.npy", mmap_mode="r")
    z3 = np.load(L + "/z3.npy", mmap_mode="r")
    y = np.load(L + "/y.npy")
    st = np.load(L + "/st.npy")
    vxy = np.load(L + "/vxy.npy")
    peta = np.load(L + "/peta.npy")
    lgt = np.load(L + "/lgt.npy")
    evt = np.load(L + "/evt.npy")
    n = len(y)

    blob = torch.load(args.model, map_location="cpu", weights_only=False)
    arm = blob.get("arm", "scalar")
    zmu = np.array(blob["z3_mean"], np.float32)
    zsd = np.array(blob["z3_std"], np.float32)
    n_in, n_hid = blob["arch"][0], blob["arch"][1]
    n_out = blob["arch"][3]
    import torch.nn as nn
    model = nn.Sequential(nn.Linear(n_in, n_hid), nn.ReLU(), nn.Linear(n_hid, n_hid), nn.ReLU(),
                          nn.Linear(n_hid, n_out))
    model.load_state_dict(blob["state_dict"])
    model.eval()
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model.to(dev)

    # same event split as the trainer
    rng = np.random.default_rng(args.seed)
    nev = int(evt.max()) + 1
    perm = rng.permutation(nev)
    ntr, nva = int(0.6 * nev), int(0.2 * nev)
    role = np.zeros(nev, np.int8)
    role[perm[ntr:ntr + nva]] = 1
    role[perm[ntr + nva:]] = 2
    r = role[evt]

    # --- score every row with the new head ------------------------------------------------
    snew = np.empty(n, np.float32)
    B = 1 << 21
    with torch.no_grad():
        for i in range(0, n, B):
            a = np.asarray(X20[i:i + B])
            b = (np.asarray(z3[i:i + B], dtype=np.float32) - zmu) / zsd
            x = np.concatenate([a[:, :11], b, a[:, 12:]], axis=1)
            zz = model(torch.tensor(x, dtype=torch.float32).to(dev))
            s = zz[:, 0] if n_out == 1 else (torch.logsumexp(zz[:, 1:3], dim=1) - zz[:, 0])
            snew[i:i + B] = s.cpu().numpy()

    pt = 10.0 ** (np.asarray(X20[:, 0]) * PT_STD + PT_MEAN)
    ae = np.abs(peta)
    eb = etaband(ae)
    cell = wp_bins(pt, ae)

    UNI = {"A": st == 0, "B": st == 1, "C": (st == 0) | (st == 2)}
    fitsel = {"trainval": r != 2, "test": r == 2, "all": np.ones(n, bool)}[args.fit_on]
    jdgsel = {"test": r == 2, "all": np.ones(n, bool)}[args.judge_on]

    out = {"model": os.path.abspath(args.model), "arm": arm, "fit_on": args.fit_on,
           "offset": args.offset, "shipped": SHIPPED, "bars": {}, "cells": {}}
    for name, uni, band in BARS:
        m = UNI[uni] & (y == 1)
        if band is not None:
            m &= eb == band
        mf = m & fitsel
        mj = m & jdgsel
        sb = SHIPPED[name]
        acc_ref = float((lgt[mf] >= sb).mean()) if mf.sum() else float("nan")
        bar = quantile_bar(snew[mf], acc_ref)
        bar = None if bar is None else bar + args.offset
        # judged on the frozen split: does it reproduce the shipped acceptance there?
        accj_ref = float((lgt[mj] >= sb).mean()) if mj.sum() else float("nan")
        accj_new = float((snew[mj] >= bar).mean()) if (mj.sum() and bar is not None) else float("nan")
        # background side at that operating point (the quantity head quality is supposed to move)
        fb = UNI[uni] & (y == 0) & jdgsel
        if band is not None:
            fb &= eb == band
        w = np.where(st[fb] == 1, 16.0, 1.0)
        fpr_ref = float((w * (lgt[fb] >= sb)).sum() / max(w.sum(), 1))
        fpr_new = float((w * (snew[fb] >= bar)).sum() / max(w.sum(), 1)) if bar is not None else float("nan")
        out["bars"][name] = {
            "universe": uni, "band": band, "shipped": sb, "new": bar,
            "n_true_fit": int(mf.sum()), "n_true_judge": int(mj.sum()),
            "acc_fit_shipped": acc_ref, "acc_judge_shipped": accj_ref, "acc_judge_new": accj_new,
            "fpr_judge_shipped": fpr_ref, "fpr_judge_new": fpr_new,
            "fpr_ratio": (fpr_new / fpr_ref) if fpr_ref > 0 else None,
        }
        print("%-14s uni %s band %s  shipped %.4f -> %.4f | acc %.5f -> %.5f (ref %.5f) | "
              "fpr %.3e -> %.3e  ratio %.4f  nTrue %d"
              % (name, uni, band, sb, bar if bar is not None else float("nan"), accj_ref, accj_new,
                 acc_ref, fpr_ref, fpr_new, (fpr_new / fpr_ref) if fpr_ref > 0 else float("nan"),
                 mj.sum()))
        # 2x10 diagnostic: per-cell shipped vs new acceptance at the SCALAR bar
        cd = {}
        for c in range(20):
            mc = mj & (cell == c)
            if mc.sum() < 20:
                continue
            cd[str(c)] = {"n": int(mc.sum()),
                          "acc_shipped": float((lgt[mc] >= sb).mean()),
                          "acc_new": float((snew[mc] >= bar).mean())}
        out["cells"][name] = cd
        if cd:
            dv = np.array([v["acc_new"] - v["acc_shipped"] for v in cd.values()])
            print("      2x10 per-cell acceptance delta: mean %+.4f  max|%.4f| over %d cells"
                  % (dv.mean(), np.abs(dv).max(), len(dv)))

    # global fake-weighted FPR at matched per-cell signal efficiency -- A4's / B1's verdict metric
    for uni in ("A", "B", "C"):
        mt = UNI[uni] & (y == 1) & jdgsel
        mfk = UNI[uni] & (y == 0) & jdgsel
        w = np.where(st[mfk] == 1, 16.0, 1.0)
        fr, fn = 0.0, 0.0
        tot = 0.0
        for c in range(20):
            mc = mt & (cell == c)
            fc = mfk & (cell == c)
            if mc.sum() < 20 or fc.sum() == 0:
                continue
            ref = float((lgt[mc] >= SHIPPED["attachTheta" if uni != "B" else "attachThetaT3"]).mean())
            b = quantile_bar(snew[mc], ref)
            wc = np.where(st[fc] == 1, 16.0, 1.0)
            fr += float((wc * (lgt[fc] >= SHIPPED["attachTheta" if uni != "B" else "attachThetaT3"])).sum())
            fn += float((wc * (snew[fc] >= b)).sum())
            tot += float(wc.sum())
        out.setdefault("fpr_matched", {})[uni] = {
            "shipped": fr / max(tot, 1), "new": fn / max(tot, 1),
            "ratio": (fn / fr) if fr > 0 else None, "w_fake": tot}
        print("FPR at matched per-CELL signal efficiency, universe %s: shipped %.3e new %.3e "
              "ratio %.4f" % (uni, fr / max(tot, 1), fn / max(tot, 1), (fn / fr) if fr else float("nan")))

    with open(args.out, "w") as fh:
        json.dump(out, fh, indent=1)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
