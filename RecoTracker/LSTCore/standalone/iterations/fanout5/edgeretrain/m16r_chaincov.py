#!/usr/bin/env python3
"""m16r_chaincov.py -- chain-universe coverage of the ctl_noatt LOST sims (READ-ONLY).

Inputs (pre-existing, unmodified):
  <scratch>/m16r_sims_ctl_noatt.npy   per-sim outcome table (m16r_sims.py)
  <scratch>/m13_chains_w7.npz         EVERY welded chain of the 300-evt sample with its
                                      best-sim index / matchFrac / nLayers / dcaXY /
                                      3-class margins mP,mD,mX / thetaPass / exemptMask.
                                      The weld universe is config-independent (-e 0
                                      -L 0.5, kWeldSweeps 3, edge net v3) -> valid for the
                                      m16 anchor shape too; only the GATE thresholds are
                                      re-applied here (anchor values, not w7's).

Anchor gate (-G 6, from ab_ctl_noatt.log):
  IP  (dca <  0.5): nL<=4 killed iff mX < 3.5
                    nL>=5 killed iff (mP < 1e9) AND NOT (mX >= 0.5)   -> mX < 0.5
  exempt(dca>=0.5): nL<=4 killed iff mD < -0.75
                    nL>=5 killed iff (mD < 1e9) AND NOT (mX >= -1.8)  -> mX < -1.8
  C1 cell (nNodes==2, nLayers==5): additionally killed iff mP < 2 AND mD < -2
     (nNodes is not in the dump -> this extra kill is NOT applied; it can only make the
      "gate-passing" set here an upper bound, which is the conservative direction.)
"""
import numpy as np

SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-"
           "LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")

DCASPLIT = 0.5
M4, M4D, MRI, MR = 3.5, -0.75, 0.5, -1.8


def gate_kill(nL, dca, mP, mD, mX):
    ip = dca < DCASPLIT
    t4 = nL <= 4
    k = np.zeros(len(nL), dtype=bool)
    k |= ip & t4 & (mX < M4)
    k |= (~ip) & t4 & (mD < M4D)
    k |= ip & (~t4) & (mX < MRI)
    k |= (~ip) & (~t4) & (mX < MR)
    return k


def main():
    a = np.load(f"{SCRATCH}/m16r_sims_ctl_noatt.npy")
    C = np.load(f"{SCRATCH}/m13_chains_w7.npz")

    band = (np.abs(a["vz"]) < 30) & (a["q"] != 0)
    over = band & (a["vxy"] < 2.5)
    lost = over & a["baseTC"] & ~a["anyTC"]
    print(f"LOST(overall/prompt) = {lost.sum()}   LOST(all vxy) = {(band & a['baseTC'] & ~a['anyTC']).sum()}")

    kill = gate_kill(C["nLayers"], C["dca"], C["mP"], C["mD"], C["mX"])
    alive = C["thetaPass"] & ~kill
    print(f"chain universe {len(kill)}: thetaPass={C['thetaPass'].sum()} "
          f"anchor-gate-alive={alive.sum()}  (w7 kill flag alive={ (C['thetaPass']&~C['kill']).sum() })")

    ck = C["evt"].astype(np.int64) * 100000 + C["simIdx"].astype(np.int64)
    has = C["simIdx"] >= 0
    # per-sim aggregates over the chains whose BEST sim is that sim
    order = np.argsort(ck[has], kind="stable")
    k_s = ck[has][order]
    mf_s = C["matchFrac"][has][order]
    tp_s = C["thetaPass"][has][order]
    al_s = alive[has][order]
    nl_s = C["nLayers"][has][order]
    dc_s = C["dca"][has][order]
    mP_s, mD_s, mX_s = (C[x][has][order] for x in ("mP", "mD", "mX"))
    uk, ustart = np.unique(k_s, return_index=True)
    uend = np.append(ustart[1:], len(k_s))

    simkey = a["evt"].astype(np.int64) * 100000 + a["sim"].astype(np.int64)
    idx = np.searchsorted(uk, simkey)
    idx[idx >= len(uk)] = 0
    found = uk[idx] == simkey

    n = len(a)
    nch = np.zeros(n, np.int32)           # chains whose best sim is this sim
    best_all = np.zeros(n, np.float32)    # best matchFrac, any chain
    best_theta = np.zeros(n, np.float32)  # best matchFrac among theta-passing chains
    best_alive = np.zeros(n, np.float32)  # best matchFrac among theta+gate survivors
    n75 = np.zeros(n, np.int32)           # chains with matchFrac > 0.75
    n75_theta = np.zeros(n, np.int32)
    n75_alive = np.zeros(n, np.int32)
    # for the best >0.75 chain that is theta-passing but gate-killed: margins/branch
    kb_nl = np.full(n, -1, np.int32)
    kb_dca = np.full(n, -1.0, np.float32)
    kb_mP = np.full(n, np.nan, np.float32)
    kb_mD = np.full(n, np.nan, np.float32)
    kb_mX = np.full(n, np.nan, np.float32)
    # best sub-threshold (<=0.75) chain that survives theta+gate: the (d) rematch candidate
    sb_frac = np.zeros(n, np.float32)
    sb_nl = np.full(n, -1, np.int32)

    for i in np.nonzero(found)[0]:
        s, e = ustart[idx[i]], uend[idx[i]]
        mf, tp, al, nl, dc = mf_s[s:e], tp_s[s:e], al_s[s:e], nl_s[s:e], dc_s[s:e]
        nch[i] = e - s
        best_all[i] = mf.max()
        if tp.any():
            best_theta[i] = mf[tp].max()
        if al.any():
            best_alive[i] = mf[al].max()
        g = mf > 0.75
        n75[i] = g.sum()
        n75_theta[i] = (g & tp).sum()
        n75_alive[i] = (g & al).sum()
        gk = g & tp & ~al
        if gk.any():
            j = s + np.nonzero(gk)[0][np.argmax(mf[gk])]
            kb_nl[i], kb_dca[i] = nl_s[j], dc_s[j]
            kb_mP[i], kb_mD[i], kb_mX[i] = mP_s[j], mD_s[j], mX_s[j]
        sub = (~g) & al
        if sub.any():
            j = s + np.nonzero(sub)[0][np.argmax(mf[sub])]
            sb_frac[i], sb_nl[i] = mf_s[j], nl_s[j]

    np.savez(f"{SCRATCH}/m16r_cov.npz", found=found, nch=nch, best_all=best_all,
             best_theta=best_theta, best_alive=best_alive, n75=n75, n75_theta=n75_theta,
             n75_alive=n75_alive, kb_nl=kb_nl, kb_dca=kb_dca, kb_mP=kb_mP, kb_mD=kb_mD,
             kb_mX=kb_mX, sb_frac=sb_frac, sb_nl=sb_nl, lost=lost, over=over, band=band)

    for name, sel in [("LOST prompt(vxy<2.5)", lost),
                      ("LOST all-vxy", band & a["baseTC"] & ~a["anyTC"])]:
        L = np.nonzero(sel)[0]
        print(f"\n=== {name}: N={len(L)} ===")
        f_no = ~found[L]
        print(f"  no welded chain has this sim as its best match : {f_no.sum()}")
        b = best_all[L]
        print(f"  best matchFrac of any welded chain:  <=0.5 {(b<=0.5).sum():4d}"
              f" | (0.5,0.75] {((b>0.5)&(b<=0.75)).sum():4d} | >0.75 {(b>0.75).sum():4d}")
        cov = n75[L] > 0
        print(f"  A) FORMATION  (no chain >0.75)                : {(~cov).sum():4d}")
        th = cov & (n75_theta[L] > 0)
        print(f"  A') theta-gate kill (chain >0.75 but no thetaPass): {(cov & ~th).sum():4d}")
        al = th & (n75_alive[L] > 0)
        print(f"  B) 3-CLASS GATE kill (thetaPass >0.75, none alive): {(th & ~al).sum():4d}")
        print(f"  C) gate-surviving >0.75 chain exists -> CLAIM/order : {al.sum():4d}")
        # (d) diagnostic: among the FORMATION bucket, how many have a gate-alive chain
        #     whose match is just under threshold
        d = (~cov) & (sb_frac[L] > 0.5)
        print(f"  (d)-candidate: no >0.75 chain, but a gate-ALIVE chain with"
              f" matchFrac in (0.5,0.75]: {d.sum():4d}")


if __name__ == "__main__":
    main()
