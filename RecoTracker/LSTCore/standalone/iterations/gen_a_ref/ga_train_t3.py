#!/usr/bin/env python3
"""GEN-A: train the DEDICATED BARE-T3 (pT3-class) attach head and export its C++ header.

WHY A SEPARATE HEAD
The shipping r2 head was trained on both target universes but SELECTED on a CHAIN-pair
validation metric, so nothing guaranteed its calibration for 3-layer targets; and its
19-slot feature layout carries no T3 quality information and only single-point pair
residuals. This trains a head on the bare-T3 universe ONLY, on
[19 frozen base slots ++ 12 AttachT3Extra slots], and SELECTS on a bare-T3 metric in the
regime the pipeline actually operates in.

MODEL SELECTION -- the point of the exercise
Global AUC is the wrong knob here: the delivery accepts O(1e-5) of the enumerated pairs,
so what matters is the far tail. Selection metric = weighted TPR at weighted FPR 1e-5
(reported alongside TPR@1e-4 and AUC). Weights are the dump's inverse fake-sampling
weights, so every number describes the FULL undownsampled deployed population.

Discipline kept from train_attach_gen.py: fixed seeds, EVENT-level split against the
FROZEN test-60 list, conditioning before standardization recorded for the C++ port,
mean/std fit on TRAIN rows only, class imbalance via pos_weight.

Usage:
  python3 ga_train_t3.py --input pd_bt3x.root --tag t1
"""
import argparse
import json
import os
import time

import numpy as np

T0 = time.time()
GA = os.path.dirname(os.path.abspath(__file__))
PROTO = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/protoA"
FROZEN_TEST60 = ("/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/"
                 "standalone/prototype/m12_test60_evts.json")
N_BASE, N_EXTRA = 19, 12
N_FEAT = N_BASE + N_EXTRA
UNCLIPPED = 1e30


def log(m):
    print("[%8.1fs] %s" % (time.time() - T0, m), flush=True)


# Conditioning: the four heavy-tailed base slots keep the frozen M7/M16 vocabulary; the
# extra slots add clips only where the quantity is unbounded by construction.
COND = [
    {"feature": "af_ptErrRel", "op": "log10_1p"},
    {"feature": "af_circleCenterDist", "op": "log10_1p"},
    {"feature": "af_log10PtIn", "op": "clip", "lo": -1.0, "hi": 4.0},
    {"feature": "af_log10CircleRadius", "op": "clip", "lo": 1.0, "hi": 5.0},
    {"feature": "af_fitKappaSigned", "op": "clip", "lo": -1.0, "hi": 1.0},
    {"feature": "af_dKappa", "op": "clip", "lo": -1.0, "hi": 1.0},
    {"feature": "ax_log10T3Radius", "op": "clip", "lo": 0.0, "hi": 6.0},
    {"feature": "ax_log10RPhiRms", "op": "clip", "lo": -3.0, "hi": 4.0},
    {"feature": "ax_log10RPhiMax", "op": "clip", "lo": -3.0, "hi": 4.0},
    {"feature": "ax_log10RzRms", "op": "clip", "lo": -3.0, "hi": 4.0},
    {"feature": "ax_log10RzMax", "op": "clip", "lo": -3.0, "hi": 4.0},
]


def apply_cond(X, names):
    out = []
    for c in COND:
        if c["feature"] not in names:
            continue
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        else:
            np.log10(1.0 + np.maximum(X[:, j], 0.0), out=X[:, j])
        out.append(dict(c, index=j))
    return out


def wauc(y, s, w):
    """Weighted ROC AUC via the rank/Mann-Whitney form."""
    o = np.argsort(s, kind="mergesort")
    y, s, w = y[o], s[o], w[o]
    # cumulative weighted negatives strictly below each point (ties get half)
    neg = w * (1 - y)
    cneg = np.concatenate([[0.0], np.cumsum(neg)])[:-1]
    # handle ties: group by score
    uniq, inv, cnt = np.unique(s, return_inverse=True, return_counts=True)
    gneg = np.bincount(inv, weights=neg, minlength=len(uniq))
    gstart = np.concatenate([[0.0], np.cumsum(gneg)])[:-1]
    below = gstart[inv]
    same = gneg[inv]
    num = np.sum(w * y * (below + 0.5 * same))
    den = np.sum(w * y) * np.sum(neg)
    del cneg
    return float(num / den) if den > 0 else float("nan")


def tpr_at_fpr(y, s, w, targets):
    """Weighted TPR at the given weighted FPRs (threshold from the negative tail)."""
    o = np.argsort(-s, kind="mergesort")
    y, s, w = y[o], s[o], w[o]
    cpos = np.cumsum(w * y)
    cneg = np.cumsum(w * (1 - y))
    P, N = cpos[-1], cneg[-1]
    res = {}
    for t in targets:
        k = np.searchsorted(cneg, t * N, side="left")
        k = min(k, len(cpos) - 1)
        res[t] = float(cpos[k] / P) if P > 0 else float("nan")
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", default=os.path.join(GA, "pd_bt3x.root"))
    ap.add_argument("--tag", default="t1")
    ap.add_argument("--hidden", type=int, default=32)
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--patience", type=int, default=8)
    ap.add_argument("--batch-size", type=int, default=32768)
    ap.add_argument("--lr", type=float, default=2e-3)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--select", default="tpr1e-5",
                    choices=["tpr1e-5", "tpr1e-4", "auc"])
    ap.add_argument("--cache", default=None)
    args = ap.parse_args()

    import torch
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)

    cache = args.cache or os.path.join(GA, "cache_%s.npz" % os.path.basename(args.input).replace(".root", ""))
    if os.path.exists(cache):
        log("loading cache %s" % cache)
        z = np.load(cache, allow_pickle=True)
        X, y, w, evt = z["X"], z["y"], z["w"], z["evt"]
        names = list(z["names"])
    else:
        import uproot
        log("reading %s" % args.input)
        f = uproot.open(args.input)
        af = f["feature_spec"].member("fTitle")[3:].split(",")
        ax = f["extra_spec"].member("fTitle")[3:].split(",")
        assert len(af) == N_BASE and len(ax) == N_EXTRA
        names = ["af_" + n for n in af] + ["ax_" + n for n in ax]
        t = f["pairs"]
        cols = ["af_%02d" % i for i in range(N_BASE)] + ["ax_%02d" % i for i in range(N_EXTRA)]
        d = t.arrays(cols + ["label", "wgt", "evt", "ttype"], library="np")
        assert np.all(d["ttype"] == 1), "this trainer is bare-T3 only (-PDT 1 dump)"
        X = np.column_stack([d[c] for c in cols]).astype(np.float32)
        y = d["label"].astype(np.float32)
        w = d["wgt"].astype(np.float32)
        evt = d["evt"].astype(np.int64)
        np.savez(cache, X=X, y=y, w=w, evt=evt, names=np.array(names))
    log("rows %d  features %d  weighted true frac %.6f"
        % (len(y), X.shape[1], float((w * y).sum() / w.sum())))

    cond = apply_cond(X, names)
    log("conditioning: %s" % ", ".join(c["feature"] + ":" + c["op"] for c in cond))

    # ---- EVENT-level split against the frozen test-60 -------------------------------
    keys = np.unique(evt.astype(np.uint64))
    rng = np.random.default_rng(42)   # EXACT train_attach_gen.py procedure
    sh = keys.copy()
    rng.shuffle(sh)
    n_tr = int(round(0.6 * len(sh)))
    n_va = int(round(0.2 * len(sh)))
    tr_k, va_k, te_k = set(sh[:n_tr].tolist()), set(sh[n_tr:n_tr + n_va].tolist()), set(sh[n_tr + n_va:].tolist())
    if os.path.exists(FROZEN_TEST60):
        frozen = set(int(v) for v in json.load(open(FROZEN_TEST60)))
        te_k = set(int(v) for v in te_k)
        if frozen != te_k:
            log("WARNING: test set differs from the frozen test-60 list (%d vs %d, %d shared)"
                % (len(te_k), len(frozen), len(te_k & frozen)))
        else:
            log("test set == FROZEN test-60 (asserted)")
    m_tr = np.isin(evt, list(tr_k))
    m_va = np.isin(evt, list(va_k))
    m_te = np.isin(evt, list(te_k))
    log("split rows train %d val %d test %d" % (m_tr.sum(), m_va.sum(), m_te.sum()))

    mean = X[m_tr].mean(axis=0)
    std = X[m_tr].std(axis=0)
    std[std < 1e-8] = 1.0
    Xs = ((X - mean) / std).astype(np.float32)

    Wtrue = float((w * y)[m_tr].sum())
    Wfake = float((w * (1 - y))[m_tr].sum())
    pos_weight = Wfake / max(Wtrue, 1.0)
    log("pos_weight %.4f (Wfake %.3e / Wtrue %.3e)" % (pos_weight, Wfake, Wtrue))

    dev = "cpu"

    def T(a):
        a = np.ascontiguousarray(a, dtype=np.float32)
        t = torch.frombuffer(bytearray(a.tobytes()), dtype=torch.float32)
        return t.reshape(a.shape)

    Xtr = T(Xs[m_tr]); ytr = T(y[m_tr]); wtr = T(w[m_tr])
    Xva, yva, wva = Xs[m_va], y[m_va], w[m_va]
    Xte, yte, wte = Xs[m_te], y[m_te], w[m_te]

    net = torch.nn.Sequential(
        torch.nn.Linear(N_FEAT, args.hidden), torch.nn.ReLU(),
        torch.nn.Linear(args.hidden, args.hidden), torch.nn.ReLU(),
        torch.nn.Linear(args.hidden, 1)).to(dev)
    opt = torch.optim.Adam(net.parameters(), lr=args.lr)
    lossf = torch.nn.BCEWithLogitsLoss(reduction="none",
                                       pos_weight=torch.tensor(float(pos_weight)))

    n = Xtr.shape[0]
    best = (-1.0, None, -1, None)
    hist = []
    for ep in range(args.epochs):
        net.train()
        perm = torch.randperm(n)
        tot = 0.0
        for b in range(0, n, args.batch_size):
            idx = perm[b:b + args.batch_size]
            xb, yb, wb = Xtr[idx], ytr[idx], wtr[idx]
            opt.zero_grad()
            out = net(xb).squeeze(1)
            l = (lossf(out, yb) * wb).sum() / wb.sum()
            l.backward()
            opt.step()
            tot += float(l) * len(idx)
        net.eval()
        with torch.no_grad():
            sva = np.asarray(net(T(Xva)).squeeze(1).tolist(), dtype=np.float64)
        a = wauc(yva, sva, wva)
        tp = tpr_at_fpr(yva, sva, wva, [1e-5, 1e-4, 1e-3])
        metric = {"auc": a, "tpr1e-5": tp[1e-5], "tpr1e-4": tp[1e-4]}[args.select]
        hist.append(dict(epoch=ep, loss=tot / n, auc=a, tpr1e5=tp[1e-5], tpr1e4=tp[1e-4], tpr1e3=tp[1e-3]))
        log("ep %2d loss %.5f | val AUC %.5f TPR@1e-5 %.4f @1e-4 %.4f @1e-3 %.4f%s"
            % (ep, tot / n, a, tp[1e-5], tp[1e-4], tp[1e-3], "  *" if metric > best[0] else ""))
        if metric > best[0]:
            best = (metric, {k: v.clone() for k, v in net.state_dict().items()}, ep, hist[-1])
        elif ep - best[2] >= args.patience:
            log("early stop (patience %d)" % args.patience)
            break

    net.load_state_dict(best[1])
    net.eval()
    with torch.no_grad():
        ste = np.asarray(net(T(Xte)).squeeze(1).tolist(), dtype=np.float64)
    a_te = wauc(yte, ste, wte)
    tp_te = tpr_at_fpr(yte, ste, wte, [1e-5, 1e-4, 1e-3])
    log("TEST (frozen 60 evts): AUC %.5f TPR@1e-5 %.4f @1e-4 %.4f @1e-3 %.4f"
        % (a_te, tp_te[1e-5], tp_te[1e-4], tp_te[1e-3]))
    log("selected epoch %d on %s = %.5f" % (best[2], args.select, best[0]))

    # ---- export the C++ header ------------------------------------------------------
    sd = {k: np.asarray(v.tolist(), dtype=np.float32) for k, v in net.state_dict().items()}
    clipLo = np.full(N_FEAT, -UNCLIPPED); clipHi = np.full(N_FEAT, UNCLIPPED)
    logp1 = np.zeros(N_FEAT, dtype=bool)
    for c in cond:
        if c["op"] == "clip":
            clipLo[c["index"]] = c["lo"]; clipHi[c["index"]] = c["hi"]
        else:
            logp1[c["index"]] = True

    def f32(v):
        s = "%.9g" % float(np.float32(v))
        if "." not in s and "e" not in s and "n" not in s and "i" not in s:
            s += ".0"
        return s + "f"

    def arr1(vals, per=6, ind="    "):
        L = [ind + ", ".join(f32(v) for v in vals[i:i + per]) + "," for i in range(0, len(vals), per)]
        return "{\n" + "\n".join(L)[:-1] + "\n}"

    def arr2(m, ind="    "):
        return "{\n" + ",\n".join(ind + arr1(r, 6, ind + "    ") for r in m) + "\n}"

    H = args.hidden
    out = os.path.join(PROTO, "attach_t3_mlp_weights.h")
    with open(out, "w") as fh:
        fh.write("// GENERATED by gen_a_ref/ga_train_t3.py -- DO NOT EDIT BY HAND.\n")
        fh.write("// GEN-A dedicated BARE-T3 (pT3-class) attach head.\n")
        fh.write("//   dump: %s   tag: %s\n" % (args.input, args.tag))
        fh.write("//   selection metric: %s = %.6f at epoch %d\n" % (args.select, best[0], best[2]))
        fh.write("//   TEST(frozen 60): AUC %.5f TPR@1e-5 %.4f @1e-4 %.4f\n"
                 % (a_te, tp_te[1e-5], tp_te[1e-4]))
        fh.write("//   arch [%d, %d, %d, 1]\n" % (N_FEAT, H, H))
        fh.write("// Inputs: af_00..af_18 (frozen PixelAttach.h layout) ++ ax_00..ax_11\n")
        for i, nm in enumerate(names):
            fh.write("//  [%2d] %s\n" % (i, nm))
        fh.write("#ifndef PROTOTYPE_ATTACH_T3_MLP_WEIGHTS_H\n#define PROTOTYPE_ATTACH_T3_MLP_WEIGHTS_H\n\n")
        fh.write("namespace attacht3mlp {\n\n")
        fh.write("constexpr int kInput = %d;\nconstexpr int kHidden = %d;\n\n" % (N_FEAT, H))
        fh.write("constexpr float kFeatMean[kInput] = %s;\n\n" % arr1(mean))
        fh.write("constexpr float kFeatStd[kInput] = %s;\n\n" % arr1(std))
        fh.write("constexpr float kClipLo[kInput] = %s;\n\n" % arr1(clipLo))
        fh.write("constexpr float kClipHi[kInput] = %s;\n\n" % arr1(clipHi))
        fh.write("constexpr bool kLog10p1[kInput] = {%s};\n\n"
                 % ", ".join("true" if b else "false" for b in logp1))
        fh.write("constexpr float wgt_l1[kInput][kHidden] = %s;\n\n" % arr2(sd["0.weight"].T))
        fh.write("constexpr float bias_l1[kHidden] = %s;\n\n" % arr1(sd["0.bias"]))
        fh.write("constexpr float wgt_l2[kHidden][kHidden] = %s;\n\n" % arr2(sd["2.weight"].T))
        fh.write("constexpr float bias_l2[kHidden] = %s;\n\n" % arr1(sd["2.bias"]))
        fh.write("constexpr float wgt_out[kHidden] = %s;\n\n" % arr1(sd["4.weight"][0]))
        fh.write("constexpr float bias_out = %s;\n\n" % f32(sd["4.bias"][0]))
        fh.write("}  // namespace attacht3mlp\n\n#endif\n")
    log("wrote %s" % out)
    json.dump(dict(tag=args.tag, hist=hist, best_epoch=best[2], select=args.select,
                   select_value=best[0], test_auc=a_te,
                   test_tpr={str(k): v for k, v in tp_te.items()}),
              open(os.path.join(GA, "train_%s.json" % args.tag), "w"), indent=1)

    # score distribution of the selected head over the test rows, for threshold planning
    np.save(os.path.join(GA, "score_test_%s.npy" % args.tag),
            np.column_stack([ste, yte, wte]))


if __name__ == "__main__":
    main()
