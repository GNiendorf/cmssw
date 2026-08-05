#!/usr/bin/env python3
"""m13_edges2.py - is a 3-CLASS EDGE head (fake/prompt/displaced) worth building?

READ-ONLY. The decisive test the M13 mission asks for: inside the just-below-zero
window, does the displaced-true class occupy a DIFFERENT region of feature space than
the prompt-true class (-> a dedicated displaced output helps), or the same one
(-> only generic capacity/objective gain, and the displaced deficit is a THRESHOLD /
CALIBRATION problem, not a representation problem)?

Protocol (all inside the [-2,0) window of the live v3 edge logit):
  1. probe_P = logistic(prompt-true vs fake)      trained on 70%, held out 30%
  2. probe_D = logistic(displaced-true vs fake)   same split discipline
  3. CROSS-APPLY: AUC(probe_P on displaced-vs-fake) vs AUC(probe_D on displaced-vs-fake).
     Equal => one shared "true" direction; a displaced head cannot beat a well-trained
     2-class head on features alone.
  4. recovery-vs-fake-budget: at a fixed number of extra fake edges admitted per event,
     what fraction of in-window displaced-true edges is recovered (by the probe, and by
     simply lowering thetaEdge)?
Caches the feature matrix so re-runs are instant.
"""
import os

import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SC = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-"
      "LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")
CACHE = f"{SC}/m13_edgewin.npz"
NEV = 40
COLS = ([f"ni_{i:02d}" for i in range(13)] + [f"no_{i:02d}" for i in range(13)] +
        [f"ef_{i:02d}" for i in range(14)])
NI = ["kappaSigned", "log10R", "tanLambda", "chordEta", "dphi01", "dz01", "dz12", "drt01",
      "drt12", "innermostLayer", "nBarrel", "nPS", "fakeScoreT3"]
EFN = ["etype", "dKappa", "dKappaRel", "chargeAgree", "dTanLambda", "kinkPhi", "kinkTheta",
       "centerDist", "centerDistRel", "sharedLayer", "sharedIsPS", "sharedIsBarrel",
       "degIn", "degOut"]
NAMES = [f"in.{n}" for n in NI] + [f"out.{n}" for n in NI] + [f"e.{n}" for n in EFN]


def build():
    cache = np.load(f"{SC}/m10_edgecache_300.npz")
    LOG, LAB, SIM = cache["logit"], cache["label"], cache["simIdx"]
    idx = np.load(f"{SC}/m10_edgeidx.npz")
    starts, ends, evb = idx["starts"], idx["ends"], idx["evt"]
    nt = uproot.open(f"{SA}/LSTNtuple_PU200RelVal_300evt.root")["tree"]
    sa = nt.arrays(["sim_vx", "sim_vy", "sim_pca_dxy", "sim_pt", "evt"], library="np")
    em = {int(sa["evt"][i]): i for i in range(len(sa["evt"]))}
    vxy_e = np.full(len(LOG), -1.0, np.float32)
    dxy_e = np.full(len(LOG), -1.0, np.float32)
    for b in range(len(starts)):
        i = em[int(evb[b])]
        vxy = np.hypot(sa["sim_vx"][i], sa["sim_vy"][i])
        dxy = np.abs(sa["sim_pca_dxy"][i])
        lo, hi = int(starts[b]), int(ends[b])
        s = SIM[lo:hi]
        ok = (s >= 0) & (s < len(vxy))
        vv = np.full(hi - lo, -1.0, np.float32); dd = np.full(hi - lo, -1.0, np.float32)
        vv[ok] = vxy[s[ok]]; dd[ok] = dxy[s[ok]]
        vxy_e[lo:hi], dxy_e[lo:hi] = vv, dd
    lo_, hi_ = int(starts[0]), int(ends[NEV - 1])
    WIN = (LOG >= -2.0) & (LOG < 0.0)
    sub = np.zeros(len(LOG), bool); sub[lo_:hi_] = True
    cls = np.zeros(len(LOG), np.int8)                    # 0 fake
    cls[(LAB == 1) & (vxy_e >= 0) & (vxy_e < 1)] = 1     # prompt-true
    cls[(LAB == 1) & (vxy_e >= 1)] = 2                   # displaced-true
    cls[(LAB == 1) & (vxy_e < 0)] = 3                    # true but pileup-only sim
    sel = sub & WIN & (cls != 3)
    rng = np.random.default_rng(11)
    k = np.nonzero(sel & (cls == 0))[0]
    drop = k[rng.permutation(len(k))[600000:]]
    sel[drop] = False
    kidx = np.nonzero(sel)[0]
    X = np.empty((len(kidx), len(COLS)), np.float32)
    t = uproot.open(f"{SA}/prototype/edges_300evt.root")["edges"]
    fill = 0
    for c0 in range(lo_, hi_, 2_000_000):
        c1 = min(c0 + 2_000_000, hi_)
        m = sel[c0:c1]
        if not m.any():
            continue
        a = t.arrays(COLS, entry_start=c0, entry_stop=c1, library="np")
        n = int(m.sum())
        for j, cn in enumerate(COLS):
            X[fill:fill + n, j] = a[cn][m]
        fill += n
    np.savez(CACHE, X=X, cls=cls[kidx], logit=LOG[kidx], vxy=vxy_e[kidx], dxy=dxy_e[kidx])
    print(f"cached {X.shape}")


if not os.path.exists(CACHE):
    build()
c = np.load(CACHE)
X, cls, LG, vxy, dxy = c["X"], c["cls"], c["logit"], c["vxy"], c["dxy"]
F = cls == 0
P = cls == 1
Dall = cls == 2
D10 = Dall & ((vxy >= 10) | (dxy >= 10))
print(f"in-window sample ({NEV} evts): fake={F.sum()} prompt-true={P.sum()} "
      f"displaced-true(vxy>=1)={Dall.sum()} of which vxy/dxy>=10 = {D10.sum()}")


def auc(pos, neg):
    a = np.concatenate([pos, neg]); o = np.argsort(a, kind="stable")
    r = np.empty(len(a)); r[o] = np.arange(1, len(a) + 1)
    v = a[o]; i = 0
    while i < len(v):
        j = i
        while j + 1 < len(v) and v[j + 1] == v[i]:
            j += 1
        if j > i:
            r[o[i:j + 1]] = (i + 1 + j + 1) / 2.0
        i = j + 1
    return (r[:len(pos)].sum() - len(pos) * (len(pos) + 1) / 2) / (len(pos) * len(neg))


mu, sd = X.mean(0), X.std(0) + 1e-9
Z = np.hstack([(X - mu) / sd, np.ones((len(X), 1))])
rng = np.random.default_rng(3)
tr = rng.random(len(X)) < 0.7
te = ~tr


def fit(pos, neg, iters=600, lr=0.5):
    A = np.vstack([Z[pos], Z[neg]])
    y = np.concatenate([np.ones(int(pos.sum())), np.zeros(int(neg.sum()))])
    sw = np.where(y > 0, 0.5 / max(y.sum(), 1), 0.5 / max(len(y) - y.sum(), 1))
    w = np.zeros(A.shape[1])
    for _ in range(iters):
        p = 1 / (1 + np.exp(-A @ w))
        w -= lr * (A.T @ (sw * (p - y)))
    return w


wP = fit(P & tr, F & tr)
wD = fit(Dall & tr, F & tr)
sP, sD = Z @ wP, Z @ wD
print("\n===== CROSS-APPLICATION TEST (held-out 30%) =====")
print(f"  probe_P (trained prompt-vs-fake)    : AUC prompt={auc(sP[P & te], sP[F & te]):.4f}  "
      f"displaced={auc(sP[Dall & te], sP[F & te]):.4f}  vxy/dxy>=10={auc(sP[D10 & te], sP[F & te]):.4f}")
print(f"  probe_D (trained displaced-vs-fake) : AUC prompt={auc(sD[P & te], sD[F & te]):.4f}  "
      f"displaced={auc(sD[Dall & te], sD[F & te]):.4f}  vxy/dxy>=10={auc(sD[D10 & te], sD[F & te]):.4f}")
print(f"  live v3 edge logit                  : AUC prompt={auc(LG[P & te], LG[F & te]):.4f}  "
      f"displaced={auc(LG[Dall & te], LG[F & te]):.4f}  vxy/dxy>=10={auc(LG[D10 & te], LG[F & te]):.4f}")
cos = float(wP[:-1] @ wD[:-1] / (np.linalg.norm(wP[:-1]) * np.linalg.norm(wD[:-1])))
print(f"  cosine(weight_P, weight_D) = {cos:.4f}   (1.0 = the two classes share ONE "
      f"true-vs-fake direction -> a dedicated displaced OUTPUT buys nothing on these features)")

print("\n===== CALIBRATION: is displaced-true simply SHIFTED DOWN by the current head? =====")
for nm, m in [("prompt-true", P), ("displaced-true vxy[1,10)", Dall & ~D10),
              ("displaced-true vxy/dxy>=10", D10), ("fake", F)]:
    print(f"  {nm:26s} live logit med={np.median(LG[m]):6.2f}  probe_P percentile-rank "
          f"vs fake={auc(sP[m], sP[F]):.3f}")

print("\n===== RECOVERY vs FAKE BUDGET inside the window (held-out) =====")
print("  target = in-window displaced-true edges (vxy>=1). Baseline lever = lowering")
print("  thetaEdge; proposed lever = re-scoring the window with probe_D.")
nfk = int((F & te).sum())
scale = nfk / 40.0 * (300 / 300)  # fakes per event in this held-out sample
print(f"  {'admitted fake/evt':>18s}{'thetaEdge equiv':>17s}{'recovered disp %':>18s}"
      f"{'probe_D recovered %':>21s}")
for frac in (0.02, 0.05, 0.10, 0.20, 0.40):
    nadm = int(frac * nfk)
    thr = np.sort(LG[F & te])[::-1][nadm - 1]
    recT = (LG[Dall & te] >= thr).mean()
    thrD = np.sort(sD[F & te])[::-1][nadm - 1]
    recD = (sD[Dall & te] >= thrD).mean()
    print(f"  {nadm/(NEV*0.3):18.0f}{thr:17.2f}{100*recT:18.1f}{100*recD:21.1f}")
