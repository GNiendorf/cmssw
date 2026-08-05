#!/usr/bin/env python3
"""m13_dxy.py - the dxy[10,30) deficit dissected at w7, plus the baseline-gap census.

READ-ONLY. Uses the same joins as m13_funnel.py. Three questions:
  Q1  WHICH sims does the LST baseline find that w7 does not (per band), and where do
      those exact sims die in our funnel? (the deficit is defined against baseline, not
      against the whole band)
  Q2  profile of the dxy[10,30) losses per stage
  Q3  MEASURED thetaEdge what-if (m10 caches: full re-weld + re-K9 at thetaEdge -1 / -2)
      for the edge-theta pool, per band.
"""
import numpy as np

SC = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-"
      "LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")

W = np.load(f"{SC}/m13_sims_w7.npy")
F = np.load(f"{SC}/m10_funnel2.npy")
D = np.load(f"{SC}/m10_deep.npy")
C = np.load(f"{SC}/m13_chains_w7.npz")

OK = (np.abs(W["vz"]) < 30) & (W["q"] != 0)
KEY = W["evt"].astype(np.int64) * 100000 + W["sim"].astype(np.int64)


def join(a, cols):
    k = a["evt"].astype(np.int64) * 100000 + a["sim"].astype(np.int64)
    o = np.argsort(k); ks = k[o]
    p = np.searchsorted(ks, KEY)
    hit = (p < len(ks)) & (ks[np.clip(p, 0, len(ks) - 1)] == KEY)
    i = o[np.clip(p, 0, len(ks) - 1)]
    return hit, {c: np.where(hit, a[c][i], -1) for c in cols}


inF, f = join(F, ["nPass", "nWeld", "nTrue", "maxL", "max2L", "nAcc", "nWeld_m1", "nAcc_m1",
                  "nWeld_m2", "nAcc_m2", "nCh", "maxNL"])
inD, d = join(D, ["nMDmatch", "nT3match", "bestPur"])

ck = C["evt"].astype(np.int64) * 100000 + C["simIdx"].astype(np.int64)
cap = (C["label"] == 1) & (C["simIdx"] >= 0)
kc = np.sort(ck[cap])
p = np.searchsorted(kc, KEY)
nCap = (np.searchsorted(kc, KEY, "right") - p)
kcp = np.sort(ck[cap & C["thetaPass"]])
nCapPass = np.searchsorted(kcp, KEY, "right") - np.searchsorted(kcp, KEY)

ST = [("vxy<1", "vxy", 0, 1), ("vxy[1,5)", "vxy", 1, 5), ("vxy[5,10)", "vxy", 5, 10),
      ("vxy[10,30)", "vxy", 10, 30), ("dxy<1", "dxy", 0, 1), ("dxy[1,5)", "dxy", 1, 5),
      ("dxy[5,10)", "dxy", 5, 10), ("dxy[10,30)", "dxy", 10, 30)]
MASKS = [(nm, OK & (W[v] >= lo) & (W[v] < hi)) for nm, v, lo, hi in ST]

STAGE = [("not formable", ~inF),
         ("edge-theta", inF & (f["nPass"] == 0)),
         ("welding", (f["nPass"] > 0) & (f["nWeld"] == 0)),
         ("purity(<75%)", (f["nWeld"] > 0) & (nCap == 0)),
         ("gate kill", (nCap > 0) & (nCapPass == 0)),
         ("pixdrop/claim", (nCapPass > 0))]

print("===== Q1  BASELINE-GAP CENSUS: sims LST finds and w7 does not =====")
gap = OK & W["baseTC"] & ~W["anyTC"]
sur = OK & ~W["baseTC"] & W["anyTC"]
print(f"{'band':12s}{'baseOnly':>9s}{'w7Only':>8s}{'net':>6s} | stage of the baseline-only sims")
for nm, m in MASKS:
    g = m & gap
    cells = "  ".join(f"{lab}={int((g & s).sum())}" for lab, s in STAGE)
    print(f"{nm:12s}{int(g.sum()):9d}{int((m & sur).sum()):8d}{int((m & sur).sum() - g.sum()):+6d} | {cells}")

print("\n  how baseline reconstructed the baseline-only sims (tc_type):")
for nm, m in MASKS:
    g = m & gap
    if g.sum() == 0:
        continue
    t, c = np.unique(W["baseType"][g], return_counts=True)
    print(f"    {nm:12s} n={int(g.sum()):4d}  " + " ".join(f"type{int(a)}={int(b)}" for a, b in zip(t, c)))

print("\n===== Q2  dxy[10,30) PROFILE (N=468, w7 finds 10, baseline 22) =====")
band = OK & (W["dxy"] >= 10) & (W["dxy"] < 30)
for lab, s in STAGE + [("FOUND(any TC)", W["anyTC"])]:
    m = band & s if lab != "FOUND(any TC)" else band & W["anyTC"]
    if m.sum() == 0:
        continue
    print(f"  {lab:14s} n={int(m.sum()):4d}  pt med={np.median(W['pt'][m]):5.2f} "
          f"|eta| med={np.median(np.abs(W['eta'][m])):4.2f}  vxy med={np.median(W['vxy'][m]):6.2f} "
          f"dxy med={np.median(W['dxy'][m]):6.2f}  baseTC={int((m & W['baseTC']).sum()):3d}  "
          f"nTrueEdge med={np.median(f['nTrue'][m]) if m.sum() else -1:5.0f}")
    pdg, cnt = np.unique(np.abs(W["pdg"][m]), return_counts=True)
    top = np.argsort(-cnt)[:4]
    print(f"                  pdg: " + " ".join(f"|{int(pdg[i])}|={int(cnt[i])}" for i in top))

print("\n  the 51 edge-theta deaths in dxy[10,30): best true-edge logit distribution")
et = band & inF & (f["nPass"] == 0)
ml = f["maxL"][et]
print(f"    n={int(et.sum())}  med={np.median(ml):6.2f} p10={np.percentile(ml,10):6.2f} "
      f"p90={np.percentile(ml,90):6.2f} | >=-0.5 {int((ml>=-0.5).sum())}  >=-1 {int((ml>=-1).sum())}"
      f"  >=-2 {int((ml>=-2).sum())}  >=-4 {int((ml>=-4).sum())}")

print("\n===== Q3  MEASURED thetaEdge WHAT-IF (m10 caches: full re-weld + re-K9) =====")
print(f"{'band':12s}{'N':>6s}{'S3 th0':>8s}{'S3 th-1':>9s}{'S3 th-2':>9s}{'S4 th0':>8s}"
      f"{'S4 th-1':>9s}{'S4 th-2':>9s}{'dS4/N -1':>10s}{'dS4/N -2':>10s}")
for nm, m in MASKS:
    mf = m & inF
    N = int(m.sum())
    w0 = int((mf & (f["nWeld"] > 0)).sum()); w1 = int((mf & (f["nWeld_m1"] > 0)).sum())
    w2 = int((mf & (f["nWeld_m2"] > 0)).sum())
    a0 = int((mf & (f["nAcc"] > 0)).sum()); a1 = int((mf & (f["nAcc_m1"] > 0)).sum())
    a2 = int((mf & (f["nAcc_m2"] > 0)).sum())
    print(f"{nm:12s}{N:6d}{w0:8d}{w1:9d}{w2:9d}{a0:8d}{a1:9d}{a2:9d}"
          f"{(a1-a0)/N:10.4f}{(a2-a0)/N:10.4f}")

print("\n===== Q4  PURITY POOL (welded, but no chain reaches 75%) =====")
for nm, m in MASKS:
    pm = m & (f["nWeld"] > 0) & (nCap == 0)
    if pm.sum() == 0:
        continue
    print(f"  {nm:12s} n={int(pm.sum()):4d}  bestChainPurity(MD-level, m10) med="
          f"{np.median(d['bestPur'][pm]):.3f}  frac>=0.99 {(d['bestPur'][pm] >= 0.99).mean():.3f}"
          f"  nChains med={np.median(f['nCh'][pm]):3.0f}  maxNLayers med={np.median(f['maxNL'][pm]):2.0f}"
          f"  lostEntirely={int((pm & ~W['anyTC']).sum()):4d}")
