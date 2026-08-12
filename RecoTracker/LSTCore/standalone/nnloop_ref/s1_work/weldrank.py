#!/usr/bin/env python3
"""S1: the ARGMAX-FAITHFUL displaced ranking metric (coordinator's spec, 21:08).

The weld is mutual-best: node n's out-slot and node m's in-slot each take the argmax over their
ELIGIBLE incident edges, and edge (n,m) is welded iff it wins BOTH (ChainWeld.h K6a/K6b, sweep 1).
A threshold protocol pins how many edges are eligible; it says nothing about who wins. So:

  WELD RATE of eligible TRUE edges that face COMPETITION (their inner or outer node has another
  eligible incident edge on the relevant side), reported separately for
      prompt      shared-sim vxy < 1 cm (and pileup-only trues, vxy == -999)
      displaced   shared-sim vxy >= 1 cm
  plus the slot-win rates that decompose it.

Ties: the kernel breaks exact-logit ties with the stable tie word; here the logit alone decides,
which differs only for duplicate feature rows.

Usage: weldrank.py <nevents> <model.pt>:<table.json> [<model.pt>:<table.json> ...]
       (the shipped head with its scalar bars is always the first row)
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


def weld(inner, outer, sc, el, nNodes):
    """sweep-1 mutual best: returns (welded, compet_in, compet_out) boolean arrays over edges."""
    s = np.where(el, sc, -np.inf)
    bo = np.full(nNodes, -np.inf)
    bi = np.full(nNodes, -np.inf)
    np.maximum.at(bo, inner, s)
    np.maximum.at(bi, outer, s)
    winO = el & (s == bo[inner])
    winI = el & (s == bi[outer])
    degO = np.zeros(nNodes, dtype=np.int64)
    degI = np.zeros(nNodes, dtype=np.int64)
    np.add.at(degO, inner[el], 1)
    np.add.at(degI, outer[el], 1)
    compet = (degO[inner] > 1) | (degI[outer] > 1)
    return winO & winI, compet, winO, winI


def main():
    nev = int(sys.argv[1])
    arms = sys.argv[2:]
    M = np.load(os.path.join(CACHE, "meta.npz"))
    off = M["evt_off"]
    ck0 = torch.load(arms[0].split(":")[0], map_location="cpu", weights_only=False)
    tr, va, te = event_split(len(off) - 1, ck0["args"]["seed"])
    te = np.sort(te)[:nev]
    m = row_mask(off, te, int(off[-1]))
    lab, ss, fam = M["label"][m], M["logit"][m], M["type"][m]
    ptb, etb, vxy = M["ptbin"][m], M["etabin"][m], M["simVxy"][m]
    disp = vxy >= 1.0
    prompt = ~disp

    inner = np.empty(len(lab), dtype=np.int64)
    outer = np.empty(len(lab), dtype=np.int64)
    nn = []
    pos = 0
    keep = set(int(x) for x in te)
    for (ievt, nN, nE1, nE2, ei, eo, et, lo) in iter_edges(D + "/edges.bin"):
        if ievt not in keep:
            continue
        k = len(ei)
        inner[pos:pos + k] = ei
        outer[pos:pos + k] = eo
        nn.append(nN)
        pos += k
    assert pos == len(lab)
    goff = np.zeros(len(te) + 1, dtype=np.int64)
    goff[1:] = np.cumsum(nn)
    ev_of_row = np.repeat(np.arange(len(te)), [int(off[e + 1] - off[e]) for e in te])
    inner += goff[ev_of_row]
    outer += goff[ev_of_row]
    nTot = int(goff[-1])

    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    X = np.load(os.path.join(CACHE, "X.npy"), mmap_mode="r")

    print("test events %d, edges %d, nodes %d  (true %.4f, of which displaced %.4f)" %
          (len(te), len(lab), nTot, lab.mean(), disp[lab == 1].mean()))
    print("%-24s %10s %10s %10s %10s %10s" %
          ("arm", "n_elig_D", "weld_D", "n_elig_P", "weld_P", "D/P"))

    rows = [("shipped v3", ss, np.where(fam == 1, 0.0, -2.0))]
    for a in arms:
        mp, tp = a.split(":")
        ck = torch.load(mp, map_location="cpu", weights_only=False)
        model = build_model(40, 32).to(dev)
        model.load_state_dict({k: v.to(dev) for k, v in ck["state_dict"].items()})
        Xs = gather(X, off, te, dev)
        Xs.sub_(torch.tensor(ck["mu"], device=dev)).div_(torch.tensor(ck["sd"], device=dev))
        sn = scores(model, Xs).cpu().numpy()
        del Xs
        torch.cuda.empty_cache()
        T = np.array(json.load(open(tp))["table"])
        rows.append((os.path.basename(mp).replace("edge_", "").replace(".pt", ""), sn,
                     T[fam - 1, ptb.astype(int) * 10 + etb.astype(int)]))

    out = {}
    for tag, sc, br in rows:
        el = sc >= br
        w, comp, winO, winI = weld(inner, outer, sc, el, nTot)
        selD = el & comp & (lab == 1) & disp
        selP = el & comp & (lab == 1) & prompt
        wd = float(w[selD].mean())
        wp = float(w[selP].mean())
        print("%-24s %10d %10.5f %10d %10.5f %10.4f" %
              (tag, int(selD.sum()), wd, int(selP.sum()), wp, wd / max(wp, 1e-9)))
        out[tag] = {"n_elig_disp_contested": int(selD.sum()), "weld_rate_disp": wd,
                    "n_elig_prompt_contested": int(selP.sum()), "weld_rate_prompt": wp,
                    "slotwin_out_disp": float(winO[selD].mean()),
                    "slotwin_in_disp": float(winI[selD].mean())}
    json.dump(out, open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     "weldrank.json"), "w"), indent=1)
    print("\nslot-win decomposition (displaced, contested):")
    for k, v in out.items():
        print("  %-24s out-slot %.5f  in-slot %.5f" % (k, v["slotwin_out_disp"], v["slotwin_in_disp"]))


if __name__ == "__main__":
    main()
