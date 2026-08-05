#!/usr/bin/env python3
"""Build the featgate AUC comparison table from the per-test-row npz files.

All arms score the SAME frozen test-60 rows (same dump, same seed, same split code), so
the AUCs are directly differenceable row by row.
"""

import argparse
import json
import os

import numpy as np
from sklearn.metrics import roc_auc_score


def auc(s_pos, s_neg):
    if len(s_pos) == 0 or len(s_neg) == 0:
        return None
    y = np.concatenate([np.ones(len(s_pos)), np.zeros(len(s_neg))])
    return float(roc_auc_score(y, np.concatenate([s_pos, s_neg])))


def blocks(z, band_lo, band_hi):
    y3 = z["y3"]
    f_m, p_m, d_m = y3 == 0, y3 == 1, y3 == 2
    aeta = np.abs(z["chainEta"])
    band = (aeta >= band_lo) & (aeta < band_hi)
    outb = (aeta < band_lo) | ((aeta >= band_hi) & (aeta < 100.0))
    mP, mD, mX = z["mP"], z["mD"], z["mX"]
    t_m = p_m | d_m
    out = {}
    for tag, sel in (("all", np.ones(len(y3), bool)), ("band", band), ("outband", outb)):
        out[f"{tag}|mP"] = (auc(mP[p_m & sel], mP[f_m & sel]), int((p_m & sel).sum()), int((f_m & sel).sum()))
        out[f"{tag}|mD"] = (auc(mD[d_m & sel], mD[f_m & sel]), int((d_m & sel).sum()), int((f_m & sel).sum()))
        out[f"{tag}|mX"] = (auc(mX[t_m & sel], mX[f_m & sel]), int((t_m & sel).sum()), int((f_m & sel).sum()))
    # length-sliced band mX (where the gate's T4/5+ branches live)
    nl = z["nLayers"]
    for tag, m in (("band nL=4", band & (nl == 4)), ("band nL>=5", band & (nl >= 5)),
                   ("outband nL=4", outb & (nl == 4)), ("outband nL>=5", outb & (nl >= 5))):
        out[f"{tag}|mX"] = (auc(mX[t_m & m], mX[f_m & m]), int((t_m & m).sum()), int((f_m & m).sum()))
    return out


def fmt(v):
    return "n/a" if v is None else f"{v:.5f}"


def main():
    d = os.path.dirname(os.path.abspath(__file__))
    p = argparse.ArgumentParser()
    p.add_argument("--arms", nargs="+", required=True,
                   help="LABEL=path.npz, first one is the reference (resident)")
    p.add_argument("--band-lo", type=float, default=1.1)
    p.add_argument("--band-hi", type=float, default=1.7)
    p.add_argument("--out-json", default=f"{d}/fg_auc_table.json")
    args = p.parse_args()

    labels, data = [], {}
    for spec in args.arms:
        lab, path = spec.split("=", 1)
        labels.append(lab)
        z = np.load(path)
        data[lab] = blocks(z, args.band_lo, args.band_hi)
        print(f"{lab}: {path} rows={len(z['y3'])}")
    ref = labels[0]
    rows = [k for k in data[ref].keys()]

    hdr = ["metric", "n_pos", "n_neg"] + labels + [f"d({l}-{ref})" for l in labels[1:]]
    lines = ["| " + " | ".join(hdr) + " |",
             "|" + "|".join(["---"] * len(hdr)) + "|"]
    for k in rows:
        a0, npos, nneg = data[ref][k]
        cells = [k.replace("|", " "), str(npos), str(nneg)]
        for l in labels:
            cells.append(fmt(data[l][k][0]))
        for l in labels[1:]:
            v = data[l][k][0]
            cells.append("n/a" if (v is None or a0 is None) else f"{v - a0:+.5f}")
        lines.append("| " + " | ".join(cells) + " |")
    table = "\n".join(lines)
    print()
    print(table)
    with open(args.out_json, "w") as fh:
        json.dump({l: {k: {"auc": v[0], "n_pos": v[1], "n_neg": v[2]} for k, v in data[l].items()}
                   for l in labels}, fh, indent=1)
    with open(os.path.splitext(args.out_json)[0] + ".md", "w") as fh:
        fh.write(table + "\n")


if __name__ == "__main__":
    main()
