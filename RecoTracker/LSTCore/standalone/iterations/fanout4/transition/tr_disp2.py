#!/usr/bin/env python3
"""Refine the band exempt-5 floor with extra predicates; count displaced SIMS at risk."""
import sys
import numpy as np
import uproot
t = uproot.open(sys.argv[1])["tree"]
a = t.arrays(["tc_pt", "tc_eta", "tc_isFake", "tc_isChain", "tc_dbgBr", "tc_dbgNL", "tc_dbgNN",
              "tc_dbgInLay", "tc_dbgNB", "tc_dbgNMD", "tc_dbgMP", "tc_dbgMD", "tc_dbgDca",
              "tc_simIdxAll", "sim_vx", "sim_vy", "sim_pca_dxy", "sim_pt", "sim_eta"], library="np")
ev_keep, ev_sim = [], []
for i in range(len(a["tc_pt"])):
    vxy = np.hypot(np.asarray(a["sim_vx"][i]), np.asarray(a["sim_vy"][i]))
    dxy = np.abs(np.asarray(a["sim_pca_dxy"][i]))
    ns = len(vxy)
    n = len(a["tc_pt"][i])
    sl = a["tc_simIdxAll"][i]
    simkey = np.full(n, -1, dtype=np.int64)   # global sim id of the first match
    mv = np.zeros(n); md = np.zeros(n)
    for k in range(n):
        s = [x for x in sl[k] if 0 <= x < ns]
        if s:
            simkey[k] = i * 100000 + s[0]
            mv[k] = vxy[s].max(); md[k] = dxy[s].max()
    ev_keep.append((np.asarray(a["tc_pt"][i]), np.asarray(a["tc_eta"][i]),
                    np.asarray(a["tc_isFake"][i]), np.asarray(a["tc_isChain"][i]),
                    np.asarray(a["tc_dbgBr"][i]), np.asarray(a["tc_dbgNL"][i]),
                    np.asarray(a["tc_dbgNN"][i]), np.asarray(a["tc_dbgInLay"][i]),
                    np.asarray(a["tc_dbgMP"][i]), np.asarray(a["tc_dbgMD"][i]),
                    np.asarray(a["tc_dbgDca"][i]), simkey, mv, md))
pt, eta, fk, ich, br, nl, nn, il, mp, mdm, dca, simkey, mvxy, mdxy = [np.concatenate(x) for x in zip(*ev_keep)]
fk = fk.astype(bool); ch = ich > 0; mX = np.maximum(mp, mdm)
pt9 = pt > 0.9
band = pt9 & ch & (np.abs(eta) >= 1.1) & (np.abs(eta) < 1.7)
b0f, b0n = fk[pt9].sum(), pt9.sum()
bandall = pt9 & (np.abs(eta) >= 1.1) & (np.abs(eta) < 1.7)
bf, bn = fk[bandall].sum(), bandall.sum()
ex5 = band & (br == 3) & (nl == 5)
ex6 = band & (br == 3) & (nl >= 6)

def uniq_sims_at_risk(kill, dsel):
    """sims whose ONLY surviving chain/carried TC would be killed."""
    victim = kill & ~fk & dsel
    vs = set(simkey[victim].tolist()) - {-1}
    surv = pt9 & ~fk & ~kill & dsel
    ss = set(simkey[surv].tolist())
    return len(vs - ss)

DS = {"vxy>=5": mvxy >= 5, "vxy>=10": mvxy >= 10, "dxy>=1": mdxy >= 1, "dxy>=5": mdxy >= 5}
print("  %-46s killF killT | bandFake globFake | sims lost: %s" % ("predicate", " ".join("%9s" % k for k in DS)))
def rep(name, kill):
    kf, kt = (kill & fk).sum(), (kill & ~fk).sum()
    los = " ".join("%9d" % uniq_sims_at_risk(kill, d) for d in DS.values())
    print("  %-46s %5d %5d | %.4f  %.4f  | %s"
          % (name, kf, kt, (bf - kf) / (bn - kill.sum()), (b0f - kf) / (b0n - kill.sum()), los))

for f in (0.0, 0.25, 0.5, 0.75):
    rep("ex5 mX<%.2f" % f, ex5 & (mX < f))
rep("ex6 mX<0.00", ex6 & (mX < 0.0))
for f in (0.0, 0.5, 1.0):
    rep("ex5 mX<%.2f & inLay==1" % f, ex5 & (mX < f) & (il == 1))
    rep("ex5 mX<%.2f & inLay>=2" % f, ex5 & (mX < f) & (il >= 2))
for f in (0.0, 0.5, 1.0):
    rep("ex5 mX<%.2f & nNodes==2" % f, ex5 & (mX < f) & (nn == 2))
    rep("ex5 mX<%.2f & nNodes>=3" % f, ex5 & (mX < f) & (nn >= 3))
for f in (0.5, 1.0, 1.5):
    rep("ex5+6 mX<%.2f & inLay>=2" % f, band & (br == 3) & (mX < f) & (il >= 2))
rep("T4-exempt all", band & (br == 1))
rep("T4-exempt inLay>=2", band & (br == 1) & (il >= 2))
for f in (0.0, 0.5):
    for g in (0.0, 0.5, 1.0):
        rep("ex5 mX<%.2f  |  ex5&inLay>=2 mX<%.2f" % (f, g),
            (ex5 & (mX < f)) | (ex5 & (il >= 2) & (mX < g)))
