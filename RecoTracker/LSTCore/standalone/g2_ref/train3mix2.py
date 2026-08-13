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

=====================  G (round 2) ADDITIONS -- three, all nested at their defaults  ===========

 1. `--share NAME=frac` (repeatable) replaces the single collective `--jet-share` when there are
    THREE or more samples: each non-reference block is rescaled INDEPENDENTLY to its own share of
    the total train loss weight (P3's named six-line change).  `--jet-share L` is kept and is
    exactly `--share <all non-reference>=L` split proportionally, so every round-1 arm is
    reproducible from this file.  The reference sample keeps 1 - sum(shares).

 2. `--dup-mode {none,norm,demote}` -- the [P3 CLOSED] dup-aware-loss hook.  The same-sim group is
    `(evt, simIdx)` over TRUE rows (dupproxy.py's key), computed per sample.
      norm    : each true row's weight is divided by the size of its same-sim group, so a sim with
                eight redundant chains contributes the gradient of one.
      demote f: SOFT relabel of every non-best member of a group,
                loss_row = (1-f)*CE(z, y_row) + f*CE(z, 0).  The best member is the max by
                (nLayers, frac, nUniq) -- an OBJECTIVE quality key, not the deployed head's own
                score, which would make the target off-policy.  f = 0 is bit-for-bit the baseline.
    Both are per-row transformations, so the training loop, the batching and the RNG stream are
    untouched.  `--dup-on NAME[,NAME]` restricts either to named samples (default: all).

 3. `--sel-min-rows N` -- a sample enters the SELECTOR only if its val split has >= N rows in each
    of (fake, prompt, displaced); otherwise its AUCs are REPORTED but cannot select an epoch.  This
    exists because the gun samples contribute only a few hundred rows per class and a 300-row AUC
    would otherwise drive model selection for the whole head.  With two large samples it is inert.
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
    p.add_argument("--share", action="append", default=[],
                   help="NAME=frac, repeatable: per-sample share of the total train loss weight")
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
                   help="weight MULTIPLIER for the 4-layer rows of the --only5 samples. 0 = drop "
                        "them entirely (the A2L5 endpoint), 1 = keep them (the A02 endpoint); "
                        "intermediate values interpolate between the two measured endpoints.")
    p.add_argument("--only5", default="",
                   help="comma-separated sample names whose 4-LAYER rows are dropped from the loss "
                        "(weight 0). Motivated by the localization at [G 21:05]: 73% of A02's PU200 "
                        "fake excess is T4-class TCs, so the enrichment's lesson about 4-layer "
                        "topology is the thing to remove, leaving the 4-layer cells governed by "
                        "PU200 alone.")
    p.add_argument("--flat", default="",
                   help="comma-separated sample names whose rows get UNIFORM weight 1 instead of "
                        "the m12 per-sample pos_weight. Required for the gun samples: they are "
                        "98.9%% TRUE, so pos_weight = n_fake/n_true = 0.011 and the m12 convention "
                        "would hand the sample's whole share to its 21 fake rows.")
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

    # ---- G: --only5, the localization-driven mix filter -----------------------------------------
    for nm in [x for x in a.only5.split(",") if x]:
        k = [i for i, (n, _) in enumerate(labs) if n == nm]
        assert k, "--only5 names a sample not in the mix: %s" % nm
        m = (S == k[0]) & (NL < 5)
        w[m] *= a.w4
        log("only5 %s: %d rows with nLayers < 5 scaled by w4=%.3f (%d rows with nLayers >= 5 kept)"
            % (nm, int(m.sum()), a.w4, int(((S == k[0]) & (NL >= 5)).sum())))
    spec["only5"] = {"samples": [x for x in a.only5.split(",") if x], "w4": a.w4}

    # ---- G: the dup-aware term, applied BEFORE the share rescale so the shares stay exact -------
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
            # divide by group size, then RESTORE the total weight of the affected rows PER SAMPLE.
            # Without the restore step this would also shift the fake/true balance inside the
            # protected sample (multiplicity 2.39 on PU200 -> trues lose 2.4x of the gradient), and
            # the arm would no longer be a pure REDISTRIBUTION among a sim's redundant chains, which
            # is the only thing it is supposed to test.
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
            # demote ONLY members with strictly FEWER LAYERS than their group's best. Motivation:
            # every enrichment arm of round 1 inflated the T4-class TC count (+3.0% to +6.7%) while
            # the PU200 duplicate rate rose, so the suspect duplicate is "a 4-layer chain of a sim
            # whose 5-layer chain is also delivered". This form leaves equal-length siblings alone.
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
            # best member per group by the objective quality key; ties -> lowest row index
            q = GQ[gm]
            best = np.zeros(len(uk), np.float64)
            np.maximum.at(best, inv, q)
            isbest = q >= best[inv]
            # break remaining ties: keep only the FIRST row that attains the group max
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
        # legacy collective --jet-share: split proportionally to the samples' own train weight
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
