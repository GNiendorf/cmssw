#!/usr/bin/env python3
"""ANGLE 7 step 1: build a cached, standardized, split-ordered edge matrix.

Reproduces train_edge.py's v3 pipeline EXACTLY (same seed, same combination rule,
same conditioning, same train-row standardization) and writes the result to a flat
float32 memmap ordered [train | val | test] so the training chunks (10-minute
foreground budget each) can start in seconds instead of re-parsing 70M rows.

Verification: the computed mean/std are compared against edge_norm_v3.json; any
mismatch means the split moved and the whole comparison would be invalid.
"""
import json
import os
import sys
import time

import numpy as np

D = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, D)
import train_edge as TE  # noqa: E402

PRIMARY = f"{D}/edges_300evt.root"
EXTRA = f"{D}/edges_498evt.root"
SEED = 42


class Args:
    train_frac = 0.6
    val_frac = 0.2
    pool_train_frac = 0.75


def main():
    t0 = time.time()
    rng = np.random.default_rng(SEED)
    metas, xs, srcs = [], [], []
    names = None
    prim_evts = None
    for si, path in enumerate([PRIMARY, EXTRA]):
        meta_i, X_i, names_i, edge_names_i, n_node_i = TE.load_dump(path)
        if si == 0:
            names = names_i
            prim_evts = np.unique(meta_i["evt"])
        else:
            assert names_i == names
            keep = ~np.isin(meta_i["evt"], prim_evts)
            meta_i = {k: v[keep] for k, v in meta_i.items()}
            X_i = X_i[keep]
        metas.append(meta_i)
        xs.append(X_i)
        srcs.append(np.full(len(X_i), si, dtype=np.int8))
        print(f"[{time.time()-t0:7.1f}s] loaded {path}: {len(X_i)} rows", flush=True)

    meta = {k: np.concatenate([m[k] for m in metas]) for k in TE.META_BRANCHES}
    X = np.concatenate(xs)
    src = np.concatenate(srcs)
    del metas, xs, srcs
    print(f"[{time.time()-t0:7.1f}s] total {len(X)} rows", flush=True)

    np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
    TE.apply_conditioning(X, names, TE.CONDITIONING_SPEC)

    tr, va, te = TE.combined_event_split(meta, src, Args, rng)

    mu = X[tr].mean(axis=0, dtype=np.float64).astype(np.float32)
    sd = X[tr].std(axis=0, dtype=np.float64).astype(np.float32)
    sd[sd < 1e-8] = 1.0

    ref = json.load(open(f"{D}/edge_norm_v3.json"))
    dmu = np.abs(np.array(ref["mean"], dtype=np.float32) - mu).max()
    dsd = np.abs(np.array(ref["std"], dtype=np.float32) - sd).max()
    print(f"SPLIT/NORM PARITY vs edge_norm_v3.json: max|dmean|={dmu:.3g} "
          f"max|dstd|={dsd:.3g}", flush=True)
    assert dmu < 1e-4 and dsd < 1e-4, "pipeline diverged from v3 -- comparison invalid"

    idx = np.concatenate([np.flatnonzero(tr), np.flatnonzero(va), np.flatnonzero(te)])
    n_tr, n_va, n_te = int(tr.sum()), int(va.sum()), int(te.sum())
    n = len(idx)
    out = np.lib.format.open_memmap(f"{D}/cache_X.npy", mode="w+",
                                    dtype=np.float32, shape=(n, X.shape[1]))
    CH = 2_000_000
    for i in range(0, n, CH):
        j = idx[i:i + CH]
        out[i:i + CH] = (X[j] - mu) / sd
    out.flush()
    del out
    print(f"[{time.time()-t0:7.1f}s] wrote cache_X.npy {n}x{X.shape[1]}", flush=True)

    np.savez(f"{D}/cache_meta.npz",
             label=meta["label"][idx].astype(np.int8),
             etype=meta["etype"][idx].astype(np.int8),
             simVxy=meta["simVxy"][idx].astype(np.float32),
             simPt=meta["simPt"][idx].astype(np.float32),
             bounds=np.array([n_tr, n_va, n_te], dtype=np.int64))
    json.dump({"feature_names": names, "mean": mu.tolist(), "std": sd.tolist(),
               "conditioning": TE.CONDITIONING_SPEC,
               "n_train": n_tr, "n_val": n_va, "n_test": n_te, "seed": SEED},
              open(f"{D}/cache_norm.json", "w"), indent=1)
    print(f"[{time.time()-t0:7.1f}s] DONE train/val/test = {n_tr}/{n_va}/{n_te}",
          flush=True)


if __name__ == "__main__":
    main()
