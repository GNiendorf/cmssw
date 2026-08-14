#!/usr/bin/env python3
"""W5 PHASE 0b: the exchange rate of a junction-degree KNEE.

Above the knee the E2-first family order applies; at or below it the shipped key applies, so a
weld the re-key would have killed (or added) BELOW the knee is left exactly as the ship has it.
This prices, for each knee, how much of the jets-core GAIN survives against how much of the PU200
displaced LOSS is avoided.  Edge-level; the per-sim R8 statement is ab.py's.
"""
import sys

import numpy as np

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
KNEES = [0, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 4096]


def main():
    P = np.load(SA + "/w5_ref/ph0_dp.npz")
    J = np.load(SA + "/w5_ref/ph0_dj.npz")
    npe, nje = int(P["n_ev"]), int(J["n_ev"])

    # PU200 loss: true welds the re-key kills whose sim is displaced (the crown-jewel bands)
    pl_disp = P["Kt"][(P["Kband"] >= 1) | (P["Ktb"] >= 1)]
    pl_v13 = P["Kt"][P["Kband"] == 3]
    pl_d15 = P["Kt"][P["Ktb"] == 1]
    pl_all = P["Kt"]
    # jets gain: true welds the re-key adds on a jet-core sim
    jg_core = J["At"][J["Acore"]]
    jg_all = J["At"]
    jk_core = J["Kt"][J["Kcore"]]

    print("knee is on degProd = degIn*degOut at the junction (ChainGate f[14]'s summand)")
    print("%6s | %10s %10s | %10s %10s %10s %10s | %10s"
          % ("knee", "jetGAIN", "jetKILL", "puLOSSall", "puLOSSdisp", "pu vxy1030",
             "pu dxy1_5", "gain/loss"))
    for k in KNEES:
        g = (jg_core >= k).mean() if len(jg_core) else 0.0
        gk = (jk_core >= k).mean() if len(jk_core) else 0.0
        la = (pl_all >= k).mean()
        ld = (pl_disp >= k).mean()
        lv = (pl_v13 >= k).mean()
        lx = (pl_d15 >= k).mean()
        print("%6d | %10.3f %10.3f | %10.3f %10.3f %10.3f %10.3f | %10.2f"
              % (k, g, gk, la, ld, lv, lx, (g / ld) if ld > 0 else float("inf")))
    print("\ncounts: jets core true-added %d (%.1f/evt), core true-killed %d ; "
          "PU200 true-killed %d (%.1f/evt), of which displaced %d"
          % (len(jg_core), len(jg_core) / nje, len(jk_core), len(pl_all),
             len(pl_all) / npe, len(pl_disp)))
    print("jets all true-added %d ; jets events %d ; PU200 events %d" % (len(jg_all), nje, npe))


if __name__ == "__main__":
    sys.exit(main())
