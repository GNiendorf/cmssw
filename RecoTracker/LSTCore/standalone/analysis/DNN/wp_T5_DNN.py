#!/usr/bin/env python3
"""Working points and header export for a model trained by train_T5_DNN.py.

WPs are one per-bin table on the score 1 - P(fake): pass = 1 - P(fake) > wp[pt][eta], pt bin = (innerRadius *
k2Rinv1GeVf * 2 > 5 GeV), eta bin = 0.25-wide on |eta| of the binning anchor (last bin open). At equal PU200 fake
retention this beats an OR of per-class prompt/displaced tables for prompt and for displaced up to vxy ~ 5 cm, at the
creation and the promotion point (displaced_ref/t5dnn/wp/scheme_full.txt). Each table keeps a fixed fraction
(--retention) of FULLY matched (pMatched > 0.95) T5s per bin, derived on the held-out (val + test) PU200 events of the
training split. Creation tables are binned on the first anchor
eta (as the creation cut is applied); promotion tables on the layer-2 anchor eta (as the promotion is applied).
Offline check on the held-out PU200 and jet events: kept fraction of fakes / prompt / displaced bands for the new tables
vs the current kWp98 (creation) and kWp93 (promotion) on the same T5s. Deployment decides, this only orients.
"""
import argparse
import importlib.util
import json
import os
import re

import numpy as np

K2RINV1GEV2 = 2.99792458e-3 * 3.8  # k2Rinv1GeVf * 2
ETA_SIZE, NETA = 0.25, 10
VXY_BANDS = [(1.0, 5.0), (5.0, 25.0), (25.0, 1e9)]

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("tr", os.path.join(HERE, "train_T5_DNN.py"))
tr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tr)


def bins(pt, eta):
    e = np.minimum((np.abs(eta) / ETA_SIZE).astype(int), NETA - 1)
    return (pt > 5.0).astype(int), e


def table(p, sel, ptb, etab, ret):
    """Per-bin threshold keeping fraction ret of the selected rows; counts returned for the record."""
    wp, n = np.zeros((2, NETA)), np.zeros((2, NETA), int)
    for i in range(2):
        for j in range(NETA):
            m = sel & (ptb == i) & (etab == j)
            n[i, j] = m.sum()
            wp[i, j] = np.quantile(p[m], 1.0 - ret) if n[i, j] else 0.0
    return wp, n


def old_tables(common_h):
    txt = open(common_h).read()
    blk = txt[txt.index("namespace t5dnn"):]
    out = {}
    for nm in ("kWp98", "kWp93"):
        m = re.search(nm + r"\[kPtBins\]\[kEtaBins\]\s*=\s*\{(.*?)\};", blk, re.S)
        out[nm] = np.array([float(v) for v in re.findall(r"-?[0-9.]+(?=f)", m.group(1))]).reshape(2, NETA)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True)
    ap.add_argument("--retention", type=float, nargs="+",
                    default=[0.97, 0.98, 0.99, 0.992, 0.995, 0.998])
    ap.add_argument("--promo-retention", type=float, nargs="+", default=[0.95, 0.96, 0.97, 0.973, 0.98, 0.985, 0.99])
    ap.add_argument("--export-promo", type=float, default=0.973)
    ap.add_argument("--common-h", default=os.path.join(HERE, "../../../interface/alpaka/Common.h"))
    ap.add_argument("--export", help="write weights header + WP snippet with this retention (e.g. 0.98)")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    import torch
    ck = torch.load(a.model, map_location="cpu", weights_only=False)
    targs, names = ck["args"], ck["feature_names"]
    mu, sd = np.array(ck["mean"]), np.array(ck["std"])
    labs = dict(x.split("=", 1) for x in targs["lab"])
    rng = np.random.default_rng(targs["seed"])
    model = torch.nn.Sequential(torch.nn.Linear(len(names), ck["arch"][1]), torch.nn.ReLU(),
                                torch.nn.Linear(ck["arch"][1], ck["arch"][2]), torch.nn.ReLU(),
                                torch.nn.Linear(ck["arch"][2], 3))
    model.load_state_dict(ck["state_dict"])
    model.eval()
    old = old_tables(a.common_h)
    res = dict(model=a.model, retention=a.retention, promo_retention=a.promo_retention, samples={})
    tabs = {}
    for nm, pat in labs.items():  # same order and rng draws as the training split
        H, f, evt, _ = tr.load_sample(pat)
        ev = np.unique(evt)
        perm = rng.permutation(ev)
        if nm not in (targs["ref"], "jet"):
            continue
        ntr = int(0.6 * len(ev))
        held = np.isin(evt, perm[ntr:])
        X = tr.build_features(H, f)
        Xm = np.stack([X[n] for n in names], axis=1)
        good = held & np.isfinite(Xm).all(axis=1)
        Xs = ((Xm[good] - mu) / sd).astype(np.float32)
        with torch.no_grad():
            p = torch.softmax(model(torch.tensor(np.ascontiguousarray(Xs))), dim=1).tolist()
        p = np.array(p)
        pt = f["t5_innerRadius"][good] * K2RINV1GEV2
        eta1 = H[(0, "eta")][good]
        l2 = np.where(np.isin(H[(0, "layer")][good], [1, 7]),
                      H[(1, "eta")][good], H[(0, "eta")][good])
        pmat, vxy, fake = f["t5_pMatched"][good], f["t5_sim_vxy"][good], f["t5_isFake"][good] == 1
        split = targs["vxy_split"]
        full = pmat > 0.95
        ptb, eb1 = bins(pt, eta1)
        _, eb2 = bins(pt, l2)
        olds = f["t5_dnnScore"][good]
        R = {}
        if nm == targs["ref"]:
            for r in a.retention:
                w, n = table(1.0 - p[:, 0], full, ptb, eb1, r)
                tabs[r] = dict(wp=w, n=n)
            for r in a.promo_retention:
                w, n = table(1.0 - p[:, 0], full, ptb, eb2, r)
                tabs[f"promo{r}"] = dict(wp=w, n=n)
        true = ~fake
        classes = {"fake": fake, "prompt": true & (vxy < split)}
        classes.update({f"disp{lo:g}-{hi:g}": true & (vxy >= lo) & (vxy < hi) for lo, hi in VXY_BANDS})
        passes = {"old_kWp98": olds > old["kWp98"][ptb, eb1], "old_kWp93promo": olds >= old["kWp93"][ptb, eb2]}
        for r in a.retention:
            passes[f"new_{r}"] = 1.0 - p[:, 0] > tabs[r]["wp"][ptb, eb1]
        for r in a.promo_retention:
            passes[f"new_promo_{r}"] = 1.0 - p[:, 0] > tabs[f"promo{r}"]["wp"][ptb, eb2]
        for k, ps in passes.items():
            R[k] = {c: [int((ps & m).sum()), int(m.sum())] for c, m in classes.items()}
        res["samples"][nm] = R
        print(f"== {nm} held-out T5s {good.sum()}: kept fraction (kept/total)")
        print(f"{'':22s}" + "".join(f"{c:>16s}" for c in classes))
        for k, d in R.items():
            print(f"{k:22s}" + "".join(f"{d[c][0] / max(d[c][1], 1):16.4f}" for c in classes))
    res["tables"] = {str(k): {kk: np.asarray(vv).tolist() for kk, vv in v.items()} for k, v in tabs.items()}
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    json.dump(res, open(a.out, "w"), indent=1)
    print("wrote", a.out)

    if a.export:
        r = float(a.export)
        W1 = ck["state_dict"]["0.weight"].double().tolist()
        W1, b1 = np.array(W1), np.array(ck["state_dict"]["0.bias"].double().tolist())
        # fold standardisation: W1 (x - mu)/sd + b1 = (W1/sd) x + (b1 - W1 mu/sd)
        W1f, b1f = W1 / sd, b1 - (W1 * (mu / sd)).sum(axis=1)
        L = [(W1f, b1f)] + [(np.array(ck["state_dict"][f"{i}.weight"].double().tolist()),
                             np.array(ck["state_dict"][f"{i}.bias"].double().tolist())) for i in (2, 4)]

        def carr(v):
            return ", ".join(f"{x:.8g}f" for x in v)
        nin, nh = len(names), ck["arch"][1]
        out = ["#ifndef RecoTracker_LSTCore_src_alpaka_T5NeuralNetworkWeights_h",
               "#define RecoTracker_LSTCore_src_alpaka_T5NeuralNetworkWeights_h", "",
               "#include <alpaka/alpaka.hpp>", "",
               "#include \"FWCore/Utilities/interface/HostDeviceConstant.h\"", "",
               "namespace ALPAKA_ACCELERATOR_NAMESPACE::lst::dnn::t5dnn {"]
        for nmL, (W, b), (i, o) in zip(("layer1", "layer2", "output_layer"), L, ((nin, nh), (nh, nh), (nh, 3))):
            out.append(f"  HOST_DEVICE_CONSTANT float bias_{nmL}[{o}] = {{{carr(b)}}};")
            out.append(f"  HOST_DEVICE_CONSTANT float wgtT_{nmL}[{i}][{o}] = {{")
            out += [f"      {{{carr(row)}}}," for row in W.T]
            out.append("  };")
        out += ["}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst::dnn::t5dnn", "", "#endif", ""]
        hdr = os.path.splitext(a.out)[0] + "_T5NeuralNetworkWeights.h"
        open(hdr, "w").write("\n".join(out))
        snip = []
        for key, lab in ((r, "kWp"), (f"promo{a.export_promo}", "kWpPromo")):
            rows = ",\n          ".join("{" + ", ".join(f"{x:.4f}f" for x in tabs[key]["wp"][i]) + "}" for i in range(2))
            snip.append(f"      HOST_DEVICE_CONSTANT float {lab}[kPtBins][kEtaBins] = {{\n          {rows}}};")
        open(os.path.splitext(a.out)[0] + "_wp_snippet.h", "w").write("\n".join(snip) + "\n")
        print("wrote", hdr, "and WP snippet; inputs in order:", names)


if __name__ == "__main__":
    main()
