#!/usr/bin/env python3
"""M12: map the c5 (-G 5) IP-branch acceptance onto the -G 6 margin scale.

c5 kills IP-compatible chains (dcaXY < -X) whose 2-CLASS gate logit falls below a
per-length threshold (-T4 2 / -T5 1 / -T6 0). -G 6 kills the same cells on the 3-class
MARGIN scale (-M4 on mX for nLayers<=4, -M5/-M6 on mP for nLayers==5 / >=6). The two
scales are unrelated, so the "equivalent" threshold is defined by EQUAL KILL RATE in
each cell, measured on the same chain population the A/B runs over (the 300-event dump).

Also prints, for the exempt (dca >= -X) 5+ branch, the kill rate that each -MD value in
the M12 scan grid implies, plus its true/fake decomposition -- the quantity the scan is
actually trading.
"""
import argparse
import json
import os

import numpy as np

PROTO = os.path.dirname(os.path.abspath(__file__))


def load_model(pt, normjson, dump, drop_none=True):
    """Returns (scores, names) for every row of `dump` under the model in `pt`."""
    import torch
    import uproot

    with open(normjson) as fh:
        norm = json.load(fh)
    names = norm["feature_names"]
    mu = np.asarray(norm["mean"], dtype=np.float32)
    sd = np.asarray(norm["std"], dtype=np.float32)

    f = uproot.open(dump)
    cf_names = f["feature_spec"].member("fTitle")[3:].split(",")
    branches = []
    for nm in names:
        base = nm[3:]
        branches.append("dcaXY" if base == "dcaXY" else f"cf_{cf_names.index(base):02d}")
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

    try:
        blob = torch.load(pt, map_location="cpu")
    except Exception:
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
    Xt = torch.tensor(X)
    with torch.no_grad():
        for i in range(0, n, 1 << 19):
            out[i:i + (1 << 19)] = np.asarray(model(Xt[i:i + (1 << 19)].to(dev)).cpu().tolist(),
                                              dtype=np.float32)
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--dump", default=f"{PROTO}/chains_m12_300evt.root")
    p.add_argument("--gate2", default=f"{PROTO}/chain_mlp_a2.pt")
    p.add_argument("--norm2", default=f"{PROTO}/chain_norm_a2.json")
    p.add_argument("--gate3", default=f"{PROTO}/chain3_mlp_m12.pt")
    p.add_argument("--norm3", default=f"{PROTO}/chain3_norm_m12.json")
    p.add_argument("--dca-split", type=float, default=0.5)
    p.add_argument("--t4", type=float, default=2.0)
    p.add_argument("--t5", type=float, default=1.0)
    p.add_argument("--t6", type=float, default=0.0)
    p.add_argument("--md-grid", type=float, nargs="+",
                   default=[-2.0, -1.574, -1.35, -1.1, -0.85, -0.6])
    p.add_argument("--out", default=f"{PROTO}/m12_calib.json")
    args = p.parse_args()

    import uproot
    tree = uproot.open(args.dump)["chains"]
    meta = tree.arrays(["label", "label_old", "simVxy", "nLayers", "dcaXY"], library="np")
    nl, dca = meta["nLayers"], meta["dcaXY"]
    lab, vxy = meta["label"], meta["simVxy"]

    z2 = load_model(args.gate2, args.norm2, args.dump)[:, 0]
    z3 = load_model(args.gate3, args.norm3, args.dump)
    mP = z3[:, 1] - z3[:, 0]
    mD = z3[:, 2] - z3[:, 0]
    mX = np.maximum(z3[:, 1], z3[:, 2]) - z3[:, 0]
    print(f"{len(nl)} chains; 2-class logit range [{z2.min():.2f},{z2.max():.2f}]")

    ip = dca < args.dca_split
    ex = ~ip
    res = {"dca_split": args.dca_split, "c5_thresholds": {"T4": args.t4, "T5": args.t5, "T6": args.t6}}

    print("\n=== IP branch (dcaXY < %.2f): equal-kill-rate mapping ===" % args.dca_split)
    print(f"  {'cell':>8} {'n':>9} {'c5 thr':>8} {'kill%':>8} {'margin':>7} {'-M equiv':>10} "
          f"{'killed true%':>13} {'c5 killed true%':>16}")
    mapping = {}
    for cell, mask, thr, marg, mname, flag in (
            ("nL<=4", ip & (nl <= 4), args.t4, mX, "mX", "-M4"),
            ("nL==5", ip & (nl == 5), args.t5, mP, "mP", "-M5"),
            ("nL>=6", ip & (nl >= 6), args.t6, mP, "mP", "-M6")):
        n = int(mask.sum())
        killed = mask & (z2 < thr)
        rate = killed.sum() / max(n, 1)
        # margin value with the same kill rate inside the cell
        q = float(np.quantile(marg[mask], rate)) if n else -1e9
        newkill = mask & (marg < q)
        kt = float((killed & (lab == 1)).sum()) / max(int(killed.sum()), 1)
        kt_new = float((newkill & (lab == 1)).sum()) / max(int(newkill.sum()), 1)
        print(f"  {cell:>8} {n:>9d} {thr:>8.2f} {100 * rate:>7.2f}% {mname:>7} {q:>10.4f} "
              f"{100 * kt_new:>12.2f}% {100 * kt:>15.2f}%")
        mapping[flag] = q
        res[cell] = {"n": n, "c5_thr": thr, "kill_rate": rate, "margin": mname,
                     "M_equiv": q, "killed_true_frac_new": kt_new, "killed_true_frac_c5": kt}
    res["mapping"] = mapping
    print("\n  -> -G 6 IP thresholds: " + " ".join(f"{k} {v:.4f}" for k, v in mapping.items()))

    print("\n=== EXEMPT branch (dcaXY >= %.2f), 5+ layers: -MD scan preview ===" % args.dca_split)
    m5 = ex & (nl >= 5)
    n5 = int(m5.sum())
    disp = (lab == 1) & (vxy >= 1) & (vxy > -900)
    print(f"  population {n5} chains: true {int((m5 & (lab == 1)).sum())} "
          f"(displaced {int((m5 & disp).sum())}), fake {int((m5 & (lab == 0)).sum())}")
    print(f"  {'-MD':>8} {'kill%':>8} {'fakes killed':>13} {'trues killed':>13} "
          f"{'disp killed':>12} {'fake purity of kill':>20}")
    scan = {}
    for v in args.md_grid:
        k = m5 & (mD < v)
        nk = int(k.sum())
        nf = int((k & (lab == 0)).sum())
        nt = int((k & (lab == 1)).sum())
        nd = int((k & disp).sum())
        print(f"  {v:>8.3f} {100 * nk / max(n5, 1):>7.2f}% {nf:>13d} {nt:>13d} {nd:>12d} "
              f"{100 * nf / max(nk, 1):>19.2f}%")
        scan[str(v)] = {"kill": nk, "fakes": nf, "trues": nt, "displaced": nd}
    res["md_scan"] = scan

    # Reference: what the c5 -V5 -1.35 gate-scale kill did on this branch.
    k = m5 & (z2 < -1.35)
    print(f"\n  c5 reference (-V5 -1.35 on the 2-class scale): kill {int(k.sum())} "
          f"({100 * k.sum() / max(n5, 1):.2f}%), fakes {int((k & (lab == 0)).sum())}, "
          f"trues {int((k & (lab == 1)).sum())}, displaced {int((k & disp).sum())}")
    res["c5_V5_ref"] = {"kill": int(k.sum()), "fakes": int((k & (lab == 0)).sum()),
                        "trues": int((k & (lab == 1)).sum()), "displaced": int((k & disp).sum())}
    # -MD giving the same kill RATE as c5's -V5 -1.35
    if n5:
        q = float(np.quantile(mD[m5], k.sum() / n5))
        print(f"  equal-kill-rate -MD for c5's -V5 -1.35 = {q:.4f}")
        res["MD_equal_rate_to_c5"] = q

    with open(args.out, "w") as fh:
        json.dump(res, fh, indent=1)
    print(f"\nwrote {args.out}")


if __name__ == "__main__":
    main()
