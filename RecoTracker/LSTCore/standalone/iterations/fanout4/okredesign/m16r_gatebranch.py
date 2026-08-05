#!/usr/bin/env python3
"""m16r_gatebranch.py -- which gate branch/margin kills the class-(b) LOST sims.

Uses m16r_cov.npz (built by m16r_chaincov.py from the frozen welded-chain dump) for the
best >0.75-matching, theta-passing, anchor-gate-KILLED chain of each sim, and the
ablation-derived class labels from the m16r_* runs.
"""
import numpy as np

S = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-"
     "LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")
M4, M4D, MRI, MR, DCASPLIT = 3.5, -0.75, 0.5, -1.8, 0.5


def main():
    A = {t: np.load(f"{S}/m16r_sims_{t}.npy") for t in
         ["base", "thetaoff", "gateoff", "claimoff", "alloff"]}
    b = A["base"]
    cov = np.load(f"{S}/m16r_cov.npz")
    band = (np.abs(b["vz"]) < 30) & (b["q"] != 0)
    over = band & (b["vxy"] < 2.5)
    for dname, den in [("PROMPT (vxy<2.5)", over), ("ALL-VXY", band)]:
        lost = den & b["baseTC"] & ~A["base"]["anyTC"]
        form = lost & ~A["alloff"]["anyTC"]
        gate = lost & ~form & (A["gateoff"]["anyTC"] | A["thetaoff"]["anyTC"])
        print(f"\n=== {dname}: class (b) GATE N={gate.sum()} ===")
        g = np.nonzero(gate)[0]
        nl, dca = cov["kb_nl"][g], cov["kb_dca"][g]
        mP, mD, mX = cov["kb_mP"][g], cov["kb_mD"][g], cov["kb_mX"][g]
        have = nl >= 0
        print(f"  best >0.75 gate-killed chain identified for {have.sum()}/{len(g)}"
              f" (rest: killed chain is not the sim's argmax-match chain)")
        nl, dca, mP, mD, mX = nl[have], dca[have], mP[have], mD[have], mX[have]
        ip = dca < DCASPLIT
        t4 = nl <= 4
        branches = [("IP T4   (mX < 3.5)", ip & t4, mX, M4),
                    ("IP 5+   (mX < 0.5)", ip & ~t4, mX, MRI),
                    ("exempt T4 (mD < -0.75)", ~ip & t4, mD, M4D),
                    ("exempt 5+ (mX < -1.8)", ~ip & ~t4, mX, MR)]
        for lab, m, marg, thr in branches:
            if m.sum() == 0:
                print(f"   {lab:24s} N=  0")
                continue
            d = thr - marg[m]  # how far BELOW threshold
            print(f"   {lab:24s} N={m.sum():4d}  median deficit={np.median(d):6.2f}"
                  f"  within 0.5 of thr={int((d<=0.5).sum()):3d}"
                  f"  within 1.0={int((d<=1.0).sum()):3d}  within 2.0={int((d<=2.0).sum()):3d}")
        print(f"   nLayers of the killed chain: "
              f"{ {int(k): int(v) for k, v in zip(*np.unique(nl, return_counts=True))} }")


if __name__ == "__main__":
    main()
