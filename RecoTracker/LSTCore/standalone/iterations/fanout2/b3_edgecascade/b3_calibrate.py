#!/usr/bin/env python3
"""ANGLE-B3 cascade step 4: map the m15_f2 acceptance thresholds from the M12 gate scale
onto the RETRAINED B3 gate scale by EQUAL KILL RATE per -G 6 cell (the m12_calibrate.py
pattern, generalized to the five rules the f2 config actually uses).

Reference side : chains_m12_300evt.root  scored by chain3_mlp_m12.pt  (the -E3 0 world)
Target    side : chains_b3_300evt.root   scored by chain3_mlp_b3.pt   (the -E3 1 world)

Rules (from main.cc -G 6), with the f2 values:
  1 IP     nL<=4 (dca <  0.5): kill iff mX <  -M4  (3.5)
  2 exempt nL<=4 (dca >= 0.5): kill iff mD <  -M4D (-0.75)
  3 IP     nL>=5 (dca <  0.5): kill iff mP < -M5/-M6 (1e9 = always) AND mX < -MRI (0.5)
                               => effectively  kill iff mX < -MRI
  4 exempt nL>=5 (dca >= 0.5): kill iff mD < -MD (1e9 = always) AND mX < -MR (-1.8)
                               => effectively  kill iff mX < -MR
  5 CELL (nL==5, nNodes==2, survivor of 1-4): kill iff mP < -C25 (2.0) AND mD < -C25D (-2.0)

Cells 1-4 are single-margin, so the target threshold is the margin quantile that
reproduces the reference kill RATE inside the same cell. Cell 5 is an AND rule: -C25 is
matched on its own mP quantile (M15 measured it as saturated/insensitive) and -C25D is
then solved so the JOINT kill rate inside the surviving cell matches.
"""
import argparse
import json
import os

import numpy as np

D = os.path.dirname(os.path.abspath(__file__))


def score(pt, normjson, dump):
    import torch
    import uproot

    norm = json.load(open(normjson))
    names = norm["feature_names"]
    mu = np.asarray(norm["mean"], dtype=np.float32)
    sd = np.asarray(norm["std"], dtype=np.float32)
    f = uproot.open(dump)
    cf_names = f["feature_spec"].member("fTitle")[3:].split(",")
    branches = ["dcaXY" if nm[3:] == "dcaXY" else f"cf_{cf_names.index(nm[3:]):02d}" for nm in names]
    tree = f["chains"]
    arr = tree.arrays(sorted(set(branches)), library="np")
    n = tree.num_entries
    X = np.empty((n, len(names)), dtype=np.float32)
    for j, b in enumerate(branches):
        X[:, j] = arr[b]
    del arr
    for c in norm.get("conditioning") or []:
        if c["feature"] not in names:
            continue
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        elif c["op"] == "log10_1p":
            X[:, j] = np.log10(1.0 + X[:, j])
    X = (X - mu) / sd
    blob = torch.load(pt, map_location="cpu", weights_only=False)
    a = blob["arch"]
    model = torch.nn.Sequential(torch.nn.Linear(a[0], a[1]), torch.nn.ReLU(),
                                torch.nn.Linear(a[1], a[2]), torch.nn.ReLU(),
                                torch.nn.Linear(a[2], a[3]))
    model.load_state_dict(blob["state_dict"])
    model.eval()
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model.to(dev)
    out = np.empty((n, a[3]), dtype=np.float32)
    Xt = torch.tensor(X.tolist(), dtype=torch.float32)
    with torch.no_grad():
        for i in range(0, n, 1 << 19):
            out[i:i + (1 << 19)] = np.asarray(model(Xt[i:i + (1 << 19)].to(dev)).cpu().tolist(),
                                              dtype=np.float32)
    m = {"mP": out[:, 1] - out[:, 0], "mD": out[:, 2] - out[:, 0],
         "mX": np.maximum(out[:, 1], out[:, 2]) - out[:, 0]}
    meta = tree.arrays(["label", "simVxy", "nLayers", "dcaXY"], library="np")
    # nNodes is ChainFeatures column 0 (the C25 cell is defined on it).
    meta["nNodes"] = np.rint(tree["cf_00"].array(library="np")).astype(np.int32)
    return m, meta


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--ref-dump", default=f"{D}/chains_m12_300evt.root")
    p.add_argument("--ref-model", default=f"{D}/chain3_mlp_m12.pt")
    p.add_argument("--ref-norm", default=f"{D}/chain3_norm_m12.json")
    p.add_argument("--new-dump", default=f"{D}/chains_b3_300evt.root")
    p.add_argument("--new-model", default=f"{D}/chain3_mlp_b3.pt")
    p.add_argument("--new-norm", default=f"{D}/chain3_norm_b3.json")
    p.add_argument("--dca-split", type=float, default=0.5)
    p.add_argument("--m4", type=float, default=3.5)
    p.add_argument("--m4d", type=float, default=-0.75)
    p.add_argument("--mri", type=float, default=0.5)
    p.add_argument("--mr", type=float, default=-1.800)
    p.add_argument("--c25", type=float, default=2.0)
    p.add_argument("--c25d", type=float, default=-2.0)
    p.add_argument("--out", default=f"{D}/b3_calib.json")
    a = p.parse_args()

    ref, rm = score(a.ref_model, a.ref_norm, a.ref_dump)
    new, nm = score(a.new_model, a.new_norm, a.new_dump)
    print(f"reference {len(rm['label'])} chains ({a.ref_dump.split('/')[-1]});"
          f" target {len(nm['label'])} chains ({a.new_dump.split('/')[-1]})")

    def cells(meta):
        nl, dca, nn = meta["nLayers"], meta["dcaXY"], meta["nNodes"]
        ip = dca < a.dca_split
        return {"IP_T4": ip & (nl <= 4), "EX_T4": (~ip) & (nl <= 4),
                "IP_5p": ip & (nl >= 5), "EX_5p": (~ip) & (nl >= 5),
                "CELL25": (nl == 5) & (nn == 2)}

    rc, nc = cells(rm), cells(nm)
    res = {"rules": {}}
    print(f"\n{'rule':>10} {'margin':>7} {'ref thr':>9} {'ref kill%':>10} {'ref n':>9} "
          f"| {'NEW thr':>9} {'new kill%':>10} {'new n':>9} {'killed-true% ref/new':>22}")
    mapping = {}
    for flag, cell, marg, thr in (("-M4", "IP_T4", "mX", a.m4),
                                  ("-M4D", "EX_T4", "mD", a.m4d),
                                  ("-MRI", "IP_5p", "mX", a.mri),
                                  ("-MR", "EX_5p", "mX", a.mr)):
        rmask, nmask = rc[cell], nc[cell]
        rkill = rmask & (ref[marg] < thr)
        rate = rkill.sum() / max(rmask.sum(), 1)
        q = float(np.quantile(new[marg][nmask], rate))
        nkill = nmask & (new[marg] < q)
        rt = 100 * (rkill & (rm["label"] == 1)).sum() / max(rkill.sum(), 1)
        nt = 100 * (nkill & (nm["label"] == 1)).sum() / max(nkill.sum(), 1)
        print(f"{flag:>10} {marg:>7} {thr:>9.4f} {100*rate:>9.3f}% {int(rmask.sum()):>9} "
              f"| {q:>9.4f} {100*nkill.sum()/max(nmask.sum(),1):>9.3f}% {int(nmask.sum()):>9} "
              f"{rt:>10.2f}% /{nt:>9.2f}%")
        mapping[flag] = round(q, 4)
        res["rules"][flag] = {"cell": cell, "margin": marg, "ref_thr": thr,
                              "ref_kill_rate": float(rate), "new_thr": q,
                              "ref_killed_true_pct": rt, "new_killed_true_pct": nt}

    # ---- cell 5: the C25 AND-rule, measured on the branch survivors only.
    def survivors(meta, m, cellmasks, thr):
        nl, dca = meta["nLayers"], meta["dcaXY"]
        ip = dca < a.dca_split
        killed = np.zeros(len(nl), dtype=bool)
        killed |= cellmasks["IP_T4"] & (m["mX"] < thr["-M4"])
        killed |= cellmasks["EX_T4"] & (m["mD"] < thr["-M4D"])
        killed |= cellmasks["IP_5p"] & (m["mX"] < thr["-MRI"])
        killed |= cellmasks["EX_5p"] & (m["mX"] < thr["-MR"])
        return ~killed

    rsurv = survivors(rm, ref, rc, {"-M4": a.m4, "-M4D": a.m4d, "-MRI": a.mri, "-MR": a.mr})
    nsurv = survivors(nm, new, nc, mapping)
    rcell, ncell = rc["CELL25"] & rsurv, nc["CELL25"] & nsurv
    rkill = rcell & (ref["mP"] < a.c25) & (ref["mD"] < a.c25d)
    rate = rkill.sum() / max(rcell.sum(), 1)
    # -C25 on its own mP quantile inside the surviving cell
    rrateP = (rcell & (ref["mP"] < a.c25)).sum() / max(rcell.sum(), 1)
    c25_new = float(np.quantile(new["mP"][ncell], rrateP))
    # solve -C25D for the matching JOINT rate
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
    res["mapping"] = mapping
    print("\n  -> B3-scale f2-equivalent flags: "
          + " ".join(f"{k} {v}" for k, v in mapping.items()))
    json.dump(res, open(a.out, "w"), indent=1)
    print(f"wrote {a.out}")


if __name__ == "__main__":
    main()
