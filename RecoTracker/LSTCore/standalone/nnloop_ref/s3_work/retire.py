#!/usr/bin/env python3
"""S3: the RETIREMENT counts as a function of a uniform offset on the retirement bars, and the
offset that returns each to the SHIPPED head's own count on the same on-policy rows.

WHY a second calibration is needed here and what it is.  lostsim.py measured that the sims S3A1
loses relative to the GM12F baseline are carried, 164 of 350, by BARE pLS rows and 89 more by
stage-B pT3 rows -- i.e. by the two decisions that REMOVE a row rather than deliver one:

  -XC   xcTheta / xcThetaT / xcThetaE   retire a bare quad seed whose attach logit toward SOME
                                        delivered seedless chain TC reaches the |seed eta| bar
  -RPS  rpsThetaChain                   retire a carried bare-pLS row whose seed had a scored pair
                                        above the chain-side retirement bar but lost the contention
        attachThetaT3                   the same on the bare-T3 side (one bar, two jobs)

A fixed-TRUE-PAIR-acceptance bar holds the fraction of GENUINE duplicates retired, which is the
right invariant for the attach LABEL -- but the efficiency denominator is the >75% TC-level hit
match, a DIFFERENT predicate, so a chain that shares a sim with the retired pLS at MD level need not
match that sim as a track candidate.  That gap is exactly what shows up as -59 net pLS sims.  So the
retirement side gets its own count-matched dial, priced here, with the delivery bars left at the
fixed-signal-efficiency point.

Both decisions are per-pLS maxima over a target set followed by a threshold, so the count is
computable offline the same way the delivery count is.
usage: retire.py <labdir> <model.pt> <bars.json> [offsets]
"""
import json
import os
import struct
import sys

import numpy as np

L, MODEL, BARS = sys.argv[1], sys.argv[2], sys.argv[3]
OFFS = [float(x) for x in (sys.argv[4] if len(sys.argv) > 4 else
                           "0,0.2,0.4,0.6,0.8,1.0,1.3,1.6,2.0").split(",")]
SHIP_XC = {0: 4.5, 1: 4.0, 2: 4.2}
SHIP_RPS = 6.084
SHIP_T3 = 6.450

import torch
import torch.nn as nn
blob = torch.load(MODEL, map_location="cpu", weights_only=False)
zmu = np.array(blob["z3_mean"], np.float32); zsd = np.array(blob["z3_std"], np.float32)
n_in, n_hid, n_out = blob["arch"][0], blob["arch"][1], blob["arch"][3]
m = nn.Sequential(nn.Linear(n_in, n_hid), nn.ReLU(), nn.Linear(n_hid, n_hid), nn.ReLU(),
                  nn.Linear(n_hid, n_out))
m.load_state_dict(blob["state_dict"]); m.eval()
dev = torch.device("cuda" if torch.cuda.is_available() else "cpu"); m.to(dev)

X20 = np.load(L + "/X20.npy", mmap_mode="r"); z3 = np.load(L + "/z3.npy", mmap_mode="r")
st = np.load(L + "/st.npy"); peta = np.load(L + "/peta.npy"); lgt = np.load(L + "/lgt.npy")
evt = np.load(L + "/evt.npy")
n = len(st)
P = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(L))), "round3", "pairs.bin")
pls = np.empty(n, np.int64)
pos = 0
size = os.path.getsize(P)
dt = np.dtype([("stage", "<u4"), ("target", "<u4"), ("pls", "<u4"), ("logit", "<f4"), ("x", "<f4", (20,))])
with open(P, "rb") as f:
    while f.tell() < size:
        h = struct.unpack("<10I", f.read(40)); nr = h[5]
        raw = np.frombuffer(f.read(nr * 96), dtype=dt)
        pls[pos:pos + nr] = raw["pls"]; pos += nr
assert pos == n

snew = np.empty(n, np.float32)
B = 1 << 21
with torch.no_grad():
    for i in range(0, n, B):
        a = np.asarray(X20[i:i + B])
        b = (np.asarray(z3[i:i + B], dtype=np.float32) - zmu) / zsd
        x = np.concatenate([a[:, :11], b, a[:, 12:]], axis=1)
        zz = m(torch.tensor(x, dtype=torch.float32).to(dev))
        s = zz[:, 0] if n_out == 1 else (torch.logsumexp(zz[:, 1:3], dim=1) - zz[:, 0])
        snew[i:i + B] = s.cpu().numpy()

bars = json.load(open(BARS))["bars"]
key = evt.astype(np.int64) * (1 << 20) + pls
ae = np.abs(peta)
band = np.where(ae < 1.1, 0, np.where(ae < 1.7, 1, 2))


def per_pls_max(sel, v):
    k = key[sel]
    o = np.argsort(k, kind="stable")
    ks = k[o]
    uk, first = np.unique(ks, return_index=True)
    grp = np.searchsorted(uk, ks)
    out = np.full(len(uk), -1e30, np.float32)
    np.maximum.at(out, grp, v[sel][o])
    return uk, out, band[sel][o][first]


print("%-26s %12s %12s %10s" % ("decision", "shipped", "new", "delta"))
for tag, sel, shipbar, newbar, banded in (
        ("-XC  (chain-kind targets)", (st == 0) | (st == 2), SHIP_XC,
         {0: bars["xcTheta"]["new"], 1: bars["xcThetaT"]["new"], 2: bars["xcThetaE"]["new"]}, True),
        ("-RPS chain side", st == 0, SHIP_RPS, bars["rpsThetaChain"]["new"], False),
        ("-RPS/-AT3 bare-T3 side", st == 1, SHIP_T3, bars["attachThetaT3"]["new"], False)):
    uk, mx_new, bd = per_pls_max(sel, snew)
    _, mx_ship, _ = per_pls_max(sel, lgt)
    if banded:
        rs = sum(int(((bd == b) & (mx_ship >= shipbar[b])).sum()) for b in (0, 1, 2))
    else:
        rs = int((mx_ship >= shipbar).sum())
    print("%s  shipped-retired %d of %d pLS" % (tag, rs, len(uk)))
    for off in OFFS:
        if banded:
            rn = sum(int(((bd == b) & (mx_new >= newbar[b] + off)).sum()) for b in (0, 1, 2))
        else:
            rn = int((mx_new >= newbar + off).sum())
        print("    offset %+.2f -> %8d  (%+.2f%%)" % (off, rn, 100 * (rn / max(rs, 1) - 1)))
