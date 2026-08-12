#!/usr/bin/env python3
"""S2: SIM-TRACK COVERAGE by surviving chains -- a much closer offline proxy to deployed
efficiency than a chain count, and one that is directly comparable ACROSS arms.

For each |dxy| / vxy band, count the DISTINCT accepted sim tracks that have at least one LIVE
chain matched to them (harness rule, frac > 0.75) after the gate.  The denominators are the same
1000 events for every arm, so the numerator alone is the comparison; a displaced sim that no
surviving chain covers cannot be reconstructed by the chain pipeline no matter what K9 does, and a
sim that IS covered may still be lost downstream -- so this is an UPPER BOUND per arm, not a
prediction.  (S1 [21:55] proved offline proxies mispredict; this one is reported as a diagnostic.)

Usage: simcover.py <barfit.json> [<barfit.json> ...]
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from barfit import SHIP, cells  # noqa: E402
from train3 import build_inputs, event_split  # noqa: E402

DXY = [(0, 1), (1, 5), (5, 10), (10, 30)]
VXY = [(0, 1), (1, 5), (5, 10), (10, 30)]


def margins(model, norm_path, X):
    import torch
    blob = torch.load(model, map_location="cpu", weights_only=False)
    nj = json.load(open(norm_path))
    mu = np.array(nj["mean"], dtype=np.float32)
    sd = np.array(nj["std"], dtype=np.float32)
    hid = blob["arch"][1]
    net = torch.nn.Sequential(torch.nn.Linear(X.shape[1], hid), torch.nn.ReLU(),
                              torch.nn.Linear(hid, hid), torch.nn.ReLU(),
                              torch.nn.Linear(hid, 3))
    net.load_state_dict(blob["state_dict"])
    net.eval()
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    net.to(dev)
    Xs = torch.tensor(np.ascontiguousarray((X - mu) / sd))
    Z = np.empty((len(X), 3), dtype=np.float32)
    with torch.no_grad():
        for i in range(0, len(Xs), 1 << 20):
            z = net(Xs[i:i + (1 << 20)].to(dev)).float().cpu()
            Z[i:i + (1 << 20)] = np.asarray(z.tolist(), dtype=np.float32)
    return Z


def kill_of(M, C, far, mP, mD, mX, bars, cT, cD):
    band = C["band"]
    k = np.zeros(len(mP), bool)
    k |= C["ip4"] & ~band & (mX < bars["ip4"])
    k |= C["ip4"] & band & (mX < bars["ip4z"])
    k |= C["ex4"] & ~far & ~band & (mD < bars["ex4"])
    k |= C["ex4"] & ~far & band & (mD < bars["ex4z"])
    k |= C["ip5"] & (mX < bars["ip5"])
    barR = np.where(C["inB"], bars["ex5b"], np.where(band, bars["ex5t"], bars["ex5e"]))
    k |= C["ex5"] & (mX < barR)
    c = C["c25"] & ~k
    k |= c & (mP < cT) & (mD < cD)
    return k


def cover(M, live, band, lo, hi):
    m = live & (M["label"] == 1) & (M["simIdx"] >= 0) & (band >= lo) & (band < hi)
    if not m.any():
        return 0
    key = M["evt"][m].astype(np.int64) * 1000000 + M["simIdx"][m].astype(np.int64)
    return int(np.unique(key).size)


def main():
    for rep_path in sys.argv[1:]:
        rep = json.load(open(rep_path))
        lab = rep["lab"]
        X, names, M = build_inputs(lab)
        tr, va, te = event_split(M["evt"], 42)
        C = cells(M)
        f16 = np.asarray(np.load(os.path.join(lab, "X.npy"), mmap_mode="r")[:, 16])
        far = C["far_dca"] & (f16 <= SHIP["t4FarMaxResid"])
        Z = margins(rep["model"], os.path.join(os.path.dirname(rep["model"]),
                    "chain3_norm_" + os.path.basename(rep["model"])[7:-3] + ".json"), X)
        A, B = rep["affine"]["a"], rep["affine"]["b"]
        nP, nD = A * (Z[:, 1] - Z[:, 0]) + B, A * (Z[:, 2] - Z[:, 0]) + B
        nX = A * (np.maximum(Z[:, 1], Z[:, 2]) - Z[:, 0]) + B
        sP, sD = M["zP"] - M["zF"], M["zD"] - M["zF"]
        sX = np.maximum(M["zP"], M["zD"]) - M["zF"]
        ship_bars = {"ip4": SHIP["m3Theta4"], "ip4z": SHIP["m3Theta4"] + SHIP["zdM4"],
                     "ex4": SHIP["m3Theta4D"], "ex4z": SHIP["m3Theta4D"] + SHIP["zdM4D"],
                     "ip5": SHIP["m3ThetaRI"], "ex5b": SHIP["m3ThetaRB"],
                     "ex5t": SHIP["m3ThetaRT"], "ex5e": SHIP["m3ThetaR"]}
        rows = [("SHIPPED gate", kill_of(M, C, far, sP, sD, sX, ship_bars,
                                        SHIP["c25Theta"], SHIP["c25ThetaD"]))]
        for v, cfgv in rep["chainconfig"].items():
            b = {"ip4": cfgv["m3Theta4"], "ip4z": cfgv["m3Theta4"] + cfgv["zdM4"],
                 "ex4": cfgv["m3Theta4D"], "ex4z": cfgv["m3Theta4D"] + cfgv["zdM4D"],
                 "ip5": cfgv["m3ThetaRI"], "ex5b": cfgv["m3ThetaRB"],
                 "ex5t": cfgv["m3ThetaRT"], "ex5e": cfgv["m3ThetaR"]}
            rows.append((v, kill_of(M, C, far, nP, nD, nX, b,
                                    cfgv["c25Theta"], cfgv["c25ThetaD"])))
        print("\n=== %s  (%s)" % (os.path.basename(rep_path), lab))
        hdr = "%-13s" % "gate"
        hdr += "".join("%12s" % ("|dxy|%d-%d" % b) for b in DXY)
        hdr += "".join("%12s" % ("vxy%d-%d" % b) for b in VXY)
        print(hdr + "%10s" % "liveFake")
        base = None
        for nm, k in rows:
            live = ~k
            vals = [cover(M, live, np.abs(M["dxy"]), lo, hi) for lo, hi in DXY]
            vals += [cover(M, live, M["vxy"], lo, hi) for lo, hi in VXY]
            lf = int((live & (M["label"] != 1)).sum())
            if base is None:
                base = vals + [lf]
                print("%-13s" % nm + "".join("%12d" % v for v in vals) + "%10d" % lf)
            else:
                print("%-13s" % nm
                      + "".join("%12s" % ("%d (%+d)" % (v, v - b)) for v, b in zip(vals, base))
                      + "%10s" % ("%+d" % (lf - base[-1])))


if __name__ == "__main__":
    main()
