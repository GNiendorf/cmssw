#!/usr/bin/env python3
"""Per-cell operating points BUILT ON TOP of an existing table (angle a6, M10).

The clean question for this angle is not "what table would I build from scratch" (that
throws away the M9 frontier) but "starting from the current winner, how much fake can
per-(branch, length, |eta|) working points remove per unit of true-chain loss?".

So: read the WP dump + a base table (the v1j-equivalent flat table reproduces the M9
winner bit-for-bit, verified), keep only the chains that SURVIVE the base table, and in
each cell raise the threshold to the score quantile that holds a per-cell TRUE-CHAIN
efficiency target among those survivors. Displaced strata get their own (looser =
higher-retention) targets and the cell takes the more permissive of the two, so the
plan's priority-1 vxy curve is protected cell by cell.

Thresholds can only RISE (max with the base), so every generated table is a subset of
the winner's acceptance -- efficiency can only move one way, which makes the A/B
interpretation unambiguous.

Also prints the trade curve (fake kill vs true-chain kill vs per-eta sim retention) over
a target grid, which is the actual deliverable of the angle: it says whether the residual
fake is reachable by threshold geometry at all.
"""

import argparse
import sys

import numpy as np

KILL = 1e9
LEN_NAME = ("<=4", "==5", ">=6")
ETA_NAME = ("|eta|<1.1", "1.1-1.7", ">=1.7")
BR_NAME = ("IP/gate", "exempt/legacy")
DISP_VXY = 1.0


def cell_index(nlay, eta, branch):
    lb = np.where(nlay >= 6, 2, np.where(nlay == 5, 1, 0))
    a = np.abs(eta)
    eb = np.where(a >= 1.7, 2, np.where(a >= 1.1, 1, 0))
    return (branch * 3 + lb) * 3 + eb


def read_table(path):
    thr = np.full(18, np.nan)
    for line in open(path):
        line = line.split("#")[0].split()
        if len(line) != 4:
            continue
        b, l, e, t = int(line[0]), int(line[1]), int(line[2]), float(line[3])
        thr[(b * 3 + l) * 3 + e] = t
    assert not np.isnan(thr).any(), f"incomplete table {path}"
    return thr


def quantile_keep(scores, target, margin):
    """Largest t (drawn from the sample) keeping >= target of `scores`."""
    n = scores.size
    if n == 0:
        return None
    k = int(np.floor(n * (1.0 - target)))
    if k <= 0:
        return float(np.min(scores)) - margin
    return float(np.sort(scores)[k]) - margin


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", required=True)
    ap.add_argument("--base", required=True, help="base table (thresholds may only rise)")
    ap.add_argument("--out", help="write the table for --target (omit to only print the curve)")
    ap.add_argument("--dca-split", type=float, default=0.5)
    ap.add_argument("--t4-floor", type=float, default=0.0)
    ap.add_argument("--target", type=float, default=0.97)
    ap.add_argument("--target-disp", type=float, default=0.99)
    ap.add_argument("--grid", default="", help="comma list of prompt targets for the trade curve")
    ap.add_argument("--margin", type=float, default=1e-4)
    ap.add_argument("--min-cell", type=int, default=100, help="min true survivors to cut a cell")
    ap.add_argument("--no-eta", action="store_true",
                    help="ABLATION: pool the three |eta| bins of each (branch,length) row, i.e. "
                         "a length-only table at the same per-row true-chain retention. Isolates "
                         "what the eta axis itself is worth.")
    args = ap.parse_args()

    d = np.loadtxt(args.dump, comments="#", dtype=np.float64)
    d = d[d[:, 6] == 0]  # pixdrop == 0: only chains that face a threshold
    evt, nlay, eta, dca = d[:, 0].astype(np.int64), d[:, 1].astype(np.int32), d[:, 2], d[:, 3]
    gate, legacy, label, sim, vxy = d[:, 4], d[:, 5], d[:, 7].astype(np.int8), d[:, 8].astype(np.int64), d[:, 9]

    floor = np.where(nlay <= 4, max(args.dca_split, args.t4_floor), args.dca_split)
    branch = (dca >= floor).astype(np.int64)
    cell = cell_index(nlay, eta, branch)
    score = np.where(branch == 0, gate, legacy)

    base = read_table(args.base)
    alive = score >= base[cell]
    print(f"chains: {d.shape[0]} pixdrop-free, {int(alive.sum())} survive the base table "
          f"({int((alive & (label == 1)).sum())} true / {int((alive & (label == 0)).sum())} fake)")

    # per-eta sim retention bookkeeping (the plan 10.5 flatness contingency check)
    ebin = np.where(np.abs(eta) >= 1.7, 2, np.where(np.abs(eta) >= 1.1, 1, 0))
    truth = (label == 1) & (vxy > -900)
    key = evt * 10_000_000 + sim
    usim, sidx = np.unique(key[truth], return_inverse=True)
    sim_eta = np.zeros(usim.size, np.int32)
    sim_eta[sidx] = ebin[truth]
    sim_vxy = np.zeros(usim.size)
    sim_vxy[sidx] = vxy[truth]

    def retention(mask_alive):
        got = np.zeros(usim.size, bool)
        np.logical_or.at(got, sidx, mask_alive[truth])
        return got

    def report(tag, thr):
        a = score >= thr[cell]
        got = retention(a)
        nf, nt = int((label == 0).sum()), int(truth.sum())
        kf = int(((label == 0) & ~a).sum())
        kt = int((truth & ~a).sum())
        line = (f"{tag:>10s} killFake {100.0 * kf / nf:6.2f}%  killTrueChain {100.0 * kt / nt:6.2f}%  "
                f"simRet")
        for e in range(3):
            m = sim_eta == e
            line += f" {ETA_NAME[e]}={got[m].mean():.4f}"
        md = sim_vxy >= 5
        line += f"  simRet(vxy>=5)={got[md].mean():.4f}"
        print(line)
        return got

    base_got = report("base", base)

    grid = [float(x) for x in args.grid.split(",")] if args.grid else [args.target]
    tables = {}
    for tg in grid:
        tgd = min(1.0, tg + (args.target_disp - args.target))
        thr = base.copy()
        groups = ([[k] for k in range(18)] if not args.no_eta
                  else [[r * 3 + e for e in range(3)] for r in range(6)])
        for grp in groups:
            m = alive & np.isin(cell, grp)
            tm = m & truth
            if int(tm.sum()) < args.min_cell:
                continue
            sp = score[tm & (vxy < DISP_VXY)]
            sd = score[tm & (vxy >= DISP_VXY)]
            cands = [q for q in (quantile_keep(sp, tg, args.margin),
                                 quantile_keep(sd, tgd, args.margin)) if q is not None]
            if cands:
                for k in grp:
                    thr[k] = max(thr[k], min(cands))
        tables[tg] = thr
        report(f"t={tg:.3f}", thr)

    if args.out:
        thr = tables[args.target]
        with open(args.out, "w") as f:
            f.write(f"# a6 per-cell WPs tightened on {args.base}: target={args.target} "
                    f"targetDisp={args.target_disp} dcaSplit={args.dca_split}\n")
            f.write("# branch lenBin etaBin threshold\n")
            for k in range(18):
                b, l, e = k // 9, (k // 3) % 3, k % 3
                f.write(f"{b} {l} {e} {thr[k]:.6f}\n")
        print(f"wrote {args.out}")
        print(f"{'branch':14s} {'len':4s} {'eta':10s} {'base':>10s} {'new':>10s}")
        for k in range(18):
            b, l, e = k // 9, (k // 3) % 3, k % 3
            bs = "kill" if base[k] >= KILL else f"{base[k]:.3f}"
            ns = "kill" if thr[k] >= KILL else f"{thr[k]:.3f}"
            print(f"{BR_NAME[b]:14s} {LEN_NAME[l]:4s} {ETA_NAME[e]:10s} {bs:>10s} {ns:>10s}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
