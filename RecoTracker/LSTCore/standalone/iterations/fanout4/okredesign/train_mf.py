#!/usr/bin/env python3
"""OKR matchFrac REGRESSOR: predict a chain's raw harness match fraction directly.

Motivation. The K9 claim-order key currently penalises a chain by a CLASSIFIER logit
(fake vs true). But the thing the greedy claim actually trades is HITS: when chain A
walks first and takes a slot, chain B loses it. The natural ordering quantity is
therefore "how much of a real sim track does this chain actually cover", which the
chaindump stores per chain as `matchFrac` (best-sim harness hit fraction, continuous in
[0,1]) -- a strictly richer target than the thresholded `label`.

Discipline is train_chain3.py's, unchanged: fixed seeds, EVENT-level split with a frozen
test set, conditioning applied before standardization and recorded in the norm json,
resumable checkpoints. Differences:
  * ONE output, BCE-with-logits against the SOFT target matchFrac in [0,1] (a proper
    scoring rule for a fraction; MSE is available via --loss mse),
  * no class weighting by default; --displaced-weight-{mid,hi} still up-weight true
    displaced rows if asked,
  * model selection on VALIDATION SPEARMAN RANK correlation between prediction and
    matchFrac -- the claim order only ever uses the RANKING, never the value.

Outputs: mf_mlp_v1.pt (state_dict + meta) and mf_norm_v1.json (mean/std/conditioning).
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
    d = os.path.dirname(os.path.abspath(__file__))
    p.add_argument("--drop-features", nargs="*", default=[],
                   help="ChainFeatures column NAMES to exclude (default: keep all 25)")
    p.add_argument("--input", nargs="+", default=[f"{d}/chains_m17_300evt.root"],
                   help="chain dump file(s). Multiple files engage the M8 combination "
                        "rule (file[0] primary, its frozen 60-event test split preserved; "
                        "events overlapping the primary are dropped from the extras)")
    p.add_argument("--pool-train-frac", type=float, default=0.75)
    p.add_argument("--out-model", default=f"{d}/mf_mlp_v1.pt")
    p.add_argument("--out-norm", default=f"{d}/mf_norm_v1.json")
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--epochs", type=int, default=90)
    p.add_argument("--patience", type=int, default=10)
    p.add_argument("--hidden", type=int, default=32)
    p.add_argument("--batch-size", type=int, default=16384)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--train-frac", type=float, default=0.6)
    p.add_argument("--val-frac", type=float, default=0.2)
    p.add_argument("--loss", choices=["bce", "mse"], default="bce")
    p.add_argument("--displaced-weight-mid", type=float, default=1.0)
    p.add_argument("--displaced-weight-hi", type=float, default=1.0)
    p.add_argument("--state-file", default="")
    p.add_argument("--wall-limit-sec", type=float, default=0.0)
    p.add_argument("--stage-mode", choices=["all", "ctl_noatt", "hybrid_f2"], default="ctl_noatt",
                   help="restrict rows to the chains that reach the K9 claim in the given "
                        "replacement profile (ctl_noatt = the -RT5 1 anchor)")
    return p.parse_args()


META_BRANCHES = ["evt", "label", "matchFrac", "simVxy", "simPt", "nLayers", "dcaXY"]
OPT_BRANCHES = ["score", "pixPT5", "pixPT3"]

# Identical to train_chain3.CONDITIONING_SPEC (order matters: log10_1p then clip).
CONDITIONING_SPEC = [
    {"feature": "cf_fullFitChi2PerHit", "op": "log10_1p"},
    {"feature": "cf_rzLineChi2PerHit", "op": "log10_1p"},
    {"feature": "cf_maxJunctionDegProduct", "op": "log10_1p"},
    {"feature": "cf_fitKappa", "op": "clip", "lo": -1.0, "hi": 1.0},
    {"feature": "cf_dKappaFitVsMedianT3", "op": "clip", "lo": -2.0, "hi": 2.0},
    {"feature": "cf_maxXyResid", "op": "log10_1p"},
    {"feature": "cf_maxRzResid", "op": "log10_1p"},
    {"feature": "cf_maxBridgeChi2", "op": "log10_1p"},
    {"feature": "cf_dcaXY", "op": "log10_1p"},
    {"feature": "cf_dcaXY", "op": "clip", "lo": 0.0, "hi": 1.4913617},
]


def load_dump(path, drop):
    import uproot

    f = uproot.open(path)
    spec = f["feature_spec"].member("fTitle")
    assert spec.startswith("cf:"), f"unexpected feature_spec '{spec[:20]}...'"
    cf_names = spec[3:].split(",")
    keep = [i for i, nm in enumerate(cf_names) if nm not in drop]
    missing = [d for d in drop if d not in cf_names]
    assert not missing, f"--drop-features names not in the contract: {missing}"
    feat_branches = [f"cf_{i:02d}" for i in keep]
    names = [f"cf_{cf_names[i]}" for i in keep] + ["cf_dcaXY"]

    tree = f["chains"]
    have = set(k.split(";")[0] for k in tree.keys())
    assert "matchFrac" in have, f"{path} has no matchFrac branch"
    opt = [b for b in OPT_BRANCHES if b in have]
    arr = tree.arrays(META_BRANCHES + opt + feat_branches, library="np")
    meta = {k: arr[k] for k in META_BRANCHES}
    n = len(meta["label"])
    for b in OPT_BRANCHES:
        meta[b] = arr[b] if b in opt else np.zeros(n, dtype=np.int32)
    X = np.empty((n, len(keep) + 1), dtype=np.float32)
    for j, b in enumerate(feat_branches):
        X[:, j] = arr[b]
        del arr[b]
    X[:, len(keep)] = meta["dcaXY"]
    return meta, X, names


def apply_conditioning(X, names, spec):
    spec = [c for c in spec if c["feature"] in names]
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


def event_split(meta, train_frac, val_frac, rng):
    key = meta["evt"].astype(np.uint64)
    uniq = np.unique(key)
    rng.shuffle(uniq)
    n = len(uniq)
    n_tr = int(round(train_frac * n))
    n_va = int(round(val_frac * n))
    tr_keys, va_keys = uniq[:n_tr], uniq[n_tr:n_tr + n_va]
    te_keys = uniq[n_tr + n_va:]
    tr, va, te = np.isin(key, tr_keys), np.isin(key, va_keys), np.isin(key, te_keys)
    log(f"event split: {n} distinct evt keys -> {n_tr}/{n_va}/{n - n_tr - n_va} events -> "
        f"{tr.sum()}/{va.sum()}/{te.sum()} chains (train/val/test)")
    return tr, va, te


def combined_event_split(meta, src, args, rng):
    """M8 combination rule: the frozen test set is 20% of the PRIMARY input's events and
    is never allowed to appear in an extra input; everything else pools into train/val."""
    key = meta["evt"].astype(np.uint64)
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
    tr, va = np.isin(key, tr_keys), np.isin(key, va_keys)
    log(f"COMBINED event split: frozen test = {len(te_keys)} primary events; pool "
        f"{len(pool_keys)} -> train {len(tr_keys)} / val {len(va_keys)} -> "
        f"{tr.sum()}/{va.sum()}/{te.sum()} chains")
    return tr, va, te


def build_model(n_in, n_hid):
    import torch.nn as nn
    return nn.Sequential(nn.Linear(n_in, n_hid), nn.ReLU(),
                         nn.Linear(n_hid, n_hid), nn.ReLU(),
                         nn.Linear(n_hid, 1))


def batched_pred(model, X_t, device, bs=1 << 19):
    import torch
    model.eval()
    out = np.empty(len(X_t), dtype=np.float32)
    with torch.no_grad():
        for i in range(0, len(X_t), bs):
            # .tolist() rather than .numpy(): this torch build has no NumPy support.
            z = model(X_t[i:i + bs].to(device)).float().cpu().reshape(-1)
            out[i:i + bs] = np.asarray(z.tolist(), dtype=np.float32)
    return out


def spearman(a, b):
    from scipy.stats import rankdata
    ra, rb = rankdata(a), rankdata(b)
    ra = ra - ra.mean()
    rb = rb - rb.mean()
    den = np.sqrt((ra * ra).sum() * (rb * rb).sum())
    return float((ra * rb).sum() / den) if den > 0 else 0.0


def auc_of(s_pos, s_neg):
    from sklearn.metrics import roc_auc_score
    if len(s_pos) == 0 or len(s_neg) == 0:
        return None
    y = np.concatenate([np.ones(len(s_pos)), np.zeros(len(s_neg))])
    return float(roc_auc_score(y, np.concatenate([s_pos, s_neg])))


def main():
    args = parse_args()
    import torch

    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.cuda.manual_seed_all(args.seed)
    rng = np.random.default_rng(args.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log(f"seed={args.seed} device={device} loss={args.loss}")

    metas, x_parts, src_parts = [], [], []
    names = prim_evts = None
    for si, path in enumerate(args.input):
        meta_i, X_i, names_i = load_dump(path, set(args.drop_features))
        if si == 0:
            names = names_i
            prim_evts = np.unique(meta_i["evt"])
            log(f"input[0] PRIMARY {path}: {len(X_i)} chains over {len(prim_evts)} events")
        else:
            assert names_i == names
            keep = ~np.isin(meta_i["evt"], prim_evts)
            log(f"input[{si}] EXTRA {path}: keep {int(keep.sum())}/{len(X_i)} chains")
            meta_i = {k: v[keep] for k, v in meta_i.items()}
            X_i = X_i[keep]
        metas.append(meta_i)
        x_parts.append(X_i)
        src_parts.append(np.full(len(X_i), si, dtype=np.int8))
    if len(args.input) == 1:
        meta, X, src = metas[0], x_parts[0], None
    else:
        meta = {k: np.concatenate([m[k] for m in metas]) for k in META_BRANCHES + OPT_BRANCHES}
        X = np.concatenate(x_parts)
        src = np.concatenate(src_parts)
    del metas, x_parts, src_parts

    if args.stage_mode != "all":
        drop5 = args.stage_mode == "hybrid_f2"
        keep = meta["pixPT3"] == 0
        if drop5:
            keep &= meta["pixPT5"] == 0
        n_before = len(X)
        assert keep.sum() > 0, "stage filter kept nothing -- dump lacks pixPT3/pixPT5?"
        meta = {k: v[keep] for k, v in meta.items()}
        X = X[keep]
        if src is not None:
            src = src[keep]
        log(f"STAGE FILTER '{args.stage_mode}': {int(keep.sum())}/{n_before} chains kept "
            f"({keep.mean():.4f})")

    n_all = len(X)
    log(f"loaded {n_all} chains x {X.shape[1]} features (dropped: {sorted(args.drop_features)})")
    log("inputs: " + ", ".join(f"{j}:{nm}" for j, nm in enumerate(names)))

    n_bad = int((~np.isfinite(X)).sum())
    if n_bad:
        log(f"WARNING: {n_bad} non-finite feature values -> 0")
        np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)

    y = np.clip(meta["matchFrac"].astype(np.float32), 0.0, 1.0)
    q = np.quantile(y, [0.05, 0.25, 0.5, 0.75, 0.95])
    log("matchFrac 5/25/50/75/95%: " + " ".join(f"{v:.3f}" for v in q)
        + f" | mean={y.mean():.4f} frac(y>=0.75)={float((y >= 0.75).mean()):.4f}")

    conditioning = apply_conditioning(X, names, CONDITIONING_SPEC)
    if src is None:
        tr, va, te = event_split(meta, args.train_frac, args.val_frac, rng)
    else:
        tr, va, te = combined_event_split(meta, src, args, rng)

    mu = X[tr].mean(axis=0, dtype=np.float64).astype(np.float32)
    sd = X[tr].std(axis=0, dtype=np.float64).astype(np.float32)
    sd[sd < 1e-8] = 1.0
    Xs = (X - mu) / sd

    is_true = meta["label"] == 1
    vxy = meta["simVxy"]
    w_all = np.ones(n_all, dtype=np.float32)
    if args.displaced_weight_mid != 1.0 or args.displaced_weight_hi != 1.0:
        w_all[is_true & (vxy >= 1.0) & (vxy < 5.0)] = args.displaced_weight_mid
        w_all[is_true & (vxy >= 5.0)] = args.displaced_weight_hi
    weight_spec = {"rule": "w=1 everywhere unless displaced multipliers set",
                   "displaced_weight_mid": args.displaced_weight_mid,
                   "displaced_weight_hi": args.displaced_weight_hi}

    Xtr = torch.tensor(np.ascontiguousarray(Xs[tr]))
    ytr = torch.tensor(np.ascontiguousarray(y[tr]))
    wtr = torch.tensor(np.ascontiguousarray(w_all[tr]))
    Xva = torch.tensor(np.ascontiguousarray(Xs[va]))
    yva = y[va]
    va_true = is_true[va]
    if device.type == "cuda":
        Xtr, ytr, wtr = Xtr.to(device), ytr.to(device), wtr.to(device)

    model = build_model(X.shape[1], args.hidden).to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    crit = (torch.nn.BCEWithLogitsLoss(reduction="none") if args.loss == "bce"
            else torch.nn.MSELoss(reduction="none"))

    gen = torch.Generator(device="cpu").manual_seed(args.seed)
    best_sel, best_state, best_epoch, bad = -2.0, None, -1, 0
    best_meta = {}
    start_epoch = 1
    if args.state_file and os.path.exists(args.state_file):
        st = torch.load(args.state_file, map_location="cpu", weights_only=False)
        model.load_state_dict(st["model"])
        model.to(device)
        opt.load_state_dict(st["opt"])
        gen.set_state(st["gen"])
        best_sel, best_epoch, bad = st["best_sel"], st["best_epoch"], st["bad"]
        best_state, best_meta = st["best_state"], st["best_meta"]
        start_epoch = st["epoch"] + 1
        log(f"RESUMED from {args.state_file}: next epoch {start_epoch}")

    n_tr = len(Xtr)
    for epoch in range(start_epoch, args.epochs + 1):
        model.train()
        perm = torch.randperm(n_tr, generator=gen)
        tot_loss = 0.0
        for i in range(0, n_tr, args.batch_size):
            idx = perm[i:i + args.batch_size]
            if device.type == "cuda":
                idx = idx.to(device)
            opt.zero_grad()
            z = model(Xtr[idx]).reshape(-1)
            if args.loss == "mse":
                z = torch.sigmoid(z)
            loss = (crit(z, ytr[idx]) * wtr[idx]).mean()
            loss.backward()
            opt.step()
            tot_loss += float(loss.detach()) * len(idx)
        zva = batched_pred(model, Xva, device)
        rho = spearman(zva, yva)
        auc = auc_of(zva[va_true], zva[~va_true])
        mae = float(np.abs(1.0 / (1.0 + np.exp(-zva)) - yva).mean())
        sel = rho
        epoch_meta = {"val_spearman": rho, "val_auc_labelTrue": auc, "val_mae": mae}
        log(f"epoch {epoch:3d} train_loss={tot_loss / n_tr:.5f} spearman={rho:.5f} "
            f"AUC(label)={auc:.5f} MAE={mae:.4f}")
        stop = False
        if sel > best_sel:
            best_sel, best_epoch, bad = sel, epoch, 0
            best_meta = epoch_meta
            best_state = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
        else:
            bad += 1
            if bad >= args.patience:
                log(f"early stop at epoch {epoch} (best {best_sel:.5f} @ {best_epoch})")
                stop = True
        if args.state_file:
            tmp = args.state_file + ".tmp"
            torch.save({"model": {k: v.cpu() for k, v in model.state_dict().items()},
                        "opt": opt.state_dict(), "gen": gen.get_state(),
                        "best_sel": best_sel, "best_epoch": best_epoch, "bad": bad,
                        "best_state": best_state, "best_meta": best_meta,
                        "epoch": epoch}, tmp)
            os.replace(tmp, args.state_file)
        if stop:
            break
        if args.wall_limit_sec > 0 and time.time() - T0 > args.wall_limit_sec and epoch < args.epochs:
            log(f"wall limit reached after epoch {epoch}; exit(3) to resume")
            sys.exit(3)

    log(f"best checkpoint: epoch {best_epoch} sel={best_sel:.5f} meta={best_meta}")
    model.load_state_dict(best_state)
    model.to(device)

    torch.save({"state_dict": best_state, "arch": [X.shape[1], args.hidden, args.hidden, 1],
                "feature_names": names, "seed": args.seed, "conditioning": conditioning,
                "best_epoch": best_epoch, "best_val": float(best_sel),
                "best_val_meta": best_meta}, args.out_model)
    with open(args.out_norm, "w") as fh:
        json.dump({"feature_names": names, "conditioning": conditioning,
                   "mean": mu.tolist(), "std": sd.tolist(), "seed": args.seed,
                   "target": "matchFrac (raw best-sim harness hit fraction), soft [0,1]",
                   "weighting": weight_spec,
                   "train_args": {"epochs": args.epochs, "patience": args.patience,
                                  "batch_size": args.batch_size, "lr": args.lr,
                                  "hidden": args.hidden, "inputs": args.input,
                                  "loss": args.loss, "stage_mode": args.stage_mode}}, fh, indent=1)
    log(f"saved {args.out_model} and {args.out_norm}")

    # ---------------- TEST evaluation ----------------
    Xte = torch.tensor(np.ascontiguousarray(Xs[te]))
    zte = batched_pred(model, Xte, device)
    pte = 1.0 / (1.0 + np.exp(-zte))
    yte = y[te]
    tt = is_true[te]
    nl = meta["nLayers"][te]
    dca_te = meta["dcaXY"][te]
    res = {"spearman_all": spearman(zte, yte),
           "auc_labelTrue": auc_of(zte[tt], zte[~tt]),
           "mae": float(np.abs(pte - yte).mean())}
    print("\n=== matchFrac regressor on TEST events ===")
    print(f"  spearman(pred, matchFrac) = {res['spearman_all']:.5f}")
    print(f"  AUC(label==1 vs 0)        = {res['auc_labelTrue']:.5f}")
    print(f"  MAE(sigmoid(pred), y)     = {res['mae']:.4f}")
    for tag, m in (("nL=4", nl == 4), ("nL=5", nl == 5), ("nL>=6", nl >= 6),
                   ("IP 5+", (dca_te < 0.5) & (nl >= 5)), ("exempt 5+", (dca_te >= 0.5) & (nl >= 5))):
        if m.sum() < 100:
            continue
        r = spearman(zte[m], yte[m])
        a = auc_of(zte[m & tt], zte[m & ~tt])
        res[f"{tag} spearman"] = r
        res[f"{tag} auc"] = a
        print(f"  {tag:>10} n={int(m.sum()):>7d} spearman={r:.5f} "
              + ("AUC=n/a" if a is None else f"AUC={a:.5f}"))
    print("\n  --- predicted-fraction quantiles ---")
    for tag, m in (("matchFrac>=0.9", yte >= 0.9), ("matchFrac in [0.5,0.9)", (yte >= 0.5) & (yte < 0.9)),
                   ("matchFrac<0.5", yte < 0.5)):
        if m.sum() == 0:
            continue
        qq = np.quantile(pte[m], [0.05, 0.25, 0.5, 0.75, 0.95])
        print(f"  {tag:>24} n={int(m.sum()):>7d} pred 5/25/50/75/95%: "
              + " ".join(f"{v:.3f}" for v in qq))
    with open(os.path.splitext(args.out_model)[0] + "_test.json", "w") as fh:
        json.dump(res, fh, indent=1)
    log("done")


if __name__ == "__main__":
    main()
