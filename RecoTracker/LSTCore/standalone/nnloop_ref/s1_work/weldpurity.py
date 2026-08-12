#!/usr/bin/env python3
"""S1: the metric the WP protocol cannot see -- WELD ARGMAX PURITY.

K6a gives each node ONE out-slot and ONE in-slot and fills them with the argmax over its
ELIGIBLE incident edges (ChainWeld.h chainWeldKey). So what decides physics is not how many
edges clear the bar but WHICH edge wins the slot. This scores exactly that, offline, on the
held-out test events:

  for every node that has at least one TRUE eligible incident edge on a given side,
  did the argmax pick a TRUE edge?   -- split by prompt / displaced (shared-sim vxy >= 1 cm)

Ties are broken by the stable tie word in the real kernel; here the raw logit alone decides,
which differs only for exact-logit ties (~1e4 per event, and they are duplicate feature rows).

Usage: weldpurity.py <model.pt> <table.json> [nevents]
"""
import json
import os
import sys

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from train_s1 import CACHE, build_model, event_split, gather, row_mask, scores  # noqa: E402
from dumpio import iter_edges  # noqa: E402

D = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/nnloop_ref/round1"


def purity(node, lab, disp, score, elig, nNodes):
    """argmax per node over eligible edges; return (n_prompt, pur_prompt, n_disp, pur_disp)."""
    s = np.where(elig, score, -np.inf)
    best = np.full(nNodes, -np.inf)
    np.maximum.at(best, node, s)
    win = (s == best[node]) & np.isfinite(s)
    # a node "wants" a true edge if it has an eligible true one
    hasT = np.zeros(nNodes, dtype=bool)
    np.logical_or.at(hasT, node[elig & (lab == 1)], True)
    hasD = np.zeros(nNodes, dtype=bool)
    np.logical_or.at(hasD, node[elig & (lab == 1) & disp], True)
    gotT = np.zeros(nNodes, dtype=bool)
    np.logical_or.at(gotT, node[win & (lab == 1)], True)
    gotD = np.zeros(nNodes, dtype=bool)
    np.logical_or.at(gotD, node[win & (lab == 1) & disp], True)
    pn = hasT & ~hasD
    return (int(pn.sum()), float(gotT[pn].mean()) if pn.any() else -1.0,
            int(hasD.sum()), float(gotD[hasD].mean()) if hasD.any() else -1.0)


def main():
    mp, tp = sys.argv[1], sys.argv[2]
    nev = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    ck = torch.load(mp, map_location="cpu", weights_only=False)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = build_model(40, 32).to(dev)
    model.load_state_dict({k: v.to(dev) for k, v in ck["state_dict"].items()})
    mu = torch.tensor(ck["mu"], device=dev)
    sd = torch.tensor(ck["sd"], device=dev)
    T = np.array(json.load(open(tp))["table"])

    M = np.load(os.path.join(CACHE, "meta.npz"))
    off = M["evt_off"]
    tr, va, te = event_split(len(off) - 1, ck["args"]["seed"])
    te = np.sort(te)[:nev]
    X = np.load(os.path.join(CACHE, "X.npy"), mmap_mode="r")
    Xs = gather(X, off, te, dev)
    Xs.sub_(mu).div_(sd)
    sn = scores(model, Xs).cpu().numpy()
    del Xs
    m = row_mask(off, te, int(off[-1]))
    lab, ss, fam = M["label"][m], M["logit"][m], M["type"][m]
    ptb, etb, vxy = M["ptbin"][m], M["etabin"][m], M["simVxy"][m]
    bar_new = T[fam - 1, ptb.astype(int) * 10 + etb.astype(int)]
    bar_shp = np.where(fam == 1, 0.0, -2.0)
    disp = vxy >= 1.0

    # inner/outer node index per row, in the same order
    inner = np.empty(len(lab), dtype=np.int64)
    outer = np.empty(len(lab), dtype=np.int64)
    nn = np.empty(len(te), dtype=np.int64)
    pos = 0
    keep = set(int(x) for x in te)
    for iev, (ievt, nN, nE1, nE2, ei, eo, et, lo) in enumerate(iter_edges(D + "/edges.bin")):
        if ievt not in keep:
            continue
        k = len(ei)
        inner[pos:pos + k] = ei
        outer[pos:pos + k] = eo
        nn[list(sorted(keep)).index(ievt)] = nN
        pos += k
    assert pos == len(lab), (pos, len(lab))
    # make node indices globally unique across the concatenated events
    goff = np.zeros(len(te) + 1, dtype=np.int64)
    goff[1:] = np.cumsum(nn)
    ev_of_row = np.repeat(np.arange(len(te)), [int(off[e + 1] - off[e]) for e in te])
    inner_g = inner + goff[ev_of_row]
    outer_g = outer + goff[ev_of_row]
    nTot = int(goff[-1])

    print("events %d, edges %d, nodes %d" % (len(te), len(lab), nTot))
    print("%-26s %10s %10s %10s %10s" % ("arm / slot", "n_prompt", "pur_prompt", "n_disp", "pur_disp"))
    for tag, sc, br in (("shipped v3", ss, bar_shp), (os.path.basename(mp), sn, bar_new)):
        for slot, node in (("out(inner)", inner_g), ("in(outer)", outer_g)):
            el = sc >= br
            a, b, c, d = purity(node, lab, disp, sc, el, nTot)
            print("%-26s %10d %10.5f %10d %10.5f" % (tag + " " + slot, a, b, c, d))


if __name__ == "__main__":
    main()
