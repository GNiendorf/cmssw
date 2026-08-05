#!/usr/bin/env python3
"""M15 angle C2 -- E2/T4-quality edge head.

Trains, in ONE data load and on the EXACT v3 data pipeline (M8 combination rule,
frozen test-60, same conditioning/standardization/pos_weight/optimizer/seed), a set
of architecture variants and evaluates them on the frozen TEST events with the same
eval_block used for v3, so the reported per-type AUCs are directly comparable to
v3's E1 0.97269 / E2 0.91723 / displaced 0.87114.

Variants (-V):
  ctrl    40-32-32-1 (v3 architecture) -- MATCHED-EPOCH control (v3 itself ran 100
          epochs; ctrl removes the epoch-count confound from the comparison)
  h1      shared trunk 40-32-32 + separate Linear(32,1) output heads for E1 / E2
  h2      shared trunk 40-32-32 + separate Linear(32,16)-ReLU-Linear(16,1) heads
  e2spec  40-32-32-1 trained on E2 ROWS ONLY -- the CEILING probe: a fully separate
          per-type network upper-bounds what any shared-trunk/separate-head split can
          buy from specialization (modulo a small multi-task transfer term)
  e1spec  40-32-32-1 trained on E1 rows only (symmetry check)

Everything except the model/rowset is byte-for-byte the v3 recipe (imports
train_edge.py directly so the data pipeline cannot drift).
"""

import argparse
import copy
import json
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import train_edge as te  # noqa: E402  (data pipeline reuse: same file, no drift)

T0 = time.time()


def log(msg):
    print(f"[{time.time() - T0:8.1f}s] {msg}", flush=True)


def parse_args():
    d = os.path.dirname(os.path.abspath(__file__))
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", nargs="+",
                   default=[f"{d}/edges_300evt.root", f"{d}/edges_498evt.root"])
    p.add_argument("--pool-train-frac", type=float, default=0.75)
    p.add_argument("--train-frac", type=float, default=0.6)
    p.add_argument("--val-frac", type=float, default=0.2)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--batch-size", type=int, default=65536)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--max-fakes", type=int, default=0)
    p.add_argument("-V", "--variants", default="e2spec,ctrl,h1",
                   help="comma list, trained in the given order")
    p.add_argument("--epochs", default="e2spec:60,e1spec:45,ctrl:30,h1:30,h2:30",
                   help="per-variant epoch budget, var:N comma list (or a bare int)")
    p.add_argument("--outdir", default=d)
    p.add_argument("--tag", default="c2")
    p.add_argument("--cache", default="",
                   help="npz cache of the loaded arrays (written if absent)")
    return p.parse_args()


# ----------------------------------------------------------------- models

def make_model(kind, n_in):
    import torch
    import torch.nn as nn

    class Plain(nn.Module):
        def __init__(self):
            super().__init__()
            self.net = nn.Sequential(nn.Linear(n_in, 32), nn.ReLU(),
                                     nn.Linear(32, 32), nn.ReLU(),
                                     nn.Linear(32, 1))

        def forward(self, x, et):
            return self.net(x)

    class TwoHead(nn.Module):
        """Shared trunk, per-etype output head. et is 1 (E1) or 2 (E2)."""

        def __init__(self, deep):
            super().__init__()
            self.trunk = nn.Sequential(nn.Linear(n_in, 32), nn.ReLU(),
                                       nn.Linear(32, 32), nn.ReLU())
            if deep:
                mk = lambda: nn.Sequential(nn.Linear(32, 16), nn.ReLU(), nn.Linear(16, 1))
            else:
                mk = lambda: nn.Linear(32, 1)
            self.head1 = mk()
            self.head2 = mk()

        def forward(self, x, et):
            t = self.trunk(x)
            o1 = self.head1(t)
            o2 = self.head2(t)
            return torch.where((et == 1).unsqueeze(1), o1, o2)

    class Big(nn.Module):
        """Capacity-ceiling probe: 6x the parameters of the production arch."""

        def __init__(self):
            super().__init__()
            self.net = nn.Sequential(nn.Linear(n_in, 64), nn.ReLU(),
                                     nn.Linear(64, 64), nn.ReLU(),
                                     nn.Linear(64, 64), nn.ReLU(),
                                     nn.Linear(64, 1))

        def forward(self, x, et):
            return self.net(x)

    if kind in ("ctrl", "e2spec", "e1spec"):
        return Plain()
    if kind in ("e2big", "e1big"):
        return Big()
    if kind == "h1":
        return TwoHead(False)
    if kind == "h2":
        return TwoHead(True)
    raise ValueError(kind)


def batched_scores(model, X_t, et_t, device, bs=1 << 20):
    import torch
    model.eval()
    out = np.empty(len(X_t), dtype=np.float32)
    with torch.no_grad():
        for i in range(0, len(X_t), bs):
            lg = model(X_t[i:i + bs].to(device),
                       et_t[i:i + bs].to(device)).squeeze(1).float().cpu()
            out[i:i + bs] = lg.tolist()
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

    ep_budget = {}
    if ":" in args.epochs:
        for kv in args.epochs.split(","):
            k, v = kv.split(":")
            ep_budget[k] = int(v)
    else:
        ep_budget = {k: int(args.epochs) for k in
                     ("ctrl", "h1", "h2", "e2spec", "e1spec")}

    # ---- data: EXACTLY the train_edge.py v3 pipeline -------------------------
    cache = args.cache
    if cache and os.path.exists(cache):
        z = np.load(cache, allow_pickle=True)
        X = z["X"]
        names = list(z["names"])
        meta = {k: z["m_" + k] for k in te.META_BRANCHES}
        src = z["src"]
        log(f"loaded cache {cache}: {X.shape}")
    else:
        metas, x_parts, src_parts = [], [], []
        names = edge_names = n_node = prim_evts = None
        for si, path in enumerate(args.input):
            meta_i, X_i, names_i, edge_names_i, n_node_i = te.load_dump(path)
            if si == 0:
                names, edge_names, n_node = names_i, edge_names_i, n_node_i
                prim_evts = np.unique(meta_i["evt"])
                log(f"input[0] PRIMARY {path}: {len(X_i)} edges over {len(prim_evts)} events")
            else:
                assert names_i == names
                keep = ~np.isin(meta_i["evt"], prim_evts)
                log(f"input[{si}] EXTRA {path}: keep {int(keep.sum())} of {len(X_i)} edges")
                meta_i = {k: v[keep] for k, v in meta_i.items()}
                X_i = X_i[keep]
            metas.append(meta_i)
            x_parts.append(X_i)
            src_parts.append(np.full(len(X_i), si, dtype=np.int8))
        meta = {k: np.concatenate([m[k] for m in metas]) for k in te.META_BRANCHES}
        X = np.concatenate(x_parts)
        src = np.concatenate(src_parts)
        del metas, x_parts, src_parts
        np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
        te.apply_conditioning(X, names, te.CONDITIONING_SPEC)
        if cache:
            log(f"writing cache {cache} ...")
            np.savez(cache, X=X, names=np.array(names), src=src,
                     **{"m_" + k: v for k, v in meta.items()})
    log(f"data ready: {X.shape}")
    if not [v for v in args.variants.split(",") if v.strip()]:
        log("no variants requested (cache-build only) -- exiting")
        return

    tr, va, test = te.combined_event_split(meta, src, args, rng)

    mu = X[tr].mean(axis=0, dtype=np.float64).astype(np.float32)
    sd = X[tr].std(axis=0, dtype=np.float64).astype(np.float32)
    sd[sd < 1e-8] = 1.0
    Xs = (X - mu) / sd
    del X

    y = (meta["label"] == 1).astype(np.float32)
    ety = meta["etype"].astype(np.int64)
    n_pos, n_neg = int(y[tr].sum()), int((1 - y[tr]).sum())
    pos_weight = n_neg / max(n_pos, 1)
    log(f"train: {n_pos} true / {n_neg} fake -> pos_weight={pos_weight:.4f}; "
        f"etype counts train E1={int((ety[tr] == 1).sum())} E2={int((ety[tr] == 2).sum())}")

    Xtr = torch.tensor(np.ascontiguousarray(Xs[tr])).to(device)
    ytr = torch.tensor(np.ascontiguousarray(y[tr])).to(device)
    ettr = torch.tensor(np.ascontiguousarray(ety[tr])).to(device)
    Xva = torch.tensor(np.ascontiguousarray(Xs[va]))
    etva = torch.tensor(np.ascontiguousarray(ety[va]))
    yva_np = y[va]
    etva_np = ety[va]
    Xte = torch.tensor(np.ascontiguousarray(Xs[test]))
    ette = torch.tensor(np.ascontiguousarray(ety[test]))
    del Xs
    lab_te = meta["label"][test]
    vxy_te = meta["simVxy"][test]
    ety_te = meta["etype"][test]
    log("tensors ready")

    results = {}
    for kind in args.variants.split(","):
        kind = kind.strip()
        if not kind:
            continue
        n_ep = ep_budget.get(kind, 30)
        # row subset for the *spec variants
        if kind in ("e2spec", "e2big"):
            sel = (ettr == 2)
            vsel_np = etva_np == 2
        elif kind in ("e1spec", "e1big"):
            sel = (ettr == 1)
            vsel_np = etva_np == 1
        else:
            sel = None
            vsel_np = np.ones(len(yva_np), dtype=bool)
        idx_all = torch.nonzero(sel).squeeze(1) if sel is not None else None
        n_tr = len(idx_all) if idx_all is not None else len(Xtr)

        torch.manual_seed(args.seed)
        model = make_model(kind, Xtr.shape[1]).to(device)
        npar = sum(p.numel() for p in model.parameters())
        opt = torch.optim.Adam(model.parameters(), lr=args.lr)
        # pos_weight: recomputed on the variant's own train rows (so the *spec
        # variants see the same class balance treatment as the pooled ones)
        if sel is not None:
            yy = ytr[idx_all]
            pw = float((1 - yy).sum() / max(float(yy.sum()), 1.0))
        else:
            pw = pos_weight
        crit = torch.nn.BCEWithLogitsLoss(
            pos_weight=torch.tensor(pw, device=device))
        gen = torch.Generator(device="cpu").manual_seed(args.seed)
        log(f"=== variant {kind}: {npar} params, {n_tr} train rows, "
            f"pos_weight={pw:.4f}, {n_ep} epochs")
        best_auc, best_state, best_epoch = -1.0, None, -1
        Xva_sub = Xva[torch.tensor(np.flatnonzero(vsel_np))] if sel is not None else Xva
        etva_sub = etva[torch.tensor(np.flatnonzero(vsel_np))] if sel is not None else etva
        yva_sub = yva_np[vsel_np]
        t_v0 = time.time()
        for epoch in range(1, n_ep + 1):
            model.train()
            perm = torch.randperm(n_tr, generator=gen)
            tot = 0.0
            for i in range(0, n_tr, args.batch_size):
                ii = perm[i:i + args.batch_size].to(device)
                idx = idx_all[ii] if idx_all is not None else ii
                opt.zero_grad()
                loss = crit(model(Xtr[idx], ettr[idx]).squeeze(1), ytr[idx])
                loss.backward()
                opt.step()
                tot += float(loss.detach()) * len(idx)
            s_va = batched_scores(model, Xva_sub, etva_sub, device)
            auc = roc_auc_score(yva_sub, s_va)
            if epoch % 5 == 0 or epoch == 1 or epoch == n_ep:
                log(f"  {kind} epoch {epoch:3d} loss={tot / n_tr:.5f} val_auc={auc:.5f}")
            if auc > best_auc:
                best_auc, best_epoch = auc, epoch
                best_state = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
        log(f"  {kind} best val AUC {best_auc:.5f} @ epoch {best_epoch} "
            f"({time.time() - t_v0:.0f}s, {(time.time() - t_v0) / n_ep:.1f}s/epoch)")
        model.load_state_dict(best_state)
        model.to(device)

        out_pt = os.path.join(args.outdir, f"edge_mlp_{args.tag}_{kind}.pt")
        arch = ([Xtr.shape[1], 64, 64, 64, 1] if kind in ("e2big", "e1big") else
                [Xtr.shape[1], 32, 32, 1] if kind in ("ctrl", "e2spec", "e1spec")
                else ["trunk", Xtr.shape[1], 32, 32, "heads",
                      (32, 1) if kind == "h1" else (32, 16, 1)])
        torch.save({"state_dict": best_state, "arch": arch, "kind": kind,
                    "feature_names": names, "seed": args.seed,
                    "conditioning": te.CONDITIONING_SPEC,
                    "best_epoch": best_epoch, "best_val_auc": float(best_auc)}, out_pt)
        out_norm = os.path.join(args.outdir, f"edge_norm_{args.tag}_{kind}.json")
        with open(out_norm, "w") as fh:
            json.dump({"feature_names": names, "conditioning": te.CONDITIONING_SPEC,
                       "mean": mu.tolist(), "std": sd.tolist(), "seed": args.seed,
                       "kind": kind, "arch": [a if not isinstance(a, tuple) else list(a)
                                              for a in arch],
                       "train_args": {"epochs": n_ep, "batch_size": args.batch_size,
                                      "lr": args.lr, "inputs": args.input,
                                      "pool_train_frac": args.pool_train_frac}}, fh, indent=1)

        # ---- frozen-TEST evaluation, identical blocks to train_edge.py -------
        s_te = batched_scores(model, Xte, ette, device)
        is_true = lab_te == 1
        is_fake = ~is_true
        print(f"\n=== {kind} on frozen TEST events ===")
        r = {}
        if sel is None:
            s_fake_all = s_te[is_fake]
            r["all"] = te.eval_block("all", s_te[is_true], s_fake_all)
            r["prompt"] = te.eval_block("prompt vxy<1", s_te[is_true & (vxy_te < 1)], s_fake_all)
            r["disp1"] = te.eval_block("displaced vxy>=1", s_te[is_true & (vxy_te >= 1)], s_fake_all)
            r["disp5"] = te.eval_block("displaced vxy>=5", s_te[is_true & (vxy_te >= 5)], s_fake_all)
        for et, tag in ((1, "E1 sharedMD"), (2, "E2 sharedLS")):
            if kind in ("e2spec", "e2big") and et == 1:
                continue
            if kind in ("e1spec", "e1big") and et == 2:
                continue
            m_et = ety_te == et
            r[tag] = te.eval_block(tag, s_te[is_true & m_et], s_te[is_fake & m_et])
            # per-type displaced slice (the T4-quality question)
            for lo, tg in ((1.0, "disp>=1"), (5.0, "disp>=5")):
                mm = m_et & (vxy_te >= lo)
                r[f"{tag} {tg}"] = te.eval_block(f"{tag} {tg}", s_te[is_true & mm],
                                                 s_te[is_fake & m_et])
        results[kind] = r
        np.save(os.path.join(args.outdir, f"scores_{args.tag}_{kind}.npy"), s_te)
        with open(os.path.join(args.outdir, f"results_{args.tag}.json"), "w") as fh:
            json.dump(results, fh, indent=1)

    log("done")


if __name__ == "__main__":
    main()
