#!/usr/bin/env python3
"""Score the RESIDENT 3-class chain gate (chain3_mlp_m12.pt, the head behind the canonical
prototype/chain3_mlp_weights.h) on the featgate dump's frozen test-60 rows.

Reuses train_chain3_fg.load_dump / combined_event_split / event_split verbatim, with the
resident's own norm json (feature order, conditioning, mean/std), so the resident's test
rows are the SAME rows the (a)/(b)/(c) retrains report on:
  * the featgate dump re-runs the identical chaindump code path over the identical 300 (and
    498) events, so the evt key sets match the m12 dumps exactly;
  * the split is a pure function of (seed, evt key set, fracs), so seed 42 + the same
    --input order reproduces the frozen test-60 event list bit for bit.

Writes the same testrows npz layout train_chain3_fg.py writes, so fg_table.py can build the
resident column of the AUC table with identical code.
"""

import argparse
import json
import os

import numpy as np

import train_chain3_fg as T


def main():
    d = os.path.dirname(os.path.abspath(__file__))
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", default=f"{d}/chain3_mlp_m12.pt")
    p.add_argument("--norm", default=f"{d}/chain3_norm_m12.json")
    p.add_argument("--input", nargs="+",
                   default=[f"{d}/chains_fg_300evt.root", f"{d}/chains_fg_498evt.root"])
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--train-frac", type=float, default=0.6)
    p.add_argument("--val-frac", type=float, default=0.2)
    p.add_argument("--pool-train-frac", type=float, default=0.75)
    p.add_argument("--drop-features", nargs="*", default=["maxBridgeChi2"])
    p.add_argument("--band-lo", type=float, default=1.1)
    p.add_argument("--band-hi", type=float, default=1.7)
    p.add_argument("--out-testrows", default=f"{d}/testrows_resident.npz")
    args = p.parse_args()

    import torch

    with open(args.norm) as fh:
        norm = json.load(fh)
    want = norm["feature_names"]
    rng = np.random.default_rng(args.seed)

    metas, x_parts, src_parts = [], [], []
    names = prim = None
    for si, path in enumerate(args.input):
        meta_i, X_i, names_i = T.load_dump(path, set(args.drop_features))
        if si == 0:
            names = names_i
            prim = np.unique(meta_i["evt"])
            print(f"input[0] PRIMARY {path}: {len(X_i)} chains / {len(prim)} events")
        else:
            assert names_i == names
            keep = ~np.isin(meta_i["evt"], prim)
            print(f"input[{si}] EXTRA {path}: keep {int(keep.sum())}/{len(X_i)}")
            meta_i = {k: v[keep] for k, v in meta_i.items()}
            X_i = X_i[keep]
        metas.append(meta_i)
        x_parts.append(X_i)
        src_parts.append(np.full(len(X_i), si, dtype=np.int8))
    keys = T.META_BRANCHES + T.OPT_BRANCHES + T.FG_BRANCHES
    if len(args.input) == 1:
        meta, X, src = metas[0], x_parts[0], None
    else:
        meta = {k: np.concatenate([m[k] for m in metas]) for k in keys}
        X = np.concatenate(x_parts)
        src = np.concatenate(src_parts)
    del metas, x_parts, src_parts

    assert names == want, f"feature order mismatch\n dump: {names}\n norm: {want}"
    np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)

    # Resident conditioning, exactly as recorded in its norm json.
    for c in norm["conditioning"]:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        elif c["op"] == "log10_1p":
            X[:, j] = np.log10(1.0 + X[:, j])
        else:
            raise ValueError(c["op"])
    mu = np.asarray(norm["mean"], dtype=np.float32)
    sd = np.asarray(norm["std"], dtype=np.float32)
    Xs = (X - mu) / sd

    if src is None:
        tr, va, te = T.event_split(meta, args.train_frac, args.val_frac, rng)
    else:
        class A:
            pass
        a = A()
        a.train_frac, a.val_frac, a.pool_train_frac = args.train_frac, args.val_frac, args.pool_train_frac
        tr, va, te = T.combined_event_split(meta, src, a, rng)
    print(f"frozen test rows: {int(te.sum())} over {len(np.unique(meta['evt'][te]))} events")

    ck = torch.load(args.model, map_location="cpu", weights_only=False)
    arch = ck["arch"]
    assert arch[0] == len(want), f"model expects {arch[0]} inputs, norm has {len(want)}"
    model = T.build_model(arch[0], arch[1])
    model.load_state_dict(ck["state_dict"])
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model.to(device).eval()
    print(f"resident {os.path.basename(args.model)} arch={arch} best_epoch={ck.get('best_epoch')}")

    Xte = torch.tensor(np.ascontiguousarray(Xs[te]))
    z = T.batched_logits(model, Xte, device)
    mP = z[:, 1] - z[:, 0]
    mD = z[:, 2] - z[:, 0]
    mX = np.maximum(z[:, 1], z[:, 2]) - z[:, 0]

    is_true = meta["label"] == 1
    vxy = meta["simVxy"]
    y3 = np.zeros(len(X), dtype=np.int64)
    y3[is_true & (vxy < 1.0)] = 1
    y3[is_true & (vxy >= 1.0)] = 2
    y3te = y3[te]
    f_m, p_m, d_m = y3te == 0, y3te == 1, y3te == 2

    print("\n=== RESIDENT gate on the featgate frozen test-60 ===")
    res = {}
    res["prompt-vs-fake (mP)"] = T.eval_block("prompt-vs-fake (mP)", mP[p_m], mP[f_m])
    res["displaced-vs-fake (mD)"] = T.eval_block("displaced-vs-fake (mD)", mD[d_m], mD[f_m])
    res["alltrue-vs-fake (mX)"] = T.eval_block("alltrue-vs-fake (mX)", mX[p_m | d_m], mX[f_m])
    aeta = np.abs(meta["chainEta"][te])
    band = (aeta >= args.band_lo) & (aeta < args.band_hi)
    outb = (aeta < args.band_lo) | ((aeta >= args.band_hi) & (aeta < 100.0))
    print(f"  --- BAND |eta| [{args.band_lo},{args.band_hi}) n={int(band.sum())} ---")
    res["band prompt"] = T.eval_block("band prompt (mP)", mP[p_m & band], mP[f_m & band])
    res["band disp"] = T.eval_block("band disp (mD)", mD[d_m & band], mD[f_m & band])
    res["band all"] = T.eval_block("band alltrue (mX)", mX[(p_m | d_m) & band], mX[f_m & band])
    print(f"  --- OUTSIDE BAND n={int(outb.sum())} ---")
    res["outband prompt"] = T.eval_block("outband prompt (mP)", mP[p_m & outb], mP[f_m & outb])
    res["outband disp"] = T.eval_block("outband disp (mD)", mD[d_m & outb], mD[f_m & outb])
    res["outband all"] = T.eval_block("outband alltrue (mX)", mX[(p_m | d_m) & outb], mX[f_m & outb])

    np.savez_compressed(args.out_testrows, mP=mP, mD=mD, mX=mX, y3=y3te,
                        chainEta=meta["chainEta"][te], barrelMDFrac=meta["barrelMDFrac"][te],
                        chainTanLambda=meta["chainTanLambda"][te], nLayers=meta["nLayers"][te],
                        dcaXY=meta["dcaXY"][te], simVxy=meta["simVxy"][te], evt=meta["evt"][te])
    with open(os.path.splitext(args.out_testrows)[0] + "_auc.json", "w") as fh:
        json.dump(res, fh, indent=1)
    print(f"wrote {args.out_testrows}")


if __name__ == "__main__":
    main()
