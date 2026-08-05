#!/usr/bin/env python3
"""ANGLE-B3: 3-CLASS EDGE HEAD (fake / prompt-true / displaced-true).

Same discipline as train_edge.py (which it imports for data loading, conditioning,
the M8 combination rule split, and the eval helpers) with three changes:

  1. THREE OUTPUTS, softmax cross-entropy. Class 0 = fake (label != 1), class 1 =
     prompt-true (label == 1 and simVxy <  1 cm), class 2 = displaced-true
     (label == 1 and simVxy >= 1 cm).  M13 measured that displaced-true edges sit
     BETWEEN prompt-true and fake on the curvature-consistency block, so the binary
     boundary cuts through them; a separate head is the learnable direction.
  2. WARM START from the live 2-class model (edge_mlp_v3.pt): hidden layers copied
     verbatim; the output layer is initialised as
        row_fake = -w/2, row_prompt = +w/2, row_disp = +w/2  (biases likewise)
     so that AT EPOCH 0 the margins mP = mD = mX are EXACTLY the v3 binary logit.
     Training can then only improve on v3 -- and it converges in ~20 epochs instead
     of 100 (the 90-min angle budget).  --no-warm-start disables.
  3. TIERED class weights in the CE (M6c recipe, the one that converged the chain
     gate's displaced head): w_fake = 1, w_prompt = n_fake/n_true,
     w_disp = w_prompt * --disp-boost.

Decision margins (softmax is monotone in them -> calibrated log-odds ratios):
    mP = l_prompt - l_fake
    mD = l_disp   - l_fake
    mX = max(l_prompt, l_disp) - l_fake      <- the "max-logit summed evidence"
    mS = logsumexp(l_prompt, l_disp) - l_fake  = log( P(true) / P(fake) ), the exact
         2-class-equivalent log-odds implied by the 3-class model.
K6 welding eligibility is mX >= thetaEdge (== "max(promptL, displacedL) >= -e"), and
the chain score sums mX (mode 1) or mS (mode 2); see EdgeInference.cc.

CONTROL: the frozen-test evaluation also scores the LIVE v3 binary model on the SAME
test rows, so every AUC / rejection number is a paired A/B, not a log comparison.

Outputs: edge3_mlp_v1.pt, edge3_norm_v1.json, edge3_metrics.json
"""

import argparse
import copy
import json
import os
import sys
import time

import numpy as np

import train_edge as te

T0 = time.time()


def log(msg):
    print(f"[{time.time() - T0:8.1f}s] {msg}", flush=True)


def parse_args():
    d = os.path.dirname(os.path.abspath(__file__))
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", nargs="+",
                   default=[f"{d}/edges_300evt.root", f"{d}/edges_498evt.root"])
    p.add_argument("--pool-train-frac", type=float, default=0.75)
    p.add_argument("--out-model", default=f"{d}/edge3_mlp_v1.pt")
    p.add_argument("--out-norm", default=f"{d}/edge3_norm_v1.json")
    p.add_argument("--out-metrics", default=f"{d}/edge3_metrics.json")
    p.add_argument("--warm-from", default=f"{d}/edge_mlp_v3.pt")
    p.add_argument("--warm-norm", default=f"{d}/edge_norm_v3.json")
    p.add_argument("--no-warm-start", action="store_true")
    p.add_argument("--disp-boost", type=float, default=8.0)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--epochs", type=int, default=24)
    p.add_argument("--patience", type=int, default=6)
    p.add_argument("--batch-size", type=int, default=65536)
    p.add_argument("--lr", type=float, default=3e-4)
    p.add_argument("--train-frac", type=float, default=0.6)
    p.add_argument("--val-frac", type=float, default=0.2)
    p.add_argument("--wall-limit-sec", type=float, default=0.0)
    return p.parse_args()


def build_model3(n_in, n_hid=32):
    import torch.nn as nn
    return nn.Sequential(nn.Linear(n_in, n_hid), nn.ReLU(),
                         nn.Linear(n_hid, n_hid), nn.ReLU(),
                         nn.Linear(n_hid, 3))


def margins(logits3):
    """logits3 [N,3] float32 numpy -> dict of the four margins."""
    l0, l1, l2 = logits3[:, 0], logits3[:, 1], logits3[:, 2]
    mx = np.maximum(l1, l2) - l0
    m = np.maximum(l1, l2)
    ms = m + np.log1p(np.exp(-np.abs(l1 - l2))) - l0
    return {"mP": l1 - l0, "mD": l2 - l0, "mX": mx, "mS": ms}


def batched3(model, X_t, device, bs=1 << 20):
    import torch
    model.eval()
    out = np.empty((len(X_t), 3), dtype=np.float32)
    with torch.no_grad():
        for i in range(0, len(X_t), bs):
            o = model(X_t[i:i + bs].to(device)).float().cpu()
            out[i:i + bs] = np.asarray(o.tolist(), dtype=np.float32)
    return out


def main():
    args = parse_args()
    import torch
    from sklearn.metrics import roc_auc_score

    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.cuda.manual_seed_all(args.seed)
    rng = np.random.default_rng(args.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log(f"seed={args.seed} device={device}")

    # ---- load (M8 combination rule, identical to train_edge.py) ----
    metas, x_parts, src_parts = [], [], []
    names = n_node = prim_evts = None
    for si, path in enumerate(args.input):
        meta_i, X_i, names_i, edge_names_i, n_node_i = te.load_dump(path)
        if si == 0:
            names, n_node = names_i, n_node_i
            prim_evts = np.unique(meta_i["evt"])
            log(f"input[0] PRIMARY {path}: {len(X_i)} edges over {len(prim_evts)} events")
        else:
            assert names_i == names
            keep = ~np.isin(meta_i["evt"], prim_evts)
            log(f"input[{si}] EXTRA {path}: keep {int(keep.sum())}/{len(X_i)} edges")
            meta_i = {k: v[keep] for k, v in meta_i.items()}
            X_i = X_i[keep]
        metas.append(meta_i)
        x_parts.append(X_i)
        src_parts.append(np.full(len(X_i), si, dtype=np.int8))
    meta = {k: np.concatenate([m[k] for m in metas]) for k in te.META_BRANCHES}
    X = np.concatenate(x_parts)
    src = np.concatenate(src_parts)
    del metas, x_parts, src_parts
    log(f"loaded {len(X)} edges x {X.shape[1]} features")

    te.apply_conditioning(X, names, te.CONDITIONING_SPEC)
    conditioning = te.CONDITIONING_SPEC

    tr, va, testm = te.combined_event_split(meta, src, args, rng)

    mu = X[tr].mean(axis=0, dtype=np.float64).astype(np.float32)
    sd = X[tr].std(axis=0, dtype=np.float64).astype(np.float32)
    sd[sd < 1e-8] = 1.0
    Xs = (X - mu) / sd

    # ---- 3-class labels ----
    is_true = meta["label"] == 1
    y3 = np.zeros(len(X), dtype=np.int64)
    y3[is_true & (meta["simVxy"] < 1.0)] = 1
    y3[is_true & (meta["simVxy"] >= 1.0)] = 2
    n0, n1, n2 = [int((y3[tr] == k).sum()) for k in range(3)]
    w_pr = n0 / max(n1 + n2, 1)
    cls_w = np.array([1.0, w_pr, w_pr * args.disp_boost], dtype=np.float32)
    log(f"train class counts fake/prompt/disp = {n0}/{n1}/{n2}; "
        f"CE weights = {cls_w.tolist()}")

    Xtr = torch.tensor(np.ascontiguousarray(Xs[tr]))
    ytr = torch.tensor(np.ascontiguousarray(y3[tr]))
    Xva = torch.tensor(np.ascontiguousarray(Xs[va]))
    if device.type == "cuda":
        Xtr, ytr = Xtr.to(device), ytr.to(device)
    y3va, is_true_va, vxy_va = y3[va], is_true[va], meta["simVxy"][va]

    model = build_model3(X.shape[1]).to(device)

    # ---- warm start from the live 2-class v3 model ----
    warm = False
    if not args.no_warm_start and os.path.exists(args.warm_from):
        with open(args.warm_norm) as fh:
            wn = json.load(fh)
        assert wn["feature_names"] == names, "warm-start norm feature order mismatch"
        dmu, dsd = np.max(np.abs(np.array(wn["mean"]) - mu)), np.max(np.abs(np.array(wn["std"]) - sd))
        blob = torch.load(args.warm_from, map_location="cpu", weights_only=False)
        sd2 = blob["state_dict"]
        with torch.no_grad():
            model[0].weight.copy_(sd2["0.weight"])
            model[0].bias.copy_(sd2["0.bias"])
            model[2].weight.copy_(sd2["2.weight"])
            model[2].bias.copy_(sd2["2.bias"])
            w, b = sd2["4.weight"], sd2["4.bias"]      # [1,H], [1]
            model[4].weight.copy_(torch.cat([-0.5 * w, 0.5 * w, 0.5 * w], dim=0))
            model[4].bias.copy_(torch.cat([-0.5 * b, 0.5 * b, 0.5 * b], dim=0))
        warm = True
        log(f"WARM START from {args.warm_from} (v3 val AUC {blob.get('best_val_auc')}); "
            f"standardization drift vs warm norm: max|dmean|={dmu:.3e} max|dstd|={dsd:.3e} "
            f"(0 => margins are EXACTLY the v3 logit at epoch 0)")

    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    crit = torch.nn.CrossEntropyLoss(weight=torch.tensor(cls_w, device=device))

    def val_scores():
        lg = batched3(model, Xva, device)
        mg = margins(lg)
        a_all = roc_auc_score(is_true_va.astype(int), mg["mX"])
        m_p = (y3va != 2)
        a_pr = roc_auc_score((y3va[m_p] == 1).astype(int), mg["mX"][m_p])
        m_d = (y3va != 1)
        a_di = roc_auc_score((y3va[m_d] == 2).astype(int), mg["mD"][m_d])
        return a_all, a_pr, a_di

    gen = torch.Generator(device="cpu").manual_seed(args.seed)
    best, best_state, best_epoch, bad = -1e9, None, -1, 0
    n_tr = len(Xtr)
    hist = []
    for epoch in range(1, args.epochs + 1):
        model.train()
        perm = torch.randperm(n_tr, generator=gen)
        tot = 0.0
        for i in range(0, n_tr, args.batch_size):
            idx = perm[i:i + args.batch_size]
            if device.type == "cuda":
                idx = idx.to(device)
            opt.zero_grad()
            loss = crit(model(Xtr[idx]), ytr[idx])
            loss.backward()
            opt.step()
            tot += float(loss.detach()) * len(idx)
        a_all, a_pr, a_di = val_scores()
        sc = a_all + a_di           # checkpoint on BOTH heads (M6c min-style, summed)
        hist.append({"epoch": epoch, "loss": tot / n_tr, "auc_all_mX": a_all,
                     "auc_prompt_mX": a_pr, "auc_disp_mD": a_di, "sel": sc})
        log(f"epoch {epoch:2d} loss={tot / n_tr:.5f} val mX_all={a_all:.5f} "
            f"mX_prompt={a_pr:.5f} mD_disp={a_di:.5f} sel={sc:.5f}")
        if sc > best:
            best, best_epoch, bad = sc, epoch, 0
            best_state = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
        else:
            bad += 1
            if bad >= args.patience:
                log(f"early stop at {epoch} (best sel {best:.5f} @ {best_epoch})")
                break
        if args.wall_limit_sec > 0 and time.time() - T0 > args.wall_limit_sec:
            log(f"wall limit hit after epoch {epoch}")
            break
    model.load_state_dict(best_state)
    model.to(device)

    torch.save({"state_dict": best_state, "arch": [X.shape[1], 32, 32, 3],
                "feature_names": names, "seed": args.seed, "conditioning": conditioning,
                "best_epoch": best_epoch, "best_val_sel": float(best),
                "warm_start": warm, "history": hist}, args.out_model)
    with open(args.out_norm, "w") as fh:
        json.dump({"feature_names": names, "conditioning": conditioning,
                   "mean": mu.tolist(), "std": sd.tolist(), "seed": args.seed,
                   "class_weights": cls_w.tolist(),
                   "train_args": {"epochs": args.epochs, "patience": args.patience,
                                  "batch_size": args.batch_size, "lr": args.lr,
                                  "disp_boost": args.disp_boost, "warm_start": warm,
                                  "inputs": args.input,
                                  "pool_train_frac": args.pool_train_frac}}, fh, indent=1)
    log(f"saved {args.out_model} / {args.out_norm}")

    # ---------------------------------------------------------------- TEST (frozen)
    Xte = torch.tensor(np.ascontiguousarray(Xs[testm]))
    lg = batched3(model, Xte, device)
    mg = margins(lg)
    lab = meta["label"][testm]
    vxy = meta["simVxy"][testm]
    ety = meta["etype"][testm]
    T, F = lab == 1, lab != 1

    # CONTROL: the live 2-class v3 model on the SAME rows.
    ref = None
    if os.path.exists(args.warm_from):
        blob = torch.load(args.warm_from, map_location="cpu", weights_only=False)
        m2 = te.build_model(X.shape[1]).to(device)
        m2.load_state_dict(blob["state_dict"])
        ref = te.batched_scores(m2, Xte, device)

    print("\n=== FROZEN TEST: 3-class margins vs the live v3 binary logit (same rows) ===")
    res = {}
    subs = [("all", T, F),
            ("prompt vxy<1", T & (vxy < 1), F),
            ("displaced vxy>=1", T & (vxy >= 1), F),
            ("displaced vxy>=5", T & (vxy >= 5), F),
            ("E1 sharedMD", T & (ety == 1), F & (ety == 1)),
            ("E2 sharedLS", T & (ety == 2), F & (ety == 2))]
    for tag, mt, mf in subs:
        row = {}
        for key in ("mX", "mS", "mD", "mP"):
            row[key] = te.eval_block(f"{tag} [{key}]", mg[key][mt], mg[key][mf])
        if ref is not None:
            row["v3"] = te.eval_block(f"{tag} [v3 ref]", ref[mt], ref[mf])
        res[tag] = row
        print("")

    # THE OPERATIONAL QUESTION: at the SAME overall edge pass rate (the welding
    # eligibility cut), how many displaced-true edges survive under each score?
    print("=== welding-eligibility A/B: displaced-true edge efficiency at matched "
          "OVERALL edge pass rate (thetaEdge=0 on v3 sets the reference rate) ===")
    ref_thr = 0.0
    pass_rate = float((ref >= ref_thr).mean()) if ref is not None else 0.315
    elig = {}
    for key in ("mX", "mS", "mD"):
        thr = float(np.quantile(mg[key], 1.0 - pass_rate))
        p = mg[key] >= thr
        elig[key] = {"thr_at_matched_rate": thr, "pass_rate": float(p.mean()),
                     "true_eff": float(p[T].mean()),
                     "prompt_eff": float(p[T & (vxy < 1)].mean()),
                     "disp1_eff": float(p[T & (vxy >= 1)].mean()),
                     "disp5_eff": float(p[T & (vxy >= 5)].mean()),
                     "disp10_eff": float(p[T & (vxy >= 10)].mean()),
                     "fake_pass": float(p[F].mean())}
    if ref is not None:
        p = ref >= ref_thr
        elig["v3@0"] = {"thr_at_matched_rate": ref_thr, "pass_rate": float(p.mean()),
                        "true_eff": float(p[T].mean()),
                        "prompt_eff": float(p[T & (vxy < 1)].mean()),
                        "disp1_eff": float(p[T & (vxy >= 1)].mean()),
                        "disp5_eff": float(p[T & (vxy >= 5)].mean()),
                        "disp10_eff": float(p[T & (vxy >= 10)].mean()),
                        "fake_pass": float(p[F].mean())}
    print(f"{'score':>8} {'pass':>7} {'trueEff':>8} {'promptEff':>10} {'disp>=1':>8} "
          f"{'disp>=5':>8} {'disp>=10':>9} {'fakePass':>9}")
    for k, v in elig.items():
        print(f"{k:>8} {v['pass_rate']:7.4f} {v['true_eff']:8.4f} {v['prompt_eff']:10.4f} "
              f"{v['disp1_eff']:8.4f} {v['disp5_eff']:8.4f} {v['disp10_eff']:9.4f} "
              f"{v['fake_pass']:9.4f}")

    # The -ED OR-rescue: mX >= 0 OR mD >= thr_d.
    print("\n=== -ED displaced OR-rescue on top of mX>=0 (welding eligibility) ===")
    base = mg["mX"] >= 0.0
    rescue = {}
    print(f"{'thr_d':>7} {'pass':>7} {'trueEff':>8} {'disp>=1':>8} {'disp>=5':>8} "
          f"{'disp>=10':>9} {'fakePass':>9}")
    for td in (1e9, 0.0, -0.5, -1.0, -2.0):
        p = base | (mg["mD"] >= td)
        rescue[str(td)] = {"pass_rate": float(p.mean()), "true_eff": float(p[T].mean()),
                           "disp1_eff": float(p[T & (vxy >= 1)].mean()),
                           "disp5_eff": float(p[T & (vxy >= 5)].mean()),
                           "disp10_eff": float(p[T & (vxy >= 10)].mean()),
                           "fake_pass": float(p[F].mean())}
        v = rescue[str(td)]
        print(f"{td:7g} {v['pass_rate']:7.4f} {v['true_eff']:8.4f} {v['disp1_eff']:8.4f} "
              f"{v['disp5_eff']:8.4f} {v['disp10_eff']:9.4f} {v['fake_pass']:9.4f}")

    with open(args.out_metrics, "w") as fh:
        json.dump({"test_auc": res, "eligibility": elig, "ed_rescue": rescue,
                   "history": hist, "best_epoch": best_epoch,
                   "n_test_true": int(T.sum()), "n_test_fake": int(F.sum())}, fh, indent=1)
    log(f"wrote {args.out_metrics}; done")


if __name__ == "__main__":
    sys.exit(main() or 0)
