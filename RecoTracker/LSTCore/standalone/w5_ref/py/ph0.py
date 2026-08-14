#!/usr/bin/env python3
"""W5 PHASE 0: does the E2-first re-key's PU200 VICTIM population separate from its jets-core
BENEFICIARY population on LOCAL OCCUPANCY?

The re-key is replayed exactly (wr.py validates the BASE replay against the kernel's own welded
set, per event, zero mismatches).  A weld is
    KILLED  if it is welded under the shipped key and not under E2-first,
    ADDED   if it is welded under E2-first and not under the shipped key.
The occupancy observable is the junction incidence degree product degIn*degOut -- the per-edge
summand of ChainGate feature 14, and the only local-density quantity the weld itself can read.

usage: ph0.py <dp dir> <dj dir>
"""
import glob
import os
import sys

import numpy as np

QS = [1, 5, 10, 25, 50, 75, 90, 95, 99]


def load(d):
    out = []
    for p in sorted(glob.glob(os.path.join(d, "e*.npz")),
                    key=lambda s: int(os.path.basename(s)[1:-4])):
        z = np.load(p, allow_pickle=True)
        if int(z["replay_mismatch"]) != 0:
            print("SKIP %s replay mismatch %d" % (p, int(z["replay_mismatch"])))
            continue
        out.append(z)
    return out


def stats(name, v, n_ev):
    if len(v) == 0:
        print("  %-38s n=0" % name)
        return
    q = np.percentile(v, QS)
    print("  %-38s n=%7d (%7.1f/evt)  " % (name, len(v), len(v) / n_ev)
          + " ".join("%s:%.0f" % (a, b) for a, b in zip(QS, q))
          + "  mean %.1f" % v.mean())


def main(dp, dj):
    for tag, d in (("PU200 event_1000", dp), ("JETS tune", dj)):
        Z = load(d)
        n_ev = len(Z)
        print("\n=== %s : %d events ===" % (tag, n_ev))
        if not n_ev:
            continue
        rt = max(float(z["roundtrip"]) for z in Z)
        print("head replay max |dmX| = %.2e ; BASE weld replay mismatches = %d over %d edges"
              % (rt, sum(int(z["replay_mismatch"]) for z in Z),
                 sum(int(z["nE"]) for z in Z)))

        K, A, S = [], [], []          # killed / added / surviving-base, degProd
        Kt, At, St = [], [], []       # ... restricted to TRUE (same-sim) edges
        Kfam, Afam = [], []
        Kband, Aband = [], []         # sim vxy band code of the true edge's sim
        Ktb, Atb = [], []             # sim dxy band code
        Kcore, Acore = [], []
        nb = np.zeros(4, np.int64)
        for z in Z:
            base, e2 = z["wBase"], z["wE2"]
            dp_ = (z["degIn"].astype(np.int64) * z["degOut"].astype(np.int64))
            killed = base & ~e2
            added = e2 & ~base
            surv = base & e2
            tr = z["trueEdge"]
            et = z["et"]
            esim = z["esim"].astype(np.int64)
            vxy, dxy = z["sim_vxy"], z["sim_dxy"]
            core = z["sim_core"]

            def bandcode(q):
                return np.digitize(q, [1.0, 5.0, 10.0, 30.0])

            for m, L, Lt, Lf, Lb, Ltb, Lc in ((killed, K, Kt, Kfam, Kband, Ktb, Kcore),
                                              (added, A, At, Afam, Aband, Atb, Acore)):
                L.append(dp_[m])
                mt = m & tr
                Lt.append(dp_[mt])
                Lf.append(et[mt])
                s = esim[mt]
                Lb.append(bandcode(vxy[s]))
                Ltb.append(bandcode(dxy[s]))
                Lc.append(core[s] if len(core) else np.zeros(len(s), bool))
            S.append(dp_[surv])
            St.append(dp_[surv & tr])

        K, A, S = np.concatenate(K), np.concatenate(A), np.concatenate(S)
        Kt, At, St = np.concatenate(Kt), np.concatenate(At), np.concatenate(St)
        Kfam, Afam = np.concatenate(Kfam), np.concatenate(Afam)
        Kband, Aband = np.concatenate(Kband), np.concatenate(Aband)
        Ktb, Atb = np.concatenate(Ktb), np.concatenate(Atb)
        Kcore, Acore = np.concatenate(Kcore), np.concatenate(Acore)

        print("\n welds/evt: base %.1f  e2first %.1f   killed %.1f  added %.1f  surviving %.1f"
              % ((len(K) + len(S)) / n_ev, (len(A) + len(S)) / n_ev,
                 len(K) / n_ev, len(A) / n_ev, len(S) / n_ev))
        print(" TRUE (same-sim) welds/evt: base %.2f  killed %.2f  added %.2f"
              % ((len(Kt) + len(St)) / n_ev, len(Kt) / n_ev, len(At) / n_ev))
        print(" killed TRUE by family: E1 %d  E2 %d   |  added TRUE: E1 %d  E2 %d"
              % ((Kfam == 1).sum(), (Kfam == 2).sum(), (Afam == 1).sum(), (Afam == 2).sum()))

        print("\n degProd = degIn*degOut percentiles %s" % QS)
        stats("ALL killed welds", K, n_ev)
        stats("ALL added welds", A, n_ev)
        stats("ALL surviving welds", S, n_ev)
        stats("TRUE killed", Kt, n_ev)
        stats("TRUE added", At, n_ev)
        stats("TRUE surviving", St, n_ev)
        for b, nm in ((0, "vxy[0,1)"), (1, "vxy[1,5)"), (2, "vxy[5,10)"), (3, "vxy[10,30)")):
            stats("TRUE killed, %s" % nm, Kt[Kband == b], n_ev)
        for b, nm in ((1, "dxy[1,5)"), (2, "dxy[5,10)"), (3, "dxy[10,30)")):
            stats("TRUE killed, %s" % nm, Kt[Ktb == b], n_ev)
        if Kcore.any() or Acore.any():
            stats("TRUE killed, jet-CORE sim", Kt[Kcore], n_ev)
            stats("TRUE added,  jet-CORE sim", At[Acore], n_ev)

        np.savez_compressed(os.path.join(os.path.dirname(dp.rstrip("/")),
                                         "ph0_%s.npz" % os.path.basename(d.rstrip("/"))),
                            K=K, A=A, S=S, Kt=Kt, At=At, St=St, Kfam=Kfam, Afam=Afam,
                            Kband=Kband, Aband=Aband, Ktb=Ktb, Atb=Atb,
                            Kcore=Kcore, Acore=Acore, n_ev=n_ev)
    print("\nsaved ph0_dp.npz / ph0_dj.npz next to the dump dirs")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
