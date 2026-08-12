#!/usr/bin/env python3
"""S1 AFFINE ARM: pin the retrained head's LOGIT SCALE to the shipped one.

Why: everything downstream of the weld consumes the logit as a MAGNITUDE, not as an order --
the chain score is a SUM of member logOdds traded against lambdaLen and it is the K9 greedy
hit-claim ORDER KEY, and chain features 2/3/4/18 are edge-logit aggregates. [S1 21:18] measured
the shipped-vs-arm-C chain score p90 contracting 1.7% and far-displaced (dcaXY >= 12 cm) chains
dropping 1.43% while eligibility and weld ranking were neutral.

An affine map z -> a*z + b cannot change ANY per-edge ordering, so arm C's background gain and
its weld ranking survive by construction; only the scale moves. a, b are fitted on the VAL
events by matching the mean and std of the ELIGIBLE-edge logit distribution (eligible under each
head's own bars), i.e. the population that actually enters chain scores.

The map is baked into the OUTPUT LAYER (wgt_out *= a, bias_out = a*bias_out + b), so there is no
code change at all and no extra constant: it is the same 40->32->32->1 head.

Usage: affine.py --model models/edge_C_cos3e3.pt --table wp_C_strat.json --out models/edge_C_affine.pt
"""
import argparse
import json
import os
import sys

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from train_s1 import CACHE, SHIP_BAR, build_model, event_split, gather, row_mask, scores  # noqa: E402


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--model", required=True)
    p.add_argument("--table", required=True)
    p.add_argument("--out", required=True)
    a = p.parse_args()

    ck = torch.load(a.model, map_location="cpu", weights_only=False)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = build_model(40, 32).to(dev)
    model.load_state_dict({k: v.to(dev) for k, v in ck["state_dict"].items()})

    M = np.load(os.path.join(CACHE, "meta.npz"))
    off = M["evt_off"]
    tr, va, te = event_split(len(off) - 1, ck["args"]["seed"])
    m = row_mask(off, va, int(off[-1]))
    X = np.load(os.path.join(CACHE, "X.npy"), mmap_mode="r")
    Xs = gather(X, off, va, dev)
    Xs.sub_(torch.tensor(ck["mu"], device=dev)).div_(torch.tensor(ck["sd"], device=dev))
    sn = scores(model, Xs).cpu().numpy()
    del Xs
    ss, fam = M["logit"][m], M["type"][m]
    T = np.array(json.load(open(a.table))["table"])
    bar_new = T[fam - 1, M["ptbin"][m].astype(int) * 10 + M["etabin"][m].astype(int)]
    bar_shp = np.where(fam == 1, 0.0, -2.0)
    elS = ss >= bar_shp
    elN = sn >= bar_new
    mS, sS = float(ss[elS].mean()), float(ss[elS].std())
    mN, sN = float(sn[elN].mean()), float(sn[elN].std())
    A = sS / sN
    B = mS - A * mN
    print("eligible logits: shipped mean %.4f std %.4f | arm mean %.4f std %.4f" % (mS, sS, mN, sN))
    print("AFFINE a = %.6f  b = %.6f   (z -> a*z + b)" % (A, B))
    chk = A * sn[elN] + B
    print("after: arm mean %.4f std %.4f  (eligible SET unchanged: %d rows)" %
          (chk.mean(), chk.std(), int(elN.sum())))

    sd = {k: v.clone() for k, v in ck["state_dict"].items()}
    sd["4.weight"] = sd["4.weight"] * A
    sd["4.bias"] = sd["4.bias"] * A + B
    out = dict(ck)
    out["state_dict"] = {k: v.cpu() for k, v in sd.items()}
    out["affine"] = {"a": A, "b": B, "fit": "mean+std of eligible-edge logits, VAL events",
                     "parent": os.path.abspath(a.model)}
    torch.save(out, a.out)
    print("wrote %s (affine baked into the output layer; the head is still 40->32->32->1)" % a.out)


if __name__ == "__main__":
    main()
