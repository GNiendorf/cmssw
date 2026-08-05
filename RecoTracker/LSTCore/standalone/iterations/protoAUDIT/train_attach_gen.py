#!/usr/bin/env python3
"""M16 GENERAL pLS->OT attach-head training (plan section 11: ONE attach machine over
BOTH outer-tracker target kinds).

Reads the M16 general pair dump (TTree "pairs" over pairs_gen_c*.root; feature order =
the frozen 19-slot PixelAttach.h kAttachFeat contract with af_18 = targetType), trains a
small MLP pair head over BOTH target universes and evaluates it on the FROZEN TEST-60
events.

Discipline mirrored from train_attach.py / train_chain3.py:
  - fixed seeds for numpy AND torch,
  - EVENT-level split (row-level splits forbidden, plan 5c). The seed-42 shuffle of the
    300 RelVal event keys with 0.6/0.2 reproduces the FROZEN test-60 list
    (prototype/m12_test60_evts.json) used by every previous head -- asserted, not assumed,
    so this head's test events are the same held-out 60 as the chain gate's.
  - feature conditioning BEFORE standardization, recorded in the norm json for the C++
    port (export_attach_weights.py bakes it into attach_mlp_weights.h),
  - standardization mean/std fit on TRAIN rows only,
  - class imbalance via BCEWithLogits pos_weight = W_fake/W_true (train, weighted),
  - 100 epochs, early stopping patience 10 on val AUC,
  - resumable chunked runs (--state/--cache/--time-budget-s) with a bitwise-identical
    schedule to an uninterrupted run.

M16 ADDITIONS
-------------
1. SAMPLING WEIGHTS (`wgt` branch). The dump downsamples FAKE pairs of the bare-T3
   universe 1-in-32 (wgt = 32) because that universe is 272x the chain universe; true
   pairs are never downsampled (wgt = 1). Every loss term and every reported AUC is
   weighted by `wgt`, so the head is trained and judged on the FULL undownsampled
   deployed population. This matters beyond bookkeeping: the general attach resolves
   cross-type pLS contention BY LOGIT, so the logit must be a calibrated posterior on
   the population the pipeline actually enumerates -- which is the wgt-weighted one.
2. NO type re-balancing. The chain universe is 0.37% of the deployed fake mass; forcing
   it to 50% would decalibrate exactly the cross-type ordering the design decision needs.
   The chain-pair AUC is instead MEASURED against the chain-only v1 head on the same
   rows, and a per-type-head variant (shared trunk, --per-type) is the documented
   fallback if the single head loses more than 0.002 AUC on chain pairs.
3. DISPLACED: reported per target type per vxy stratum, NOT force-weighted (M7 rule:
   pLS-side displaced true pairs are physically rare because displaced tracks mostly have
   no pLS; weighting O(1e3)-row strata would only inject noise).

Conditioning: unchanged from the M7 spec (the same four columns are the heavy-tailed
ones in the general dump -- verified on the 300-evt dump: ptErrRel tail to 30,
circleCenterDist tail to 9.9e8 cm, log10PtIn/log10CircleRadius junk-seed tails). Slots
0-17 therefore use the IDENTICAL op vocabulary as the v1 head, only refit mean/std.

Outputs:
  attach_mlp_g1.pt    - torch state_dict + metadata (arch, feature names, best epoch/AUC)
  attach_norm_g1.json - per-feature mean/std + conditioning spec
"""

import argparse
import copy
import glob
import json
import os
import time

import numpy as np

T0 = time.time()

N_ATTACH_FEAT = 19
PROTO_DIR = os.path.dirname(os.path.abspath(__file__))
FROZEN_TEST60 = ("/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/"
                 "standalone/prototype/m12_test60_evts.json")


def log(msg):
    print(f"[{time.time() - T0:8.1f}s] {msg}", flush=True)


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", default=f"{PROTO_DIR}/pairs_gen_c*.root",
                   help="glob over the M16 general pair-dump chunks")
    p.add_argument("--out-model", default=f"{PROTO_DIR}/attach_mlp_g1.pt")
    p.add_argument("--out-norm", default=f"{PROTO_DIR}/attach_norm_g1.json")
    p.add_argument("--out-testauc", default=None, help="json dump of the TEST report")
    p.add_argument("--seed", type=int, default=42, help="seed for numpy AND torch")
    p.add_argument("--epochs", type=int, default=100)
    p.add_argument("--patience", type=int, default=10, help="early stopping on val AUC")
    p.add_argument("--hidden", type=int, default=24)
    p.add_argument("--per-type", action="store_true",
                   help="shared-trunk 2-output variant: output 0 scores CHAIN targets, "
                        "output 1 scores BARE-T3 targets; each row trains only its own "
                        "output. Fallback if the single head loses chain-pair AUC.")
    p.add_argument("--no-feature-clip", action="store_true")
    p.add_argument("--batch-size", type=int, default=16384)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--train-frac", type=float, default=0.6)
    p.add_argument("--val-frac", type=float, default=0.2)
    p.add_argument("--cache", default=None)
    p.add_argument("--state", default=None)
    p.add_argument("--time-budget-s", type=float, default=None)
    p.add_argument("--eval-only", action="store_true",
                   help="skip training, load --out-model and run the TEST report")
    return p.parse_args()


# ---------------------------------------------------------------- data loading

META_BRANCHES = ["evt", "label", "ttype", "wgt", "simVxy", "simPt", "chainNLayers"]


def load_dump(pattern):
    """Load the chunked general pair dump into preallocated arrays."""
    import uproot

    files = sorted(glob.glob(pattern))
    assert files, f"no dump chunks matched {pattern}"
    trees, ns, names = [], [], None
    for path in files:
        f = uproot.open(path)
        spec = f["feature_spec"].member("fTitle")
        assert spec.startswith("af:"), f"unexpected feature_spec in {path}"
        af_names = spec[3:].split(",")
        assert len(af_names) == N_ATTACH_FEAT, \
            f"{path}: expected {N_ATTACH_FEAT} features, got {len(af_names)}"
        nm = [f"af_{n}" for n in af_names]
        assert names is None or nm == names, f"{path}: feature_spec disagrees"
        names = nm
        t = f["pairs"]
        trees.append(t)
        ns.append(t.num_entries)
    n_tot = int(sum(ns))
    log(f"{len(files)} chunks, {n_tot} pairs total")

    feat_branches = [f"af_{i:02d}" for i in range(N_ATTACH_FEAT)]
    X = np.empty((n_tot, N_ATTACH_FEAT), dtype=np.float32)
    meta = {"evt": np.empty(n_tot, dtype=np.uint64),
            "label": np.empty(n_tot, dtype=np.int8),
            "ttype": np.empty(n_tot, dtype=np.int8),
            "wgt": np.empty(n_tot, dtype=np.float32),
            "simVxy": np.empty(n_tot, dtype=np.float32),
            "simPt": np.empty(n_tot, dtype=np.float32),
            "chainNLayers": np.empty(n_tot, dtype=np.int8)}
    off = 0
    for path, t, n in zip(files, trees, ns):
        a = t.arrays(META_BRANCHES, library="np")
        for k in META_BRANCHES:
            meta[k][off:off + n] = a[k]
        del a
        a = t.arrays(feat_branches, library="np")
        for j, b in enumerate(feat_branches):
            X[off:off + n, j] = a[b]
            del a[b]
        del a
        off += n
        log(f"  loaded {os.path.basename(path)} ({n} rows, {off}/{n_tot})")
    assert off == n_tot
    return meta, X, names


def data_quality_report(X, names, ttype):
    log("--- data quality (all rows, per target type) ---")
    degenerate = []
    print(f"{'feature':>26} {'tt':>3} {'min':>12} {'max':>12} {'mean':>12} {'std':>12}")
    for j, name in enumerate(names):
        col = X[:, j]
        sd_all = float(col.std())
        for tt in (0, 1):
            v = col[ttype == tt]
            print(f"{name:>26} {tt:>3} {float(v.min()):>12.4g} {float(v.max()):>12.4g} "
                  f"{float(v.mean()):>12.4g} {float(v.std()):>12.4g}")
        if sd_all < 1e-8:
            degenerate.append(name)
            print(f"{'':>26} {'':>3}   <-- DEGENERATE (zero variance over all rows)")
    n_bad = int((~np.isfinite(X)).sum())
    if n_bad:
        log(f"WARNING: {n_bad} non-finite feature values -> replaced with 0")
        np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
    if degenerate:
        log(f"degenerate (zero-variance) columns: {degenerate}")
    return degenerate


# ------------------------------------------------------- feature conditioning

CONDITIONING_SPEC = [
    {"feature": "af_ptErrRel", "op": "log10_1p"},
    {"feature": "af_circleCenterDist", "op": "log10_1p"},
    {"feature": "af_log10PtIn", "op": "clip", "lo": -1.0, "hi": 4.0},
    {"feature": "af_log10CircleRadius", "op": "clip", "lo": 1.0, "hi": 5.0},
    # M16 ADDITION (the v2-edge "crippled column" lesson, reproduced on the general dump):
    # the bare-T3 target universe contains DEGENERATE (collinear) T3 fits whose signed
    # curvature saturates at the 1e9 guard value -- 37 rows in 79,344,409 (4.7e-7, all
    # FAKE), against a physical range of |kappa| <= 0.376 cm^-1. Those 37 rows alone drive
    # std(af_fitKappaSigned) to 7.1e5 and std(af_dKappa) to 7.1e5, which standardizes the
    # entire physical range of BOTH curvature features into |z| < 1e-3 -- i.e. it deletes
    # the curvature-agreement block, believed (M2/M6a maintainer note) to be THE most
    # informative discriminator. Clipping at +-1 cm^-1 is ~3x looser than any physical
    # value in the dump, so it is a pure sentinel kill, not a distortion.
    {"feature": "af_fitKappaSigned", "op": "clip", "lo": -1.0, "hi": 1.0},
    {"feature": "af_dKappa", "op": "clip", "lo": -1.0, "hi": 1.0},
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


# ---------------------------------------------------------------- event split

def event_split(meta, train_frac, val_frac, rng):
    """Seed-42 shuffle of the distinct evt keys, 60/20/20 -- the exact procedure of
    train_attach.py / train_chain3.py. The resulting TEST keys are asserted to be the
    frozen test-60 list every previous head was judged on."""
    key = meta["evt"].astype(np.uint64)
    uniq = np.unique(key)
    rng.shuffle(uniq)
    n = len(uniq)
    n_tr = int(round(train_frac * n))
    n_va = int(round(val_frac * n))
    tr_keys, va_keys, te_keys = uniq[:n_tr], uniq[n_tr:n_tr + n_va], uniq[n_tr + n_va:]
    if os.path.exists(FROZEN_TEST60):
        with open(FROZEN_TEST60) as fh:
            frozen = sorted(int(v) for v in json.load(fh))
        got = sorted(int(v) for v in te_keys)
        assert got == frozen, ("TEST split is NOT the frozen test-60 "
                               f"({len(got)} keys, {len(set(got) ^ set(frozen))} differ)")
        log(f"TEST split == frozen test-60 (prototype/m12_test60_evts.json) VERIFIED")
    tr = np.isin(key, tr_keys)
    va = np.isin(key, va_keys)
    te = np.isin(key, te_keys)
    log(f"event split: {n} distinct evt keys -> {n_tr}/{n_va}/{n - n_tr - n_va} events -> "
        f"{tr.sum()}/{va.sum()}/{te.sum()} pairs (train/val/test)")
    return tr, va, te


def strata_report(meta, tag):
    """Displaced accounting -- REPORTED, never weighted (M7 rule)."""
    is_true = meta["label"] == 1
    acc = is_true & (meta["simPt"] > -998.0)
    print(f"  --- true-pair strata ({tag}) ---")
    print(f"  {'stratum':>22} {'chain(tt0)':>12} {'bareT3(tt1)':>12}")
    bands = [("accepted vxy [0,1)", 0.0, 1.0), ("accepted vxy [1,5)", 1.0, 5.0),
             ("accepted vxy [5,10)", 5.0, 10.0), ("accepted vxy [10,30)", 10.0, 30.0),
             ("accepted vxy >=30", 30.0, 1e9)]
    for name, lo, hi in bands:
        m = acc & (meta["simVxy"] >= lo) & (meta["simVxy"] < hi)
        print(f"  {name:>22} {int((m & (meta['ttype'] == 0)).sum()):>12d} "
              f"{int((m & (meta['ttype'] == 1)).sum()):>12d}")
    m = is_true & ~acc
    print(f"  {'pileup-sim only':>22} {int((m & (meta['ttype'] == 0)).sum()):>12d} "
          f"{int((m & (meta['ttype'] == 1)).sum()):>12d}")
    print(f"  {'ALL true':>22} {int((is_true & (meta['ttype'] == 0)).sum()):>12d} "
          f"{int((is_true & (meta['ttype'] == 1)).sum()):>12d}")


def build_cache(args, rng):
    meta, X, names = load_dump(args.input)
    n_all = len(X)
    degenerate = data_quality_report(X, names, meta["ttype"])
    strata_report(meta, "all rows")

    conditioning = []
    if not args.no_feature_clip:
        conditioning = apply_conditioning(X, names, CONDITIONING_SPEC)

    tr, va, te = event_split(meta, args.train_frac, args.val_frac, rng)

    mu = X[tr].mean(axis=0, dtype=np.float64).astype(np.float32)
    sd = X[tr].std(axis=0, dtype=np.float64).astype(np.float32)
    sd[sd < 1e-8] = 1.0
    X -= mu
    X /= sd

    y = (meta["label"] == 1).astype(np.float32)
    cache = {
        "Xtr": X[tr], "ytr": y[tr], "wtr": meta["wgt"][tr], "ttr": meta["ttype"][tr],
        "Xva": X[va], "yva": y[va], "wva": meta["wgt"][va], "tva": meta["ttype"][va],
        "Xte": X[te], "yte": y[te], "wte": meta["wgt"][te], "tte": meta["ttype"][te],
        "vxy_te": meta["simVxy"][te], "pt_te": meta["simPt"][te],
        "nl_te": meta["chainNLayers"][te],
        "mu": mu, "sd": sd,
        "meta_json": np.bytes_(json.dumps({
            "names": names, "conditioning": conditioning, "degenerate": degenerate,
            "n_all": int(n_all), "input": args.input, "seed": args.seed,
            "train_frac": args.train_frac, "val_frac": args.val_frac,
            "feature_clip": not args.no_feature_clip})),
    }
    if args.cache:
        np.savez(args.cache, **cache)
        log(f"wrote cache {args.cache} ({os.path.getsize(args.cache) / 1e9:.2f} GB)")
    return cache


def load_cache(path, args):
    z = np.load(path)
    cache = {k: z[k] for k in z.files}
    mj = json.loads(bytes(cache["meta_json"]).decode())
    assert mj["seed"] == args.seed and mj["input"] == args.input, \
        "cache was built with different seed/input -- delete it to rebuild"
    assert mj["feature_clip"] == (not args.no_feature_clip), \
        "cache was built with different conditioning -- delete it to rebuild"
    log(f"loaded cache {path}: train/val/test = "
        f"{len(cache['Xtr'])}/{len(cache['Xva'])}/{len(cache['Xte'])} pairs")
    return cache


# ---------------------------------------------------------------- model

def build_model(n_in, n_hid, n_out):
    import torch.nn as nn
    return nn.Sequential(nn.Linear(n_in, n_hid), nn.ReLU(),
                         nn.Linear(n_hid, n_hid), nn.ReLU(),
                         nn.Linear(n_hid, n_out))


def batched_scores(model, X_t, device, ttype=None, bs=1 << 20):
    """Returns the per-row logit. For the per-type variant the row's own target-type
    output is selected -- exactly what the C++ port does with feature 18."""
    import torch
    model.eval()
    n = len(X_t)
    out = np.empty(n, dtype=np.float32)
    with torch.no_grad():
        for i in range(0, n, bs):
            z = model(X_t[i:i + bs].to(device)).float().cpu()
            zz = np.asarray(z.tolist(), dtype=np.float32)
            if zz.ndim == 2 and zz.shape[1] > 1:
                sel = ttype[i:i + bs].astype(np.int64)
                zz = zz[np.arange(len(zz)), sel]
            else:
                zz = zz.reshape(-1)
            out[i:i + bs] = zz
    return out


def wauc(s_pos, w_pos, s_neg, w_neg):
    from sklearn.metrics import roc_auc_score
    if len(s_pos) == 0 or len(s_neg) == 0:
        return None
    y = np.concatenate([np.ones(len(s_pos)), np.zeros(len(s_neg))])
    s = np.concatenate([s_pos, s_neg])
    w = np.concatenate([w_pos, w_neg])
    return float(roc_auc_score(y, s, sample_weight=w))


def eval_block(tag, s_pos, w_pos, s_neg, w_neg, res=None):
    a = wauc(s_pos, w_pos, s_neg, w_neg)
    txt = "n/a" if a is None else f"{a:.5f}"
    print(f"  {tag:>32} n_true={len(s_pos):>9d} n_fake={len(s_neg):>9d} "
          f"(eff.fake {float(w_neg.sum()):>13.0f}) AUC={txt}")
    if res is not None:
        res[tag] = {"auc": a, "n_true": int(len(s_pos)), "n_fake": int(len(s_neg)),
                    "eff_fake": float(w_neg.sum()), "eff_true": float(w_pos.sum())}
    return a


# ---------------------------------------------------------------- main

def main():
    args = parse_args()
    import torch

    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.cuda.manual_seed_all(args.seed)
    rng = np.random.default_rng(args.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log(f"seed={args.seed} device={device} per_type={args.per_type}")

    if args.cache and os.path.exists(args.cache):
        cache = load_cache(args.cache, args)
    else:
        cache = build_cache(args, rng)
    mj = json.loads(bytes(cache["meta_json"]).decode())
    names, conditioning, degenerate = mj["names"], mj["conditioning"], mj["degenerate"]

    n_out = 2 if args.per_type else 1
    ytr_np, wtr_np, ttr_np = cache["ytr"], cache["wtr"], cache["ttr"]
    W_pos = float((wtr_np * ytr_np).sum())
    W_neg = float((wtr_np * (1.0 - ytr_np)).sum())
    pos_weight = W_neg / max(W_pos, 1.0)
    log(f"train weighted: {W_pos:.4g} true / {W_neg:.4g} fake -> pos_weight={pos_weight:.4f}")

    Xtr = torch.tensor(np.ascontiguousarray(cache["Xtr"]))
    ytr = torch.tensor(np.ascontiguousarray(ytr_np))
    wtr = torch.tensor(np.ascontiguousarray(wtr_np))
    ttr = torch.tensor(np.ascontiguousarray(ttr_np.astype(np.int64)))
    Xva = torch.tensor(np.ascontiguousarray(cache["Xva"]))
    yva_np, wva_np, tva_np = cache["yva"], cache["wva"], cache["tva"]
    if device.type == "cuda":
        Xtr, ytr, wtr, ttr = Xtr.to(device), ytr.to(device), wtr.to(device), ttr.to(device)

    model = build_model(N_ATTACH_FEAT, args.hidden, n_out).to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    pw = torch.tensor(pos_weight, device=device)
    crit = torch.nn.BCEWithLogitsLoss(pos_weight=pw, reduction="none")
    gen = torch.Generator(device="cpu").manual_seed(args.seed)

    start_epoch, best_auc, best_state, best_epoch, bad = 1, -1.0, None, -1, 0
    if args.state and os.path.exists(args.state):
        st = torch.load(args.state, map_location="cpu", weights_only=False)
        model.load_state_dict(st["model"])
        model.to(device)
        opt.load_state_dict(st["opt"])
        gen.set_state(st["gen_state"])
        start_epoch = st["epoch"] + 1
        best_auc, best_state = st["best_auc"], st["best_state"]
        best_epoch, bad = st["best_epoch"], st["bad"]
        log(f"resumed {args.state}: next epoch {start_epoch} "
            f"(best val AUC {best_auc:.5f} @ epoch {best_epoch}, bad={bad})")

    if args.eval_only:
        blob = torch.load(args.out_model, map_location="cpu", weights_only=False)
        model.load_state_dict(blob["state_dict"])
        model.to(device)
        best_epoch, best_auc = blob.get("best_epoch", -1), blob.get("best_val_auc", -1.0)
        finished = True
    else:
        finished = start_epoch > args.epochs or (0 < args.patience <= bad)
        n_tr = len(Xtr)
        for epoch in range(start_epoch, args.epochs + 1):
            model.train()
            perm = torch.randperm(n_tr, generator=gen)
            tot_loss, tot_w = 0.0, 0.0
            for i in range(0, n_tr, args.batch_size):
                idx = perm[i:i + args.batch_size]
                if device.type == "cuda":
                    idx = idx.to(device)
                xb, yb, wb = Xtr[idx], ytr[idx], wtr[idx]
                opt.zero_grad()
                z = model(xb)
                if n_out > 1:
                    z = z.gather(1, ttr[idx].unsqueeze(1)).squeeze(1)
                else:
                    z = z.squeeze(1)
                l = (crit(z, yb) * wb).sum() / wb.sum()
                l.backward()
                opt.step()
                tot_loss += float(l.detach()) * float(wb.sum())
                tot_w += float(wb.sum())
            s_va = batched_scores(model, Xva, device, tva_np)
            m = yva_np == 1
            auc = wauc(s_va[m], wva_np[m], s_va[~m], wva_np[~m])
            log(f"epoch {epoch:3d} train_loss={tot_loss / tot_w:.5f} val_auc={auc:.5f}")
            if auc > best_auc:
                best_auc, best_epoch, bad = auc, epoch, 0
                best_state = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
            else:
                bad += 1
            if args.state:
                torch.save({"model": {k: v.cpu() for k, v in model.state_dict().items()},
                            "opt": opt.state_dict(), "gen_state": gen.get_state(),
                            "epoch": epoch, "best_auc": best_auc, "best_state": best_state,
                            "best_epoch": best_epoch, "bad": bad}, args.state)
            if bad >= args.patience:
                log(f"early stop at epoch {epoch} (best val AUC {best_auc:.5f} @ {best_epoch})")
                finished = True
                break
            if epoch == args.epochs:
                finished = True
                break
            if args.time_budget_s is not None and time.time() - T0 > args.time_budget_s:
                log(f"PAUSED after epoch {epoch} (budget {args.time_budget_s:.0f}s; "
                    f"re-run the same command to resume from {args.state})")
                return
        log(f"best checkpoint: epoch {best_epoch} val_auc={best_auc:.5f}")
        assert finished and best_state is not None
        model.load_state_dict(best_state)
        model.to(device)

        torch.save({"state_dict": {k: v.cpu() for k, v in model.state_dict().items()},
                    "arch": [N_ATTACH_FEAT, args.hidden, args.hidden, n_out],
                    "feature_names": names, "seed": args.seed,
                    "conditioning": conditioning, "per_type": args.per_type,
                    "best_epoch": best_epoch, "best_val_auc": float(best_auc)},
                   args.out_model)
        with open(args.out_norm, "w") as fh:
            json.dump({"feature_names": names, "conditioning": conditioning,
                       "mean": cache["mu"].tolist(), "std": cache["sd"].tolist(),
                       "seed": args.seed, "degenerate_columns": degenerate,
                       "per_type": args.per_type,
                       "sampling_weights": {
                           "rule": "loss and AUC weighted by the dump 'wgt' branch "
                                   "(bare-T3 FAKES downsampled 1-in-32, wgt=32; true "
                                   "pairs never downsampled) -> trained/judged on the "
                                   "full undownsampled deployed population",
                           "train_W_true": W_pos, "train_W_fake": W_neg,
                           "pos_weight": pos_weight},
                       "displaced_weighting": {
                           "rule": "none (M7/M16: pLS-side displaced true pairs are "
                                   "physically rare -- reported, never force-weighted)"},
                       "train_args": {"epochs": args.epochs, "patience": args.patience,
                                      "batch_size": args.batch_size, "lr": args.lr,
                                      "hidden": args.hidden,
                                      "feature_clip": not args.no_feature_clip}}, fh, indent=1)
        log(f"saved {args.out_model} and {args.out_norm}")

    # ------------------------------------------------ TEST report (frozen test-60)
    Xte = torch.tensor(np.ascontiguousarray(cache["Xte"]))
    tte = cache["tte"]
    s_te = batched_scores(model, Xte, device, tte)
    yte, wte = cache["yte"], cache["wte"]
    vxy, spt, nl = cache["vxy_te"], cache["pt_te"], cache["nl_te"]
    is_true = yte == 1
    is_fake = ~is_true
    acc = is_true & (spt > -998.0)

    print("\n=== M16 general attach head on the FROZEN TEST-60 events "
          "(all AUCs population-weighted by wgt) ===")
    res = {}
    eval_block("ALL (both target types)", s_te[is_true], wte[is_true],
               s_te[is_fake], wte[is_fake], res)
    for tt, nm in ((0, "CHAIN targets (ttype 0)"), (1, "BARE-T3 targets (ttype 1)")):
        mt = tte == tt
        eval_block(nm, s_te[is_true & mt], wte[is_true & mt],
                   s_te[is_fake & mt], wte[is_fake & mt], res)
        # displacement strata vs ALL fakes OF THE SAME TARGET TYPE (fakes carry no
        # displacement class -- the train_chain.py convention, restricted per type).
        fk, wfk = s_te[is_fake & mt], wte[is_fake & mt]
        for tag, lo, hi in (("prompt vxy<1", 0.0, 1.0), ("displaced vxy [1,5)", 1.0, 5.0),
                            ("displaced vxy [5,10)", 5.0, 10.0),
                            ("displaced vxy >=10", 10.0, 1e9),
                            ("displaced vxy >=1 (all)", 1.0, 1e9)):
            m = acc & mt & (vxy >= lo) & (vxy < hi)
            eval_block(f"  tt{tt} {tag}", s_te[m], wte[m], fk, wfk, res)
        m = is_true & ~acc & mt
        eval_block(f"  tt{tt} pileup-sim true", s_te[m], wte[m], fk, wfk, res)
    for tag, m in (("chain nLayers=5", (tte == 0) & (nl == 5)),
                   ("chain nLayers>=6", (tte == 0) & (nl >= 6))):
        eval_block(tag, s_te[is_true & m], wte[is_true & m],
                   s_te[is_fake & m], wte[is_fake & m], res)

    print("\n  logit quantiles 5/25/50/75/95% (unweighted rows):")
    for tt in (0, 1):
        for tag, m in ((f"tt{tt} true", is_true & (tte == tt)),
                       (f"tt{tt} fake", is_fake & (tte == tt))):
            q = np.quantile(s_te[m], [0.05, 0.25, 0.5, 0.75, 0.95])
            print(f"    {tag:>12}: " + " ".join(f"{v:+.2f}" for v in q))

    if args.out_testauc:
        with open(args.out_testauc, "w") as fh:
            json.dump({"model": args.out_model, "per_type": args.per_type,
                       "best_epoch": best_epoch, "best_val_auc": float(best_auc),
                       "test": res}, fh, indent=1)
        log(f"wrote {args.out_testauc}")
    log("TRAINING COMPLETE")


if __name__ == "__main__":
    main()
