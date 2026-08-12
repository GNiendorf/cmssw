#!/usr/bin/env python3
"""S3 attach-head trainer -- 22 inputs, `Chain2NetworkWeights.h` DELETED, no surrogate.

Inputs come from the s3label.py columns over the on-policy round-3 pair dump
(nnloop_ref/round3, binary md5 0a49ac71980c8d85a2b8c4f17d2f1d44 = shipped + armG + GM12F +
the pair-dump instrument).  Head input vector, 22 wide:

    0..10   the dumped x[0..10] VERBATIM -- already standardized by the SHIPPED attach_norm
            constants, so those eleven slots keep the shipped conditioning bit for bit and no
            de-standardize/re-standardize round trip can introduce error
    11,12,13  the target chain's THREE RAW 3-class gate logits zFake/zPrompt/zDisp, standardized
            with constants fitted HERE (no clip, no log10p1 -- the same conditioning the deleted
            slot 11 `af_chainGateLogit` had).  A bare-T3 target has no chain gate, so all three
            take the 0 sentinel, the literal extension of the shipped `attachStdz<11>(0.f)`.
    14..21  the dumped x[11..19] VERBATIM (old slots 12..19)

Two output arms, both judged DEPLOYED:
    --arm scalar   one logit, as shipped
    --arm three    three logits (fake / prompt-true / displaced-true); the DEPLOYED scalar is
                   logsumexp(zP, zD) - zF, i.e. log-odds of "true of either kind vs fake".
                   logsumexp not max(): max() discards WHICH class won, which is exactly the
                   defect that made the deleted 2-class gateLogit2 score .336 (inverted) on
                   displaced-vs-prompt.  Optionally affine-pinned to the shipped logit scale.

Objective: the SHIPPED M19 recipe by default (r1_ref/r1_train.py + attach_norm_MIN1.json's own
`m19_objective`): wgt-weighted BCE with pos_weight, ONE-SIDED focal gamma_neg=2 on negatives,
displaced TRUE tiers disp_mid=8 / disp_hi=16 on the shared sim's vxy, chain_weight=1, early stop
on the wgt-weighted CHAIN-universe val AUC.  `--sched cos` swaps the constant lr for a cosine
decay, which is the one lever S1/S2/B1 all found the shipped recipe was missing.
"""
import argparse
import copy
import json
import os
import time

import numpy as np

T0 = time.time()


def log(m):
    print("[%7.1fs] %s" % (time.time() - T0, m), flush=True)


def wauc(s_pos, w_pos, s_neg, w_neg):
    from sklearn.metrics import roc_auc_score
    if len(s_pos) == 0 or len(s_neg) == 0:
        return None
    y = np.concatenate([np.ones(len(s_pos)), np.zeros(len(s_neg))])
    s = np.concatenate([s_pos, s_neg])
    w = np.concatenate([w_pos, w_neg])
    return float(roc_auc_score(y, s, sample_weight=w))


def build_model(n_in, n_hid, n_out):
    import torch.nn as nn
    return nn.Sequential(nn.Linear(n_in, n_hid), nn.ReLU(),
                         nn.Linear(n_hid, n_hid), nn.ReLU(),
                         nn.Linear(n_hid, n_out))


# The 22-slot head-input contract. Slots 0..10 and 14..21 keep the SHIPPED names AND the shipped
# norm constants verbatim (the dump's x is already standardized with them, so train == serve by
# construction); 11/12/13 are the three raw gate logits and take constants fitted here.
SHIPPED_NORM = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))), "r1_ref", "attach_norm_MIN1.json")
NEW_NAMES = ["af_chainGateZFake", "af_chainGateZPrompt", "af_chainGateZDisp"]


def write_norm(path, zmu, zsd, use_cols_tag):
    sh = json.load(open(SHIPPED_NORM))
    nm = sh["feature_names"]
    assert len(nm) == 20 and nm[11] == "af_chainGateLogit"
    names = nm[:11] + NEW_NAMES + nm[12:]
    mean = sh["mean"][:11] + [float(v) for v in zmu] + sh["mean"][12:]
    std = sh["std"][:11] + [float(v) for v in zsd] + sh["std"][12:]
    assert len(names) == len(mean) == len(std) == 22
    # conditioning is keyed by feature NAME and none of the shipped entries names slot 11, so it
    # transfers unchanged; the three new slots get no clip and no log10_1p, which is exactly the
    # conditioning the deleted af_chainGateLogit had.
    cond = [c for c in sh["conditioning"] if c["feature"] != "af_chainGateLogit"]
    assert len(cond) == len(sh["conditioning"])
    with open(path, "w") as fh:
        json.dump({"feature_names": names, "conditioning": cond, "mean": mean, "std": std,
                   "r1_use_cols": use_cols_tag, "r1_cpp_prefix": True, "seed": 42,
                   "degenerate_columns": [], "per_type": False}, fh, indent=1)
    return names


def deployed_scalar(z, arm):
    import torch
    if arm == "scalar":
        return z[:, 0]
    return torch.logsumexp(z[:, 1:3], dim=1) - z[:, 0]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--lab", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "lab"))
    p.add_argument("--out", required=True)
    p.add_argument("--arm", choices=["scalar", "three"], default="scalar")
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--epochs", type=int, default=60)
    p.add_argument("--patience", type=int, default=8)
    p.add_argument("--hidden", type=int, default=24)
    p.add_argument("--batch-size", type=int, default=16384)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--sched", choices=["const", "cos"], default="const")
    p.add_argument("--lr-min", type=float, default=1e-5)
    p.add_argument("--gamma-neg", type=float, default=2.0)
    p.add_argument("--disp-mid", type=float, default=8.0)
    p.add_argument("--disp-hi", type=float, default=16.0)
    p.add_argument("--chain-weight", type=float, default=1.0)
    p.add_argument("--class-weight", choices=["m19", "lst"], default="m19",
                   help="three-arm only: 'lst' = total/(3*count), the T3-DNN convention")
    p.add_argument("--val-cap", type=int, default=6_000_000)
    p.add_argument("--drop-aux", action="store_true",
                   help="exclude stage-2 (aux 4-layer -XC4) rows from TRAINING")
    p.add_argument("--frac", type=float, default=1.0, help="row subsample for a quick probe")
    args = p.parse_args()

    import torch
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.cuda.manual_seed_all(args.seed)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log("arm=%s sched=%s lr=%g gamma_neg=%g disp=(%g,%g) cw=%s dev=%s"
        % (args.arm, args.sched, args.lr, args.gamma_neg, args.disp_mid, args.disp_hi,
           args.class_weight, dev))

    L = args.lab
    X20 = np.load(L + "/X20.npy", mmap_mode="r")
    z3 = np.load(L + "/z3.npy", mmap_mode="r")
    y = np.load(L + "/y.npy")
    st = np.load(L + "/st.npy")
    vxy = np.load(L + "/vxy.npy")
    evt = np.load(L + "/evt.npy")
    n = len(y)
    log("rows %d true %d (%.4f)" % (n, y.sum(), y.mean()))

    # ---- event-level 600/200/200 split, seed 42 (S1/S2 convention) ---------------------
    rng = np.random.default_rng(args.seed)
    nev = int(evt.max()) + 1
    perm = rng.permutation(nev)
    ntr, nva = int(0.6 * nev), int(0.2 * nev)
    role = np.zeros(nev, np.int8)
    role[perm[:ntr]] = 0
    role[perm[ntr:ntr + nva]] = 1
    role[perm[ntr + nva:]] = 2
    r = role[evt]
    # dump sampling weight: stage A / aux4L kept whole, stage B kept 1-in-16
    w = np.where(st == 1, 16.0, 1.0).astype(np.float32)

    keep = np.ones(n, bool)
    if args.drop_aux:
        keep &= (st != 2) | (r != 0)
    if args.frac < 1.0:
        keep &= rng.random(n) < args.frac

    itr = np.flatnonzero((r == 0) & keep)
    iva = np.flatnonzero(r == 1)
    ite = np.flatnonzero(r == 2)
    log("split events %d/%d/%d  rows %d/%d/%d" % (ntr, nva, nev - ntr - nva,
                                                  len(itr), len(iva), len(ite)))

    # ---- the three new slots' standardization, FIT ON TRAIN ROWS ONLY ------------------
    zt = np.asarray(z3[itr], dtype=np.float64)
    zmu = zt.mean(0)
    zsd = zt.std(0)
    zsd[zsd < 1e-6] = 1.0
    log("z3 mean %s std %s" % (np.round(zmu, 6).tolist(), np.round(zsd, 6).tolist()))
    del zt

    def make_x(idx):
        a = np.asarray(X20[idx])
        b = (np.asarray(z3[idx], dtype=np.float32) - zmu.astype(np.float32)) / zsd.astype(np.float32)
        return np.ascontiguousarray(np.concatenate([a[:, :11], b, a[:, 12:]], axis=1))

    Xtr = make_x(itr)
    ytr = y[itr].astype(np.float32)
    wtr = w[itr].copy()
    vtr = vxy[itr]
    sttr = st[itr]
    if args.chain_weight != 1.0:
        wtr[sttr != 1] *= args.chain_weight
    tru = ytr > 0.5
    if args.disp_mid != 1.0:
        wtr[tru & (vtr >= 1.0) & (vtr < 5.0)] *= args.disp_mid
    if args.disp_hi != 1.0:
        wtr[tru & (vtr >= 5.0)] *= args.disp_hi
    W_pos = float((wtr * ytr).sum())
    W_neg = float((wtr * (1 - ytr)).sum())
    pos_weight = W_neg / max(W_pos, 1.0)
    log("train weighted %.4g true / %.4g fake -> pos_weight %.4f" % (W_pos, W_neg, pos_weight))

    # three-class targets: 0 fake, 1 prompt-true (vxy < 1, INCLUDING the -999 pileup-only
    # convention), 2 displaced-true (vxy >= 1)
    cls = np.zeros(len(ytr), np.int64)
    cls[tru & (vtr < 1.0)] = 1
    cls[tru & (vtr >= 1.0)] = 2
    log("class census train: fake %d prompt %d disp %d"
        % ((cls == 0).sum(), (cls == 1).sum(), (cls == 2).sum()))

    Xva = make_x(iva)
    yva = y[iva].astype(np.float32)
    wva = w[iva].copy()
    stva = st[iva]
    vsel = np.flatnonzero(stva != 1)          # val metric = the CHAIN universe (shipped choice)
    if len(vsel) > args.val_cap:
        vsel = np.sort(np.random.default_rng(args.seed + 1)
                       .choice(vsel, size=args.val_cap, replace=False))
    Xva_t = torch.tensor(Xva[vsel]).to(dev)
    yva_s, wva_s = yva[vsel], wva[vsel]
    log("val (chain universe) %d rows, %d true" % (len(vsel), int(yva_s.sum())))

    n_out = 1 if args.arm == "scalar" else 3
    model = build_model(22, args.hidden, n_out).to(dev)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    sched = None
    if args.sched == "cos":
        sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs,
                                                           eta_min=args.lr_min)

    Xtr_t = torch.tensor(Xtr).to(dev)
    ytr_t = torch.tensor(ytr).to(dev)
    wtr_t = torch.tensor(wtr).to(dev)
    cls_t = torch.tensor(cls).to(dev)
    del Xtr

    if args.arm == "scalar":
        crit = torch.nn.BCEWithLogitsLoss(pos_weight=torch.tensor(pos_weight, device=dev),
                                          reduction="none")
        cw_t = None
    else:
        if args.class_weight == "lst":
            cnt = np.array([(cls == 0).sum(), (cls == 1).sum(), (cls == 2).sum()], np.float64)
            cwv = cls.size / (3.0 * np.maximum(cnt, 1))
        else:
            # M19 convention carried over: the fake/true balance is pos_weight and the two
            # true classes keep the disp tiers already folded into wtr.
            cwv = np.array([1.0, pos_weight, pos_weight])
        log("class weights %s" % np.round(cwv, 5).tolist())
        cw_t = torch.tensor(cwv, dtype=torch.float32, device=dev)
        crit = torch.nn.CrossEntropyLoss(weight=cw_t, reduction="none")

    gen = torch.Generator(device="cpu").manual_seed(args.seed)
    best, best_ep, best_state, bad = -1.0, -1, None, 0
    hist = []
    ntr_rows = Xtr_t.shape[0]
    for ep in range(1, args.epochs + 1):
        model.train()
        pm = torch.randperm(ntr_rows, generator=gen).to(dev)
        tl, tw = 0.0, 0.0
        for i in range(0, ntr_rows, args.batch_size):
            idx = pm[i:i + args.batch_size]
            xb, wb = Xtr_t[idx], wtr_t[idx]
            opt.zero_grad(set_to_none=True)
            zz = model(xb)
            if args.arm == "scalar":
                zs = zz.squeeze(1)
                yb = ytr_t[idx]
                base = crit(zs, yb)
                if args.gamma_neg > 0:
                    mod = torch.sigmoid(zs.detach()).pow(args.gamma_neg)
                    fw = torch.where(yb > 0.5, torch.ones_like(mod), mod)
                    l = (base * wb * fw).sum() / (wb * fw).sum().clamp_min(1e-12)
                else:
                    l = (base * wb).sum() / wb.sum()
            else:
                cb = cls_t[idx]
                base = crit(zz, cb)
                if args.gamma_neg > 0:
                    s = deployed_scalar(zz.detach(), args.arm)
                    mod = torch.sigmoid(s).pow(args.gamma_neg)
                    fw = torch.where(cb > 0, torch.ones_like(mod), mod)
                    l = (base * wb * fw).sum() / (wb * fw).sum().clamp_min(1e-12)
                else:
                    l = (base * wb).sum() / wb.sum()
            l.backward()
            opt.step()
            bw = float(wb.sum())
            tl += float(l.detach()) * bw
            tw += bw
        if sched is not None:
            sched.step()
        model.eval()
        with torch.no_grad():
            sv = []
            for i in range(0, Xva_t.shape[0], 1 << 20):
                sv.append(deployed_scalar(model(Xva_t[i:i + (1 << 20)]), args.arm).cpu().numpy())
            sv = np.concatenate(sv)
        m = yva_s > 0.5
        a = wauc(sv[m], wva_s[m], sv[~m], wva_s[~m])
        hist.append({"epoch": ep, "loss": tl / max(tw, 1e-9), "val_auc": a})
        log("epoch %3d loss=%.5f val_auc=%.6f lr=%.2e" % (ep, tl / max(tw, 1e-9), a,
                                                          opt.param_groups[0]["lr"]))
        if a > best:
            best, best_ep, bad = a, ep, 0
            best_state = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
        else:
            bad += 1
        if bad >= args.patience:
            log("early stop at %d (best %.6f @ %d)" % (ep, best, best_ep))
            break
    model.load_state_dict(best_state)
    model.to(dev)

    # ---- frozen TEST report -----------------------------------------------------------
    res = {}
    Xte = make_x(ite)
    with torch.no_grad():
        ste = []
        for i in range(0, len(Xte), 1 << 20):
            ste.append(deployed_scalar(
                model(torch.tensor(Xte[i:i + (1 << 20)]).to(dev)), args.arm).cpu().numpy())
        ste = np.concatenate(ste)
    yte, wte, stte, vte = y[ite], w[ite], st[ite], vxy[ite]
    for tag, sel in (("ALL", np.ones(len(ite), bool)),
                     ("chain5+ (stage 0)", stte == 0),
                     ("bareT3 (stage 1)", stte == 1),
                     ("aux4L (stage 2)", stte == 2)):
        m = sel & (yte == 1)
        f = sel & (yte == 0)
        a = wauc(ste[m], wte[m], ste[f], wte[f])
        res[tag] = {"auc": a, "n_true": int(m.sum()), "n_fake": int(f.sum())}
        log("  %-20s n_true=%8d n_fake=%9d AUC=%s" % (tag, m.sum(), f.sum(),
                                                      "n/a" if a is None else "%.5f" % a))
    for tag, lo, hi in (("prompt vxy<1", 0.0, 1.0), ("disp vxy[1,5)", 1.0, 5.0),
                        ("disp vxy>=5", 5.0, 1e9), ("pileup-only", -1e9, -998.0)):
        m = (stte == 0) & (yte == 1) & (vte >= lo) & (vte < hi)
        f = (stte == 0) & (yte == 0)
        a = wauc(ste[m], wte[m], ste[f], wte[f])
        res["chain " + tag] = {"auc": a, "n_true": int(m.sum())}
        log("  chain %-16s n_true=%8d AUC=%s" % (tag, m.sum(), "n/a" if a is None else "%.5f" % a))

    import torch as _t
    names = write_norm(args.out.replace(".pt", "_norm.json"), zmu, zsd,
                       "s3_22input_3gatelogits")
    _t.save({"state_dict": {k: v.cpu() for k, v in model.state_dict().items()},
             "arch": [22, args.hidden, args.hidden, n_out], "arm": args.arm,
             "feature_names": names,
             "z3_mean": zmu.tolist(), "z3_std": zsd.tolist(),
             "best_epoch": best_ep, "best_val_auc": float(best),
             "args": vars(args)}, args.out)
    with open(args.out.replace(".pt", ".json"), "w") as fh:
        json.dump({"args": vars(args), "best_epoch": best_ep, "best_val_auc": float(best),
                   "z3_mean": zmu.tolist(), "z3_std": zsd.tolist(),
                   "pos_weight": pos_weight, "W_pos": W_pos, "W_neg": W_neg,
                   "test": res, "history": hist,
                   "split": {"train_events": int(ntr), "val_events": int(nva),
                             "test_events": int(nev - ntr - nva),
                             "train_rows": int(len(itr)), "val_rows": int(len(iva)),
                             "test_rows": int(len(ite))}}, fh, indent=1)
    log("saved %s" % args.out)


if __name__ == "__main__":
    main()
