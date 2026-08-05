#!/usr/bin/env python3
"""M19 EDGE-HEAD DISPLACEMENT RETRAIN (fanout5/edgeretrain).

Derived from train_edge.py (golden tree). Everything about the data pipeline is
IDENTICAL to the resident v3 training run so the only difference between the
variants and v3 is the TRUE-edge sample weighting:
  - same inputs (edges_300evt.root PRIMARY + edges_498evt.root EXTRA),
  - same M8 combination rule / frozen test-60 event split (seed 42,
    train_frac 0.6, val_frac 0.2, pool_train_frac 0.75),
  - same conditioning spec, same TRAIN-row standardization,
  - same arch [40,32,32,1], same lr/batch/epochs/patience, same torch seed and
    same minibatch permutation sequence (every variant re-seeds its generator to
    --seed, and all variants share ONE permutation stream: they see the same
    batches in the same order).

MOTIVATION (T4 trace, fanout4/t4trace/trace_anchor2.txt): dxy[10,30) delivery
fails at the EDGE head -- true E2 (sharedLS) welds of displaced sims score below
the -e 0 weld threshold. Edge-training positives are ~96.8% prompt (vxy<1 cm), so
the head's capacity is spent on prompt topology.

WEIGHTING DESIGN (documented; the C++ side is unaffected -- weights only shape the
training loss, never inference):
  Per TRUE edge with shared-sim vxy and edge type etype (1 = E1 sharedMD,
  2 = E2 sharedLS):
      T(vxy) = 1.0        vxy <  1 cm      (prompt)
               a1         1 <= vxy < 5 cm
               a2         vxy >= 5 cm
      B      = b          if etype == 2 AND vxy >= 1 cm   (the measured E2-vs-E2
               1.0        otherwise                        misranking mode)
      w_raw  = T(vxy) * B
  FAKE edges keep weight exactly 1 (as in the v2 machinery).
  Normalization: w = w_raw / mean_{TRAIN trues}(w_raw)  -- mean-1 over train trues.
  Rationale for mean-1: the total positive loss mass is preserved, so the global
  logit calibration (and therefore the meaning of the FIXED -e 0 weld threshold
  and of the K6 log-odds SUM) is not shifted wholesale; the weighting only moves
  mass from prompt to displaced WITHIN the positive class.

Outputs per variant <tag>: er_edge_mlp_<tag>.pt, er_edge_norm_<tag>.json,
plus er_edge_auc.json with the full test-60 table for every variant AND for the
resident v3 model scored on the same rows.
"""

import argparse
import copy
import json
import os
import sys
import time

import numpy as np

T0 = time.time()
PDIR = os.path.dirname(os.path.abspath(__file__))
PROTO = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype"


def log(msg):
    print(f"[{time.time() - T0:8.1f}s] {msg}", flush=True)


# ---- variant table: tag -> (a1, a2, b, description) -------------------------
VARIANTS = {
    "d1": (4.0, 8.0, 1.0, "vxy-tier moderate (1/4/8), no E2 boost"),
    "d2": (4.0, 8.0, 2.5, "vxy-tier moderate + E2-displaced boost x2.5"),
    "d3": (8.0, 16.0, 2.5, "vxy-tier aggressive (1/8/16) + E2-displaced boost x2.5"),
    # M19 round 2: d1/d2/d3 all traded eff vxy[1,5) (-0.004..-0.007) for large
    # vxy>=5 gains -- the tier ratio a2/a1 = 2 favours the far-displaced band. d4/d5
    # FLATTEN the tier (a1 == a2) so the [1,5) band gets the same pressure as >=5.
    "d4": (8.0, 8.0, 2.5, "FLAT tier (1/8/8) + E2-displaced boost x2.5"),
    "d5": (6.0, 6.0, 4.0, "FLAT tier (1/6/6) + strong E2-displaced boost x4"),
}

META_BRANCHES = ["evt", "lumi", "etype", "label", "simVxy", "simPt"]

CONDITIONING_SPEC = [
    {"feature": "ni_kappaSigned", "op": "clip", "lo": -1.0, "hi": 1.0},
    {"feature": "no_kappaSigned", "op": "clip", "lo": -1.0, "hi": 1.0},
    {"feature": "ni_log10R", "op": "clip", "lo": 0.0, "hi": 9.0},
    {"feature": "no_log10R", "op": "clip", "lo": 0.0, "hi": 9.0},
    {"feature": "ef_dKappa", "op": "clip", "lo": -2.0, "hi": 2.0},
    {"feature": "ef_centerDist", "op": "log10_1p"},
]


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", nargs="+",
                   default=[f"{PROTO}/edges_300evt.root", f"{PROTO}/edges_498evt.root"])
    p.add_argument("--variants", nargs="+", default=["d1", "d2", "d3"])
    p.add_argument("--resident", default=f"{PDIR}/edge_mlp_v3.pt")
    p.add_argument("--resident-norm", default=f"{PDIR}/edge_norm_v3.json")
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--epochs", type=int, default=100)
    p.add_argument("--patience", type=int, default=8)
    p.add_argument("--batch-size", type=int, default=65536)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--train-frac", type=float, default=0.6)
    p.add_argument("--val-frac", type=float, default=0.2)
    p.add_argument("--pool-train-frac", type=float, default=0.75)
    p.add_argument("--out-auc", default=f"{PDIR}/er_edge_auc.json")
    p.add_argument("--state-dir", default=f"{PDIR}/state")
    return p.parse_args()


def load_dump(path):
    import uproot
    f = uproot.open(path)
    spec = f["feature_spec"].member("fTitle")
    ni_part, ef_part = spec.split(";")
    node_names = ni_part.split(":")[1].split(",")
    edge_names = ef_part.split(":")[1].split(",")
    n_node, n_edge = len(node_names), len(edge_names)
    feat_branches = ([f"ni_{i:02d}" for i in range(n_node)]
                     + [f"no_{i:02d}" for i in range(n_node)]
                     + [f"ef_{i:02d}" for i in range(n_edge)])
    names = ([f"ni_{n}" for n in node_names]
             + [f"no_{n}" for n in node_names]
             + [f"ef_{n}" for n in edge_names])
    tree = f["edges"]
    arr = tree.arrays(META_BRANCHES + feat_branches, library="np")
    meta = {k: arr[k] for k in META_BRANCHES}
    n = len(meta["label"])
    X = np.empty((n, len(feat_branches)), dtype=np.float32)
    for j, b in enumerate(feat_branches):
        X[:, j] = arr[b]
        del arr[b]
    return meta, X, names, edge_names, n_node


def apply_conditioning(X, names, spec):
    for c in spec:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        elif c["op"] == "log10_1p":
            X[:, j] = np.log10(1.0 + X[:, j])
    return spec


def combined_event_split(meta, src, args, rng):
    """M8 COMBINATION RULE split -- byte-identical logic to train_edge.py."""
    key = (meta["lumi"].astype(np.uint64) << np.uint64(32)) | meta["evt"].astype(np.uint64)
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
    log(f"COMBINED event split: frozen test = {len(te_keys)} primary events; "
        f"pool {len(pool_keys)} -> train {len(tr_keys)} / val {len(va_keys)} "
        f"-> {tr.sum()}/{va.sum()}/{te.sum()} edges")
    return tr, va, te


def build_weights(meta, tr, a1, a2, b):
    """Displacement-aware TRUE-edge weights; see module docstring."""
    is_true = meta["label"] == 1
    vxy = meta["simVxy"]
    ety = meta["etype"]
    t = np.ones(len(vxy), dtype=np.float64)
    t[(vxy >= 1.0) & (vxy < 5.0)] = a1
    t[vxy >= 5.0] = a2
    boost = np.where((ety == 2) & (vxy >= 1.0), b, 1.0)
    w_raw = t * boost
    w = np.ones(len(vxy), dtype=np.float64)
    w[is_true] = w_raw[is_true]
    scale = float(w[tr & is_true].mean())
    w[is_true] /= scale
    # report
    rows = []
    for lo, hi, lab in ((0.0, 1.0, "vxy<1"), (1.0, 5.0, "vxy[1,5)"), (5.0, 1e18, "vxy>=5")):
        for et, elab in ((1, "E1"), (2, "E2")):
            m = tr & is_true & (vxy >= lo) & (vxy < hi) & (ety == et)
            n = int(m.sum())
            wm = float(w[m].mean()) if n else 0.0
            rows.append({"band": lab, "etype": elab, "n_train_true": n, "weight": wm})
            print(f"  {lab:>9} {elab:>3} n_train_true={n:>10d} weight={wm:.4f}")
    spec = {"a1_vxy1to5": a1, "a2_vxy5plus": a2, "b_E2_displaced": b,
            "norm_scale_mean_over_train_trues": scale, "rows": rows,
            "note": "true-edge weight = T(vxy)*B(etype,vxy)/scale; fakes weight 1"}
    return w.astype(np.float32), spec


def build_model(n_in):
    import torch.nn as nn
    return nn.Sequential(nn.Linear(n_in, 32), nn.ReLU(),
                         nn.Linear(32, 32), nn.ReLU(),
                         nn.Linear(32, 1))


def batched_scores(model, X_t, device, bs=1 << 20):
    import torch
    model.eval()
    out = np.empty(len(X_t), dtype=np.float32)
    with torch.no_grad():
        for i in range(0, len(X_t), bs):
            logits = model(X_t[i:i + bs].to(device)).squeeze(1).float().cpu()
            out[i:i + bs] = logits.tolist()
    return out


def rejection_at_eff(s_true, s_fake, eff):
    thr = float(np.quantile(s_true, 1.0 - eff, method="lower"))
    return float((s_fake < thr).mean())


def eval_block(tag, s_true, s_fake, effs=(0.99, 0.995, 0.999)):
    from sklearn.metrics import roc_auc_score
    if len(s_true) == 0 or len(s_fake) == 0:
        return None
    y = np.concatenate([np.ones(len(s_true)), np.zeros(len(s_fake))])
    s = np.concatenate([s_true, s_fake])
    auc = float(roc_auc_score(y, s))
    rejs = {e: rejection_at_eff(s_true, s_fake, e) for e in effs}
    pass0 = float((s_true > 0).mean())
    med = float(np.median(s_true))
    print(f"  {tag:>24} n_true={len(s_true):>8d} n_fake={len(s_fake):>8d} AUC={auc:.5f} "
          + " ".join(f"rej@{e * 100:g}%={rejs[e]:.5f}" for e in effs)
          + f" med={med:+.3f} frac>0={pass0:.4f}")
    return {"auc": auc, **{f"rej@{e}": rejs[e] for e in effs},
            "n_true": int(len(s_true)), "n_fake": int(len(s_fake)),
            "median_true_logit": med, "true_frac_above0": pass0}


def full_eval(s_te, lab_te, vxy_te, ety_te):
    is_true = lab_te == 1
    is_fake = ~is_true
    sf_all = s_te[is_fake]
    res = {}
    res["all"] = eval_block("all", s_te[is_true], sf_all)
    res["prompt vxy<1"] = eval_block("prompt vxy<1", s_te[is_true & (vxy_te < 1)], sf_all)
    res["displaced vxy>=1"] = eval_block("displaced vxy>=1", s_te[is_true & (vxy_te >= 1)], sf_all)
    res["displaced vxy>=5"] = eval_block("displaced vxy>=5", s_te[is_true & (vxy_te >= 5)], sf_all)
    for et, tag in ((1, "E1 sharedMD"), (2, "E2 sharedLS")):
        m = ety_te == et
        res[tag] = eval_block(tag, s_te[is_true & m], s_te[is_fake & m])
        res[f"{tag} prompt"] = eval_block(f"{tag} prompt",
                                          s_te[is_true & m & (vxy_te < 1)], s_te[is_fake & m])
        res[f"{tag} displ>=1"] = eval_block(f"{tag} displ>=1",
                                            s_te[is_true & m & (vxy_te >= 1)], s_te[is_fake & m])
        res[f"{tag} displ>=5"] = eval_block(f"{tag} displ>=5",
                                            s_te[is_true & m & (vxy_te >= 5)], s_te[is_fake & m])
    # global weld-threshold occupancy at the FIXED -e 0 cut
    res["_weld"] = {"true_frac_above0": float((s_te[is_true] > 0).mean()),
                    "fake_frac_above0": float((s_te[is_fake] > 0).mean()),
                    "all_frac_above0": float((s_te > 0).mean())}
    print(f"  -e 0 occupancy: true {res['_weld']['true_frac_above0']:.4f} "
          f"fake {res['_weld']['fake_frac_above0']:.4f} all {res['_weld']['all_frac_above0']:.4f}")
    return res


def main():
    args = parse_args()
    import torch
    os.makedirs(args.state_dir, exist_ok=True)

    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.cuda.manual_seed_all(args.seed)
    rng = np.random.default_rng(args.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log(f"seed={args.seed} device={device}")

    metas, x_parts, src_parts = [], [], []
    names = edge_names = n_node = prim_evts = None
    for si, path in enumerate(args.input):
        meta_i, X_i, names_i, edge_names_i, n_node_i = load_dump(path)
        if si == 0:
            names, edge_names, n_node = names_i, edge_names_i, n_node_i
            prim_evts = np.unique(meta_i["evt"])
            log(f"input[0] PRIMARY {path}: {len(X_i)} edges over {len(prim_evts)} events")
        else:
            assert names_i == names
            keep = ~np.isin(meta_i["evt"], prim_evts)
            log(f"input[{si}] EXTRA {path}: {len(X_i)} edges -> keep {int(keep.sum())}")
            meta_i = {k: v[keep] for k, v in meta_i.items()}
            X_i = X_i[keep]
        metas.append(meta_i)
        x_parts.append(X_i)
        src_parts.append(np.full(len(X_i), si, dtype=np.int8))
    meta = {k: np.concatenate([m[k] for m in metas]) for k in META_BRANCHES}
    X = np.concatenate(x_parts)
    src = np.concatenate(src_parts)
    del metas, x_parts, src_parts
    log(f"loaded {len(X)} edges x {X.shape[1]} features")

    n_bad = int((~np.isfinite(X)).sum())
    if n_bad:
        log(f"WARNING: {n_bad} non-finite -> 0")
        np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
    conditioning = apply_conditioning(X, names, CONDITIONING_SPEC)
    log("conditioning applied")

    tr, va, te = combined_event_split(meta, src, args, rng)

    mu = X[tr].mean(axis=0, dtype=np.float64).astype(np.float32)
    sd = X[tr].std(axis=0, dtype=np.float64).astype(np.float32)
    sd[sd < 1e-8] = 1.0

    # PIPELINE PARITY CHECK against the resident v3 norm: identical split =>
    # identical standardization constants. Any drift here invalidates the A/B.
    with open(args.resident_norm) as fh:
        rn = json.load(fh)
    dmu = float(np.max(np.abs(np.array(rn["mean"], dtype=np.float32) - mu)))
    dsd = float(np.max(np.abs(np.array(rn["std"], dtype=np.float32) - sd)))
    log(f"PIPELINE PARITY vs edge_norm_v3.json: max|dmean|={dmu:.3e} max|dstd|={dsd:.3e}")
    assert dmu < 1e-5 and dsd < 1e-5, "standardization drift -> split/pipeline mismatch"

    X -= mu
    X /= sd  # in place; X is now standardized

    y = (meta["label"] == 1).astype(np.float32)
    n_pos, n_neg = int(y[tr].sum()), int((1 - y[tr]).sum())
    pos_weight = n_neg / max(n_pos, 1)
    log(f"train: {n_pos} true / {n_neg} fake -> pos_weight={pos_weight:.4f}")

    Xtr = torch.tensor(np.ascontiguousarray(X[tr]))
    ytr = torch.tensor(np.ascontiguousarray(y[tr]))
    Xva = torch.tensor(np.ascontiguousarray(X[va]))
    Xte = torch.tensor(np.ascontiguousarray(X[te]))
    yva_np = y[va]
    if device.type == "cuda":
        Xtr, ytr = Xtr.to(device), ytr.to(device)
    log(f"tensors ready (train {tuple(Xtr.shape)})")

    lab_te, vxy_te, ety_te = meta["label"][te], meta["simVxy"][te], meta["etype"][te]
    out = {"variants": {}, "design": {v: {"a1": VARIANTS[v][0], "a2": VARIANTS[v][1],
                                          "b": VARIANTS[v][2], "desc": VARIANTS[v][3]}
                                      for v in args.variants}}

    # ---- resident v3 control on the same test rows ----
    blob = torch.load(args.resident, map_location="cpu", weights_only=False)
    m0 = build_model(X.shape[1])
    m0.load_state_dict(blob["state_dict"])
    m0.to(device)
    print("\n=== RESIDENT v3 on TEST-60 ===")
    out["v3_resident"] = full_eval(batched_scores(m0, Xte, device), lab_te, vxy_te, ety_te)
    del m0

    from sklearn.metrics import roc_auc_score

    # ---- build all variant weight vectors ----
    wts, wspecs = {}, {}
    for tag in args.variants:
        a1, a2, b = VARIANTS[tag][:3]
        print(f"\n--- weights {tag}: a1={a1} a2={a2} b={b} ({VARIANTS[tag][3]}) ---")
        w_np, spec = build_weights(meta, tr, a1, a2, b)
        wspecs[tag] = spec
        t = torch.tensor(np.ascontiguousarray(w_np[tr]))
        wts[tag] = t.to(device) if device.type == "cuda" else t
        del w_np

    # ---- joint training: same batches, one model per variant ----
    models, opts, best = {}, {}, {}
    for tag in args.variants:
        torch.manual_seed(args.seed)  # identical init across variants
        models[tag] = build_model(X.shape[1]).to(device)
        opts[tag] = torch.optim.Adam(models[tag].parameters(), lr=args.lr)
        best[tag] = {"auc": -1.0, "state": None, "epoch": -1, "bad": 0, "stopped": False}
    crit = torch.nn.BCEWithLogitsLoss(pos_weight=torch.tensor(pos_weight, device=device),
                                      reduction="none")
    gen = torch.Generator(device="cpu").manual_seed(args.seed)
    n_tr = len(Xtr)
    state_path = os.path.join(args.state_dir, "er_edge_joint_state.pt")
    start_epoch = 1
    if os.path.exists(state_path):
        st = torch.load(state_path, map_location="cpu", weights_only=False)
        for tag in args.variants:
            models[tag].load_state_dict(st["models"][tag])
            models[tag].to(device)
            opts[tag].load_state_dict(st["opts"][tag])
            best[tag] = st["best"][tag]
        gen.set_state(st["gen"])
        start_epoch = st["epoch"] + 1
        log(f"RESUMED at epoch {start_epoch}")

    for epoch in range(start_epoch, args.epochs + 1):
        perm = torch.randperm(n_tr, generator=gen)
        for tag in args.variants:
            models[tag].train()
        tot = {tag: 0.0 for tag in args.variants}
        for i in range(0, n_tr, args.batch_size):
            idx = perm[i:i + args.batch_size]
            if device.type == "cuda":
                idx = idx.to(device)
            xb, yb = Xtr[idx], ytr[idx]
            for tag in args.variants:
                if best[tag]["stopped"]:
                    continue
                opts[tag].zero_grad()
                loss = (crit(models[tag](xb).squeeze(1), yb) * wts[tag][idx]).mean()
                loss.backward()
                opts[tag].step()
                tot[tag] += float(loss.detach()) * len(idx)
        msg = []
        for tag in args.variants:
            if best[tag]["stopped"]:
                msg.append(f"{tag}:STOPPED")
                continue
            auc = roc_auc_score(yva_np, batched_scores(models[tag], Xva, device))
            if auc > best[tag]["auc"]:
                best[tag].update(auc=auc, epoch=epoch, bad=0,
                                 state=copy.deepcopy({k: v.cpu() for k, v in
                                                      models[tag].state_dict().items()}))
            else:
                best[tag]["bad"] += 1
                if best[tag]["bad"] >= args.patience:
                    best[tag]["stopped"] = True
            msg.append(f"{tag}:loss={tot[tag] / n_tr:.5f} auc={auc:.5f}"
                       + ("*" if best[tag]["epoch"] == epoch else ""))
        log(f"epoch {epoch:3d} " + "  ".join(msg))
        tmp = state_path + ".tmp"
        torch.save({"models": {t: {k: v.cpu() for k, v in models[t].state_dict().items()}
                               for t in args.variants},
                    "opts": {t: opts[t].state_dict() for t in args.variants},
                    "gen": gen.get_state(), "best": best, "epoch": epoch}, tmp)
        os.replace(tmp, state_path)
        if all(best[t]["stopped"] for t in args.variants):
            log("all variants early-stopped")
            break

    # ---- save + evaluate every variant ----
    for tag in args.variants:
        models[tag].load_state_dict(best[tag]["state"])
        models[tag].to(device)
        mp = f"{PDIR}/er_edge_mlp_{tag}.pt"
        npth = f"{PDIR}/er_edge_norm_{tag}.json"
        torch.save({"state_dict": best[tag]["state"], "arch": [X.shape[1], 32, 32, 1],
                    "feature_names": names, "seed": args.seed,
                    "conditioning": conditioning,
                    "best_epoch": best[tag]["epoch"],
                    "best_val_auc": float(best[tag]["auc"])}, mp)
        with open(npth, "w") as fh:
            json.dump({"feature_names": names, "conditioning": conditioning,
                       "mean": mu.tolist(), "std": sd.tolist(), "seed": args.seed,
                       "degenerate_columns": [],
                       "displaced_weighting": wspecs[tag],
                       "train_args": {"epochs": args.epochs, "patience": args.patience,
                                      "batch_size": args.batch_size, "lr": args.lr,
                                      "feature_clip": True, "displaced_weight": True,
                                      "inputs": args.input,
                                      "pool_train_frac": args.pool_train_frac}}, fh, indent=1)
        print(f"\n=== VARIANT {tag} ({VARIANTS[tag][3]}) best epoch {best[tag]['epoch']} "
              f"val_auc {best[tag]['auc']:.5f} on TEST-60 ===")
        out["variants"][tag] = full_eval(batched_scores(models[tag], Xte, device),
                                         lab_te, vxy_te, ety_te)
        out["variants"][tag]["_meta"] = {"best_epoch": best[tag]["epoch"],
                                         "best_val_auc": float(best[tag]["auc"]),
                                         "weight_spec": wspecs[tag],
                                         "model": mp, "norm": npth}
        log(f"saved {mp} / {npth}")

    with open(args.out_auc, "w") as fh:
        json.dump(out, fh, indent=1)
    log(f"wrote {args.out_auc}")


if __name__ == "__main__":
    sys.exit(main())
