#!/usr/bin/env python3
"""Turn dump-build outputs (otpullDiag/ev trees of DiagLST OTPullDiagAnalyzer, dump.patch) into training rows.

One row per dumped (T3, cell) pair = every pair that reached the network (inside the score windows, joint-rms
prefilter passed). Label: the T3 (>= 5 of its 6 OT hits) and the cell (both pixel hits) belong to the same simulated
particle (ClusterTPAssociation; in-time pileup counts). The truth particle's production radius and pT go with true rows.
  python3 make_rows.py OUT.npz dump/ttbar/*/diag.root   (a prov.txt next to each diag.root is copied into the rows)
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import REC_CELL, REC_CHI2, REC_COST, REC_GATE1, REC_ID, REC_LOGIT, REC_N, REC_X0, NIN  # noqa: E402


def main():
    import uproot
    out, files = sys.argv[1], sys.argv[2:]
    cols = {k: [] for k in ("X", "y", "w", "ev", "tpvxy", "tppt", "tpsig", "chi2", "gate1", "logit", "t3", "cell", "cost")}
    evkeys, prov = [], []
    iev = 0
    for f in files:
        pv = os.path.join(os.path.dirname(f), "prov.txt")
        prov.append(f"{f}: " + (open(pv).read().strip() if os.path.exists(pv) else "no prov.txt"))
        uf = uproot.open(f)
        t = uf["otpullPairDump/ev"] if "otpullPairDump" in uf else uf["otpullDiag/ev"]
        a = t.arrays(["run", "lumi", "event", "tp_key", "tp_pt", "tp_vx", "tp_vy", "tp_sig", "pair_f", "pair_ctp",
                      "pair_otp", "pair_onm", "pair_w"], library="np")
        for i in range(t.num_entries):
            pf = a["pair_f"][i].reshape(-1, REC_N)
            n = len(pf)
            evkeys.append((int(a["run"][i]), int(a["lumi"][i]), int(a["event"][i])))
            if n:
                ctp, otp, onm = a["pair_ctp"][i], a["pair_otp"][i], a["pair_onm"][i]
                lab = (ctp >= 0) & (ctp == otp) & (onm >= 5)
                key = a["tp_key"][i]
                order = np.argsort(key)
                pos = np.searchsorted(key[order], np.where(lab, ctp, -1))
                pos = np.clip(pos, 0, max(len(key) - 1, 0))
                j = order[pos] if len(key) else np.zeros(n, int)
                has = lab & (len(key) > 0) & (key[j] == ctp) if len(key) else np.zeros(n, bool)
                vxy = np.where(has, np.hypot(a["tp_vx"][i][j], a["tp_vy"][i][j]) if len(key) else 0, -1.0)
                cols["X"].append(pf[:, REC_X0:REC_X0 + NIN].astype(np.float32))
                cols["y"].append(lab.astype(np.int8))
                cols["w"].append(a["pair_w"][i].astype(np.float32))
                cols["ev"].append(np.full(n, iev, np.int64))
                cols["tpvxy"].append(vxy.astype(np.float32))
                cols["tppt"].append(np.where(has, a["tp_pt"][i][j] if len(key) else 0, 0.0).astype(np.float32))
                cols["tpsig"].append(np.where(has, a["tp_sig"][i][j] if len(key) else 0, 0).astype(np.int8))
                cols["chi2"].append(pf[:, REC_CHI2].astype(np.float32))
                cols["gate1"].append(pf[:, REC_GATE1].astype(np.int8))
                cols["logit"].append(pf[:, REC_LOGIT].astype(np.float32))
                cols["t3"].append(pf[:, REC_ID].astype(np.int32))
                cols["cell"].append(pf[:, REC_CELL].astype(np.int32))
                cols["cost"].append(pf[:, REC_COST].astype(np.float32))
            iev += 1
    res = {k: np.concatenate(v) if v else np.zeros(0) for k, v in cols.items()}
    ek = np.array(evkeys, np.int64).reshape(-1, 3)
    np.savez(out, **res, nev=np.int64(iev), events=ek, prov=np.array("\n".join(prov)))
    print(f"{out}: {iev} events, {len(res['y'])} rows, {int(res['y'].sum())} true")


if __name__ == "__main__":
    main()
