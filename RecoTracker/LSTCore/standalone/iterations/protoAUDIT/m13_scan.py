#!/usr/bin/env python3
"""m13_scan.py - exchange rates for every remaining w7 pool (READ-ONLY).

(A) GATE-KILL pool: exact threshold-relaxation scan on the verified -G 6 replica.
    For each candidate relaxation: extra fake chains admitted per event, extra
    harness-true chains, and how many currently-LOST sims gain a gate-passing
    75%-pure chain (an upper bound on the efficiency recovery -- the claim can still
    drop them, so it is an upper bound by construction).
(B) CLAIM pool: pixdrop vs arbitration split, from the m10 per-sim counters.
(C) the dxy[10,30) baseline-only sims, one row each.
"""
import numpy as np

SC = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-"
      "LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")
C = np.load(f"{SC}/m13_chains_w7.npz")
W = np.load(f"{SC}/m13_sims_w7.npy")
F = np.load(f"{SC}/m10_funnel2.npy")

KEY = W["evt"].astype(np.int64) * 100000 + W["sim"].astype(np.int64)
OK = (np.abs(W["vz"]) < 30) & (W["q"] != 0)
LOST = OK & ~W["anyTC"]
nL, dca, mP, mD, mX, sc = C["nLayers"], C["dca"], C["mP"], C["mD"], C["mX"], C["score"]
lab = C["label"] == 1
ck = C["evt"].astype(np.int64) * 100000 + C["simIdx"].astype(np.int64)
cap = lab & (C["simIdx"] >= 0)

DCA, Z = 0.5, 0.0
M4, M4D, M5, M6, MR = 1.4950, -0.5, 0.8974, -0.6054, -0.800
eT4 = (nL <= 4) & (dca >= max(DCA, Z))
iT4 = (nL <= 4) & ~eT4
i5 = (nL >= 5) & (dca < DCA)
e5 = (nL >= 5) & (dca >= DCA)


def theta(m4=M4, m4d=M4D, m5=M5, m6=M6, mr=MR):
    kill = np.zeros(len(nL), bool)
    kill |= eT4 & (mD < m4d)
    kill |= iT4 & (mX < m4)
    kill |= i5 & (mP < np.where(nL >= 6, m6, m5)) & (mX < mr)
    kill |= e5 & (mX < mr)
    thr = np.where(eT4 | e5, 0.0, -1e5)
    return (sc - np.where(kill, 1e9, 0.0)) >= thr


base = theta()
assert int(base.sum()) == 839508


def simcount(passmask, m):
    k = np.sort(ck[cap & passmask])
    n = np.searchsorted(k, KEY, "right") - np.searchsorted(k, KEY)
    return int(((n > 0) & m).sum())


ST = [("vxy<1", "vxy", 0, 1), ("vxy[1,5)", "vxy", 1, 5), ("vxy[5,10)", "vxy", 5, 10),
      ("vxy[10,30)", "vxy", 10, 30), ("dxy<1", "dxy", 0, 1), ("dxy[1,5)", "dxy", 1, 5),
      ("dxy[5,10)", "dxy", 5, 10), ("dxy[10,30)", "dxy", 10, 30)]
MASKS = [(nm, LOST & (W[v] >= lo) & (W[v] < hi)) for nm, v, lo, hi in ST]

print("===== (A) GATE-KILL RELAXATION SCAN (exact -G 6 replica; sims = UPPER BOUND) =====")
print(f"{'variant':34s}{'dFake/evt':>10s}{'dTrue/evt':>10s}" +
      "".join(f"{nm:>11s}" for nm, _ in MASKS))
base_fake = int((~lab & base).sum())
base_true = int((lab & base).sum())
b0 = [simcount(base, m) for _, m in MASKS]
print(f"{'w7 (reference)':34s}{'-':>10s}{'-':>10s}" + "".join(f"{v:11d}" for v in b0))
VAR = [
    ("-MR -1.5 (exempt-5+ / IP rescue)", dict(mr=-1.5)),
    ("-MR -2.5", dict(mr=-2.5)),
    ("-MR -4.0", dict(mr=-4.0)),
    ("-M4D -1.5 (exempt-T4 displaced)", dict(m4d=-1.5)),
    ("-M4D -2.5", dict(m4d=-2.5)),
    ("-M4 0.5 (IP-T4)", dict(m4=0.5)),
    ("-M4 -0.5", dict(m4=-0.5)),
    ("-M5 0.0 / -M6 -1.5 (IP-5+)", dict(m5=0.0, m6=-1.5)),
    ("-MR -1.5 AND -M4D -1.5", dict(mr=-1.5, m4d=-1.5)),
    ("all kills off (ceiling)", dict(m4=-1e9, m4d=-1e9, m5=-1e9, m6=-1e9, mr=-1e9)),
]
for nmv, kw in VAR:
    p = theta(**kw)
    df = (int((~lab & p).sum()) - base_fake) / 300.0
    dt = (int((lab & p).sum()) - base_true) / 300.0
    print(f"{nmv:34s}{df:10.1f}{dt:10.1f}" +
          "".join(f"{simcount(p, m) - b:+11d}" for (_, m), b in zip(MASKS, b0)))
print("  (columns = lost sims that would newly own a gate-passing 75%-pure chain)")

print("\n===== (B) CLAIM POOL: pixdrop vs arbitration (m10 per-sim counters, h4b K9) =====")
fk = F["evt"].astype(np.int64) * 100000 + F["sim"].astype(np.int64)
fo = np.argsort(fk); fks = fk[fo]
p = np.searchsorted(fks, KEY)
inF = (p < len(fks)) & (fks[np.clip(p, 0, len(fks) - 1)] == KEY)
fi = fo[np.clip(p, 0, len(fks) - 1)]
nPix = np.where(inF, F["nPixdrop"][fi], 0)
nClaim = np.where(inF, F["nClaimLost"][fi], 0)
k = np.sort(ck[cap & base])
nPass = np.searchsorted(k, KEY, "right") - np.searchsorted(k, KEY)
for nm, m in MASKS:
    pool = m & (nPass > 0)
    if pool.sum() == 0:
        print(f"  {nm:12s} claim pool = 0")
        continue
    print(f"  {nm:12s} claim pool={int(pool.sum()):4d} | covering chains pixdropped>0: "
          f"{int((pool & (nPix > 0)).sum()):4d}   claim-lost>0: {int((pool & (nClaim > 0)).sum()):4d}"
          f"   both 0: {int((pool & (nPix == 0) & (nClaim == 0)).sum()):4d}")

print("\n===== (C) the dxy[10,30) sims LST finds and w7 does not =====")
gap = OK & W["baseTC"] & ~W["anyTC"] & (W["dxy"] >= 10) & (W["dxy"] < 30)
nCapAll = np.searchsorted(np.sort(ck[cap]), KEY, "right") - np.searchsorted(np.sort(ck[cap]), KEY)
nPassEdge = np.where(inF, F["nPass"][fi], 0)
nWeld = np.where(inF, F["nWeld"][fi], 0)
maxL = np.where(inF, F["maxL"][fi], -99)
nTrue = np.where(inF, F["nTrue"][fi], 0)
print(f"  {'evt':>6s}{'sim':>5s}{'pt':>6s}{'eta':>7s}{'vxy':>8s}{'dxy':>7s}{'pdg':>6s}"
      f"{'baseType':>9s}{'nTrueE':>7s}{'nPassE':>7s}{'maxL':>7s}{'nWeld':>6s}{'nPure':>6s}"
      f"{'nGate':>6s}  stage")
for i in np.nonzero(gap)[0]:
    if not inF[i]:
        st = "not-formable"
    elif nPassEdge[i] == 0:
        st = "edge-theta"
    elif nWeld[i] == 0:
        st = "welding"
    elif nCapAll[i] == 0:
        st = "purity<75%"
    elif nPass[i] == 0:
        st = "gate-kill"
    else:
        st = "claim"
    print(f"  {W['evt'][i]:6d}{W['sim'][i]:5d}{W['pt'][i]:6.2f}{W['eta'][i]:7.2f}"
          f"{W['vxy'][i]:8.2f}{W['dxy'][i]:7.2f}{W['pdg'][i]:6d}{W['baseType'][i]:9d}"
          f"{nTrue[i]:7d}{nPassEdge[i]:7d}{maxL[i]:7.2f}{nWeld[i]:6d}{nCapAll[i]:6d}"
          f"{nPass[i]:6d}  {st}")
