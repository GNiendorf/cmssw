#!/usr/bin/env python3
"""ANGLE 9 part 2: (i) CONTROL - does the displaced gain come from the MODEL CLASS
(GBDT) or from the OBJECTIVE (3-class / displaced-only head)? Trains the SAME
16->24->24 MLP as the production chain gate but with a 3-class head, on the same
rows/weights/conditioning. (ii) OPERATING-POINT table - AUC is abstract; the M9
corner cares about fake rejection at fixed displaced-true efficiency inside the
exempt 5+ branch. No C++ changes."""
import json
import sys
import time

import numpy as np
import torch

sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout/a9_gbdt")
from gbdt_probe import load_cache, add_extras, strat_auc, MYDIR, PROTO  # noqa: E402

T0 = time.time()


def log(m):
    print(f"[{time.time() - T0:7.1f}s] {m}", flush=True)


meta, X16, names, tr, va, te = load_cache(f"{MYDIR}/chains_cache.npz")
Xex, names_ex = add_extras(X16, names)
y = (meta["label"] == 1).astype(np.int32)
vxy = meta["simVxy"]
nl = meta["nLayers"]
cls = np.zeros(len(y), np.int32)
cls[(y == 1) & (vxy < 1)] = 1
cls[(y == 1) & (vxy >= 1)] = 2

# ---------------- v3 conditioning + standardization (for both MLPs) ----------
norm = json.load(open(f"{PROTO}/chain_norm_v3.json"))
Xc = X16.copy()
for c in norm["conditioning"]:
    j = names.index(c["feature"])
    if c["op"] == "clip":
        np.clip(Xc[:, j], c["lo"], c["hi"], out=Xc[:, j])
    else:
        Xc[:, j] = np.log10(1.0 + Xc[:, j])
mu = np.array(norm["mean"], np.float32)
sd = np.array(norm["std"], np.float32)
Xs = (Xc - mu) / sd

dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
log(f"device {dev}")


def softmax_np(z):
    z = z - z.max(axis=1, keepdims=True)
    e = np.exp(z)
    return e / e.sum(axis=1, keepdims=True)


def net_scores(m, Xn, out_dim):
    Xt = torch.tensor(np.ascontiguousarray(Xn))
    out = np.empty((len(Xt), out_dim), np.float32)
    m.eval()
    with torch.no_grad():
        for i in range(0, len(Xt), 1 << 20):
            o = m(Xt[i:i + (1 << 20)].to(dev)).float().cpu()
            out[i:i + (1 << 20)] = np.array(o.tolist(), np.float32).reshape(-1, out_dim)
    return out


# ---------------- production MLP v3 (binary) --------------------------------
ck = torch.load(f"{PROTO}/chain_mlp_v3.pt", map_location="cpu", weights_only=False)
mlp_v3 = torch.nn.Sequential(torch.nn.Linear(16, 24), torch.nn.ReLU(),
                             torch.nn.Linear(24, 24), torch.nn.ReLU(),
                             torch.nn.Linear(24, 1))
mlp_v3.load_state_dict(ck["state_dict"])
mlp_v3.to(dev)
s_mlp_v3 = net_scores(mlp_v3, Xs[te], 1)[:, 0]
log("MLP v3 scored")

# ---------------- CONTROL: same-size MLP, 3-class head ----------------------
torch.manual_seed(42)
np.random.seed(42)
mlp3 = torch.nn.Sequential(torch.nn.Linear(16, 24), torch.nn.ReLU(),
                           torch.nn.Linear(24, 24), torch.nn.ReLU(),
                           torch.nn.Linear(24, 3)).to(dev)
opt = torch.optim.Adam(mlp3.parameters(), lr=1e-3)
c_tr = cls[tr]
w3 = np.ones(int(tr.sum()), np.float32)
n0 = (c_tr == 0).sum()
for k in (1, 2):
    w3[c_tr == k] = n0 / max((c_tr == k).sum(), 1)
Xtr = torch.tensor(np.ascontiguousarray(Xs[tr])).to(dev)
ytr = torch.tensor(np.ascontiguousarray(c_tr.astype(np.int64))).to(dev)
wtr = torch.tensor(w3).to(dev)
crit = torch.nn.CrossEntropyLoss(reduction="none")
gen = torch.Generator(device="cpu").manual_seed(42)
lab_va, vxy_va = meta["label"][va] == 1, meta["simVxy"][va]
best, best_state, bad = -1.0, None, 0
for ep in range(1, 61):
    mlp3.train()
    perm = torch.randperm(len(Xtr), generator=gen)
    tot = 0.0
    for i in range(0, len(Xtr), 16384):
        idx = perm[i:i + 16384].to(dev)
        opt.zero_grad()
        loss = (crit(mlp3(Xtr[idx]), ytr[idx]) * wtr[idx]).mean()
        loss.backward()
        opt.step()
        tot += float(loss.detach()) * len(idx)
    P = softmax_np(net_scores(mlp3, Xs[va], 3))
    s_true = 1.0 - P[:, 0]
    s_disp = P[:, 2] / np.maximum(P[:, 2] + P[:, 0], 1e-9)
    ap = strat_auc(s_true[lab_va & (vxy_va < 1)], s_true[~lab_va])
    ad = strat_auc(s_disp[lab_va & (vxy_va >= 1)], s_disp[~lab_va])
    sel = min(ap, ad)
    if sel > best:
        best, best_state, bad = sel, {k: v.cpu().clone() for k, v in mlp3.state_dict().items()}, 0
    else:
        bad += 1
    if ep % 5 == 0 or ep == 1:
        log(f"mlp3 epoch {ep:3d} loss={tot / len(Xtr):.4f} val prompt(true axis)={ap:.5f} "
            f"val disp(disp axis)={ad:.5f} sel={sel:.5f}")
    if bad >= 10:
        log(f"early stop ep {ep} best sel {best:.5f}")
        break
mlp3.load_state_dict(best_state)
mlp3.to(dev)
P3 = softmax_np(net_scores(mlp3, Xs[te], 3))
s_mlp3_true = (1.0 - P3[:, 0]).astype(np.float32)
s_mlp3_disp = (P3[:, 2] / np.maximum(P3[:, 2] + P3[:, 0], 1e-9)).astype(np.float32)
torch.save({"state_dict": best_state, "arch": [16, 24, 24, 3], "best_sel": float(best),
            "feature_names": names, "conditioning": norm["conditioning"]},
           f"{MYDIR}/chain_mlp3_a9.pt")
log("3-class MLP done")

# ---------------- GBDT 3-class (cf16 and cf+extras) -------------------------
from sklearn.ensemble import HistGradientBoostingClassifier  # noqa: E402

gb_scores = {}
for tag, XX in (("gbdt3_cf16", X16), ("gbdt3_ext", Xex)):
    m = HistGradientBoostingClassifier(max_iter=300, learning_rate=0.1, max_leaf_nodes=31,
                                       l2_regularization=1.0, early_stopping=False,
                                       random_state=42)
    m.fit(XX[tr], c_tr, sample_weight=w3)
    P = m.predict_proba(XX[te])
    gb_scores[tag + "_true"] = (1.0 - P[:, 0]).astype(np.float32)
    gb_scores[tag + "_disp"] = (P[:, 2] / np.maximum(P[:, 2] + P[:, 0], 1e-9)).astype(np.float32)
    log(f"{tag} done")

# ---------------- operating-point table -------------------------------------
lab = meta["label"][te] == 1
vx = meta["simVxy"][te]
nlt = nl[te]
scorers = {"MLP_v3_binary(production)": s_mlp_v3,
           "MLP_3class_true_axis": s_mlp3_true,
           "MLP_3class_disp_axis": s_mlp3_disp,
           "GBDT_3class_cf16_true": gb_scores["gbdt3_cf16_true"],
           "GBDT_3class_cf16_disp": gb_scores["gbdt3_cf16_disp"],
           "GBDT_3class_ext_true": gb_scores["gbdt3_ext_true"],
           "GBDT_3class_ext_disp": gb_scores["gbdt3_ext_disp"]}

res = {"branches": {}}
for bname, bm in (("nL>=5 (M9 exempt branch: ~85% of residual fake)", nlt >= 5),
                  ("nL<=4 (T4-class: displaced upside branch)", nlt <= 4),
                  ("all", np.ones(len(lab), bool))):
    pos = lab & (vx >= 1) & bm      # displaced true
    posA = lab & bm                 # ALL true
    neg = (~lab) & bm               # fake
    rows = {}
    print(f"\n=== branch {bname}: n_dispTrue={int(pos.sum())} n_allTrue={int(posA.sum())} "
          f"n_fake={int(neg.sum())} ===")
    hdr = (f"{'scorer':>28} {'AUC_disp':>9} {'AUC_all':>9} "
           + " ".join(f"{'fkill@dE' + str(int(e * 100)):>11}" for e in (0.99, 0.95, 0.90))
           + " " + " ".join(f"{'fkill@aE' + str(int(e * 100)):>11}" for e in (0.99, 0.95)))
    print(hdr)
    for sname, s in scorers.items():
        a_d = strat_auc(s[pos], s[neg])
        a_a = strat_auc(s[posA], s[neg])
        cells, vals = [], {}
        for e in (0.99, 0.95, 0.90):
            thr = np.quantile(s[pos], 1.0 - e)
            fk = float((s[neg] < thr).mean())
            vals[f"fkill@dispEff{int(e * 100)}"] = fk
            cells.append(f"{fk:>11.4f}")
        for e in (0.99, 0.95):
            thr = np.quantile(s[posA], 1.0 - e)
            fk = float((s[neg] < thr).mean())
            vals[f"fkill@allEff{int(e * 100)}"] = fk
            cells.append(f"{fk:>11.4f}")
        print(f"{sname:>28} {a_d:>9.5f} {a_a:>9.5f} " + " ".join(cells))
        rows[sname] = dict(auc_disp=a_d, auc_all=a_a, **vals)
    res["branches"][bname] = {"n_dispTrue": int(pos.sum()), "n_allTrue": int(posA.sum()),
                              "n_fake": int(neg.sum()), "scorers": rows}

json.dump(res, open(f"{MYDIR}/wp_results.json", "w"), indent=1)
log(f"wrote {MYDIR}/wp_results.json")
