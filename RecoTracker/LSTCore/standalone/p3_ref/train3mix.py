#!/usr/bin/env python3
"""P3: on-policy retrain of the 3-CLASS CHAIN GATE with JET-CORE chains MIXED INTO the PU200 rows.

Everything about the head is inherited from nnloop_ref/s2_work/train3.py, which is itself the
shipped recipe: arch 25->32->32->3, the M12 CONDITIONING_SPEC, the m12 tiered displaced class
weighting, the cosine schedule S1/S2 proved is the converged one, seed 42, and the label
(labelChainsHarness, replicated not corrected).  THREE things are new and they are the whole arm:

 1. TWO row-sets are trained jointly: an on-policy PU200 dump (event_1000, 1000 evt) and an
    on-policy JET dump (jet_ref/trackingNtuple_jets_1000.root events 0-499 ONLY -- 500-999 is the
    round's holdout and is never trained or calibrated on).  Both come from the SAME binary at
    ec08aba9e5b with the provenance stamped next to the dump (p3_ref/dump/*.prov).

 2. `--jet-share L` sets the fraction of the TOTAL TRAIN LOSS WEIGHT carried by jet rows.  Row
    COUNTS are not the knob: the two samples have similar row counts (PU200 ~6.9M, jets ~4.1M)
    but wildly different composition (jets are 81% gate-killed, 70% 4-layer, and their true-chain
    prevalence is far lower), so a naive concatenation would hand the jet sample a share of the
    gradient nobody chose.  L is chosen and reported, and L = 0 is the CONTROL ARM: a PU200-only
    re-dump retrain, which is what separates the ENRICHMENT effect from the RE-DUMP effect
    (the round-5 rule).

 3. STANDARDIZATION IS ALWAYS FITTED ON THE PU200 TRAIN SPLIT, never on the mix.  The protected
    sample's normalized input distribution then does not move with L, so the control arm is a
    genuine nested case of the enrichment arms and the only thing L changes is the loss.

VAL METRIC.  The shipped selector is min(AUC(mP prompt-vs-fake), AUC(mD disp-vs-fake)).  With two
samples in the mix the selector is the MINIMUM OVER EVERY SAMPLE IN THE MIX of that same pair, so
an epoch that buys jets by giving up PU200 cannot be selected.  At L = 0 only PU200 is in the mix
and the selector is bit-for-bit the shipped one.  The jet-core separation (AUC of mX between
core-true and core-fake chains, the 0.81-on-11-units defect of FINDINGS_JETPHYS Q2) is REPORTED
every epoch but deliberately NOT selected on.
"""
import argparse
import copy
import json
import os
import sys
import time

import numpy as np

T0 = time.time()
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "nnloop_ref", "s2_work"))
from train3 import COND, build_inputs, event_split  # noqa: E402


def log(m):
    print("[%7.1fs] %s" % (time.time() - T0, m), flush=True)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--lab", action="append", required=True,
                   help="NAME=dir, repeatable. The FIRST one is the reference sample whose train "
                        "split fits the standardization (use pu=...).")
    p.add_argument("--tag", required=True)
    p.add_argument("--jet-share", type=float, default=0.0,
                   help="fraction of total train loss weight given to all NON-reference samples")
    p.add_argument("--loss", choices=["m12", "lst"], default="m12")
    p.add_argument("--sched", choices=["const", "cos"], default="cos")
    p.add_argument("--epochs", type=int, default=0)
    p.add_argument("--patience", type=int, default=15)
    p.add_argument("--lr", type=float, default=0.0)
    p.add_argument("--hidden", type=int, default=32)
    p.add_argument("--batch-size", type=int, default=16384)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--dw-mid", type=float, default=8.0)
    p.add_argument("--dw-hi", type=float, default=16.0)
    p.add_argument("--jet-tier", action="store_true",
                   help="also apply the tiered displaced boost to NON-reference samples")
    a = p.parse_args()
    if a.epochs == 0:
        a.epochs = 120 if a.sched == "const" else 300
    if a.lr == 0.0:
        a.lr = 1e-3 if a.sched == "const" else 3e-3

    import torch
    np.random.seed(a.seed)
    torch.manual_seed(a.seed)
    torch.cuda.manual_seed_all(a.seed)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    labs = [s.split("=", 1) for s in a.lab]
    log("tag=%s labs=%s jet_share=%.3f loss=%s sched=%s lr=%g epochs=%d dev=%s"
        % (a.tag, [n for n, _ in labs], a.jet_share, a.loss, a.sched, a.lr, a.epochs, dev))

    Xs, ys, sid, trm, vam, tem, cores = [], [], [], [], [], [], []
    for k, (nm, d) in enumerate(labs):
        X, names, M = build_inputs(d)
        tr, va, te = event_split(M["evt"], a.seed)
        is_true = M["label"] == 1
        vxy = M["vxy"]
        y3 = np.zeros(len(X), np.int64)
        y3[is_true & (vxy < 1.0)] = 1
        y3[is_true & (vxy >= 1.0)] = 2
        core = (M["isCore"] == 1) if "isCore" in M else np.zeros(len(X), np.int8)
        log("  %-4s %s: %d chains  fake %d prompt %d disp %d  core-true %d"
            % (nm, d, len(X), (y3 == 0).sum(), (y3 == 1).sum(), (y3 == 2).sum(), int((core == 1).sum())))
        Xs.append(X)
        ys.append(y3)
        sid.append(np.full(len(X), k, np.int8))
        trm.append(tr)
        vam.append(va)
        tem.append(te)
        cores.append(np.asarray(core, np.int8))
    X = np.concatenate(Xs)
    y3 = np.concatenate(ys)
    S = np.concatenate(sid)
    tr, va, te = np.concatenate(trm), np.concatenate(vam), np.concatenate(tem)
    core = np.concatenate(cores)
    del Xs, ys, sid, trm, vam, tem, cores

    # ---- standardization: the REFERENCE sample's train split only ---------------------------
    ref = (S == 0) & tr
    mu = X[ref].mean(axis=0, dtype=np.float64).astype(np.float32)
    sd = X[ref].std(axis=0, dtype=np.float64).astype(np.float32)
    sd[sd < 1e-8] = 1.0
    Xn = (X - mu) / sd
    del X

    # ---- per-sample class weights, then the jet-share rescale --------------------------------
    vxy_all = np.concatenate([np.load(os.path.join(d, "meta.npz"))["vxy"] for _, d in labs])
    w = np.ones(len(Xn), dtype=np.float32)
    spec = {"convention": a.loss, "jet_share_target": a.jet_share,
            "tier_on_nonreference": bool(a.jet_tier), "per_sample": {}}
    for k, (nm, _) in enumerate(labs):
        m = S == k
        mt = m & tr
        n0, n1, n2 = [int((y3[mt] == c).sum()) for c in (0, 1, 2)]
        if a.loss == "m12":
            pw = n0 / max(n1 + n2, 1)
            w[m & (y3 > 0)] = pw
            spec["per_sample"][nm] = {"pos_weight": pw, "counts_train": [n0, n1, n2]}
        else:
            tot = n0 + n1 + n2
            cw = [tot / (3.0 * max(c, 1)) for c in (n0, n1, n2)]
            for c in (0, 1, 2):
                w[m & (y3 == c)] = cw[c]
            spec["per_sample"][nm] = {"class_weights": cw, "counts_train": [n0, n1, n2]}
        # THE TIERED DISPLACED BOOST IS APPLIED TO THE REFERENCE (PU200) SAMPLE ONLY unless
        # --jet-tier. It exists to serve the PU200/cube displaced physics the E1-B2 win lives in,
        # and the jet sample's "displaced-true" population is a DIFFERENT thing: on jets
        # displaced/true is 25.4% against PU200's 1.25% (in-jet decays and nuclear interactions, in
        # a sample with no pileup), and its pos_weight is 32.3 against PU200's ~1.3. Tiering it
        # would put ~80% of the jet block's gradient on in-jet displaced rows and turn a core-
        # TOPOLOGY lesson into an in-jet-displacement lesson -- pointed straight at the protected
        # behaviour. `--jet-tier` is available to test that claim rather than assert it.
        if a.loss == "m12" and (k == 0 or a.jet_tier):
            it = m & (y3 > 0)
            w[it & (vxy_all >= 1.0) & (vxy_all < 5.0)] *= a.dw_mid
            w[it & (vxy_all >= 5.0)] *= a.dw_hi
    del vxy_all

    if len(labs) > 1 and a.jet_share > 0:
        wref = float(w[(S == 0) & tr].sum())
        wjet = float(w[(S != 0) & tr].sum())
        # want wjet' / (wref + wjet') = L  ->  wjet' = L/(1-L) * wref
        f = (a.jet_share / (1.0 - a.jet_share)) * wref / max(wjet, 1e-12)
        w[S != 0] *= f
        spec["jet_rescale_factor"] = f
        log("jet-share rescale: factor %.6f (ref train weight %.3e, jet %.3e -> %.3e)"
            % (f, wref, wjet, wjet * f))
    elif len(labs) > 1:
        w[S != 0] = 0.0
        spec["jet_rescale_factor"] = 0.0
        log("jet-share 0: jet rows carry ZERO loss weight (they are still reported, not trained)")
    tot = float(w[tr].sum())
    for k, (nm, _) in enumerate(labs):
        spec["per_sample"][nm]["train_weight_share"] = float(w[tr & (S == k)].sum()) / tot
    shares = [float(w[tr][y3[tr] == c].sum()) / tot for c in (0, 1, 2)]
    spec["train_loss_weight_share_by_class"] = shares
    log("train weight share by SAMPLE: " + "  ".join(
        "%s %.4f" % (nm, spec["per_sample"][nm]["train_weight_share"]) for nm, _ in labs))
    log("train weight share by CLASS: fake %.4f prompt %.4f displaced %.4f" % tuple(shares))

    # rows with zero weight are dropped from the train tensor entirely (saves memory and time)
    trk = tr & (w > 0)
    Xtr = torch.tensor(np.ascontiguousarray(Xn[trk])).to(dev)
    ytr = torch.tensor(np.ascontiguousarray(y3[trk])).to(dev)
    wtr = torch.tensor(np.ascontiguousarray(w[trk])).to(dev)
    log("train rows %d of %d (zero-weight rows dropped)" % (int(trk.sum()), int(tr.sum())))

    inmix = [k for k in range(len(labs)) if float(w[tr & (S == k)].sum()) > 0]
    Xva = torch.tensor(np.ascontiguousarray(Xn[va])).to(dev)
    yva, Sva, cva = y3[va], S[va], core[va]

    model = torch.nn.Sequential(torch.nn.Linear(Xn.shape[1], a.hidden), torch.nn.ReLU(),
                                torch.nn.Linear(a.hidden, a.hidden), torch.nn.ReLU(),
                                torch.nn.Linear(a.hidden, 3)).to(dev)
    opt = torch.optim.Adam(model.parameters(), lr=a.lr)
    sch = (torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=a.epochs, eta_min=1e-5)
           if a.sched == "cos" else None)
    crit = torch.nn.CrossEntropyLoss(reduction="none")
    gen = torch.Generator(device="cpu").manual_seed(a.seed)

    def torch_auc(pos, neg):
        """Mann-Whitney U with TIE-AVERAGED ranks -- exactly roc_auc_score (train3.py verbatim)."""
        if len(pos) == 0 or len(neg) == 0:
            return float("nan")
        x = torch.cat([pos, neg])
        n1, n2 = len(pos), len(neg)
        order = torch.argsort(x)
        xs = x[order]
        rk = torch.arange(1, n1 + n2 + 1, dtype=torch.float64, device=x.device)
        uniq, inv, cnt = torch.unique(xs, return_inverse=True, return_counts=True)
        ssum = torch.zeros(len(uniq), dtype=torch.float64, device=x.device)
        ssum.scatter_add_(0, inv, rk)
        rk = (ssum / cnt.double())[inv]
        ispos = torch.zeros(n1 + n2, dtype=torch.bool, device=x.device)
        ispos[:n1] = True
        r1 = rk[ispos[order]].sum()
        return float((r1 - n1 * (n1 + 1) / 2.0) / (float(n1) * float(n2)))

    msk = {}
    for k in range(len(labs)):
        msk[("f", k)] = torch.tensor(np.ascontiguousarray((Sva == k) & (yva == 0))).to(dev)
        msk[("p", k)] = torch.tensor(np.ascontiguousarray((Sva == k) & (yva == 1))).to(dev)
        msk[("d", k)] = torch.tensor(np.ascontiguousarray((Sva == k) & (yva == 2))).to(dev)
        # the jet-core cell: core-true chains vs the fakes of that same sample
        msk[("ct", k)] = torch.tensor(np.ascontiguousarray((Sva == k) & (cva == 1) & (yva > 0))).to(dev)

    def val_metrics():
        model.eval()
        with torch.no_grad():
            zs = []
            for i in range(0, len(Xva), 1 << 20):
                zs.append(model(Xva[i:i + (1 << 20)]).float())
            z = torch.cat(zs)
            mP, mD = z[:, 1] - z[:, 0], z[:, 2] - z[:, 0]
            mX = torch.maximum(z[:, 1], z[:, 2]) - z[:, 0]
            out = {}
            for k, (nm, _) in enumerate(labs):
                out[nm + "_P"] = torch_auc(mP[msk[("p", k)]], mP[msk[("f", k)]])
                out[nm + "_D"] = torch_auc(mD[msk[("d", k)]], mD[msk[("f", k)]])
                if int(msk[("ct", k)].sum()) > 0:
                    out[nm + "_core"] = torch_auc(mX[msk[("ct", k)]], mX[msk[("f", k)]])
            return out

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
        vm = val_metrics()
        sel = min(vm[labs[k][0] + s] for k in inmix for s in ("_P", "_D"))
        hist.append([ep, tot_loss / n_tr, sel] + [vm[k] for k in sorted(vm)])
        if ep % 5 == 0 or ep <= 3:
            log("ep %3d loss=%.5f sel=%.5f  " % (ep, tot_loss / n_tr, sel)
                + " ".join("%s=%.5f" % (k, v) for k, v in sorted(vm.items()))
                + ("  *" if sel > best_sel else ""))
        if sel > best_sel:
            best_sel, best_ep, bad = sel, ep, 0
            best_meta = dict(vm)
            best_state = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
        else:
            bad += 1
            if a.sched == "const" and bad >= a.patience:
                log("early stop at epoch %d (best %.5f @ %d)" % (ep, best_sel, best_ep))
                break
    log("BEST epoch %d sel=%.5f %s" % (best_ep, best_sel,
                                       " ".join("%s=%.5f" % kv for kv in sorted(best_meta.items()))))

    os.makedirs(os.path.join(HERE, "models"), exist_ok=True)
    mp = os.path.join(HERE, "models", "chain3_%s.pt" % a.tag)
    torch.save({"state_dict": best_state, "arch": [Xn.shape[1], a.hidden, a.hidden, 3],
                "feature_names": names, "seed": a.seed, "conditioning": COND,
                "best_epoch": best_ep, "best_sel": float(best_sel),
                "best_val_meta": best_meta}, mp)
    np.save(os.path.join(HERE, "models", "hist_%s.npy" % a.tag), np.array(hist, dtype=np.float64))
    nj = os.path.join(HERE, "models", "chain3_norm_%s.json" % a.tag)
    with open(nj, "w") as fh:
        json.dump({"feature_names": names, "conditioning": COND,
                   "mean": mu.tolist(), "std": sd.tolist(), "seed": a.seed,
                   "class_spec": {"0": "fake", "1": "prompt-true (simVxy<1)",
                                  "2": "displaced-true (simVxy>=1)"},
                   "displaced_weighting": spec,
                   "val_metric_keys": sorted(best_meta),
                   "train_args": {"epochs": a.epochs, "batch_size": a.batch_size, "lr": a.lr,
                                  "hidden": a.hidden, "sched": a.sched, "loss": a.loss,
                                  "jet_share": a.jet_share,
                                  "inputs": {n: d for n, d in labs},
                                  "standardized_on": labs[0][0] + " train split",
                                  "val_metric": "min over samples in the mix of "
                                                "(AUC(mP prompt-vs-fake), AUC(mD disp-vs-fake))"}},
                  fh, indent=1)
    log("saved %s + %s" % (mp, nj))


if __name__ == "__main__":
    main()
