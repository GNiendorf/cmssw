#!/usr/bin/env python3
"""Train the otpull pair network (LST OT-seeded lookup, NeuralNetwork.h otpulldnn) -- the recipe of the chain attach
head (standalone/attach_report_ref/REPORT_ATTACH.tex) and the other LST networks.

Rows: make_rows.py outputs of on-policy dumps (dump.patch build, every pair that reaches the network, the decision
itself not applied to the dumped set; false pairs kept 1 in 1/falsePairKeep with weight 1/falsePairKeep).
Recipe:
  * samples: --ref (PU200 ttbar) plus enrichment samples with a share of the total train loss weight (--share qcd=0.25)
  * label: the T3 (>= 5 of 6 OT hits) and the cell (both pixel hits) come from the same simulated particle (in-time
    pileup counts as true)
  * weights: dump sampling weight (and --false-keep subsampling of train-split false pairs); true pairs of displaced particles x8 (vxy 1-5 cm) and x16 (vxy >= 5 cm); class
    balance by pos_weight = (weighted false) / (weighted true) on the reference train split; focal factor
    sigmoid(z)^gamma on false pairs; no pT weights (high pT is handled by the working-point rows, wp_otpull.py)
  * 60/20/20 split by event per sample (seed 42); clip at the reference train split's [q, 1-q] quantiles and
    standardise there (export_otpull.py folds the standardisation into layer 1 and writes the clip bounds)
  * 15 -> 32 -> 32 -> 1 ReLU MLP, Adam lr 3e-3 cosine to 1e-5, batch 16384; keeps the epoch with the best worst-sample
    validation AUC (sampling-weighted)
Example (see README.md):
  python3 train_otpull.py --lab ttbar='rows/ttbar_*.npz' --lab qcd='rows/qcd_*.npz' --ref ttbar --share qcd=0.25 \
      --tag v1 --out models
"""
import argparse
import json
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import FEATURES, NIN, load_rows, split_events, to_np, to_t  # noqa: E402

T0 = time.time()


def log(m):
    print("[%7.1fs] %s" % (time.time() - T0, m), flush=True)


def wauc(s, y, w):
    """weighted ROC AUC (ties broken by order; scores are continuous)"""
    o = np.argsort(s, kind="stable")
    y, w = y[o], w[o]
    wp, wn = np.where(y, w, 0.0), np.where(y, 0.0, w)
    cn = np.cumsum(wn) - wn  # false weight strictly below
    return float((wp * (cn + 0.5 * wn)).sum() / max(wp.sum() * wn.sum(), 1e-30))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lab", action="append", required=True, help="NAME=glob of make_rows.py npz")
    ap.add_argument("--ref", default="ttbar")
    ap.add_argument("--share", action="append", default=[], help="NAME=frac of the total train loss weight")
    ap.add_argument("--epochs", type=int, default=120)
    ap.add_argument("--lr", type=float, default=3e-3)
    ap.add_argument("--lr-min", type=float, default=1e-5)
    ap.add_argument("--batch", type=int, default=16384)
    ap.add_argument("--hidden", type=int, default=32)
    ap.add_argument("--gamma", type=float, default=2.0, help="focal exponent on false pairs")
    ap.add_argument("--disp-mid", type=float, default=8.0)
    ap.add_argument("--disp-hi", type=float, default=16.0)
    ap.add_argument("--clip-q", type=float, default=5e-4)
    ap.add_argument("--false-keep", type=float, default=1.0, help="train-split false-pair subsampling")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--device", default="cuda:0")
    ap.add_argument("--out", default="models")
    ap.add_argument("--tag", required=True)
    a = ap.parse_args()

    import torch
    torch.manual_seed(a.seed)
    dev = torch.device(a.device if torch.cuda.is_available() else "cpu")
    labs = dict(x.split("=", 1) for x in a.lab)
    shares = {k: float(v) for k, v in (x.split("=", 1) for x in a.share)}
    assert a.ref in labs and a.ref not in shares and sum(shares.values()) < 1
    S, prov = {}, {}
    for nm, pat in labs.items():
        R, pv = load_rows(pat)
        prov[nm] = pv
        good = np.isfinite(R["X"]).all(axis=1)
        part, (ev, ntr, nva) = split_events(R["ev"], a.seed)
        # train split only: false pairs kept 1 in 1/false_keep with weight 1/false_keep (val/test keep every row)
        drop = (part == 0) & (R["y"] == 0) & (np.random.default_rng(a.seed + 1).random(len(part)) >= a.false_keep)
        good &= ~drop
        R["w"] = np.where((part == 0) & (R["y"] == 0), R["w"] / a.false_keep, R["w"]).astype(np.float32)
        R = {k: v[good] for k, v in R.items()}
        R["part"] = part[good]
        S[nm] = R
        y = R["y"] > 0
        log(f"{nm}: {len(y)} rows ({(~good).sum()} non-finite dropped), true {y.sum()} (weighted false "
            f"{R['w'][~y].sum():.4g}), events {len(ev)} (train {ntr}, val {nva}, test {len(ev) - ntr - nva})")

    # weights: sampling x displaced tiers; pos_weight on the reference train split; shares
    W = {}
    for nm, R in S.items():
        w = R["w"].astype(np.float64).copy()
        t = R["y"] > 0
        w[t & (R["tpvxy"] >= 1.0) & (R["tpvxy"] < 5.0)] *= a.disp_mid
        w[t & (R["tpvxy"] >= 5.0)] *= a.disp_hi
        W[nm] = w
    rtr = S[a.ref]["part"] == 0
    yref = S[a.ref]["y"][rtr] > 0
    W_pos, W_neg = W[a.ref][rtr][yref].sum(), W[a.ref][rtr][~yref].sum()
    pos_weight = W_neg / max(W_pos, 1e-30)
    log(f"reference {a.ref}: weighted true {W_pos:.4g}, false {W_neg:.4g} -> pos_weight {pos_weight:.3f}")
    # share: the total (class-balanced) train weight of a sample relative to the reference
    def tot(nm):
        R = S[nm]
        tr = R["part"] == 0
        t = R["y"][tr] > 0
        return W[nm][tr][t].sum() * pos_weight + W[nm][tr][~t].sum()
    tref = tot(a.ref)
    ssum = sum(shares.values())
    for nm in S:
        if nm == a.ref:
            continue
        f = (shares.get(nm, 0.0) / (1.0 - ssum)) * tref / max(tot(nm), 1e-30)
        W[nm] *= f
        log(f"share {nm}: {shares.get(nm, 0.0)} -> scale {f:.5f}")
    T = sum(tot(nm) for nm in S)
    log("train loss-weight shares: " + ", ".join(f"{nm} {tot(nm) / T:.3f}" for nm in S))

    # clip + standardise on the reference train split
    Xr = S[a.ref]["X"][rtr].astype(np.float64)
    lo = np.quantile(Xr, a.clip_q, axis=0)
    hi = np.quantile(Xr, 1.0 - a.clip_q, axis=0)
    Xc = np.clip(Xr, lo, hi)
    mu, sd = Xc.mean(axis=0), Xc.std(axis=0)
    sd[sd < 1e-9] = 1.0
    for i, n in enumerate(FEATURES):
        log(f"  input {i:2d} {n:12s} clip [{lo[i]:.4g}, {hi[i]:.4g}] mean {mu[i]:.4g} std {sd[i]:.4g}")

    def prep(X):
        return ((np.clip(X, lo, hi) - mu) / sd).astype(np.float32)

    tr = {nm: S[nm]["part"] == 0 for nm in S}
    Xtr = to_t(np.concatenate([prep(S[nm]["X"][tr[nm]]) for nm in S]), dev)
    ytr = to_t(np.concatenate([(S[nm]["y"][tr[nm]] > 0).astype(np.float32) for nm in S]), dev)
    wtr_np = np.concatenate([W[nm][tr[nm]] for nm in S]).astype(np.float32)
    wtr = to_t(wtr_np / wtr_np.mean(), dev)
    VA = {}
    for nm in S:
        m = S[nm]["part"] == 1
        VA[nm] = (to_t(prep(S[nm]["X"][m]), dev), (S[nm]["y"][m] > 0), S[nm]["w"][m].astype(np.float64))
    log(f"train rows {len(wtr_np)}; val rows " + ", ".join(f"{k} {len(v[1])}" for k, v in VA.items()))

    model = torch.nn.Sequential(torch.nn.Linear(NIN, a.hidden), torch.nn.ReLU(), torch.nn.Linear(a.hidden, a.hidden),
                                torch.nn.ReLU(), torch.nn.Linear(a.hidden, 1)).to(dev)
    opt = torch.optim.Adam(model.parameters(), lr=a.lr)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=a.epochs, eta_min=a.lr_min)
    crit = torch.nn.BCEWithLogitsLoss(pos_weight=torch.tensor(pos_weight, device=dev, dtype=torch.float32),
                                      reduction="none")

    @torch.no_grad()
    def scores(Xv):
        model.eval()
        return np.concatenate([to_np(model(Xv[i:i + (1 << 20)])[:, 0]) for i in range(0, Xv.shape[0], 1 << 20)])

    best, best_ep, best_state, hist = -1.0, -1, None, []
    n = Xtr.shape[0]
    gen = torch.Generator(device="cpu").manual_seed(a.seed)
    for ep in range(a.epochs):
        model.train()
        pm = torch.randperm(n, generator=gen).to(dev)
        tl = torch.zeros((), device=dev)
        tw = 0
        for i in range(0, n, a.batch):
            idx = pm[i:i + a.batch]
            z = model(Xtr[idx])[:, 0]
            yb, wb = ytr[idx], wtr[idx]
            base = crit(z, yb)
            fw = torch.where(yb > 0.5, torch.ones_like(z), torch.sigmoid(z.detach()).pow(a.gamma))
            loss = (base * wb * fw).sum() / (wb * fw).sum().clamp_min(1e-12)
            opt.zero_grad(set_to_none=True)
            loss.backward()
            opt.step()
            tl += loss.detach() * len(idx)
            tw += len(idx)
        sched.step()
        tl = float(tl)
        res = {nm: wauc(scores(Xv), yv, wv) for nm, (Xv, yv, wv) in VA.items()}
        sel = min(res.values())
        hist.append(dict(ep=ep, loss=tl / tw, sel=sel, **res))
        if sel > best:
            best, best_ep = sel, ep
            best_state = {k: v.detach().clone() for k, v in model.state_dict().items()}
        if ep % 10 == 0 or ep == a.epochs - 1:
            log(f"ep {ep:3d} loss {tl / tw:.5f} sel {sel:.6f} best {best:.6f}@{best_ep} | " +
                " ".join(f"{k} {v:.6f}" for k, v in res.items()))
    model.load_state_dict(best_state)
    model.eval()
    test = {}
    for nm in S:
        m = S[nm]["part"] == 2
        test[nm] = wauc(scores(to_t(prep(S[nm]["X"][m]), dev)), S[nm]["y"][m] > 0, S[nm]["w"][m].astype(np.float64))
    log(f"best epoch {best_ep}, val selector {best:.6f}; test AUC " + ", ".join(f"{k} {v:.6f}" for k, v in test.items()))

    os.makedirs(a.out, exist_ok=True)
    sd_ = {k: to_np(v).tolist() for k, v in model.state_dict().items()}
    split = {nm: {"seed": a.seed, "n_events": int(len(np.unique(S[nm]["ev"])))} for nm in S}
    meta = dict(tag=a.tag, args=vars(a), features=FEATURES, clip_lo=lo.tolist(), clip_hi=hi.tolist(), mean=mu.tolist(),
                std=sd.tolist(), arch=[NIN, a.hidden, a.hidden, 1], pos_weight=pos_weight, best_epoch=best_ep,
                best_val_sel=best, test_auc=test, split=split, provenance=prov, history=hist, state_dict=sd_)
    fn = os.path.join(a.out, f"otpull_{a.tag}.json")
    json.dump(meta, open(fn, "w"))
    log(f"saved {fn}")


if __name__ == "__main__":
    main()
