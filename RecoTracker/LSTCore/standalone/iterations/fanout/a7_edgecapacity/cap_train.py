#!/usr/bin/env python3
"""ANGLE 7 step 2: edge-classifier CAPACITY + E2-HEAD variants, trained on the cached
v3 pipeline (cap_prep.py).

Two models trained side by side on IDENTICAL batches (same generator, same
pos_weight, same optimizer settings, same data as edge_mlp_v3):

  A "wide"  : 40 -> 64 -> 64 -> 1                     (v3 shape doubled; 6785 params)
  B "e2head": trunk 40 -> 64 (ReLU), then TWO heads
              64 -> 64 -> 1 selected by edge type (E1 sharedMD / E2 sharedLS)
              (10945 params; the trunk is shared, discrimination is per-etype)

Chunked execution: --wall-limit-sec saves state and exit(3) after the first epoch
past the limit; rerun the same command to resume exactly (10-minute foreground cap).
"""
import argparse
import copy
import json
import os
import sys
import time

import numpy as np

D = os.path.dirname(os.path.abspath(__file__))
T0 = time.time()


def log(m):
    print(f"[{time.time() - T0:7.1f}s] {m}", flush=True)


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--epochs", type=int, default=60)
    p.add_argument("--patience", type=int, default=8)
    p.add_argument("--batch-size", type=int, default=65536)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--state-file", default=f"{D}/cap_state.pt")
    p.add_argument("--wall-limit-sec", type=float, default=460.0)
    p.add_argument("--cpu-train-data", action="store_true",
                   help="keep the train matrix in host RAM (GPU OOM fallback)")
    return p.parse_args()


def build_models(n_in, hid=64):
    import torch
    import torch.nn as nn

    wide = nn.Sequential(nn.Linear(n_in, hid), nn.ReLU(),
                         nn.Linear(hid, hid), nn.ReLU(),
                         nn.Linear(hid, 1))

    class E2Head(nn.Module):
        """Shared trunk 40->hid (ReLU); two heads hid->hid->1 keyed by edge type."""

        def __init__(self):
            super().__init__()
            self.trunk = nn.Sequential(nn.Linear(n_in, hid), nn.ReLU())
            self.head1 = nn.Sequential(nn.Linear(hid, hid), nn.ReLU(), nn.Linear(hid, 1))
            self.head2 = nn.Sequential(nn.Linear(hid, hid), nn.ReLU(), nn.Linear(hid, 1))

        def forward(self, x, is_e2):
            h = self.trunk(x)
            out = torch.empty(len(x), 1, device=x.device, dtype=h.dtype)
            m2 = is_e2
            m1 = ~is_e2
            if m1.any():
                out[m1] = self.head1(h[m1])
            if m2.any():
                out[m2] = self.head2(h[m2])
            return out

    return wide, E2Head()


def main():
    args = parse_args()
    import torch
    from sklearn.metrics import roc_auc_score

    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.cuda.manual_seed_all(args.seed)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log(f"device={dev}")

    X = np.load(f"{D}/cache_X.npy", mmap_mode="r")
    M = np.load(f"{D}/cache_meta.npz")
    n_tr, n_va, n_te = [int(v) for v in M["bounds"]]
    n_in = X.shape[1]
    lab = M["label"].astype(np.float32)
    e2 = (M["etype"] == 2)
    log(f"cache: {X.shape} train/val/test={n_tr}/{n_va}/{n_te}")

    Xtr = torch.tensor(np.array(X[:n_tr]))
    ytr = torch.tensor(lab[:n_tr])
    e2tr = torch.tensor(e2[:n_tr])
    if dev.type == "cuda" and not args.cpu_train_data:
        try:
            Xtr, ytr, e2tr = Xtr.to(dev), ytr.to(dev), e2tr.to(dev)
        except RuntimeError as ex:
            log(f"GPU OOM moving train data ({ex}); falling back to host RAM")
            torch.cuda.empty_cache()
    Xva = torch.tensor(np.array(X[n_tr:n_tr + n_va]))
    e2va = torch.tensor(e2[n_tr:n_tr + n_va])
    yva = lab[n_tr:n_tr + n_va]
    log("train/val tensors ready")

    n_pos = float(ytr.sum())
    pos_weight = (n_tr - n_pos) / max(n_pos, 1)
    log(f"train {int(n_pos)} true / {n_tr - int(n_pos)} fake -> pos_weight={pos_weight:.4f}")

    wide, e2h = build_models(n_in)
    wide, e2h = wide.to(dev), e2h.to(dev)
    nparA = sum(p.numel() for p in wide.parameters())
    nparB = sum(p.numel() for p in e2h.parameters())
    log(f"params: wide={nparA} e2head={nparB}")
    optA = torch.optim.Adam(wide.parameters(), lr=args.lr)
    optB = torch.optim.Adam(e2h.parameters(), lr=args.lr)
    crit = torch.nn.BCEWithLogitsLoss(pos_weight=torch.tensor(pos_weight, device=dev))

    gen = torch.Generator(device="cpu").manual_seed(args.seed)
    best = {"A": [-1.0, None, -1, 0], "B": [-1.0, None, -1, 0]}  # auc,state,epoch,bad
    start = 1
    if os.path.exists(args.state_file):
        st = torch.load(args.state_file, map_location="cpu", weights_only=False)
        wide.load_state_dict(st["A"]); e2h.load_state_dict(st["B"])
        wide.to(dev); e2h.to(dev)
        optA.load_state_dict(st["optA"]); optB.load_state_dict(st["optB"])
        gen.set_state(st["gen"]); best = st["best"]; start = st["epoch"] + 1
        log(f"RESUMED: next epoch {start}; best A {best['A'][0]:.5f}@{best['A'][2]} "
            f"B {best['B'][0]:.5f}@{best['B'][2]}")

    def score(model, Xs, e2s, is_b):
        model.eval()
        out = np.empty(len(Xs), dtype=np.float32)
        bs = 1 << 20
        with torch.no_grad():
            for i in range(0, len(Xs), bs):
                xb = Xs[i:i + bs].to(dev)
                if is_b:
                    lg = model(xb, e2s[i:i + bs].to(dev))
                else:
                    lg = model(xb)
                out[i:i + bs] = lg.squeeze(1).float().cpu().tolist()
        return out

    for epoch in range(start, args.epochs + 1):
        wide.train(); e2h.train()
        perm = torch.randperm(n_tr, generator=gen)
        la = lb = 0.0
        for i in range(0, n_tr, args.batch_size):
            idx = perm[i:i + args.batch_size]
            if Xtr.is_cuda:
                idx = idx.to(dev)
            xb, yb, eb = Xtr[idx], ytr[idx], e2tr[idx]
            if not Xtr.is_cuda:
                xb, yb, eb = xb.to(dev), yb.to(dev), eb.to(dev)
            optA.zero_grad()
            lossA = crit(wide(xb).squeeze(1), yb)
            lossA.backward(); optA.step()
            optB.zero_grad()
            lossB = crit(e2h(xb, eb).squeeze(1), yb)
            lossB.backward(); optB.step()
            la += float(lossA.detach()) * len(idx)
            lb += float(lossB.detach()) * len(idx)
        aucA = roc_auc_score(yva, score(wide, Xva, e2va, False))
        aucB = roc_auc_score(yva, score(e2h, Xva, e2va, True))
        log(f"epoch {epoch:3d} lossA={la/n_tr:.5f} aucA={aucA:.5f} | "
            f"lossB={lb/n_tr:.5f} aucB={aucB:.5f}")
        stop = True
        for tag, auc, model in (("A", aucA, wide), ("B", aucB, e2h)):
            b = best[tag]
            if auc > b[0]:
                b[0], b[2], b[3] = auc, epoch, 0
                b[1] = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
            else:
                b[3] += 1
            if b[3] < args.patience:
                stop = False
        tmp = args.state_file + ".tmp"
        torch.save({"A": {k: v.cpu() for k, v in wide.state_dict().items()},
                    "B": {k: v.cpu() for k, v in e2h.state_dict().items()},
                    "optA": optA.state_dict(), "optB": optB.state_dict(),
                    "gen": gen.get_state(), "best": best, "epoch": epoch}, tmp)
        os.replace(tmp, args.state_file)
        if stop:
            log(f"early stop at epoch {epoch}")
            break
        if args.wall_limit_sec > 0 and time.time() - T0 > args.wall_limit_sec \
                and epoch < args.epochs:
            log(f"wall limit after epoch {epoch}; state saved -> exit(3)")
            sys.exit(3)

    wide.load_state_dict(best["A"][1]); wide.to(dev)
    e2h.load_state_dict(best["B"][1]); e2h.to(dev)
    torch.save({"state_dict": best["A"][1], "arch": [n_in, 64, 64, 1],
                "feature_names": json.load(open(f"{D}/cache_norm.json"))["feature_names"],
                "seed": args.seed, "conditioning": json.load(
                    open(f"{D}/cache_norm.json"))["conditioning"],
                "best_epoch": best["A"][2], "best_val_auc": float(best["A"][0])},
               f"{D}/edge_mlp_a7wide.pt")
    torch.save({"state_dict": best["B"][1], "arch": [n_in, 64, 64, 1, "e2head"],
                "seed": args.seed, "best_epoch": best["B"][2],
                "best_val_auc": float(best["B"][0])}, f"{D}/edge_mlp_a7e2h.pt")
    cn = json.load(open(f"{D}/cache_norm.json"))
    for tag in ("a7wide", "a7e2h"):
        json.dump({"feature_names": cn["feature_names"], "conditioning": cn["conditioning"],
                   "mean": cn["mean"], "std": cn["std"], "seed": args.seed,
                   "degenerate_columns": [],
                   "train_args": {"epochs": args.epochs, "batch_size": args.batch_size,
                                  "lr": args.lr, "arch": "40-64-64-1" if tag == "a7wide"
                                  else "trunk40-64 + 2x(64-64-1) by etype"}},
                  open(f"{D}/edge_norm_{tag}.json", "w"), indent=1)
    log("saved models + norms")

    # ---------------- TEST evaluation (same blocks as train_edge.py) --------------
    Xte = torch.tensor(np.array(X[n_tr + n_va:]))
    e2te = torch.tensor(e2[n_tr + n_va:])
    labte = M["label"][n_tr + n_va:]
    vxy = M["simVxy"][n_tr + n_va:]
    ety = M["etype"][n_tr + n_va:]
    isT = labte == 1
    isF = ~isT

    import torch.nn as nn
    v3 = nn.Sequential(nn.Linear(n_in, 32), nn.ReLU(), nn.Linear(32, 32), nn.ReLU(),
                       nn.Linear(32, 1))
    blob = torch.load(f"{D}/edge_mlp_v3.pt", map_location="cpu", weights_only=False)
    v3.load_state_dict(blob["state_dict"]); v3.to(dev)

    def rej(sT, sF, e):
        thr = float(np.quantile(sT, 1.0 - e, method="lower"))
        return float((sF < thr).mean())

    scores = {"v3": score(v3, Xte, e2te, False),
              "wide": score(wide, Xte, e2te, False),
              "e2head": score(e2h, Xte, e2te, True)}
    res = {}
    print("\n=== TEST (60 frozen primary events, 6.22M edges) ===")
    print(f"{'model':>8} {'block':>20} {'n_true':>9} {'AUC':>8} {'rej@99%':>8} "
          f"{'rej@99.9%':>9}")
    blocks = [("all", isT, isF),
              ("prompt vxy<1", isT & (vxy < 1), isF),
              ("displaced vxy>=1", isT & (vxy >= 1), isF),
              ("displaced vxy>=5", isT & (vxy >= 5), isF),
              ("E1 sharedMD", isT & (ety == 1), isF & (ety == 1)),
              ("E2 sharedLS", isT & (ety == 2), isF & (ety == 2))]
    for mtag, s in scores.items():
        res[mtag] = {}
        for btag, mT, mF in blocks:
            sT, sF = s[mT], s[mF]
            y = np.concatenate([np.ones(len(sT)), np.zeros(len(sF))])
            auc = float(roc_auc_score(y, np.concatenate([sT, sF])))
            r99, r999 = rej(sT, sF, 0.99), rej(sT, sF, 0.999)
            res[mtag][btag] = {"auc": auc, "n_true": int(len(sT)),
                               "rej@0.99": r99, "rej@0.999": r999}
            print(f"{mtag:>8} {btag:>20} {len(sT):>9d} {auc:>8.5f} {r99:>8.5f} "
                  f"{r999:>9.5f}")

    # ------- formation-margin population (P1): true displaced edges near logit 0 -----
    print("\n=== FORMATION MARGIN: logit distribution of TRUE edges (thetaEdge=0 cut) ===")
    marg = {}
    for mtag, s in scores.items():
        marg[mtag] = {}
        for btag, m in (("prompt", isT & (vxy < 1)), ("disp>=1", isT & (vxy >= 1)),
                        ("disp>=5", isT & (vxy >= 5)), ("fake", isF)):
            v = s[m]
            d = {"n": int(len(v)), "frac_neg": float((v < 0).mean()),
                 "frac_absmargin_lt1": float((np.abs(v) < 1).mean()),
                 "frac_absmargin_lt2": float((np.abs(v) < 2).mean()),
                 "median": float(np.median(v)),
                 "q05": float(np.quantile(v, 0.05)),
                 "q25": float(np.quantile(v, 0.25))}
            marg[mtag][btag] = d
            print(f"  {mtag:>8} {btag:>8} n={d['n']:>8d} frac(logit<0)={d['frac_neg']:.4f} "
                  f"frac(|l|<1)={d['frac_absmargin_lt1']:.4f} "
                  f"frac(|l|<2)={d['frac_absmargin_lt2']:.4f} "
                  f"med={d['median']:+.2f} q05={d['q05']:+.2f}")
    json.dump({"test": res, "margin": marg,
               "best_val_auc": {"wide": best["A"][0], "e2head": best["B"][0]},
               "best_epoch": {"wide": best["A"][2], "e2head": best["B"][2]},
               "params": {"wide": nparA, "e2head": nparB, "v3": 2401}},
              open(f"{D}/cap_metrics.json", "w"), indent=1)
    log("wrote cap_metrics.json")


if __name__ == "__main__":
    main()
