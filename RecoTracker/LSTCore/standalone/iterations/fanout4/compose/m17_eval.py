#!/usr/bin/env python3
"""M17 step 2 judge: evaluate any 3-class chain gate on the FROZEN test-60 events,
restricted to a given replacement-mode staging population, so the retrained gate and the
RESIDENT gate are compared on identical rows.
"""
import argparse
import json
import os
import sys

import numpy as np

D = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, D)
from m17_gatelib import score_dump  # noqa: E402


def auc(pos, neg):
    from sklearn.metrics import roc_auc_score
    if len(pos) == 0 or len(neg) == 0:
        return float("nan")
    y = np.concatenate([np.ones(len(pos)), np.zeros(len(neg))])
    return float(roc_auc_score(y, np.concatenate([pos, neg])))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", default=f"{D}/chains_m17_300evt.root")
    ap.add_argument("--models", nargs="+", required=True,
                    help="tag=model.pt=norm.json triples")
    ap.add_argument("--test-evts", default=f"{D}/m12_test60_evts.json")
    ap.add_argument("--stage-mode", choices=["all", "ctl_noatt", "hybrid_f2"], default="ctl_noatt")
    ap.add_argument("--json-out", default="")
    a = ap.parse_args()

    te_evts = set(json.load(open(a.test_evts)))
    rows = {}
    out = {}
    for spec in a.models:
        tag, pt, nj = spec.split("=")
        m, meta = score_dump(pt, nj, a.dump)
        sel = np.isin(meta["evt"], list(te_evts))
        if a.stage_mode != "all":
            sel &= meta["pixPT3"] == 0
            if a.stage_mode == "hybrid_f2":
                sel &= meta["pixPT5"] == 0
        mP, mD, mX = m["mP"][sel], m["mD"][sel], m["mX"][sel]
        lab = meta["label"][sel]
        vxy = meta["simVxy"][sel]
        nL = meta["nLayers"][sel]
        dca = meta["dcaXY"][sel]
        f = lab != 1
        p = (lab == 1) & (vxy < 1.0)
        d = (lab == 1) & (vxy >= 1.0)
        ip5 = (dca < 0.5) & (nL >= 5)
        ex5 = (dca >= 0.5) & (nL >= 5)
        t4 = nL <= 4
        r = {
            "n_rows": int(sel.sum()), "n_fake": int(f.sum()), "n_prompt": int(p.sum()),
            "n_disp": int(d.sum()),
            "mP prompt-vs-fake": auc(mP[p], mP[f]),
            "mD disp-vs-fake": auc(mD[d], mD[f]),
            "mX alltrue-vs-fake": auc(mX[p | d], mX[f]),
            "mD disp(vxy>=5)-vs-fake": auc(mD[d & (vxy >= 5)], mD[f]),
            "nL=4 mX alltrue": auc(mX[t4 & (p | d)], mX[t4 & f]),
            "nL=4 mD disp": auc(mD[t4 & d], mD[t4 & f]),
            "IP5+ mX alltrue": auc(mX[ip5 & (p | d)], mX[ip5 & f]),
            "IP5+ mP prompt": auc(mP[ip5 & p], mP[ip5 & f]),
            "exempt5+ mX alltrue": auc(mX[ex5 & (p | d)], mX[ex5 & f]),
            "exempt5+ mD disp": auc(mD[ex5 & d], mD[ex5 & f]),
            "exempt5+ mP prompt": auc(mP[ex5 & p], mP[ex5 & f]),
        }
        rows[tag] = r
        out[tag] = r

    keys = [k for k in next(iter(rows.values())) if not k.startswith("n_")]
    tags = list(rows)
    print(f"\n=== 3-class gate AUCs, frozen test-60, stage-mode={a.stage_mode} ===")
    r0 = rows[tags[0]]
    print(f"rows={r0['n_rows']} fake={r0['n_fake']} prompt={r0['n_prompt']} disp={r0['n_disp']}")
    hdr = f"{'metric':<26}" + "".join(f"{t:>12}" for t in tags)
    if len(tags) > 1:
        hdr += "".join(f"{'d(' + t + ')':>14}" for t in tags[1:])
    print(hdr)
    print("-" * len(hdr))
    for k in keys:
        line = f"{k:<26}" + "".join(f"{rows[t][k]:>12.5f}" for t in tags)
        if len(tags) > 1:
            line += "".join(f"{rows[t][k] - rows[tags[0]][k]:>+14.5f}" for t in tags[1:])
        print(line)
    if a.json_out:
        json.dump(out, open(a.json_out, "w"), indent=1)


if __name__ == "__main__":
    main()
