#!/usr/bin/env python3
"""m10_report.py - funnel tables from the m10 caches (READ-ONLY)."""
import numpy as np

SCRATCH = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad"

S = np.load(f"{SCRATCH}/m10_sims.npy")          # all selected sims + TC outcome
F = np.load(f"{SCRATCH}/m10_funnel2.npy")       # formable sims + chain funnel
W = np.load(f"{SCRATCH}/m10_weldfail.npy")
E = np.load(f"{SCRATCH}/m10_evt2.npy")

STRATA = [("prompt vxy<1", 0.0, 1.0), ("vxy[1,5)", 1.0, 5.0), ("vxy[5,10)", 5.0, 10.0),
          ("vxy[10,30)", 10.0, 30.0), ("vxy>=30", 30.0, 1e9)]

# join key
def key(a):
    return (a["lumi"].astype(np.int64) << 40) + (a["evt"].astype(np.int64) << 16) + a["sim"].astype(np.int64)

ks, kf = key(S), key(F)
o = np.argsort(ks)
ks_s = ks[o]
pos = np.searchsorted(ks_s, kf)
hit = (pos < len(ks_s)) & (ks_s[np.minimum(pos, len(ks_s) - 1)] == kf)
sidx = o[np.clip(pos, 0, len(ks_s) - 1)]
chainTC = np.where(hit, S["chainTC"][sidx], False)
anyTC = np.where(hit, S["anyTC"][sidx], False)
pixTC = np.where(hit, S["pixTC"][sidx], False)
baseOT = np.where(hit, S["baseOT"][sidx], False)
baseTC = np.where(hit, S["baseTC"][sidx], False)
print(f"formable sims joined to TC outcome: {hit.sum()}/{len(F)}")

print("\n================ Q1  STAGE-LOSS FUNNEL (300 PU200RelVal evts) ================")
hdr = f"{'stage':38s}" + "".join(f"{n:>16s}" for n, _, _ in STRATA)
print(hdr)
rows = []
for name, lo, hi in STRATA:
    ms = (S["vxy"] >= lo) & (S["vxy"] < hi)
    mf = (F["vxy"] >= lo) & (F["vxy"] < hi)
    n0 = int(ms.sum())
    n1 = int(mf.sum())
    n2 = int((mf & (F["nPass"] > 0)).sum())
    n3 = int((mf & (F["nWeld"] > 0)).sum())
    n4 = int((mf & (F["nAcc"] > 0)).sum())
    n5 = int((mf & chainTC).sum())
    n5b = int((ms & S["chainTC"]).sum())
    rows.append((name, n0, n1, n2, n3, n4, n5, n5b,
                 int((ms & S["baseOT"]).sum()), int((ms & S["baseTC"]).sum()),
                 int((ms & S["anyTC"]).sum())))
for lab, i in [("S0 accepted sim pt>0.9 |eta|<4.5", 1), ("S1 formable (>=1 true edge)", 2),
               ("S2 >=1 true edge passes theta=0", 3), ("S3 >=1 true edge WELDED", 4),
               ("S4 >=1 covering chain ACCEPTED(K9)", 5), ("S5 chain-TC matched (75% hits)", 6)]:
    print(f"{lab:38s}" + "".join(f"{r[i]:16d}" for r in rows))
print(f"{'-- LST baseline: T5/T4-type TC':38s}" + "".join(f"{r[8]:16d}" for r in rows))
print(f"{'-- LST baseline: any TC':38s}" + "".join(f"{r[9]:16d}" for r in rows))
print(f"{'-- hybrid: any TC':38s}" + "".join(f"{r[10]:16d}" for r in rows))

print("\nSTAGE LOSS (absolute sims lost at each step, and % of formable):")
print(f"{'loss':38s}" + "".join(f"{n:>16s}" for n, _, _ in STRATA))
for lab, i, j in [("L1 not formable (no true edge)", 1, 2), ("L2 all true edges FAIL theta=0", 2, 3),
                  ("L3 passing true edges NOT WELDED", 3, 4), ("L4 chain formed, K9 REJECTED", 4, 5),
                  ("L5 accepted but TC-match failed", 5, 6)]:
    cells = []
    for r in rows:
        d = r[i] - r[j]
        cells.append(f"{d:7d} ({100.0*d/max(r[2],1):5.1f}%)")
    print(f"{lab:38s}" + "".join(f"{c:>16s}" for c in cells))

print("\n================ Q2  TRUE-EDGE LOGITS OF SIMS DYING AT theta=0 =============")
died2 = (F["nPass"] == 0)
for name, lo, hi in STRATA[:4]:
    mf = (F["vxy"] >= lo) & (F["vxy"] < hi) & died2
    n = int(mf.sum())
    if n == 0:
        continue
    ml = F["maxL"][mf]
    q = np.percentile(ml, [10, 25, 50, 75, 90])
    print(f"  {name:12s} N={n:5d}  best true-edge logit: p10={q[0]:6.2f} p25={q[1]:6.2f} "
          f"med={q[2]:6.2f} p75={q[3]:6.2f} p90={q[4]:6.2f}   "
          f">=-1: {(ml>=-1).mean():.3f}  >=-2: {(ml>=-2).mean():.3f}  "
          f">=-4: {(ml>=-4).mean():.3f}")
    print(f"                 nTrueEdges/sim med={np.median(F['nTrue'][mf]):.0f}  "
          f"pt med={np.median(F['pt'][mf]):.2f}")

print("\n  MEASURED recovery by re-welding at looser thetaEdge (full pipeline re-run):")
print(f"  {'stratum':14s}{'formable':>9s}{'S3 th=0':>9s}{'S3 th=-1':>9s}{'S3 th=-2':>9s}"
      f"{'S4 th=0':>9s}{'S4 th=-1':>9s}{'S4 th=-2':>9s}")
for name, lo, hi in STRATA[:4]:
    mf = (F["vxy"] >= lo) & (F["vxy"] < hi)
    print(f"  {name:14s}{int(mf.sum()):9d}{int((mf&(F['nWeld']>0)).sum()):9d}"
          f"{int((mf&(F['nWeld_m1']>0)).sum()):9d}{int((mf&(F['nWeld_m2']>0)).sum()):9d}"
          f"{int((mf&(F['nAcc']>0)).sum()):9d}{int((mf&(F['nAcc_m1']>0)).sum()):9d}"
          f"{int((mf&(F['nAcc_m2']>0)).sum()):9d}")
print(f"  per-event chains {E['nChains'].sum()/300:.0f} -> th=-1 {E['nChains_m1'].sum()/300:.0f}"
      f" -> th=-2 {E['nChains_m2'].sum()/300:.0f}; accepted {E['nAcc'].sum()/300:.0f} -> "
      f"{E['nAcc_m1'].sum()/300:.0f} -> {E['nAcc_m2'].sum()/300:.0f}")

print("\n================ Q3  WELD-FAILURE MODES (displaced, passing true edges) =====")
print(f"  displaced sims (vxy>=1) with >=1 passing true edge and ZERO welded: "
      f"{len(np.unique(W['lumi'].astype(np.int64)*10**9+W['evt']*1000+W['sim']))}")
if len(W):
    both = (W["outTaken"] == 1) & (W["inTaken"] == 1)
    onlyo = (W["outTaken"] == 1) & (W["inTaken"] == 0)
    onlyi = (W["outTaken"] == 0) & (W["inTaken"] == 1)
    none = (W["outTaken"] == 0) & (W["inTaken"] == 0)
    n = len(W)
    print(f"  unwelded passing true edges: {n}")
    print(f"    both endpoints welded elsewhere (fragmentation) : {both.sum():5d} ({100*both.mean():.1f}%)")
    print(f"    only tail out-slot lost                          : {onlyo.sum():5d} ({100*onlyo.mean():.1f}%)")
    print(f"    only head in-slot lost                           : {onlyi.sum():5d} ({100*onlyi.mean():.1f}%)")
    print(f"    both slots FREE (mutual-best starvation)         : {none.sum():5d} ({100*none.mean():.1f}%)")
    comp = np.concatenate([W["outCompL"][W["outTaken"] == 1], W["inCompL"][W["inTaken"] == 1]])
    compT = np.concatenate([W["outCompTrue"][W["outTaken"] == 1], W["inCompTrue"][W["inTaken"] == 1]])
    if len(comp):
        print(f"    competing (winning) edges: n={len(comp)}  median logit={np.median(comp):.2f} "
              f"vs lost edge median {np.median(W['logit']):.2f}")
        print(f"    competitor is a TRUE edge (of some accepted sim): {compT.mean():.3f} "
              f"-> FAKE competitor {1-compT.mean():.3f}")

print("\n================ Q4  ACCEPTANCE LOSS BREAKDOWN =============================")
print(f"  {'stratum':14s}{'S3':>7s}{'S4':>7s}{'lost':>7s}{'thetaKill':>11s}{'pixdrop':>9s}"
      f"{'claim':>8s}{'T4cls':>8s}")
for name, lo, hi in STRATA[:4]:
    mf = (F["vxy"] >= lo) & (F["vxy"] < hi) & (F["nWeld"] > 0)
    lost = mf & (F["nAcc"] == 0)
    # attribute by what happened to the sim's covering chains
    tk = lost & (F["nThetaKill"] > 0) & (F["nPixdrop"] == 0) & (F["nClaimLost"] == 0)
    pd = lost & (F["nPixdrop"] > 0)
    cl = lost & (F["nClaimLost"] > 0) & (F["nPixdrop"] == 0)
    t4 = lost & (F["nT4cls"] > 0)
    print(f"  {name:14s}{int(mf.sum()):7d}{int((mf&(F['nAcc']>0)).sum()):7d}{int(lost.sum()):7d}"
          f"{int(tk.sum()):11d}{int(pd.sum()):9d}{int(cl.sum()):8d}{int(t4.sum()):8d}")

print("\n  S4->S5 (accepted chain exists but harness did not 75%-match it):")
for name, lo, hi in STRATA[:4]:
    mf = (F["vxy"] >= lo) & (F["vxy"] < hi) & (F["nAcc"] > 0)
    lost = mf & (~chainTC)
    print(f"  {name:14s} S4={int(mf.sum()):5d} lost={int(lost.sum()):5d} "
          f"({100*lost.sum()/max(mf.sum(),1):.1f}%)  of which pixel-TC-covered "
          f"{int((lost&pixTC).sum()):4d}  maxNLayers med={np.median(F['maxNL'][lost]) if lost.sum() else -1:.0f}")
