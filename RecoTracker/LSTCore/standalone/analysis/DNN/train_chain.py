#!/usr/bin/env python3
"""Train the chain gate: the three-class head that judges a welded chain.

WHAT IT TRAINS
    A 25 -> hidden -> hidden -> 3 MLP over the chain feature row, with classes 0 = fake,
    1 = prompt-true (simVxy < 1 cm), 2 = displaced-true (simVxy >= 1 cm).  Every deployed consumer
    reads a logit MARGIN (mP = zPrompt - zFake, mD = zDisp - zFake, mX = max(zPrompt, zDisp) -
    zFake), never a probability, so only differences between the three outputs matter.
    Samples are trained jointly: --lab is repeatable and the FIRST one is the REFERENCE.  Its
    train split alone fits the standardization and its own class counts fit its pos_weight, so a
    mix is a nested case of the single-sample run and the only thing the mix changes is the loss.
    Row COUNTS are not the knob -- two samples of similar size can differ wildly in composition
    (how much of the sample the gate kills, the layer-count mix, the true-chain prevalence), so a
    naive concatenation would hand one of them a share of the gradient nobody chose.  Instead
    --share NAME=frac gives each non-reference sample an explicit fraction of the TOTAL TRAIN LOSS
    WEIGHT and the reference keeps the rest; --jet-share is the collective form of the same thing.

INPUTS (NOT IN THE REPOSITORY)
    --lab NAME=dir points at a labelled chain corpus directory holding the feature matrix and a
    meta.npz with `evt`, `label`, `vxy`, `simIdx`, `nLayers`, `frac`, `nUniq`, and `isCore` when
    the sample has jet-core truth.  Those directories are built offline from the chain-level
    sidecar the binary writes when LST_CHAIN_CHAIN_DUMP names a file (one record per chain: its
    geometry, its gate logits and margins, its 25-feature row, its member nodes and hit rows),
    joined to sim truth.  Neither the dumps nor the labeller are part of this directory.
    `train_chain_base.py`, imported below for COND / build_inputs / event_split, is likewise not present in
    this checkout, so the import at the top fails on a fresh clone.
    The split is per sample and by EVENT, so no event contributes rows to two splits.  Where a
    sample is split by hand into a tuning half and a sealed holdout (the jet sample: events 0-499
    tune, 500-999 holdout), only the tuning half is ever labelled into a corpus directory.

OUTPUTS
    models/chain3_<tag>.pt         best weights, arch, feature names, seed, conditioning spec
    models/chain3_norm_<tag>.json  feature names, conditioning, mean/std, class meanings and the
                                   full weighting spec
    models/hist_<tag>.npy          per-epoch history
    export_chain_weights.py consumes the .pt and the norm json and writes
    src/alpaka/ChainNetworkWeights.h.
    The attach head takes this head's three RAW logits as its inputs 11-13, so retraining the gate
    requires rerunning train_attach.py.

SELECTION
    The epoch selector is the MINIMUM over the samples in the mix of (AUC(mP, prompt vs fake),
    AUC(mD, displaced vs fake)), so an epoch that buys one sample by giving up another cannot be
    selected.  A sample enters the selector only if its validation split has at least
    --sel-min-rows rows in EACH of the three classes; without that guard a sample contributing a
    few hundred rows could drive model selection for the whole head.  The jet-core AUC (mX between
    core-true chains and that sample's fakes) is reported every epoch but never selected on.

RUN
    python3 train_chain.py --tag NAME --lab pu=<dir> [--lab jet=<dir> --share jet=0.25]
        [--jet-share L] [--loss m12|lst] [--sched const|cos] [--epochs N] [--patience 15]
        [--lr X] [--hidden 32] [--batch-size 16384] [--seed 42] [--dw-mid 8] [--dw-hi 16]
        [--jet-tier] [--dup-mode none|norm|demote --dup-f F --dup-scope all|shorter
        --dup-on NAMES] [--only5 NAMES --w4 X] [--flat NAMES] [--sel-min-rows N]
    --epochs 0 and --lr 0 mean "take the schedule's own default": 120 epochs at 1e-3 for const,
    300 at 3e-3 for cos.  Early stopping on --patience applies to the const schedule only; a
    cosine run is meant to reach the end of its decay.
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
# The shared chain-gate recipe -- COND (the conditioning spec baked into the exported header),
# build_inputs (dir -> feature matrix, feature names, meta) and event_split -- lives in train_chain_base.py,
# which must sit next to this file: Python puts the script's own directory on sys.path, and no
# other path is searched.
from train_chain_base import COND, build_inputs, event_split  # noqa: E402


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
    p.add_argument("--share", action="append", default=[],
                   help="NAME=frac, repeatable: per-sample share of the total train loss weight")
    # The dup-aware term groups the chains of one sim, keyed by (sample, evt, simIdx) over TRUE
    # rows. `norm` divides each true row's weight by its group size, so a sim with eight redundant
    # chains contributes the gradient of one; `demote` softly relabels every non-best member of a
    # group toward the fake class, loss_row = (1-f)*CE(z, y_row) + f*CE(z, 0), with the best member
    # picked by an OBJECTIVE quality key (nLayers, then frac, then nUniq) rather than by the head's
    # own score, which would make the target chase the model. Both are per-row weight/label
    # transformations, so the training loop, the batching and the RNG stream are untouched, and
    # mode none / --dup-f 0 is the untouched baseline.
    p.add_argument("--dup-mode", choices=["none", "norm", "demote"], default="none")
    p.add_argument("--dup-f", type=float, default=1.0,
                   help="demote mixing fraction toward the fake class (0 = baseline)")
    p.add_argument("--dup-scope", choices=["all", "shorter"], default="all",
                   help="demote every non-best group member (all) or only those with strictly "
                        "fewer LAYERS than their group's best (shorter)")
    p.add_argument("--dup-on", default="",
                   help="comma-separated sample names the dup term applies to (default: all)")
    p.add_argument("--sel-min-rows", type=int, default=500)
    p.add_argument("--w4", type=float, default=0.0,
                   help="weight MULTIPLIER for the 4-layer rows of the --only5 samples. 0 drops "
                        "them from the loss entirely, 1 keeps them at full weight, and values in "
                        "between interpolate.")
    p.add_argument("--only5", default="",
                   help="comma-separated sample names whose 4-LAYER rows are scaled by --w4. What "
                        "a mixed-in sample teaches about 4-layer topology can show up as extra "
                        "4-layer fakes on the reference sample; scaling those rows down leaves the "
                        "4-layer cells governed by the reference sample alone.")
    p.add_argument("--flat", default="",
                   help="comma-separated sample names whose rows get UNIFORM weight 1 instead of "
                        "the m12 per-sample pos_weight. Needed for a sample that is almost all "
                        "TRUE: pos_weight = n_fake/n_true then falls far below 1 and the m12 "
                        "convention would hand the sample's whole share to its few fake rows.")
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
    gk, gq, nls = [], [], []  # same-sim group key, objective quality key, nLayers per row
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
        # same-sim group key: (sample, evt, simIdx) for TRUE rows, -1 elsewhere.  The sample index
        # is in the key so two samples' (evt, simIdx) pairs can never collide.
        key = np.full(len(X), -1, np.int64)
        ist = (y3 > 0) & (M["simIdx"] >= 0)
        key[ist] = ((int(k) << 56) | (M["evt"][ist].astype(np.int64) << 24)
                    | M["simIdx"][ist].astype(np.int64))
        gk.append(key)
        # objective quality key for "best member of the group": nLayers, then frac, then nUniq
        nls.append(M["nLayers"].astype(np.int16))
        gq.append((M["nLayers"].astype(np.float64) * 1e6
                   + M["frac"].astype(np.float64) * 1e3 + M["nUniq"].astype(np.float64)))
    X = np.concatenate(Xs)
    GK = np.concatenate(gk)
    GQ = np.concatenate(gq)
    NL = np.concatenate(nls)
    del gk, gq, nls
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
        if nm in [s for s in a.flat.split(",") if s]:
            spec["per_sample"][nm] = {"pos_weight": 1.0, "flat": True, "counts_train": [n0, n1, n2]}
            log("  %s: FLAT per-row weight 1 (no class reweighting, no displaced tiering)" % nm)
            continue
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
        # The tiered displaced boost applies to the REFERENCE sample only, unless --jet-tier. It
        # exists to serve displaced tracks from the primary interaction region, and a jet sample's
        # "displaced-true" population is a different thing: in-jet decays and nuclear interactions
        # make roughly a quarter of its true chains displaced, against about one in eighty on
        # PU200. Tiering that block would put most of its gradient on in-jet displacement and turn
        # what it has to teach about dense-core topology into a lesson about displacement instead.
        # --jet-tier exists so the claim can be measured rather than only asserted.
        if a.loss == "m12" and (k == 0 or a.jet_tier):
            it = m & (y3 > 0)
            w[it & (vxy_all >= 1.0) & (vxy_all < 5.0)] *= a.dw_mid
            w[it & (vxy_all >= 5.0)] *= a.dw_hi
    del vxy_all

    # ---- --only5: scale the 4-layer rows of the named samples by --w4 --------------------------
    for nm in [x for x in a.only5.split(",") if x]:
        k = [i for i, (n, _) in enumerate(labs) if n == nm]
        assert k, "--only5 names a sample not in the mix: %s" % nm
        m = (S == k[0]) & (NL < 5)
        w[m] *= a.w4
        log("only5 %s: %d rows with nLayers < 5 scaled by w4=%.3f (%d rows with nLayers >= 5 kept)"
            % (nm, int(m.sum()), a.w4, int(((S == k[0]) & (NL >= 5)).sum())))
    spec["only5"] = {"samples": [x for x in a.only5.split(",") if x], "w4": a.w4}

    # ---- the dup-aware term, applied BEFORE the share rescale so the shares stay exact ---------
    fmix = np.zeros(len(Xn), dtype=np.float32)      # per-row mixing fraction toward class 0
    dup_on = [s for s in a.dup_on.split(",") if s] or [nm for nm, _ in labs]
    spec["dup"] = {"mode": a.dup_mode, "f": a.dup_f, "on": dup_on}
    if a.dup_mode != "none":
        onmask = np.zeros(len(Xn), bool)
        for k, (nm, _) in enumerate(labs):
            if nm in dup_on:
                onmask |= (S == k)
        gm = (GK >= 0) & onmask
        uk, inv, cnt = np.unique(GK[gm], return_inverse=True, return_counts=True)
        gsz = cnt[inv]
        if a.dup_mode == "norm":
            # Divide by group size, then RESTORE the total weight of the affected rows PER SAMPLE.
            # Without the restore the true rows would lose weight by the mean multiplicity of the
            # sample, which shifts its fake/true balance as well; with it, the term is a pure
            # REDISTRIBUTION among one sim's redundant chains and nothing else moves.
            wnew = w[gm] / gsz.astype(np.float32)
            sgm = S[gm]
            for k in range(len(labs)):
                sm = sgm == k
                if not sm.any():
                    continue
                b, aft = float(w[gm][sm].sum()), float(wnew[sm].sum())
                if aft > 0:
                    wnew[sm] *= b / aft
            w[gm] = wnew
            spec["dup"]["groups"] = int(len(uk))
            spec["dup"]["rows"] = int(gm.sum())
            spec["dup"]["preserve_sample_weight"] = True
            log("dup-norm: %d same-sim groups over %d true rows (mean multiplicity %.3f), "
                "weight divided by group size then per-sample total RESTORED"
                % (len(uk), int(gm.sum()), gsz.mean()))
        elif a.dup_scope == "shorter":
            # Demote ONLY members with strictly FEWER LAYERS than their group's best: the duplicate
            # this form targets is "a 4-layer chain of a sim whose 5-layer chain is also
            # delivered". Equal-length siblings of the best member are left alone.
            nl = np.floor(GQ[gm] / 1e6)
            best = np.zeros(len(uk), np.float64)
            np.maximum.at(best, inv, nl)
            dem = np.nonzero(gm)[0][nl < best[inv]]
            fmix[dem] = a.dup_f
            spec["dup"]["groups"] = int(len(uk))
            spec["dup"]["rows"] = int(gm.sum())
            spec["dup"]["demoted_rows"] = int(len(dem))
            log("dup-demote(shorter) f=%.3f: %d groups, %d true rows, %d DEMOTED (strictly fewer "
                "layers than their group best)" % (a.dup_f, len(uk), int(gm.sum()), len(dem)))
        else:
            # Best member per group by the objective quality key; ties go to the lowest row index.
            q = GQ[gm]
            best = np.zeros(len(uk), np.float64)
            np.maximum.at(best, inv, q)
            isbest = q >= best[inv]
            # Break remaining ties: keep only the FIRST row that attains the group max.
            first = np.full(len(uk), np.iinfo(np.int64).max, np.int64)
            idx = np.nonzero(gm)[0]
            np.minimum.at(first, inv[isbest], idx[isbest])
            keep = idx == first[inv]
            dem = idx[~keep]
            fmix[dem] = a.dup_f
            spec["dup"]["groups"] = int(len(uk))
            spec["dup"]["rows"] = int(gm.sum())
            spec["dup"]["demoted_rows"] = int(len(dem))
            log("dup-demote f=%.3f: %d same-sim groups, %d true rows, %d DEMOTED (soft) "
                "(mean multiplicity %.3f)" % (a.dup_f, len(uk), int(gm.sum()), len(dem), gsz.mean()))
        del uk, inv, cnt, gsz

    # ---- per-sample loss-weight shares ---------------------------------------------------------
    names_ns = [nm for nm, _ in labs[1:]]
    shares = {}
    for s in a.share:
        nm, fr = s.split("=", 1)
        assert nm in names_ns, "--share names a non-existent non-reference sample: %s" % nm
        shares[nm] = float(fr)
    if not shares and len(labs) > 1:
        # Collective --jet-share: one number for all non-reference samples, split between them in
        # proportion to the train weight they carry before the rescale.
        wns = {nm: float(w[(S == k + 1) & tr].sum()) for k, nm in enumerate(names_ns)}
        tw = sum(wns.values())
        for nm in names_ns:
            shares[nm] = a.jet_share * (wns[nm] / tw if tw > 0 else 0.0)
    for nm in names_ns:
        shares.setdefault(nm, 0.0)
    ssum = sum(shares.values())
    assert ssum < 1.0, "shares sum to %.4f" % ssum
    spec["share_targets"] = shares
    spec["jet_share_target"] = ssum
    if len(labs) > 1:
        wref = float(w[(S == 0) & tr].sum())
        for k, nm in enumerate(names_ns):
            m = S == k + 1
            wk = float(w[m & tr].sum())
            if shares[nm] <= 0.0 or wk <= 0.0:
                w[m] = 0.0
                spec["per_sample"][nm]["rescale_factor"] = 0.0
                log("share %s = 0: its rows carry ZERO loss weight (reported, not trained)" % nm)
                continue
            f = (shares[nm] / (1.0 - ssum)) * wref / wk
            w[m] *= f
            spec["per_sample"][nm]["rescale_factor"] = f
            log("share rescale %s: target %.4f -> factor %.6f (ref train weight %.3e, %s %.3e)"
                % (nm, shares[nm], f, wref, nm, wk))
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
    use_soft = a.dup_mode == "demote" and a.dup_f > 0.0
    ftr = torch.tensor(np.ascontiguousarray(fmix[trk])).to(dev) if use_soft else None
    y0tr = torch.zeros_like(ytr) if use_soft else None
    log("train rows %d of %d (zero-weight rows dropped)%s"
        % (int(trk.sum()), int(tr.sum()),
           ("; soft-demoted train rows %d" % int((fmix[trk] > 0).sum())) if use_soft else ""))

    inmix = []
    for k, (nm, _) in enumerate(labs):
        if float(w[tr & (S == k)].sum()) <= 0:
            continue
        nrow = [int(((S == k) & va & (y3 == c)).sum()) for c in (0, 1, 2)]
        if min(nrow) < a.sel_min_rows:
            log("SELECTOR: %s EXCLUDED (val rows per class %s < --sel-min-rows %d); its AUCs are "
                "reported but cannot select an epoch" % (nm, nrow, a.sel_min_rows))
            continue
        inmix.append(k)
    spec["selector_samples"] = [labs[k][0] for k in inmix]
    log("SELECTOR over: %s" % spec["selector_samples"])
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
        """Mann-Whitney U with TIE-AVERAGED ranks, i.e. numerically sklearn's roc_auc_score."""
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
        # The jet-core cell: core-true chains against the fakes of that same sample.
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
            if use_soft:
                out = model(Xtr[idx])
                fm = ftr[idx]
                l = (1.0 - fm) * crit(out, ytr[idx]) + fm * crit(out, y0tr[idx])
                loss = (l * wtr[idx]).mean()
            else:
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
