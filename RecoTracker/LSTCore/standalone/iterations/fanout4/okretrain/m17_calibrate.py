#!/usr/bin/env python3
"""M17 step 4: map the ctl_noatt (= m15_f2 flag set) acceptance thresholds from the
RESIDENT M12 gate scale onto the RETRAINED M17 gate scale by EQUAL KILL RATE per -G 6
cell -- the b3_calibrate.py pattern, with two M17 changes:

  * both sides are measured on the ctl_noatt STAGING POPULATION (pixPT3 == 0), i.e. the
    chains whose fate the gate actually decides under -RT5 1, not on every welded chain;
  * the reference and target dumps are the SAME file (chains_m17_300evt.root) -- the
    welded population is mode-independent, only the scoring model changes.

Rules (main.cc -G 6) with the f2 values:
  1 IP     nL<=4 (dca <  0.5): kill iff mX <  -M4  (3.5)
  2 exempt nL<=4 (dca >= 0.5): kill iff mD <  -M4D (-0.75)
  3 IP     nL>=5 (dca <  0.5): -M5/-M6 = 1e9 => kill iff mX < -MRI (0.5)
  4 exempt nL>=5 (dca >= 0.5): -MD = 1e9      => kill iff mX < -MR  (-1.8)
  5 CELL   (nL==5, nNodes==2, survivor of 1-4): kill iff mP < -C25 (2.0) AND mD < -C25D (-2.0)
"""
import argparse
import json
import os
import sys

import numpy as np

D = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, D)
from m17_gatelib import score_dump  # noqa: E402


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--dump", default=f"{D}/chains_m17_300evt.root")
    p.add_argument("--ref-model", default=f"{D}/chain3_mlp_m12.pt")
    p.add_argument("--ref-norm", default=f"{D}/chain3_norm_m12.json")
    p.add_argument("--new-model", default=f"{D}/chain3_mlp_m17b.pt")
    p.add_argument("--new-norm", default=f"{D}/chain3_norm_m17b.json")
    p.add_argument("--stage-mode", choices=["all", "ctl_noatt", "hybrid_f2"], default="ctl_noatt")
    p.add_argument("--dca-split", type=float, default=0.5)
    p.add_argument("--m4", type=float, default=3.5)
    p.add_argument("--m4d", type=float, default=-0.75)
    p.add_argument("--mri", type=float, default=0.5)
    p.add_argument("--mr", type=float, default=-1.800)
    p.add_argument("--c25", type=float, default=2.0)
    p.add_argument("--c25d", type=float, default=-2.0)
    p.add_argument("--out", default=f"{D}/m17_calib.json")
    a = p.parse_args()

    ref, meta = score_dump(a.ref_model, a.ref_norm, a.dump)
    new, _ = score_dump(a.new_model, a.new_norm, a.dump)

    keep = np.ones(len(meta["label"]), dtype=bool)
    if a.stage_mode != "all":
        keep &= meta["pixPT3"] == 0
        if a.stage_mode == "hybrid_f2":
            keep &= meta["pixPT5"] == 0
    ref = {k: v[keep] for k, v in ref.items()}
    new = {k: v[keep] for k, v in new.items()}
    meta = {k: v[keep] for k, v in meta.items()}
    print(f"calibration population ({a.stage_mode}): {int(keep.sum())} chains of {len(keep)}")

    nl, dca, nn = meta["nLayers"], meta["dcaXY"], meta["nNodes"]
    ip = dca < a.dca_split
    cells = {"IP_T4": ip & (nl <= 4), "EX_T4": (~ip) & (nl <= 4),
             "IP_5p": ip & (nl >= 5), "EX_5p": (~ip) & (nl >= 5),
             "CELL25": (nl == 5) & (nn == 2)}
    lab = meta["label"] == 1

    res = {"rules": {}}
    mapping = {}
    print(f"\n{'rule':>10} {'margin':>7} {'ref thr':>9} {'ref kill%':>10} {'n cell':>9} "
          f"| {'NEW thr':>9} {'new kill%':>10} {'killed-true% ref/new':>22}")
    for flag, cell, marg, thr in (("-M4", "IP_T4", "mX", a.m4),
                                  ("-M4D", "EX_T4", "mD", a.m4d),
                                  ("-MRI", "IP_5p", "mX", a.mri),
                                  ("-MR", "EX_5p", "mX", a.mr)):
        mask = cells[cell]
        rkill = mask & (ref[marg] < thr)
        rate = rkill.sum() / max(mask.sum(), 1)
        q = float(np.quantile(new[marg][mask], rate))
        nkill = mask & (new[marg] < q)
        rt = 100 * (rkill & lab).sum() / max(rkill.sum(), 1)
        nt = 100 * (nkill & lab).sum() / max(nkill.sum(), 1)
        print(f"{flag:>10} {marg:>7} {thr:>9.4f} {100*rate:>9.3f}% {int(mask.sum()):>9} "
              f"| {q:>9.4f} {100*nkill.sum()/max(mask.sum(),1):>9.3f}% "
              f"{rt:>10.2f}% /{nt:>9.2f}%")
        mapping[flag] = round(q, 4)
        res["rules"][flag] = {"cell": cell, "margin": marg, "ref_thr": thr,
                              "ref_kill_rate": float(rate), "new_thr": q,
                              "ref_killed_true_pct": rt, "new_killed_true_pct": nt}

    def survivors(m, thr):
        killed = np.zeros(len(nl), dtype=bool)
        killed |= cells["IP_T4"] & (m["mX"] < thr["-M4"])
        killed |= cells["EX_T4"] & (m["mD"] < thr["-M4D"])
        killed |= cells["IP_5p"] & (m["mX"] < thr["-MRI"])
        killed |= cells["EX_5p"] & (m["mX"] < thr["-MR"])
        return ~killed

    rsurv = survivors(ref, {"-M4": a.m4, "-M4D": a.m4d, "-MRI": a.mri, "-MR": a.mr})
    nsurv = survivors(new, mapping)
    rcell, ncell = cells["CELL25"] & rsurv, cells["CELL25"] & nsurv
    rkill = rcell & (ref["mP"] < a.c25) & (ref["mD"] < a.c25d)
    rate = rkill.sum() / max(rcell.sum(), 1)
    rrateP = (rcell & (ref["mP"] < a.c25)).sum() / max(rcell.sum(), 1)
    c25_new = float(np.quantile(new["mP"][ncell], rrateP))
    cand = new["mD"][ncell & (new["mP"] < c25_new)]
    c25d_new = float(np.quantile(cand, min(1.0, rate * ncell.sum() / max(len(cand), 1)))) if len(cand) else a.c25d
    nkill = ncell & (new["mP"] < c25_new) & (new["mD"] < c25d_new)
    print(f"\n{'-C25/-C25D':>10} {'mP&mD':>7} {a.c25:>4.2f}/{a.c25d:>5.2f} {100*rate:>9.3f}% "
          f"{int(rcell.sum()):>9} | {c25_new:>4.2f}/{c25d_new:>5.2f} "
          f"{100*nkill.sum()/max(ncell.sum(),1):>9.3f}% {int(ncell.sum()):>9}")
    mapping["-C25"] = round(c25_new, 4)
    mapping["-C25D"] = round(c25d_new, 4)
    res["rules"]["-C25/-C25D"] = {"cell": "CELL25 survivors", "ref_thr": [a.c25, a.c25d],
                                  "ref_kill_rate": float(rate), "new_thr": [c25_new, c25d_new],
                                  "new_kill_rate": float(nkill.sum() / max(ncell.sum(), 1))}

    # Total kill census (the funnel-level check the descent starts from).
    rk = ~rsurv | (rcell & (ref["mP"] < a.c25) & (ref["mD"] < a.c25d))
    nk = ~nsurv | nkill
    print(f"\n  TOTAL kills  ref={int(rk.sum())} ({100*rk.mean():.2f}%, true {100*(rk & lab).sum()/max(rk.sum(),1):.2f}%)"
          f"  new={int(nk.sum())} ({100*nk.mean():.2f}%, true {100*(nk & lab).sum()/max(nk.sum(),1):.2f}%)")
    res["mapping"] = mapping
    res["total_kills"] = {"ref": int(rk.sum()), "new": int(nk.sum())}
    print("\n  -> M17-scale ctl_noatt-equivalent flags: "
          + " ".join(f"{k} {v}" for k, v in mapping.items()))
    json.dump(res, open(a.out, "w"), indent=1)
    print(f"wrote {a.out}")


if __name__ == "__main__":
    main()
