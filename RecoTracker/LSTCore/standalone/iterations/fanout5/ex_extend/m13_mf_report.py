#!/usr/bin/env python3
"""M13 mission-1 report: where the residual fake lives at anchor m12_w7.

Consumes m13_mf_accepted.pkl (written by m13_mf_anatomy.py). READ-ONLY.
All rates are quoted on the harness FR convention (denominator = TCs with pt > 0.9).
"""
import pickle

import numpy as np

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/"
PTCUT = 0.9
DCASPLIT = 0.5
M4, M5, M6, M4D, MR = 1.4949901503891982, 0.8974147108711509, -0.6053881223017352, -0.5, -0.8
DENOM_ALL = 573648      # w7 in-cut TC denominator (harness)
PIX_FAKES = 13541       # in-cut pixel-slice fakes (fixed, bit-identical to baseline)
BASE_FR = 21620 / 475212

A = pickle.load(open(P + "m13_mf_accepted.pkl", "rb"))
m = A["pt"] > PTCUT
for k in list(A):
    A[k] = np.asarray(A[k])[m]
n = len(A["pt"])
fake = A["fake"] > 0
nL = A["nL_d"]
dca = A["dca"]
ip = dca < DCASPLIT
ae = np.abs(A["eta"])
NF = fake.sum()
print("=== in-cut chain TCs: %d, fakes %d (chain-slice FR %.4f), contribution to aggregate %.4f ==="
      % (n, NF, NF / n, NF / DENOM_ALL))
print("    aggregate w7 FR %.4f = pixel %.4f + chain %.4f ; baseline %.4f"
      % ((NF + PIX_FAKES) / DENOM_ALL, PIX_FAKES / DENOM_ALL, NF / DENOM_ALL, BASE_FR))
need = (NF + PIX_FAKES - BASE_FR * DENOM_ALL) / (1 - BASE_FR)
print("    chain fakes that must die (zero true loss) to reach baseline aggregate: %.0f = %.1f%% of chain fakes"
      % (need, 100 * need / NF))


def cells(name, keys, labels):
    print("\n--- %s ---" % name)
    print("  %-26s %8s %8s %8s %8s %8s %8s" %
          ("cell", "nTC", "fakes", "cellFR", "%allfk", "aggcontr", "trues"))
    order = sorted(set(zip(*keys)) if len(keys) > 1 else set(keys[0]))
    rows = []
    for c in order:
        sel = np.ones(n, bool)
        for k, v in zip(keys, c if len(keys) > 1 else (c,)):
            sel &= (k == v)
        if sel.sum() == 0:
            continue
        rows.append((int((sel & fake).sum()), c, int(sel.sum())))
    rows.sort(reverse=True)
    for nf, c, ns in rows:
        lab = labels(c)
        print("  %-26s %8d %8d %8.4f %7.1f%% %8.4f %8d"
              % (lab, ns, nf, nf / ns, 100 * nf / NF, nf / DENOM_ALL, ns - nf))


# ---------------------------------------------------------------- 1. branch x length --
br = np.where(nL <= 4, np.where(ip, 0, 1), np.where(ip, 2, 3))
brn = {0: "IP-T4      (dca<0.5,nL<=4)", 1: "exempt-T4  (dca>=.5,nL<=4)",
       2: "IP-5+      (dca<0.5,nL>=5)", 3: "exempt-5+  (dca>=.5,nL>=5)"}
cells("BRANCH (the -G 6 kill branches)", [br], lambda c: brn[c])

nLc = np.clip(nL, 4, 7)
cells("BRANCH x nLayers", [br, nLc],
      lambda c: "%s nL=%s" % (brn[c[0]][:10], "7+" if c[1] == 7 else c[1]))

cells("nLayers", [nLc], lambda c: "nLayers %s" % ("7+" if c == 7 else c))

nn = np.clip(A["nNodes"].astype(int), 2, 5)
cells("nNodes (member T3 count, 5=5+)", [nn], lambda c: "nNodes %s" % ("5+" if c == 5 else c))
cells("(nNodes, nLayers) cell -- the M10 (2,5) hot cell", [nn, nLc],
      lambda c: "nNodes %s nLayers %s" % ("5+" if c[0] == 5 else c[0], "7+" if c[1] == 7 else c[1]))

reg = np.where(ae < 1.1, 0, np.where(ae < 1.7, 1, 2))
cells("eta region", [reg], lambda c: ["barrel", "transition", "endcap"][c])
cells("eta region x branch", [reg, br],
      lambda c: "%-10s %s" % (["barrel", "transition", "endcap"][c[0]], brn[c[1]][:10]))

# ---------------------------------------------------------------- 2. composition ------
mf = A["matchFrac"]
comp = np.where(mf < 1e-6, 0, np.where(mf < 0.34, 1, np.where(mf < 0.5, 2, np.where(mf <= 0.75, 3, 4))))
cn = {0: "pure junk   (frac=0)", 1: "junk-dom    (0,1/3)", 2: "mixed       [1/3,1/2)",
      3: "contaminated[1/2,.75]", 4: "TRUE        (>0.75)"}
print("\n--- COMPOSITION of accepted chain TCs by best hit-match fraction (harness metric) ---")
print("  %-24s %9s %9s %9s" % ("class", "n", "%of fakes", "%of all"))
for c in range(5):
    s = comp == c
    print("  %-24s %9d %8.1f%% %8.1f%%" % (cn[c], s.sum(), 100 * (s & fake).sum() / NF, 100 * s.sum() / n))
print("  matchFrac quantiles among FAKES (5/25/50/75/95): %s"
      % np.round(np.percentile(mf[fake], [5, 25, 50, 75, 95]), 4))
print("  --- composition x branch (%% of that branch's fakes) ---")
print("  %-26s %8s %8s %8s %8s" % ("branch", "frac=0", "(0,1/3)", "[1/3,1/2)", "[1/2,.75]"))
for b in range(4):
    s = fake & (br == b)
    if s.sum() == 0:
        continue
    print("  %-26s %7.1f%% %7.1f%% %7.1f%% %7.1f%%"
          % (brn[b], *[100 * (s & (comp == c)).mean() * len(s) / s.sum() for c in range(4)]))

# M10 label bug: how many surviving fakes were LABEL-1 under the old rule
lo = A["label_old"].astype(int)
print("\n--- M10 LABEL BUG, remeasured at w7 ---")
print("  surviving fakes that are label_old==1 (old rule would train them TRUE): %d / %d = %.4f"
      % ((fake & (lo == 1)).sum(), NF, (fake & (lo == 1)).mean()))
print("  M10 measured 0.234 of accepted fakes; perfect-classifier floor on the OLD label was 0.1235")
print("  NEW harness label == (1 - tc_isFake) EXACTLY (verified 170918/170918):")
print("    => LABEL-LIMITED FLOOR of the chain slice with the new labels = 0.0000")
print("    => perfect-classifier aggregate FR = pixel-only = %.4f (vs baseline %.4f)"
      % (PIX_FAKES / DENOM_ALL, BASE_FR))

# ---------------------------------------------------------------- 3. gate margins -----
mP, mD, mX = A["mP"], A["mD"], A["mX"]
gov = np.where(br == 0, mX, np.where(br == 1, mD, mP))     # governing margin per branch
thr = np.where(br == 0, M4, np.where(br == 1, M4D, np.where(nL >= 6, M6, M5)))
print("\n--- GATE MARGINS of surviving chain TCs (governing margin minus its kill threshold) ---")
print("  branch                      margin  thr      fake p5   p25   p50   p75 | true p5   p25   p50")
for b in range(4):
    s = br == b
    if s.sum() == 0:
        continue
    gname = {0: "mX", 1: "mD", 2: "mP", 3: "mX(resc)"}[b]
    g = mX[s] if b == 3 else gov[s]
    t = MR if b == 3 else thr[s][0] if b != 2 else float("nan")
    d = g - (MR if b == 3 else (thr[s] if b == 2 else t))
    f_, tr = fake[s], ~fake[s]
    print("  %-26s %-7s %-8s %s | %s"
          % (brn[b], gname, "per-nL" if b == 2 else "%.3f" % (MR if b == 3 else t),
             np.round(np.percentile(d[f_], [5, 25, 50, 75]), 2),
             np.round(np.percentile(d[tr], [5, 25, 50]), 2)))

print("\n--- THRESHOLD HEADROOM: post-hoc sweep on the ACCEPTED set (upper bound on kill,")
print("    ignores MD re-claim; a killed chain frees hits that other chains may re-take) ---")


def sweep(sel, score, name, qs=(0.005, 0.01, 0.02, 0.03, 0.05, 0.10, 0.20)):
    st = score[sel & ~fake]
    sf = score[sel & fake]
    if len(st) < 50 or len(sf) < 50:
        return
    a = st[:, None] > sf[None, :] if len(st) * len(sf) < 4e7 else None
    auc = float(a.mean()) if a is not None else float(np.mean(
        [np.mean(st > x) for x in np.random.default_rng(0).choice(sf, 4000)]))
    print("  %-30s n_true=%6d n_fake=%6d AUC=%.4f" % (name, len(st), len(sf), auc))
    for q in qs:
        cut = np.quantile(st, q)
        kf = int((sf < cut).sum())
        kt = int((st < cut).sum())
        print("       true loss %5.1f%% -> cut %8.3f kills %6d fakes (%.1f%% of branch fakes,"
              " %.1f%% of ALL chain fakes) | aggregate FR %.4f"
              % (100 * q, cut, kf, 100 * kf / len(sf), 100 * kf / NF,
                 (NF + PIX_FAKES - kf - kt * 0) / (DENOM_ALL - kf - kt)))


for b, sc, nm in [(0, mX, "IP-T4 on mX"), (1, mD, "exempt-T4 on mD"),
                  (2, mP, "IP-5+ on mP"), (2, mX, "IP-5+ on mX"),
                  (3, mX, "exempt-5+ on mX (the -MR rescue)"), (3, mD, "exempt-5+ on mD")]:
    sweep(br == b, sc, nm)
sweep(np.ones(n, bool), mX, "ALL branches on mX")
sweep(np.ones(n, bool), np.maximum(mP, mD), "ALL branches on max(mP,mD)")

# ---------------------------------------------------------------- 4. saturation -------
print("\n--- SATURATION CHECK: what fraction of each branch is ALREADY at/near its kill edge? ---")
for b in range(4):
    s = br == b
    if s.sum() == 0:
        continue
    g = mX[s] if b in (0, 3) else (mD[s] if b == 1 else mP[s])
    t = M4 if b == 0 else (M4D if b == 1 else (MR if b == 3 else np.where(nL[s] >= 6, M6, M5)))
    d = g - t
    print("  %-26s frac within 0.5 of thr: fakes %.3f trues %.3f | frac > thr+3: fakes %.3f trues %.3f"
          % (brn[b], (d[fake[s]] < 0.5).mean(), (d[~fake[s]] < 0.5).mean(),
             (d[fake[s]] > 3).mean(), (d[~fake[s]] > 3).mean()))

# ---------------------------------------------------------------- 5. displaced cost ---
print("\n--- what the TRUES in each branch are (displaced content = the price of any kill) ---")
vxy = A["simVxy"]
for b in range(4):
    s = (br == b) & ~fake
    kn = s & (vxy > -900)
    print("  %-26s trues %6d  with kinematics %6d  vxy>=1 %5d (%.4f)  vxy>=5 %4d  vxy>=10 %4d"
          % (brn[b], s.sum(), kn.sum(), (kn & (vxy >= 1)).sum(),
             (vxy[kn] >= 1).mean() if kn.sum() else 0, (kn & (vxy >= 5)).sum(),
             (kn & (vxy >= 10)).sum()))

# ---------------------------------------------------------------- 6. duplicates -------
dup = A["dup"] > 0
print("\n--- fake vs duplicate (M10: disjoint populations) ---")
print("  chain TCs: fake %d, dup %d, both %d ; dup among trues %.4f"
      % (fake.sum(), dup.sum(), (fake & dup).sum(), dup[~fake].mean()))
