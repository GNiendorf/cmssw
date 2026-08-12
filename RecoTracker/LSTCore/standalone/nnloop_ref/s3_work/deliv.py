#!/usr/bin/env python3
"""S3: the per-target DELIVERY COUNT as a function of a uniform offset on the three stage-A
delivery bars, and the offset that returns it to the SHIPPED head's own count on the SAME rows.

Rationale, and why this is a SECOND calibration rather than the primary one.  The shipped bars are
NOT a fixed-signal-efficiency point: interface/ChainConfig.h says in so many words that -a 7.3/7.0/
6.4 were chosen to "hold conversion at the 19-input head's operating point", and that loosening
conversion "is exactly what costs displaced efficiency" because a bare chain welded to a WRONG seed
loses its match.  So a signal-efficiency-matched bar on a BETTER head necessarily raises delivery
(measured: conv_T5 .6836 -> .6980), and the overall-efficiency cost of that is a real effect with a
named mechanism.  This script prices the dial: delivery is a per-TARGET argmax over pLS followed by
a threshold, so the count is computable offline exactly as the kernel computes it, before contention.

usage: deliv.py <labdir> <model.pt> [--offsets a,b,c]
"""
import sys, json
import numpy as np

L = sys.argv[1]
MODEL = sys.argv[2]
SHIP = {0: 7.3, 1: 7.0, 2: 6.4}

import torch, torch.nn as nn
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
import struct
tgt = None
# target index per row, re-read straight from pairs.bin (the label cache does not carry it)
P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/nnloop_ref/round3/pairs.bin"
n = len(st)
tgt = np.empty(n, np.int64)
pos = 0
import os
size = os.path.getsize(P)
with open(P, "rb") as f:
    while f.tell() < size:
        h = struct.unpack("<10I", f.read(40))
        nr = h[5]
        raw = np.frombuffer(f.read(nr * 96), dtype=np.dtype([("stage", "<u4"), ("target", "<u4"),
              ("pls", "<u4"), ("logit", "<f4"), ("x", "<f4", (20,))]))
        tgt[pos:pos + nr] = raw["target"]
        pos += nr
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

sel = st == 0
ae = np.abs(peta[sel]); band = np.where(ae < 1.1, 0, np.where(ae < 1.7, 1, 2))
key = evt[sel].astype(np.int64) * (1 << 20) + tgt[sel]
so = snew[sel]; ss = lgt[sel]
# per-target argmax (the K8b contention winner's logit, before any threshold) and its band
order = np.argsort(key, kind="stable")
key_s = key[order]
uk, first = np.unique(key_s, return_index=True)
grp = np.searchsorted(uk, key_s)
def per_target_max(v):
    out = np.full(len(uk), -1e30, np.float32)
    np.maximum.at(out, grp, v[order])
    return out
bmax_new = per_target_max(so); bmax_ship = per_target_max(ss)
# the band is a SEED property; take the band of the winning pLS
def winner_band(v):
    idx = np.full(len(uk), -1, np.int64)
    vs = v[order]; bs = band[order]
    best = np.full(len(uk), -1e30, np.float32)
    for _ in (0,):
        np.maximum.at(best, grp, vs)
    hit = vs >= best[grp]
    # first winner in stable order
    w = np.full(len(uk), -1, np.int64)
    idxs = np.flatnonzero(hit)
    w[grp[idxs[::-1]]] = idxs[::-1]
    return bs[w]
wb_new = winner_band(so); wb_ship = winner_band(ss)
ship_del = sum(int(((wb_ship == b) & (bmax_ship >= SHIP[b])).sum()) for b in (0, 1, 2))
print("targets %d | SHIPPED delivered (pre-contention) %d" % (len(uk), ship_del))
rows = []
for off in [float(x) for x in (sys.argv[4].split(",") if len(sys.argv) > 4 else
                               "-0.4,-0.2,0,0.2,0.3,0.4,0.5,0.6,0.8,1.0,1.2".split(","))]:
    bars = json.load(open(sys.argv[3]))["bars"]
    nb = {0: bars["attachTheta"]["new"] + off, 1: bars["attachThetaT"]["new"] + off,
          2: bars["attachThetaE"]["new"] + off}
    d = sum(int(((wb_new == b) & (bmax_new >= nb[b])).sum()) for b in (0, 1, 2))
    rows.append((off, d, d / ship_del - 1.0))
    print("offset %+.2f -> delivered %d  (%+.2f%% vs shipped)" % (off, d, 100 * (d / ship_del - 1)))
