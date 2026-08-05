#!/usr/bin/env python3
"""M6 chain-gate classifier training for the LST chain-tracking prototype.

Reads the flat per-chain dump produced by ChainDumpWriter (TTree "chains", one entry
per PRE-arbitration welded chain; feature order recorded in the TNamed
"feature_spec"), trains a small MLP chain-gate classifier (plan 5a: the hard
irreversible decision, taken at maximum evidence) and evaluates it on held-out TEST
events.

Mirrors train_edge.py discipline exactly:
  - fixed seeds for numpy AND torch,
  - event-level 60/20/20 split by evt (row-level splits forbidden, plan 5c),
  - feature conditioning BEFORE standardization, recorded in the norm json for the
    C++ port (export_chain_weights.py bakes it into chain_mlp_weights.h),
  - standardization mean/std fit on TRAIN rows only, saved to the norm json,
  - class imbalance via BCEWithLogits pos_weight = n_fake/n_true (train),
  - displaced-aware weighting: TRUE chains with simVxy >= 1 cm get weight x4
    (simple and recorded in the norm json; the M5 dxy diagnosis showed the binding
    displaced losses sit at the scoring margin),
  - 60 epochs, early stopping patience 8 on val AUC.

Conditioning (v2-edge lesson: heavy-tailed / curvature columns must be tamed BEFORE
standardization or they cripple their own slots):
  cf_05 fullFitChi2PerHit  -> log10(1+x)   (chi2 right tail spans decades)
  cf_06 rzLineChi2PerHit   -> log10(1+x)
  cf_14 maxJunctionDegProduct -> log10(1+x) (jet-tail degree products)
  cf_07 fitKappa           -> clip [-1, 1]  (curvature block, v2 edge spec)
  cf_08 dKappaFitVsMedianT3-> clip [-2, 2]

Report: test AUC overall + per nLayers (4 / 5 / 6+) + prompt/displaced (vxy>=1).

Outputs:
  chain_mlp_v1.pt    - torch state_dict + metadata (arch, feature names, best epoch/AUC)
  chain_norm_v1.json - per-feature mean/std + conditioning + weighting spec
"""

import argparse
import copy
import json
import os
import sys
import time

import numpy as np

T0 = time.time()

N_CHAIN_FEAT = 25  # a2: extended contract (16 frozen M6 slots + 9 a2 additions)


def log(msg):
    print(f"[{time.time() - T0:8.1f}s] {msg}", flush=True)


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    d = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype"
    p.add_argument("--input", nargs="+", default=[f"{d}/chains_300evt.root"],
                   help="chain dump file(s). ONE file: original behavior (train_frac/"
                        "val_frac event split). MULTIPLE files: M8 COMBINATION RULE -- "
                        "file[0] is the PRIMARY (its original seed-<seed> test events "
                        "stay the frozen test set); extra files drop every event whose "
                        "evt id appears in the primary file, and the remaining events "
                        "(primary non-test + extra new-id) are re-split by event "
                        "pool_train_frac/(1-pool_train_frac) into train/val")
    p.add_argument("--pool-train-frac", type=float, default=0.75,
                   help="combined mode only: train fraction of the (non-frozen-test) "
                        "event pool")
    p.add_argument("--out-model", default=f"{d}/chain_mlp_v1.pt")
    p.add_argument("--out-norm", default=f"{d}/chain_norm_v1.json")
    p.add_argument("--seed", type=int, default=42, help="seed for numpy AND torch")
    p.add_argument("--epochs", type=int, default=60)
    p.add_argument("--patience", type=int, default=8, help="early stopping on val AUC")
    p.add_argument("--displaced-weight", type=float, default=4.0,
                   help="weight multiplier for TRUE chains with simVxy >= 1 cm")
    p.add_argument("--displaced-weight-mid", type=float, default=None,
                   help="tiered mode: weight for TRUE chains with 1 <= simVxy < 5 cm "
                        "(with --displaced-weight-hi, replaces the flat --displaced-weight rule)")
    p.add_argument("--displaced-weight-hi", type=float, default=None,
                   help="tiered mode: weight for TRUE chains with simVxy >= 5 cm")
    p.add_argument("--select-metric", choices=["val_auc", "min_prompt_disp", "surv_auc"], default="val_auc",
                   help="best-checkpoint / early-stop metric: 'val_auc' = overall val AUC (v1 "
                        "behavior); 'min_prompt_disp' = min(prompt val AUC, displaced val AUC), "
                        "prompt = true vxy<1 vs all val fakes, displaced = true vxy>=1 vs all "
                        "val fakes")
    p.add_argument("--no-feature-clip", action="store_true",
                   help="disable feature conditioning (debug)")
    p.add_argument("--hidden", type=int, default=32,
                   help="hidden width (a2 gate capacity axis; M6-M9 gates used 24)")
    p.add_argument("--n-input", type=int, default=0,
                   help="a2 CONTROL: use only the FIRST N feature columns (0 = all). "
                        "--n-input 16 reproduces the M6-M9 16-feature gate on exactly "
                        "the same rows/split, which is the honest ablation baseline "
                        "for the 9 a2 additions.")
    p.add_argument("--dca-split", type=float, default=0.5,
                   help="a2 report: -G 5 branch boundary for the per-branch AUC table")
    p.add_argument("--no-perm-importance", action="store_true")
    p.add_argument("--out-report", default="", help="a2: json summary of the test report")
    p.add_argument("--batch-size", type=int, default=16384)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--train-frac", type=float, default=0.6)
    p.add_argument("--val-frac", type=float, default=0.2)
    p.add_argument("--state-file", default="",
                   help="checkpoint/resume file: state (model+optimizer+rng+best "
                        "trackers) is saved after every epoch; if the file exists the "
                        "run resumes from it EXACTLY (same command). The data pipeline "
                        "(load/split/standardize) is deterministic and recomputed.")
    # ---- okretrain (M18) additions: the ORDER-KEY head trains on the K9 claim-stage
    # survivor population, not on all welded chains.
    p.add_argument("--survivor-npz", default="",
                   help="npz from ok_gate.py with a boolean 'surv' array aligned to the "
                        "dump rows (the K9 claim-stage population). Enables the survivor "
                        "weighting / restriction / reporting below.")
    p.add_argument("--survivor-weight", type=float, default=1.0,
                   help="multiply the per-sample loss weight of SURVIVOR rows by this "
                        "(1 = off; variant (b) uses 8)")
    p.add_argument("--survivor-only", action="store_true",
                   help="variant (c): train AND validate on survivor rows only "
                        "(test evaluation still reports both populations)")
    p.add_argument("--target", choices=["label", "matchfrac"], default="label",
                   help="variant (d): 'matchfrac' trains BCEWithLogits against the SOFT "
                        "target matchFrac (graded 'how much of this chain will the harness "
                        "match'), keeping the logit output scale the -B order key needs")
    p.add_argument("--matchfrac-lo", type=float, default=0.0,
                   help="matchfrac target: rescale soft target as clip((mf-lo)/(hi-lo),0,1)")
    p.add_argument("--matchfrac-hi", type=float, default=1.0)
    p.add_argument("--wall-limit-sec", type=float, default=0.0,
                   help="if > 0: after the first epoch that ends beyond this wall time, "
                        "save state and exit(3) (resume by rerunning the same command). "
                        "Lets long trainings run as sequential foreground chunks.")
    return p.parse_args()


# ---------------------------------------------------------------- data loading

META_BRANCHES = ["evt", "label", "simVxy", "simPt", "nLayers", "dcaXY", "matchFrac", "score"]


def load_dump(path):
    """Load the chain dump. Returns (meta dict, X float32 [N,16], names)."""
    import uproot

    f = uproot.open(path)
    spec = f["feature_spec"].member("fTitle")
    assert spec.startswith("cf:"), f"unexpected feature_spec '{spec[:20]}...'"
    cf_names = spec[3:].split(",")
    assert len(cf_names) == N_CHAIN_FEAT, f"expected {N_CHAIN_FEAT} features, got {len(cf_names)}"
    feat_branches = [f"cf_{i:02d}" for i in range(N_CHAIN_FEAT)]
    names = [f"cf_{n}" for n in cf_names]

    tree = f["chains"]
    arr = tree.arrays(META_BRANCHES + feat_branches, library="np")
    meta = {k: arr[k] for k in META_BRANCHES}
    n = len(meta["label"])
    X = np.empty((n, N_CHAIN_FEAT), dtype=np.float32)
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
            "(ChainFeatures.cc contract says this cannot happen)")
        np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
    if degenerate:
        log(f"degenerate (zero-variance) columns: {degenerate}")
    return degenerate


# ------------------------------------------------------- feature conditioning

# Applied IN ORDER to the named columns BEFORE standardization. The C++ inference
# port must reproduce this exactly; the applied spec is written into the norm json
# (same op vocabulary as train_edge.py / export_weights.py: "clip", "log10_1p").
CONDITIONING_SPEC = [
    {"feature": "cf_fullFitChi2PerHit", "op": "log10_1p"},
    {"feature": "cf_rzLineChi2PerHit", "op": "log10_1p"},
    {"feature": "cf_maxJunctionDegProduct", "op": "log10_1p"},
    {"feature": "cf_fitKappa", "op": "clip", "lo": -1.0, "hi": 1.0},
    {"feature": "cf_dKappaFitVsMedianT3", "op": "clip", "lo": -2.0, "hi": 2.0},
    # a2 additions: the three heavy-tailed residual/chi2 columns get the same
    # log10(1+x) treatment as their mean-valued cousins (cf_05/cf_06). The five
    # t3dnn score columns are already probabilities in [0,1] and cf_18 (edge-logit
    # std) is bounded by the logit scale -- both left raw.
    {"feature": "cf_maxXyResid", "op": "log10_1p"},
    {"feature": "cf_maxRzResid", "op": "log10_1p"},
    {"feature": "cf_maxBridgeChi2", "op": "log10_1p"},
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
    """60/20/20 split on evt keys (the chain dump has no lumi branch; the 300-evt
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
        f"{tr.sum()}/{va.sum()}/{te.sum()} chains (train/val/test)")
    return tr, va, te


def combined_event_split(meta, src, args, rng):
    """M8 COMBINATION RULE split (leak-proof, plan 10.4c) -- mirrors train_edge.py.

    Frozen test = the ORIGINAL seed event-level test split of the PRIMARY (first
    --input) file, reproduced exactly (shuffle of the primary unique evt keys with
    the fresh seed rng, same train_frac/val_frac slices). Every other event
    (primary non-test + kept extra-file events; overlap ids dropped at load) forms
    the pool, re-split by event pool_train_frac/(1-pool_train_frac)."""
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
    prim = set(uniq0.tolist())
    n_tr_p = sum(1 for k in tr_keys.tolist() if k in prim)
    n_va_p = sum(1 for k in va_keys.tolist() if k in prim)
    log(f"COMBINED event split: frozen test = {len(te_keys)} primary events (unchanged); "
        f"pool {len(pool_keys)} events -> train {len(tr_keys)} "
        f"({n_tr_p} primary + {len(tr_keys) - n_tr_p} extra) / "
        f"val {len(va_keys)} ({n_va_p} primary + {len(va_keys) - n_va_p} extra) "
        f"-> {tr.sum()}/{va.sum()}/{te.sum()} chains (train/val/test)")
    return tr, va, te


# ---------------------------------------------------------------- model

def build_model(n_in, n_hid=32):
    import torch.nn as nn
    return nn.Sequential(nn.Linear(n_in, n_hid), nn.ReLU(),
                         nn.Linear(n_hid, n_hid), nn.ReLU(),
                         nn.Linear(n_hid, 1))


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

    # Load input file(s). Multiple files engage the M8 COMBINATION RULE: file[0] is
    # the primary; extra files drop every event whose evt id appears in the primary.
    metas, x_parts, src_parts = [], [], []
    names = prim_evts = None
    for si, path in enumerate(args.input):
        meta_i, X_i, names_i = load_dump(path)
        if si == 0:
            names = names_i
            prim_evts = np.unique(meta_i["evt"])
            log(f"input[0] PRIMARY {path}: {len(X_i)} chains over {len(prim_evts)} events")
        else:
            assert names_i == names, f"feature_spec mismatch: {path}"
            evts_i = np.unique(meta_i["evt"])
            keep = ~np.isin(meta_i["evt"], prim_evts)
            kept_evts = np.unique(meta_i["evt"][keep])
            log(f"input[{si}] EXTRA {path}: {len(X_i)} chains over {len(evts_i)} events; "
                f"combination rule drops {len(evts_i) - len(kept_evts)} overlap events -> "
                f"keep {int(keep.sum())} chains over {len(kept_evts)} events")
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
    log(f"loaded {n_all} chains x {X.shape[1]} features from {len(args.input)} file(s)")

    # ---- okretrain: K9 claim-stage survivor mask (deployment population of the -B key)
    surv = np.zeros(n_all, dtype=bool)
    if args.survivor_npz:
        z = np.load(args.survivor_npz)
        s = z["surv"]
        assert len(s) == n_all, f"survivor npz has {len(s)} rows, dump has {n_all}"
        assert np.array_equal(z["evt"].astype(np.uint64), meta["evt"].astype(np.uint64)), \
            "survivor npz evt order does not match the dump (stale npz?)"
        surv = s.astype(bool)
        log(f"survivor mask: {int(surv.sum())} / {n_all} rows ({surv.mean():.4f}); "
            f"trueFrac all={float((meta['label'] == 1).mean()):.4f} "
            f"surv={float((meta['label'][surv] == 1).mean()):.4f}")
    elif args.survivor_only or args.survivor_weight != 1.0:
        raise SystemExit("--survivor-only / --survivor-weight require --survivor-npz")

    degenerate = data_quality_report(X, names)

    conditioning = []
    if not args.no_feature_clip:
        conditioning = apply_conditioning(X, names, CONDITIONING_SPEC)

    # a2 CONTROL knob: restrict to the first N columns (slots 0-15 are the frozen M6
    # contract, so --n-input 16 IS the pre-a2 gate on identical rows and split).
    if args.n_input and args.n_input < X.shape[1]:
        keep = args.n_input
        dropped = names[keep:]
        X = np.ascontiguousarray(X[:, :keep])
        names = names[:keep]
        conditioning = [c for c in conditioning if c["feature"] in names]
        log(f"--n-input {keep}: dropped {len(dropped)} columns {dropped}")

    if src is None:
        tr, va, te = event_split(meta, args.train_frac, args.val_frac, rng)
    else:
        tr, va, te = combined_event_split(meta, src, args, rng)

    # ---- okretrain variant (c): survivor-ONLY fit set. The event split is unchanged
    # (same frozen test events); only the ROWS the model sees in train/val are cut down
    # to the K9 claim-stage population. The test mask stays FULL so the report can show
    # both the survivor-restricted and the all-chain AUC.
    if args.survivor_only:
        n_tr0, n_va0 = int(tr.sum()), int(va.sum())
        tr = tr & surv
        va = va & surv
        log(f"--survivor-only: train {n_tr0} -> {int(tr.sum())}, val {n_va0} -> {int(va.sum())} rows")

    # ---- standardization from TRAIN rows ----
    mu = X[tr].mean(axis=0, dtype=np.float64).astype(np.float32)
    sd = X[tr].std(axis=0, dtype=np.float64).astype(np.float32)
    sd[sd < 1e-8] = 1.0
    Xs = (X - mu) / sd

    y = (meta["label"] == 1).astype(np.float32)
    n_pos, n_neg = int(y[tr].sum()), int((1 - y[tr]).sum())
    pos_weight = n_neg / max(n_pos, 1)
    log(f"train: {n_pos} true / {n_neg} fake -> pos_weight={pos_weight:.4f}")

    # ---- displaced-aware weighting ----
    # Flat rule (v1): true chains with simVxy >= 1 cm get x args.displaced_weight.
    # Tiered rule (v2, --displaced-weight-mid/-hi): true [1,5) get x mid, true >= 5 get
    # x hi; fakes and prompt true stay 1.
    w_all = np.ones(n_all, dtype=np.float32)
    is_true_all = meta["label"] == 1
    disp_true = is_true_all & (meta["simVxy"] >= 1.0)
    tiered = args.displaced_weight_mid is not None and args.displaced_weight_hi is not None
    if tiered:
        mid_true = is_true_all & (meta["simVxy"] >= 1.0) & (meta["simVxy"] < 5.0)
        hi_true = is_true_all & (meta["simVxy"] >= 5.0)
        w_all[mid_true] = args.displaced_weight_mid
        w_all[hi_true] = args.displaced_weight_hi
        weight_spec = {"rule": "tiered: true chains with 1<=simVxy<5 get weight x mid, "
                               "simVxy>=5 get x hi; fakes and prompt true 1",
                       "displaced_weight_mid": args.displaced_weight_mid,
                       "displaced_weight_hi": args.displaced_weight_hi,
                       "n_weighted_mid_train": int((mid_true & tr).sum()),
                       "n_weighted_hi_train": int((hi_true & tr).sum())}
        log(f"displaced weighting (tiered): x{args.displaced_weight_mid:g} on "
            f"{int((mid_true & tr).sum())} train true chains vxy [1,5), "
            f"x{args.displaced_weight_hi:g} on {int((hi_true & tr).sum())} train true "
            f"chains vxy>=5 ({int(disp_true.sum())} displaced true total)")
    else:
        w_all[disp_true] = args.displaced_weight
        weight_spec = {"rule": "true chains with simVxy >= 1 cm get weight x displaced_weight; all else 1",
                       "displaced_weight": args.displaced_weight,
                       "n_weighted_train": int((disp_true & tr).sum())}
        log(f"displaced weighting: x{args.displaced_weight:g} on {int((disp_true & tr).sum())} "
            f"train true chains with simVxy>=1 ({int(disp_true.sum())} total)")

    # ---- okretrain variant (b): survivor emphasis. Multiplicative on top of the
    # displaced weighting, so a displaced TRUE survivor gets both factors.
    if args.survivor_weight != 1.0:
        w_all[surv] *= args.survivor_weight
        weight_spec["survivor_weight"] = args.survivor_weight
        weight_spec["n_survivor_train"] = int((surv & tr).sum())
        log(f"survivor weighting: x{args.survivor_weight:g} on {int((surv & tr).sum())} "
            f"train survivor rows")
    weight_spec["survivor_only"] = bool(args.survivor_only)
    weight_spec["target"] = args.target

    # ---- okretrain variant (d): graded target. The order key wants "how much of this
    # chain will the harness match", which is matchFrac, not the thresholded label.
    # BCEWithLogits with a SOFT target keeps the output on the logit scale the -B key
    # (score - alpha*max(0,-logit)) needs; a plain regression head would not.
    if args.target == "matchfrac":
        mf = meta["matchFrac"].astype(np.float32)
        lo, hi = args.matchfrac_lo, args.matchfrac_hi
        t_soft = np.clip((np.where(mf < 0, 0.0, mf) - lo) / max(hi - lo, 1e-6), 0.0, 1.0)
        t_soft = t_soft.astype(np.float32)
        log(f"target=matchfrac: soft target from matchFrac rescaled [{lo:g},{hi:g}] -> "
            f"mean={float(t_soft.mean()):.4f} (train mean {float(t_soft[tr].mean()):.4f}); "
            f"binary label mean {float(y[tr].mean()):.4f}")
        y_fit = t_soft
    else:
        y_fit = y

    Xtr = torch.tensor(np.ascontiguousarray(Xs[tr]))
    ytr = torch.tensor(np.ascontiguousarray(y_fit[tr]))
    wtr = torch.tensor(np.ascontiguousarray(w_all[tr]))
    Xva = torch.tensor(np.ascontiguousarray(Xs[va]))
    yva_np = y[va]
    vxy_va = meta["simVxy"][va]
    va_true = yva_np == 1
    va_fake = ~va_true
    va_prompt_true = va_true & (vxy_va < 1.0)
    va_disp_true = va_true & (vxy_va >= 1.0)
    va_surv = surv[va]
    if device.type == "cuda":
        Xtr, ytr, wtr = Xtr.to(device), ytr.to(device), wtr.to(device)

    model = build_model(X.shape[1], args.hidden).to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    # Elementwise loss * per-sample weight, then mean (identical to plain mean
    # reduction when all weights are 1).
    crit = torch.nn.BCEWithLogitsLoss(pos_weight=torch.tensor(pos_weight, device=device),
                                      reduction="none")

    from sklearn.metrics import roc_auc_score

    def subgroup_auc(s, m_pos, m_neg):
        yy = np.concatenate([np.ones(int(m_pos.sum())), np.zeros(int(m_neg.sum()))])
        ss = np.concatenate([s[m_pos], s[m_neg]])
        return roc_auc_score(yy, ss)

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
        log(f"RESUMED from {args.state_file}: next epoch {start_epoch}, "
            f"best sel {best_sel:.5f} @ epoch {best_epoch}, bad={bad}")
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
            loss = (crit(model(xb).squeeze(1), yb) * wtr[idx]).mean()
            loss.backward()
            opt.step()
            tot_loss += float(loss.detach()) * len(idx)
        s_va = batched_scores(model, Xva, device)
        auc = roc_auc_score(yva_np, s_va)
        if args.select_metric == "min_prompt_disp":
            auc_p = subgroup_auc(s_va, va_prompt_true, va_fake)
            auc_d = subgroup_auc(s_va, va_disp_true, va_fake)
            sel = min(auc_p, auc_d)
            epoch_meta = {"val_auc": float(auc), "val_auc_prompt": float(auc_p),
                          "val_auc_disp": float(auc_d)}
            log(f"epoch {epoch:3d} train_loss={tot_loss / n_tr:.5f} val_auc={auc:.5f} "
                f"prompt={auc_p:.5f} disp={auc_d:.5f} sel=min={sel:.5f}")
        elif args.select_metric == "surv_auc":
            # okretrain: select on the DEPLOYMENT population (K9 claim-stage survivors)
            # in the val events, not on all welded chains.
            sel = subgroup_auc(s_va, va_true & va_surv, va_fake & va_surv)
            epoch_meta = {"val_auc": float(auc), "val_auc_surv": float(sel)}
            log(f"epoch {epoch:3d} train_loss={tot_loss / n_tr:.5f} val_auc={auc:.5f} "
                f"surv={sel:.5f}")
        else:
            sel = auc
            epoch_meta = {"val_auc": float(auc)}
            log(f"epoch {epoch:3d} train_loss={tot_loss / n_tr:.5f} val_auc={auc:.5f}")
        stop = False
        if sel > best_sel:
            best_sel, best_epoch, bad = sel, epoch, 0
            best_meta = epoch_meta
            best_state = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
        else:
            bad += 1
            if bad >= args.patience:
                log(f"early stop at epoch {epoch} (best sel metric {best_sel:.5f} @ epoch {best_epoch})")
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
            log(f"wall limit {args.wall_limit_sec:.0f}s reached after epoch {epoch}; "
                f"state saved to {args.state_file} -> exit(3), rerun same command to resume")
            sys.exit(3)
    best_auc = best_meta.get("val_auc", best_sel)
    log(f"best checkpoint: epoch {best_epoch} select_metric={args.select_metric} "
        f"sel={best_sel:.5f} meta={best_meta}")
    model.load_state_dict(best_state)
    model.to(device)

    # ---- save model + normalization ----
    torch.save({"state_dict": best_state,
                "arch": [X.shape[1], args.hidden, args.hidden, 1],
                "feature_names": names, "seed": args.seed,
                "conditioning": conditioning,
                "best_epoch": best_epoch, "best_val_auc": float(best_auc),
                "select_metric": args.select_metric, "best_sel": float(best_sel),
                "best_val_meta": best_meta}, args.out_model)
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
                                  "displaced_weight": args.displaced_weight,
                                  "displaced_weight_mid": args.displaced_weight_mid,
                                  "displaced_weight_hi": args.displaced_weight_hi,
                                  "select_metric": args.select_metric,
                                  "hidden": args.hidden, "n_input": args.n_input,
                                  "inputs": args.input,
                                  "pool_train_frac": args.pool_train_frac}}, fh, indent=1)
    log(f"saved {args.out_model} and {args.out_norm}")

    # ---- TEST evaluation: overall + per nLayers (4/5/6+) + prompt/displaced ----
    Xte = torch.tensor(np.ascontiguousarray(Xs[te]))
    s_te = batched_scores(model, Xte, device)
    lab_te = meta["label"][te]
    vxy_te = meta["simVxy"][te]
    nl_te = meta["nLayers"][te]
    is_true = lab_te == 1
    is_fake = ~is_true

    print("\n=== chain-gate MLP on TEST events ===")
    res = {}
    res["all"] = eval_block("all", s_te[is_true], s_te[is_fake])
    for tag, m in (("nLayers=4", nl_te == 4), ("nLayers=5", nl_te == 5), ("nLayers>=6", nl_te >= 6)):
        res[tag] = eval_block(tag, s_te[is_true & m], s_te[is_fake & m])
    # vxy subgroups: true chains of the stratum vs ALL test fakes (fakes have simVxy
    # = -999, they carry no displacement class -- same convention as train_edge.py).
    res["prompt vxy<1"] = eval_block("prompt vxy<1", s_te[is_true & (vxy_te < 1)], s_te[is_fake])
    res["displaced vxy>=1"] = eval_block("displaced vxy>=1", s_te[is_true & (vxy_te >= 1)], s_te[is_fake])
    res["displaced vxy>=5"] = eval_block("displaced vxy>=5", s_te[is_true & (vxy_te >= 5)], s_te[is_fake])

    # ---- okretrain: SURVIVOR-RESTRICTED test AUC. This is the deployment population
    # of the -B order key: the chains that actually reach K9's greedy claim. Every
    # variant is compared here; the all-chain block above is for the record only.
    sv_te = surv[te]
    print("\n=== SURVIVOR-RESTRICTED (K9 claim-stage) TEST AUC ===")
    res["surv all"] = eval_block("surv all", s_te[is_true & sv_te], s_te[is_fake & sv_te])
    for tag, m in (("surv nLayers=4", nl_te == 4), ("surv nLayers=5", nl_te == 5),
                   ("surv nLayers>=6", nl_te >= 6)):
        res[tag] = eval_block(tag, s_te[is_true & sv_te & m], s_te[is_fake & sv_te & m])
    res["surv prompt vxy<1"] = eval_block("surv prompt vxy<1",
                                          s_te[is_true & sv_te & (vxy_te < 1)], s_te[is_fake & sv_te])
    res["surv displaced vxy>=1"] = eval_block("surv displaced vxy>=1",
                                              s_te[is_true & sv_te & (vxy_te >= 1)], s_te[is_fake & sv_te])
    res["surv displaced vxy>=5"] = eval_block("surv displaced vxy>=5",
                                              s_te[is_true & sv_te & (vxy_te >= 5)], s_te[is_fake & sv_te])
    # The -B key only penalizes chains with a NEGATIVE logit, so the ranking that
    # matters most is inside the negative tail of the survivor population.
    neg = sv_te & (s_te < 0)
    res["surv negative-logit tail"] = eval_block("surv logit<0", s_te[is_true & neg], s_te[is_fake & neg])
    print(f"  survivor rows in test: {int(sv_te.sum())} of {len(sv_te)} "
          f"({float(sv_te.mean()):.4f}); negative-logit survivors {int(neg.sum())}")

    # Logit-scale context for threshold picking in the K9 sweep.
    for tag, m in (("true", is_true), ("fake", is_fake)):
        q = np.quantile(s_te[m], [0.05, 0.25, 0.5, 0.75, 0.95])
        print(f"  test {tag} logit quantiles 5/25/50/75/95%: "
              + " ".join(f"{v:+.2f}" for v in q))

    # ---- a2: PER -G 5 BRANCH evaluation ------------------------------------------
    # The M9 residual fake is localized (~85%) in the EXEMPT (dcaXY >= dcaSplit)
    # 5+-layer branch, and the best-displaced w1/z-configs live in the exempt T4
    # branch. Those are the two cells that must move, so they get their own AUCs.
    dca_te = meta["dcaXY"][te]
    XS = args.dca_split
    print(f"\n=== a2 per-branch AUC (dcaSplit = {XS} cm; branch = the -G 5 split) ===")
    branch = {}
    for bname, bmask in (("IP dca<%g" % XS, dca_te < XS), ("EXEMPT dca>=%g" % XS, dca_te >= XS)):
        for lname, lmask in (("L4", nl_te <= 4), ("L5+", nl_te >= 5), ("Lall", np.ones(len(nl_te), bool))):
            m = bmask & lmask
            print(f"-- {bname} {lname}: n={int(m.sum())}")
            branch[f"{bname}|{lname}|all"] = eval_block("all-true vs fake", s_te[is_true & m], s_te[is_fake & m])
            branch[f"{bname}|{lname}|prompt"] = eval_block(
                "prompt vxy<1", s_te[is_true & m & (vxy_te < 1)], s_te[is_fake & m])
            branch[f"{bname}|{lname}|disp1"] = eval_block(
                "displaced vxy>=1", s_te[is_true & m & (vxy_te >= 1)], s_te[is_fake & m])
            branch[f"{bname}|{lname}|disp5"] = eval_block(
                "displaced vxy>=5", s_te[is_true & m & (vxy_te >= 5)], s_te[is_fake & m])
    res["branch"] = branch

    # ---- a2: PERMUTATION IMPORTANCE ----------------------------------------------
    # Shuffle one STANDARDIZED test column at a time (fixed rng) and record the AUC
    # drop, overall and in the two branch cells that matter. Reported sorted by the
    # exempt-5+ displaced drop -- the M9 representational target.
    if not args.no_perm_importance:
        log("permutation importance on TEST rows ...")
        prng = np.random.default_rng(args.seed + 1)
        Xte_np = np.ascontiguousarray(Xs[te])
        base_all = res["all"]["auc"]
        ex5 = (dca_te >= XS) & (nl_te >= 5)
        ip4 = (dca_te < XS) & (nl_te <= 4)

        def cell_auc(scores, m, true_extra=None):
            tm = is_true & m if true_extra is None else is_true & m & true_extra
            fm = is_fake & m
            if tm.sum() == 0 or fm.sum() == 0:
                return None
            yy = np.concatenate([np.ones(int(tm.sum())), np.zeros(int(fm.sum()))])
            ss = np.concatenate([scores[tm], scores[fm]])
            return float(roc_auc_score(yy, ss))

        base_ex5_all = cell_auc(s_te, ex5)
        base_ex5_disp = cell_auc(s_te, ex5, vxy_te >= 1)
        base_ip4_all = cell_auc(s_te, ip4)
        rows = []
        perm_idx = prng.permutation(len(Xte_np))
        for j, nm in enumerate(names):
            Xp = Xte_np.copy()
            Xp[:, j] = Xte_np[perm_idx, j]
            sp = batched_scores(model, torch.tensor(Xp), device)
            rows.append((nm,
                         base_all - roc_auc_score(np.concatenate([np.ones(int(is_true.sum())),
                                                                  np.zeros(int(is_fake.sum()))]),
                                                  np.concatenate([sp[is_true], sp[is_fake]])),
                         (base_ex5_all - cell_auc(sp, ex5)) if base_ex5_all else 0.0,
                         (base_ex5_disp - cell_auc(sp, ex5, vxy_te >= 1)) if base_ex5_disp else 0.0,
                         (base_ip4_all - cell_auc(sp, ip4)) if base_ip4_all else 0.0))
            del Xp
        print(f"\n=== a2 permutation importance (AUC DROP when the column is shuffled) ===")
        print(f"  baselines: all={base_all:.5f} exempt5+_all={base_ex5_all:.5f} "
              f"exempt5+_disp={base_ex5_disp:.5f} ip_L4_all={base_ip4_all:.5f}")
        print(f"  {'feature':>24} {'dAUC_all':>10} {'dAUC_ex5':>10} {'dAUC_ex5disp':>13} {'dAUC_ipL4':>10}")
        for nm, d0, d1, d2, d3 in sorted(rows, key=lambda r: -r[3]):
            print(f"  {nm:>24} {d0:>10.5f} {d1:>10.5f} {d2:>13.5f} {d3:>10.5f}")
        res["perm_importance"] = [{"feature": nm, "dAUC_all": d0, "dAUC_exempt5": d1,
                                   "dAUC_exempt5_disp": d2, "dAUC_ip_L4": d3}
                                  for nm, d0, d1, d2, d3 in rows]

    if args.out_report:
        with open(args.out_report, "w") as fh:
            json.dump({"model": args.out_model, "n_input": X.shape[1],
                       "hidden": args.hidden, "best_epoch": best_epoch,
                       "best_sel": float(best_sel), "best_val_meta": best_meta,
                       "test": res, "inputs": args.input, "dca_split": XS}, fh, indent=1)
        log(f"wrote {args.out_report}")

    log("done")


if __name__ == "__main__":
    main()
