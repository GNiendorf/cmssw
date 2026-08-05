#!/usr/bin/env python3
"""M13 mission-1, part 4: is the mX axis SATURATED, or is there headroom?

The winner rule binds on the displaced bands. This script identifies, per accepted chain
TC, whether it is the SOLE >0.75 provider of an accepted sim in a given displacement band
(killing it is then a real efficiency loss in that band), and compares the gate-margin
distribution of those irreplaceable displaced chains against the surviving fakes.

Also: an ORACLE-PROTECTED sweep -- kill on mX but exempt every sole-provider of a
displaced sim -- which upper-bounds what a PERFECT displaced-protection feature could buy.

READ-ONLY. Consumes m13_mf_accepted.pkl + m13_mf_w7.root.
"""
import pickle

import numpy as np
import uproot

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/"
PTCUT, ETACUT, VZ, VPERP = 0.9, 4.5, 30.0, 2.5
DCASPLIT = 0.5
DENOM_ALL, PIX_FAKES = 573648, 13541
BASE_FR = 21620 / 475212

A = pickle.load(open(P + "m13_mf_accepted.pkl", "rb"))
o = uproot.open(P + "m13_mf_w7.root:tree")
od = o.arrays(["tc_pt", "tc_isChain", "tc_simIdxAll", "sim_pt", "sim_eta", "sim_q",
               "sim_vx", "sim_vy", "sim_vz", "sim_pca_dxy"], library="np")
nev = len(od["tc_pt"])
nCh = len(A["pt"])

BANDS = [("vxy0_1", "vxy", 0, 1), ("vxy1_5", "vxy", 1, 5), ("vxy5_10", "vxy", 5, 10),
         ("vxy10_30", "vxy", 10, 30), ("dxy0_1", "dxy", 0, 1), ("dxy1_5", "dxy", 1, 5),
         ("dxy5_10", "dxy", 5, 10), ("dxy10_30", "dxy", 10, 30)]
sole = {b[0]: np.zeros(nCh, dtype=np.int32) for b in BANDS}
sole_eta = np.zeros(nCh, dtype=np.int32)   # sole provider of ANY eta-hist-selected sim
band_den = {b[0]: 0 for b in BANDS}
ptr = 0
for i in range(nev):
    isch = np.asarray(od["tc_isChain"][i]) > 0
    nch = int(isch.sum())
    gs = slice(ptr, ptr + nch)
    ptr += nch
    nS = len(od["sim_pt"][i])
    cnt = np.zeros(nS, dtype=np.int32)
    owner = np.full(nS, -1, dtype=np.int64)
    for t, lst in enumerate(od["tc_simIdxAll"][i]):
        for s in lst:
            if 0 <= s < nS:
                cnt[s] += 1
                owner[s] = t
    spt = np.asarray(od["sim_pt"][i]); sq = np.asarray(od["sim_q"][i])
    svz = np.asarray(od["sim_vz"][i]); seta = np.asarray(od["sim_eta"][i])
    vp = np.hypot(np.asarray(od["sim_vx"][i]), np.asarray(od["sim_vy"][i]))
    adxy = np.abs(np.asarray(od["sim_pca_dxy"][i]))
    base = (sq != 0) & (spt > PTCUT)
    sel_eta = base & (np.abs(svz) < VZ) & (vp < VPERP)
    sel_v = base & (np.abs(seta) < ETACUT) & (np.abs(svz) < VZ)
    ci = np.nonzero(isch)[0]
    pos = {int(t): k for k, t in enumerate(ci)}
    for nm, var, lo, hi in BANDS:
        q = vp if var == "vxy" else adxy
        s = sel_v & (q >= lo) & (q < hi)
        band_den[nm] += int(s.sum())
        for j in np.nonzero(s & (cnt == 1))[0]:
            t = int(owner[j])
            if t in pos:
                sole[nm][gs.start + pos[t]] += 1
    for j in np.nonzero(sel_eta & (cnt == 1))[0]:
        t = int(owner[j])
        if t in pos:
            sole_eta[gs.start + pos[t]] += 1

fake = np.asarray(A["fake"]) > 0
pt = np.asarray(A["pt"])
incut = pt > PTCUT
mX = np.asarray(A["mX"])
nL = np.asarray(A["nL_d"])
dca = np.asarray(A["dca"])
br = np.where(nL <= 4, np.where(dca < DCASPLIT, 0, 1), np.where(dca < DCASPLIT, 2, 3))
brn = ["IP-T4", "exempt-T4", "IP-5+", "exempt-5+"]

print("=== IRREPLACEABLE chain TCs per displacement band (sole >0.75 provider of a band sim) ===")
print("  %-10s %7s %8s %10s | %s" % ("band", "denom", "n_sole", "of which", "mX quantiles of those sole chains (5/25/50/75)"))
for nm, var, lo, hi in BANDS:
    s = sole[nm] > 0
    if s.sum() == 0:
        continue
    print("  %-10s %7d %8d %10s | %s   branch mix %s"
          % (nm, band_den[nm], int(s.sum()), "", np.round(np.percentile(mX[s], [5, 25, 50, 75]), 2),
             {brn[b]: int((s & (br == b)).sum()) for b in range(4)}))
print("\n  surviving FAKES mX quantiles (5/25/50/75/95): %s"
      % np.round(np.percentile(mX[fake & incut], [5, 25, 50, 75, 95]), 2))
print("  ALL chain TCs      mX quantiles            : %s"
      % np.round(np.percentile(mX, [5, 25, 50, 75, 95]), 2))

disp_sole = (sole["dxy1_5"] + sole["dxy5_10"] + sole["dxy10_30"] +
             sole["vxy1_5"] + sole["vxy5_10"] + sole["vxy10_30"]) > 0
print("\n  DISPLACED sole-providers (any vxy>=1 or dxy>=1 band): %d chain TCs" % disp_sole.sum())
print("  their mX quantiles (5/25/50/75)           : %s" % np.round(np.percentile(mX[disp_sole], [5, 25, 50, 75]), 3))
print("  fraction with mX < 0.18 (the FR-parity cut): %.4f  -> %d of them would die"
      % ((mX[disp_sole] < 0.18).mean(), int((mX[disp_sole] < 0.18).sum())))
print("  fraction of surviving fakes with mX < 0.18 : %.4f  -> %d die"
      % ((mX[fake & incut] < 0.18).mean(), int((mX[fake & incut] < 0.18).sum())))
print("  separation AUC (displaced-sole vs fake) on mX: %.4f"
      % np.mean(mX[disp_sole][:, None] > mX[fake & incut][None, :]))
print("  separation AUC (ALL sole-providers vs fake)  : %.4f"
      % np.mean(mX[sole_eta > 0][:, None] > mX[fake & incut][None, :]))

print("\n=== ORACLE-PROTECTED KILL: exempt every displaced sole-provider, then cut on mX ===")
print("  (upper bound on what a PERFECT displaced-protection feature could deliver)")
prot = disp_sole
order = np.argsort(np.where(prot, 1e9, mX), kind="stable")
f = fake[order] & incut[order]
d = incut[order]
lost_eta = (sole_eta > 0)[order]
cf, cd, cl = np.cumsum(f), np.cumsum(d), np.cumsum(lost_eta)
print("  %8s %8s %9s %12s %10s" % ("killed", "fakes", "aggFR", "sims lost(eta)", "mX cut"))
for k in [5000, 10000, 20000, 30000, 40000, 50000, 60000]:
    if k >= len(order):
        break
    agg = (PIX_FAKES + int(fake[incut].sum()) - int(cf[k - 1])) / (DENOM_ALL - int(cd[k - 1]))
    print("  %8d %8d %9.4f %12d %10.3f" % (k, cf[k - 1], agg, cl[k - 1], mX[order[k - 1]]))
aggs = (PIX_FAKES + int(fake[incut].sum()) - cf) / (DENOM_ALL - cd)
hit = np.nonzero(aggs <= BASE_FR)[0]
if len(hit):
    k = int(hit[0])
    print("  >>> BASELINE FR reached at %d killed (mX %.3f): %d fakes, %d non-displaced sims lost"
          % (k + 1, mX[order[k]], cf[k], cl[k]))
print("  protected set size: %d ; fakes among them: %d (protection is truth-based, so 0 expected)"
      % (prot.sum(), int((prot & fake).sum())))

# how much of the fake population is REACHABLE below the lowest displaced sole-provider?
lo_disp = np.percentile(mX[disp_sole], 5)
print("\n  fakes below the 5th pct of displaced-sole mX (%.3f): %d = %.1f%% of fakes"
      % (lo_disp, int((mX[fake & incut] < lo_disp).sum()),
         100 * (mX[fake & incut] < lo_disp).mean()))
lo_disp1 = mX[disp_sole].min()
print("  fakes below the MINIMUM displaced-sole mX (%.3f): %d = %.1f%% of fakes"
      % (lo_disp1, int((mX[fake & incut] < lo_disp1).sum()),
         100 * (mX[fake & incut] < lo_disp1).mean()))
