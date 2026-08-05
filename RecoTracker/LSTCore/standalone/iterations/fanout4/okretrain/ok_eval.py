#!/usr/bin/env python3
"""okretrain: survivor-restricted ranking AUC for any 2-class chain head.

Scores the chain dump with a (model, norm) pair, restricts to the FROZEN TEST events
(the same event_split train_chain.py uses: seed 42, 0.6/0.2 by evt key) and reports
AUC on the K9 claim-stage SURVIVOR rows -- the deployment population of the -B
order key -- plus the all-chain AUC for the record.

  python3 ok_eval.py --model chain_mlp_a2.pt --norm chain_norm_a2.json
"""
import argparse
import json
import os
import sys

import numpy as np

D = os.path.dirname(os.path.abspath(__file__))

N_CHAIN_FEAT = 25
META = ["evt", "label", "simVxy", "nLayers", "dcaXY", "matchFrac"]


def load(dump):
    import uproot
    f = uproot.open(dump)
    spec = f["feature_spec"].member("fTitle")
    cf_names = spec[3:].split(",")
    names = [f"cf_{n}" for n in cf_names]
    tree = f["chains"]
    arr = tree.arrays(META + [f"cf_{i:02d}" for i in range(N_CHAIN_FEAT)], library="np")
    meta = {k: arr[k] for k in META}
    X = np.empty((len(meta["label"]), N_CHAIN_FEAT), dtype=np.float32)
    for j in range(N_CHAIN_FEAT):
        X[:, j] = arr[f"cf_{j:02d}"]
    return meta, X, names


def score(model_pt, norm_json, X, names):
    import torch
    norm = json.load(open(norm_json))
    nm = norm["feature_names"]
    Xc = X.copy()
    for c in norm.get("conditioning") or []:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(Xc[:, j], c["lo"], c["hi"], out=Xc[:, j])
        elif c["op"] == "log10_1p":
            Xc[:, j] = np.log10(1.0 + Xc[:, j])
    cols = [names.index(n) for n in nm]
    Xc = np.ascontiguousarray(Xc[:, cols])
    mu = np.asarray(norm["mean"], dtype=np.float32)
    sd = np.asarray(norm["std"], dtype=np.float32)
    Xc = (Xc - mu) / sd
    blob = torch.load(model_pt, map_location="cpu", weights_only=False)
    a = blob["arch"]
    model = torch.nn.Sequential(torch.nn.Linear(a[0], a[1]), torch.nn.ReLU(),
                                torch.nn.Linear(a[1], a[2]), torch.nn.ReLU(),
                                torch.nn.Linear(a[2], a[3]))
    model.load_state_dict(blob["state_dict"])
    model.eval()
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model.to(dev)
    n = len(Xc)
    out = np.empty((n, a[3]), dtype=np.float32)
    Xt = torch.frombuffer(memoryview(np.ascontiguousarray(Xc).reshape(-1)),
                          dtype=torch.float32).reshape(n, a[0])
    ot = torch.frombuffer(memoryview(out.reshape(-1)), dtype=torch.float32).reshape(n, a[3])
    with torch.no_grad():
        for i in range(0, n, 1 << 19):
            ot[i:i + (1 << 19)] = model(Xt[i:i + (1 << 19)].to(dev)).cpu()
    return out[:, 0]


def test_mask(meta, seed=42, train_frac=0.6, val_frac=0.2):
    rng = np.random.default_rng(seed)
    key = meta["evt"].astype(np.uint64)
    uniq = np.unique(key)
    rng.shuffle(uniq)
    n = len(uniq)
    n_tr = int(round(train_frac * n))
    n_va = int(round(val_frac * n))
    te_keys = uniq[n_tr + n_va:]
    return np.isin(key, te_keys), te_keys


def auc(y_true_mask, y_fake_mask, s):
    from sklearn.metrics import roc_auc_score
    if y_true_mask.sum() == 0 or y_fake_mask.sum() == 0:
        return None
    yy = np.concatenate([np.ones(int(y_true_mask.sum())), np.zeros(int(y_fake_mask.sum()))])
    ss = np.concatenate([s[y_true_mask], s[y_fake_mask]])
    return float(roc_auc_score(yy, ss))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", default=f"{D}/chains_m18.root")
    ap.add_argument("--surv", default=f"{D}/ok_surv_m18.npz")
    ap.add_argument("--model", action="append", required=True,
                    help="tag=model.pt:norm.json (repeatable)")
    ap.add_argument("--out", default=f"{D}/ok_survauc.json")
    a = ap.parse_args()

    meta, X, names = load(a.dump)
    surv = np.load(a.surv)["surv"].astype(bool)
    te, te_keys = test_mask(meta)
    print(f"frozen test events: {len(te_keys)}  rows {int(te.sum())}  "
          f"survivor rows in test {int((te & surv).sum())}")

    lab = meta["label"] == 1
    vxy = meta["simVxy"]
    nl = meta["nLayers"]
    out = {}
    for spec in a.model:
        tag, paths = spec.split("=", 1)
        mpt, mnorm = paths.split(":", 1)
        s = score(mpt, mnorm, X, names)
        r = {}
        for name, base in (("all", te), ("surv", te & surv)):
            r[f"{name}"] = auc(lab & base, ~lab & base, s)
            for ln, lm in (("L4", nl == 4), ("L5", nl == 5), ("L6+", nl >= 6)):
                r[f"{name}|{ln}"] = auc(lab & base & lm, ~lab & base & lm, s)
            r[f"{name}|prompt"] = auc(lab & base & (vxy < 1), ~lab & base, s)
            r[f"{name}|disp1"] = auc(lab & base & (vxy >= 1), ~lab & base, s)
            r[f"{name}|disp5"] = auc(lab & base & (vxy >= 5), ~lab & base, s)
        neg = te & surv & (s < 0)
        r["surv|neg-tail"] = auc(lab & neg, ~lab & neg, s)
        r["n_surv_neg"] = int(neg.sum())
        r["frac_surv_neg"] = float(neg.sum()) / max(int((te & surv).sum()), 1)
        q = np.quantile(s[te & surv], [0.05, 0.25, 0.5, 0.75, 0.95])
        r["surv_logit_q"] = [float(v) for v in q]
        out[tag] = r
        print(f"\n--- {tag} ({os.path.basename(mpt)}) ---")
        for k in ("all", "surv", "surv|L4", "surv|L5", "surv|L6+", "surv|prompt",
                  "surv|disp1", "surv|disp5", "surv|neg-tail"):
            v = r.get(k)
            print(f"  {k:>16}: {v:.5f}" if v is not None else f"  {k:>16}: n/a")
        print(f"  {'surv neg rows':>16}: {r['n_surv_neg']} ({r['frac_surv_neg']:.4f} of surv)")
        print(f"  {'surv logit q':>16}: " + " ".join(f"{v:+.2f}" for v in r["surv_logit_q"]))
    json.dump(out, open(a.out, "w"), indent=1)
    print(f"\nwrote {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
