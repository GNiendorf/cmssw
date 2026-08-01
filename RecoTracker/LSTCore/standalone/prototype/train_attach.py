#!/usr/bin/env python3
"""M7 K8 attach-head (pair classifier) training for the LST chain-tracking prototype.

Reads the flat per-pair dump produced by PairDumpWriter (TTree "pairs", one entry per
PREFILTERED (accepted nLayers>=5 chain, pLS) pair; feature order recorded in the TNamed
"feature_spec" = the frozen PixelAttach.h kAttachFeat contract), trains a small MLP pair
head (plan 5a additive evidence: chain+pLS -> pT5-class TC) and evaluates it on held-out
TEST events.

Mirrors train_chain.py discipline exactly:
  - fixed seeds for numpy AND torch,
  - event-level 60/20/20 split by evt (row-level splits forbidden, plan 5c),
  - feature conditioning BEFORE standardization, recorded in the norm json for the
    C++ port (export_attach_weights.py bakes it into attach_mlp_weights.h),
  - standardization mean/std fit on TRAIN rows only, saved to the norm json,
  - class imbalance via BCEWithLogits pos_weight = n_fake/n_true (train),
  - 100 epochs, early stopping patience 10 on val AUC.

NO displaced weighting (deliberate M7 deviation from train_chain.py): displaced true
pairs are RARE because displaced tracks have no pLS -- that is physically correct.
Their count is reported; forcing a weight on O(1e3) rows of 27M would only add noise.

Conditioning (v2-edge lesson: heavy-tailed columns must be tamed BEFORE
standardization or they cripple their own slots; measured on the 300-evt dump):
  af_01 ptErrRel          -> log10_1p      (right tail to 74, bulk at 2e-3)
  af_16 circleCenterDist  -> log10_1p      (right tail to 3.3e7 cm, q99.9 = 1.5e4)
  af_00 log10PtIn         -> clip [-1, 4]  (junk seeds to log10(pt) = 5.6, q99.9 = 2.2)
  af_05 log10CircleRadius -> clip [1, 5]   (same junk seeds, max 7.5, q99.9 = 4.2)

Report: test AUC overall + per chainNLayers (5 / 6+) + prompt/displaced (vxy>=1;
pileup-sim true pairs carry simVxy = -999 and are reported as their own stratum).

Outputs:
  attach_mlp_v1.pt    - torch state_dict + metadata (arch, feature names, best epoch/AUC)
  attach_norm_v1.json - per-feature mean/std + conditioning spec
"""

import argparse
import copy
import json
import time

import numpy as np

T0 = time.time()

N_ATTACH_FEAT = 18


def log(msg):
    print(f"[{time.time() - T0:8.1f}s] {msg}", flush=True)


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    d = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype"
    p.add_argument("--input", default=f"{d}/pairs_300evt.root")
    p.add_argument("--out-model", default=f"{d}/attach_mlp_v1.pt")
    p.add_argument("--out-norm", default=f"{d}/attach_norm_v1.json")
    p.add_argument("--seed", type=int, default=42, help="seed for numpy AND torch")
    p.add_argument("--epochs", type=int, default=100)
    p.add_argument("--patience", type=int, default=10, help="early stopping on val AUC")
    p.add_argument("--no-feature-clip", action="store_true",
                   help="disable feature conditioning (debug)")
    p.add_argument("--batch-size", type=int, default=16384)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--train-frac", type=float, default=0.6)
    p.add_argument("--val-frac", type=float, default=0.2)
    return p.parse_args()


# ---------------------------------------------------------------- data loading

META_BRANCHES = ["evt", "label", "simVxy", "simPt", "chainNLayers"]


def load_dump(path):
    """Load the pair dump. Returns (meta dict, X float32 [N,18], names)."""
    import uproot

    f = uproot.open(path)
    spec = f["feature_spec"].member("fTitle")
    assert spec.startswith("af:"), f"unexpected feature_spec '{spec[:20]}...'"
    af_names = spec[3:].split(",")
    assert len(af_names) == N_ATTACH_FEAT, f"expected {N_ATTACH_FEAT} features, got {len(af_names)}"
    feat_branches = [f"af_{i:02d}" for i in range(N_ATTACH_FEAT)]
    names = [f"af_{n}" for n in af_names]

    tree = f["pairs"]
    arr = tree.arrays(META_BRANCHES + feat_branches, library="np")
    meta = {k: arr[k] for k in META_BRANCHES}
    n = len(meta["label"])
    X = np.empty((n, N_ATTACH_FEAT), dtype=np.float32)
    for j, b in enumerate(feat_branches):
        X[:, j] = arr[b]
        del arr[b]
    return meta, X, names


def data_quality_report(X, names):
    log("--- data quality (all rows) ---")
    degenerate = []
    print(f"{'feature':>26} {'min':>12} {'max':>12} {'mean':>12} {'std':>12} {'nan':>8} {'inf':>8}")
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
        print(f"{name:>26} {mn:>12.4g} {mx:>12.4g} {mu:>12.4g} {sd:>12.4g} {n_nan:>8d} {n_inf:>8d}{flag}")
    n_bad = int((~np.isfinite(X)).sum())
    if n_bad:
        log(f"WARNING: {n_bad} non-finite feature values -> replaced with 0 "
            "(PixelAttach.cc sanitize contract says this cannot happen)")
        np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
    if degenerate:
        log(f"degenerate (zero-variance) columns: {degenerate}")
    return degenerate


# ------------------------------------------------------- feature conditioning

# Applied IN ORDER to the named columns BEFORE standardization. The C++ inference
# port must reproduce this exactly; the applied spec is written into the norm json
# (same op vocabulary as train_chain.py / export_chain_weights.py: "clip", "log10_1p").
CONDITIONING_SPEC = [
    {"feature": "af_ptErrRel", "op": "log10_1p"},
    {"feature": "af_circleCenterDist", "op": "log10_1p"},
    {"feature": "af_log10PtIn", "op": "clip", "lo": -1.0, "hi": 4.0},
    {"feature": "af_log10CircleRadius", "op": "clip", "lo": 1.0, "hi": 5.0},
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


# ---------------------------------------------------------------- event split

def event_split(meta, train_frac, val_frac, rng):
    """60/20/20 split on evt keys (the pair dump has no lumi branch; the 300-evt
    RelVal sample has 300 distinct evt values -- asserted implicitly by the count)."""
    key = meta["evt"].astype(np.uint64)
    uniq = np.unique(key)
    rng.shuffle(uniq)
    n = len(uniq)
    n_tr = int(round(train_frac * n))
    n_va = int(round(val_frac * n))
    tr_keys, va_keys, te_keys = uniq[:n_tr], uniq[n_tr:n_tr + n_va], uniq[n_tr + n_va:]
    tr = np.isin(key, tr_keys)
    va = np.isin(key, va_keys)
    te = np.isin(key, te_keys)
    log(f"event split: {n} distinct evt keys -> {n_tr}/{n_va}/{n - n_tr - n_va} events -> "
        f"{tr.sum()}/{va.sum()}/{te.sum()} pairs (train/val/test)")
    return tr, va, te


# ---------------------------------------------------------------- model

def build_model(n_in):
    import torch.nn as nn
    return nn.Sequential(nn.Linear(n_in, 24), nn.ReLU(),
                         nn.Linear(24, 24), nn.ReLU(),
                         nn.Linear(24, 1))


def batched_scores(model, X_t, device, bs=1 << 20):
    import torch
    model.eval()
    out = np.empty(len(X_t), dtype=np.float32)
    with torch.no_grad():
        for i in range(0, len(X_t), bs):
            logits = model(X_t[i:i + bs].to(device)).squeeze(1).float().cpu()
            # CMSSW torch build lacks numpy interop (.numpy() raises); tolist is fine.
            out[i:i + bs] = logits.tolist()
    return out


def eval_block(tag, s_true, s_fake):
    from sklearn.metrics import roc_auc_score
    if len(s_true) == 0 or len(s_fake) == 0:
        print(f"  {tag:>24} n_true={len(s_true):>8d} n_fake={len(s_fake):>8d} AUC=n/a (empty class)")
        return {"auc": None, "n_true": int(len(s_true)), "n_fake": int(len(s_fake))}
    y = np.concatenate([np.ones(len(s_true)), np.zeros(len(s_fake))])
    s = np.concatenate([s_true, s_fake])
    auc = roc_auc_score(y, s)
    print(f"  {tag:>24} n_true={len(s_true):>8d} n_fake={len(s_fake):>8d} AUC={auc:.5f}")
    return {"auc": float(auc), "n_true": int(len(s_true)), "n_fake": int(len(s_fake))}


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

    meta, X, names = load_dump(args.input)
    n_all = len(X)
    log(f"loaded {n_all} pairs x {X.shape[1]} features from {args.input}")

    degenerate = data_quality_report(X, names)

    conditioning = []
    if not args.no_feature_clip:
        conditioning = apply_conditioning(X, names, CONDITIONING_SPEC)

    # Displaced accounting (NO weighting -- see the module docstring): true pairs by
    # sim stratum. Pileup-sim true pairs carry simVxy = simPt = -999 (main.cc pairdump).
    is_true_all = meta["label"] == 1
    acc_true = is_true_all & (meta["simPt"] > -998.0)
    pileup_true = is_true_all & ~acc_true
    disp_true = acc_true & (meta["simVxy"] >= 1.0)
    log(f"true-pair strata: total={int(is_true_all.sum())} "
        f"accepted-sim={int(acc_true.sum())} (displaced vxy>=1: {int(disp_true.sum())}, "
        f"vxy>=5: {int((acc_true & (meta['simVxy'] >= 5.0)).sum())}) "
        f"pileup-sim={int(pileup_true.sum())} -- displaced true pairs are rare because "
        "displaced tracks have no pLS (physically correct); no weighting applied")

    tr, va, te = event_split(meta, args.train_frac, args.val_frac, rng)

    # ---- standardization from TRAIN rows (in-place: X becomes Xs) ----
    mu = X[tr].mean(axis=0, dtype=np.float64).astype(np.float32)
    sd = X[tr].std(axis=0, dtype=np.float64).astype(np.float32)
    sd[sd < 1e-8] = 1.0
    X -= mu
    X /= sd
    Xs = X

    y = is_true_all.astype(np.float32)
    n_pos, n_neg = int(y[tr].sum()), int((1 - y[tr]).sum())
    pos_weight = n_neg / max(n_pos, 1)
    log(f"train: {n_pos} true / {n_neg} fake -> pos_weight={pos_weight:.4f}")

    Xtr = torch.tensor(np.ascontiguousarray(Xs[tr]))
    ytr = torch.tensor(np.ascontiguousarray(y[tr]))
    Xva = torch.tensor(np.ascontiguousarray(Xs[va]))
    yva_np = y[va]
    if device.type == "cuda":
        Xtr, ytr = Xtr.to(device), ytr.to(device)

    model = build_model(X.shape[1]).to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    crit = torch.nn.BCEWithLogitsLoss(pos_weight=torch.tensor(pos_weight, device=device))

    from sklearn.metrics import roc_auc_score

    gen = torch.Generator(device="cpu").manual_seed(args.seed)
    best_auc, best_state, best_epoch, bad = -1.0, None, -1, 0
    n_tr = len(Xtr)
    for epoch in range(1, args.epochs + 1):
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
            loss.backward()
            opt.step()
            tot_loss += float(loss.detach()) * len(idx)
        s_va = batched_scores(model, Xva, device)
        auc = roc_auc_score(yva_np, s_va)
        log(f"epoch {epoch:3d} train_loss={tot_loss / n_tr:.5f} val_auc={auc:.5f}")
        if auc > best_auc:
            best_auc, best_epoch, bad = auc, epoch, 0
            best_state = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
        else:
            bad += 1
            if bad >= args.patience:
                log(f"early stop at epoch {epoch} (best val AUC {best_auc:.5f} @ epoch {best_epoch})")
                break
    log(f"best checkpoint: epoch {best_epoch} val_auc={best_auc:.5f}")
    model.load_state_dict(best_state)
    model.to(device)

    # ---- save model + normalization ----
    torch.save({"state_dict": best_state, "arch": [X.shape[1], 24, 24, 1],
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
                   "displaced_weighting": {"rule": "none (M7: displaced true pairs are "
                                                   "physically rare -- displaced tracks have no pLS)",
                                           "n_true_displaced_total": int(disp_true.sum())},
                   "train_args": {"epochs": args.epochs, "patience": args.patience,
                                  "batch_size": args.batch_size, "lr": args.lr,
                                  "feature_clip": not args.no_feature_clip}}, fh, indent=1)
    log(f"saved {args.out_model} and {args.out_norm}")

    # ---- TEST evaluation: overall + per chainNLayers (5/6+) + prompt/displaced ----
    Xte = torch.tensor(np.ascontiguousarray(Xs[te]))
    s_te = batched_scores(model, Xte, device)
    lab_te = meta["label"][te]
    vxy_te = meta["simVxy"][te]
    pt_te = meta["simPt"][te]
    nl_te = meta["chainNLayers"][te]
    is_true = lab_te == 1
    is_fake = ~is_true
    acc_te = is_true & (pt_te > -998.0)

    print("\n=== attach-head MLP on TEST events ===")
    res = {}
    res["all"] = eval_block("all", s_te[is_true], s_te[is_fake])
    for tag, m in (("chainNLayers=5", nl_te == 5), ("chainNLayers>=6", nl_te >= 6)):
        res[tag] = eval_block(tag, s_te[is_true & m], s_te[is_fake & m])
    # vxy subgroups: true pairs of the stratum vs ALL test fakes (fakes carry no
    # displacement class -- same convention as train_chain.py). Pileup-sim true pairs
    # (simVxy = -999) are their own stratum, NOT lumped into prompt.
    res["prompt vxy<1"] = eval_block("prompt vxy<1", s_te[acc_te & (vxy_te < 1)], s_te[is_fake])
    res["displaced vxy>=1"] = eval_block("displaced vxy>=1", s_te[acc_te & (vxy_te >= 1)], s_te[is_fake])
    res["pileup-sim true"] = eval_block("pileup-sim true", s_te[is_true & ~acc_te], s_te[is_fake])

    # Logit-scale context for the thetaAttach threshold pick in K8.
    for tag, m in (("true", is_true), ("fake", is_fake)):
        q = np.quantile(s_te[m], [0.05, 0.25, 0.5, 0.75, 0.95])
        print(f"  test {tag} logit quantiles 5/25/50/75/95%: "
              + " ".join(f"{v:+.2f}" for v in q))

    log("done")


if __name__ == "__main__":
    main()
