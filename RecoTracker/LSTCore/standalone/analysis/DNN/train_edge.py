#!/usr/bin/env python3
"""Train the edge head: the MLP that scores a triplet-to-triplet edge before the weld.

WHAT IT TRAINS
    A 40 -> hidden -> hidden -> 1 MLP on edge rows dumped from the binary, with BCEWithLogits at
    pos_weight = nFake/nTrue, Adam, standardization fitted on the TRAIN rows only, an event-level
    60/20/20 split, and early stopping on validation AUC.  The conditioning (log10p1 / clip) is
    already applied when the cache is built, so this script sees plain float columns.
    The input width 40 is the deployed contract, asserted in C++ as
    dnn::edgemlp::kInput == 2 * Params_ChainNode::kFeatures + kChainEdgeFeatures = 13 + 13 + 14:
    the inner node's features, the outer node's, then the edge's (ni_00..ni_12, no_00..no_12,
    ef_00..ef_13, in that column order).  40 is hardcoded in `gather` and `build_model`, so a
    cache of any other width is a silent mis-read rather than an error.

INPUTS (NOT IN THE REPOSITORY)
    A prepared row cache in `cache/` next to this script:
      cache/X.npy     float32 (nRows, 40) feature matrix, read memory-mapped
      cache/meta.npz  per-row columns: `evt_off` (row offset of each event, length nEvents + 1;
                      one event's rows are contiguous, which is what makes an event-level split
                      possible), `label` (1 = true edge), `logit` (the score the deployed head
                      gave that same row), `type` (edge family: 1 = shares a mini-doublet,
                      2 = shares a line segment), `ptbin` / `etabin` (the 2 x 10 working-point
                      cell), `simPt`, `simVxy`.
    The rows come from the binary's own sidecars -- LST_CHAIN_FEAT_DUMP taps the 14 edge features
    of every scored edge, LST_CHAIN_EDGE_DUMP writes the surviving edges with their log-odds --
    joined offline to the node feature table and to sim truth.  Neither the dumps nor the cache
    builder are part of this directory, so the script cannot run on a fresh checkout.

OUTPUTS
    models/edge_<tag>.pt         best-val-AUC weights, the standardization (`mu`, `sd`), the event
                                 split and the full history
    models/edge_<tag>_fprsel.pt  the epoch with the best deployed-metric ratio instead; written
                                 only when --fpr-every actually evaluated that metric
    models/edge_<tag>.json       history, the per-cell bar table, the validation summary
    export_edge_weights.py writes src/alpaka/EdgeNetworkWeights.h from a checkpoint plus a
    working-point table json.  That exporter expects the deployed three-class output layer
    (kOutputs = 3) and a --table file; this trainer emits the single-logit form and no table, so
    the two are not plug-compatible as they stand.

RUN
    python3 train_edge.py --tag NAME [--lr 1e-3] [--patience 8] [--epochs 100]
        [--batch-size 65536] [--seed 42] [--hidden 32] [--fpr-every 10] [--max-train-events N]
        [--cosine LR_MIN] [--tier-weight a1,a2 | --displaced-weight]

METRICS
    Per epoch: train loss and validation AUC.  Every --fpr-every epochs and at the end, also the
    deployed metric: fake-weighted false-positive rate at per-cell signal efficiency MATCHED to
    the deployed head's own dumped logits on the SAME rows, with the two edge families tabulated
    separately.  AUC ranks globally while the deployed decision is a per-cell threshold, so the
    two can disagree; both selections are saved rather than one being chosen here.
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
# The deployed weld bar of each edge family: E1 inherits thetaEdge (0), E2 uses thetaEdgeE2 (-2).
SHIP_BAR = {1: 0.0, 2: -2.0}


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
    """Concatenate the row blocks of the given events, on `dev` if one is given.

    The events are sorted first so the memory-mapped cache is read strictly forwards.
    """
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


def build_model(nin, hidden=32):
    return nn.Sequential(nn.Linear(nin, hidden), nn.ReLU(),
                         nn.Linear(hidden, hidden), nn.ReLU(),
                         nn.Linear(hidden, 1))


@torch.no_grad()
def scores(model, X, bs=1 << 21):
    model.eval()
    out = torch.empty(len(X), dtype=torch.float32, device=X.device)
    for i in range(0, len(X), bs):
        out[i:i + bs] = model(X[i:i + bs]).squeeze(1)
    return out


def fast_auc(s, y):
    """AUC via rank sum, computed on whatever device the scores live on."""
    order = torch.argsort(s)
    ys = y[order].double()
    n = len(ys)
    ranks = torch.arange(1, n + 1, device=s.device, dtype=torch.float64)
    npos = ys.sum()
    nneg = n - npos
    return float((ranks[ys > 0].sum() - npos * (npos + 1) / 2) / (npos * nneg))


def fpr_table(s_new, s_ship, y, fam, ptb, etb, min_true=20):
    """Fake-weighted FPR at matched per-cell signal efficiency, E1 and E2 tabulated SEPARATELY.

    For each (family, ptbin, etabin) cell: measure the fraction of that cell's true edges the
    deployed head admits at its own bar, then take the quantile of the new head's true-edge
    scores that reproduces the same fraction.  Signal efficiency is pinned by construction, so
    only the false-positive rate is free to move with head quality.  A cell with fewer than
    `min_true` true edges (or no fakes) is reported without a bar and keeps the deployed one; it
    also drops out of the fake-count-weighted totals returned alongside the table.
    """
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
    p.add_argument("--max-train-events", type=int, default=0,
                   help="0 = every event of the train split; a smaller number is a quick probe")
    p.add_argument("--cosine", type=float, default=0.0,
                   help="if > 0: cosine-decay the LR from --lr down to this over --epochs")
    p.add_argument("--tier-weight", default="",
                   help="\"a1,a2\": TIERED true-edge weights -- 1 for vxy<1cm and for pileup-only "
                        "trues, a1 for vxy in [1,5), a2 for vxy>=5. Fakes keep 1.")
    p.add_argument("--displaced-weight", action="store_true",
                   help="inverse-frequency true-edge weighting: each true edge is weighted by the "
                        "inverse frequency of its coarse joint log10(simPt) x vxy-class bin among "
                        "the TRAIN trues, clipped to [1,20] and renormalised to mean 1 over the "
                        "train trues; fakes keep weight 1. Off by default.")
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
    log("rows: train %d (true %.4f) / val %d (true %.4f)" %
        (mtr.sum(), ytr_np.mean(), mva.sum(), yva_np.mean()))

    Xtr = gather(X, off, tr_ev, dev)
    log("train tensor on %s: %.1f GB" % (dev, Xtr.numel() * 4 / 1e9))
    # Mean and variance are accumulated in float64 over chunks: a whole-tensor
    # .mean(dtype=float64) would materialise a float64 copy of the entire train tensor, which is
    # tens of GB and does not fit next to the tensor itself.
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
    ytr = torch.from_numpy(ytr_np.astype(np.float32)).to(dev)
    yva = torch.from_numpy(yva_np.astype(np.float32)).to(dev)

    npos, nneg = float(ytr.sum()), float((1 - ytr).sum())
    pos_weight = nneg / max(npos, 1)
    log("pos_weight=%.4f" % pos_weight)

    # Optional per-row weighting of the TRUE edges, in two mutually exclusive forms. With neither,
    # pos_weight above is the only thing separating the classes and every true edge counts alike.
    wtr = None
    wspec = None
    if args.tier_weight:
        # Tiered displaced weighting: a true edge weighs 1 for vxy < 1, a1 for vxy in [1,5) and a2
        # for vxy >= 5; fakes keep 1. A pileup-only true edge carries simVxy == -999 (no accepted
        # sim, so no kinematics) and therefore lands in the first tier, i.e. it is treated as
        # prompt. Deliberately NOT mean-normalized: the tiers are meant to multiply pos_weight, and
        # rescaling them to mean 1 would undo the boost that was asked for.
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
    elif args.displaced_weight:
        # Inverse-frequency weighting over the joint (log10 simPt, vxy class) grid. The bin index
        # packs three vxy classes per pt bin, so bin // 3 is the pt bin and bin % 3 the vxy class;
        # the weight of an occupied bin is the count of the fullest bin over its own, clipped, and
        # the whole set is then renormalised to mean 1 so the true/fake balance stays pos_weight.
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

    model = build_model(40, args.hidden).to(dev)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    crit = nn.BCEWithLogitsLoss(pos_weight=torch.tensor(pos_weight, device=dev),
                                reduction="none" if wtr is not None else "mean")
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
            loss = crit(model(Xtr[idx]).squeeze(1), ytr[idx])
            if wtr is not None:
                loss = (loss * wtr[idx]).mean()
            loss.backward()
            opt.step()
            tot += float(loss.detach()) * len(idx)
        if sched is not None:
            sched.step()
        s = scores(model, Xva)
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
    s = scores(model, Xva).cpu().numpy()
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
