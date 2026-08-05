#!/usr/bin/env python3
"""Offline frontier predictor for the A02 dedup head.

The scoreboard cost of the bare-chain arm is LINEAR in the seed-level retirement counts,
and the assembled baseline's own -XCT bracket calibrates both slopes on the frozen 300:

  -XCT 4.5  eff .81037  dup .06675   XCretire  A=108.8  B=4.6  C=4.1
  -XCT 4.0  eff .80992  dup .06230             A=113.9  B=5.8  C=4.2
  -XCT 3.5  eff .80904  dup .05925             A=117.3  B=7.0  C=4.3

  d(dup)/dA = -.00445/5.1 = -.00087 and -.00305/3.4 = -.00090  ->  -0.00088 per A/evt
  d(eff)/dB = -.00045/1.2 = -.00037 and -.00088/1.2 = -.00073  ->  -0.00055 per B/evt
              (the eff slope is the noisier of the two: not every class-B seed carries an
               in-cut sim, so treat predicted efficiency as +-.0004)

So a candidate operating point can be PRICED from the dump alone, and only the two or
three points worth confirming need a 13-minute harness run. Everything printed here is a
PREDICTION; the scoreboard numbers in STATUS.md are measured.

Usage: predict_frontier.py <pairs.txt> [--model m.pt --norm m_norm.json] [--frozen keys]
"""
import argparse
import json
import sys

import numpy as np

NFEAT = 22
DUP_PER_A = -0.00088
EFF_PER_B = -0.00055
BASE_EFF, BASE_DUP = 0.80992, 0.06230


def load(path, frozen_path=None, only_frozen=None):
    frozen = set()
    if frozen_path:
        for line in open(frozen_path):
            t = line.split()
            if len(t) >= 3:
                frozen.add((int(t[0]), int(t[1]), int(t[2])))
    keys, seeds, cls, feats = [], [], [], []
    for line in open(path):
        if line.startswith("#"):
            continue
        t = line.split()
        if len(t) != 6 + NFEAT:
            continue
        k = (int(t[0]), int(t[1]), int(t[2]))
        if only_frozen is True and k not in frozen:
            continue
        if only_frozen is False and k in frozen:
            continue
        keys.append(k)
        seeds.append(int(t[3]))
        cls.append(int(t[5]))
        feats.append([float(v) for v in t[6:]])
    return (np.asarray(feats), np.asarray(cls), keys,
            np.asarray([hash((k, s)) for k, s in zip(keys, seeds)]), len(set(keys)))


def seedmax(score, sid, cls):
    o = np.argsort(sid, kind="stable")
    s_sid, s_sc, s_cl = sid[o], score[o], cls[o]
    idx = np.flatnonzero(np.r_[True, s_sid[1:] != s_sid[:-1]])
    return np.maximum.reduceat(s_sc, idx), s_cl[idx]


def table(best, kls, nev, ths, label, ref):
    print(f"--- {label} ({nev} events) ---")
    print(f"{'thr':>9} {'A/evt':>7} {'B/evt':>7} {'C/evt':>6} {'sel':>7} "
          f"{'dEff':>8} {'dDup':>8} {'predEff':>8} {'predDup':>8}")
    out = []
    for t in ths:
        m = best >= t
        a, b, c = ((kls == 0) & m).sum() / nev, ((kls == 1) & m).sum() / nev, ((kls == 2) & m).sum() / nev
        de = EFF_PER_B * (b - ref[1])
        dd = DUP_PER_A * (a - ref[0])
        print(f"{t:9.3f} {a:7.2f} {b:7.2f} {c:6.2f} {a/max(b,1e-9):7.1f} "
              f"{de:+8.5f} {dd:+8.5f} {BASE_EFF+de:8.5f} {BASE_DUP+dd:8.5f}")
        out.append((t, a, b, c, BASE_EFF + de, BASE_DUP + dd))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pairs")
    ap.add_argument("--model")
    ap.add_argument("--norm")
    ap.add_argument("--frozen")
    ap.add_argument("--only", choices=["frozen", "trainable", "all"], default="all")
    args = ap.parse_args()
    only = {"frozen": True, "trainable": False, "all": None}[args.only]
    X, cls, keys, sid, nev = load(args.pairs, args.frozen, only)
    print(f"{len(X)} pairs, {nev} events, pool per evt: "
          f"A={(cls==0).sum()/nev:.1f} B={(cls==1).sum()/nev:.1f} C={(cls==2).sum()/nev:.1f} (pairs)")

    ab, ak = seedmax(X[:, 7], sid, cls)
    print(f"seed pool per evt: A={(ak==0).sum()/nev:.2f} B={(ak==1).sum()/nev:.2f} "
          f"C={(ak==2).sum()/nev:.2f}  <- the ceiling any criterion can reach")
    ref_rows = table(ab, ak, nev, [6.0, 5.0, 4.5, 4.0, 3.5, 3.0, 2.0, 0.0], "ATTACH LOGIT (the -XCT baseline)",
                     (0.0, 0.0))
    ref = [r for r in ref_rows if abs(r[0] - 4.0) < 1e-9][0]
    ref = (ref[1], ref[2])
    print(f"reference (-XCT 4 on these events): A={ref[0]:.2f} B={ref[1]:.2f} per evt")
    table(ab, ak, nev, [4.5, 4.0, 3.5, 3.0], "ATTACH LOGIT re-priced against that reference", ref)

    if args.model:
        import torch
        blob = torch.load(args.model, map_location="cpu", weights_only=False)
        norm = json.load(open(args.norm))
        lo = np.asarray(norm["clip_lo"]); hi = np.asarray(norm["clip_hi"])
        mu = np.asarray(norm["mean"]); sd = np.asarray(norm["std"])
        Z = ((np.clip(X, lo, hi) - mu) / sd).astype(np.float32)
        import torch.nn as nn
        h = blob["arch"][1]
        m = nn.Sequential(nn.Linear(NFEAT, h), nn.ReLU(), nn.Linear(h, h), nn.ReLU(), nn.Linear(h, 1))
        m.load_state_dict(blob["state_dict"]); m.eval()
        with torch.no_grad():
            s = np.asarray(m(torch.tensor(Z.tolist(), dtype=torch.float32)).squeeze(1).tolist())
        hb, hk = seedmax(s, sid, cls)
        ths = np.unique(np.round(np.percentile(hb, np.arange(2, 100, 2)), 3))
        table(hb, hk, nev, ths, "DEDUP HEAD", ref)
    return 0


if __name__ == "__main__":
    sys.exit(main())
