#!/usr/bin/env python3

"""Re-derive the eight ChainConfig bars that live on the attach-head logit scale.

The attach head's logit scale is not stable across trainings: a retrained head can shift its whole
logit distribution without being any worse, so a bar carried over unchanged silently moves the
operating point it encodes. Every one of these eight bars therefore has to be re-derived whenever
AttachNetworkWeights.h changes, which is what this script does -- at FIXED per-cell TRUE-PAIR
(signal) acceptance and with no free parameters.

The eight bars and the universe x band each one lives in (interface/ChainConfig.h):

    attachTheta   / attachThetaT / attachThetaE   delivery margin of the chain attach, |seed eta|
                                                  bands < 1.1 / [1.1,1.7) / >= 1.7
                                                  universe: chain targets with nLayers >= 5
    attachThetaT3                                 delivery margin of the bare-T3 attach, GLOBAL
                                                  universe: bare-T3 targets
    rpsThetaChain                                 chain-side retirement bar, GLOBAL
                                                  universe: chain targets with nLayers >= 5
    xcTheta       / xcThetaT      / xcThetaE      bare-chain cross-clean bar, same |seed eta|
                                                  bands; universe: chain-kind targets (the 5+
                                                  layer stage plus the auxiliary 4-layer tail,
                                                  which is the only reason those rows are scored)

THE RULE, and why it has no parameters. Every row of the pair dump carries, in its `lgt` column,
the logit the head that produced the dump gave that exact row, so the reference operating point is
IN THE DATA. For each bar: measure the fraction of TRUE pairs in its universe x band that the
reference head admits at the reference bar (the SHIPPED table below), then take the quantile of the
NEW head's true-pair logits in the same cell that reproduces that fraction. Signal acceptance is
pinned by construction, so the only thing free to move is the false-positive rate, which is exactly
the quantity head quality is supposed to improve. Pair acceptance rate and per-target conversion
rate are MIXED quantities -- matching either instead has failed -- so they are reported as outputs,
never matched.

The 2x10 (pT x |eta|) cell table is REPORTED as a diagnostic per bar. The C++ bars are scalars per
eta band, so a per-cell bar is not expressible without new plumbing; the diagnostic says by how much
one scalar per band misses the per-cell acceptance it is standing in for.

Inputs -- neither lives in this repository:
  --lab   a directory of the .npy columns the attach labeller writes (X, y, st, vxy, peta, lgt,
          evt, z3), built from a pair dump of the head named in the SHIPPED table.
  --model a PyTorch checkpoint (.pt) of the NEW head, holding "state_dict", "arch", "z3_mean" and
          "z3_std", and optionally "arm".
  --lab2  optional second corpus in the same format, for a bar that must hold on two samples.

Output: --out, a JSON file with, per bar, the reference and new bar values, the fitted and judged
true-pair acceptance, the fake-weighted false-positive rate on both sides and its ratio, plus the
per-cell diagnostic; the same numbers are printed as they are computed.

Run:
  python3 barfit_attach.py --lab <labdir> --model <head>.pt --out bars.json \
      [--lab2 <labdir2>] [--fit-on trainval|test|all] [--judge-on test|all] [--seed 42] \
      [--offset 0.0]
"""
import argparse
import json
import os

import numpy as np


def tonp(t):
    """torch -> numpy without the numpy bridge.

    The CMSSW py3-torch-cuda build in this release is compiled WITHOUT NumPy support, so
    `Tensor.numpy()` raises. DLPack is zero-copy and available; the copy() is because the source
    tensor is a slice of a buffer the caller may reuse.
    """
    import numpy as _np
    return _np.from_dlpack(t.detach().cpu()).copy()

# The bars of the head that produced the dump: the reference operating point this fit pins per-cell
# TRUE-pair acceptance to. They pair with the dump's `lgt` column, which is that same head's logit
# on that exact row, so the reference is in the data and the fit needs no free parameter. Update
# these together with the dump, never separately.
SHIPPED = {
    "attachTheta": 6.626049, "attachThetaT": 5.753662, "attachThetaE": 5.907102,
    "attachThetaT3": 5.515511, "rpsThetaChain": 5.480793,
    "xcTheta": 3.430463, "xcThetaT": 2.82865, "xcThetaE": 3.641699,
}
# universe: 'A' chain 5+ (stage 0), 'B' bare T3 (stage 1), 'C' chain-kind (stage 0 + 2)
BARS = [
    ("attachTheta", "A", 0), ("attachThetaT", "A", 1), ("attachThetaE", "A", 2),
    ("attachThetaT3", "B", None), ("rpsThetaChain", "A", None),
    ("xcTheta", "C", 0), ("xcThetaT", "C", 1), ("xcThetaE", "C", 2),
]
# Attach input 0 is conditioned as clip(log10(pt), -1, 4) then standardized; these are that
# standardization's constants, used here to invert the column back to a pT for the cell binning.
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
    quantile. Ties are resolved toward ADMITTING, because the deployed bars are `>=` tests."""
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
    ap.add_argument("--lab2",
                   help="SECOND corpus, in the same format as --lab. With it, each bar is the "
                        "MINIMUM of its acceptance-matching quantile on --lab and on --lab2, so the "
                        "bar cannot lose TRUE-pair acceptance relative to the reference head on "
                        "EITHER sample. This is not a tuned knob: min() is forced by the direction "
                        "of a `>=` bar, and the quantity protected is the same per-cell signal "
                        "acceptance the one-sample rule pins. It matters because a fit on one sample "
                        "alone raises rpsThetaChain/xcThetaT/xcThetaE and costs retirement recall on "
                        "the other.")
    args = ap.parse_args()

    import torch
    L = args.lab
    X20 = np.load(L + "/X.npy", mmap_mode="r")   # as wide as the dump's own kAttachFeatures row
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

    # The same seeded event-level 60/20/20 split the trainer makes, so the split judged on here is
    # the one the head was not trained on.
    rng = np.random.default_rng(args.seed)
    nev = int(evt.max()) + 1
    perm = rng.permutation(nev)
    ntr, nva = int(0.6 * nev), int(0.2 * nev)
    role = np.zeros(nev, np.int8)
    role[perm[ntr:ntr + nva]] = 1
    role[perm[ntr + nva:]] = 2
    r = role[evt]

    # --- score every row with the new head ------------------------------------------------
    # Columns 11-13 carry the target's three raw chain-gate logits, standardized in the dump with
    # the constants of the head that wrote it; they are restandardized here with this head's own
    # constants. Every other column is used exactly as dumped. A single-output head scores its one
    # logit; a 3-class head is reduced to the same scale as log(pP + pD) - log(pF).
    snew = np.empty(n, np.float32)
    B = 1 << 21
    with torch.no_grad():
        for i in range(0, n, B):
            a = np.array(X20[i:i + B], dtype=np.float32)
            a[:, 11:14] = (np.asarray(z3[i:i + B], dtype=np.float32) - zmu) / zsd
            zz = model(torch.tensor(a, dtype=torch.float32).to(dev))
            s = zz[:, 0] if n_out == 1 else (torch.logsumexp(zz[:, 1:3], dim=1) - zz[:, 0])
            snew[i:i + B] = tonp(s)

    pt = 10.0 ** (np.asarray(X20[:, 0]) * PT_STD + PT_MEAN)
    ae = np.abs(peta)
    eb = etaband(ae)
    cell = wp_bins(pt, ae)

    UNI = {"A": st == 0, "B": st == 1, "C": (st == 0) | (st == 2)}
    fitsel = {"trainval": r != 2, "test": r == 2, "all": np.ones(n, bool)}[args.fit_on]
    jdgsel = {"test": r == 2, "all": np.ones(n, bool)}[args.judge_on]

    # ---- optional second corpus: the same acceptance-matching quantile, computed there ---------
    bar2 = {}
    if args.lab2:
        L2 = args.lab2
        X2 = np.load(L2 + "/X.npy", mmap_mode="r")
        z32 = np.load(L2 + "/z3.npy", mmap_mode="r")
        y2 = np.load(L2 + "/y.npy")
        st2 = np.load(L2 + "/st.npy")
        peta2 = np.load(L2 + "/peta.npy")
        lgt2 = np.load(L2 + "/lgt.npy")
        n2 = len(y2)
        s2 = np.empty(n2, np.float32)
        with torch.no_grad():
            for i in range(0, n2, B):
                a = np.array(X2[i:i + B], dtype=np.float32)
                a[:, 11:14] = (np.asarray(z32[i:i + B], dtype=np.float32) - zmu) / zsd
                zz = model(torch.tensor(a, dtype=torch.float32).to(dev))
                sc = zz[:, 0] if n_out == 1 else (torch.logsumexp(zz[:, 1:3], dim=1) - zz[:, 0])
                s2[i:i + B] = tonp(sc)
        eb2 = etaband(np.abs(peta2))
        UNI2 = {"A": st2 == 0, "B": st2 == 1, "C": (st2 == 0) | (st2 == 2)}
        print("--- second corpus %s: %d rows, %d true" % (L2, n2, int(y2.sum())))
        for name, uni, band in BARS:
            m = UNI2[uni] & (y2 == 1)
            if band is not None:
                m &= eb2 == band
            if not m.any():
                bar2[name] = None
                continue
            ref = float((lgt2[m] >= SHIPPED[name]).mean())
            bar2[name] = quantile_bar(s2[m], ref)
            print("    %-14s lab2 acc_ref %.5f -> bar %.4f (n_true %d)"
                  % (name, ref, bar2[name], int(m.sum())))

    out = {"model": os.path.abspath(args.model), "arm": arm, "fit_on": args.fit_on,
           "offset": args.offset, "shipped": SHIPPED, "lab2": args.lab2, "bars": {}, "cells": {}}
    for name, uni, band in BARS:
        m = UNI[uni] & (y == 1)
        if band is not None:
            m &= eb == band
        mf = m & fitsel
        mj = m & jdgsel
        sb = SHIPPED[name]
        acc_ref = float((lgt[mf] >= sb).mean()) if mf.sum() else float("nan")
        bar = quantile_bar(snew[mf], acc_ref)
        bar_pu = bar
        if args.lab2 and bar is not None and bar2.get(name) is not None:
            bar = min(bar, bar2[name])
        bar = None if bar is None else bar + args.offset
        # Judged on the held-out split: does the fitted bar reproduce the reference acceptance on
        # rows it was not fitted on?
        accj_ref = float((lgt[mj] >= sb).mean()) if mj.sum() else float("nan")
        accj_new = float((snew[mj] >= bar).mean()) if (mj.sum() and bar is not None) else float("nan")
        # The background side at that operating point: with signal acceptance pinned, this is the
        # only quantity a better head can move. Bare-T3 rows are dumped one in sixteen, so they
        # carry weight 16 to undo the prescale and make the rate a rate over all scored pairs.
        fb = UNI[uni] & (y == 0) & jdgsel
        if band is not None:
            fb &= eb == band
        w = np.where(st[fb] == 1, 16.0, 1.0)
        fpr_ref = float((w * (lgt[fb] >= sb)).sum() / max(w.sum(), 1))
        fpr_new = float((w * (snew[fb] >= bar)).sum() / max(w.sum(), 1)) if bar is not None else float("nan")
        out["bars"][name] = {
            "universe": uni, "band": band, "shipped": sb, "new": bar,
            "bar_lab1": bar_pu, "bar_lab2": bar2.get(name),
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
        # Per-cell diagnostic: reference vs new acceptance at the SCALAR bar, cells with too few
        # true pairs to measure skipped. It is how far one scalar per band is from per-cell.
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

    # Head-quality summary, one number per universe: the fake-weighted false-positive rate the two
    # heads reach when signal acceptance is matched CELL BY CELL rather than per band, so the
    # comparison is not helped or hurt by how well a single scalar covers the cells.
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
