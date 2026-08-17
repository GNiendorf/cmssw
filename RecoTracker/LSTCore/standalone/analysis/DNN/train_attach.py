#!/usr/bin/env python3

"""Train the attach head: the MLP that decides whether a pixel line segment belongs to a chain.

WHAT IT TRAINS
    A 22 -> hidden -> hidden -> (1 or 3) MLP over the pair rows the binary scored, one row per
    (target, pixel seed) pair that survived the geometric prefilter.  The target is either a chain
    or a bare triplet.
        --arm scalar  one logit, the deployed form
        --arm three   three logits (fake / prompt-true / displaced-true), from which the deployed
                      scalar is logsumexp(zPrompt, zDisp) - zFake, i.e. the log-odds of "true of
                      either kind".  logsumexp and not max(): max() discards WHICH class won, and
                      telling displaced from prompt is the whole reason the three outputs exist.
    Objective: weight-weighted BCE (cross-entropy in the three arm) at pos_weight, a ONE-SIDED
    focal term --gamma-neg applied to negatives only, and displaced-true weight tiers --disp-mid /
    --disp-hi keyed on the vxy of the sim the pair shares.

    THE PAIR ROW IS 22 FLOATS WIDE (interface/ChainConfig.h kAttachFeatures = 22).  Slots 0-10 and
    14-21 are taken from the dump VERBATIM: they are already standardized with the deployed
    header's constants, so those columns keep train == serve with no de/re-standardize round trip.
    Only columns 11:14 are overwritten, with the target chain's three RAW gate logits
    (zFake, zPrompt, zDisp) re-standardized on constants fitted here (no clip, no log10p1).  Code
    that assumes a 20-wide row and splices [x[:, :11], z, x[:, 12:]] instead is SILENTLY WRONG on
    these rows: it drops slot 11 and shifts eight columns.
    A bare-triplet target has no chain gate, so its three logit slots carry the 0 sentinel; input
    20 (target type) is what flags the absence.

INPUTS (NOT IN THE REPOSITORY)
    --lab NAME=dir, repeatable; the FIRST is the reference sample (use pu=...).  Each directory is
    an output of label_attach.py and holds row-aligned columns: X.npy (rows x 22), z3.npy (the
    target's three raw gate logits), y.npy (1 iff target and pixel seed share a sim), st.npy
    (stage: 0 = chain of at least 5 layers, 1 = bare triplet, 2 = auxiliary 4-layer chain),
    vxy.npy, evt.npy.
    label_attach.py in turn consumes three binary sidecars of ONE single-stream instrumented run
    (the scored pairs, the chain records, and the node/seed join) plus the tracking ntuple they
    were produced from.  Those dumps are not in the repository and the pair sidecar is not written
    by the committed code, so this script cannot run on a fresh checkout.
    Bare-triplet pairs are kept 1-in-16 in the dump, so a stage-1 row carries weight 16 and stage
    0/2 rows weight 1.
    Each sample gets its own seed-driven 60/20/20 split BY EVENT.  Where a sample is split by hand
    into a tuning half and a sealed holdout, only the tuning half is labelled into a corpus.

OUTPUTS
    --out <path>          checkpoint: weights, arch, arm, feature names, the slot 11-13
                          standardization, and the args
    <path>_norm.json      the full 22-slot norm, built as the deployed norm with ONLY
                          mean/std[11..13] replaced, so it cannot silently disagree with the
                          header in the tree
    <path>.json           args, per-epoch history, and the frozen TEST report broken down by
                          sample, by stage and by vxy band
    export_attach_weights.py turns the checkpoint and the norm json into
    src/alpaka/AttachNetworkWeights.h.
    The eight ChainConfig bars that live on this head's logit scale are NOT produced here:
    barfit_attach.py fits them from the same corpus directory and this checkpoint.
    The head reads the chain gate's three raw logits as its inputs 11-13, so RETRAINING THE GATE
    REQUIRES RERUNNING THIS.

MIXING AND SELECTION
    --share NAME=frac gives a non-reference sample that fraction of the TOTAL TRAIN LOSS WEIGHT
    and the reference keeps the rest; a share of 0 zeroes the sample's rows, which are then
    reported but not trained on.  The slot 11-13 standardization AND pos_weight are fitted on the
    REFERENCE train rows ALONE, so a mix is a nested case of the single-sample run and only the
    loss share moves.  The validation selector is the MINIMUM over the samples of the weighted AUC
    over the CHAIN universe (stage != 1), so an epoch that buys one sample by giving up another
    cannot be selected; with a single --lab it is simply that sample's own AUC.

RUN
    python3 train_attach.py --lab pu=<dir> [--lab jet=<dir> --share jet=0.25] --out models/x.pt
        [--arm scalar|three] [--seed 42] [--epochs 60] [--patience 8] [--hidden 24]
        [--batch-size 16384] [--lr 1e-3] [--sched const|cos --lr-min 1e-5] [--gamma-neg 2]
        [--disp-mid 8] [--disp-hi 16] [--chain-weight 1] [--class-weight m19|lst] [--val-cap N]
        [--drop-aux] [--frac F] [--sel-min-rows N]

    SHIPPED_NORM22 below is read as the template for the norm json.  It resolves to a path outside
    this directory and must exist, or the run dies at the very end after training.
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


# The 22-slot head-input contract. Slots 0..10 and 14..21 keep the deployed names AND the deployed
# norm constants verbatim (the dump's x is already standardized with them, so train == serve by
# construction); 11/12/13 are the three raw gate logits and take constants fitted here. The norm
# json is therefore written as the deployed 22-slot norm with only mean/std[11..13] replaced, which
# is one dependency fewer than rebuilding it and cannot silently disagree with the header in tree.
SHIPPED_NORM22 = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                              "nnloop_ref", "s3_work", "models", "A2_cos3e3_norm.json")
# [ARM-RETRAIN] The 23-slot template lives IN this directory (a copy of the deployed
# AttachNetworkWeights.h's own norm json), so a 23-wide retrain has no dependency outside it.
# 23 is the CURRENT deployed width: af_dBeta was promoted out of the pair dump's probe columns
# into kAttachFeatures, so a current dump has nProbe = 0 and the column arrives already
# standardized with the deployed constants, exactly like slots 0-10 and 14-21.
SHIPPED_NORM23 = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "attach_norm23_template.json")
NFEAT = 22        # set from the corpus in main(); see the note there
NEW_NAMES = ["af_chainGateZFake", "af_chainGateZPrompt", "af_chainGateZDisp"]


def write_norm(path, zmu, zsd, use_cols_tag, nfeat=22):
    sh = json.load(open(SHIPPED_NORM23 if nfeat == 23 else SHIPPED_NORM22))
    names = sh["feature_names"]
    assert len(names) == nfeat and names[11:14] == NEW_NAMES, (len(names), names[11:14])
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
                   help="exclude stage-2 (auxiliary 4-layer chain) rows from TRAINING; they stay "
                        "in the val and test reports")
    p.add_argument("--frac", type=float, default=1.0, help="row subsample for a quick probe")
    p.add_argument("--lowpt-w", type=float, default=1.0,
                   help="[ARM-RETRAIN] multiply the TRAIN loss weight of every row (true and fake "
                        "alike) whose SEED has ptIn < --lowpt-max and |eta| >= --lowpt-etamin. "
                        "1.0 = off = the shipped recipe.")
    p.add_argument("--lowpt-max", type=float, default=2.0, help="seed ptIn ceiling of that cell")
    p.add_argument("--lowpt-etamin", type=float, default=1.1, help="|seed eta| floor of that cell")
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
    petas = []
    # Each sample gets its OWN event split, drawn from the same seed. Splitting by event and not by
    # row is what keeps the pairs of one event out of two different splits.
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
        petas.append(np.load(d + "/peta.npy"))
        rs.append(rolek[ek]); sids.append(np.full(len(yk), k, np.int8))
        log("lab %-6s %-60s rows %d true %d (%.4f) events %d split %d/%d/%d"
            % (nm, d, len(yk), yk.sum(), yk.mean(), nevk, ntrk, nvak, nevk - ntrk - nvak))
        if k == 0:
            ntr, nva, nev = ntrk, nvak, nevk
    y = np.concatenate(ys); st = np.concatenate(sts); vxy = np.concatenate(vxys)
    peta = np.abs(np.concatenate(petas))
    r = np.concatenate(rs); S = np.concatenate(sids)
    off = np.concatenate(([0], np.cumsum([len(v) for v in ys])))
    n = len(y)
    # [ARM-RETRAIN] The head width comes from the CORPUS, never from a constant: the deployed head
    # went 22 -> 23 when af_dBeta was promoted out of the probe columns, and a hardcoded 22 would
    # train a head one column narrower than the one the C++ evaluates, with no error anywhere.
    global NFEAT
    NFEAT = int(Xs[0].shape[1])
    for k in range(1, len(labs)):
        assert Xs[k].shape[1] == NFEAT, "corpora disagree on head width: %s" % (
            [int(v.shape[1]) for v in Xs],)
    log("head input width from corpus: %d" % NFEAT)
    log("rows %d true %d (%.4f) over %d sample(s)" % (n, y.sum(), y.mean(), len(labs)))
    for s in (0, 1, 2, 3):
        m = st == s
        if m.any():
            log("  stage %d rows %d true %d (%.4f)" % (s, m.sum(), y[m].sum(), y[m].mean()))

    def X20_get(idx, block=1 << 23):
        """Gather rows of the concatenated corpus out of the per-sample memmaps.

        SEQUENTIALLY, in source-order blocks.  Fancy-indexing a memmap of tens of GB with tens of
        millions of sorted indices degenerates to per-row reads and takes longer than the training
        does; reading whole blocks and selecting inside them is the same result at streaming speed.
        `idx` must be sorted ascending, which every caller here guarantees (np.flatnonzero).
        Returns the feature rows and the matching raw gate logits, in the order `idx` gives them.
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
    # Undo the dump's own sampling: chain and aux-4-layer pairs are kept whole, bare-triplet pairs
    # (stage 1) are kept 1-in-16, so each surviving stage-1 row stands for sixteen.
    w = np.where(st == 1, 16.0, 1.0).astype(np.float32)

    keep = np.ones(n, bool)
    if args.drop_aux:
        keep &= (st != 2) | (r != 0)
    if args.frac < 1.0:
        keep &= rng.random(n) < args.frac

    itr = np.flatnonzero((r == 0) & keep)
    # --frac subsamples val and test as well as train, so a probe run stays a probe run end to end.
    # It is inert at frac = 1.0.
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
    # [ARM-RETRAIN] LOW-pT / OUTSIDE-BARREL CELL EMPHASIS, default OFF (--lowpt-w 1).
    # The deficit this retrain serves is a low-pT, outside-barrel block (748 master-pT5 sims we
    # deliver bare at 1 <= |eta| < 2, 533 of them below 2 GeV), and those cells are a rounding
    # error of the corpus, so the unweighted fit spends its capacity elsewhere. The multiplier
    # is applied to TRUE AND FAKE ROWS ALIKE inside the cell, so the local positive/negative
    # balance -- and therefore the head's calibration inside the cell -- is untouched; only the
    # cell's share of the total loss moves. It is applied BEFORE the --share rescale, so the
    # per-sample loss shares stay exactly where --share puts them.
    # The conditioner is the SEED's own reconstructed ptIn and |eta| (the same two quantities the
    # deployed delivery table is binned on), recovered from the standardized feature 0 with the
    # deployed norm constants. No simulation quantity enters.
    if args.lowpt_w != 1.0:
        shnorm = json.load(open(SHIPPED_NORM23 if NFEAT == 23 else SHIPPED_NORM22))
        mu0, sd0 = float(shnorm["mean"][0]), float(shnorm["std"][0])
        assert shnorm["feature_names"][0] == "af_log10PtIn", shnorm["feature_names"][0]
        ptin = np.power(10.0, Xtr[:, 0].astype(np.float64) * sd0 + mu0)
        cell = (ptin < args.lowpt_max) & (peta[itr] >= args.lowpt_etamin)
        wtr[cell] *= args.lowpt_w
        log("lowpt emphasis: ptIn < %g and |seed eta| >= %g -> x%g on %d/%d train rows "
            "(%.4f), of which true %d"
            % (args.lowpt_max, args.lowpt_etamin, args.lowpt_w, int(cell.sum()), len(cell),
               cell.mean(), int((cell & tru).sum())))
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
    # 11-13 standardization is: it must not move with the mix, or a mixed run stops being a nested
    # case of the single-sample one and two things change at once. With ONE --lab this is every
    # train row anyway.
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
    vsel = np.flatnonzero(stva != 1)          # the val metric lives in the CHAIN universe only
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
    model = build_model(NFEAT, args.hidden, n_out).to(dev)
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
            # m19 convention: the fake/true balance is carried by pos_weight alone, and the two
            # true classes are left with the displaced tiers already folded into wtr.
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
        # With one sample the selector is just that sample's own AUC over the whole val universe.
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
    # [ARM-RETRAIN] the three cell reports below are the ones this round is judged on: the deficit
    # block is stage-A prompt pairs whose SEED sits outside the barrel, and inside that, below 2 GeV.
    shn = json.load(open(SHIPPED_NORM23 if NFEAT == 23 else SHIPPED_NORM22))
    pt_te = np.power(10.0, Xte[:, 0].astype(np.float64) * float(shn["std"][0]) + float(shn["mean"][0]))
    eta_te = peta[ite]
    out_te = eta_te >= 1.1
    for tag, sel in (("ALL", np.ones(len(ite), bool)),
                     ("chain5+ (stage 0)", stte == 0),
                     ("bareT3 (stage 1)", stte == 1),
                     ("aux4L (stage 2)", stte == 2),
                     ("rescue (stage 3)", stte == 3),
                     ("A out |eta|>=1.1", (stte == 0) & out_te),
                     ("A out ptIn<2", (stte == 0) & out_te & (pt_te < 2.0)),
                     ("A out ptIn>=2", (stte == 0) & out_te & (pt_te >= 2.0)),
                     ("A barrel", (stte == 0) & ~out_te)):
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
                       "s3_%dinput_3gatelogits" % NFEAT, nfeat=NFEAT)
    _t.save({"state_dict": {k: v.cpu() for k, v in model.state_dict().items()},
             "arch": [NFEAT, args.hidden, args.hidden, n_out], "arm": args.arm,
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
