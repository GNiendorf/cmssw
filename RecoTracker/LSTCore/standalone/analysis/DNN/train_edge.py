#!/usr/bin/env python3

# SUPERSEDED -- does NOT produce the shipped EdgeNetworkWeights.h.
# The shipped edge head comes from standalone/nnloop_ref/s1_work/train_s1.py (NN-loop arm G,
# on-policy, 3-class equal class weights). See analysis/DNN/README.md. Kept for lineage only.

"""M2 gate study + first edge-classifier training for the LST chain-tracking prototype.

Reads the flat per-edge dump produced by DumpWriter (TTree "edges", one entry per
edge; feature order recorded in the TNamed "feature_spec"), runs the analytic gate
study (plan section 3 K2, efficiency-first: NO tightening beyond 99.9 percent true-edge
efficiency, thresholds set per simVxy stratum and the LOOSER one binds), then trains a
small MLP edge classifier and evaluates it on held-out TEST events.

Reproducibility: fixed seeds for numpy and torch; event-level split (plan 5c: never
row-level); normalization saved alongside the model.

v2 changes (each individually disableable so v1 is reproducible via flags):
  1. Feature conditioning BEFORE standardization (--no-feature-clip disables):
     clip kappaSigned to [-1,1], log10R to [0,9], dKappa to [-2,2];
     centerDist -> log10(1+centerDist) in place (same ef_07 slot). The 9 degenerate
     t3_radius~1e-9 rows otherwise blow up the standardization of these columns.
     The applied spec is recorded in the norm json for the C++ inference port.
  2. Longer training: default epochs 100, early-stop patience 8 on val AUC
     (v1: --epochs 30 --patience 5).
  3. Displaced weighting (plan 5c joint density grid, simplified;
     --no-displaced-weight disables): per-TRUE-edge weight = inverse frequency in
     coarse joint bins of log10(simPt) x displacement class [vxy<1, 1-5, >=5],
     relative to the most populated bin, clipped to [1,20], normalized to mean 1
     over train trues. Fakes keep weight 1 (times pos_weight).
Exact v1 behavior: --no-feature-clip --no-displaced-weight --epochs 30 --patience 5
                   --out-model edge_mlp_v1.pt --out-norm edge_norm_v1.json

Outputs:
  edge_mlp_v2.pt    - torch state_dict + metadata (arch, feature names, best epoch/AUC)
  edge_norm_v2.json - per-feature mean/std + conditioning/weighting spec
"""

import argparse
import copy
import json
import os
import sys
import time

import numpy as np

T0 = time.time()


def log(msg):
    print(f"[{time.time() - T0:8.1f}s] {msg}", flush=True)


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    d = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype"
    p.add_argument("--input", nargs="+", default=[f"{d}/edges_300evt.root"],
                   help="edge dump file(s). ONE file: original behavior (train_frac/"
                        "val_frac event split). MULTIPLE files: M8 COMBINATION RULE -- "
                        "file[0] is the PRIMARY (its original seed-<seed> test events "
                        "stay the frozen test set); extra files drop every event whose "
                        "evt id appears in the primary file, and the remaining events "
                        "(primary non-test + extra new-id) are re-split by event "
                        "pool_train_frac/(1-pool_train_frac) into train/val")
    p.add_argument("--pool-train-frac", type=float, default=0.75,
                   help="combined mode only: train fraction of the (non-frozen-test) "
                        "event pool")
    p.add_argument("--out-model", default=f"{d}/edge_mlp_v2.pt")
    p.add_argument("--out-norm", default=f"{d}/edge_norm_v2.json")
    p.add_argument("--seed", type=int, default=42, help="seed for numpy AND torch")
    p.add_argument("--max-fakes", type=int, default=0,
                   help="subsample fakes to at most this many rows (0 = keep all; "
                        "true edges are always all kept)")
    p.add_argument("--epochs", type=int, default=100)
    p.add_argument("--patience", type=int, default=8, help="early stopping on val AUC")
    p.add_argument("--no-feature-clip", action="store_true",
                   help="disable v2 feature conditioning (v1 behavior)")
    p.add_argument("--no-displaced-weight", action="store_true",
                   help="disable v2 joint (log10 simPt x vxy class) true-edge weighting "
                        "(v1 behavior)")
    p.add_argument("--batch-size", type=int, default=65536)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--train-frac", type=float, default=0.6)
    p.add_argument("--val-frac", type=float, default=0.2)
    p.add_argument("--state-file", default="",
                   help="checkpoint/resume file: state (model+optimizer+rng+best "
                        "trackers) is saved after every epoch; if the file exists the "
                        "run resumes from it EXACTLY (same command). The data pipeline "
                        "(load/split/standardize) is deterministic and recomputed.")
    p.add_argument("--wall-limit-sec", type=float, default=0.0,
                   help="if > 0: after the first epoch that ends beyond this wall time, "
                        "save state and exit(3) (resume by rerunning the same command). "
                        "Lets long trainings run as sequential foreground chunks.")
    return p.parse_args()


# ---------------------------------------------------------------- data loading

META_BRANCHES = ["evt", "lumi", "etype", "label", "simVxy", "simPt"]


def load_dump(path):
    """Load needed columns only. Returns (meta dict, X float32 [N,40], names)."""
    import uproot

    f = uproot.open(path)
    spec = f["feature_spec"].member("fTitle")
    ni_part, ef_part = spec.split(";")
    node_names = ni_part.split(":")[1].split(",")
    edge_names = ef_part.split(":")[1].split(",")
    n_node, n_edge = len(node_names), len(edge_names)
    feat_branches = ([f"ni_{i:02d}" for i in range(n_node)]
                     + [f"no_{i:02d}" for i in range(n_node)]
                     + [f"ef_{i:02d}" for i in range(n_edge)])
    names = ([f"ni_{n}" for n in node_names]
             + [f"no_{n}" for n in node_names]
             + [f"ef_{n}" for n in edge_names])

    tree = f["edges"]
    arr = tree.arrays(META_BRANCHES + feat_branches, library="np")
    meta = {k: arr[k] for k in META_BRANCHES}
    n = len(meta["label"])
    X = np.empty((n, len(feat_branches)), dtype=np.float32)
    for j, b in enumerate(feat_branches):
        X[:, j] = arr[b]
        del arr[b]
    return meta, X, names, edge_names, n_node


def data_quality_report(X, names):
    log("--- data quality (all rows) ---")
    degenerate = []
    print(f"{'feature':>18} {'min':>12} {'max':>12} {'mean':>12} {'std':>12} {'nan':>8} {'inf':>8}")
    for j, name in enumerate(names):
        col = X[:, j]
        n_nan = int(np.isnan(col).sum())
        n_inf = int(np.isinf(col).sum())
        finite = col[np.isfinite(col)] if (n_nan or n_inf) else col
        mn, mx = float(finite.min()), float(finite.max())
        mu, sd = float(finite.mean()), float(finite.std())
        flag = ""
        if sd < 1e-8:
            degenerate.append(name)
            flag = "  <-- DEGENERATE"
        print(f"{name:>18} {mn:>12.4g} {mx:>12.4g} {mu:>12.4g} {sd:>12.4g} {n_nan:>8d} {n_inf:>8d}{flag}")
    n_bad = int((~np.isfinite(X)).sum())
    if n_bad:
        log(f"WARNING: {n_bad} non-finite feature values -> replaced with 0")
        np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
    if degenerate:
        log(f"degenerate (zero-variance) columns: {degenerate}")
    return degenerate


# ------------------------------------------------------- v2 feature conditioning

# Applied IN ORDER to the named columns BEFORE standardization. The C++ inference
# port must reproduce this exactly; the applied spec is written into the norm json.
# Ops: "clip"     -> x = min(max(x, lo), hi)
#      "log10_1p" -> x = log10(1 + x)   (x >= 0; compresses heavy right tail)
CONDITIONING_SPEC = [
    {"feature": "ni_kappaSigned", "op": "clip", "lo": -1.0, "hi": 1.0},
    {"feature": "no_kappaSigned", "op": "clip", "lo": -1.0, "hi": 1.0},
    {"feature": "ni_log10R", "op": "clip", "lo": 0.0, "hi": 9.0},
    {"feature": "no_log10R", "op": "clip", "lo": 0.0, "hi": 9.0},
    {"feature": "ef_dKappa", "op": "clip", "lo": -2.0, "hi": 2.0},
    {"feature": "ef_centerDist", "op": "log10_1p"},  # stays in the ef_07 slot
]


def apply_conditioning(X, names, spec):
    """In-place column conditioning. Returns the applied spec (for the norm json)."""
    for c in spec:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        elif c["op"] == "log10_1p":
            X[:, j] = np.log10(1.0 + X[:, j])
        else:
            raise ValueError(f"unknown conditioning op {c['op']}")
        log(f"conditioned {c['feature']}: {c['op']}"
            + (f" [{c['lo']:g},{c['hi']:g}]" if c["op"] == "clip" else ""))
    return spec


# ------------------------------------------------ v2 displaced true-edge weighting

LPT_EDGES = [0.0, 0.5, 1.0, 1.5, 2.0]  # inner log10(simPt/GeV) bin edges (+-inf outer)
VXY_EDGES = [1.0, 5.0]                 # cm: prompt / 1-5 / >=5 displacement classes
W_CLIP = (1.0, 20.0)


def joint_bin_index(simpt, vxy):
    lpt = np.log10(np.maximum(simpt, 1e-6))
    ipt = np.digitize(lpt, LPT_EDGES)   # 0 .. len(LPT_EDGES)
    ivx = np.digitize(vxy, VXY_EDGES)   # 0 .. len(VXY_EDGES)
    return ipt * (len(VXY_EDGES) + 1) + ivx


def displaced_weights(meta, tr):
    """Per-row sample weights (plan 5c joint density grid, simplified).

    Bin weights are FIT PARAMETERS from TRAIN true edges only: inverse frequency of
    the coarse joint (log10 simPt x vxy class) bin relative to the most populated
    bin, clipped to W_CLIP, then the per-edge weights are normalized to mean 1 over
    train trues (so the total positive mass matches the unweighted v1 loss).
    Fakes keep weight exactly 1. Returns (w [float32, all rows], spec dict)."""
    is_true = meta["label"] == 1
    b = joint_bin_index(meta["simPt"], meta["simVxy"])
    n_bins = (len(LPT_EDGES) + 1) * (len(VXY_EDGES) + 1)
    cnt = np.bincount(b[tr & is_true], minlength=n_bins).astype(np.float64)
    w_bin = np.full(n_bins, W_CLIP[1])
    occ = cnt > 0
    w_bin[occ] = np.clip(cnt.max() / cnt[occ], W_CLIP[0], W_CLIP[1])
    w = np.ones(len(b), dtype=np.float64)
    w[is_true] = w_bin[b[is_true]]
    scale = float(w[tr & is_true].mean())
    w[is_true] /= scale
    nvx = len(VXY_EDGES) + 1
    print(f"  {'log10pt bin':>16} {'vxy class':>10} {'n_train_true':>12} "
          f"{'w_raw':>8} {'w_final':>8}")
    lpt_lab = ([f"<{LPT_EDGES[0]:g}"]
               + [f"[{a:g},{b_:g})" for a, b_ in zip(LPT_EDGES[:-1], LPT_EDGES[1:])]
               + [f">={LPT_EDGES[-1]:g}"])
    vxy_lab = [f"<{VXY_EDGES[0]:g}"] + [
        f"[{a:g},{b_:g})" for a, b_ in zip(VXY_EDGES[:-1], VXY_EDGES[1:])
    ] + [f">={VXY_EDGES[-1]:g}"]
    for ip in range(len(LPT_EDGES) + 1):
        for iv in range(nvx):
            k = ip * nvx + iv
            print(f"  {lpt_lab[ip]:>16} {vxy_lab[iv]:>10} {int(cnt[k]):>12d} "
                  f"{w_bin[k]:>8.3f} {w_bin[k] / scale:>8.3f}")
    spec = {"lpt_edges": LPT_EDGES, "vxy_edges": VXY_EDGES,
            "weight_clip": list(W_CLIP), "bin_counts_train_true": cnt.tolist(),
            "bin_weight_raw": w_bin.tolist(), "norm_scale": scale,
            "bin_weight_final": (w_bin / scale).tolist(),
            "note": "true-edge weight = bin_weight_final[joint bin]; fakes weight 1"}
    return w.astype(np.float32), spec


# ---------------------------------------------------------------- event split

def event_split(meta, train_frac, val_frac, rng):
    """60/20/20 split on (lumi, evt) keys. Row-level splits are forbidden (plan 5c)."""
    key = (meta["lumi"].astype(np.uint64) << np.uint64(32)) | meta["evt"].astype(np.uint64)
    uniq = np.unique(key)
    rng.shuffle(uniq)
    n = len(uniq)
    n_tr = int(round(train_frac * n))
    n_va = int(round(val_frac * n))
    tr_keys, va_keys, te_keys = uniq[:n_tr], uniq[n_tr:n_tr + n_va], uniq[n_tr + n_va:]
    tr = np.isin(key, tr_keys)
    va = np.isin(key, va_keys)
    te = np.isin(key, te_keys)
    log(f"event split: {n_tr}/{n_va}/{n - n_tr - n_va} events -> "
        f"{tr.sum()}/{va.sum()}/{te.sum()} edges (train/val/test)")
    return tr, va, te


def combined_event_split(meta, src, args, rng):
    """M8 COMBINATION RULE split (leak-proof, plan 10.4c).

    The frozen test set is the ORIGINAL seed event-level test split of the PRIMARY
    (first --input) file, reproduced exactly: shuffle the primary file's unique
    (lumi,evt) keys with the fresh seed-<seed> rng (the same first rng use as the
    original event_split) and take the same train_frac/val_frac slices. All other
    events -- the primary train+val events plus the kept extra-file events (overlap
    ids dropped at load) -- form the pool, re-split by event
    pool_train_frac/(1-pool_train_frac). Reported metrics stay comparable to every
    previous milestone because the test events never change."""
    key = (meta["lumi"].astype(np.uint64) << np.uint64(32)) | meta["evt"].astype(np.uint64)
    uniq0 = np.unique(key[src == 0])
    rng.shuffle(uniq0)
    n0 = len(uniq0)
    n_tr0 = int(round(args.train_frac * n0))
    n_va0 = int(round(args.val_frac * n0))
    te_keys = uniq0[n_tr0 + n_va0:]
    te = np.isin(key, te_keys)
    assert not (te & (src != 0)).any(), "frozen-test key present in an extra input (leak)"
    pool_keys = np.unique(key[~te])
    rng.shuffle(pool_keys)
    n_ptr = int(round(args.pool_train_frac * len(pool_keys)))
    tr_keys, va_keys = pool_keys[:n_ptr], pool_keys[n_ptr:]
    tr = np.isin(key, tr_keys)
    va = np.isin(key, va_keys)
    prim = set(uniq0.tolist())
    n_tr_p = sum(1 for k in tr_keys.tolist() if k in prim)
    n_va_p = sum(1 for k in va_keys.tolist() if k in prim)
    log(f"COMBINED event split: frozen test = {len(te_keys)} primary events (unchanged); "
        f"pool {len(pool_keys)} events -> train {len(tr_keys)} "
        f"({n_tr_p} primary + {len(tr_keys) - n_tr_p} extra) / "
        f"val {len(va_keys)} ({n_va_p} primary + {len(va_keys) - n_va_p} extra) "
        f"-> {tr.sum()}/{va.sum()}/{te.sum()} edges (train/val/test)")
    return tr, va, te


# ---------------------------------------------------------------- gate study

def quantile_thr_upper(vals, eff):
    """Smallest upper cut keeping >= eff of vals (cut: v <= thr)."""
    return float(np.quantile(vals, eff, method="higher"))


def gate_study(X, names, meta, mask, effs=(0.999, 0.995)):
    """Plan section 3 K2 gate study on TRUE edges of `mask` rows.

    For each gate the threshold is computed SEPARATELY in strata simVxy < 1 cm and
    simVxy >= 1 cm; the binding threshold is the LOOSER of the two so BOTH strata
    retain >= the target efficiency. chargeAgree is binary: the gate (==1) is only
    usable if it alone keeps >= target efficiency in both strata.
    Returns {eff: dict} with thresholds, per-stratum efficiencies and fake rejections.
    """
    i_dkr = names.index("ef_dKappaRel")
    i_chg = names.index("ef_chargeAgree")
    i_dtl = names.index("ef_dTanLambda")

    lab = meta["label"][mask]
    vxy = meta["simVxy"][mask]
    dkr = X[mask, i_dkr]
    chg = X[mask, i_chg]
    adtl = np.abs(X[mask, i_dtl])

    is_true = lab == 1
    is_fake = ~is_true
    prompt = is_true & (vxy < 1.0)
    displ = is_true & (vxy >= 1.0)
    n_fake = is_fake.sum()
    log(f"gate study rows: {is_true.sum()} true ({prompt.sum()} prompt, "
        f"{displ.sum()} displaced vxy>=1cm), {n_fake} fake")

    out = {}
    for eff in effs:
        thr_dkr = max(quantile_thr_upper(dkr[prompt], eff), quantile_thr_upper(dkr[displ], eff))
        thr_dtl = max(quantile_thr_upper(adtl[prompt], eff), quantile_thr_upper(adtl[displ], eff))
        chg_eff_p = float((chg[prompt] == 1).mean())
        chg_eff_d = float((chg[displ] == 1).mean())
        chg_usable = chg_eff_p >= eff and chg_eff_d >= eff

        pass_dkr = dkr <= thr_dkr
        pass_dtl = adtl <= thr_dtl
        pass_chg = (chg == 1) if chg_usable else np.ones_like(pass_dkr)
        pass_and = pass_dkr & pass_dtl & pass_chg

        def stats(p):
            return {"eff_all": float(p[is_true].mean()),
                    "eff_prompt": float(p[prompt].mean()),
                    "eff_displ": float(p[displ].mean()),
                    "fake_rej": float((~p[is_fake]).mean())}

        out[eff] = {"thr_dKappaRel": thr_dkr, "thr_absdTanLambda": thr_dtl,
                    "chargeAgree_eff_prompt": chg_eff_p, "chargeAgree_eff_displ": chg_eff_d,
                    "chargeAgree_usable": chg_usable,
                    "dKappaRel": stats(pass_dkr), "absdTanLambda": stats(pass_dtl),
                    "chargeAgree": stats(chg == 1), "AND": stats(pass_and)}

        print(f"\n=== gate study @ {eff * 100:.1f}% per-stratum true-edge efficiency ===")
        print(f"  thresholds: dKappaRel <= {thr_dkr:.6g}  |dTanLambda| <= {thr_dtl:.6g}  "
              f"chargeAgree==1 {'APPLIED' if chg_usable else 'NOT USABLE (eff below target) -> pass-all'}")
        print(f"  chargeAgree==1 true-eff: prompt {chg_eff_p:.5f}  displaced {chg_eff_d:.5f}")
        print(f"  {'gate':>15} {'eff_all':>9} {'eff_prompt':>10} {'eff_displ':>9} {'fake_rej':>9}")
        for g in ("chargeAgree", "dKappaRel", "absdTanLambda", "AND"):
            s = out[eff][g]
            print(f"  {g:>15} {s['eff_all']:>9.5f} {s['eff_prompt']:>10.5f} "
                  f"{s['eff_displ']:>9.5f} {s['fake_rej']:>9.5f}")
    return out


def gate_pass_mask(X, names, study, eff):
    """Apply the AND of the gates fit at `eff` to arbitrary rows."""
    g = study[eff]
    p = X[:, names.index("ef_dKappaRel")] <= g["thr_dKappaRel"]
    p &= np.abs(X[:, names.index("ef_dTanLambda")]) <= g["thr_absdTanLambda"]
    if g["chargeAgree_usable"]:
        p &= X[:, names.index("ef_chargeAgree")] == 1
    return p


# ---------------------------------------------------------------- model

def build_model(n_in):
    import torch.nn as nn
    return nn.Sequential(nn.Linear(n_in, 32), nn.ReLU(),
                         nn.Linear(32, 32), nn.ReLU(),
                         nn.Linear(32, 1))


def batched_scores(model, X_t, device, bs=1 << 20):
    import torch
    model.eval()
    out = np.empty(len(X_t), dtype=np.float32)
    with torch.no_grad():
        for i in range(0, len(X_t), bs):
            logits = model(X_t[i:i + bs].to(device)).squeeze(1).float().cpu()
            # CMSSW torch build lacks numpy interop (.numpy() raises); tolist is fast enough.
            out[i:i + bs] = logits.tolist()
    return out


def rejection_at_eff(s_true, s_fake, eff):
    """Fake rejection at >= eff true-edge efficiency (keep score >= thr)."""
    thr = float(np.quantile(s_true, 1.0 - eff, method="lower"))
    return float((s_fake < thr).mean()), thr


def eval_block(tag, s_true, s_fake, effs=(0.99, 0.995, 0.999)):
    from sklearn.metrics import roc_auc_score
    y = np.concatenate([np.ones(len(s_true)), np.zeros(len(s_fake))])
    s = np.concatenate([s_true, s_fake])
    auc = roc_auc_score(y, s)
    rejs = {e: rejection_at_eff(s_true, s_fake, e)[0] for e in effs}
    print(f"  {tag:>22} n_true={len(s_true):>8d} n_fake={len(s_fake):>8d} AUC={auc:.5f} "
          + " ".join(f"rej@{e * 100:g}%={rejs[e]:.5f}" for e in effs))
    return {"auc": float(auc), **{f"rej@{e}": rejs[e] for e in effs}}


# ---------------------------------------------------------------- main

def main():
    args = parse_args()
    import torch

    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.cuda.manual_seed_all(args.seed)
    rng = np.random.default_rng(args.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log(f"seed={args.seed} device={device}")

    # Load input file(s). Multiple files engage the M8 COMBINATION RULE: file[0] is
    # the primary; extra files drop every event whose evt id appears in the primary.
    metas, x_parts, src_parts = [], [], []
    names = edge_names = n_node = prim_evts = None
    for si, path in enumerate(args.input):
        meta_i, X_i, names_i, edge_names_i, n_node_i = load_dump(path)
        if si == 0:
            names, edge_names, n_node = names_i, edge_names_i, n_node_i
            prim_evts = np.unique(meta_i["evt"])
            log(f"input[0] PRIMARY {path}: {len(X_i)} edges over {len(prim_evts)} events")
        else:
            assert names_i == names, f"feature_spec mismatch: {path}"
            evts_i = np.unique(meta_i["evt"])
            keep = ~np.isin(meta_i["evt"], prim_evts)
            kept_evts = np.unique(meta_i["evt"][keep])
            log(f"input[{si}] EXTRA {path}: {len(X_i)} edges over {len(evts_i)} events; "
                f"combination rule drops {len(evts_i) - len(kept_evts)} overlap events -> "
                f"keep {int(keep.sum())} edges over {len(kept_evts)} events")
            meta_i = {k: v[keep] for k, v in meta_i.items()}
            X_i = X_i[keep]
        metas.append(meta_i)
        x_parts.append(X_i)
        src_parts.append(np.full(len(X_i), si, dtype=np.int8))
    if len(args.input) == 1:
        meta, X, src = metas[0], x_parts[0], None
    else:
        meta = {k: np.concatenate([m[k] for m in metas]) for k in META_BRANCHES}
        X = np.concatenate(x_parts)
        src = np.concatenate(src_parts)
    del metas, x_parts, src_parts
    n_all = len(X)
    log(f"loaded {n_all} edges x {X.shape[1]} features from {len(args.input)} file(s)")
    assert X.shape[1] == 2 * n_node + len(edge_names)

    degenerate = data_quality_report(X, names)

    # ---- v2 change 1: feature conditioning (before standardization) ----
    conditioning = []
    if not args.no_feature_clip:
        conditioning = apply_conditioning(X, names, CONDITIONING_SPEC)

    # Optional fake subsampling (true edges always all kept).
    lab_all = meta["label"]
    if args.max_fakes > 0:
        fake_idx = np.flatnonzero(lab_all == 0)
        if len(fake_idx) > args.max_fakes:
            keep_fake = rng.choice(fake_idx, size=args.max_fakes, replace=False)
            keep = np.zeros(n_all, dtype=bool)
            keep[lab_all == 1] = True
            keep[keep_fake] = True
            X = X[keep]
            meta = {k: v[keep] for k, v in meta.items()}
            if src is not None:
                src = src[keep]
            log(f"subsampled fakes {len(fake_idx)} -> {args.max_fakes} (seed {args.seed}); "
                f"total rows {len(X)}")

    if src is None:
        tr, va, te = event_split(meta, args.train_frac, args.val_frac, rng)
    else:
        tr, va, te = combined_event_split(meta, src, args, rng)

    # ---- gate study on TRAIN events (thresholds are fit parameters) ----
    study = gate_study(X, names, meta, tr)

    # ---- standardization from TRAIN rows ----
    mu = X[tr].mean(axis=0, dtype=np.float64).astype(np.float32)
    sd = X[tr].std(axis=0, dtype=np.float64).astype(np.float32)
    sd[sd < 1e-8] = 1.0
    Xs = (X - mu) / sd

    y = (meta["label"] == 1).astype(np.float32)
    n_pos, n_neg = int(y[tr].sum()), int((1 - y[tr]).sum())
    pos_weight = n_neg / max(n_pos, 1)
    log(f"train: {n_pos} true / {n_neg} fake -> pos_weight={pos_weight:.4f}")

    # ---- v2 change 3: displaced/pt joint-density true-edge weighting ----
    use_w = not args.no_displaced_weight
    weight_spec = None
    if use_w:
        log("displaced weighting (fit on TRAIN true edges):")
        w_all, weight_spec = displaced_weights(meta, tr)

    # torch.tensor(ndarray) uses the buffer protocol and is fast even in builds
    # without numpy interop (torch.from_numpy raises in the CMSSW python env).
    Xtr = torch.tensor(np.ascontiguousarray(Xs[tr]))
    ytr = torch.tensor(np.ascontiguousarray(y[tr]))
    Xva = torch.tensor(np.ascontiguousarray(Xs[va]))
    yva_np = y[va]
    if device.type == "cuda":
        Xtr, ytr = Xtr.to(device), ytr.to(device)
    if use_w:
        wtr = torch.tensor(np.ascontiguousarray(w_all[tr]))
        if device.type == "cuda":
            wtr = wtr.to(device)

    model = build_model(X.shape[1]).to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    # With weighting: elementwise loss * per-sample weight, then mean — identical to
    # the v1 mean-reduction loss when all weights are 1.
    crit = torch.nn.BCEWithLogitsLoss(pos_weight=torch.tensor(pos_weight, device=device),
                                      reduction="none" if use_w else "mean")

    from sklearn.metrics import roc_auc_score
    gen = torch.Generator(device="cpu").manual_seed(args.seed)
    best_auc, best_state, best_epoch, bad = -1.0, None, -1, 0
    start_epoch = 1
    if args.state_file and os.path.exists(args.state_file):
        st = torch.load(args.state_file, map_location="cpu", weights_only=False)
        model.load_state_dict(st["model"])
        model.to(device)
        opt.load_state_dict(st["opt"])
        gen.set_state(st["gen"])
        best_auc, best_epoch, bad = st["best_auc"], st["best_epoch"], st["bad"]
        best_state = st["best_state"]
        start_epoch = st["epoch"] + 1
        log(f"RESUMED from {args.state_file}: next epoch {start_epoch}, "
            f"best val AUC {best_auc:.5f} @ epoch {best_epoch}, bad={bad}")
    n_tr = len(Xtr)
    for epoch in range(start_epoch, args.epochs + 1):
        model.train()
        perm = torch.randperm(n_tr, generator=gen)
        tot_loss = 0.0
        for i in range(0, n_tr, args.batch_size):
            idx = perm[i:i + args.batch_size]
            if device.type == "cuda":
                idx = idx.to(device)
            xb, yb = Xtr[idx], ytr[idx]
            opt.zero_grad()
            loss = crit(model(xb).squeeze(1), yb)
            if use_w:
                loss = (loss * wtr[idx]).mean()
            loss.backward()
            opt.step()
            tot_loss += float(loss.detach()) * len(idx)
        s_va = batched_scores(model, Xva, device)
        auc = roc_auc_score(yva_np, s_va)
        log(f"epoch {epoch:2d} train_loss={tot_loss / n_tr:.5f} val_auc={auc:.5f}")
        stop = False
        if auc > best_auc:
            best_auc, best_epoch, bad = auc, epoch, 0
            best_state = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
        else:
            bad += 1
            if bad >= args.patience:
                log(f"early stop at epoch {epoch} (best val AUC {best_auc:.5f} @ epoch {best_epoch})")
                stop = True
        if args.state_file:
            tmp = args.state_file + ".tmp"
            torch.save({"model": {k: v.cpu() for k, v in model.state_dict().items()},
                        "opt": opt.state_dict(), "gen": gen.get_state(),
                        "best_auc": best_auc, "best_epoch": best_epoch, "bad": bad,
                        "best_state": best_state, "epoch": epoch}, tmp)
            os.replace(tmp, args.state_file)
        if stop:
            break
        if (args.wall_limit_sec > 0 and time.time() - T0 > args.wall_limit_sec
                and epoch < args.epochs):
            log(f"wall limit {args.wall_limit_sec:.0f}s reached after epoch {epoch}; "
                f"state saved to {args.state_file} -> exit(3), rerun same command to resume")
            sys.exit(3)
    model.load_state_dict(best_state)
    model.to(device)

    # ---- save model + normalization ----
    torch.save({"state_dict": best_state, "arch": [X.shape[1], 32, 32, 1],
                "feature_names": names, "seed": args.seed,
                "conditioning": conditioning,
                "best_epoch": best_epoch, "best_val_auc": float(best_auc)}, args.out_model)
    with open(args.out_norm, "w") as fh:
        json.dump({"feature_names": names,
                   # C++ inference: apply "conditioning" ops IN ORDER to the raw
                   # features FIRST, then x_std = (x - mean) / std.
                   "conditioning": conditioning,
                   "mean": mu.tolist(), "std": sd.tolist(),
                   "seed": args.seed, "degenerate_columns": degenerate,
                   "displaced_weighting": weight_spec,
                   "train_args": {"epochs": args.epochs, "patience": args.patience,
                                  "batch_size": args.batch_size, "lr": args.lr,
                                  "feature_clip": not args.no_feature_clip,
                                  "displaced_weight": use_w,
                                  "inputs": args.input,
                                  "pool_train_frac": args.pool_train_frac}}, fh, indent=1)
    log(f"saved {args.out_model} and {args.out_norm}")

    # ---- TEST evaluation ----
    Xte = torch.tensor(np.ascontiguousarray(Xs[te]))
    s_te = batched_scores(model, Xte, device)
    lab_te = meta["label"][te]
    vxy_te = meta["simVxy"][te]
    ety_te = meta["etype"][te]
    is_true = lab_te == 1
    is_fake = ~is_true

    print("\n=== MLP on TEST events (thresholds from each subgroup's true edges; "
          "vxy subgroups share ALL test fakes, etype subgroups use same-etype fakes) ===")
    res = {}
    s_fake_all = s_te[is_fake]
    res["all"] = eval_block("all", s_te[is_true], s_fake_all)
    res["prompt vxy<1"] = eval_block("prompt vxy<1", s_te[is_true & (vxy_te < 1)], s_fake_all)
    res["displaced vxy>=1"] = eval_block("displaced vxy>=1", s_te[is_true & (vxy_te >= 1)], s_fake_all)
    res["displaced vxy>=5"] = eval_block("displaced vxy>=5", s_te[is_true & (vxy_te >= 5)], s_fake_all)
    for et, tag in ((1, "E1 sharedMD"), (2, "E2 sharedLS")):
        m_et = ety_te == et
        res[tag] = eval_block(tag, s_te[is_true & m_et], s_te[is_fake & m_et])

    print("\n=== gates-AND baseline on TEST (thresholds fit on train @99.9%/stratum) ===")
    for eff in (0.999, 0.995):
        p = gate_pass_mask(X[te], names, study, eff)
        print(f"  gates@{eff * 100:.1f}%: true-eff all={p[is_true].mean():.5f} "
              f"prompt={p[is_true & (vxy_te < 1)].mean():.5f} "
              f"displ>=1={p[is_true & (vxy_te >= 1)].mean():.5f} "
              f"displ>=5={p[is_true & (vxy_te >= 5)].mean():.5f} "
              f"fake_rej={(~p[is_fake]).mean():.5f}")

    log("done")


if __name__ == "__main__":
    main()
