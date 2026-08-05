#!/usr/bin/env python3
"""ANGLE B5 (gate capacity + co-retrain): one driver, one data load, ONE split, several
gate variants + a HistGBDT ceiling probe, all evaluated on the IDENTICAL frozen test set
with the IDENTICAL branch definitions used by train_chain3.py (so the AUC ladder is
directly comparable to the M12 numbers).

Variants (see VARIANTS below):
  v0_ctrl   h32, M12 feature set (24 cf minus maxBridgeChi2, + dcaXY)  -> must reproduce
            mP .9662 / mD .9406
  v1_wide   h48, same features                    (capacity axis alone)
  v2_widef  h48, +maxBridgeChi2 +4 derived        (capacity + representation)
  v3_w64f   h64, same features as v2              (capacity saturation probe)
  v4_dw     h48, v2 features, displaced weights x16/x32 (weighting axis)
  gbdt      HistGradientBoostingClassifier on the v2 feature set (ceiling probe)

Reuses train_chain3.py's audited load/conditioning/split code by import.
"""
import argparse
import copy
import json
import os
import sys
import time

import numpy as np

T0 = time.time()
PROTO = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, PROTO)
import train_chain3 as tc3  # noqa: E402

SHARED = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype"


def log(msg):
    print(f"[{time.time() - T0:8.1f}s] {msg}", flush=True)


# ---------------------------------------------------------------------------------
# Derived features (the "+4"): the ONLY raw dump column not already consumed by the M12
# gate is maxBridgeChi2, so the representation axis is extended with four NONLINEAR
# combinations of existing columns (an MLP represents differences for free, so only
# ratios / products are worth adding). All four are 1-2 flops in C++.
DERIVED = [
    # name, needs (cf names), fn(raw dict) -> raw value, conditioning ops
    ("d_invPt", ["ptEst"], lambda c: 1.0 / np.maximum(np.abs(c["ptEst"]), 0.1),
     [{"op": "clip", "lo": 0.0, "hi": 10.0}]),
    ("d_nodesPerLayer", ["nNodes", "nLayers"],
     lambda c: c["nNodes"] / np.maximum(c["nLayers"], 1.0), []),
    ("d_logChi2Ratio", ["fullFitChi2PerHit", "rzLineChi2PerHit"],
     lambda c: np.log10((np.maximum(c["fullFitChi2PerHit"], 0.0) + 0.01)
                        / (np.maximum(c["rzLineChi2PerHit"], 0.0) + 0.01)),
     [{"op": "clip", "lo": -4.0, "hi": 4.0}]),
    ("d_dcaOverR", ["dcaXY", "fitKappa"],
     lambda c: np.abs(c["dcaXY"]) * np.abs(c["fitKappa"]),
     [{"op": "log10_1p"}, {"op": "clip", "lo": 0.0, "hi": 1.0}]),
]


def load_all(paths, drop):
    """Load the dumps with tc3.load_dump, then append the derived columns (raw)."""
    metas, xs, srcs = [], [], []
    names = prim = None
    raw_needed = sorted({n for _, needs, _, _ in DERIVED for n in needs})
    for si, path in enumerate(paths):
        meta_i, X_i, names_i = tc3.load_dump(path, drop)
        # raw columns for the derived features, read straight from the same file
        import uproot
        f = uproot.open(path)
        cf_names = f["feature_spec"].member("fTitle")[3:].split(",")
        raw = {}
        arr = f["chains"].arrays([f"cf_{cf_names.index(n):02d}" for n in raw_needed
                                  if n != "dcaXY"], library="np")
        for n in raw_needed:
            raw[n] = (meta_i["dcaXY"] if n == "dcaXY"
                      else arr[f"cf_{cf_names.index(n):02d}"].astype(np.float32))
        del arr
        D = np.empty((len(X_i), len(DERIVED)), dtype=np.float32)
        for j, (_, _, fn, _) in enumerate(DERIVED):
            D[:, j] = fn(raw).astype(np.float32)
        X_i = np.concatenate([X_i, D], axis=1)
        dnames = names_i + [d[0] for d in DERIVED]
        if si == 0:
            names, prim = dnames, np.unique(meta_i["evt"])
            log(f"input[0] PRIMARY {os.path.basename(path)}: {len(X_i)} chains / "
                f"{len(prim)} events")
        else:
            assert dnames == names
            keep = ~np.isin(meta_i["evt"], prim)
            log(f"input[{si}] EXTRA {os.path.basename(path)}: keep {int(keep.sum())}/{len(X_i)}")
            meta_i = {k: v[keep] for k, v in meta_i.items()}
            X_i = X_i[keep]
        metas.append(meta_i)
        xs.append(X_i)
        srcs.append(np.full(len(X_i), si, dtype=np.int8))
    meta = {k: np.concatenate([m[k] for m in metas]) for k in tc3.META_BRANCHES}
    X = np.concatenate(xs)
    src = np.concatenate(srcs)
    return meta, X, src, names


def eval_ladder(mP, mD, mX, y3, nl, dca, vxy):
    f_m, p_m, d_m = y3 == 0, y3 == 1, y3 == 2
    res = {}

    def blk(tag, s_pos, s_neg):
        a = tc3.auc_of(s_pos, s_neg)
        res[tag] = {"auc": a, "n_pos": int(len(s_pos)), "n_neg": int(len(s_neg))}
        return a

    blk("prompt-vs-fake (mP)", mP[p_m], mP[f_m])
    blk("displaced-vs-fake (mD)", mD[d_m], mD[f_m])
    blk("alltrue-vs-fake (mX)", mX[p_m | d_m], mX[f_m])
    blk("disp>=5-vs-fake (mD)", mD[d_m & (vxy >= 5)], mD[f_m])
    for tag, m in (("nL=4", nl == 4), ("nL=5", nl == 5), ("nL>=6", nl >= 6)):
        blk(f"{tag} prompt", mP[p_m & m], mP[f_m & m])
        blk(f"{tag} disp", mD[d_m & m], mD[f_m & m])
        blk(f"{tag} all", mX[(p_m | d_m) & m], mX[f_m & m])
    ip = (dca < 0.5) & (nl >= 5)
    ex = (dca >= 0.5) & (nl >= 5)
    blk("IP 5+ prompt", mP[p_m & ip], mP[f_m & ip])
    blk("IP 5+ all", mX[(p_m | d_m) & ip], mX[f_m & ip])
    blk("exempt 5+ disp", mD[d_m & ex], mD[f_m & ex])
    blk("exempt 5+ all", mX[(p_m | d_m) & ex], mX[f_m & ex])
    blk("exempt 5+ prompt", mP[p_m & ex], mP[f_m & ex])
    # extra: the M12 exempt-5+ kill actually runs on mX, so track its displaced arm too
    blk("exempt 5+ disp (mX)", mX[d_m & ex], mX[f_m & ex])
    blk("nL=4 disp (mX)", mX[d_m & (nl == 4)], mX[f_m & (nl == 4)])
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variants", nargs="*", default=["v0_ctrl", "v1_wide", "v2_widef",
                                                      "v3_w64f", "v4_dw"])
    ap.add_argument("--epochs", type=int, default=140)
    ap.add_argument("--patience", type=int, default=15)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--out", default=f"{PROTO}/b5_ladder.json")
    ap.add_argument("--gbdt", action="store_true")
    ap.add_argument("--gbdt-iter", type=int, default=200)
    args = ap.parse_args()

    import torch
    np.random.seed(args.seed)
    rng = np.random.default_rng(args.seed)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log(f"device={dev}")

    inputs = [f"{SHARED}/chains_m12_300evt.root", f"{SHARED}/chains_m12_498evt.root"]
    # NOTE: drop set EMPTY here -> maxBridgeChi2 is loaded; the M12 feature set is
    # produced by column selection below, so both feature sets share one load.
    meta, X, src, names = load_all(inputs, set())
    log(f"loaded {len(X)} chains x {X.shape[1]} columns")
    log("columns: " + ", ".join(f"{j}:{n}" for j, n in enumerate(names)))
    np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)

    dca_raw = meta["dcaXY"].copy()
    cond = list(tc3.CONDITIONING_SPEC)
    for nm, _, _, ops in DERIVED:
        for o in ops:
            cond.append(dict(o, feature=nm))
    cond = tc3.apply_conditioning(X, names, cond)

    # ONE split, shared by every variant (M8 combination rule, frozen test-60).
    class A:
        train_frac, val_frac, pool_train_frac = 0.6, 0.2, 0.75
    tr, va, te = tc3.combined_event_split(meta, src, A, rng)

    is_true = meta["label"] == 1
    vxy = meta["simVxy"]
    y3 = np.zeros(len(X), dtype=np.int64)
    y3[is_true & (vxy < 1.0)] = 1
    y3[is_true & (vxy >= 1.0)] = 2
    n0 = int((y3[tr] == 0).sum())
    n12 = int((y3[tr] > 0).sum())
    pos_weight = n0 / max(n12, 1)
    log(f"train classes fake={n0} prompt={int((y3[tr]==1).sum())} disp={int((y3[tr]==2).sum())}"
        f" pos_weight={pos_weight:.4f}")

    def weights(mid, hi):
        w = np.ones(len(X), dtype=np.float32)
        w[is_true] = pos_weight
        w[is_true & (vxy >= 1.0) & (vxy < 5.0)] = pos_weight * mid
        w[is_true & (vxy >= 5.0)] = pos_weight * hi
        return w

    m12_cols = [j for j, n in enumerate(names)
                if n != "cf_maxBridgeChi2" and not n.startswith("d_")]
    all_cols = list(range(len(names)))
    log(f"m12 feature set = {len(m12_cols)} cols; extended = {len(all_cols)} cols")

    VARIANTS = {
        "v0_ctrl": dict(cols=m12_cols, hidden=32, mid=8.0, hi=16.0),
        "v1_wide": dict(cols=m12_cols, hidden=48, mid=8.0, hi=16.0),
        "v2_widef": dict(cols=all_cols, hidden=48, mid=8.0, hi=16.0),
        "v3_w64f": dict(cols=all_cols, hidden=64, mid=8.0, hi=16.0),
        "v4_dw": dict(cols=all_cols, hidden=48, mid=16.0, hi=32.0),
        # noise floor: identical to v0_ctrl except the init/shuffle seed (SAME split),
        # so |v0s2 - v0_ctrl| measures the run-to-run AUC scatter the ladder is read
        # against.
        "v0s2": dict(cols=m12_cols, hidden=32, mid=8.0, hi=16.0, iseed=1234),
        "v0s3": dict(cols=m12_cols, hidden=32, mid=8.0, hi=16.0, iseed=7),
        "v2s2": dict(cols=all_cols, hidden=48, mid=8.0, hi=16.0, iseed=1234),
        # capacity taken far past anything portable, to locate the MLP-vs-GBDT gap
        "v5_h128": dict(cols=all_cols, hidden=128, mid=8.0, hi=16.0),
        "v6_h256": dict(cols=all_cols, hidden=256, mid=8.0, hi=16.0),
    }

    out = {}
    if os.path.exists(args.out):
        out = json.load(open(args.out))

    nl_te, dca_te, vxy_te, y3_te = (meta["nLayers"][te], dca_raw[te], vxy[te], y3[te])

    for vname in args.variants:
        cfg = VARIANTS[vname]
        cols = cfg["cols"]
        vnames = [names[j] for j in cols]
        Xs_tr_mu = X[np.ix_(tr, cols)].mean(axis=0, dtype=np.float64).astype(np.float32)
        Xs_tr_sd = X[np.ix_(tr, cols)].std(axis=0, dtype=np.float64).astype(np.float32)
        Xs_tr_sd[Xs_tr_sd < 1e-8] = 1.0
        Xc = (X[:, cols] - Xs_tr_mu) / Xs_tr_sd
        w_all = weights(cfg["mid"], cfg["hi"])

        iseed = cfg.get("iseed", args.seed)
        torch.manual_seed(iseed)
        torch.cuda.manual_seed_all(iseed)
        Xtr = torch.tensor(np.ascontiguousarray(Xc[tr])).to(dev)
        ytr = torch.tensor(np.ascontiguousarray(y3[tr])).to(dev)
        wtr = torch.tensor(np.ascontiguousarray(w_all[tr])).to(dev)
        Xva = torch.tensor(np.ascontiguousarray(Xc[va]))
        y3va = y3[va]
        va_f, va_p, va_d = y3va == 0, y3va == 1, y3va == 2

        model = tc3.build_model(len(cols), cfg["hidden"]).to(dev)
        opt = torch.optim.Adam(model.parameters(), lr=1e-3)
        crit = torch.nn.CrossEntropyLoss(reduction="none")
        gen = torch.Generator(device="cpu").manual_seed(iseed)
        best_sel, best_state, best_epoch, bad = -1.0, None, -1, 0
        n_tr = len(Xtr)
        t_start = time.time()
        for ep in range(1, args.epochs + 1):
            model.train()
            perm = torch.randperm(n_tr, generator=gen).to(dev)
            for i in range(0, n_tr, 16384):
                idx = perm[i:i + 16384]
                opt.zero_grad()
                loss = (crit(model(Xtr[idx]), ytr[idx]) * wtr[idx]).mean()
                loss.backward()
                opt.step()
            z = tc3.batched_logits(model, Xva, dev)
            ap_ = tc3.auc_of(z[:, 1][va_p] - z[:, 0][va_p], z[:, 1][va_f] - z[:, 0][va_f])
            ad_ = tc3.auc_of(z[:, 2][va_d] - z[:, 0][va_d], z[:, 2][va_f] - z[:, 0][va_f])
            sel = min(ap_, ad_)
            if sel > best_sel:
                best_sel, best_epoch, bad = sel, ep, 0
                best_state = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
            else:
                bad += 1
            if ep % 10 == 0 or ep == 1:
                log(f"  {vname} ep{ep:3d} P={ap_:.5f} D={ad_:.5f} sel={sel:.5f} "
                    f"(best {best_sel:.5f}@{best_epoch})")
            if bad >= args.patience:
                log(f"  {vname} early stop ep{ep} best {best_sel:.5f}@{best_epoch}")
                break
        model.load_state_dict(best_state)
        model.to(dev)
        z = tc3.batched_logits(model, torch.tensor(np.ascontiguousarray(Xc[te])), dev)
        mP, mD = z[:, 1] - z[:, 0], z[:, 2] - z[:, 0]
        mX = np.maximum(z[:, 1], z[:, 2]) - z[:, 0]
        res = eval_ladder(mP, mD, mX, y3_te, nl_te, dca_te, vxy_te)
        out[vname] = {"cfg": {k: v for k, v in cfg.items() if k != "cols"},
                      "n_inputs": len(cols), "features": vnames,
                      "best_epoch": best_epoch, "best_sel": float(best_sel),
                      "train_sec": round(time.time() - t_start, 1), "auc": res}
        log(f"{vname}: inputs={len(cols)} h={cfg['hidden']} best_ep={best_epoch} "
            f"mP={res['prompt-vs-fake (mP)']['auc']:.5f} "
            f"mD={res['displaced-vs-fake (mD)']['auc']:.5f} "
            f"ex5+D={res['exempt 5+ disp']['auc']:.5f} "
            f"nL4D={res['nL=4 disp']['auc']:.5f}")
        torch.save({"state_dict": best_state,
                    "arch": [len(cols), cfg["hidden"], cfg["hidden"], 3],
                    "feature_names": vnames, "seed": args.seed,
                    "conditioning": [c for c in cond if c["feature"] in vnames],
                    "best_epoch": best_epoch, "best_sel": float(best_sel)},
                   f"{PROTO}/b5_{vname}.pt")
        with open(f"{PROTO}/b5_{vname}_norm.json", "w") as fh:
            json.dump({"feature_names": vnames,
                       "conditioning": [c for c in cond if c["feature"] in vnames],
                       "mean": Xs_tr_mu.tolist(), "std": Xs_tr_sd.tolist(),
                       "seed": args.seed}, fh, indent=1)
        json.dump(out, open(args.out, "w"), indent=1)
        del Xtr, ytr, wtr, Xva, Xc
        torch.cuda.empty_cache()

    if args.gbdt:
        from sklearn.ensemble import HistGradientBoostingClassifier
        cols = all_cols
        w_all = weights(8.0, 16.0)
        t_start = time.time()
        clf = HistGradientBoostingClassifier(
            max_iter=args.gbdt_iter, learning_rate=0.1, max_leaf_nodes=63,
            min_samples_leaf=50, l2_regularization=1.0, max_bins=128,
            early_stopping=False, random_state=args.seed, verbose=1)
        log(f"GBDT fit on {int(tr.sum())} rows x {len(cols)} cols ...")
        clf.fit(X[np.ix_(tr, cols)], y3[tr], sample_weight=w_all[tr])
        log(f"GBDT fit done in {time.time()-t_start:.0f}s; scoring test")
        lp = clf.predict_proba(X[np.ix_(te, cols)])
        eps = 1e-12
        lz = np.log(np.clip(lp, eps, None))
        mP, mD = lz[:, 1] - lz[:, 0], lz[:, 2] - lz[:, 0]
        mX = np.maximum(lz[:, 1], lz[:, 2]) - lz[:, 0]
        res = eval_ladder(mP, mD, mX, y3_te, nl_te, dca_te, vxy_te)
        out["gbdt"] = {"cfg": {"max_iter": args.gbdt_iter, "leaves": 63},
                       "n_inputs": len(cols), "features": names,
                       "train_sec": round(time.time() - t_start, 1), "auc": res}
        log(f"gbdt: mP={res['prompt-vs-fake (mP)']['auc']:.5f} "
            f"mD={res['displaced-vs-fake (mD)']['auc']:.5f} "
            f"ex5+D={res['exempt 5+ disp']['auc']:.5f} "
            f"nL4D={res['nL=4 disp']['auc']:.5f}")
        json.dump(out, open(args.out, "w"), indent=1)

    log(f"wrote {args.out}")


if __name__ == "__main__":
    main()
