#!/usr/bin/env python3
"""Per-event counts from an LST output ntuple (sim pT > 0.9 always).
Efficiency denominator: charged sims, |eta| < 2.4, |vz| < 30 cm, pT > 0.9, split by vxy [cm]; numerator: sim_tcIdx >= 0.
TCs: pT > 0.9, |eta| < 2.4; fake = tc_isFake, dup = !fake && tc_isDuplicate; also per tc_type.
Per event the raw counts are stored (keyed by a sim fingerprint) so arms can be paired event by event.
usage: summarize.py <ntuple.root> <out.json>"""
import json
import sys

import numpy as np
import uproot

VXY = [0.0, 1.0, 5.0, 25.0, 50.0]
TYPES = {4: "T5", 5: "pT3", 7: "pT5", 8: "pLS", 9: "T4"}
SIM = ["sim_pt", "sim_eta", "sim_q", "sim_vx", "sim_vy", "sim_vz", "sim_tcIdx"]
TC = ["tc_pt", "tc_eta", "tc_isFake", "tc_isDuplicate", "tc_type"]
out = {}
for a in uproot.open(sys.argv[1])["tree"].iterate(SIM + TC, step_size=50, library="np"):
    for i in range(len(a["sim_pt"])):
        pt, eta = a["sim_pt"][i].astype(float), a["sim_eta"][i].astype(float)
        vxy = np.hypot(a["sim_vx"][i], a["sim_vy"][i])
        den = (a["sim_q"][i] != 0) & (np.abs(eta) < 2.4) & (np.abs(a["sim_vz"][i]) < 30) & (pt > 0.9)
        hit = a["sim_tcIdx"][i] >= 0
        e = {}
        for lo, hi in zip(VXY[:-1], VXY[1:]):
            m = den & (vxy >= lo) & (vxy < hi)
            e[f"den_{lo:g}_{hi:g}"] = int(m.sum())
            e[f"num_{lo:g}_{hi:g}"] = int((m & hit).sum())
        tpt, teta = a["tc_pt"][i].astype(float), a["tc_eta"][i].astype(float)
        sel = (tpt > 0.9) & (np.abs(teta) < 2.4)
        fk = a["tc_isFake"][i].astype(bool)
        dp = (~fk) & a["tc_isDuplicate"][i].astype(bool)
        ty = a["tc_type"][i]
        e["tc"], e["fake"], e["dup"] = int(sel.sum()), int((sel & fk).sum()), int((sel & dp).sum())
        for t, nm in TYPES.items():
            m = sel & (ty == t)
            e[f"tc_{nm}"], e[f"fake_{nm}"] = int(m.sum()), int((m & fk).sum())
        key = f"{len(pt)}:" + ":".join(f"{x:.4f}" for x in pt[:3])
        out[key] = e
json.dump(out, open(sys.argv[2], "w"))
tot = {k: sum(e[k] for e in out.values()) for k in next(iter(out.values()))}
print(len(out), "events")
for lo, hi in zip(VXY[:-1], VXY[1:]):
    n, d = tot[f"num_{lo:g}_{hi:g}"], tot[f"den_{lo:g}_{hi:g}"]
    print(f"eff vxy {lo:g}-{hi:g}: {n}/{d} = {n / max(d, 1):.4f}")
print(f"fake rate {tot['fake'] / max(tot['tc'], 1):.4f}  dup rate {tot['dup'] / max(tot['tc'], 1):.4f}  TCs {tot['tc']}")
