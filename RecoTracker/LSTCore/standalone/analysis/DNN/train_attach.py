#!/usr/bin/env python3

# SUPERSEDED -- does NOT produce the shipped AttachNetworkWeights.h.
# The shipped attach head comes from standalone/nnloop_ref/s3_work/train_s3.py (22 inputs, the
# fourth network deleted). An on-policy retrain behind the round-2 chain gate is in progress.
# See analysis/DNN/README.md. Kept for lineage only.

"""train_attach.py -- trainer of the DEPLOYED attach head.

This is the script that produced the head in src/alpaka/AttachNetworkWeights.h (20 inputs:
the 19-slot frozen contract plus af_rphiResidInwards). It replaces the earlier 18-input-only
version, which could not express the deployed head at all. The pair dump it consumes is
produced by the standalone prototype with -PDML 1 (see standalone/r1_ref/r1_dump.sh); export
the trained checkpoint with export_attach_weights.py beside this file.
"""


import argparse
import copy
import glob
import json
import os
import time

import numpy as np

T0 = time.time()

# R1: the dump's own feature_spec is authoritative; this is only the LOWER bound every
# dump must satisfy (slots 0-18 are the frozen M16 contract).
N_BASE_FEAT = 19
PROTO_DIR = os.path.dirname(os.path.abspath(__file__))
# The C++ slot order (PixelAttach.cc kAttachFeatNames). A selection that is a PREFIX of
# this list is deployable with no C++ edit; any other selection needs the layout permuted.
CPP_ORDER = ["af_log10PtIn", "af_ptErrRel", "af_etaErr", "af_charge", "af_isQuad",
             "af_log10CircleRadius", "af_plsDeltaPhi", "af_fitKappaSigned",
             "af_chainTanLambda", "af_innermostLayer", "af_nLayers", "af_chainGateLogit",
             "af_chargeAgree", "af_dKappa", "af_dTanLambda", "af_dPhiAtInnermost",
             "af_circleCenterDist", "af_zResidAtInnermost", "af_targetType",
             # R1 block, ordered by measured permutation importance (r1_perm.py) so every
             # candidate head is a prefix -- MUST match PixelAttach.cc kAttachFeatNames.
             "af_rphiResidInwards", "af_rphiResidRms", "af_rzResidRms",
             "af_dPhiAtOutermost", "af_rphiResidMax", "af_rzResidMax", "af_rphiChi2Lst",
             "af_rzChi2Lst", "af_absPlsEta", "af_radiusPull", "af_radiusAsym",
             "af_log10TgtRadius", "af_log10SigmaR", "af_zResidPull"]
FROZEN_TEST60 = ("/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/"
                 "standalone/prototype/m12_test60_evts.json")


def log(msg):
    print(f"[{time.time() - T0:8.1f}s] {msg}", flush=True)


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", default=f"{PROTO_DIR}/dump/pr_c*.root",
                   help="PRIMARY dump glob (defines the frozen TEST-60)")
    p.add_argument("--extra", default=f"{PROTO_DIR}/dump/sv_c*.root",
                   help="EXTRA dump glob (salvage events; overlap dropped by evt key)")
    p.add_argument("--out-model", default=f"{PROTO_DIR}/attach_mlp_r1.pt")
    p.add_argument("--out-norm", default=f"{PROTO_DIR}/attach_norm_r1.json")
    p.add_argument("--out-testauc", default=None)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--epochs", type=int, default=60)
    p.add_argument("--patience", type=int, default=8)
    p.add_argument("--hidden", type=int, default=24)
    p.add_argument("--batch-size", type=int, default=16384)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--train-frac", type=float, default=0.6)
    p.add_argument("--val-frac", type=float, default=0.2)
    p.add_argument("--pool-train-frac", type=float, default=0.75)
    # --- M19 objective knobs ---
    p.add_argument("--gamma-neg", type=float, default=0.0,
                   help="one-sided focal exponent on NEGATIVES (0 = plain BCE = the g1 "
                        "recipe). w_neg *= sigmoid(z)**gamma_neg")
    p.add_argument("--disp-mid", type=float, default=1.0,
                   help="extra multiplier on TRUE pairs with 1 <= simVxy < 5")
    p.add_argument("--disp-hi", type=float, default=1.0,
                   help="extra multiplier on TRUE pairs with simVxy >= 5")
    p.add_argument("--chain-weight", type=float, default=1.0,
                   help="multiplier on ALL ttype==0 (chain-target) rows")
    p.add_argument("--val-metric", choices=["all", "chain"], default="chain",
                   help="early-stopping metric: wgt-weighted AUC over both target types "
                        "('all', the g1 behaviour) or over CHAIN pairs only ('chain', the "
                        "universe -a cuts on)")
    p.add_argument("--val-cap", type=int, default=6_000_000,
                   help="max rows used for the PER-EPOCH val AUC (seeded subsample). "
                        "The full 26M-row weighted AUC costs 61 s/epoch against ~12 s of "
                        "actual training, and an early-stopping signal does not need it; "
                        "the FINAL model is still judged on the full frozen TEST-60.")
    p.add_argument("--use-cols", default="all",
                   help="R1 column selection: 'all', 'base19', or a comma-separated list "
                        "of feature names (af_* or bare). The order given IS the head's "
                        "input order.")
    p.add_argument("--cache", default=f"{PROTO_DIR}/cache_ret.npz")
    p.add_argument("--state", default=None)
    p.add_argument("--eval-only", action="store_true")
    return p.parse_args()


# ---------------------------------------------------------------- data loading

META_BRANCHES = ["evt", "label", "ttype", "wgt", "simVxy", "simPt", "chainNLayers"]


def load_dump(pattern, tag):
    import uproot

    files = sorted(glob.glob(pattern))
    assert files, f"no dump chunks matched {pattern}"
    trees, ns, names = [], [], None
    for path in files:
        f = uproot.open(path)
        spec = f["feature_spec"].member("fTitle")
        assert spec.startswith("af:"), f"unexpected feature_spec in {path}"
        af_names = spec[3:].split(",")
        assert len(af_names) >= N_BASE_FEAT, \
            f"{path}: expected >= {N_BASE_FEAT} features, got {len(af_names)}"
        nm = [f"af_{n}" for n in af_names]
        assert names is None or nm == names, f"{path}: feature_spec disagrees"
        names = nm
        t = f["pairs"]
        trees.append(t)
        ns.append(t.num_entries)
    n_tot = int(sum(ns))
    log(f"{tag}: {len(files)} chunks, {n_tot} pairs total")

    n_feat = len(names)
    feat_branches = [f"af_{i:02d}" for i in range(n_feat)]
    X = np.empty((n_tot, n_feat), dtype=np.float32)
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
    assert off == n_tot
    return meta, X, names


# ------------------------------------------------------- feature conditioning
# IDENTICAL to train_attach_gen.py (the M16 spec). Not touched: the C++ port bakes this
# into attach_mlp_weights.h and the feature contract is frozen.

CONDITIONING_SPEC = [
    {"feature": "af_ptErrRel", "op": "log10_1p"},
    {"feature": "af_circleCenterDist", "op": "log10_1p"},
    {"feature": "af_log10PtIn", "op": "clip", "lo": -1.0, "hi": 4.0},
    {"feature": "af_log10CircleRadius", "op": "clip", "lo": 1.0, "hi": 5.0},
    {"feature": "af_fitKappaSigned", "op": "clip", "lo": -1.0, "hi": 1.0},
    {"feature": "af_dKappa", "op": "clip", "lo": -1.0, "hi": 1.0},
    # ---- R1 matching block. Same two-op vocabulary as above (clip / log10_1p): the C++
    # preprocess in AttachInference.cc is NOT extended. Every clip is far outside the
    # physical range and exists to stop a sentinel row from collapsing the column into
    # |z| < 1e-3 (the M16 "crippled column" lesson), never to reshape the bulk.
    {"feature": "af_absPlsEta", "op": "clip", "lo": 0.0, "hi": 4.0},
    # The two PULLS and the inwards residual are signed and heavy-tailed (measured on
    # dump/pr_c00: radiusPull spans +-1e6, zResidPull +-3e3). The clip is deliberately
    # NARROW: past ~20 sigma the verdict is already "not the same track", so the only cost
    # is resolution the decision does not use, while a wide clip would put the informative
    # |pull| < 5 region inside |z| < 0.1 after standardization (the M16 crippled-column
    # failure mode in mild form).
    {"feature": "af_radiusPull", "op": "clip", "lo": -20.0, "hi": 20.0},
    {"feature": "af_radiusAsym", "op": "clip", "lo": -1.0, "hi": 1.0},
    {"feature": "af_log10TgtRadius", "op": "clip", "lo": 0.0, "hi": 5.0},
    {"feature": "af_log10SigmaR", "op": "clip", "lo": -3.0, "hi": 4.0},
    {"feature": "af_rphiResidRms", "op": "log10_1p"},
    {"feature": "af_rphiResidRms", "op": "clip", "lo": 0.0, "hi": 3.0},
    {"feature": "af_rphiResidMax", "op": "log10_1p"},
    {"feature": "af_rphiResidMax", "op": "clip", "lo": 0.0, "hi": 3.0},
    {"feature": "af_rphiChi2Lst", "op": "log10_1p"},
    {"feature": "af_rphiChi2Lst", "op": "clip", "lo": 0.0, "hi": 10.0},
    {"feature": "af_rphiResidInwards", "op": "clip", "lo": -30.0, "hi": 30.0},
    {"feature": "af_rzResidRms", "op": "log10_1p"},
    {"feature": "af_rzResidRms", "op": "clip", "lo": 0.0, "hi": 3.0},
    {"feature": "af_rzResidMax", "op": "log10_1p"},
    {"feature": "af_rzResidMax", "op": "clip", "lo": 0.0, "hi": 3.0},
    {"feature": "af_rzChi2Lst", "op": "log10_1p"},
    {"feature": "af_rzChi2Lst", "op": "clip", "lo": 0.0, "hi": 10.0},
    {"feature": "af_zResidPull", "op": "clip", "lo": -20.0, "hi": 20.0},
]


def apply_conditioning(X, names, spec):
    """R1: entries naming an absent feature are skipped (so the spec is valid for a
    19-column and a 33-column dump alike) and the APPLIED subset is what gets recorded."""
    applied = []
    for c in spec:
        if c["feature"] not in names:
            continue
        applied.append(c)
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        elif c["op"] == "log10_1p":
            X[:, j] = np.log10(1.0 + X[:, j])
        else:
            raise ValueError(f"unknown conditioning op {c['op']}")
        log(f"conditioned {c['feature']}: {c['op']}"
            + (f" [{c['lo']:g},{c['hi']:g}]" if c["op"] == "clip" else ""))
    return applied


# ---------------------------------------------------------------- event split

def combined_event_split(meta, src, args, rng):
    """train_chain3.py `combined_event_split`, verbatim in behaviour: the frozen TEST
    split is derived from the PRIMARY source alone (so it is byte-for-byte the test-60
    every previous head was judged on), and the remaining pool -- primary train+val plus
    every non-overlapping salvage event -- is reshuffled into train/val."""
    key = meta["evt"].astype(np.uint64)
    uniq0 = np.unique(key[src == 0])
    rng.shuffle(uniq0)
    n0 = len(uniq0)
    n_tr0 = int(round(args.train_frac * n0))
    n_va0 = int(round(args.val_frac * n0))
    te_keys = uniq0[n_tr0 + n_va0:]
    te = np.isin(key, te_keys)
    assert not (te & (src != 0)).any(), "frozen-test key present in an extra input (leak)"
    with open(FROZEN_TEST60) as fh:
        frozen = sorted(int(v) for v in json.load(fh))
    got = sorted(int(v) for v in te_keys)
    assert got == frozen, ("TEST split is NOT the frozen test-60 "
                           f"({len(got)} keys, {len(set(got) ^ set(frozen))} differ)")
    log("TEST split == frozen test-60 (prototype/m12_test60_evts.json) VERIFIED")
    pool_keys = np.unique(key[~te])
    rng.shuffle(pool_keys)
    n_ptr = int(round(args.pool_train_frac * len(pool_keys)))
    tr_keys, va_keys = pool_keys[:n_ptr], pool_keys[n_ptr:]
    tr = np.isin(key, tr_keys)
    va = np.isin(key, va_keys)
    log(f"COMBINED split: frozen test = {len(te_keys)} primary events; pool "
        f"{len(pool_keys)} events -> train {len(tr_keys)} / val {len(va_keys)} -> "
        f"{tr.sum()}/{va.sum()}/{te.sum()} pairs")
    return tr, va, te


def strata_report(meta, mask, tag):
    is_true = (meta["label"] == 1) & mask
    acc = is_true & (meta["simPt"] > -998.0)
    print(f"  --- true-pair strata ({tag}) ---")
    print(f"  {'stratum':>22} {'chain(tt0)':>12} {'bareT3(tt1)':>12}")
    for name, lo, hi in [("accepted vxy [0,1)", 0.0, 1.0), ("accepted vxy [1,5)", 1.0, 5.0),
                         ("accepted vxy [5,10)", 5.0, 10.0),
                         ("accepted vxy [10,30)", 10.0, 30.0),
                         ("accepted vxy >=30", 30.0, 1e9)]:
        m = acc & (meta["simVxy"] >= lo) & (meta["simVxy"] < hi)
        print(f"  {name:>22} {int((m & (meta['ttype'] == 0)).sum()):>12d} "
              f"{int((m & (meta['ttype'] == 1)).sum()):>12d}")
    m = is_true & ~acc
    print(f"  {'pileup-sim only':>22} {int((m & (meta['ttype'] == 0)).sum()):>12d} "
          f"{int((m & (meta['ttype'] == 1)).sum()):>12d}")
    print(f"  {'ALL true':>22} {int((is_true & (meta['ttype'] == 0)).sum()):>12d} "
          f"{int((is_true & (meta['ttype'] == 1)).sum()):>12d}")


def build_cache(args, rng):
    meta0, X0, names = load_dump(args.input, "PRIMARY")
    prim_evts = np.unique(meta0["evt"])
    log(f"PRIMARY: {len(X0)} pairs over {len(prim_evts)} events")
    parts_meta, parts_X, parts_src = [meta0], [X0], [np.zeros(len(X0), dtype=np.int8)]
    if args.extra:
        meta1, X1, names1 = load_dump(args.extra, "EXTRA")
        assert names1 == names, "extra dump feature_spec disagrees with primary"
        n_ev1 = len(np.unique(meta1["evt"]))
        keep = ~np.isin(meta1["evt"], prim_evts)
        n_new = len(np.unique(meta1["evt"][keep]))
        log(f"EXTRA: {n_ev1} events -> {n_new} NEW events "
            f"({n_ev1 - n_new} already in primary, dropped); "
            f"{int(keep.sum())}/{len(X1)} pairs kept")
        parts_meta.append({k: v[keep] for k, v in meta1.items()})
        parts_X.append(X1[keep])
        parts_src.append(np.ones(int(keep.sum()), dtype=np.int8))
        del meta1, X1
    meta = {k: np.concatenate([m[k] for m in parts_meta]) for k in META_BRANCHES}
    X = np.concatenate(parts_X)
    src = np.concatenate(parts_src)
    del parts_meta, parts_X, parts_src, meta0, X0
    n_all = len(X)
    log(f"COMBINED: {n_all} pairs over {len(np.unique(meta['evt']))} events")

    n_bad = int((~np.isfinite(X)).sum())
    if n_bad:
        log(f"WARNING: {n_bad} non-finite feature values -> 0")
        np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
    strata_report(meta, np.ones(n_all, dtype=bool), "all rows")

    conditioning = apply_conditioning(X, names, CONDITIONING_SPEC)
    tr, va, te = combined_event_split(meta, src, args, rng)
    strata_report(meta, tr, "TRAIN")
    strata_report(meta, te, "TEST-60")

    mu = X[tr].mean(axis=0, dtype=np.float64).astype(np.float32)
    sd = X[tr].std(axis=0, dtype=np.float64).astype(np.float32)
    sd[sd < 1e-8] = 1.0
    X -= mu
    X /= sd

    y = (meta["label"] == 1).astype(np.float32)
    cache = {}
    for nm, m in (("tr", tr), ("va", va), ("te", te)):
        cache[f"X{nm}"] = X[m]
        cache[f"y{nm}"] = y[m]
        cache[f"w{nm}"] = meta["wgt"][m]
        cache[f"t{nm}"] = meta["ttype"][m]
        cache[f"v{nm}"] = meta["simVxy"][m]
        cache[f"p{nm}"] = meta["simPt"][m]
    cache["nl_te"] = meta["chainNLayers"][te]
    cache["mu"] = mu
    cache["sd"] = sd
    cache["meta_json"] = np.bytes_(json.dumps({
        "names": names, "conditioning": conditioning, "n_all": int(n_all),
        "input": args.input, "extra": args.extra, "seed": args.seed}))
    if args.cache:
        np.savez(args.cache, **cache)
        log(f"wrote cache {args.cache} ({os.path.getsize(args.cache) / 1e9:.2f} GB)")
    return cache


def load_cache(path, args):
    z = np.load(path)
    cache = {k: z[k] for k in z.files}
    mj = json.loads(bytes(cache["meta_json"]).decode())
    assert mj["seed"] == args.seed, "cache built with a different seed -- delete to rebuild"
    if mj["input"] != args.input:
        log(f"NOTE: cache was built from input={mj['input']} (requested {args.input})")
    log(f"loaded cache {path}: train/val/test = "
        f"{len(cache['Xtr'])}/{len(cache['Xva'])}/{len(cache['Xte'])} pairs")
    return cache


# ---------------------------------------------------------- R1 column selection

def resolve_cols(spec, names):
    """Returns (indices into the cache's column axis, selected names). 'all' keeps the
    dump order; 'base19' keeps the frozen M16 contract; otherwise the comma list IS the
    order. Also reports whether the selection is deployable without a C++ layout edit."""
    if spec == "all":
        sel = list(names)
    elif spec == "base19":
        sel = list(names[:N_BASE_FEAT])
    else:
        sel = []
        for tok in spec.split(","):
            tok = tok.strip()
            if not tok:
                continue
            nm = tok if tok.startswith("af_") else f"af_{tok}"
            assert nm in names, f"--use-cols: '{nm}' is not in the dump ({names})"
            sel.append(nm)
    idx = [names.index(nm) for nm in sel]
    prefix = sel == CPP_ORDER[:len(sel)]
    log(f"columns: {len(sel)} selected -- "
        + ("PREFIX of the C++ layout (deployable as-is)" if prefix else
           "NOT a C++ prefix: permute PixelAttach.cc slots before deploying"))
    log("  " + ",".join(nm[3:] for nm in sel))
    return idx, sel, prefix


# ---------------------------------------------------------------- model / eval

def build_model(n_in, n_hid, n_out=1):
    import torch.nn as nn
    return nn.Sequential(nn.Linear(n_in, n_hid), nn.ReLU(),
                         nn.Linear(n_hid, n_hid), nn.ReLU(),
                         nn.Linear(n_hid, n_out))


def batched_scores(model, X_t, device, bs=1 << 20):
    import torch
    model.eval()
    n = len(X_t)
    out = np.empty(n, dtype=np.float32)
    with torch.no_grad():
        for i in range(0, n, bs):
            z = model(X_t[i:i + bs].to(device)).float().cpu()
            out[i:i + bs] = np.asarray(z.reshape(-1).tolist(), dtype=np.float32)
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


def precision_frontier(s_true, s_fake, cuts):
    """Per-cut precision / true-recall on the CHAIN universe -- the quantity the -a scan
    trades against. Unweighted counts: every chain-universe row has wgt 1."""
    out = {}
    n_true = len(s_true)
    for c in cuts:
        tp = int((s_true >= c).sum())
        fp = int((s_fake >= c).sum())
        out[f"{c:g}"] = {"cut": c, "tp": tp, "fp": fp,
                         "precision": (tp / (tp + fp)) if (tp + fp) else None,
                         "recall": (tp / n_true) if n_true else None}
    return out


# ---------------------------------------------------------------- main

def main():
    args = parse_args()
    import torch

    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.cuda.manual_seed_all(args.seed)
    rng = np.random.default_rng(args.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log(f"seed={args.seed} device={device} gamma_neg={args.gamma_neg} "
        f"disp=({args.disp_mid},{args.disp_hi}) chain_weight={args.chain_weight} "
        f"val_metric={args.val_metric}")

    if args.cache and os.path.exists(args.cache):
        cache = load_cache(args.cache, args)
    else:
        cache = build_cache(args, rng)
    mj = json.loads(bytes(cache["meta_json"]).decode())
    names, conditioning = mj["names"], mj["conditioning"]

    # R1: the TEST rows' |pLS eta| and nLayers, recovered BEFORE column selection so the
    # per-band working-point table can be reported even for a variant that does not use
    # eta as an input. The cache is standardized and af_absPlsEta's only conditioning is a
    # clip to [0, 4] which is inert on the measured range, so un-standardizing is exact.
    eta_te = None
    if "af_absPlsEta" in names:
        je = names.index("af_absPlsEta")
        eta_te = cache["Xte"][:, je] * float(cache["sd"][je]) + float(cache["mu"][je])

    # R1: column selection. Applied AFTER the cache (which always holds every dumped
    # column, standardized per column), so an ablation costs a reload and not a re-dump.
    col_idx, col_names, col_prefix = resolve_cols(args.use_cols, names)
    n_in = len(col_idx)
    for k in ("Xtr", "Xva", "Xte"):
        cache[k] = np.ascontiguousarray(cache[k][:, col_idx])
    cache["mu"] = cache["mu"][col_idx]
    cache["sd"] = cache["sd"][col_idx]
    conditioning = [c for c in conditioning if c["feature"] in col_names]

    ytr_np, ttr_np, vtr_np = cache["ytr"], cache["ttr"], cache["vtr"]
    # ---- per-row TRAINING weight = dump sampling weight x M19 emphasis multipliers ----
    wtr_np = cache["wtr"].astype(np.float32).copy()
    if args.chain_weight != 1.0:
        wtr_np[ttr_np == 0] *= args.chain_weight
    if args.disp_mid != 1.0 or args.disp_hi != 1.0:
        istrue = ytr_np == 1
        wtr_np[istrue & (vtr_np >= 1.0) & (vtr_np < 5.0)] *= args.disp_mid
        wtr_np[istrue & (vtr_np >= 5.0)] *= args.disp_hi
    W_pos = float((wtr_np * ytr_np).sum())
    W_neg = float((wtr_np * (1.0 - ytr_np)).sum())
    pos_weight = W_neg / max(W_pos, 1.0)
    log(f"train weighted: {W_pos:.4g} true / {W_neg:.4g} fake -> pos_weight={pos_weight:.4f}")
    log(f"  chain rows {int((ttr_np == 0).sum())} (weighted {float(wtr_np[ttr_np == 0].sum()):.4g})"
        f" | T3 rows {int((ttr_np == 1).sum())} (weighted {float(wtr_np[ttr_np == 1].sum()):.4g})")

    Xtr = torch.tensor(np.ascontiguousarray(cache["Xtr"]))
    ytr = torch.tensor(np.ascontiguousarray(ytr_np))
    wtr = torch.tensor(np.ascontiguousarray(wtr_np))
    # ---- per-epoch validation set -------------------------------------------------
    # Restricted to the universe the early-stopping metric is about (chain rows only for
    # --val-metric chain) and then capped by a SEEDED subsample. Scoring + weighted AUC
    # over all 26.2M val rows costs 61 s against ~12 s of training per epoch, and it is
    # only ever used as a stop/keep signal -- the reported model quality comes from the
    # full frozen TEST-60 pass at the end, which is NOT subsampled.
    yva_np, wva_np, tva_np = cache["yva"], cache["wva"], cache["tva"]
    vsel = np.flatnonzero(tva_np == 0) if args.val_metric == "chain" \
        else np.arange(len(yva_np))
    if len(vsel) > args.val_cap:
        # FRESH seeded generator, not `rng`: rng's draw history differs between the run
        # that BUILDS the cache and the runs that LOAD it, and every variant must see the
        # identical val subsample for its early-stopping numbers to be comparable.
        vsel = np.sort(np.random.default_rng(args.seed + 1)
                       .choice(vsel, size=args.val_cap, replace=False))
    yva_np, wva_np = yva_np[vsel], wva_np[vsel]
    Xva = torch.tensor(np.ascontiguousarray(cache["Xva"][vsel]))
    log(f"val metric '{args.val_metric}': {len(vsel)} of {len(cache['yva'])} val rows "
        f"({int((yva_np == 1).sum())} true)")
    if device.type == "cuda":
        Xtr, ytr, wtr = Xtr.to(device), ytr.to(device), wtr.to(device)
        Xva = Xva.to(device)

    model = build_model(n_in, args.hidden).to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    pw = torch.tensor(pos_weight, device=device)
    crit = torch.nn.BCEWithLogitsLoss(pos_weight=pw, reduction="none")
    gen = torch.Generator(device="cpu").manual_seed(args.seed)
    gneg = float(args.gamma_neg)

    start_epoch, best_auc, best_state, best_epoch, bad = 1, -1.0, None, -1, 0
    if args.eval_only:
        blob = torch.load(args.out_model, map_location="cpu", weights_only=False)
        model.load_state_dict(blob["state_dict"])
        model.to(device)
        best_epoch, best_auc = blob.get("best_epoch", -1), blob.get("best_val_auc", -1.0)
    else:
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
                z = model(xb).squeeze(1)
                base = crit(z, yb)
                if gneg > 0.0:
                    # ONE-SIDED focal: negatives only. sigmoid(z) is the model's own
                    # "this is a true pair" score, so the factor is ~1 exactly where a
                    # tighter -a has to make its cut and ~0 deep in the easy-fake bulk.
                    # detach(): this is a gradient-BUDGET reweighting, not an extra
                    # objective term -- no gradient flows through the modulation itself.
                    mod = torch.sigmoid(z.detach()).pow(gneg)
                    fw = torch.where(yb > 0.5, torch.ones_like(mod), mod)
                    l = (base * wb * fw).sum() / (wb * fw).sum().clamp_min(1e-12)
                else:
                    l = (base * wb).sum() / wb.sum()
                l.backward()
                opt.step()
                tot_loss += float(l.detach()) * float(wb.sum())
                tot_w += float(wb.sum())
            s_va = batched_scores(model, Xva, device)
            m = yva_np == 1
            auc = wauc(s_va[m], wva_np[m], s_va[~m], wva_np[~m])
            log(f"epoch {epoch:3d} train_loss={tot_loss / tot_w:.5f} val_auc={auc:.6f}")
            if auc > best_auc:
                best_auc, best_epoch, bad = auc, epoch, 0
                best_state = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
            else:
                bad += 1
            if bad >= args.patience:
                log(f"early stop at epoch {epoch} (best val AUC {best_auc:.6f} @ {best_epoch})")
                break
        log(f"best checkpoint: epoch {best_epoch} val_auc={best_auc:.6f}")
        assert best_state is not None
        model.load_state_dict(best_state)
        model.to(device)

        torch.save({"state_dict": {k: v.cpu() for k, v in model.state_dict().items()},
                    "arch": [n_in, args.hidden, args.hidden, 1],
                    "feature_names": col_names, "seed": args.seed,
                    "conditioning": conditioning, "per_type": False,
                    "best_epoch": best_epoch, "best_val_auc": float(best_auc)},
                   args.out_model)
        with open(args.out_norm, "w") as fh:
            json.dump({"feature_names": col_names, "conditioning": conditioning,
                       "r1_use_cols": args.use_cols, "r1_cpp_prefix": bool(col_prefix),
                       "mean": cache["mu"].tolist(), "std": cache["sd"].tolist(),
                       "seed": args.seed, "degenerate_columns": [], "per_type": False,
                       "sampling_weights": {
                           "rule": "loss and AUC weighted by the dump 'wgt' branch "
                                   "(bare-T3 FAKES downsampled 1-in-64) x the M19 "
                                   "emphasis multipliers below",
                           "train_W_true": W_pos, "train_W_fake": W_neg,
                           "pos_weight": pos_weight},
                       "m19_objective": {
                           "gamma_neg": args.gamma_neg,
                           "disp_mid": args.disp_mid, "disp_hi": args.disp_hi,
                           "chain_weight": args.chain_weight,
                           "val_metric": args.val_metric, "val_cap": args.val_cap},
                       "train_args": {"epochs": args.epochs, "patience": args.patience,
                                      "batch_size": args.batch_size, "lr": args.lr,
                                      "hidden": args.hidden}}, fh, indent=1)
        log(f"saved {args.out_model} and {args.out_norm}")

    # ------------------------------------------------ TEST report (frozen test-60)
    Xte = torch.tensor(np.ascontiguousarray(cache["Xte"]))
    tte = cache["tte"]
    s_te = batched_scores(model, Xte, device)
    yte, wte = cache["yte"], cache["wte"]
    vxy, spt, nl = cache["vte"], cache["pte"], cache["nl_te"]
    is_true = yte == 1
    is_fake = ~is_true
    acc = is_true & (spt > -998.0)

    print("\n=== M19 attach head on the FROZEN TEST-60 events "
          "(all AUCs population-weighted by wgt) ===")
    res = {}
    eval_block("ALL (both target types)", s_te[is_true], wte[is_true],
               s_te[is_fake], wte[is_fake], res)
    for tt, nm in ((0, "CHAIN targets (ttype 0)"), (1, "BARE-T3 targets (ttype 1)")):
        mt = tte == tt
        eval_block(nm, s_te[is_true & mt], wte[is_true & mt],
                   s_te[is_fake & mt], wte[is_fake & mt], res)
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

    # ---- the M19 deliverable: CHAIN-universe precision/recall frontier vs the -a cut ----
    ch = tte == 0
    cuts = [0.0, 2.0, 4.0, 5.0, 6.0, 7.0, 7.5, 8.0, 8.5, 9.0, 10.0, 11.0, 13.0]
    front = precision_frontier(s_te[is_true & ch], s_te[is_fake & ch], cuts)
    print("\n  CHAIN-universe frontier on TEST-60 (unweighted; wgt==1 in this universe):")
    print(f"    {'cut':>6} {'tp':>7} {'fp':>9} {'precision':>10} {'recall':>8}")
    for k in front:
        r = front[k]
        p = "n/a" if r["precision"] is None else f"{r['precision']:.4f}"
        rc = "n/a" if r["recall"] is None else f"{r['recall']:.4f}"
        print(f"    {r['cut']:>6.1f} {r['tp']:>7d} {r['fp']:>9d} {p:>10} {rc:>8}")
    res["chain_frontier"] = front
    # ---- PER-ETA-BAND frontier: the input to a WP table -----------------------------
    # LST's practice is one working point per eta bin chosen for ~uniform signal
    # efficiency (interface/alpaka/Common.h kWp_pT3[10], 0.25-wide bins). Our -a is banded
    # at 1.1 / 1.7 only, so this table answers directly whether one bar has one meaning.
    if eta_te is not None:
        bands = [("|eta|<1.1", 0.0, 1.1), ("1.1-1.7", 1.1, 1.7), (">=1.7", 1.7, 9.9)]
        fine = [(f"{lo:.2f}-{lo + 0.25:.2f}", lo, lo + 0.25) for lo in np.arange(0, 2.5, 0.25)]
        for label, blist in (("BAND (the -a/-a2/-a3 bands)", bands),
                             ("FINE (LST's 0.25 bins)", fine)):
            print(f"\n  CHAIN-universe recall/precision at cut 6.0 by |pLS eta| -- {label}:")
            print(f"    {'band':>12} {'n_true':>8} {'n_fake':>9} {'recall':>8} {'prec':>8}"
                  f" {'cut@rec.306':>12}")
            for nm, lo, hi in blist:
                mb = ch & (eta_te >= lo) & (eta_te < hi)
                st, sf = s_te[is_true & mb], s_te[is_fake & mb]
                if len(st) == 0:
                    continue
                tp = int((st >= 6.0).sum())
                fp = int((sf >= 6.0).sum())
                rc = tp / len(st)
                pr = tp / (tp + fp) if (tp + fp) else float("nan")
                # the cut this band would need for the GLOBAL recall of 0.3062 (r2 @ 6.0)
                cq = float(np.quantile(st, 1.0 - 0.3062))
                print(f"    {nm:>12} {len(st):>8d} {len(sf):>9d} {rc:>8.4f} {pr:>8.4f}"
                      f" {cq:>12.3f}")
                res[f"etaband_{label.split()[0]}_{nm}"] = {
                    "n_true": len(st), "n_fake": len(sf), "recall@6": rc,
                    "precision@6": pr, "cut_at_recall_0.3062": cq}
    # displaced-pair recall on the chain universe at each cut
    dm = acc & ch & (vxy >= 1.0)
    res["chain_displaced_true_n"] = int(dm.sum())
    res["chain_displaced_recall"] = {
        f"{c:g}": (float((s_te[dm] >= c).mean()) if int(dm.sum()) else None) for c in cuts}
    # same for the T3 universe (its threshold -AT3 lives at 6.0 in the FLAGSHIP)
    t3 = tte == 1
    res["t3_frontier"] = precision_frontier(s_te[is_true & t3], s_te[is_fake & t3],
                                            [4.0, 5.0, 6.0, 7.0, 8.0])

    print("\n  logit quantiles 5/25/50/75/95% (unweighted rows):")
    for tt in (0, 1):
        for tag, m in ((f"tt{tt} true", is_true & (tte == tt)),
                       (f"tt{tt} fake", is_fake & (tte == tt))):
            q = np.quantile(s_te[m], [0.05, 0.25, 0.5, 0.75, 0.95])
            print(f"    {tag:>12}: " + " ".join(f"{v:+.2f}" for v in q))

    if args.out_testauc:
        with open(args.out_testauc, "w") as fh:
            json.dump({"model": args.out_model, "best_epoch": best_epoch,
                       "best_val_auc": float(best_auc),
                       "objective": {"gamma_neg": args.gamma_neg,
                                     "disp_mid": args.disp_mid, "disp_hi": args.disp_hi,
                                     "chain_weight": args.chain_weight,
                                     "val_metric": args.val_metric},
                       "test": res}, fh, indent=1)
        log(f"wrote {args.out_testauc}")
    log("TRAINING COMPLETE")


if __name__ == "__main__":
    main()
