#!/usr/bin/env python3
"""A02 M18 -- train the seed-vs-chain DEDUP head against the SHARP label.

The M5-M13 head was trained on the -XCD 2 class partition ("does this seed's sim already
appear in the sim set of some delivered TC"). M17 showed that partition is not the
quantity the scoreboard measures, and the deployed head lost efficiency while retiring
FEWER class-B seeds than the criterion it replaced.

This script trains against what the harness actually counts, dumped by `-XCP ... -XCL 1`:

    isDup     retiring this seed's bare row removes one DUPLICATE row
    isFake    retiring it removes one FAKE row
    nSoleCut  accepted, in-denominator sims that lose their ONLY >75% match
              -> retiring costs exactly this much efficiency
    nPartner  matched sims left with exactly one other row (second-order duplicate gain)

LABEL  y = 1 iff (isDup or isFake) and nSoleCut == 0.
The third population -- rows that are the sole match of a PILEUP sim only -- gets y = 0
on purpose: retiring them costs no efficiency but gains nothing either, and since nTC is
the denominator of both rate metrics, retiring them RAISES dup and fake. The old label
could not express that at all.

MODEL SELECTION is on the PREDICTED SCOREBOARD, not on class counts and not on AUC:
per-seed max-pool, walk the threshold, and take the point whose predicted efficiency
matches what the deployed attach-logit criterion spends -- then minimise predicted
duplicate RATE there. Same arithmetic as a02_ref/price18.py.
"""

import argparse
import hashlib
import json
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
BASE = dict(nTC=618793, dup=0.06230, fake=0.05551, eff=0.80992, nSim=62555, nEvt=300)
BASE_XCT = 4.0
PARTNER = 1.0  # second-order duplicate credit; calibrated on the measured -XCT bracket


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--pairs", nargs="+", required=True)
    p.add_argument("--frozen", required=True)
    p.add_argument("--out-prefix", required=True)
    p.add_argument("--hidden", type=int, default=32)
    p.add_argument("--epochs", type=int, default=40)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--nseeds", type=int, default=3)
    p.add_argument("--lr", type=float, default=2e-3)
    p.add_argument("--batch", type=int, default=4096)
    p.add_argument("--val-frac", type=float, default=0.18)
    p.add_argument("--partner", type=float, default=1.0)
    p.add_argument("--cost-weight", type=float, default=20.0,
                   help="sample weight on the rare rows that DO cost efficiency")
    return p.parse_args()


def load(paths, frozen_path):
    frozen = set()
    with open(frozen_path) as fh:
        for line in fh:
            t = line.split()
            if len(t) >= 3:
                frozen.add((int(t[0]), int(t[1]), int(t[2])))
    keys, seeds, feats, lab = [], [], [], []
    hdr = None
    for path in paths:
        with open(path) as fh:
            h = fh.readline().split()[1:]
            if hdr is None:
                hdr = h
            elif h != hdr:
                sys.exit("column mismatch between dumps")
            idx = {n: i for i, n in enumerate(h)}
            for n in ("nSoleCut", "isDup", "isFake", "nPartner"):
                if n not in idx:
                    sys.exit("dump %s lacks %s -- rerun with -XCL 1" % (path, n))
            for line in fh:
                if line.startswith("#") or not line.endswith("\n"):
                    continue
                t = line.split()
                if len(t) != len(h):
                    continue
                try:
                    keys.append((int(t[0]), int(t[1]), int(t[2])))
                    seeds.append(int(t[3]))
                    feats.append([float(v) for v in t[6:6 + NFEAT]])
                    lab.append([int(float(t[idx[n]]))
                                for n in ("nSoleCut", "isDup", "isFake", "nPartner")])
                except ValueError:
                    keys.pop() if len(keys) > len(feats) else None
                    continue
    X = np.asarray(feats, dtype=np.float64)
    L = np.asarray(lab, dtype=np.int64)
    isfrozen = np.asarray([k in frozen for k in keys], dtype=bool)
    evhash = np.asarray(
        [int(hashlib.md5(("%d_%d_%d" % k).encode()).hexdigest()[:8], 16) for k in keys],
        dtype=np.int64)
    sid = np.asarray([hash((k, s)) for k, s in zip(keys, seeds)], dtype=np.int64)
    nev = len(set(keys))
    nfroz = len(set(k for k, f in zip(keys, isfrozen) if f))
    print("loaded %d pairs over %d events (%d frozen-300 test, %d trainable)"
          % (len(X), nev, nfroz, nev - nfroz))
    return X, L, isfrozen, evhash, sid


def seed_level(scores, sid, L):
    order = np.argsort(sid, kind="stable")
    s_sid, s_sc = sid[order], scores[order]
    starts = np.r_[True, s_sid[1:] != s_sid[:-1]]
    idx = np.flatnonzero(starts)
    best = np.maximum.reduceat(s_sc, idx)
    return best, L[order][idx]


def scoreboard(sel, base_sel, Ls, nev):
    """Predicted (eff, dup, fake) for a seed selection, vs the measured FINBASE point."""
    scale = BASE["nEvt"] / float(nev)
    sole, dup, fake, part = Ls[:, 0], Ls[:, 1], Ls[:, 2], Ls[:, 3]
    dupw = dup + np.minimum(part, 1) * PARTNER
    d_tc = -(sel.sum() - base_sel.sum()) * scale
    d_dup = -(dupw[sel].sum() - dupw[base_sel].sum()) * scale
    d_fake = -(fake[sel].sum() - fake[base_sel].sum()) * scale
    d_mat = -(sole[sel].sum() - sole[base_sel].sum()) * scale
    nTC = BASE["nTC"] + d_tc
    return ((BASE["eff"] * BASE["nSim"] + d_mat) / BASE["nSim"],
            (BASE["dup"] * BASE["nTC"] + d_dup) / nTC,
            (BASE["fake"] * BASE["nTC"] + d_fake) / nTC)


def fom(scores, Ls, base_best, nev):
    """Predicted duplicate rate at MATCHED predicted efficiency. Lower is better."""
    base_sel = base_best >= BASE_XCT
    eff0, dup0, _ = scoreboard(base_sel, base_sel, Ls, nev)
    best = (dup0, None)
    for th in np.percentile(scores, np.arange(2, 99, 1.0)):
        sel = scores >= th
        e, d, f = scoreboard(sel, base_sel, Ls, nev)
        if e >= eff0 - 1e-9 and d < best[0]:
            best = (d, float(th))
    return best


def main():
    args = parse_args()
    global PARTNER
    PARTNER = args.partner
    import torch
    import torch.nn as nn

    X, L, isfrozen, evhash, sid = load(args.pairs, args.frozen)
    y = (((L[:, 1] > 0) | (L[:, 2] > 0)) & (L[:, 0] == 0)).astype(np.float32)
    w = np.where(L[:, 0] > 0, args.cost_weight, 1.0).astype(np.float32)
    print("pair label: y=1 %d (%.1f%%) | rows that cost efficiency %d (%.2f%%)"
          % (y.sum(), 100 * y.mean(), (L[:, 0] > 0).sum(), 100 * (L[:, 0] > 0).mean()))

    trainable = ~isfrozen
    isval = trainable & ((evhash % 1000) < int(args.val_frac * 1000))
    istr = trainable & ~isval
    print("split: train %d pairs / %d evts | val %d / %d | test %d / %d"
          % (istr.sum(), len(np.unique(evhash[istr])), isval.sum(),
             len(np.unique(evhash[isval])), isfrozen.sum(), len(np.unique(evhash[isfrozen]))))

    lo = np.percentile(X[istr], 0.1, axis=0)
    hi = np.maximum(np.percentile(X[istr], 99.9, axis=0), lo + 1e-6)
    Xc = np.clip(X, lo, hi)
    mean, std = Xc[istr].mean(axis=0), Xc[istr].std(axis=0)
    std[std < 1e-6] = 1.0
    Z = ((Xc - mean) / std).astype(np.float32)

    def t(a):
        return torch.tensor(a.tolist(), dtype=torch.float32)

    Ztr, ytr, wtr = t(Z[istr]), t(y[istr]).unsqueeze(1), t(w[istr]).unsqueeze(1)
    Zva = t(Z[isval])
    ntr = len(Ztr)
    nev_val = len(np.unique(evhash[isval]))
    va_base, va_L = seed_level(X[isval, 7].astype(np.float32), sid[isval], L[isval])
    b0 = va_base >= BASE_XCT
    e0, d0, f0 = scoreboard(b0, b0, va_L, nev_val)
    print("VAL, the deployed attach criterion priced on these events: eff %.5f dup %.5f fake %.5f"
          % (e0, d0, f0))

    best = (1e9, None, None, None)
    for si in range(args.nseeds):
        sd = args.seed + 1000 * si
        torch.manual_seed(sd)
        model = nn.Sequential(nn.Linear(NFEAT, args.hidden), nn.ReLU(),
                              nn.Linear(args.hidden, args.hidden), nn.ReLU(),
                              nn.Linear(args.hidden, 1))
        opt = torch.optim.Adam(model.parameters(), lr=args.lr)
        lossf = nn.BCEWithLogitsLoss(reduction="none")
        for ep in range(1, args.epochs + 1):
            model.train()
            perm = torch.randperm(ntr)
            for i in range(0, ntr, args.batch):
                j = perm[i:i + args.batch]
                opt.zero_grad()
                lo_ = lossf(model(Ztr[j]), ytr[j])
                (lo_ * wtr[j]).mean().backward()
                opt.step()
            model.eval()
            with torch.no_grad():
                sv = np.asarray(model(Zva).squeeze(1).tolist())
            vb, _ = seed_level(sv, sid[isval], L[isval])
            dup_at, thr = fom(vb, va_L, va_base, nev_val)
            if dup_at < best[0]:
                best = (dup_at, thr, {k: v.detach().clone() for k, v in model.state_dict().items()},
                        (sd, ep))
            if ep % 10 == 0 or ep == args.epochs:
                print("  seed %d epoch %3d  val PRED dup at matched eff: %.5f (thr %s)"
                      % (sd, ep, dup_at, "%.3f" % thr if thr is not None else "-"))

    model = nn.Sequential(nn.Linear(NFEAT, args.hidden), nn.ReLU(),
                          nn.Linear(args.hidden, args.hidden), nn.ReLU(),
                          nn.Linear(args.hidden, 1))
    model.load_state_dict(best[2])
    model.eval()
    print("BEST %s: val PRED dup %.5f at matched eff (baseline %.5f), thr %.3f"
          % (best[3], best[0], d0, best[1]))

    def report(mask, name):
        n = len(np.unique(evhash[mask]))
        with torch.no_grad():
            sv = np.asarray(model(t(Z[mask])).squeeze(1).tolist())
        hb, hL = seed_level(sv, sid[mask], L[mask])
        ab, aL = seed_level(X[mask, 7].astype(np.float32), sid[mask], L[mask])
        base_sel = ab >= BASE_XCT
        print("--- %s (%d events) --- pool %.1f seeds/evt, isDup %.1f isFake %.1f cost %.2f"
              % (name, n, len(hb) / n, hL[:, 1].sum() / n, hL[:, 2].sum() / n,
                 (hL[:, 0] > 0).sum() / n))
        print("   %-8s %-6s %8s %8s %8s  %7s" % ("crit", "thr", "PREDeff", "PREDdup", "PREDfake", "ret/ev"))
        for th in [5.0, 4.5, 4.0, 3.5, 3.0, 2.0]:
            sel = ab >= th
            e, d, f = scoreboard(sel, base_sel, aL, n)
            print("   %-8s %-6.2f %8.5f %8.5f %8.5f  %7.1f" % ("attach", th, e, d, f, sel.sum() / n))
        for q in [98, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 40, 30]:
            th = float(np.percentile(hb, q))
            sel = hb >= th
            e, d, f = scoreboard(sel, base_sel, hL, n)
            print("   %-8s %-6.2f %8.5f %8.5f %8.5f  %7.1f" % ("head", th, e, d, f, sel.sum() / n))
        # The oracle on these events.
        orc = ((hL[:, 1] > 0) | (hL[:, 2] > 0)) & (hL[:, 0] == 0)
        e, d, f = scoreboard(orc, base_sel, hL, n)
        print("   %-8s %-6s %8.5f %8.5f %8.5f  %7.1f" % ("ORACLE", "-", e, d, f, orc.sum() / n))

    report(isval, "VAL")
    if isfrozen.sum():
        report(isfrozen, "TEST = frozen 300 (never trained on)")

    torch.save({"state_dict": model.state_dict(),
                "arch": [NFEAT, args.hidden, args.hidden, 1],
                "feature_names": FEATNAMES,
                "seed": best[3][0]}, args.out_prefix + ".pt")
    with open(args.out_prefix + "_norm.json", "w") as fh:
        json.dump({"feature_names": FEATNAMES, "mean": mean.tolist(), "std": std.tolist(),
                   "clip_lo": lo.tolist(), "clip_hi": hi.tolist()}, fh, indent=1)
    print("wrote %s.pt and %s_norm.json" % (args.out_prefix, args.out_prefix))
    return 0


if __name__ == "__main__":
    sys.exit(main())
