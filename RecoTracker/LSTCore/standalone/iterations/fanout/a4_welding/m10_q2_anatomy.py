#!/usr/bin/env python3
"""M10 Mission A Q2/Q4: anatomy of label==0 chains that pass the m8_h4b acceptance shape.

READ-ONLY. Uses chains_300evt_simidx.root (welded-chain dump, pre-arbitration) plus the
v3 chain-gate model for the -G 2 split score.

CAVEAT (measured, reported in the findings): the chain dump was written at 09:52 with the
v2_noweight EDGE weights active, while ab_m8_h4b ran at 13:27 with v3 edge weights, so
cf_02/03/04 (the edge-logit aggregates) here are v2nw-scale, not the anchor's v3 scale.
"""
import json
import numpy as np
import uproot
import torch
from scipy.stats import ks_2samp

PROTO = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/"

norm = json.load(open(PROTO + "chain_norm_v3.json"))
names = norm["feature_names"]
mean = np.array(norm["mean"], dtype=np.float64)
std = np.array(norm["std"], dtype=np.float64)
cond = norm["conditioning"]

t = uproot.open(PROTO + "chains_300evt_simidx.root:chains")
cols = ["evt", "label", "simIdx", "simVxy", "simPt", "nLayers"] + ["cf_%02d" % i for i in range(16)]
d = t.arrays(cols, library="np")
X = np.stack([d["cf_%02d" % i] for i in range(16)], axis=1).astype(np.float64)
lab = d["label"].astype(int)
nL = d["nLayers"].astype(int)
vxy = d["simVxy"]
evt = d["evt"]
print("chains: %d  label1 %.4f  events %d" % (len(lab), (lab == 1).mean(), len(np.unique(evt))))

# ---- conditioning + standardization (mirrors train_chain.py / ChainInference.cc) ----
Xc = X.copy()
for c in cond:
    i = names.index(c["feature"])
    if c["op"] == "log10_1p":
        Xc[:, i] = np.log10(1.0 + Xc[:, i])
    elif c["op"] == "clip":
        Xc[:, i] = np.clip(Xc[:, i], c["lo"], c["hi"])
Z = (Xc - mean) / np.where(std > 0, std, 1.0)

ck = torch.load(PROTO + "chain_mlp_v3.pt", map_location="cpu", weights_only=False)
sd = ck["model"] if "model" in ck else ck["state_dict"]
keys = [k for k in sd if k.endswith("weight")]
ws = [(np.array(sd[k].tolist(), dtype=np.float32),
       np.array(sd[k.replace("weight", "bias")].tolist(), dtype=np.float32)) for k in keys]
h = Z.astype(np.float32)
for i, (w, b) in enumerate(ws):
    h = h @ w.T + b
    if i < len(ws) - 1:
        h = np.maximum(h, 0.0)
gate = h[:, 0].astype(np.float64)
print("gate logit computed; layer shapes:", [tuple(w.shape) for w, _ in ws])

legacy = X[:, 2] + 0.5 * nL          # sumEdgeLogit + lambdaLen * nLayers

# ---- h4b acceptance shape: -G 2, T4=2 (gate scale), T5=0, T6=0 (legacy scale) ----
isT4 = nL <= 4
score = np.where(isT4, gate, legacy)
thr = np.where(isT4, 2.0, 0.0)
passT = score >= thr
print("\n=== ACCEPTANCE SHAPE (-G 2 T4=2 T5=0 T6=0 L=0.5); pre pixdrop/claim ===")
print("theta-pass: %d / %d (%.3f)  -> %.1f/evt" % (passT.sum(), len(passT),
                                                   passT.mean(), passT.sum()/300))
print("  label==0 among theta-pass  : %d (%.4f)" % ((passT & (lab == 0)).sum(),
                                                    (lab[passT] == 0).mean()))
print("  label==0 among ALL welded  : %.4f" % (lab == 0).mean())
for cl, m in [("nLayers<=4", isT4), ("nLayers==5", nL == 5), ("nLayers>=6", nL >= 6)]:
    p = passT & m
    print("  %-11s: welded %7d (%.3f pass) accepted-shape %7d  label0-frac %.4f (welded %.4f)"
          % (cl, m.sum(), passT[m].mean(), p.sum(), (lab[p] == 0).mean(), (lab[m] == 0).mean()))

# ---- frozen test-60 events (train_chain.py combined split, seed 42) ----
rng = np.random.default_rng(42)
uniq0 = np.unique(evt)
rng.shuffle(uniq0)
n0 = len(uniq0); n_tr0 = int(round(0.6 * n0)); n_va0 = int(round(0.2 * n0))
te_keys = set(uniq0[n_tr0 + n_va0:].tolist())
isTest = np.array([e in te_keys for e in evt])
print("\nfrozen test events: %d -> %d chains" % (len(te_keys), isTest.sum()))

def auc(a, b):
    """P(score(true-class a) > score(b)) via rank statistic."""
    if len(a) == 0 or len(b) == 0:
        return float("nan")
    x = np.concatenate([a, b])
    r = np.argsort(np.argsort(x, kind="stable"), kind="stable").astype(np.float64) + 1
    # tie-corrected ranks
    order = np.argsort(x, kind="stable")
    xs = x[order]
    i = 0
    rr = r.copy()
    while i < len(xs):
        j = i
        while j + 1 < len(xs) and xs[j + 1] == xs[i]:
            j += 1
        if j > i:
            rr[order[i:j + 1]] = 0.5 * (i + j) + 1
        i = j + 1
    ra = rr[:len(a)].sum()
    return (ra - len(a) * (len(a) + 1) / 2.0) / (len(a) * len(b))

FEATS = names + ["GATE_LOGIT", "LEGACY_SCORE"]
ALL = np.concatenate([X, gate[:, None], legacy[:, None]], axis=1)

def report(mask, tag, sub=200000):
    t1 = mask & (lab == 1); t0 = mask & (lab == 0)
    print("\n--- %s : accepted-shape TRUE(label1) %d  FAKE(label0) %d  (fakefrac %.4f) ---"
          % (tag, t1.sum(), t0.sum(), t0.sum()/max(mask.sum(),1)))
    if t0.sum() < 50 or t1.sum() < 50:
        print("   (too few)"); return
    rows = []
    for i, nm in enumerate(FEATS):
        a = ALL[t1, i]; b = ALL[t0, i]
        if len(a) > sub:
            a = np.random.default_rng(1).choice(a, sub, replace=False)
        if len(b) > sub:
            b = np.random.default_rng(2).choice(b, sub, replace=False)
        A = auc(a, b)
        ks = ks_2samp(a, b, method="asymp").statistic
        rows.append((abs(A - 0.5) + 0.5, A, ks, nm, np.median(ALL[t1, i]), np.median(ALL[t0, i]),
                     ALL[t1, i].mean(), ALL[t0, i].mean()))
    rows.sort(reverse=True)
    print("   %-24s %7s %7s %10s %10s %10s %10s" % ("feature", "AUC", "KS", "med_true",
                                                     "med_fake", "mean_true", "mean_fake"))
    for _, A, ks, nm, mt, mf, at, af in rows:
        print("   %-24s %7.4f %7.4f %10.3f %10.3f %10.3f %10.3f" % (nm, A, ks, mt, mf, at, af))

report(passT, "ALL accepted-shape (300 evt)")
report(passT & (nL == 5), "accepted-shape nLayers==5")
report(passT & (nL >= 6), "accepted-shape nLayers>=6")
report(passT & isT4, "accepted-shape nLayers<=4 (T4-class)")
report(passT & isTest, "ALL accepted-shape, FROZEN TEST-60 only")

# ---- edge-logit / gate-logit / chi2 quantiles on the accepted-shape subpopulation ----
print("\n=== quantiles on accepted-shape chains (5/25/50/75/95%) ===")
for i, nm in [(3, "minEdgeLogit"), (4, "meanEdgeLogit"), (2, "sumEdgeLogit"),
              (5, "fullFitChi2PerHit"), (6, "rzLineChi2PerHit"), (14, "maxJunctionDegProduct"),
              (15, "chargeConsistency"), (8, "dKappaFitVsMedianT3")]:
    for lv, tag in [(1, "true"), (0, "fake")]:
        m = passT & (lab == lv)
        q = np.percentile(X[m, i], [5, 25, 50, 75, 95])
        print("  %-22s %-5s %10.3f %10.3f %10.3f %10.3f %10.3f" % (nm, tag, *q))
for lv, tag in [(1, "true"), (0, "fake")]:
    m = passT & (lab == lv)
    q = np.percentile(gate[m], [5, 25, 50, 75, 95])
    print("  %-22s %-5s %10.3f %10.3f %10.3f %10.3f %10.3f" % ("GATE_LOGIT", tag, *q))
    q = np.percentile(legacy[m], [5, 25, 50, 75, 95])
    print("  %-22s %-5s %10.3f %10.3f %10.3f %10.3f %10.3f" % ("LEGACY_SCORE", tag, *q))

# ---- what would a gate cut on the 5+ path cost/buy? (rejection at fixed true-eff) ----
print("\n=== gate on the 5+-layer path: fake rejection at fixed label-true efficiency ===")
for cl, m in [("nLayers==5", passT & (nL == 5)), ("nLayers>=6", passT & (nL >= 6))]:
    st = gate[m & (lab == 1)]; sf = gate[m & (lab == 0)]
    stD = gate[m & (lab == 1) & (vxy >= 1)]
    print("  %s: n_true=%d n_fake=%d  n_true_disp(vxy>=1)=%d" % (cl, len(st), len(sf), len(stD)))
    for eff in [0.999, 0.995, 0.99, 0.98, 0.95]:
        th = np.quantile(st, 1 - eff)
        thD = np.quantile(stD, 1 - eff) if len(stD) > 20 else float("nan")
        print("     eff(all-true)=%.3f -> thr %7.3f  fake rej %.4f | disp-true eff at that thr %.4f"
              % (eff, th, (sf < th).mean(), (stD >= th).mean()))

# ---- displaced content of the accepted-shape true population (what a cut risks) ----
print("\n=== displaced content of accepted-shape label-1 chains ===")
for cl, m in [("<=4", passT & isT4), ("==5", passT & (nL == 5)), (">=6", passT & (nL >= 6))]:
    tt = m & (lab == 1)
    known = tt & (vxy > -900)
    print("  nLayers %s: true=%d  with accepted-sim kinematics=%d  vxy>=1: %d (%.4f of known)"
          " vxy>=5: %d" % (cl, tt.sum(), known.sum(), (known & (vxy >= 1)).sum(),
                           (vxy[known] >= 1).mean(), (known & (vxy >= 5)).sum()))
