#!/usr/bin/env python3

# PRODUCED THE SHIPPED src/alpaka/AttachNetworkWeights.h (jet round 3, arm ATJ25R).
# Exact invocation:
#   python3 train_attach.py --lab pu=<pu_lab> --lab jet=<jet_lab> --share jet=0.25 \
#     --arm scalar --epochs 200 --patience 200 --hidden 24 --batch-size 16384 \
#     --lr 3e-3 --sched cos --gamma-neg 2 --disp-mid 8 --disp-hi 16 \
#     --class-weight m19 --val-cap 6e6 --seed 42
# That is the shipped A2_cos3e3 recipe verbatim PLUS jet-core rows at 25% of the train loss weight.
# Slot 11-13 standardization and pos_weight are fitted on the PU200 REFERENCE train split alone, so
# the mix is a nested case of the control and only the loss share changes. Jet rows are events
# 0-499 ONLY (500-999 is the sealed holdout).
#
# THE PAIR DUMP IS 22 FLOATS WIDE (kAttachFeatures), not the 20 that nnloop_ref/PAIRDUMP_FORMAT.md
# originally documented. This script takes the width from the header and overwrites columns 11:14
# with the re-standardized raw gate logits, keeping every other column verbatim. The older
# nnloop_ref/s3_work/train_s3.py splices [a[:,:11], b, a[:,12:]] and is SILENTLY WRONG on a 22-wide
# row (it drops slot 11 and shifts eight columns).
#
# Bars are NOT produced here -- see barfit_attach.py (ROLE SPLIT: delivery bars at matched
# acceptance on PU200, retirement bars at min(PU200, jets)). Header: export_attach_weights.py.
# The attach head reads the chain gate's three RAW logits as inputs 11-13, so RETRAINING THE GATE
# REQUIRES RERUNNING THIS.

"""AT attach-head trainer -- nnloop_ref/s3_work/train_s3.py, on-policy behind the round-2 gate.

THREE changes from train_s3.py, and the FIRST one is forced rather than chosen:

 1. The dump's `x` is now the FULL 22-wide standardized vector (`ChainAttachPairRow::x` is
    `float x[kAttachFeatures]` and kAttachFeatures is 22 in this tree), where slots 11-13 already
    carry the gate logits standardized with the DEPLOYED header's constants.  train_s3.py's dump
    was 20-wide with a dead slot 11, so its `[a[:, :11], b, a[:, 12:]]` splice is not expressible
    here.  This file instead OVERWRITES columns 11:14 with the raw z3 re-standardized on the
    constants fitted here, and keeps every other column of the dump verbatim.  Slots 0-10 and 14-21
    therefore still keep the shipped conditioning bit for bit with no de/re-standardize round trip.
 2. `--lab NAME=dir` is repeatable and `--share NAME=frac` sets each non-reference sample's share of
    the TOTAL TRAIN LOSS WEIGHT (g2_ref/train3mix2.py's convention, same rescale algebra).  The
    FIRST --lab is the reference (use pu=) and its train split alone fits the slot 11-13
    standardization, so the mixed arms are genuine nested cases of the one-sample control.
    pos_weight is likewise fitted on the reference sample alone.  (--flat, which g2_ref needed for
    99.8%-true gun samples, is NOT provided here: the jet pair corpus is 0.91% true, i.e. LESS true
    than PU200's 2.97%, so the pathology it exists for does not arise.)
 3. The val selector is the MINIMUM over samples in the mix of train_s3.py's own chain-universe
    weighted AUC, so an epoch that buys jets by giving up PU200 cannot be selected.  With ONE lab
    and no --share the selector, the loss and the RNG stream are bit-for-bit train_s3.py's.

Original banner follows.
--------------------------------------------------------------------------------------------------
S3 attach-head trainer -- 22 inputs, `Chain2NetworkWeights.h` DELETED, no surrogate.

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

def tonp(t):
    """torch -> numpy without the numpy bridge.

    The CMSSW py3-torch-cuda build in this release is compiled WITHOUT NumPy support, so
    `Tensor.numpy()` raises. DLPack is zero-copy and available; the copy() is because the source
    tensor is a slice of a buffer the caller may reuse.
    """
    import numpy as _np
    return _np.from_dlpack(t.detach().cpu()).copy()



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
# AT: r1_ref/attach_norm_MIN1.json (the 20-slot pre-S3 norm train_s3.py rebuilt from) was purged in
# the 2026-08-11 disk cleanup.  The DEPLOYED 22-slot norm is its result and is present, so the new
# norm is that file with ONLY mean/std[11..13] replaced -- identical output, one fewer dependency,
# and it cannot silently disagree with the header actually in the tree.
SHIPPED_NORM22 = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                              "nnloop_ref", "s3_work", "models", "A2_cos3e3_norm.json")
NEW_NAMES = ["af_chainGateZFake", "af_chainGateZPrompt", "af_chainGateZDisp"]


def write_norm(path, zmu, zsd, use_cols_tag):
    sh = json.load(open(SHIPPED_NORM22))
    names = sh["feature_names"]
    assert len(names) == 22 and names[11:14] == NEW_NAMES, names[11:14]
    mean = list(sh["mean"]); std = list(sh["std"])
    for j in range(3):
        mean[11 + j] = float(zmu[j]); std[11 + j] = float(zsd[j])
    out = dict(sh)
    out.update({"feature_names": names, "mean": mean, "std": std,
                "r1_use_cols": use_cols_tag, "r1_cpp_prefix": True, "seed": 42,
                "degenerate_columns": [], "per_type": False})
    with open(path, "w") as fh:
        json.dump(out, fh, indent=1)
    return names


def deployed_scalar(z, arm):
    import torch
    if arm == "scalar":
        return z[:, 0]
    return torch.logsumexp(z[:, 1:3], dim=1) - z[:, 0]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--lab", action="append", required=True,
                   help="NAME=dir, repeatable. The FIRST is the reference sample (use pu=...).")
    p.add_argument("--share", action="append", default=[],
                   help="NAME=frac, repeatable: that sample's share of the total train loss weight")

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
    p.add_argument("--sel-min-rows", type=int, default=200,
                   help="a sample enters the val SELECTOR only with this many rows per side")
    args = p.parse_args()

    import torch
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.cuda.manual_seed_all(args.seed)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log("arm=%s sched=%s lr=%g gamma_neg=%g disp=(%g,%g) cw=%s dev=%s"
        % (args.arm, args.sched, args.lr, args.gamma_neg, args.disp_mid, args.disp_hi,
           args.class_weight, dev))

    labs = [tuple(v.split("=", 1)) for v in args.lab]
    names = [nm for nm, _ in labs]
    assert len(set(names)) == len(names), "duplicate --lab name"
    Xs, z3s, ys, sts, vxys, rs, sids = [], [], [], [], [], [], []
    # Each sample gets its OWN seed-42 event split, exactly as train_s3.py does for the one it had.
    for k, (nm, d) in enumerate(labs):
        Xk = np.load(d + "/X.npy", mmap_mode="r")
        z3k = np.load(d + "/z3.npy", mmap_mode="r")
        yk = np.load(d + "/y.npy")
        stk = np.load(d + "/st.npy")
        vk = np.load(d + "/vxy.npy")
        ek = np.load(d + "/evt.npy")
        rngk = np.random.default_rng(args.seed)
        nevk = int(ek.max()) + 1
        permk = rngk.permutation(nevk)
        ntrk, nvak = int(0.6 * nevk), int(0.2 * nevk)
        rolek = np.zeros(nevk, np.int8)
        rolek[permk[ntrk:ntrk + nvak]] = 1
        rolek[permk[ntrk + nvak:]] = 2
        Xs.append(Xk); z3s.append(z3k); ys.append(yk); sts.append(stk); vxys.append(vk)
        rs.append(rolek[ek]); sids.append(np.full(len(yk), k, np.int8))
        log("lab %-6s %-60s rows %d true %d (%.4f) events %d split %d/%d/%d"
            % (nm, d, len(yk), yk.sum(), yk.mean(), nevk, ntrk, nvak, nevk - ntrk - nvak))
        if k == 0:
            ntr, nva, nev = ntrk, nvak, nevk
    y = np.concatenate(ys); st = np.concatenate(sts); vxy = np.concatenate(vxys)
    r = np.concatenate(rs); S = np.concatenate(sids)
    off = np.concatenate(([0], np.cumsum([len(v) for v in ys])))
    n = len(y)
    log("rows %d true %d (%.4f) over %d sample(s)" % (n, y.sum(), y.mean(), len(labs)))

    def X20_get(idx, block=1 << 23):
        """Gather rows of the concatenated corpus out of the per-sample memmaps.

        SEQUENTIALLY, in source-order blocks.  numpy fancy-indexing a 19 GB memmap with tens of
        millions of sorted indices degenerates to per-row reads and takes longer than the training
        does; reading whole blocks and selecting inside them is the same result at streaming speed.
        `idx` must be sorted ascending, which every caller here guarantees (np.flatnonzero).
        """
        outX = np.empty((len(idx), Xs[0].shape[1]), np.float32)
        outZ = np.empty((len(idx), 3), np.float32)
        for k in range(len(labs)):
            lo = np.searchsorted(idx, off[k])
            hi = np.searchsorted(idx, off[k + 1])
            if hi <= lo:
                continue
            lidx = idx[lo:hi] - off[k]
            nk = Xs[k].shape[0]
            for b0 in range(0, nk, block):
                b1 = min(b0 + block, nk)
                a = np.searchsorted(lidx, b0)
                bb = np.searchsorted(lidx, b1)
                if bb <= a:
                    continue
                sel = lidx[a:bb] - b0
                outX[lo + a:lo + bb] = np.asarray(Xs[k][b0:b1])[sel]
                outZ[lo + a:lo + bb] = np.asarray(z3s[k][b0:b1])[sel]
        return outX, outZ

    rng = np.random.default_rng(args.seed)
    # dump sampling weight: stage A / aux4L kept whole, stage B kept 1-in-16
    w = np.where(st == 1, 16.0, 1.0).astype(np.float32)

    keep = np.ones(n, bool)
    if args.drop_aux:
        keep &= (st != 2) | (r != 0)
    if args.frac < 1.0:
        keep &= rng.random(n) < args.frac

    itr = np.flatnonzero((r == 0) & keep)
    # AT: --frac now subsamples val/test too, so a probe run is a probe run.  Inert at frac = 1.0.
    iva = np.flatnonzero((r == 1) & keep)
    ite = np.flatnonzero((r == 2) & keep)
    log("split events %d/%d/%d  rows %d/%d/%d" % (ntr, nva, nev - ntr - nva,
                                                  len(itr), len(iva), len(ite)))

    # ---- the three new slots' standardization, FIT ON TRAIN ROWS ONLY ------------------
    iref = np.flatnonzero((r == 0) & (S == 0) & keep)
    zt = np.asarray(z3s[0][iref - off[0]], dtype=np.float64)
    zmu = zt.mean(0)
    zsd = zt.std(0)
    zsd[zsd < 1e-6] = 1.0
    log("z3 standardization from REFERENCE sample %s train rows (%d): mean %s std %s"
        % (names[0], len(iref), np.round(zmu, 6).tolist(), np.round(zsd, 6).tolist()))
    del zt

    def make_x(idx):
        a, b = X20_get(idx)
        a[:, 11:14] = (b - zmu.astype(np.float32)) / zsd.astype(np.float32)
        return np.ascontiguousarray(a)

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
    Str = S[itr]
    shares = {}
    for sp in args.share:
        nm, fr = sp.split("=", 1)
        assert nm in names[1:], "--share names a non-reference or unknown sample: %s" % nm
        shares[nm] = float(fr)
    ssum = sum(shares.values())
    assert ssum < 1.0, "shares sum to %.4f" % ssum
    wref = float(wtr[Str == 0].sum())
    for nm, fr in shares.items():
        k = names.index(nm)
        wk = float(wtr[Str == k].sum())
        if fr <= 0.0 or wk <= 0.0:
            wtr[Str == k] = 0.0
            log("share %s = 0: its rows carry ZERO loss weight (reported, not trained)" % nm)
            continue
        f = (fr / (1.0 - ssum)) * wref / wk
        wtr[Str == k] *= f
        log("share rescale %s: target %.4f -> factor %.6f (ref train weight %.3e, %s %.3e)"
            % (nm, fr, f, wref, nm, wk))
    tot = float(wtr.sum())
    for k, nm in enumerate(names):
        log("train weight share by SAMPLE: %-6s %.4f" % (nm, float(wtr[Str == k].sum()) / tot))
    # pos_weight is fitted on the REFERENCE sample's train rows ONLY, for the same reason the slot
    # 11-13 standardization is: it must not move with the mix, or the one-sample control stops being
    # a nested case of the enriched arms and two things change at once.  With ONE --lab this is
    # every train row, i.e. bit-for-bit train_s3.py.
    m0 = Str == 0
    W_pos = float((wtr[m0] * ytr[m0]).sum())
    W_neg = float((wtr[m0] * (1 - ytr[m0])).sum())
    pos_weight = W_neg / max(W_pos, 1.0)
    log("train weighted (reference %s) %.4g true / %.4g fake -> pos_weight %.4f"
        % (names[0], W_pos, W_neg, pos_weight))

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
    Sva = S[iva]
    vsel = np.flatnonzero(stva != 1)          # val metric = the CHAIN universe (shipped choice)
    if len(vsel) > args.val_cap:
        vsel = np.sort(np.random.default_rng(args.seed + 1)
                       .choice(vsel, size=args.val_cap, replace=False))
    Xva_t = torch.tensor(Xva[vsel]).to(dev)
    yva_s, wva_s, Sva_s = yva[vsel], wva[vsel], Sva[vsel]
    log("val (chain universe) %d rows, %d true" % (len(vsel), int(yva_s.sum())))
    for k, nm in enumerate(names):
        mk = Sva_s == k
        log("  val sample %-6s %d rows %d true" % (nm, mk.sum(), int(yva_s[mk].sum())))

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
                sv.append(tonp(deployed_scalar(model(Xva_t[i:i + (1 << 20)]), args.arm)))
            sv = np.concatenate(sv)
        m = yva_s > 0.5
        a_all = wauc(sv[m], wva_s[m], sv[~m], wva_s[~m])
        per = {}
        for k, nm in enumerate(names):
            mk = Sva_s == k
            if (mk & m).sum() < args.sel_min_rows or (mk & ~m).sum() < args.sel_min_rows:
                per[nm] = None
                continue
            per[nm] = wauc(sv[mk & m], wva_s[mk & m], sv[mk & ~m], wva_s[mk & ~m])
        usable = [v for v in per.values() if v is not None]
        # ONE sample -> this is bit-for-bit train_s3.py's selector (a_all over that sample).
        a = a_all if len(names) == 1 else (min(usable) if usable else a_all)
        hist.append({"epoch": ep, "loss": tl / max(tw, 1e-9), "val_auc": a, "val_auc_all": a_all,
                     "val_auc_per_sample": per})
        log("epoch %3d loss=%.5f sel=%.6f all=%.6f %s lr=%.2e"
            % (ep, tl / max(tw, 1e-9), a, a_all,
               " ".join("%s=%s" % (nm, "n/a" if per[nm] is None else "%.6f" % per[nm])
                        for nm in names), opt.param_groups[0]["lr"]))
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
            ste.append(tonp(deployed_scalar(
                model(torch.tensor(Xte[i:i + (1 << 20)]).to(dev)), args.arm)))
        ste = np.concatenate(ste)
    yte, wte, stte, vte = y[ite], w[ite], st[ite], vxy[ite]
    Ste = S[ite]
    for k, nm in enumerate(names):
        mk = Ste == k
        mt = mk & (yte == 1)
        mf = mk & (yte == 0)
        aa = wauc(ste[mt], wte[mt], ste[mf], wte[mf])
        res["SAMPLE " + nm] = {"auc": aa, "n_true": int(mt.sum()), "n_fake": int(mf.sum())}
        log("  SAMPLE %-6s n_true=%8d n_fake=%9d AUC=%s"
            % (nm, mt.sum(), mf.sum(), "n/a" if aa is None else "%.5f" % aa))
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
