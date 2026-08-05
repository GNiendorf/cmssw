#!/usr/bin/env python3
"""A5 (K4-lite context round): TWO-PASS edge scoring for the chain-tracking prototype.

Pass 1 = the deployed v3 edge MLP (40->32->32->1) logit.
Pass 2 = a small head that sees the same 40 raw features PLUS 4 junction-context
features built from the pass-1 logits of the edges that compete at the same junction:

  for edge e = (u, v)  [u = inner T3, v = outer T3, junction = shared MD (E1) / LS (E2)]
    ctxOutMean/ctxOutMax : leave-one-out mean/max of pass-1 logits over u's OTHER
                           out-edges (u, w), w != v   -- competitors for u's out-slot
    ctxInMean /ctxInMax  : leave-one-out mean/max of pass-1 logits over v's OTHER
                           in-edges  (x, v), x != u   -- competitors for v's in-slot

These are exactly the K6 mutual-best welding competitor sets, so the head can learn
"am I the best option at this junction?" -- the plan's K4 density-context hypothesis
applied to the P1-localized formation problem.

Variants trained (all on the SAME rows/split/seed, so the deltas are the measurement):
  A  44->24->1   40 raw + 4 ctx                      (the literal task spec)
  B  45->24->1   40 raw + pass1 logit + 4 ctx        (context on top of pass 1)
  D  41->24->1   40 raw + pass1 logit                (CONTROL: same capacity, NO ctx)
  Dz 45->24->1   40 raw + pass1 logit + 4 ctx ZEROED (CONTROL: same shape, no info)

Split: the FROZEN test-60 events of edges_300evt.root (reproduced bit-exactly from
train_edge.py's seed-42 event_split), train = first 180 shuffled events, val = next 60.
Those 240 events are a subset of v3's training pool, so the test set stays clean.
"""

import argparse
import copy
import json
import os
import time

import numpy as np

T0 = time.time()
PROTO = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype"
MINE = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout/a5_context"
SENTINEL = -10.0  # ctx value when the competitor set is empty (deg==1 is an input feature)


def log(msg):
    print(f"[{time.time() - T0:8.1f}s] {msg}", flush=True)


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--dump", default=f"{MINE}/edges_ctx_300evt.root")
    p.add_argument("--norm", default=f"{PROTO}/edge_norm_v3.json")
    p.add_argument("--model", default=f"{PROTO}/edge_mlp_v3.pt")
    p.add_argument("--out-prefix", default=f"{MINE}/a5")
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--epochs", type=int, default=40)
    p.add_argument("--patience", type=int, default=6)
    p.add_argument("--batch-size", type=int, default=65536)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--max-train-fakes", type=int, default=8000000)
    p.add_argument("--variants", default="A,B,D,Dz")
    p.add_argument("--hidden", type=int, default=24)
    return p.parse_args()


META = ["evt", "lumi", "etype", "label", "simVxy", "simPt", "innerT3", "outerT3"]


def load_dump(path):
    import uproot
    f = uproot.open(path)
    spec = f["feature_spec"].member("fTitle")
    ni_part, ef_part = spec.split(";")
    node_names = ni_part.split(":")[1].split(",")
    edge_names = ef_part.split(":")[1].split(",")
    n_node, n_edge = len(node_names), len(edge_names)
    branches = ([f"ni_{i:02d}" for i in range(n_node)]
                + [f"no_{i:02d}" for i in range(n_node)]
                + [f"ef_{i:02d}" for i in range(n_edge)])
    names = ([f"ni_{n}" for n in node_names] + [f"no_{n}" for n in node_names]
             + [f"ef_{n}" for n in edge_names])
    tree = f["edges"]
    arr = tree.arrays(META + branches, library="np")
    meta = {k: arr[k] for k in META}
    n = len(meta["label"])
    X = np.empty((n, len(branches)), dtype=np.float32)
    for j, b in enumerate(branches):
        X[:, j] = arr[b]
        del arr[b]
    return meta, X, names


def apply_conditioning(X, names, spec):
    for c in spec:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        elif c["op"] == "log10_1p":
            X[:, j] = np.log10(1.0 + X[:, j])
        else:
            raise ValueError(c["op"])
    return spec


def loo_ctx(keys, vals):
    """Leave-one-out (mean, max) of `vals` over each group of equal `keys`.

    Returns float32 arrays; groups of size 1 get SENTINEL. O(n log n), no python loop
    over groups.
    """
    n = len(keys)
    out_mean = np.empty(n, dtype=np.float32)
    out_max = np.empty(n, dtype=np.float32)
    if n == 0:
        return out_mean, out_max
    order = np.lexsort((vals, keys))
    ks = keys[order]
    vs = vals[order]
    newgrp = np.empty(n, dtype=bool)
    newgrp[0] = True
    np.not_equal(ks[1:], ks[:-1], out=newgrp[1:])
    gid = np.cumsum(newgrp) - 1
    cnt = np.bincount(gid)
    ssum = np.bincount(gid, weights=vs.astype(np.float64))
    ends = np.cumsum(cnt) - 1                       # last (== max) row of each group
    max1 = vs[ends]
    max2 = np.where(cnt > 1, vs[np.maximum(ends - 1, 0)], SENTINEL)
    is_argmax = np.zeros(n, dtype=bool)
    is_argmax[ends] = True
    m_sorted = np.where(is_argmax, max2[gid], max1[gid]).astype(np.float32)
    c = cnt[gid]
    mean_sorted = np.where(c > 1, (ssum[gid] - vs) / np.maximum(c - 1, 1), SENTINEL).astype(np.float32)
    out_mean[order] = mean_sorted
    out_max[order] = m_sorted
    return out_mean, out_max


def build_context(meta, logit1):
    """4 junction-context features, computed INDEPENDENTLY PER EVENT."""
    key = (meta["lumi"].astype(np.uint64) << np.uint64(32)) | meta["evt"].astype(np.uint64)
    n = len(key)
    ctx = np.empty((n, 4), dtype=np.float32)
    # events are contiguous in the dump (writer fills event by event); verify + use bounds
    bnd = np.flatnonzero(np.concatenate(([True], key[1:] != key[:-1], [True])))
    assert len(np.unique(key)) == len(bnd) - 1, "events are not contiguous in the dump"
    inner = meta["innerT3"].astype(np.int64)
    outer = meta["outerT3"].astype(np.int64)
    for a, b in zip(bnd[:-1], bnd[1:]):
        li = logit1[a:b]
        om, ox = loo_ctx(inner[a:b], li)   # u's other out-edges
        im, ix = loo_ctx(outer[a:b], li)   # v's other in-edges
        ctx[a:b, 0] = om
        ctx[a:b, 1] = ox
        ctx[a:b, 2] = im
        ctx[a:b, 3] = ix
    np.clip(ctx, -20.0, 20.0, out=ctx)
    return ctx


def batched_scores(model, X_np, device, bs=1 << 20):
    import torch
    model.eval()
    out = np.empty(len(X_np), dtype=np.float32)
    with torch.no_grad():
        for i in range(0, len(X_np), bs):
            xb = torch.tensor(np.ascontiguousarray(X_np[i:i + bs])).to(device)
            out[i:i + bs] = model(xb).squeeze(1).float().cpu().tolist()
    return out


def rejection_at_eff(s_true, s_fake, eff):
    thr = float(np.quantile(s_true, 1.0 - eff, method="lower"))
    return float((s_fake < thr).mean())


def eval_all(tag, s, lab, vxy, ety):
    from sklearn.metrics import roc_auc_score
    is_true = lab == 1
    is_fake = ~is_true
    s_fake = s[is_fake]
    res = {}

    def blk(name, m_true, fake=None):
        st = s[m_true]
        sf = s_fake if fake is None else fake
        y = np.concatenate([np.ones(len(st)), np.zeros(len(sf))])
        auc = float(roc_auc_score(y, np.concatenate([st, sf])))
        r = {e: rejection_at_eff(st, sf, e) for e in (0.99, 0.995, 0.999)}
        res[name] = {"auc": auc, "n_true": int(len(st)), "n_fake": int(len(sf)),
                     **{f"rej@{e}": r[e] for e in r}}
        print(f"  {tag:>6} {name:>18} n_true={len(st):>8d} AUC={auc:.5f} "
              + " ".join(f"rej@{e*100:g}%={r[e]:.5f}" for e in r), flush=True)

    blk("all", is_true)
    blk("prompt", is_true & (vxy < 1))
    blk("disp>=1", is_true & (vxy >= 1))
    blk("disp>=5", is_true & (vxy >= 5))
    for et, nm in ((1, "E1"), (2, "E2")):
        m = ety == et
        blk(nm, is_true & m, fake=s[is_fake & m])
    return res


def main():
    args = parse_args()
    import torch
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.cuda.manual_seed_all(args.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log(f"device={device}")

    norm = json.load(open(args.norm))
    meta, X, names = load_dump(args.dump)
    assert names == norm["feature_names"], "feature order mismatch vs edge_norm_v3.json"
    log(f"loaded {len(X)} edges x {X.shape[1]} features")
    apply_conditioning(X, names, norm["conditioning"])
    np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)

    # ---- pass 1: deployed v3 logits ----
    mu = np.asarray(norm["mean"], dtype=np.float32)
    sd = np.asarray(norm["std"], dtype=np.float32)
    ck = torch.load(args.model, map_location="cpu", weights_only=False)
    v3 = torch.nn.Sequential(torch.nn.Linear(40, 32), torch.nn.ReLU(),
                             torch.nn.Linear(32, 32), torch.nn.ReLU(),
                             torch.nn.Linear(32, 1))
    v3.load_state_dict(ck["state_dict"])
    v3.to(device).eval()
    logit1 = np.empty(len(X), dtype=np.float32)
    bs = 1 << 21
    with torch.no_grad():
        for i in range(0, len(X), bs):
            xb = torch.tensor(np.ascontiguousarray((X[i:i + bs] - mu) / sd)).to(device)
            logit1[i:i + bs] = v3(xb).squeeze(1).float().cpu().tolist()
    log(f"pass-1 logits: mean={logit1.mean():.3f} p1={np.percentile(logit1,1):.2f} "
        f"p99={np.percentile(logit1,99):.2f} min={logit1.min():.2f} max={logit1.max():.2f}")

    # ---- junction context ----
    ctx = build_context(meta, logit1)
    log(f"ctx built; empty-set fraction per column: "
        + " ".join(f"{(ctx[:, j] == SENTINEL).mean():.3f}" for j in range(4)))

    # ---- frozen split (bit-exact reproduction of train_edge.py event_split, seed 42) ----
    key = (meta["lumi"].astype(np.uint64) << np.uint64(32)) | meta["evt"].astype(np.uint64)
    rng = np.random.default_rng(args.seed)
    uniq = np.unique(key)
    rng.shuffle(uniq)
    n_tr = int(round(0.6 * len(uniq)))
    n_va = int(round(0.2 * len(uniq)))
    tr = np.isin(key, uniq[:n_tr])
    va = np.isin(key, uniq[n_tr:n_tr + n_va])
    te = np.isin(key, uniq[n_tr + n_va:])
    log(f"split events {n_tr}/{n_va}/{len(uniq)-n_tr-n_va} -> rows {tr.sum()}/{va.sum()}/{te.sum()}")
    assert te.sum() == 6219490, f"frozen test set mismatch: {te.sum()} rows (expected 6219490)"

    lab = meta["label"]
    vxy = meta["simVxy"]
    ety = meta["etype"]
    results = {}

    print("\n=== PASS-1 BASELINE (deployed v3) on the frozen TEST-60 ===", flush=True)
    results["v3_pass1"] = eval_all("v3", logit1[te], lab[te], vxy[te], ety[te])

    # ---- assemble the pass-2 feature matrix (superset; variants select columns) ----
    # column layout: [0:40] raw, [40] pass1 logit, [41:45] ctx
    full = np.empty((len(X), 45), dtype=np.float32)
    full[:, :40] = X
    full[:, 40] = logit1
    full[:, 41:] = ctx
    del X

    VARIANT_COLS = {
        "A":  list(range(40)) + [41, 42, 43, 44],
        "B":  list(range(45)),
        "D":  list(range(41)),
        "Dz": list(range(45)),
    }

    # train-row subsample: all trues, capped fakes (the head is tiny; this only bounds memory)
    tr_idx = np.flatnonzero(tr)
    tr_true = tr_idx[lab[tr_idx] == 1]
    tr_fake = tr_idx[lab[tr_idx] == 0]
    if len(tr_fake) > args.max_train_fakes:
        sub = np.random.default_rng(args.seed + 1).choice(len(tr_fake), args.max_train_fakes, replace=False)
        tr_fake = tr_fake[np.sort(sub)]
    tr_rows = np.sort(np.concatenate([tr_true, tr_fake]))
    log(f"train rows {len(tr_rows)} ({len(tr_true)} true / {len(tr_fake)} fake)")
    va_rows = np.flatnonzero(va)
    te_rows = np.flatnonzero(te)

    y_tr = (lab[tr_rows] == 1).astype(np.float32)
    y_va = (lab[va_rows] == 1).astype(np.float32)
    pos_weight = float((y_tr == 0).sum()) / max(float(y_tr.sum()), 1.0)
    log(f"pos_weight={pos_weight:.4f}")

    from sklearn.metrics import roc_auc_score
    for vname in args.variants.split(","):
        cols = VARIANT_COLS[vname]
        Xtr = np.ascontiguousarray(full[np.ix_(tr_rows, cols)])
        Xva = np.ascontiguousarray(full[np.ix_(va_rows, cols)])
        Xte = np.ascontiguousarray(full[np.ix_(te_rows, cols)])
        if vname == "Dz":  # ablation: same shape, context information destroyed
            Xtr[:, 41:] = 0.0
            Xva[:, 41:] = 0.0
            Xte[:, 41:] = 0.0
        m2 = Xtr.mean(axis=0, dtype=np.float64).astype(np.float32)
        s2 = Xtr.std(axis=0, dtype=np.float64).astype(np.float32)
        s2[s2 < 1e-8] = 1.0
        for A in (Xtr, Xva, Xte):
            A -= m2
            A /= s2
        n_in = len(cols)
        torch.manual_seed(args.seed)
        model = torch.nn.Sequential(torch.nn.Linear(n_in, args.hidden), torch.nn.ReLU(),
                                    torch.nn.Linear(args.hidden, 1)).to(device)
        opt = torch.optim.Adam(model.parameters(), lr=args.lr)
        crit = torch.nn.BCEWithLogitsLoss(pos_weight=torch.tensor(pos_weight, device=device))
        Xtr_t = torch.tensor(Xtr)
        ytr_t = torch.tensor(y_tr)
        gen = torch.Generator(device="cpu").manual_seed(args.seed)
        best, best_state, best_ep, bad = -1.0, None, -1, 0
        n = len(Xtr_t)
        log(f"--- variant {vname}: {n_in}->{args.hidden}->1, {n} train rows")
        for ep in range(1, args.epochs + 1):
            model.train()
            perm = torch.randperm(n, generator=gen)
            tot = 0.0
            for i in range(0, n, args.batch_size):
                idx = perm[i:i + args.batch_size]
                xb = Xtr_t[idx].to(device, non_blocking=True)
                yb = ytr_t[idx].to(device, non_blocking=True)
                opt.zero_grad()
                loss = crit(model(xb).squeeze(1), yb)
                loss.backward()
                opt.step()
                tot += float(loss.detach()) * len(idx)
            s_va = batched_scores(model, Xva, device)
            auc = float(roc_auc_score(y_va, s_va))
            log(f"  {vname} epoch {ep:2d} loss={tot/n:.5f} val_auc={auc:.5f}")
            if auc > best:
                best, best_ep, bad = auc, ep, 0
                best_state = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
            else:
                bad += 1
                if bad >= args.patience:
                    log(f"  {vname} early stop @ {ep} (best {best:.5f} @ {best_ep})")
                    break
        model.load_state_dict(best_state)
        model.to(device)
        s_te = batched_scores(model, Xte, device)
        print(f"\n=== variant {vname} on the frozen TEST-60 ===", flush=True)
        results[vname] = eval_all(vname, s_te, lab[te_rows], vxy[te_rows], ety[te_rows])
        results[vname]["_meta"] = {"n_in": n_in, "hidden": args.hidden, "cols": cols,
                                   "best_val_auc": best, "best_epoch": best_ep,
                                   "sentinel": SENTINEL}
        torch.save({"state_dict": best_state, "arch": [n_in, args.hidden, 1],
                    "cols": cols, "mean": m2.tolist(), "std": s2.tolist(),
                    "sentinel": SENTINEL, "best_val_auc": best, "best_epoch": best_ep},
                   f"{args.out_prefix}_head_{vname}.pt")
        del Xtr, Xva, Xte, Xtr_t, ytr_t

    with open(f"{args.out_prefix}_twopass_results.json", "w") as fh:
        json.dump(results, fh, indent=1)
    print("\n=== SUMMARY: test AUC (delta vs v3 pass 1) ===")
    for k in results:
        if k == "v3_pass1":
            continue
        row = " ".join(f"{b}={results[k][b]['auc']:.5f}({results[k][b]['auc']-results['v3_pass1'][b]['auc']:+.5f})"
                       for b in ("all", "prompt", "disp>=1", "disp>=5", "E1", "E2"))
        print(f"  {k:>3}: {row}")
    log("done")


if __name__ == "__main__":
    main()
