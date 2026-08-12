#!/usr/bin/env python3
"""S2: on-policy retrain of the 3-CLASS CHAIN GATE.

Architecture, conditioning, standardization, split discipline, class definition and
validation metric are all COPIED from prototype/train_chain3.py + chain3_norm_m12.json
(the shipped head's own recipe); the only things that change are

  * the rows: an ON-POLICY chains.bin dump of the arm being deployed, labelled offline by
    nnloop_ref/s2_work/truth.py (the same labelChainsHarness rule), and
  * --loss, which selects the class-weighting CONVENTION:
      m12  = the shipped tiered displaced weighting: w=1 fakes, w=pos_weight prompt-true,
             w=pos_weight*8 for true vxy in [1,5), w=pos_weight*16 for true vxy>=5,
             pos_weight = n_fake / n_true  (chain3_norm_m12.json displaced_weighting)
      lst  = LST's OWN T3-DNN convention (analysis/DNN/train_T3_DNN.ipynb
             calculate_class_weights): w[class] = total / (3 * count[class]), so the three
             classes carry EQUAL total loss weight.  Aggregation is losses.mean(), the
             notebook's own reduction, i.e. per-sample weight times mean -- NOT
             CrossEntropyLoss's weighted mean.
  * --sched: `const` reproduces the shipped lr 1e-3 / patience 15 / 120 epochs literally;
    `cos` is the converged schedule S1 proved is necessary (lr 3e-3 cosine -> 1e-5, no
    early stop, keep the best epoch by the val metric).

Val metric, verbatim from train_chain3.py:371 -- sel = min(AUC(mP: prompt vs fake),
AUC(mD: displaced vs fake)).
"""
import argparse
import copy
import json
import os
import time

import numpy as np

T0 = time.time()
HERE = os.path.dirname(os.path.abspath(__file__))

# The 25 ChainFeatures column names, in contract order (src/alpaka/Chain3NetworkWeights.h
# header comment / prototype/ChainFeatures.cc kChainFeatNames).
CF = ["nNodes", "nLayers", "sumEdgeLogit", "minEdgeLogit", "meanEdgeLogit",
      "fullFitChi2PerHit", "rzLineChi2PerHit", "fitKappa", "dKappaFitVsMedianT3", "ptEst",
      "innermostLayer", "layerSpan", "nPS", "nBarrel", "maxJunctionDegProduct",
      "chargeConsistency", "maxXyResid", "maxRzResid", "stdEdgeLogit", "maxBridgeChi2",
      "minT3FakeScore", "maxT3FakeScore", "meanT3PromptScore", "minT3DisplacedScore",
      "meanT3DisplacedScore"]
DROP = ["maxBridgeChi2"]          # train_chain3.py DEFAULT_DROP (the M12 judge spec)

# train_chain3.py CONDITIONING_SPEC, applied IN ORDER, BEFORE standardization.
COND = [
    {"feature": "cf_fullFitChi2PerHit", "op": "log10_1p"},
    {"feature": "cf_rzLineChi2PerHit", "op": "log10_1p"},
    {"feature": "cf_maxJunctionDegProduct", "op": "log10_1p"},
    {"feature": "cf_fitKappa", "op": "clip", "lo": -1.0, "hi": 1.0},
    {"feature": "cf_dKappaFitVsMedianT3", "op": "clip", "lo": -2.0, "hi": 2.0},
    {"feature": "cf_maxXyResid", "op": "log10_1p"},
    {"feature": "cf_maxRzResid", "op": "log10_1p"},
    {"feature": "cf_dcaXY", "op": "log10_1p"},
    {"feature": "cf_dcaXY", "op": "clip", "lo": 0.0, "hi": 1.4913617},
]


def log(m):
    print("[%7.1fs] %s" % (time.time() - T0, m), flush=True)


def build_inputs(lab_dir):
    """(X, names, meta) -- X in the exact NETWORK input order of the shipped head."""
    M = dict(np.load(os.path.join(lab_dir, "meta.npz")))
    F = np.load(os.path.join(lab_dir, "X.npy"))
    keep = [i for i, nm in enumerate(CF) if nm not in DROP]
    names = ["cf_" + CF[i] for i in keep] + ["cf_dcaXY"]
    X = np.empty((len(F), len(keep) + 1), dtype=np.float32)
    X[:, :len(keep)] = F[:, keep]
    X[:, len(keep)] = M["dcaXY"]
    nb = int((~np.isfinite(X)).sum())
    if nb:
        log("WARNING %d non-finite -> 0" % nb)
        np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
    for c in COND:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        else:
            X[:, j] = np.log10(1.0 + X[:, j])
    return X, names, M


def event_split(evt, seed, train_frac=0.6, val_frac=0.2):
    """train_chain3.py:154-168 verbatim (single-input branch)."""
    rng = np.random.default_rng(seed)
    key = evt.astype(np.uint64)
    uniq = np.unique(key)
    rng.shuffle(uniq)
    n = len(uniq)
    n_tr = int(round(train_frac * n))
    n_va = int(round(val_frac * n))
    tr = np.isin(key, uniq[:n_tr])
    va = np.isin(key, uniq[n_tr:n_tr + n_va])
    te = np.isin(key, uniq[n_tr + n_va:])
    log("event split: %d events -> %d/%d/%d -> %d/%d/%d chains"
        % (n, n_tr, n_va, n - n_tr - n_va, tr.sum(), va.sum(), te.sum()))
    return tr, va, te


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--lab", required=True)
    p.add_argument("--tag", required=True)
    p.add_argument("--loss", choices=["m12", "lst"], default="m12")
    p.add_argument("--sched", choices=["const", "cos"], default="const")
    p.add_argument("--epochs", type=int, default=0)
    p.add_argument("--patience", type=int, default=15)
    p.add_argument("--lr", type=float, default=0.0)
    p.add_argument("--hidden", type=int, default=32)
    p.add_argument("--batch-size", type=int, default=16384)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--dw-mid", type=float, default=8.0)
    p.add_argument("--dw-hi", type=float, default=16.0)
    a = p.parse_args()
    if a.epochs == 0:
        a.epochs = 120 if a.sched == "const" else 300
    if a.lr == 0.0:
        a.lr = 1e-3 if a.sched == "const" else 3e-3

    import torch
    np.random.seed(a.seed)
    torch.manual_seed(a.seed)
    torch.cuda.manual_seed_all(a.seed)
    torch.use_deterministic_algorithms(False)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log("tag=%s loss=%s sched=%s lr=%g epochs=%d seed=%d dev=%s"
        % (a.tag, a.loss, a.sched, a.lr, a.epochs, a.seed, dev))

    X, names, M = build_inputs(a.lab)
    tr, va, te = event_split(M["evt"], a.seed)
    mu = X[tr].mean(axis=0, dtype=np.float64).astype(np.float32)
    sd = X[tr].std(axis=0, dtype=np.float64).astype(np.float32)
    sd[sd < 1e-8] = 1.0
    Xs = (X - mu) / sd

    is_true = M["label"] == 1
    vxy = M["vxy"]
    y3 = np.zeros(len(X), dtype=np.int64)
    y3[is_true & (vxy < 1.0)] = 1
    y3[is_true & (vxy >= 1.0)] = 2
    n0, n1, n2 = [int((y3[tr] == k).sum()) for k in (0, 1, 2)]
    log("train class counts: fake=%d prompt=%d displaced=%d" % (n0, n1, n2))

    w = np.ones(len(X), dtype=np.float32)
    if a.loss == "m12":
        pw = n0 / max(n1 + n2, 1)
        w[is_true] = pw
        w[is_true & (vxy >= 1.0) & (vxy < 5.0)] = pw * a.dw_mid
        w[is_true & (vxy >= 5.0)] = pw * a.dw_hi
        spec = {"convention": "m12 tiered", "pos_weight": float(pw),
                "displaced_weight_mid": a.dw_mid, "displaced_weight_hi": a.dw_hi,
                "n_mid_train": int((is_true & (vxy >= 1) & (vxy < 5) & tr).sum()),
                "n_hi_train": int((is_true & (vxy >= 5) & tr).sum())}
        log("m12 weights: pos_weight=%.6f mid x%g hi x%g" % (pw, a.dw_mid, a.dw_hi))
    else:
        tot = n0 + n1 + n2
        cw = [tot / (3.0 * max(c, 1)) for c in (n0, n1, n2)]
        for k in (0, 1, 2):
            w[y3 == k] = cw[k]
        spec = {"convention": "LST T3-DNN equal class weight "
                             "(total/(3*count), analysis/DNN/train_T3_DNN.ipynb)",
                "class_weights": cw, "class_counts_train": [n0, n1, n2]}
        log("lst weights: %.4f / %.4f / %.4f (equal total loss weight per class)" % tuple(cw))
    # per-class TOTAL loss weight share on the train split, for the record
    shares = [float(w[tr][y3[tr] == k].sum()) for k in (0, 1, 2)]
    s = sum(shares)
    log("train loss-weight share  fake %.4f  prompt %.4f  displaced %.4f"
        % tuple(x / s for x in shares))
    spec["train_loss_weight_share"] = [x / s for x in shares]

    Xtr = torch.tensor(np.ascontiguousarray(Xs[tr])).to(dev)
    ytr = torch.tensor(np.ascontiguousarray(y3[tr])).to(dev)
    wtr = torch.tensor(np.ascontiguousarray(w[tr])).to(dev)
    Xva = torch.tensor(np.ascontiguousarray(Xs[va]))
    yva = y3[va]
    vf, vp, vd = yva == 0, yva == 1, yva == 2

    model = torch.nn.Sequential(torch.nn.Linear(X.shape[1], a.hidden), torch.nn.ReLU(),
                                torch.nn.Linear(a.hidden, a.hidden), torch.nn.ReLU(),
                                torch.nn.Linear(a.hidden, 3)).to(dev)
    opt = torch.optim.Adam(model.parameters(), lr=a.lr)
    sch = (torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=a.epochs, eta_min=1e-5)
           if a.sched == "cos" else None)
    crit = torch.nn.CrossEntropyLoss(reduction="none")
    gen = torch.Generator(device="cpu").manual_seed(a.seed)

    # This torch build has no NumPy bridge (tensor.numpy() raises), which is also why
    # train_chain3.py round-trips through .tolist(). Keeping the whole val metric in torch
    # is both a workaround and ~10x cheaper than tolist() on 1.4M rows every epoch.
    def torch_auc(pos, neg):
        """Mann-Whitney U with TIE-AVERAGED ranks -- exactly roc_auc_score."""
        x = torch.cat([pos, neg])
        n1, n2 = len(pos), len(neg)
        order = torch.argsort(x)
        xs = x[order]
        rk = torch.arange(1, n1 + n2 + 1, dtype=torch.float64, device=x.device)
        # average ranks inside runs of equal value
        uniq, inv, cnt = torch.unique(xs, return_inverse=True, return_counts=True)
        ssum = torch.zeros(len(uniq), dtype=torch.float64, device=x.device)
        ssum.scatter_add_(0, inv, rk)
        rk = (ssum / cnt.double())[inv]
        ispos = torch.zeros(n1 + n2, dtype=torch.bool, device=x.device)
        ispos[:n1] = True
        r1 = rk[ispos[order]].sum()
        return float((r1 - n1 * (n1 + 1) / 2.0) / (float(n1) * float(n2)))

    Xva_d = Xva.to(dev)
    vp_d = torch.tensor(np.ascontiguousarray(vp)).to(dev)
    vd_d = torch.tensor(np.ascontiguousarray(vd)).to(dev)
    vf_d = torch.tensor(np.ascontiguousarray(vf)).to(dev)

    def val_aucs():
        model.eval()
        with torch.no_grad():
            zs = []
            for i in range(0, len(Xva_d), 1 << 20):
                zs.append(model(Xva_d[i:i + (1 << 20)]).float())
            z = torch.cat(zs)
            mP = z[:, 1] - z[:, 0]
            mD = z[:, 2] - z[:, 0]
            return (torch_auc(mP[vp_d], mP[vf_d]), torch_auc(mD[vd_d], mD[vf_d]))

    best_sel, best_state, best_ep, bad, best_meta = -1.0, None, -1, 0, {}
    n_tr = len(Xtr)
    hist = []
    for ep in range(1, a.epochs + 1):
        model.train()
        perm = torch.randperm(n_tr, generator=gen).to(dev)
        tot_loss = 0.0
        for i in range(0, n_tr, a.batch_size):
            idx = perm[i:i + a.batch_size]
            opt.zero_grad()
            loss = (crit(model(Xtr[idx]), ytr[idx]) * wtr[idx]).mean()
            loss.backward()
            opt.step()
            tot_loss += float(loss.detach()) * len(idx)
        if sch is not None:
            sch.step()
        ap, ad = val_aucs()
        sel = min(ap, ad)
        hist.append([ep, tot_loss / n_tr, ap, ad])
        if ep % 5 == 0 or ep <= 3:
            log("epoch %3d loss=%.5f promptAUC=%.5f dispAUC=%.5f sel=%.5f%s"
                % (ep, tot_loss / n_tr, ap, ad, sel, "  *" if sel > best_sel else ""))
        if sel > best_sel:
            best_sel, best_ep, bad = sel, ep, 0
            best_meta = {"val_auc_prompt": ap, "val_auc_disp": ad}
            best_state = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
        else:
            bad += 1
            if a.sched == "const" and bad >= a.patience:
                log("early stop at epoch %d (best %.5f @ %d)" % (ep, best_sel, best_ep))
                break
    log("BEST epoch %d sel=%.5f %s" % (best_ep, best_sel, best_meta))

    os.makedirs(os.path.join(HERE, "models"), exist_ok=True)
    mp = os.path.join(HERE, "models", "chain3_%s.pt" % a.tag)
    import torch as _t
    _t.save({"state_dict": best_state, "arch": [X.shape[1], a.hidden, a.hidden, 3],
             "feature_names": names, "seed": a.seed, "conditioning": COND,
             "best_epoch": best_ep, "best_sel": float(best_sel),
             "best_val_meta": best_meta}, mp)
    np.save(os.path.join(HERE, "models", "hist_%s.npy" % a.tag), np.array(hist))
    nj = os.path.join(HERE, "models", "chain3_norm_%s.json" % a.tag)
    with open(nj, "w") as fh:
        json.dump({"feature_names": names, "conditioning": COND,
                   "mean": mu.tolist(), "std": sd.tolist(), "seed": a.seed,
                   "class_spec": {"0": "fake", "1": "prompt-true (simVxy<1)",
                                  "2": "displaced-true (simVxy>=1)"},
                   "displaced_weighting": spec,
                   "train_args": {"epochs": a.epochs, "patience": a.patience,
                                  "batch_size": a.batch_size, "lr": a.lr,
                                  "hidden": a.hidden, "sched": a.sched, "loss": a.loss,
                                  "inputs": [a.lab], "val_metric":
                                  "min(AUC(mP prompt-vs-fake), AUC(mD disp-vs-fake))"}}, fh, indent=1)
    log("saved %s + %s" % (mp, nj))


if __name__ == "__main__":
    main()
