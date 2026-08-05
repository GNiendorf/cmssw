#!/usr/bin/env python3
"""M10 Mission A Q1: population of fake chain TCs at the m8_h4b anchor operating point.

READ-ONLY forensics. No source files touched.
"""
import numpy as np
import uproot
import awkward as ak

PROTO = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/"
F = PROTO + "ab_m8_h4b.root"

br = ["tc_pt", "tc_eta", "tc_phi", "tc_type", "tc_isFake", "tc_isDuplicate",
      "tc_nhitOT", "tc_isChain", "run", "lumi", "evt"]
t = uproot.open(F + ":tree")
a = t.arrays(br)

pt = a["tc_pt"]; eta = a["tc_eta"]; phi = a["tc_phi"]
typ = a["tc_type"]; isF = a["tc_isFake"]; isD = a["tc_isDuplicate"]
nh = a["tc_nhitOT"]; isC = a["tc_isChain"]

incut = (pt > 0.9) & (abs(eta) < 4.5)
chain = (isC == 1)
pix = (isC == 0)

def flat(x, m):
    return ak.to_numpy(ak.flatten(x[m]))

# ---------- global counts ----------
nev = len(pt)
mC = incut & chain
mP = incut & pix
mCF = mC & (isF == 1)
mCT = mC & (isF == 0)
mPF = mP & (isF == 1)

nC = int(ak.sum(mC)); nCF = int(ak.sum(mCF)); nP = int(ak.sum(mP)); nPF = int(ak.sum(mPF))
print("=== Q1 GLOBAL (in-cut pt>0.9 |eta|<4.5, %d events) ===" % nev)
print("chain TCs  : %7d (%6.1f/evt)  fake %6d (FR %.4f)" % (nC, nC/nev, nCF, nCF/nC))
print("pixel TCs  : %7d (%6.1f/evt)  fake %6d (FR %.4f)" % (nP, nP/nev, nPF, nPF/nP))
print("all TCs    : %7d  fake %6d (FR %.4f)" % (nC+nP, nCF+nPF, (nCF+nPF)/(nC+nP)))
print("fake chain TCs are %.1f%% of ALL fake TCs" % (100.0*nCF/(nCF+nPF)))
print("fake chain TCs per event: %.1f" % (nCF/nev))

# ---------- by type ----------
print("\n--- by tc_type (4 = T5-class nLayers>=5, 9 = T4-class nLayers==4) ---")
tC = flat(typ, mC); fC = flat(isF, mC); nhC = flat(nh, mC)
ptC = flat(pt, mC); etaC = flat(eta, mC); dC = flat(isD, mC)
for tv, name in [(4, "T5-class"), (9, "T4-class")]:
    s = tC == tv
    nf = int(((fC == 1) & s).sum())
    print("  type %d %-9s: n=%6d (%5.1f%% of chain TCs, %5.1f/evt)  fake=%5d  FR=%.4f"
          " | share of all fake chains %5.1f%%"
          % (tv, name, s.sum(), 100*s.sum()/len(s), s.sum()/nev, nf, nf/max(s.sum(),1),
             100.0*nf/nCF))

# ---------- nhitOT ----------
print("\n--- nhitOT (chain TCs; nhitOT = 2 x nMD) ---")
print("  %-6s %8s %8s %8s %8s" % ("nhitOT", "nAll", "nFake", "FR", "%ofFakes"))
for v in sorted(set(nhC.tolist())):
    s = nhC == v
    nf = int(((fC == 1) & s).sum())
    print("  %-6d %8d %8d %8.4f %8.1f" % (v, s.sum(), nf, nf/max(s.sum(),1), 100.0*nf/nCF))
print("  mean nhitOT: all %.2f  true %.2f  fake %.2f"
      % (nhC.mean(), nhC[fC == 0].mean(), nhC[fC == 1].mean()))

# ---------- pt ----------
print("\n--- pt bins ---")
edges = [0.9, 1.2, 1.6, 2.2, 3.0, 5.0, 10.0, 30.0, 1e9]
print("  %-14s %8s %8s %8s %8s" % ("pt bin", "nAll", "nFake", "FR", "%ofFakes"))
for lo, hi in zip(edges[:-1], edges[1:]):
    s = (ptC >= lo) & (ptC < hi)
    nf = int(((fC == 1) & s).sum())
    print("  %-14s %8d %8d %8.4f %8.1f" % ("[%g,%g)" % (lo, hi), s.sum(), nf,
                                            nf/max(s.sum(),1), 100.0*nf/nCF))
print("  median pt: true %.2f  fake %.2f" % (np.median(ptC[fC==0]), np.median(ptC[fC==1])))

# ---------- eta regions ----------
print("\n--- eta regions (harness: barrel<1.1, transition 1.1-1.7, endcap>1.7) ---")
regions = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 4.5)]
print("  %-12s %8s %8s %8s %8s %8s %8s" % ("region", "nAll", "nFake", "FR", "%ofFakes",
                                            "FR_T5c", "FR_T4c"))
for name, lo, hi in regions:
    s = (abs(etaC) >= lo) & (abs(etaC) < hi)
    nf = int(((fC == 1) & s).sum())
    s5 = s & (tC == 4); s4 = s & (tC == 9)
    fr5 = ((fC==1)&s5).sum()/max(s5.sum(),1); fr4 = ((fC==1)&s4).sum()/max(s4.sum(),1)
    print("  %-12s %8d %8d %8.4f %8.1f %8.4f %8.4f" % (name, s.sum(), nf,
          nf/max(s.sum(),1), 100.0*nf/nCF, fr5, fr4))

# finer eta
print("\n--- |eta| fine bins ---")
fe = [0, 0.4, 0.8, 1.1, 1.4, 1.7, 2.0, 2.4, 3.0, 4.5]
for lo, hi in zip(fe[:-1], fe[1:]):
    s = (abs(etaC) >= lo) & (abs(etaC) < hi)
    nf = int(((fC == 1) & s).sum())
    print("  |eta| [%.1f,%.1f): n=%6d fake=%5d FR=%.4f  (%4.1f%% of fakes)"
          % (lo, hi, s.sum(), nf, nf/max(s.sum(),1), 100.0*nf/nCF))

# ---------- 2D: type x nLayers-class x region ----------
print("\n--- fake counts: type x eta region ---")
for tv, name in [(4, "T5c"), (9, "T4c")]:
    row = []
    for rn, lo, hi in regions:
        s = (tC == tv) & (abs(etaC) >= lo) & (abs(etaC) < hi)
        nf = int(((fC == 1) & s).sum())
        row.append("%s n=%d f=%d FR=%.3f" % (rn, s.sum(), nf, nf/max(s.sum(),1)))
    print("  %s: %s" % (name, " | ".join(row)))

# ---------- jettiness: nearby TC density ----------
print("\n--- jettiness: nearby-TC density (all TCs in event, in-cut, dR windows) ---")
etaAll = ak.to_list(eta[incut]); phiAll = ak.to_list(phi[incut])
isFAll = ak.to_list(isF[incut]); isCAll = ak.to_list(isC[incut])
tAll = ak.to_list(typ[incut])

dens = {0.1: [], 0.3: [], 0.5: []}
lab = []   # 0 true chain, 1 fake chain, 2 pixel
for ie in range(nev):
    e = np.asarray(etaAll[ie], dtype=float); p = np.asarray(phiAll[ie], dtype=float)
    if len(e) == 0:
        continue
    de = e[:, None] - e[None, :]
    dp = p[:, None] - p[None, :]
    dp = (dp + np.pi) % (2*np.pi) - np.pi
    dr2 = de*de + dp*dp
    np.fill_diagonal(dr2, 1e9)
    for r in dens:
        dens[r].append((dr2 < r*r).sum(axis=1))
    f = np.asarray(isFAll[ie]); c = np.asarray(isCAll[ie])
    l = np.where(c == 0, 2, np.where(f == 1, 1, 0))
    lab.append(l)
lab = np.concatenate(lab)
for r in dens:
    dens[r] = np.concatenate(dens[r])

print("  %-14s %8s %10s %10s %10s %10s" % ("class", "n", "mean_dR.1", "med_dR.1",
                                            "mean_dR.3", "mean_dR.5"))
for lv, name in [(0, "chain TRUE"), (1, "chain FAKE"), (2, "pixel TC")]:
    s = lab == lv
    print("  %-14s %8d %10.2f %10.1f %10.2f %10.2f"
          % (name, s.sum(), dens[0.1][s].mean(), np.median(dens[0.1][s]),
             dens[0.3][s].mean(), dens[0.5][s].mean()))

# fraction of fakes in "jetty" environments, using true-chain distribution quantiles
q90 = np.quantile(dens[0.3][lab == 0], 0.90)
q99 = np.quantile(dens[0.3][lab == 0], 0.99)
print("  true-chain dR<0.3 density p90=%.0f p99=%.0f" % (q90, q99))
for lv, name in [(0, "chain TRUE"), (1, "chain FAKE")]:
    s = lab == lv
    print("  %-12s frac above true-p90: %.3f   above true-p99: %.3f"
          % (name, (dens[0.3][s] > q90).mean(), (dens[0.3][s] > q99).mean()))

# per-event FR vs event TC multiplicity (jetty events)
print("\n--- per-event chain FR vs event chain multiplicity (jet-event proxy) ---")
nCe = ak.to_numpy(ak.sum(mC, axis=1)).astype(float)
nCFe = ak.to_numpy(ak.sum(mCF, axis=1)).astype(float)
order = np.argsort(nCe)
qs = np.array_split(order, 5)
for i, q in enumerate(qs):
    print("  quintile %d: <nChainTC>=%6.0f  FR=%.4f" % (i, nCe[q].mean(),
                                                        nCFe[q].sum()/nCe[q].sum()))
