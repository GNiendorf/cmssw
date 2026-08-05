#!/usr/bin/env python3
"""m13_edges.py - the just-below-zero TRUE edges: prompt-vs-displaced separability.

READ-ONLY. Design input for a 3-CLASS EDGE head (fake / prompt-true / displaced-true),
the formation-side analogue of the -G 6 chain gate.

Inputs (all frozen):
  m10_edgecache_300.npz  v3 edge-MLP logit + label + simIdx + etype for all 32.9M rows
                         of edges_300evt.root (the LIVE weights w7 runs on)
  m10_edgeidx.npz        (lumi,evt) -> [row_begin,row_end) blocks
  edges_300evt.root      the 27 raw edge/node features per row

Step 1 (cache only, all 300 evts): population census of the sub-zero window by
        sim displacement -- how much fake would a GLOBAL threshold move admit, and how
        much displaced-true is sitting there.
Step 2 (feature read over a subset of event blocks): per-feature separation of
        displaced-true vs fake INSIDE the window, single-feature AUCs, and a logistic
        probe = achievable-AUC estimate for a displaced head restricted to the window.
"""
import sys

import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SC = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-"
      "LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")
NEV = int(sys.argv[1]) if len(sys.argv) > 1 else 60

NI = ["kappaSigned", "log10R", "tanLambda", "chordEta", "dphi01", "dz01", "dz12", "drt01",
      "drt12", "innermostLayer", "nBarrel", "nPS", "fakeScoreT3"]
EFN = ["etype", "dKappa", "dKappaRel", "chargeAgree", "dTanLambda", "kinkPhi", "kinkTheta",
       "centerDist", "centerDistRel", "sharedLayer", "sharedIsPS", "sharedIsBarrel",
       "degIn", "degOut"]
COLS = ([f"ni_{i:02d}" for i in range(13)] + [f"no_{i:02d}" for i in range(13)] +
        [f"ef_{i:02d}" for i in range(14)])
NAMES = [f"in.{n}" for n in NI] + [f"out.{n}" for n in NI] + [f"e.{n}" for n in EFN]

cache = np.load(f"{SC}/m10_edgecache_300.npz")
LOG, LAB, SIM = cache["logit"], cache["label"], cache["simIdx"]
idx = np.load(f"{SC}/m10_edgeidx.npz")
starts, ends, ev_of_block = idx["starts"], idx["ends"], idx["evt"]

# per-(evt,sim) displacement for EVERY sim row (edge simIdx is a full sim row)
nt = uproot.open(f"{SA}/LSTNtuple_PU200RelVal_300evt.root")["tree"]
sa = nt.arrays(["sim_vx", "sim_vy", "sim_pca_dxy", "sim_pt", "evt"], library="np")
evmap = {int(sa["evt"][i]): i for i in range(len(sa["evt"]))}

vxy_e = np.full(len(LOG), -1.0, dtype=np.float32)
dxy_e = np.full(len(LOG), -1.0, dtype=np.float32)
for b in range(len(starts)):
    i = evmap[int(ev_of_block[b])]
    vx, vy, dxy = sa["sim_vx"][i], sa["sim_vy"][i], np.abs(sa["sim_pca_dxy"][i])
    vxy = np.hypot(vx, vy)
    lo, hi = int(starts[b]), int(ends[b])
    s = SIM[lo:hi]
    ok = (s >= 0) & (s < len(vxy))
    vv = np.full(hi - lo, -1.0, dtype=np.float32)
    dd = np.full(hi - lo, -1.0, dtype=np.float32)
    vv[ok] = vxy[s[ok]]
    dd[ok] = dxy[s[ok]]
    vxy_e[lo:hi], dxy_e[lo:hi] = vv, dd

TRUE = LAB == 1
FAKE = LAB == 0
PR = TRUE & (vxy_e >= 0) & (vxy_e < 1)
D1 = TRUE & (vxy_e >= 1) & (vxy_e < 10)
D10 = TRUE & (vxy_e >= 10)
DX10 = TRUE & (dxy_e >= 10)

print(f"===== STEP 1  sub-zero window census (300 evts, {len(LOG)/1e6:.1f}M edges) =====")
print(f"{'window':14s}{'fake':>12s}{'true prompt':>13s}{'true vxy1-10':>14s}"
      f"{'true vxy>=10':>13s}{'true dxy>=10':>13s}{'fake/evt':>10s}")
for lo, hi in [(-0.5, 0.0), (-1.0, 0.0), (-2.0, 0.0), (-4.0, 0.0), (0.0, 1e9)]:
    m = (LOG >= lo) & (LOG < hi)
    print(f"[{lo:g},{hi:g})".ljust(14) + f"{int((m & FAKE).sum()):12d}{int((m & PR).sum()):13d}"
          f"{int((m & D1).sum()):14d}{int((m & D10).sum()):13d}{int((m & DX10).sum()):13d}"
          f"{(m & FAKE).sum()/300:10.0f}")
print(f"  purity of the [-2,0) window: displaced-true(vxy>=1) / fake = "
      f"{((LOG>=-2)&(LOG<0)&(D1|D10)).sum()/max(((LOG>=-2)&(LOG<0)&FAKE).sum(),1):.5f}")

# ---- STEP 2: features, on the first NEV event blocks ----
blocks = list(range(min(NEV, len(starts))))
rows = np.concatenate([np.arange(starts[b], ends[b]) for b in blocks])
lo_, hi_ = int(rows[0]), int(rows[-1]) + 1
print(f"\n===== STEP 2  feature read: {NEV} event blocks, rows {lo_}..{hi_} =====")
WIN = (LOG >= -2.0) & (LOG < 0.0)
sub = np.zeros(len(LOG), dtype=bool)
sub[lo_:hi_] = True
selD = sub & WIN & (D10 | DX10)
selP = sub & WIN & PR
selF = sub & WIN & FAKE
rng = np.random.default_rng(7)
# subsample the two huge classes to keep the arrays small but statistics ample
for nm, sel, cap in [("disp", selD, 10 ** 9), ("prompt", selP, 300000), ("fake", selF, 600000)]:
    n = int(sel.sum())
    if n > cap:
        k = np.nonzero(sel)[0]
        sel[k[rng.permutation(n)[cap:]]] = False
    print(f"  {nm:7s} rows in window: {n} -> kept {int(sel.sum())}")
keep = selD | selP | selF
kidx = np.nonzero(keep)[0]
X = np.empty((len(kidx), len(COLS)), dtype=np.float32)
t = uproot.open(f"{SA}/prototype/edges_300evt.root")["edges"]
CH = 2_000_000
fill = 0
for c0 in range(lo_, hi_, CH):
    c1 = min(c0 + CH, hi_)
    m = keep[c0:c1]
    if not m.any():
        continue
    a = t.arrays(COLS, entry_start=c0, entry_stop=c1, library="np")
    k = int(m.sum())
    for j, cn in enumerate(COLS):
        X[fill:fill + k, j] = a[cn][m]
    fill += k
    print(f"    read {c1 - lo_}/{hi_ - lo_}", flush=True)
assert fill == len(kidx)
yD = selD[kidx]; yP = selP[kidx]; yF = selF[kidx]
print(f"  matrix {X.shape}: disp={int(yD.sum())} prompt={int(yP.sum())} fake={int(yF.sum())}")


def auc(pos, neg):
    a = np.concatenate([pos, neg])
    r = np.empty(len(a))
    o = np.argsort(a, kind="stable")
    r[o] = np.arange(1, len(a) + 1)
    # tie-average
    v = a[o]
    i = 0
    while i < len(v):
        j = i
        while j + 1 < len(v) and v[j + 1] == v[i]:
            j += 1
        if j > i:
            r[o[i:j + 1]] = (i + 1 + j + 1) / 2.0
        i = j + 1
    return (r[:len(pos)].sum() - len(pos) * (len(pos) + 1) / 2) / (len(pos) * len(neg))


print("\n  per-feature separation INSIDE the [-2,0) window "
      "(medians; AUC = displaced-true vs fake, and prompt-true vs fake)")
res = []
for j, nm in enumerate(NAMES):
    a = auc(X[yD, j], X[yF, j])
    b = auc(X[yP, j], X[yF, j])
    res.append((abs(a - 0.5), a, b, nm, np.median(X[yD, j]), np.median(X[yP, j]), np.median(X[yF, j])))
res.sort(reverse=True)
print(f"    {'feature':22s}{'AUC disp/fake':>14s}{'AUC prompt/fake':>16s}{'|dAUC|':>8s}"
      f"{'med disp':>10s}{'med prompt':>11s}{'med fake':>10s}")
for s, a, b, nm, md, mp, mf in res[:18]:
    print(f"    {nm:22s}{a:14.3f}{b:16.3f}{abs(a-b):8.3f}{md:10.3f}{mp:11.3f}{mf:10.3f}")

# logistic probe: how separable are displaced-true and fake inside the window?
mu, sd = X.mean(0), X.std(0) + 1e-9
Z = (X - mu) / sd
Z = np.hstack([Z, np.ones((len(Z), 1))])


def fit(pos, neg, iters=400, lr=0.5):
    y = np.concatenate([np.ones(pos.sum()), np.zeros(neg.sum())])
    A = np.vstack([Z[pos], Z[neg]])
    w = np.zeros(A.shape[1])
    n1, n0 = y.sum(), len(y) - y.sum()
    sw = np.where(y > 0, 0.5 / n1, 0.5 / n0)
    for _ in range(iters):
        p = 1 / (1 + np.exp(-A @ w))
        w -= lr * (A.T @ (sw * (p - y)))
    return w, A, y


for tag, pos in [("displaced-true (vxy>=10 or dxy>=10)", yD), ("prompt-true", yP)]:
    tr = np.zeros(len(Z), bool); te = np.zeros(len(Z), bool)
    k = np.nonzero(pos | yF)[0]
    perm = rng.permutation(len(k))
    tr[k[perm[: int(0.7 * len(k))]]] = True
    te[k[perm[int(0.7 * len(k)):]]] = True
    w, _, _ = fit(pos & tr, yF & tr)
    s = Z @ w
    print(f"  logistic probe in-window, {tag} vs fake: heldout AUC = "
          f"{auc(s[pos & te], s[yF & te]):.4f}   (n_pos={int((pos&te).sum())})")

# cross-check: does the CURRENT edge logit itself separate them in the window?
print(f"  current v3 logit in-window AUC disp/fake = {auc(LOG[kidx][yD], LOG[kidx][yF]):.4f}"
      f" | prompt/fake = {auc(LOG[kidx][yP], LOG[kidx][yF]):.4f}")
