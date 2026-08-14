#!/usr/bin/env python3
"""W5 PHASE 1: is the FAMILY PRIOR a constant of nature, or a function of local occupancy?

N3's mechanism sentence -- "an E2 row joins two triplets of the same sim .00280 of the time
against E1's .00041, 6.8x" -- was measured on the JETS corpus alone.  This asks the same question
per sample and per junction-occupancy decade, over eligible rows only (only eligible rows enter
the argmax), on the validated replay corpora.

usage: fam.py
"""
import glob
import os

import numpy as np

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
EDGES = np.array([0, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 4096, 16384, 1 << 62],
                 dtype=np.int64)
LBL = ["1", "2-3", "4-7", "8-15", "16-31", "32-63", "64-127", "128-255", "256-511",
       "512-1023", "1k-4k", "4k-16k", ">=16k"]


def scan(d, nev=None):
    fl = sorted(glob.glob(os.path.join(d, "e*.npz")),
                key=lambda s: int(os.path.basename(s)[1:-4]))
    if nev:
        fl = fl[:nev]
    nb = len(EDGES) - 1
    n = np.zeros((2, nb), np.int64)      # [fam-1, bin] eligible rows
    t = np.zeros((2, nb), np.int64)      # ... of which same-sim
    nev_ = 0
    for p in fl:
        z = np.load(p, allow_pickle=True)
        if int(z["replay_mismatch"]) != 0:
            continue
        nev_ += 1
        e = z["elig"]
        fam = z["et"][e].astype(np.int64) - 1
        dpj = (z["degIn"].astype(np.int64) * z["degOut"].astype(np.int64))[e]
        y = z["trueEdge"][e].astype(np.int64)
        b = np.searchsorted(EDGES, dpj, side="right") - 1
        np.add.at(n, (fam, b), 1)
        np.add.at(t, (fam, b), y)
        del z
    return n, t, nev_


def report(tag, n, t, nev):
    print("\n=== %s : %d events ===" % (tag, nev))
    print("%-10s %12s %10s %12s %10s %10s %10s"
          % ("degProd", "nE1", "rateE1", "nE2", "rateE2", "LR E2/E1", "E2 share"))
    for i, lbl in enumerate(LBL):
        n1, n2, t1, t2 = n[0, i], n[1, i], t[0, i], t[1, i]
        if n1 + n2 == 0:
            continue
        r1 = t1 / n1 if n1 else np.nan
        r2 = t2 / n2 if n2 else np.nan
        lr = (r2 / r1) if (r1 and r1 == r1) else np.nan
        print("%-10s %12d %10.5f %12d %10.5f %10.3f %10.3f"
              % (lbl, n1, r1, n2, r2, lr, n2 / (n1 + n2)))
    n1, n2, t1, t2 = n[0].sum(), n[1].sum(), t[0].sum(), t[1].sum()
    print("%-10s %12d %10.5f %12d %10.5f %10.3f %10.3f"
          % ("ALL", n1, t1 / n1, n2, t2 / n2, (t2 / n2) / (t1 / n1), n2 / (n1 + n2)))


if __name__ == "__main__":
    for tag, d in (("PU200 event_1000", SA + "/w5_ref/dp"), ("JETS tune", SA + "/w5_ref/dj")):
        n, t, nev = scan(d)
        report(tag, n, t, nev)
        np.savez(SA + "/w5_ref/fam_%s.npz" % os.path.basename(d), n=n, t=t, nev=nev)
