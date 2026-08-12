#!/usr/bin/env python3
"""S1 ARM F: THREE-CLASS edge head on the T3-DNN pattern (maintainer directive, 21:33).

Classes mirror the 3-class chain gate: 0 = fake, 1 = prompt true (shared-sim vxy < 1 cm, and
pileup-only trues with no accepted-sim kinematics), 2 = displaced true (vxy >= 1 cm).
Arch [40, 32, 32, 3], cross-entropy, NO class reweighting (D/E showed weighting hurts the
argmax ranking). Everything else is arm C's converged recipe.

Model selection: val AUC of mX = max(zP, zD) - zF against the binary true/fake label, which is
the scalar the rest of the pipeline would consume.

(derived from train_s1.py)

Recipe = the SHIPPED v3 recipe (prototype/edge_norm_v3.json train_args + analysis/DNN/
train_edge.py): arch [40,32,32,1], conditioning applied at cache build, standardization
from TRAIN rows, BCEWithLogits with pos_weight = nFake/nTrue, Adam, batch 65536, seed 42,
event-level 60/20/20 split, early stop on val AUC.  displaced_weight = false (shipped).

Only lr / patience / epochs are searched (--lr --patience --epochs), per the task.

Reports, per epoch: train loss, val AUC.  Every --fpr-every epochs and at the end: the
DEPLOYED metric -- fake-weighted FPR at MATCHED per-bin signal efficiency against the
shipped head's own dumped logits, on the SAME rows (val for model selection evidence).
"""
import argparse
import json
import os
import sys
import time

import numpy as np
import torch
import torch.nn as nn

T0 = time.time()
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cache")
NBIN_PT, NBIN_ETA = 2, 10
SHIP_BAR = {1: 0.0, 2: -2.0}  # thetaEdge=0 (E1 inherits), thetaEdgeE2=-2.0


def log(m):
    print("[%8.1fs] %s" % (time.time() - T0, m), flush=True)


def event_split(nev, seed, train_frac=0.6, val_frac=0.2):
    rng = np.random.default_rng(seed)
    uniq = np.arange(nev)
    rng.shuffle(uniq)
    n_tr = int(round(train_frac * nev))
    n_va = int(round(val_frac * nev))
    return uniq[:n_tr], uniq[n_tr:n_tr + n_va], uniq[n_tr + n_va:]


def gather(X, off, evs, dev=None, dtype=torch.float32):
    """concatenate the row blocks of the given events; to `dev` if not None."""
    evs = np.sort(evs)
    n = int(sum(off[e + 1] - off[e] for e in evs))
    out = torch.empty((n, 40), dtype=dtype, device=dev if dev else "cpu")
    p = 0
    for e in evs:
        a, b = int(off[e]), int(off[e + 1])
        blk = torch.from_numpy(np.ascontiguousarray(X[a:b]))
        out[p:p + (b - a)] = blk.to(out.device, non_blocking=True)
        p += b - a
    assert p == n
    return out


def row_mask(off, evs, ntot):
    m = np.zeros(ntot, dtype=bool)
    for e in np.sort(evs):
        m[int(off[e]):int(off[e + 1])] = True
    return m


def build_model(nin, hidden=32, nout=3):
    return nn.Sequential(nn.Linear(nin, hidden), nn.ReLU(),
                         nn.Linear(hidden, hidden), nn.ReLU(),
                         nn.Linear(hidden, nout))


@torch.no_grad()
def scores3(model, X, bs=1 << 21):
    """raw 3 logits (no softmax; the gate works on logit margins and so does this)."""
    model.eval()
    out = torch.empty((len(X), 3), dtype=torch.float32, device=X.device)
    for i in range(0, len(X), bs):
        out[i:i + bs] = model(X[i:i + bs])
    return out


def mX_of(z):
    return torch.maximum(z[:, 1], z[:, 2]) - z[:, 0]


def fast_auc(s, y):
    """AUC via rank sum, on GPU."""
    order = torch.argsort(s)
    ys = y[order].double()
    n = len(ys)
    ranks = torch.arange(1, n + 1, device=s.device, dtype=torch.float64)
    npos = ys.sum()
    nneg = n - npos
    return float((ranks[ys > 0].sum() - npos * (npos + 1) / 2) / (npos * nneg))


def fpr_table(s_new, s_ship, y, fam, ptb, etb, min_true=20):
    """fake-weighted FPR at matched per-bin signal efficiency, E1 and E2 tables SEPARATE."""
    rep = []
    for f in (1, 2):
        for a in range(NBIN_PT):
            for j in range(NBIN_ETA):
                b = (fam == f) & (ptb == a) & (etb == j)
                t = b & (y == 1)
                fk = b & (y == 0)
                nT, nF = int(t.sum()), int(fk.sum())
                if nT < min_true or nF == 0:
                    rep.append({"fam": f, "pt": a, "eta": j, "nTrue": nT, "nFake": nF,
                                "bar": SHIP_BAR[f], "note": "nTrue<%d -> shipped bar kept" % min_true})
                    continue
                eff = float((s_ship[t] >= SHIP_BAR[f]).mean())
                if eff >= 1.0:
                    nb = -1e9
                elif eff <= 0.0:
                    nb = 1e9
                else:
                    nb = float(np.quantile(s_new[t], 1.0 - eff))
                rep.append({"fam": f, "pt": a, "eta": j, "nTrue": nT, "nFake": nF,
                            "target_eff": eff, "ship_bar": SHIP_BAR[f], "bar": nb,
                            "eff_new": float((s_new[t] >= nb).mean()),
                            "fpr_shipped": float((s_ship[fk] >= SHIP_BAR[f]).mean()),
                            "fpr_new": float((s_new[fk] >= nb).mean())})
    ok = [r for r in rep if "fpr_new" in r]
    nFt = sum(r["nFake"] for r in ok)
    wS = sum(r["fpr_shipped"] * r["nFake"] for r in ok) / nFt
    wN = sum(r["fpr_new"] * r["nFake"] for r in ok) / nFt
    return rep, wS, wN, wN / max(wS, 1e-12)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--tag", required=True)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--patience", type=int, default=8)
    p.add_argument("--epochs", type=int, default=100)
    p.add_argument("--batch-size", type=int, default=65536)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--hidden", type=int, default=32)
    p.add_argument("--fpr-every", type=int, default=10)
    p.add_argument("--max-train-events", type=int, default=0, help="0 = all (600)")
    p.add_argument("--cosine", type=float, default=0.0,
                   help="if > 0: cosine-decay the LR from --lr down to this over --epochs")
    p.add_argument("--class-balance", action="store_true",
                   help="LST's own T3-DNN weighting: per-sample weight N/(3*n_class) so the three "
                        "classes contribute equally (analysis/DNN/train_T3_DNN.ipynb)")
    p.add_argument("--tier-weight", default="",
                   help="\"a1,a2\": TIERED true-edge weights -- 1 for vxy<1cm and for pileup-only "
                        "trues, a1 for vxy in [1,5), a2 for vxy>=5. Fakes keep 1.")
    p.add_argument("--displaced-weight", action="store_true",
                   help="enable the GOLDEN script's own true-edge weighting (analysis/DNN/"
                        "train_edge.py displaced_weights: inverse frequency of the coarse joint "
                        "log10(simPt) x vxy-class bin among TRAIN trues, clipped [1,20], mean-1 "
                        "over train trues; fakes keep weight 1). v3 shipped with this OFF.")
    args = p.parse_args()

    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.cuda.manual_seed_all(args.seed)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log("tag=%s lr=%g patience=%d epochs=%d device=%s" % (args.tag, args.lr, args.patience,
                                                          args.epochs, dev))

    M = np.load(os.path.join(CACHE, "meta.npz"))
    off = M["evt_off"]
    ntot = int(off[-1])
    X = np.load(os.path.join(CACHE, "X.npy"), mmap_mode="r")
    tr_ev, va_ev, te_ev = event_split(len(off) - 1, args.seed)
    if args.max_train_events:
        tr_ev = tr_ev[:args.max_train_events]
    lab = M["label"]
    log("event split: %d/%d/%d events" % (len(tr_ev), len(va_ev), len(te_ev)))

    mtr = row_mask(off, tr_ev, ntot)
    mva = row_mask(off, va_ev, ntot)
    ytr_np = lab[mtr]
    yva_np = lab[mva]
    vxy_tr = M["simVxy"][mtr]
    vxy_va = M["simVxy"][mva]
    c_tr = np.zeros(len(ytr_np), dtype=np.int64)
    c_tr[(ytr_np == 1) & (vxy_tr < 1.0)] = 1
    c_tr[(ytr_np == 1) & (vxy_tr >= 1.0)] = 2
    c_va = np.zeros(len(yva_np), dtype=np.int64)
    c_va[(yva_np == 1) & (vxy_va < 1.0)] = 1
    c_va[(yva_np == 1) & (vxy_va >= 1.0)] = 2
    log("classes train: fake %d prompt %d displaced %d" %
        ((c_tr == 0).sum(), (c_tr == 1).sum(), (c_tr == 2).sum()))
    log("rows: train %d (true %.4f) / val %d (true %.4f)" %
        (mtr.sum(), ytr_np.mean(), mva.sum(), yva_np.mean()))

    Xtr = gather(X, off, tr_ev, dev)
    log("train tensor on %s: %.1f GB" % (dev, Xtr.numel() * 4 / 1e9))
    # chunked float64 accumulation: a whole-tensor .mean(dtype=float64) would allocate a
    # 20 GB float64 copy and OOM the L4.
    s1 = torch.zeros(40, dtype=torch.float64, device=Xtr.device)
    s2 = torch.zeros(40, dtype=torch.float64, device=Xtr.device)
    CH = 1 << 22
    for i in range(0, len(Xtr), CH):
        c = Xtr[i:i + CH].double()
        s1 += c.sum(dim=0)
        s2 += (c * c).sum(dim=0)
        del c
    nrow = float(len(Xtr))
    mu64 = s1 / nrow
    var = torch.clamp(s2 / nrow - mu64 * mu64, min=0.0)
    mu = mu64.float()
    sd = torch.sqrt(var).float()
    sd[sd < 1e-8] = 1.0
    Xtr.sub_(mu).div_(sd)
    Xva = gather(X, off, va_ev, dev)
    Xva.sub_(mu).div_(sd)
    ytr = torch.from_numpy(c_tr).to(dev)
    yva = torch.from_numpy(yva_np.astype(np.float32)).to(dev)

    # LST's OWN T3-DNN convention, read out of g3's analysis/DNN/train_T3_DNN.ipynb (maintainer
    # refinement 22:05): WeightedCrossEntropyLoss with a per-SAMPLE weight
    #     class_weights = total_samples / (3 * class_counts);  w_i = class_weights[class(i)]
    # and mean reduction -- i.e. each of the three CLASSES contributes equally to the objective.
    # The notebook's net is Linear(in,32) -> ReLU -> Linear(32,32) -> ReLU -> Linear(32,3) with a
    # softmax in forward() and log(p + 1e-7) in the loss; nn.CrossEntropyLoss(weight=...) is the
    # numerically stable form of exactly that objective (log-softmax fused), which is what I use.
    cw = None
    if args.class_balance:
        cnt = np.bincount(c_tr, minlength=3).astype(np.float64)
        cwn = len(c_tr) / (3.0 * cnt)
        cw = torch.tensor(cwn, dtype=torch.float32, device=dev)
        log("LST T3-DNN class balancing: counts %s -> weights %s (mean sample weight %.4f)" %
            (cnt.astype(int).tolist(), np.round(cwn, 4).tolist(),
             float((cwn[c_tr]).mean())))
    else:
        log("3-class cross-entropy, NO class reweighting")

    # GOLDEN-script true-edge weighting (analysis/DNN/train_edge.py displaced_weights), verbatim
    # constants: inner log10(simPt) edges, vxy classes, clip, mean-1 over TRAIN trues.
    wtr = None
    wspec = None
    if False and args.tier_weight:
        # Coordinator's arm (21:08): TIERED displaced positive weighting, the same shape the M19
        # experiment used (iterations/fanout5/edgeretrain/er_train_edge.py: T(vxy) = 1 / a1 / a2 on
        # vxy < 1 / [1,5) / >= 5, fakes weight 1) but with a1/a2 given here. A pileup-only true edge
        # (simVxy == -999, no accepted-sim kinematics) gets weight 1, i.e. it is treated as prompt.
        # NOT mean-normalized: the tiers are the requested pos_weight multipliers.
        a1, a2 = (float(x) for x in args.tier_weight.split(","))
        vxy_tr = M["simVxy"][mtr]
        w = np.ones(len(vxy_tr), dtype=np.float64)
        ist = ytr_np == 1
        w[ist & (vxy_tr >= 1.0) & (vxy_tr < 5.0)] = a1
        w[ist & (vxy_tr >= 5.0)] = a2
        wtr = torch.from_numpy(w.astype(np.float32)).to(dev)
        n1 = int((ist & (vxy_tr >= 1.0) & (vxy_tr < 5.0)).sum())
        n2 = int((ist & (vxy_tr >= 5.0)).sum())
        n0 = int(ist.sum()) - n1 - n2
        wspec = {"kind": "tier", "a1": a1, "a2": a2, "n_true_prompt_or_pileup": n0,
                 "n_true_vxy_1_5": n1, "n_true_vxy_ge5": n2,
                 "mean_true_weight": float(w[ist].mean())}
        log("TIER weighting: prompt/pileup %d w1 | vxy[1,5) %d w%.1f | vxy>=5 %d w%.1f | mean true w %.3f"
            % (n0, n1, a1, n2, a2, w[ist].mean()))
    elif False and args.displaced_weight:
        LPT_EDGES = [0.0, 0.5, 1.0, 1.5, 2.0]
        VXY_EDGES = [1.0, 5.0]
        W_CLIP = (1.0, 20.0)
        spt_tr = M["simPt"][mtr]
        vxy_tr = M["simVxy"][mtr]
        lpt = np.log10(np.maximum(spt_tr, 1e-6))
        b = np.digitize(lpt, LPT_EDGES) * (len(VXY_EDGES) + 1) + np.digitize(vxy_tr, VXY_EDGES)
        nb = (len(LPT_EDGES) + 1) * (len(VXY_EDGES) + 1)
        ist = ytr_np == 1
        cnt = np.bincount(b[ist], minlength=nb).astype(np.float64)
        w_bin = np.full(nb, W_CLIP[1])
        occ = cnt > 0
        w_bin[occ] = np.clip(cnt.max() / cnt[occ], W_CLIP[0], W_CLIP[1])
        w = np.ones(len(b), dtype=np.float64)
        w[ist] = w_bin[b[ist]]
        scale = float(w[ist].mean())
        w[ist] /= scale
        wtr = torch.from_numpy(w.astype(np.float32)).to(dev)
        wspec = {"lpt_edges": LPT_EDGES, "vxy_edges": VXY_EDGES, "clip": list(W_CLIP),
                 "bin_counts_train_true": cnt.tolist(), "bin_weight_raw": w_bin.tolist(),
                 "norm_scale": scale, "bin_weight_final": (w_bin / scale).tolist()}
        log("displaced weighting ON: %d bins, raw w in [%.2f,%.2f], mean-1 scale %.4f" %
            (nb, w_bin[occ].min(), w_bin[occ].max(), scale))
        for k in range(nb):
            if cnt[k]:
                log("  bin %2d (lpt %d, vxy class %d): n_train_true %10d  w %.4f" %
                    (k, k // 3, k % 3, int(cnt[k]), w_bin[k] / scale))

    model = build_model(40, args.hidden, 3).to(dev)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    crit = nn.CrossEntropyLoss(weight=cw)
    gen = torch.Generator(device="cpu").manual_seed(args.seed)

    ship_va = M["logit"][mva]
    fam_va = M["type"][mva]
    ptb_va = M["ptbin"][mva]
    etb_va = M["etabin"][mva]

    best = {"auc": -1.0, "epoch": -1}
    best_state = None
    bestf = {"ratio": 1e9, "epoch": -1}
    bestf_state = None
    bad = 0
    hist = []
    n = len(Xtr)
    sched = None
    if args.cosine > 0:
        sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs, eta_min=args.cosine)
    for ep in range(1, args.epochs + 1):
        model.train()
        perm = torch.randperm(n, generator=gen).to(dev)
        tot = 0.0
        for i in range(0, n, args.batch_size):
            idx = perm[i:i + args.batch_size]
            opt.zero_grad(set_to_none=True)
            loss = crit(model(Xtr[idx]), ytr[idx])
            loss.backward()
            opt.step()
            tot += float(loss.detach()) * len(idx)
        if sched is not None:
            sched.step()
        z = scores3(model, Xva)
        s = mX_of(z)
        auc = fast_auc(s, yva)
        row = {"epoch": ep, "loss": tot / n, "val_auc": auc}
        if args.fpr_every and (ep % args.fpr_every == 0):
            sn = s.cpu().numpy()
            _, ws, wn, ratio = fpr_table(sn, ship_va, yva_np, fam_va, ptb_va, etb_va)
            row.update({"fpr_ratio": ratio, "fpr_ship": ws, "fpr_new": wn})
            if ratio < bestf["ratio"]:
                bestf = {"ratio": ratio, "epoch": ep, "auc": auc}
                bestf_state = {k: v.detach().clone() for k, v in model.state_dict().items()}
        hist.append(row)
        log("epoch %3d loss %.6f val_auc %.6f%s" %
            (ep, row["loss"], auc, "  FPRratio %.4f" % row["fpr_ratio"] if "fpr_ratio" in row else ""))
        if auc > best["auc"]:
            best = {"auc": auc, "epoch": ep}
            best_state = {k: v.detach().clone() for k, v in model.state_dict().items()}
            bad = 0
        else:
            bad += 1
            if bad >= args.patience:
                log("early stop at epoch %d (best %d, auc %.6f)" % (ep, best["epoch"], best["auc"]))
                break

    model.load_state_dict(best_state)
    s = mX_of(scores3(model, Xva)).cpu().numpy()
    rep, ws, wn, ratio = fpr_table(s, ship_va, yva_np, fam_va, ptb_va, etb_va)
    log("BEST epoch %d val_auc %.6f | VAL FPR at matched per-bin eff: ship %.6f new %.6f ratio %.4f"
        % (best["epoch"], best["auc"], ws, wn, ratio))
    ship_auc = fast_auc(torch.from_numpy(ship_va.astype(np.float32)).to(dev), yva)
    log("shipped head val AUC on the same rows: %.6f" % ship_auc)

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "models")
    os.makedirs(out, exist_ok=True)
    if bestf_state is not None:
        torch.save({"state_dict": {k: v.cpu() for k, v in bestf_state.items()},
                    "mu": mu.cpu().numpy(), "sd": sd.cpu().numpy(),
                    "arch": [40, args.hidden, args.hidden, 1],
                    "best_epoch": bestf["epoch"], "best_val_auc": bestf.get("auc", -1.0),
                    "args": vars(args), "hist": hist, "selected_on": "val_fpr_ratio",
                    "val_fpr_ratio": bestf["ratio"],
                    "split": {"train": tr_ev.tolist(), "val": va_ev.tolist(), "test": te_ev.tolist()}},
                   os.path.join(out, "edge_%s_fprsel.pt" % args.tag))
        log("FPR-selected checkpoint: epoch %d ratio %.4f (auc %.6f)" %
            (bestf["epoch"], bestf["ratio"], bestf.get("auc", -1.0)))
    torch.save({"state_dict": {k: v.cpu() for k, v in model.state_dict().items()},
                "mu": mu.cpu().numpy(), "sd": sd.cpu().numpy(),
                "arch": [40, args.hidden, args.hidden, 1],
                "best_epoch": best["epoch"], "best_val_auc": best["auc"],
                "args": vars(args), "hist": hist, "weight_spec": wspec,
                "val_fpr_ratio": ratio, "val_fpr_ship": ws, "val_fpr_new": wn,
                "ship_val_auc": ship_auc,
                "split": {"train": tr_ev.tolist(), "val": va_ev.tolist(), "test": te_ev.tolist()}},
               os.path.join(out, "edge_%s.pt" % args.tag))
    with open(os.path.join(out, "edge_%s.json" % args.tag), "w") as fh:
        json.dump({"hist": hist, "best": best, "val_fpr": {"ship": ws, "new": wn, "ratio": ratio},
                   "ship_val_auc": ship_auc, "args": vars(args), "bins": rep}, fh, indent=1)
    log("wrote models/edge_%s.pt" % args.tag)


if __name__ == "__main__":
    main()
