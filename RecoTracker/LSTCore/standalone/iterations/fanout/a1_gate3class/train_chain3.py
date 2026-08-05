#!/usr/bin/env python3
"""ANGLE-1 3-CLASS chain-gate training (fake / prompt-true / displaced-true).

Mirrors train_chain.py discipline exactly (fixed seeds, event-level split, frozen
test-60, conditioning-before-standardization recorded in the norm json) with three
changes:

  1. THREE OUTPUTS, softmax cross-entropy -- the LST t3dnn/t4dnn shape. Class 0 =
     fake (chain label != 1), class 1 = prompt-true (label == 1 and simVxy < 1 cm),
     class 2 = displaced-true (label == 1 and simVxy >= 1 cm).
  2. SEVENTEEN INPUTS: the 16 frozen ChainFeatures columns PLUS the per-chain
     transverse DCA of the full-fit circle to the origin (k8ChainDcaXY, dumped as the
     "dca" branch by the ANGLE-1 ChainDumpWriter extension). The DCA is exactly the
     quantity the -G 3/4/5 score split keys on, so the network can learn the branch
     structure instead of having it imposed by a hard -X cut.
  3. Tiered displaced weighting inside the CE (x mid for vxy [1,5), x hi for vxy>=5,
     both on top of the global fake/true balance) -- the M6c recipe that converged the
     2-class gate's displaced head.

Decision margins used everywhere downstream (and by -G 6 in the prototype):
    mP = logit_prompt - logit_fake        (IP-compatible branch discriminator)
    mD = logit_displaced - logit_fake     (exempt / large-DCA branch discriminator)
    mX = max(logit_prompt, logit_displaced) - logit_fake   (T4-class discriminator)
Softmax is monotone in these margins, so thresholds on them are calibrated log-odds
ratios; no sigmoid is ever applied.

Outputs:
  chain3_mlp_v1.pt    - torch state_dict + metadata
  chain3_norm_v1.json - per-feature mean/std + conditioning + weighting spec
"""

import argparse
import copy
import json
import os
import sys
import time

import numpy as np

T0 = time.time()

N_CF = 16          # frozen ChainFeatures contract
N_IN = N_CF + 1    # + dca


def log(msg):
    print(f"[{time.time() - T0:8.1f}s] {msg}", flush=True)


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    d = os.path.dirname(os.path.abspath(__file__))
    p.add_argument("--input", nargs="+", default=[f"{d}/chains17_300evt.root"],
                   help="chain dump file(s) WITH the dca branch. Multiple files engage "
                        "the M8 combination rule (file[0] primary, its frozen test split "
                        "preserved; overlap events dropped from extras).")
    p.add_argument("--pool-train-frac", type=float, default=0.75)
    p.add_argument("--out-model", default=f"{d}/chain3_mlp_v1.pt")
    p.add_argument("--out-norm", default=f"{d}/chain3_norm_v1.json")
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--epochs", type=int, default=90)
    p.add_argument("--patience", type=int, default=10)
    p.add_argument("--hidden", type=int, default=32)
    p.add_argument("--displaced-weight-mid", type=float, default=8.0,
                   help="extra multiplier for TRUE chains with 1 <= simVxy < 5 cm")
    p.add_argument("--displaced-weight-hi", type=float, default=16.0,
                   help="extra multiplier for TRUE chains with simVxy >= 5 cm")
    p.add_argument("--batch-size", type=int, default=16384)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--train-frac", type=float, default=0.6)
    p.add_argument("--val-frac", type=float, default=0.2)
    p.add_argument("--state-file", default="")
    p.add_argument("--wall-limit-sec", type=float, default=0.0)
    return p.parse_args()


META_BRANCHES = ["evt", "label", "simVxy", "simPt", "nLayers", "dca"]


def load_dump(path):
    import uproot

    f = uproot.open(path)
    spec = f["feature_spec"].member("fTitle")
    assert spec.startswith("cf:"), f"unexpected feature_spec '{spec[:20]}...'"
    cf_names = spec[3:].split(",")
    assert len(cf_names) == N_CF, f"expected {N_CF} cf features, got {len(cf_names)}"
    feat_branches = [f"cf_{i:02d}" for i in range(N_CF)]
    names = [f"cf_{n}" for n in cf_names] + ["cf_dcaXY"]

    tree = f["chains"]
    arr = tree.arrays(META_BRANCHES + feat_branches, library="np")
    meta = {k: arr[k] for k in META_BRANCHES}
    n = len(meta["label"])
    X = np.empty((n, N_IN), dtype=np.float32)
    for j, b in enumerate(feat_branches):
        X[:, j] = arr[b]
        del arr[b]
    X[:, N_CF] = meta["dca"]
    return meta, X, names


# Conditioning applied IN ORDER, BEFORE standardization. C++ preprocess() applies
# log10_1p FIRST and clip SECOND, so any (log10_1p, clip) pair on the same column must
# be listed in that order here too.
CONDITIONING_SPEC = [
    {"feature": "cf_fullFitChi2PerHit", "op": "log10_1p"},
    {"feature": "cf_rzLineChi2PerHit", "op": "log10_1p"},
    {"feature": "cf_maxJunctionDegProduct", "op": "log10_1p"},
    {"feature": "cf_fitKappa", "op": "clip", "lo": -1.0, "hi": 1.0},
    {"feature": "cf_dKappaFitVsMedianT3", "op": "clip", "lo": -2.0, "hi": 2.0},
    # dca: k8ChainDcaXY spans 0 -> 1e9 (degenerate sentinel). log-compress, then clip at
    # log10(1+30) = 1.491 (30 cm, the end of the plotted displaced range).
    {"feature": "cf_dcaXY", "op": "log10_1p"},
    {"feature": "cf_dcaXY", "op": "clip", "lo": 0.0, "hi": 1.4913617},
]


def apply_conditioning(X, names, spec):
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
    tr = np.isin(key, tr_keys)
    va = np.isin(key, va_keys)
    te = np.isin(key, te_keys)
    log(f"event split: {n} distinct evt keys -> {n_tr}/{n_va}/{n - n_tr - n_va} events -> "
        f"{tr.sum()}/{va.sum()}/{te.sum()} chains (train/val/test)")
    return tr, va, te


def combined_event_split(meta, src, args, rng):
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
    tr = np.isin(key, tr_keys)
    va = np.isin(key, va_keys)
    log(f"COMBINED event split: frozen test = {len(te_keys)} primary events; pool "
        f"{len(pool_keys)} -> train {len(tr_keys)} / val {len(va_keys)} -> "
        f"{tr.sum()}/{va.sum()}/{te.sum()} chains")
    return tr, va, te


def build_model(n_in, n_hid):
    import torch.nn as nn
    return nn.Sequential(nn.Linear(n_in, n_hid), nn.ReLU(),
                         nn.Linear(n_hid, n_hid), nn.ReLU(),
                         nn.Linear(n_hid, 3))


def batched_logits(model, X_t, device, bs=1 << 19):
    import torch
    model.eval()
    out = np.empty((len(X_t), 3), dtype=np.float32)
    with torch.no_grad():
        for i in range(0, len(X_t), bs):
            z = model(X_t[i:i + bs].to(device)).float().cpu()
            out[i:i + bs] = np.asarray(z.tolist(), dtype=np.float32)
    return out


def auc_of(s_pos, s_neg):
    from sklearn.metrics import roc_auc_score
    if len(s_pos) == 0 or len(s_neg) == 0:
        return None
    y = np.concatenate([np.ones(len(s_pos)), np.zeros(len(s_neg))])
    return float(roc_auc_score(y, np.concatenate([s_pos, s_neg])))


def eval_block(tag, s_pos, s_neg):
    a = auc_of(s_pos, s_neg)
    txt = "n/a" if a is None else f"{a:.5f}"
    print(f"  {tag:>34} n_pos={len(s_pos):>8d} n_neg={len(s_neg):>8d} AUC={txt}")
    return {"auc": a, "n_pos": int(len(s_pos)), "n_neg": int(len(s_neg))}


def main():
    args = parse_args()
    import torch

    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.cuda.manual_seed_all(args.seed)
    rng = np.random.default_rng(args.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log(f"seed={args.seed} device={device}")

    metas, x_parts, src_parts = [], [], []
    names = prim_evts = None
    for si, path in enumerate(args.input):
        meta_i, X_i, names_i = load_dump(path)
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
        meta = {k: np.concatenate([m[k] for m in metas]) for k in META_BRANCHES}
        X = np.concatenate(x_parts)
        src = np.concatenate(src_parts)
    del metas, x_parts, src_parts
    n_all = len(X)
    log(f"loaded {n_all} chains x {X.shape[1]} features")

    n_bad = int((~np.isfinite(X)).sum())
    if n_bad:
        log(f"WARNING: {n_bad} non-finite feature values -> 0")
        np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)

    dca_raw = meta["dca"].copy()
    log(f"dca quantiles 1/5/25/50/75/95/99%: "
        + " ".join(f"{v:.3f}" for v in np.quantile(dca_raw, [.01, .05, .25, .5, .75, .95, .99])))

    conditioning = apply_conditioning(X, names, CONDITIONING_SPEC)

    if src is None:
        tr, va, te = event_split(meta, args.train_frac, args.val_frac, rng)
    else:
        tr, va, te = combined_event_split(meta, src, args, rng)

    mu = X[tr].mean(axis=0, dtype=np.float64).astype(np.float32)
    sd = X[tr].std(axis=0, dtype=np.float64).astype(np.float32)
    sd[sd < 1e-8] = 1.0
    Xs = (X - mu) / sd

    # ---- 3-class labels ----
    is_true = meta["label"] == 1
    vxy = meta["simVxy"]
    y3 = np.zeros(n_all, dtype=np.int64)
    y3[is_true & (vxy < 1.0)] = 1
    y3[is_true & (vxy >= 1.0)] = 2
    n0, n1, n2 = [int((y3[tr] == k).sum()) for k in (0, 1, 2)]
    log(f"train class counts: fake={n0} prompt={n1} displaced={n2}")

    # ---- per-sample weights ----
    # Global fake/true balance (same construction as the 2-class pos_weight) times the
    # tiered displaced multipliers.
    pos_weight = n0 / max(n1 + n2, 1)
    w_all = np.ones(n_all, dtype=np.float32)
    w_all[is_true] = pos_weight
    mid = is_true & (vxy >= 1.0) & (vxy < 5.0)
    hi = is_true & (vxy >= 5.0)
    w_all[mid] = pos_weight * args.displaced_weight_mid
    w_all[hi] = pos_weight * args.displaced_weight_hi
    weight_spec = {"rule": "w=1 fakes; w=pos_weight prompt-true; w=pos_weight*mid for true "
                           "vxy in [1,5); w=pos_weight*hi for true vxy>=5",
                   "pos_weight": float(pos_weight),
                   "displaced_weight_mid": args.displaced_weight_mid,
                   "displaced_weight_hi": args.displaced_weight_hi,
                   "n_mid_train": int((mid & tr).sum()), "n_hi_train": int((hi & tr).sum())}
    log(f"weights: pos_weight={pos_weight:.4f}; mid x{args.displaced_weight_mid:g} on "
        f"{int((mid & tr).sum())} train rows, hi x{args.displaced_weight_hi:g} on "
        f"{int((hi & tr).sum())} train rows")

    Xtr = torch.tensor(np.ascontiguousarray(Xs[tr]))
    ytr = torch.tensor(np.ascontiguousarray(y3[tr]))
    wtr = torch.tensor(np.ascontiguousarray(w_all[tr]))
    Xva = torch.tensor(np.ascontiguousarray(Xs[va]))
    y3va = y3[va]
    va_fake = y3va == 0
    va_prompt = y3va == 1
    va_disp = y3va == 2
    if device.type == "cuda":
        Xtr, ytr, wtr = Xtr.to(device), ytr.to(device), wtr.to(device)

    model = build_model(X.shape[1], args.hidden).to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    crit = torch.nn.CrossEntropyLoss(reduction="none")

    gen = torch.Generator(device="cpu").manual_seed(args.seed)
    best_sel, best_state, best_epoch, bad = -1.0, None, -1, 0
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
            loss = (crit(model(Xtr[idx]), ytr[idx]) * wtr[idx]).mean()
            loss.backward()
            opt.step()
            tot_loss += float(loss.detach()) * len(idx)
        z = batched_logits(model, Xva, device)
        mP = z[:, 1] - z[:, 0]
        mD = z[:, 2] - z[:, 0]
        auc_p = auc_of(mP[va_prompt], mP[va_fake])
        auc_d = auc_of(mD[va_disp], mD[va_fake])
        sel = min(auc_p, auc_d)
        epoch_meta = {"val_auc_prompt": auc_p, "val_auc_disp": auc_d}
        log(f"epoch {epoch:3d} train_loss={tot_loss / n_tr:.5f} "
            f"promptAUC={auc_p:.5f} dispAUC={auc_d:.5f} sel=min={sel:.5f}")
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
        if (args.wall_limit_sec > 0 and time.time() - T0 > args.wall_limit_sec
                and epoch < args.epochs):
            log(f"wall limit reached after epoch {epoch}; exit(3) to resume")
            sys.exit(3)

    log(f"best checkpoint: epoch {best_epoch} sel={best_sel:.5f} meta={best_meta}")
    model.load_state_dict(best_state)
    model.to(device)

    torch.save({"state_dict": best_state, "arch": [X.shape[1], args.hidden, args.hidden, 3],
                "feature_names": names, "seed": args.seed, "conditioning": conditioning,
                "best_epoch": best_epoch, "best_sel": float(best_sel),
                "best_val_meta": best_meta}, args.out_model)
    with open(args.out_norm, "w") as fh:
        json.dump({"feature_names": names, "conditioning": conditioning,
                   "mean": mu.tolist(), "std": sd.tolist(), "seed": args.seed,
                   "class_spec": {"0": "fake", "1": "prompt-true (simVxy<1)",
                                  "2": "displaced-true (simVxy>=1)"},
                   "displaced_weighting": weight_spec,
                   "train_args": {"epochs": args.epochs, "patience": args.patience,
                                  "batch_size": args.batch_size, "lr": args.lr,
                                  "hidden": args.hidden, "inputs": args.input,
                                  "pool_train_frac": args.pool_train_frac}}, fh, indent=1)
    log(f"saved {args.out_model} and {args.out_norm}")

    # ---------------- TEST evaluation ----------------
    Xte = torch.tensor(np.ascontiguousarray(Xs[te]))
    z = batched_logits(model, Xte, device)
    mP = z[:, 1] - z[:, 0]
    mD = z[:, 2] - z[:, 0]
    mX = np.maximum(z[:, 1], z[:, 2]) - z[:, 0]
    y3te = y3[te]
    nl = meta["nLayers"][te]
    dca_te = dca_raw[te]
    f_m = y3te == 0
    p_m = y3te == 1
    d_m = y3te == 2

    print("\n=== 3-class chain gate on TEST events ===")
    res = {}
    res["prompt-vs-fake (mP)"] = eval_block("prompt-vs-fake (mP)", mP[p_m], mP[f_m])
    res["displaced-vs-fake (mD)"] = eval_block("displaced-vs-fake (mD)", mD[d_m], mD[f_m])
    res["alltrue-vs-fake (mX)"] = eval_block("alltrue-vs-fake (mX)", mX[p_m | d_m], mX[f_m])
    res["disp>=5-vs-fake (mD)"] = eval_block("disp vxy>=5-vs-fake (mD)",
                                             mD[d_m & (meta["simVxy"][te] >= 5)], mD[f_m])
    print("  --- per nLayers ---")
    for tag, m in (("nL=4", nl == 4), ("nL=5", nl == 5), ("nL>=6", nl >= 6)):
        res[f"{tag} prompt"] = eval_block(f"{tag} prompt-vs-fake (mP)", mP[p_m & m], mP[f_m & m])
        res[f"{tag} disp"] = eval_block(f"{tag} displaced-vs-fake (mD)", mD[d_m & m], mD[f_m & m])
        res[f"{tag} all"] = eval_block(f"{tag} alltrue-vs-fake (mX)", mX[(p_m | d_m) & m], mX[f_m & m])
    print("  --- per -G 6 BRANCH (dca split at 0.5 cm, nLayers>=5) ---")
    ip = (dca_te < 0.5) & (nl >= 5)
    ex = (dca_te >= 0.5) & (nl >= 5)
    res["IP 5+ prompt"] = eval_block("IP(dca<0.5) 5+ prompt (mP)", mP[p_m & ip], mP[f_m & ip])
    res["IP 5+ all"] = eval_block("IP(dca<0.5) 5+ alltrue (mX)", mX[(p_m | d_m) & ip], mX[f_m & ip])
    res["exempt 5+ disp"] = eval_block("exempt(dca>=0.5) 5+ disp (mD)", mD[d_m & ex], mD[f_m & ex])
    res["exempt 5+ all"] = eval_block("exempt(dca>=0.5) 5+ alltrue (mX)",
                                      mX[(p_m | d_m) & ex], mX[f_m & ex])
    res["exempt 5+ prompt"] = eval_block("exempt(dca>=0.5) 5+ prompt (mP)", mP[p_m & ex], mP[f_m & ex])

    print("\n  --- margin quantiles (threshold picking) ---")
    for tag, m in (("fake", f_m), ("prompt", p_m), ("displaced", d_m)):
        for nm, s in (("mP", mP), ("mD", mD), ("mX", mX)):
            q = np.quantile(s[m], [0.05, 0.25, 0.5, 0.75, 0.95])
            print(f"  {tag:>10} {nm} 5/25/50/75/95%: " + " ".join(f"{v:+7.2f}" for v in q))
    print("\n  --- exempt(dca>=0.5) 5+ branch: mD quantiles (the M9 residual-fake home) ---")
    for tag, m in (("fake", f_m & ex), ("displaced", d_m & ex), ("prompt", p_m & ex)):
        if m.sum() == 0:
            continue
        q = np.quantile(mD[m], [0.05, 0.25, 0.5, 0.75, 0.95])
        print(f"  {tag:>10} n={int(m.sum()):>7d} mD 5/25/50/75/95%: "
              + " ".join(f"{v:+7.2f}" for v in q))
    print("\n  --- T4-class (nL<=4): mX quantiles ---")
    t4 = nl <= 4
    for tag, m in (("fake", f_m & t4), ("prompt", p_m & t4), ("displaced", d_m & t4)):
        q = np.quantile(mX[m], [0.05, 0.25, 0.5, 0.75, 0.95])
        print(f"  {tag:>10} n={int(m.sum()):>7d} mX 5/25/50/75/95%: "
              + " ".join(f"{v:+7.2f}" for v in q))

    with open(os.path.splitext(args.out_model)[0] + "_testauc.json", "w") as fh:
        json.dump(res, fh, indent=1)
    log("done")


if __name__ == "__main__":
    main()
