#!/usr/bin/env python3
"""A02 -- train the purpose-built seed-vs-chain DEDUP head.

Input: the -XCP pair dump (one line per candidate (bare-chain TC, post-deletion bare
seed) pair the ported CrossCleanpLS bare-chain arm can consider), columns

    run lumi evt seed tc cls df00..df21

with cls the -XCD 2 truth class: 0 = A (seed sim-matched, its sim ALREADY has a covering
delivered TC -> retiring is free duplicate removal), 1 = B (sim-matched, nothing else
covers it -> retiring is efficiency lost), 2 = C (no sim match).

LABEL y = 1 for A and C ("retiring this seed costs no efficiency"), 0 for B. That is the
dedup question stated exactly; the attach head answers a different one.

EVENT SPLIT. The frozen-300 keys are TEST-ONLY and are never trained or model-selected
on. The remaining events are split by a hash of the event key into train / val.

MODEL SELECTION is on the OPERATING POINT, not AUC: the deployed decision is a per-seed
OR over that seed's candidate pairs, so val is scored seed-level max-pooled, and the
figure of merit is "class-A seeds retired per event at the class-B budget the current
threshold already spends".
"""

import argparse
import hashlib
import json
import os
import sys

import numpy as np

NFEAT = 22
FEATNAMES = [
    "df_log10SeedPt", "df_absSeedEta", "df_seedPtErrRel", "df_seedEtaErr",
    "df_seedScore", "df_seedDeltaPhi", "df_absSeedHit0Z", "df_attachLogit",
    "df_seedBestChainLogit", "df_seedBestT3Logit", "df_chainNLayers", "df_tcNhitOT",
    "df_chainScore", "df_log10TcPt", "df_absTcEta", "df_chainNNodes",
    "df_famSize", "df_famLiveCand", "df_famRpsBlocked", "df_isFamBestPt",
    "df_nCandTcForSeed", "df_nCandSeedForTc",
]
assert len(FEATNAMES) == NFEAT


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--pairs", required=True)
    p.add_argument("--frozen", required=True, help="frozen300_keys.txt (test-only events)")
    p.add_argument("--out-prefix", required=True)
    p.add_argument("--hidden", type=int, default=24)
    p.add_argument("--epochs", type=int, default=40)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--nseeds", type=int, default=1,
                   help="train this many inits and KEEP the best val operating point")
    p.add_argument("--lr", type=float, default=2e-3)
    p.add_argument("--batch", type=int, default=4096)
    p.add_argument("--val-frac", type=float, default=0.18)
    return p.parse_args()


def load(pairs_path, frozen_path):
    frozen = set()
    with open(frozen_path) as fh:
        for line in fh:
            t = line.split()
            if len(t) >= 3:
                frozen.add((int(t[0]), int(t[1]), int(t[2])))
    keys, seeds, cls, feats = [], [], [], []
    with open(pairs_path) as fh:
        for line in fh:
            if line.startswith("#"):
                continue
            t = line.split()
            if len(t) != 6 + NFEAT or not line.endswith("\n"):
                continue  # tolerate a truncated final line of a still-running dump
            try:
                row = [float(v) for v in t[6:]]
                k = (int(t[0]), int(t[1]), int(t[2]))
                s, c = int(t[3]), int(t[5])
            except ValueError:
                continue
            keys.append(k)
            seeds.append(s)
            cls.append(c)
            feats.append(row)
    X = np.asarray(feats, dtype=np.float64)
    cls = np.asarray(cls, dtype=np.int64)
    isfrozen = np.asarray([k in frozen for k in keys], dtype=bool)
    # A stable per-event split token, independent of file order.
    evhash = np.asarray(
        [int(hashlib.md5(("%d_%d_%d" % k).encode()).hexdigest()[:8], 16) for k in keys],
        dtype=np.int64)
    # Seed identity within an event (for the seed-level max-pool).
    sid = np.asarray([hash((k, s)) for k, s in zip(keys, seeds)], dtype=np.int64)
    nev = len(set(keys))
    nfroz = len(set(k for k, f in zip(keys, isfrozen) if f))
    print(f"loaded {len(X)} pairs over {nev} events ({nfroz} frozen-300, "
          f"{nev - nfroz} trainable); class A/B/C = "
          f"{(cls==0).sum()}/{(cls==1).sum()}/{(cls==2).sum()}")
    return X, cls, isfrozen, evhash, sid, nev, nfroz


def seed_level(scores, sid, cls, nev):
    """Max-pool pair scores to seeds; return (sorted unique seed score, seed class)."""
    order = np.argsort(sid, kind="stable")
    s_sid, s_sc, s_cl = sid[order], scores[order], cls[order]
    starts = np.r_[True, s_sid[1:] != s_sid[:-1]]
    idx = np.flatnonzero(starts)
    ends = np.r_[idx[1:], len(s_sid)]
    best = np.maximum.reduceat(s_sc, idx)
    kls = s_cl[idx]
    assert len(best) == len(idx) == len(ends)
    return best, kls


def frontier(best, kls, nev, thresholds):
    rows = []
    for t in thresholds:
        m = best >= t
        a = int(((kls == 0) & m).sum())
        b = int(((kls == 1) & m).sum())
        c = int(((kls == 2) & m).sum())
        rows.append((t, a / nev, b / nev, c / nev))
    return rows


def a_at_b_budget(best, kls, nev, b_budget):
    """Class-A seeds retired per event at the threshold whose class-B rate is <= budget."""
    order = np.argsort(-best, kind="stable")
    k = kls[order]
    cumB = np.cumsum(k == 1) / nev
    cumA = np.cumsum(k == 0) / nev
    ok = np.flatnonzero(cumB <= b_budget)
    if len(ok) == 0:
        return 0.0, float("inf")
    i = ok[-1]
    return float(cumA[i]), float(best[order][i])


def main():
    args = parse_args()
    import torch
    import torch.nn as nn

    X, cls, isfrozen, evhash, sid, nev, nfroz = load(args.pairs, args.frozen)
    y = ((cls == 0) | (cls == 2)).astype(np.float32)

    trainable = ~isfrozen
    # Per-EVENT val split (never per-pair: pairs of one event are correlated).
    isval = trainable & ((evhash % 1000) < int(args.val_frac * 1000))
    istr = trainable & ~isval
    nev_val = len(set(zip(evhash[isval].tolist(), sid[isval].tolist())))  # placeholder
    nev_val = len(np.unique(evhash[isval]))
    nev_tr = len(np.unique(evhash[istr]))
    nev_te = len(np.unique(evhash[isfrozen]))
    print(f"split: train {istr.sum()} pairs / {nev_tr} evts | val {isval.sum()} / {nev_val}"
          f" | test(frozen300) {isfrozen.sum()} / {nev_te}")

    # Conditioning: clip every feature to the TRAIN set's [0.1, 99.9] percentiles, then
    # standardize. Both are baked into the exported header, so C++ and python agree.
    lo = np.percentile(X[istr], 0.1, axis=0)
    hi = np.percentile(X[istr], 99.9, axis=0)
    hi = np.maximum(hi, lo + 1e-6)
    Xc = np.clip(X, lo, hi)
    mean = Xc[istr].mean(axis=0)
    std = Xc[istr].std(axis=0)
    std[std < 1e-6] = 1.0
    Z = ((Xc - mean) / std).astype(np.float32)

    lossf = nn.BCEWithLogitsLoss()

    # CMSSW's torch build has NO numpy interop: every array crosses the boundary as a
    # python list. Cheap at this size and it keeps the training in the same environment
    # the export script runs in.
    def t(a):
        return torch.tensor(a.tolist(), dtype=torch.float32)

    Ztr = t(Z[istr])
    ytr = t(y[istr]).unsqueeze(1)
    Zva = t(Z[isval])
    ntr = len(Ztr)

    # The class-B budget the current criterion already spends on the VAL events, measured
    # from the attach logit itself (feature 7) at the deployed threshold -XCT 4. This is
    # the apples-to-apples operating point: same efficiency cost, how much more duplicate.
    va_cls, va_sid = cls[isval], sid[isval]
    base_best, base_kls = seed_level(X[isval, 7].astype(np.float32), va_sid, va_cls, nev_val)
    base_rows = frontier(base_best, base_kls, nev_val, [4.0, 3.5, 3.0])
    b_budget = base_rows[0][2]
    print("VAL baseline (attach logit): " + " | ".join(
        f"XCT {t}: A={a:.2f} B={b:.2f} C={c:.2f}/evt" for t, a, b, c in base_rows))

    best_score, best_state, best_ep, best_seed = -1.0, None, -1, -1
    for sd_i in range(args.nseeds):
        sd = args.seed + 1000 * sd_i
        torch.manual_seed(sd)
        np.random.seed(sd)
        model = nn.Sequential(
            nn.Linear(NFEAT, args.hidden), nn.ReLU(),
            nn.Linear(args.hidden, args.hidden), nn.ReLU(),
            nn.Linear(args.hidden, 1))
        opt = torch.optim.Adam(model.parameters(), lr=args.lr)
        for ep in range(1, args.epochs + 1):
            model.train()
            perm = torch.randperm(ntr)
            for i in range(0, ntr, args.batch):
                j = perm[i:i + args.batch]
                opt.zero_grad()
                loss = lossf(model(Ztr[j]), ytr[j])
                loss.backward()
                opt.step()
            model.eval()
            with torch.no_grad():
                sv = np.asarray(model(Zva).squeeze(1).tolist())
            b_, k_ = seed_level(sv, va_sid, va_cls, nev_val)
            a_at, thr = a_at_b_budget(b_, k_, nev_val, b_budget)
            if a_at > best_score:
                best_score, best_ep, best_seed = a_at, ep, sd
                best_state = {k: v.detach().clone() for k, v in model.state_dict().items()}
            if ep % 10 == 0 or ep == args.epochs:
                print(f"  seed {sd} epoch {ep:3d}  val A/evt at B<={b_budget:.2f}: {a_at:.2f} (thr {thr:.3f})")

    model = nn.Sequential(
        nn.Linear(NFEAT, args.hidden), nn.ReLU(),
        nn.Linear(args.hidden, args.hidden), nn.ReLU(),
        nn.Linear(args.hidden, 1))
    model.load_state_dict(best_state)
    model.eval()
    args.seed = best_seed
    print(f"BEST seed {best_seed} epoch {best_ep}: val A/evt = {best_score:.2f} vs "
          f"attach-logit baseline {base_rows[0][1]:.2f} at the same class-B cost {b_budget:.2f}")

    def report(mask, name):
        if mask.sum() == 0:
            print(f"--- {name}: no rows in this dump, skipped ---")
            return None, None, 0
        with torch.no_grad():
            sv = np.asarray(model(t(Z[mask])).squeeze(1).tolist())
        n = len(np.unique(evhash[mask]))
        b_, k_ = seed_level(sv, sid[mask], cls[mask], n)
        ab_, ak_ = seed_level(X[mask, 7].astype(np.float32), sid[mask], cls[mask], n)
        print(f"--- {name} ({n} events) ---")
        print("   attach logit : " + " | ".join(
            f"{t}: A={a:.2f} B={b:.2f}" for t, a, b, c in frontier(ab_, ak_, n, [5.5, 5.0, 4.5, 4.0, 3.5, 3.0, 2.5, 2.0, 1.0, 0.0])))
        ths = np.percentile(b_, [98, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 5])
        print("   dedup head   : " + " | ".join(
            f"{t:.2f}: A={a:.2f} B={b:.2f}" for t, a, b, c in frontier(b_, k_, n, ths)))
        # Ceiling.
        print(f"   pool ceiling : A={(k_==0).sum()/n:.2f} B={(k_==1).sum()/n:.2f} "
              f"C={(k_==2).sum()/n:.2f} seeds/evt")
        return b_, k_, n

    report(isval, "VAL")
    report(isfrozen, "TEST = frozen 300 (never trained on)")

    torch.save({"state_dict": model.state_dict(),
                "arch": [NFEAT, args.hidden, args.hidden, 1],
                "feature_names": FEATNAMES,
                "best_epoch": best_ep,
                "best_val_score": best_score,
                "seed": args.seed}, args.out_prefix + ".pt")
    with open(args.out_prefix + "_norm.json", "w") as fh:
        json.dump({"feature_names": FEATNAMES,
                   "mean": mean.tolist(), "std": std.tolist(),
                   "clip_lo": lo.tolist(), "clip_hi": hi.tolist()}, fh, indent=1)
    print(f"wrote {args.out_prefix}.pt and {args.out_prefix}_norm.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
